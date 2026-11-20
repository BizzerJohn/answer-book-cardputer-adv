// 易经 · 六爻 —— Cardputer ADV
// 三枚铜钱掷六次成卦：本卦 + 动爻 + 之卦。
// 卦序与上下卦编码由 tools/gen_hexagrams.py 生成。

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_system.h>

#include "cjk_font.h"
#include "hexagrams.h"
#include "keyboard_adv.h"
#include "ui.h"

using ui::canvas;

enum State { S_COVER, S_THROW, S_RESULT };

static AdvKeyboard keyboard;
static ui::ShakeDetector shake;

static State state = S_COVER;

static uint8_t lines[6];    // 1 = 阳, 0 = 阴；index 0 = 初爻
static bool moving[6];
static int throwIndex = 0;

static uint8_t pendingLine = 0;
static bool pendingMoving = false;
static uint8_t coins[3];
static uint32_t animStart = 0;
static bool animating = false;
static bool hasThrown = false;

static const Hexagram* ben = nullptr;
static const Hexagram* bian = nullptr;
static bool hasChange = false;
static bool showBian = false;

static uint32_t bootAt = 0;

static const Hexagram* findHex(uint8_t upper, uint8_t lower) {
    for (int i = 0; i < 64; ++i) {
        if (HEXAGRAMS[i].upper == upper && HEXAGRAMS[i].lower == lower)
            return &HEXAGRAMS[i];
    }
    return &HEXAGRAMS[0];
}

static void hexName(const Hexagram* h, char* out, size_t n) {
    if (h->upper == h->lower)
        snprintf(out, n, "%s为%s", h->name, TRIGRAM_NATURE[h->upper]);
    else
        snprintf(out, n, "%s%s%s", TRIGRAM_NATURE[h->upper],
                 TRIGRAM_NATURE[h->lower], h->name);
}

static void startReading() {
    memset(lines, 0, sizeof(lines));
    memset(moving, 0, sizeof(moving));
    throwIndex = 0;
    animating = false;
    hasThrown = false;
    hasChange = false;
    showBian = false;
    ben = bian = nullptr;
    state = S_THROW;
}

static void rollOne() {
    if (animating || throwIndex >= 6) return;

    for (int i = 0; i < 3; ++i) coins[i] = (uint8_t)(esp_random() & 1);
    int sum = 0;
    for (int i = 0; i < 3; ++i) sum += coins[i] ? 3 : 2;  // 字=3 背=2

    pendingLine = (sum == 7 || sum == 9) ? 1 : 0;
    pendingMoving = (sum == 6 || sum == 9);
    animStart = millis();
    animating = true;
    hasThrown = true;
}

static void commitLine() {
    lines[throwIndex] = pendingLine;
    moving[throwIndex] = pendingMoving;
    ++throwIndex;
    animating = false;
    ui::beep(pendingMoving ? 1500 : 1200, 25);

    if (throwIndex >= 6) {
        uint8_t up = lines[3] | (lines[4] << 1) | (lines[5] << 2);
        uint8_t lo = lines[0] | (lines[1] << 1) | (lines[2] << 2);
        ben = findHex(up, lo);

        uint8_t ch[6];
        hasChange = false;
        for (int i = 0; i < 6; ++i) {
            ch[i] = moving[i] ? !lines[i] : lines[i];
            if (moving[i]) hasChange = true;
        }
        uint8_t up2 = ch[3] | (ch[4] << 1) | (ch[5] << 2);
        uint8_t lo2 = ch[0] | (ch[1] << 1) | (ch[2] << 2);
        bian = findHex(up2, lo2);

        state = S_RESULT;
        showBian = false;
        ui::beep(880, 90);
        delay(70);
        ui::beep(1320, 140);
    }
}

