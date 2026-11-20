// 答案之书 · M5Stack Cardputer ADV
// 纯离线：答案、字体全部放在固件里，插不插 SD 卡都能用。
// 产物 firmware.bin 直接丢进 Launcher 的安装目录即可从 Launcher 启动。

#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <esp_system.h>

#include "answers.h"
#include "cjk_font.h"
#include "keyboard_adv.h"

// ---------------- 视觉常量 ----------------
static constexpr int W = 240;
static constexpr int H = 135;

static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static const uint16_t C_BG      = rgb(10, 16, 28);
static const uint16_t C_BG2     = rgb(14, 22, 38);
static const uint16_t C_PAPER   = rgb(242, 233, 216);
static const uint16_t C_MUTED   = rgb(122, 134, 153);
static const uint16_t C_GOLD    = rgb(217, 164, 65);
static const uint16_t C_GOLD_D  = rgb(122, 90, 34);
static const uint16_t C_YES     = rgb(74, 222, 128);
static const uint16_t C_NO      = rgb(248, 113, 113);
static const uint16_t C_WAIT    = rgb(251, 191, 36);
static const uint16_t C_MAYBE   = rgb(147, 197, 253);

static uint16_t toneColor(Tone t) {
    switch (t) {
        case TONE_YES:  return C_YES;
        case TONE_NO:   return C_NO;
        case TONE_WAIT: return C_WAIT;
        default:        return C_MAYBE;
    }
}

static const char* toneWord(Tone t) {
    switch (t) {
        case TONE_YES:  return "YES";
        case TONE_NO:   return "NO";
        case TONE_WAIT: return "WAIT";
        default:        return "MAYBE";
    }
}

// ---------------- 全局状态 ----------------
enum Screen { SCR_COVER, SCR_SHUFFLE, SCR_ANSWER, SCR_HISTORY, SCR_SETTINGS };

static M5Canvas canvas(&M5.Display);
static AdvKeyboard keyboard;
static Preferences prefs;

static Screen screen = SCR_COVER;

static bool  soundOn = true;
static uint8_t animLevel = 1;      // 0 快 / 1 中 / 2 慢
static uint8_t brightPct = 70;     // 20..100

struct HistEntry {
    uint16_t ans;
    uint16_t page;
};
static constexpr int HIST_MAX = 8;
static HistEntry history[HIST_MAX];
static int histCount = 0;

static int  curAnswer = 0;
static int  curPage = 1;
static uint32_t shuffleStart = 0;
static uint32_t shuffleDur = 1500;
static uint32_t lastTick = 0;
static int  revealChars = 0;
static int  totalChars = 0;

static int settingsSel = 0;
static constexpr int SETTINGS_N = 4;

static uint32_t lastActivity = 0;
static bool dimmed = false;
static uint32_t bootAt = 0;

// ---------------- 小工具 ----------------
static uint32_t rnd() { return esp_random(); }

static int charCount(const char* s) {
    int n = 0;
    const char* p = s;
    while (*p) {
        uint8_t c = (uint8_t)*p;
        if (c < 0x80) p += 1;
        else if ((c & 0xE0) == 0xC0) p += 2;
        else if ((c & 0xF0) == 0xE0) p += 3;
        else p += 4;
        ++n;
    }
    return n;
}

static void decode(const char* s, uint32_t* out, int maxN, int* outN) {
    int n = 0;
    const char* p = s;
    while (*p && n < maxN) {
        uint8_t c = (uint8_t)*p;
        uint32_t cp = '?';
        int len = 1;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = ((c & 0x1F) << 6) | (p[1] & 0x3F); len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); len = 3; }
        else { cp = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); len = 4; }
        out[n++] = cp;
        p += len;
    }
    *outN = n;
}

static uint32_t randomGlyphLike(uint32_t like, const FontSet& fs) {
    if (like < 0x2E80) {
        static const char pool[] = "abcdefghijklmnopqrstuvwxyz0123456789";
        return (uint32_t)pool[rnd() % (sizeof(pool) - 1)];
    }
    for (int i = 0; i < 6; ++i) {
        uint32_t cp = cf::glyphCodeAt((int)(rnd() % fs.count), fs);
        if (cp >= 0x4E00 && cp <= 0x9FFF) return cp;
    }
    return 0x7684;  // “的”
}

