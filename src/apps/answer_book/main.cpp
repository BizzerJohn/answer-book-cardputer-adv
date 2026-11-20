// 答案之书 · M5Stack Cardputer ADV
// v1.2：分类求签、摇一摇、追问一次、SD 主题答案包。
// 产物 firmware.bin 直接交给 Launcher 安装。

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_system.h>

#include "answers.h"
#include "cjk_font.h"
#include "keyboard_adv.h"
#include "packs.h"
#include "ui.h"

using ui::canvas;

enum Screen { SCR_COVER, SCR_SHUFFLE, SCR_ANSWER, SCR_HISTORY, SCR_SETTINGS };

static AdvKeyboard keyboard;
static ui::ShakeDetector shake;

static Screen screen = SCR_COVER;

static uint8_t animLevel = 1;     // 0 快 / 1 中 / 2 慢
static uint8_t category = CAT_COUNT - 1;  // 默认“随便”
static int8_t packIndex = -1;     // -1 = 内置

static constexpr int HIST_MAX = 8;
struct HistEntry {
    char zh[48];
    uint16_t page;
    Tone tone;
};
static HistEntry history[HIST_MAX];
static int histCount = 0;

// 当前这一签
static const char* curZh = "";
static const char* curWhy = "";
static Tone curTone = TONE_MAYBE;
static int curPage = 1;
static int curIdx = 0;
static int followState = 0;  // 0 收起 / 1 展开
static bool whyUsed = false;  // 本次抽签是否已经用过追问

static uint32_t shuffleStart = 0;
static uint32_t shuffleDur = 1500;
static uint32_t lastTick = 0;
static int totalChars = 0;

static int settingsSel = 0;
static constexpr int SETTINGS_N = 5;

static uint32_t lastActivity = 0;
static bool dimmed = false;
static uint32_t bootAt = 0;