// ---------------- 绘制 ----------------
static void drawCover() {
    canvas.fillScreen(ui::C_BG);
    for (int y = 0; y < ui::H; y += 4) canvas.drawFastHLine(0, y, ui::W, ui::C_BG2);

    canvas.drawRoundRect(20, 8, 200, 119, 8, ui::C_GOLD_D);
    canvas.drawRoundRect(21, 9, 198, 117, 7, ui::C_GOLD);

    cf::drawCentered(canvas, 120, 22, "易经 · 六爻", ui::C_PAPER, cf::font24);
    cf::drawCentered(canvas, 120, 56, "三枚铜钱，掷六次", ui::C_MUTED, cf::font16);
    cf::drawCentered(canvas, 120, 76, "诚心默念所问之事", ui::C_MUTED, cf::font16);

    if (((millis() - bootAt) / 550) % 2 == 0)
        cf::drawCentered(canvas, 120, 100, "按 ENTER 起卦", ui::C_GOLD, cf::font16);

    ui::drawAsciiCentered(canvas, 120, 124, "ENTER:START   ESC:BACK", ui::rgb(72, 86, 108), 1);
}

// 六条爻线，从下往上
static void drawLines(int x, int yTop, int width, const uint8_t* ls,
                      const bool* mv, uint16_t color) {
    for (int i = 0; i < 6; ++i) {
        const int y = yTop + (5 - i) * 12;
        if (ls[i]) {
            canvas.fillRect(x, y, width, 5, color);
        } else {
            canvas.fillRect(x, y, width / 2 - 3, 5, color);
            canvas.fillRect(x + width / 2 + 3, y, width / 2 - 3, 5, color);
        }
        if (mv && mv[i]) {
            canvas.drawCircle(x + width + 6, y + 2, 2, ui::C_GOLD);
        }
    }
}

static const char* lineWord(int sum) {
    switch (sum) {
        case 6: return "老阴 · 动";
        case 7: return "少阳";
        case 8: return "少阴";
        default: return "老阳 · 动";
    }
}

static int coinsSum() {
    int s = 0;
    for (int i = 0; i < 3; ++i) s += coins[i] ? 3 : 2;
    return s;
}

static void drawCoin(int x, int y, const char* face, uint16_t color) {
    canvas.drawCircle(x, y, 13, ui::C_GOLD);
    canvas.drawCircle(x, y, 12, ui::C_GOLD_D);
    cf::drawCentered(canvas, x, y - 8, face, color, cf::font16);
}

static void drawThrow(uint32_t now) {
    canvas.fillScreen(ui::C_BG);
    canvas.drawFastHLine(0, 0, ui::W, ui::C_BG2);

    char buf[24];
    snprintf(buf, sizeof(buf), "已掷 %d / 6", throwIndex);
    cf::drawText(canvas, 8, 4, "易经 · 六爻", ui::C_MUTED, cf::font16);
    cf::drawCentered(canvas, 160, 4, buf, ui::C_PAPER, cf::font16);
    canvas.drawFastHLine(8, 22, ui::W - 16, ui::C_LINE);
    canvas.drawRoundRect(8, 28, ui::W - 16, 82, 5, ui::rgb(30, 42, 60));

    // 左：已成的爻
    drawLines(18, 44, 56, lines, moving, ui::C_PAPER);

    // 右：三枚铜钱
    const bool spin = animating;
    for (int i = 0; i < 3; ++i) {
        const char* face;
        uint16_t col;
        if (spin) {
            bool up = ((now / 70 + i) & 1) != 0;
            face = up ? "字" : "背";
            col = up ? ui::C_GOLD : ui::C_PAPER;
        } else if (!hasThrown) {
            face = "?";
            col = ui::C_MUTED;
        } else {
            face = coins[i] ? "字" : "背";
            col = coins[i] ? ui::C_GOLD : ui::C_PAPER;
        }
        drawCoin(138 + i * 34, 62, face, col);
    }

    if (!animating && throwIndex < 6) {
        cf::drawCentered(canvas, 172, 88, "按 ENTER 掷币", ui::C_MUTED, cf::font16);
    } else if (!animating) {
        cf::drawCentered(canvas, 172, 88, lineWord(coinsSum()), ui::C_GOLD, cf::font16);
    }

    ui::drawAsciiCentered(canvas, ui::W / 2, 124, "ENTER:THROW   ESC:BACK", ui::rgb(72, 86, 108), 1);
}