static void beep(float hz, uint32_t ms) {
    if (!soundOn) return;
    M5.Speaker.setVolume(55);  // 用户在设置里重新打开音效后能立刻生效
    M5.Speaker.tone(hz, ms);
}

static int drawAscii(M5Canvas& g, int x, int y, const char* s, uint16_t color,
                     int size = 1) {
    g.setFont(&fonts::Font0);
    g.setTextSize(size);
    g.setTextColor(color);
    g.drawString(s, x, y);
    return g.textWidth(s);
}

static int drawAsciiCentered(M5Canvas& g, int cx, int y, const char* s,
                             uint16_t color, int size = 1) {
    g.setFont(&fonts::Font0);
    g.setTextSize(size);
    int w = g.textWidth(s);
    drawAscii(g, cx - w / 2, y, s, color, size);
    return w;
}

// ---------------- 持久化 ----------------
static void loadPrefs() {
    prefs.begin("abook", false);
    soundOn   = prefs.getBool("sound", true);
    animLevel = prefs.getUChar("anim", 1);
    brightPct = prefs.getUChar("bright", 70);
    if (brightPct < 20 || brightPct > 100) brightPct = 70;
    if (animLevel > 2) animLevel = 1;
}

static void savePrefs() {
    prefs.putBool("sound", soundOn);
    prefs.putUChar("anim", animLevel);
    prefs.putUChar("bright", brightPct);
}

static void applyBrightness() {
    M5.Display.setBrightness((uint8_t)(brightPct * 255 / 100));
}

static uint32_t animDuration() {
    switch (animLevel) {
        case 0: return 900;
        case 2: return 2400;
        default: return 1500;
    }
}

// ---------------- 绘制：封面 ----------------
static void drawCover() {
    canvas.fillScreen(C_BG);

    // 纸面质感的横向纹理
    for (int y = 0; y < H; y += 4) {
        canvas.drawFastHLine(0, y, W, C_BG2);
    }

    // 书封
    canvas.fillRoundRect(28, 8, 184, 119, 9, rgb(24, 33, 50));
    canvas.drawRoundRect(28, 8, 184, 119, 9, C_GOLD_D);
    canvas.drawRoundRect(29, 9, 182, 117, 8, C_GOLD);
    canvas.drawLine(50, 11, 50, 124, C_GOLD_D);
    canvas.drawLine(52, 11, 52, 124, C_GOLD_D);

    const int cx = 132;

    cf::drawCentered(canvas, cx, 26, "答案之书", C_PAPER, cf::font24);
    drawAsciiCentered(canvas, cx, 58, "THE BOOK OF ANSWERS", C_MUTED, 1);

    canvas.drawFastHLine(cx - 34, 74, 68, C_GOLD_D);

    cf::drawCentered(canvas, cx, 82, "默念你的问题", C_MUTED, cf::font16);

    if (((millis() - bootAt) / 550) % 2 == 0) {
        cf::drawCentered(canvas, cx, 104, "按 ENTER 翻开", C_GOLD, cf::font16);
    }

    // 电量（ADC 读取较慢，5 秒缓存一次）
    static int lvl = -1;
    static uint32_t lvlAt = 0;
    if (millis() - lvlAt > 5000) {
        lvlAt = millis();
        lvl = M5.Power.getBatteryLevel();
    }
    if (lvl >= 0 && lvl <= 100) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d%%", lvl);
        drawAscii(canvas, 6, 124, buf, C_MUTED, 1);
    }
}