static uint16_t toneColor(Tone t) {
    switch (t) {
        case TONE_YES:  return ui::C_YES;
        case TONE_NO:   return ui::C_NO;
        case TONE_WAIT: return ui::C_WAIT;
        default:        return ui::C_MAYBE;
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

static uint32_t animDuration() {
    switch (animLevel) {
        case 0: return 900;
        case 2: return 2400;
        default: return 1500;
    }
}

static uint16_t catMask() {
    if (category >= CAT_COUNT - 1) return 0xFFFF;
    return (uint16_t)(1u << category);
}

// ---------------- 抽签 ----------------
static bool pickFromBuiltin(int& outIdx) {
    static uint16_t cand[ANSWER_COUNT];
    int n = 0;
    const uint16_t mask = catMask();
    for (uint16_t i = 0; i < ANSWER_COUNT; ++i) {
        if (ANSWERS[i].cats & mask) cand[n++] = i;
    }
    if (n == 0) {
        for (uint16_t i = 0; i < ANSWER_COUNT; ++i) cand[n++] = i;
    }
    if (n == 0) return false;

    int pick = cand[esp_random() % n];
    // 尽量不要和上一签重复
    if (n > 1 && pick == curIdx) pick = cand[(esp_random() % n)];
    outIdx = pick;
    return true;
}

static bool pickFromPack(int& outIdx) {
    if (packs::count == 0) return false;
    static uint16_t cand[packs::MAX_ENTRIES];
    int n = 0;
    const uint16_t mask = catMask();
    for (int i = 0; i < packs::count; ++i) {
        if (packs::entries[i].cats & mask) cand[n++] = i;
    }
    if (n == 0) {
        for (int i = 0; i < packs::count; ++i) cand[n++] = i;
    }
    int pick = cand[esp_random() % n];
    if (n > 1 && pick == curIdx) pick = cand[(esp_random() % n)];
    outIdx = pick;
    return true;
}

static void startAsk() {
    int idx = 0;
    bool ok = (packIndex >= 0) ? pickFromPack(idx) : pickFromBuiltin(idx);
    if (!ok && !pickFromBuiltin(idx)) return;

    if (packIndex >= 0 && packs::count) {
        curZh = packs::entries[idx].zh;
        curWhy = packs::entries[idx].why;
        curTone = packs::entries[idx].tone;
    } else {
        curZh = ANSWERS[idx].zh;
        curWhy = ANSWERS[idx].why;
        curTone = ANSWERS[idx].tone;
    }
    curIdx = idx;
    curPage = (int)((idx * 137 + (esp_random() % 89) + 7) % 999) + 1;
    totalChars = ui::utf8Count(curZh);
    followState = 0;
    whyUsed = false;

    shuffleStart = millis();
    shuffleDur = animDuration();
    lastTick = 0;
    screen = SCR_SHUFFLE;

    // 历史：保存文本副本，切包后仍可回看
    for (int i = HIST_MAX - 1; i > 0; --i) history[i] = history[i - 1];
    snprintf(history[0].zh, sizeof(history[0].zh), "%s", curZh);
    history[0].page = (uint16_t)curPage;
    history[0].tone = curTone;
    if (histCount < HIST_MAX) ++histCount;
}

static void finishShuffle() {
    screen = SCR_ANSWER;
    ui::beep(880, 60);
    delay(55);
    ui::beep(1320, 90);
}

// ---------------- 绘制 ----------------
static void drawCover() {
    canvas.fillScreen(ui::C_BG);
    for (int y = 0; y < ui::H; y += 4) canvas.drawFastHLine(0, y, ui::W, ui::C_BG2);

    canvas.fillRoundRect(24, 6, 192, 123, 9, ui::rgb(24, 33, 50));
    canvas.drawRoundRect(24, 6, 192, 123, 9, ui::C_GOLD_D);
    canvas.drawRoundRect(25, 7, 190, 121, 8, ui::C_GOLD);
    canvas.drawLine(46, 9, 46, 126, ui::C_GOLD_D);
    canvas.drawLine(48, 9, 48, 126, ui::C_GOLD_D);

    const int cx = 130;

    cf::drawCentered(canvas, cx, 20, "答案之书", ui::C_PAPER, cf::font24);

    // 当前主题
    char catLine[24];
    snprintf(catLine, sizeof(catLine), "主题 · %s", CAT_NAMES[category]);
    cf::drawCentered(canvas, cx, 56, catLine, ui::C_GOLD, cf::font16);

    if (((millis() - bootAt) / 550) % 2 == 0) {
        cf::drawCentered(canvas, cx, 82, "按 ENTER 或摇一摇", ui::C_MUTED, cf::font16);
    }

    ui::drawAsciiCentered(canvas, cx, 108, "1-9 THEME   S SETUP   ESC BACK",
                          ui::rgb(72, 86, 108), 1);

    int lvl = M5.Power.getBatteryLevel();
    if (lvl >= 0 && lvl <= 100) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d%%", lvl);
        ui::drawAscii(canvas, 6, 124, buf, ui::C_MUTED, 1);
    }
}

// 答案页：follow=0 大字；follow=1 上方答案 + 下方追问；follow=2 已用过
static void drawFrame(int reveal) {
    canvas.fillScreen(ui::C_BG);
    canvas.drawFastHLine(0, 0, ui::W, ui::C_BG2);

    cf::drawText(canvas, 8, 4, "答案之书", ui::C_MUTED, cf::font16);
    char pageBuf[16];
    snprintf(pageBuf, sizeof(pageBuf), "P.%03d", curPage);
    ui::drawAscii(canvas, ui::W - 8 - 30, 8, pageBuf, ui::C_MUTED, 1);
    canvas.drawFastHLine(8, 22, ui::W - 16, ui::C_LINE);
    canvas.drawRoundRect(8, 28, ui::W - 16, 82, 5, ui::rgb(30, 42, 60));

    if (followState == 0) {
        cf::LineSpan lines[4];
        int n = cf::wrapText(curZh, cf::font24, ui::W - 24, lines, 4);
        const int lineH = cf::font24.height + 2;
        int startY = (ui::H - n * lineH) / 2 + 3;
        if (startY < 30) startY = 30;

        int consumed = 0;
        for (int i = 0; i < n; ++i) {
            int lineW = 0;
            const char* p = curZh + lines[i].start;
            const char* end = p + lines[i].len;
            while (p < end) {
                uint32_t cp;
                int adv = cf::nextChar(p, cp);
                lineW += cf::charAdvance(cp, cf::font24);
                p += adv;
            }
            int x = ui::W / 2 - lineW / 2;
            const int y = startY + i * lineH;
            p = curZh + lines[i].start;
            while (p < end) {
                uint32_t cp;
                int adv = cf::nextChar(p, cp);
                const bool shown = consumed < reveal;
                cf::drawGlyph(canvas, shown ? cp : cf::randomGlyph(cp, cf::font24),
                              x, y, shown ? toneColor(curTone) : ui::C_MUTED,
                              cf::font24);
                x += cf::charAdvance(cp, cf::font24);
                p += adv;
                ++consumed;
            }
        }
    } else {
        // 答案缩小，追问展开
        cf::drawCentered(canvas, ui::W / 2, 32, curZh, toneColor(curTone), cf::font16);
        const char* whyText =
            (curWhy && curWhy[0]) ? curWhy : "有些问题，本来就没有解释。";
        cf::LineSpan lines[4];
        int n = cf::wrapText(whyText, cf::font16, ui::W - 24, lines, 4);
        const int lineH = cf::font16.height + 2;
        int y = 58;
        for (int i = 0; i < n && y < 104; ++i) {
            cf::drawCenteredN(canvas, ui::W / 2, y, whyText + lines[i].start,
                              lines[i].len, ui::C_MUTED, cf::font16);
            y += lineH;
        }
    }

    // 底部提示
    if (followState == 1) {
        ui::drawAsciiCentered(canvas, ui::W / 2, 124, "SPACE:BACK   ENTER:ASK",
                              ui::rgb(72, 86, 108), 1);
    } else {
        ui::drawAsciiCentered(canvas, ui::W / 2 + 6, 124, "SPACE:WHY   ENTER:ASK",
                              ui::rgb(72, 86, 108), 1);
    }
    ui::drawAscii(canvas, 8, 124, toneWord(curTone), toneColor(curTone), 1);
}

static void drawShuffle(uint32_t now) {
    uint32_t elapsed = now - shuffleStart;
    float p = (float)elapsed / (float)shuffleDur;
    if (p < 0) p = 0;

    int reveal = 0;
    if (p > 0.42f) {
        float q = (p - 0.42f) / 0.52f;
        if (q > 1) q = 1;
        reveal = (int)(q * totalChars + 0.5f);
    }
    int shownPage = (reveal < totalChars) ? (int)(esp_random() % 999) + 1 : curPage;
    drawFrame(reveal);
    // 洗牌时页码也乱跳，需要重画一次页码
    if (reveal < totalChars) {
        canvas.fillRect(ui::W - 44, 4, 40, 14, ui::C_BG);
        char buf[16];
        snprintf(buf, sizeof(buf), "P.%03d", shownPage);
        ui::drawAscii(canvas, ui::W - 8 - 30, 8, buf, ui::C_MUTED, 1);
    }

    if (now - lastTick > 95) {
        lastTick = now;
        ui::beep(1100 + (esp_random() % 260), 6);
    }
}

static void drawHistory() {
    canvas.fillScreen(ui::C_BG);
    cf::drawText(canvas, 8, 6, "翻书记录", ui::C_PAPER, cf::font16);
    ui::drawAscii(canvas, ui::W - 62, 10, "ESC:BACK", ui::C_MUTED, 1);
    canvas.drawFastHLine(8, 28, ui::W - 16, ui::C_LINE);

    if (histCount == 0) {
        cf::drawCentered(canvas, ui::W / 2, 60, "还没有记录", ui::C_MUTED,
                         cf::font16);
    }
    int shown = histCount < 5 ? histCount : 5;
    for (int i = 0; i < shown; ++i) {
        int y = 34 + i * 19;
        char buf[8];
        snprintf(buf, sizeof(buf), "%03d", history[i].page);
        ui::drawAscii(canvas, 10, y + 4, buf, ui::C_GOLD, 1);
        cf::drawText(canvas, 36, y, history[i].zh, toneColor(history[i].tone),
                     cf::font16);
    }
}

static void drawSettings() {
    canvas.fillScreen(ui::C_BG);
    cf::drawText(canvas, 8, 6, "设置", ui::C_PAPER, cf::font16);
    ui::drawAscii(canvas, ui::W - 62, 10, "ESC:BACK", ui::C_MUTED, 1);
    canvas.drawFastHLine(8, 28, ui::W - 16, ui::C_LINE);

    static const char* labels[SETTINGS_N] = {"音效", "动画", "亮度", "答案包",
                                             "版本"};
    char values[SETTINGS_N][16];
    snprintf(values[0], 16, "%s", ui::soundOn ? "ON" : "OFF");
    snprintf(values[1], 16, "%s", animLevel == 0 ? "FAST" : animLevel == 1 ? "MID" : "SLOW");
    snprintf(values[2], 16, "%d%%", ui::brightPct);
    snprintf(values[3], 16, "%s", packIndex >= 0 ? packs::name(packIndex) : "BUILT-IN");
    snprintf(values[4], 16, "v1.2.1");

    for (int i = 0; i < SETTINGS_N; ++i) {
        int y = 32 + i * 19;
        if (i == settingsSel) {
            canvas.fillRoundRect(6, y - 2, ui::W - 12, 18, 4, ui::rgb(24, 36, 56));
            canvas.drawRoundRect(6, y - 2, ui::W - 12, 18, 4, ui::C_GOLD_D);
        }
        cf::drawText(canvas, 16, y, labels[i],
                     i == settingsSel ? ui::C_GOLD : ui::C_PAPER, cf::font16);
        const uint16_t vcol = i == settingsSel ? ui::C_PAPER : ui::C_MUTED;
        if (i == 3) {
            // 答案包名字可能是中文，必须用中文字库画；超出宽度就截断。
            char shown[20];
            snprintf(shown, sizeof(shown), "%s",
                     packIndex >= 0 ? packs::name(packIndex) : "BUILT-IN");
            int maxW = 96;
            int tw = cf::textWidth(shown, cf::font16);
            while (tw > maxW && strlen(shown) > 3) {
                shown[strlen(shown) - 1] = 0;
                while (strlen(shown) > 0 &&
                       ((uint8_t)shown[strlen(shown) - 1] & 0xC0) == 0x80)
                    shown[strlen(shown) - 1] = 0;
                tw = cf::textWidth(shown, cf::font16);
            }
            cf::drawText(canvas, ui::W - 16 - tw, y, shown, vcol, cf::font16);
        } else {
            int vw = 6 * strlen(values[i]);
            ui::drawAscii(canvas, ui::W - 16 - vw, y + 4, values[i], vcol, 1);
        }
    }
    ui::drawAsciiCentered(canvas, ui::W / 2, 124, "ENTER:CHANGE", ui::C_MUTED, 1);
}

// ---------------- 输入 ----------------
static void changeCategory(int idx) {
    if (idx < 0 || idx >= CAT_COUNT) return;
    category = (uint8_t)idx;
    ui::prefs.putUChar("cat", category);
    ui::beep(760, 30);
}

static void cyclePack() {
    int next = packIndex + 1;
    if (next >= packs::listCount) next = -1;

    if (next < 0) {
        packIndex = -1;
        ui::prefs.putChar("pack", -1);
        ui::prefs.remove("packname");
        return;
    }
    if (packs::load(next)) {
        packIndex = (int8_t)next;
        ui::prefs.putChar("pack", packIndex);
    } else {
        ui::beep(300, 120);
    }
}

static void handleKey(const KeyEvent& ev) {
    lastActivity = millis();
    if (dimmed) {
        dimmed = false;
        ui::applyBrightness();
        return;
    }

    const bool confirm = (ev.kind == KEY_ENTER);

    switch (screen) {
        case SCR_COVER:
            if (confirm) {
                ui::beep(660, 40);
                startAsk();
            } else if (ev.kind == KEY_SPACE) {
                ui::beep(660, 40);
                startAsk();
            } else if (ev.kind == KEY_CHAR && ev.ch >= '1' && ev.ch <= '9') {
                changeCategory(ev.ch - '1');
            } else if (ev.kind == KEY_CHAR && ev.ch == 's') {
                settingsSel = 0;
                screen = SCR_SETTINGS;
            } else if (ev.kind == KEY_UP || ev.kind == KEY_DOWN) {
                int c = (int)category + (ev.kind == KEY_UP ? -1 : 1);
                if (c < 0) c = CAT_COUNT - 1;
                if (c >= CAT_COUNT) c = 0;
                changeCategory(c);
            }
            break;

        case SCR_SHUFFLE:
            if (confirm || ev.kind == KEY_SPACE) {
                screen = SCR_ANSWER;
            }
            break;

        case SCR_ANSWER:
            if (ev.kind == KEY_SPACE) {
                // 追问内容每签固定；允许反复收起/展开，不会换一条新解释。
                if (followState == 1) {
                    followState = 0;
                } else {
                    followState = 1;
                    if (!whyUsed) {
                        whyUsed = true;
                        ui::beep(990, 40);
                    }
                }
            } else if (confirm) {
                ui::beep(660, 40);
                startAsk();
            } else if (ev.kind == KEY_LEFT || ev.kind == KEY_RIGHT ||
                       ev.kind == KEY_UP || ev.kind == KEY_DOWN) {
                screen = SCR_HISTORY;
            } else if (ev.kind == KEY_ESC || ev.kind == KEY_BACKSPACE) {
                screen = SCR_COVER;
            } else if (ev.kind == KEY_CHAR && ev.ch == 's') {
                settingsSel = 0;
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
                ui::saveSoundBright();
                ui::prefs.putUChar("anim", animLevel);
            } else if (confirm || ev.kind == KEY_LEFT || ev.kind == KEY_RIGHT) {
                switch (settingsSel) {
                    case 0:
                        ui::soundOn = !ui::soundOn;
                        ui::beep(990, 30);
                        break;
                    case 1:
                        animLevel = (animLevel + 1) % 3;
                        break;
                    case 2: {
                        int b = ui::brightPct + 20;
                        if (b > 100) b = 20;
                        ui::brightPct = (uint8_t)b;
                        ui::applyBrightness();
                        break;
                    }
                    case 3:
                        cyclePack();
                        break;
                    default:
                        break;
                }
                ui::saveSoundBright();
                ui::prefs.putUChar("anim", animLevel);
            }
            break;
    }
}

// ---------------- Arduino ----------------
void setup() {
    if (!ui::begin("abook")) return;
    M5.Display.setRotation(0);
    keyboard.begin();

    animLevel = ui::prefs.getUChar("anim", 1);
    category = ui::prefs.getUChar("cat", CAT_COUNT - 1);
    if (category >= CAT_COUNT) category = CAT_COUNT - 1;

    packs::scan();
    int saved = ui::prefs.getChar("pack", -1);
    if (saved >= 0 && saved < packs::listCount && packs::load(saved)) {
        packIndex = (int8_t)saved;
    } else {
        packIndex = -1;
    }

    Serial.printf("[abook] rot=%d %dx%d kb=%d packs=%d\n",
                  (int)M5.Display.getRotation(), (int)M5.Display.width(),
                  (int)M5.Display.height(), keyboard.started() ? 1 : 0,
                  packs::listCount);

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

    // 摇一摇：封面和答案页都能翻
    if (shake.update()) {
        Serial.println("[abook] shake");
        if (screen == SCR_COVER || screen == SCR_ANSWER) {
            ui::beep(620, 40);
            startAsk();
            dirty = true;
        }
    }

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

    if (!dimmed && now - lastActivity >
                       (uint32_t)(screen == SCR_COVER ? 60000 : 90000)) {
        dimmed = true;
        M5.Display.setBrightness(18);
        dirty = true;
    }

    const uint32_t interval = (screen == SCR_COVER) ? 33 : 200;
    if (!dirty && now - lastDraw < interval) {
        delay(10);
        return;
    }

    switch (screen) {
        case SCR_COVER:    drawCover(); break;
        case SCR_ANSWER:   drawFrame(totalChars); break;
        case SCR_HISTORY:  drawHistory(); break;
        case SCR_SETTINGS: drawSettings(); break;
        default: break;
    }

    canvas.pushSprite(0, 0);
    lastDraw = now;
    dirty = false;
}