static void drawResult() {
    canvas.fillScreen(ui::C_BG);
    canvas.drawFastHLine(0, 0, ui::W, ui::C_BG2);

    const Hexagram* h = showBian ? bian : ben;
    char title[24];
    hexName(h, title, sizeof(title));

    cf::drawText(canvas, 8, 4, "易经 · 六爻", ui::C_MUTED, cf::font16);
    char numBuf[16];
    snprintf(numBuf, sizeof(numBuf), "第 %d 卦", h->num);
    cf::drawText(canvas, ui::W - 8 - (int)cf::textWidth(numBuf, cf::font16), 4,
                 numBuf, ui::C_MUTED, cf::font16);
    canvas.drawFastHLine(8, 22, ui::W - 16, ui::C_LINE);

    // 左：本卦 / 之卦爻线
    uint8_t shown[6];
    for (int i = 0; i < 6; ++i)
        shown[i] = (showBian && moving[i]) ? !lines[i] : lines[i];
    drawLines(14, 38, 56, shown, showBian ? nullptr : moving,
              showBian ? ui::C_GOLD : ui::C_PAPER);

    // 右：卦名 + 上下卦 + 断语
    cf::drawText(canvas, 88, 28, title,
                 showBian ? ui::C_GOLD : ui::C_PAPER, cf::font24);

    char tri[24];
    snprintf(tri, sizeof(tri), "上%s 下%s", TRIGRAM_NATURE[h->upper],
             TRIGRAM_NATURE[h->lower]);
    cf::drawText(canvas, 88, 56, tri, ui::C_MUTED, cf::font16);

    cf::LineSpan linesp[4];
    int n = cf::wrapText(h->reading, cf::font16, ui::W - 94, linesp, 4);
    int y = 78;
    for (int i = 0; i < n && y < 112; ++i) {
        cf::drawText(canvas, 88, y, h->reading + linesp[i].start, ui::C_MUTED,
                     cf::font16);
        y += cf::font16.height + 2;
    }

    if (showBian) {
        ui::drawAsciiCentered(canvas, ui::W / 2 + 30, 124,
                              "SPACE:BACK  ENTER:NEW", ui::rgb(72, 86, 108), 1);
    } else if (hasChange) {
        ui::drawAsciiCentered(canvas, ui::W / 2 + 30, 124,
                              "SPACE:CHANGE  ENTER:NEW", ui::rgb(72, 86, 108), 1);
    } else {
        ui::drawAsciiCentered(canvas, ui::W / 2 + 30, 124,
                              "NO CHANGE  ENTER:NEW", ui::rgb(72, 86, 108), 1);
    }
}

// ---------------- 输入 ----------------
static void handleKey(const KeyEvent& ev) {
    const bool confirm = (ev.kind == KEY_ENTER);

    switch (state) {
        case S_COVER:
            if (confirm || ev.kind == KEY_SPACE) {
                ui::beep(660, 40);
                startReading();
            }
            break;

        case S_THROW:
            if (confirm) {
                rollOne();
            } else if ((ev.kind == KEY_ESC || ev.kind == KEY_BACKSPACE) && !animating) {
                state = S_COVER;
            }
            break;

        case S_RESULT:
            if (ev.kind == KEY_SPACE) {
                if (hasChange) showBian = !showBian;
            } else if (confirm) {
                ui::beep(660, 40);
                startReading();
            } else if (ev.kind == KEY_ESC || ev.kind == KEY_BACKSPACE) {
                state = S_COVER;
            }
            break;
    }
}

// ---------------- Arduino ----------------
void setup() {
    if (!ui::begin("iching")) return;
    keyboard.begin();
    bootAt = millis();

    Serial.printf("[iching] rot=%d %dx%d kb=%d\n",
                  (int)M5.Display.getRotation(), (int)M5.Display.width(),
                  (int)M5.Display.height(), keyboard.started() ? 1 : 0);
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

    if (state == S_COVER && shake.update()) {
        Serial.println("[iching] shake");
        ui::beep(660, 40);
        startReading();
        dirty = true;
    }

    if (animating && now - animStart > 700) {
        commitLine();
        dirty = true;
    }

    // 动画期间 60fps，其余按需
    if (!dirty && now - lastDraw < (animating ? 16 : 200)) {
        delay(5);
        return;
    }

    switch (state) {
        case S_COVER:  drawCover(); break;
        case S_THROW:  drawThrow(now); break;
        case S_RESULT: drawResult(); break;
    }

    canvas.pushSprite(0, 0);
    lastDraw = now;
    dirty = false;
}