// ---------------- 绘制：答案 ----------------
static void drawAnswerFrame(const char* answer, uint16_t color, int page,
                            int reveal) {
    canvas.fillScreen(C_BG);
    canvas.drawFastHLine(0, 0, W, C_BG2);

    // 顶栏
    cf::drawText(canvas, 8, 4, "答案之书", C_MUTED, cf::font16);
    char pageBuf[16];
    snprintf(pageBuf, sizeof(pageBuf), "P.%03d", page);
    drawAscii(canvas, W - 8 - 6 * 6, 8, pageBuf, C_MUTED, 1);

    canvas.drawFastHLine(8, 22, W - 16, rgb(40, 52, 72));
    // 答案区的一圈暗金边框，让空屏也有“纸页”的感觉
    canvas.drawRoundRect(8, 28, W - 16, 82, 5, rgb(30, 42, 60));

    // 答案排版
    cf::LineSpan lines[4];
    int n = cf::wrapText(answer, cf::font24, W - 24, lines, 4);

    const int lineH = cf::font24.height + 2;
    int startY = (H - n * lineH) / 2 + 3;
    if (startY < 30) startY = 30;

    int consumed = 0;
    for (int i = 0; i < n; ++i) {
        // 计算该行中心
        int lineW = 0;
        {
            const char* p = answer + lines[i].start;
            const char* end = p + lines[i].len;
            while (p < end) {
                uint8_t c = (uint8_t)*p;
                uint32_t cp = '?';
                int len = 1;
                if (c < 0x80) { cp = c; }
                else if ((c & 0xE0) == 0xC0) { cp = ((c & 0x1F) << 6) | (p[1] & 0x3F); len = 2; }
                else { cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); len = 3; }
                lineW += cf::charAdvance(cp, cf::font24);
                p += len;
            }
        }

        int x = W / 2 - lineW / 2;
        const int y = startY + i * lineH;

        const char* p = answer + lines[i].start;
        const char* end = p + lines[i].len;
        while (p < end) {
            uint8_t c = (uint8_t)*p;
            uint32_t cp = '?';
            int len = 1;
            if (c < 0x80) { cp = c; }
            else if ((c & 0xE0) == 0xC0) { cp = ((c & 0x1F) << 6) | (p[1] & 0x3F); len = 2; }
            else { cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); len = 3; }

            const bool revealed = consumed < reveal;
            const uint32_t shown = revealed ? cp : randomGlyphLike(cp, cf::font24);
            cf::drawGlyph(canvas, shown, x, y, revealed ? color : C_MUTED, cf::font24);
            x += cf::charAdvance(cp, cf::font24);
            p += len;
            ++consumed;
        }
    }

    // 底部提示
    drawAsciiCentered(canvas, W / 2 + 8, 124, "ENTER:ASK   ,/.:HISTORY",
                      rgb(72, 86, 108), 1);

    // 语气词（左下角，和居中提示错开）
    const Answer& a = ANSWERS[curAnswer];
    drawAscii(canvas, 8, 124, toneWord(a.tone), toneColor(a.tone), 1);
}

static void drawAnswer() {
    drawAnswerFrame(ANSWERS[curAnswer].zh, toneColor(ANSWERS[curAnswer].tone),
                    curPage, totalChars);
}

static void drawShuffle(uint32_t now) {
    uint32_t elapsed = now - shuffleStart;
    float p = (float)elapsed / (float)shuffleDur;
    if (p < 0) p = 0;

    // 前 45% 只洗牌，之后从左到右逐个定格
    int reveal = 0;
    if (p > 0.42f) {
        float q = (p - 0.42f) / 0.52f;
        if (q > 1) q = 1;
        reveal = (int)(q * totalChars + 0.5f);
    }

    // 页码也一起跳
    int shownPage = curPage;
    if (reveal < totalChars) {
        shownPage = (int)(rnd() % 999) + 1;
    }

    drawAnswerFrame(ANSWERS[curAnswer].zh, toneColor(ANSWERS[curAnswer].tone),
                    shownPage, reveal);

    if (now - lastTick > 95) {
        lastTick = now;
        beep(1100 + (rnd() % 260), 6);
    }
}

// ---------------- 绘制：历史 ----------------
static void drawHistoryBack() {
    canvas.fillScreen(C_BG);
    cf::drawText(canvas, 8, 6, "翻书记录", C_PAPER, cf::font16);
    drawAscii(canvas, W - 62, 10, "ESC:BACK", C_MUTED, 1);
    canvas.drawFastHLine(8, 28, W - 16, rgb(40, 52, 72));

    if (histCount == 0) {
        cf::drawCentered(canvas, W / 2, 60, "还没有记录", C_MUTED, cf::font16);
    }

    int shown = histCount < 5 ? histCount : 5;
    for (int i = 0; i < shown; ++i) {
        const Answer& a = ANSWERS[history[i].ans];
        char buf[10];
        snprintf(buf, sizeof(buf), "%03d", history[i].page);
        int y = 35 + i * 19;
        drawAscii(canvas, 10, y + 4, buf, C_GOLD, 1);
        cf::drawText(canvas, 36, y, a.zh, toneColor(a.tone), cf::font16);
    }
}

// ---------------- 绘制：设置 ----------------
static void drawSettings() {
    canvas.fillScreen(C_BG);
    cf::drawText(canvas, 8, 6, "设置", C_PAPER, cf::font16);
    drawAscii(canvas, W - 62, 10, "ESC:BACK", C_MUTED, 1);
    canvas.drawFastHLine(8, 28, W - 16, rgb(40, 52, 72));

    static const char* labels[SETTINGS_N] = {"音效", "动画", "亮度", "版本"};
    static char values[SETTINGS_N][16];
    snprintf(values[0], 16, "%s", soundOn ? "ON" : "OFF");
    snprintf(values[1], 16, "%s", animLevel == 0 ? "FAST" : animLevel == 1 ? "MID" : "SLOW");
    snprintf(values[2], 16, "%d%%", brightPct);
    snprintf(values[3], 16, "v1.0.1");

    for (int i = 0; i < SETTINGS_N; ++i) {
        int y = 34 + i * 22;
        if (i == settingsSel) {
            canvas.fillRoundRect(6, y - 2, W - 12, 20, 4, rgb(24, 36, 56));
            canvas.drawRoundRect(6, y - 2, W - 12, 20, 4, C_GOLD_D);
        }
        cf::drawText(canvas, 16, y + 1, labels[i],
                     i == settingsSel ? C_GOLD : C_PAPER, cf::font16);
        int vw = 6 * strlen(values[i]);
        drawAscii(canvas, W - 18 - vw, y + 5, values[i],
                  i == settingsSel ? C_PAPER : C_MUTED, 1);
    }

    drawAsciiCentered(canvas, W / 2, 126, "ENTER:CHANGE", C_MUTED, 1);
}

// ---------------- 流程 ----------------
static void startAsk() {
    curAnswer = (int)(rnd() % ANSWER_COUNT);
    curPage = (int)((curAnswer * 137 + (rnd() % 89) + 7) % 999) + 1;
    totalChars = charCount(ANSWERS[curAnswer].zh);
    revealChars = 0;
    shuffleStart = millis();
    shuffleDur = animDuration();
    lastTick = 0;
    screen = SCR_SHUFFLE;

    // 记录历史（最新的在最前）
    for (int i = HIST_MAX - 1; i > 0; --i) history[i] = history[i - 1];
    history[0].ans = (uint16_t)curAnswer;
    history[0].page = (uint16_t)curPage;
    if (histCount < HIST_MAX) ++histCount;
}

static void finishShuffle() {
    screen = SCR_ANSWER;
    beep(880, 60);
    delay(55);
    beep(1320, 90);
}

static void handleKey(const KeyEvent& ev) {
    lastActivity = millis();
    if (dimmed) {
        dimmed = false;
        applyBrightness();
        return;  // 第一下按键只负责唤醒
    }

    const bool confirm = (ev.kind == KEY_ENTER || ev.kind == KEY_SPACE);

    switch (screen) {
        case SCR_COVER:
            if (confirm) {
                beep(660, 40);
                startAsk();
            } else if (ev.kind == KEY_CHAR && ev.ch == 's') {
                screen = SCR_SETTINGS;
            }
            break;

        case SCR_SHUFFLE:
            // 洗牌过程中允许跳过，直接看结果
            if (confirm) {
                totalChars = charCount(ANSWERS[curAnswer].zh);
                screen = SCR_ANSWER;
            }
            break;

        case SCR_ANSWER:
            if (confirm) {
                beep(660, 40);
                startAsk();
            } else if (ev.kind == KEY_LEFT || ev.kind == KEY_UP) {
                screen = SCR_HISTORY;
            } else if (ev.kind == KEY_RIGHT || ev.kind == KEY_DOWN) {
                screen = SCR_HISTORY;
            } else if (ev.kind == KEY_ESC || ev.kind == KEY_BACKSPACE) {
                screen = SCR_COVER;
            } else if (ev.kind == KEY_CHAR && ev.ch == 's') {
                screen = SCR_SETTINGS;
            }
            break;

        case SCR_HISTORY:
            if (ev.kind == KEY_ESC || ev.kind == KEY_BACKSPACE || confirm) {
                screen = SCR_COVER;
            }
            break;

        case SCR_SETTINGS:
            if (ev.kind == KEY_UP) {
                settingsSel = (settingsSel + SETTINGS_N - 1) % SETTINGS_N;
            } else if (ev.kind == KEY_DOWN) {
                settingsSel = (settingsSel + 1) % SETTINGS_N;
            } else if (ev.kind == KEY_ESC || ev.kind == KEY_BACKSPACE) {
                screen = SCR_COVER;
                savePrefs();
            } else if (confirm || ev.kind == KEY_LEFT || ev.kind == KEY_RIGHT) {
                switch (settingsSel) {
                    case 0:
                        soundOn = !soundOn;
                        beep(990, 30);
                        break;
                    case 1:
                        animLevel = (animLevel + 1) % 3;
                        break;
                    case 2: {
                        int b = brightPct + 20;
                        if (b > 100) b = 20;
                        brightPct = (uint8_t)b;
                        applyBrightness();
                        break;
                    }
                    default:
                        break;
                }
                savePrefs();
            }
            break;
    }
}

// ---------------- Arduino ----------------
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    // Cardputer ADV 的面板在 M5GFX 里带 offset_rotation = 1：
    // 用户层 rotation 0 才是“自然横屏”，传 1 会变成内部 2（内容翻转 180°，
    // 且偏移只对内部 1 正确 → 出现半屏黑）。所以这里必须用 0。
    M5.Display.setRotation(0);
    M5.Display.setColorDepth(16);
    M5.Display.fillScreen(TFT_BLACK);

    loadPrefs();
    applyBrightness();

    M5.Speaker.setVolume(soundOn ? 55 : 0);

    // M5Canvas 默认 _psram=true，会去申请 SPIRAM；ADV 没有 PSRAM，必须先关掉，
    // 否则 createSprite 直接返回 nullptr（表现为开机黑屏）。
    canvas.setPsram(false);
    canvas.setColorDepth(16);
    if (!canvas.createSprite(W, H)) {
        // 正常情况不会走到这里（约需 65KB 堆）。实在分配不到就明确报错，
        // 而不是黑屏让人以为固件坏了。
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_RED);
        M5.Display.setCursor(10, 10);
        M5.Display.println("canvas alloc failed");
        while (true) delay(1000);
    }
    canvas.fillScreen(C_BG);

    keyboard.begin();

    // 真机排障用：串口（USB CDC，115200）能看到板型、旋转、分辨率和键盘状态。
    Serial.printf("[abook] board=%d rot=%d %dx%d kb=%d\n", (int)M5.getBoard(),
                  (int)M5.Display.getRotation(), (int)M5.Display.width(),
                  (int)M5.Display.height(), keyboard.started() ? 1 : 0);

    lastActivity = millis();
    bootAt = millis();
}

void loop() {
    static uint32_t lastDraw = 0;
    static bool dirty = true;

    M5.update();
    keyboard.poll();

    KeyEvent ev;
    while (keyboard.pop(ev)) {
        handleKey(ev);
        dirty = true;
    }

    const uint32_t now = millis();

    // 洗牌推进
    if (screen == SCR_SHUFFLE) {
        if (now - shuffleStart >= shuffleDur) {
            finishShuffle();
            dirty = true;
        } else {
            drawShuffle(now);
            canvas.pushSprite(0, 0);
            delay(16);
            return;
        }
    }

    // 空闲自动变暗：封面 60s，答案页 90s。第一下按键只负责唤醒。
    if (!dimmed && now - lastActivity >
                       (uint32_t)(screen == SCR_COVER ? 60000 : 90000)) {
        dimmed = true;
        M5.Display.setBrightness(18);
        dirty = true;
    }

    // 静态页面没必要 30fps 重绘，省电；封面有闪烁提示，保持高帧率。
    const uint32_t interval = (screen == SCR_COVER) ? 33 : 200;
    if (!dirty && now - lastDraw < interval) {
        delay(10);
        return;
    }

    switch (screen) {
        case SCR_COVER:    drawCover(); break;
        case SCR_ANSWER:   drawAnswer(); break;
        case SCR_HISTORY:  drawHistoryBack(); break;
        case SCR_SETTINGS: drawSettings(); break;
        default: break;
    }

    canvas.pushSprite(0, 0);
    lastDraw = now;
    dirty = false;
}
