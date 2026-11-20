// 二选一 · 决策器 —— Cardputer ADV
// 把两个选项放上天平，给一个带倾向度的结果。
// 结果只是打破僵局的工具，决定权始终在人。

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_system.h>
#include <math.h>

#include "cjk_font.h"
#include "keyboard_adv.h"
#include "ui.h"

using ui::canvas;

enum State { S_COVER, S_NAME, S_WEIGH, S_RESULT };

static AdvKeyboard keyboard;
static ui::ShakeDetector shake;

static State state = S_COVER;
static char labelA[16] = "选项一";
static char labelB[16] = "选项二";

static char inputBuf[16];
static int inputLen = 0;
static int nameTarget = 0;  // 0=A 1=B

static int winner = 0;      // 0=A 1=B
static int lean = 60;       // 倾向百分比
static uint32_t weighStart = 0;
static uint32_t bootAt = 0;

static const char* reasons[] = {
    "两边很接近，选哪个都不差。",
    "这个选项的代价更小。",
    "另一个选项有明显硬伤。",
    "你心里其实更想选它。",
    "先排除更容易后悔的。",
    "它更符合你现在的处境。",
};
static constexpr int REASON_N = sizeof(reasons) / sizeof(reasons[0]);
static const char* reasonText = reasons[0];

static void saveLabels() {
    ui::prefs.putString("la", labelA);
    ui::prefs.putString("lb", labelB);
}

static void startWeigh() {
    winner = (int)(esp_random() & 1);
    lean = 52 + (int)(esp_random() % 27);
    reasonText = reasons[esp_random() % REASON_N];
    weighStart = millis();
    state = S_WEIGH;
}

static void openName(int target) {
    nameTarget = target;
    const char* src = target == 0 ? labelA : labelB;
    snprintf(inputBuf, sizeof(inputBuf), "%s", src);
    inputLen = strlen(inputBuf);
    state = S_NAME;
    ui::beep(700, 25);
}

// ---------------- 绘制 ----------------
static void drawCover() {
    canvas.fillScreen(ui::C_BG);
    for (int y = 0; y < ui::H; y += 4) canvas.drawFastHLine(0, y, ui::W, ui::C_BG2);

    canvas.drawRoundRect(20, 8, 200, 119, 8, ui::C_GOLD_D);
    canvas.drawRoundRect(21, 9, 198, 117, 7, ui::C_GOLD);

    cf::drawCentered(canvas, 120, 18, "二选一", ui::C_PAPER, cf::font24);
    cf::drawCentered(canvas, 120, 50, "把两个选项想清楚", ui::C_MUTED, cf::font16);

    // 两个选项
    char a[20], b[20];
    snprintf(a, sizeof(a), "A · %s", labelA);
    snprintf(b, sizeof(b), "B · %s", labelB);
    cf::drawText(canvas, 44, 72, a, ui::C_GOLD, cf::font16);
    cf::drawText(canvas, 44, 92, b, ui::C_MUTED, cf::font16);

    if (((millis() - bootAt) / 550) % 2 == 0)
        cf::drawCentered(canvas, 120, 110, "ENTER 开始权衡", ui::C_GOLD, cf::font16);

    ui::drawAsciiCentered(canvas, 120, 124, "1/2:NAME   ENTER:START", ui::rgb(72, 86, 108), 1);
}

static void drawName() {
    canvas.fillScreen(ui::C_BG);
    char title[24];
    snprintf(title, sizeof(title), "命名选项 %c", nameTarget == 0 ? 'A' : 'B');
    cf::drawText(canvas, 8, 6, title, ui::C_PAPER, cf::font16);
    ui::drawAscii(canvas, ui::W - 62, 10, "ESC:CANCEL", ui::C_MUTED, 1);
    canvas.drawFastHLine(8, 28, ui::W - 16, ui::C_LINE);

    canvas.drawRoundRect(12, 44, ui::W - 24, 34, 5, ui::C_GOLD_D);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(2);
    canvas.setTextColor(ui::C_PAPER);
    canvas.drawString(inputBuf, 20, 54);

    if ((millis() / 450) % 2 == 0) {
        int w = canvas.textWidth(inputBuf);
        canvas.fillRect(20 + w + 1, 54, 2, 16, ui::C_GOLD);
    }

    cf::drawCentered(canvas, 120, 92, "输入字母/数字，留空则用默认", ui::C_MUTED,
                     cf::font16);
    ui::drawAsciiCentered(canvas, 120, 124, "ENTER:OK   ESC:CANCEL", ui::rgb(72, 86, 108), 1);
}

static void drawBalance(float angle, uint16_t colA, uint16_t colB, bool settled) {
    const int cx = 120, cy = 74, half = 76;
    const float ca = cosf(angle), sa = sinf(angle);
    const int x1 = cx - (int)(ca * half), y1 = cy + (int)(sa * half);
    const int x2 = cx + (int)(ca * half), y2 = cy - (int)(sa * half);

    // 支架
    canvas.fillTriangle(cx, cy, cx - 9, cy + 22, cx + 9, cy + 22, ui::C_GOLD_D);
    canvas.fillRect(cx - 10, cy + 21, 20, 3, ui::C_GOLD_D);

    // 横梁
    canvas.drawLine(x1, y1, x2, y2, ui::C_GOLD);
    canvas.drawLine(x1, y1 + 1, x2, y2 + 1, ui::C_GOLD_D);

    // 吊盘
    const int drop = 15;
    canvas.drawLine(x1, y1, x1, y1 + drop, ui::C_MUTED);
    canvas.drawLine(x2, y2, x2, y2 + drop, ui::C_MUTED);
    canvas.fillRoundRect(x1 - 16, y1 + drop, 32, 5, 2, colA);
    canvas.fillRoundRect(x2 - 16, y2 + drop, 32, 5, 2, colB);

    // 标签
    char a[20], b[20];
    snprintf(a, sizeof(a), "%s", labelA);
    snprintf(b, sizeof(b), "%s", labelB);
    cf::drawCentered(canvas, x1, y1 - 24, a, colA, cf::font16);
    cf::drawCentered(canvas, x2, y2 - 24, b, colB, cf::font16);

    (void)settled;
}

static void drawWeigh(uint32_t now) {
    canvas.fillScreen(ui::C_BG);
    cf::drawText(canvas, 8, 4, "二选一", ui::C_MUTED, cf::font16);
    cf::drawCentered(canvas, 170, 4, "权衡中", ui::C_GOLD, cf::font16);
    canvas.drawFastHLine(8, 22, ui::W - 16, ui::C_LINE);

    float t = (now - weighStart) / 1800.0f;
    if (t > 1) t = 1;
    float e = 1.0f - powf(1.0f - t, 3);
    // 左低 = A 赢（angle 为正）
    float angle = (winner == 0 ? 1.0f : -1.0f) * 0.28f * e;
    drawBalance(angle, ui::C_GOLD, ui::C_MUTED, false);
}

static void drawResult() {
    canvas.fillScreen(ui::C_BG);
    cf::drawText(canvas, 8, 4, "二选一", ui::C_MUTED, cf::font16);
    canvas.drawFastHLine(8, 22, ui::W - 16, ui::C_LINE);

    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", lean);
    ui::drawAscii(canvas, ui::W - 8 - 6 * strlen(pct), 8, pct, ui::C_GOLD, 1);

    const char* win = winner == 0 ? labelA : labelB;
    char big[24];
    snprintf(big, sizeof(big), "选 %c", winner == 0 ? 'A' : 'B');
    cf::drawCentered(canvas, ui::W / 2, 28, big, ui::C_GOLD, cf::font24);

    char sub[24];
    snprintf(sub, sizeof(sub), "「%s」", win);
    cf::drawCentered(canvas, ui::W / 2, 58, sub, ui::C_PAPER, cf::font16);

    // 倾向条：左 A（金） / 右 B（蓝）
    const int barX = 24, barY = 82, barW = 192;
    const int aShare = (winner == 0) ? lean : 100 - lean;
    const int aW = barW * aShare / 100;
    canvas.drawRoundRect(barX, barY, barW, 10, 5, ui::C_LINE);
    if (aW > 2) canvas.fillRoundRect(barX, barY, aW, 10, 5, ui::C_GOLD);
    if (aW < barW - 2)
        canvas.fillRoundRect(barX + aW, barY, barW - aW, 10, 5, ui::C_MAYBE);

    ui::drawAscii(canvas, barX, barY + 12, "A", ui::C_GOLD, 1);
    ui::drawAscii(canvas, barX + barW - 6, barY + 12, "B", ui::C_MAYBE, 1);

    cf::drawCentered(canvas, ui::W / 2, 104, reasonText, ui::C_MUTED, cf::font16);

    ui::drawAsciiCentered(canvas, 120, 126,
                          "ENTER:AGAIN  ESC:BACK  YOU DECIDE",
                          ui::rgb(72, 86, 108), 1);
}

// ---------------- 输入 ----------------
static void handleKey(const KeyEvent& ev) {
    const bool confirm = (ev.kind == KEY_ENTER);

    switch (state) {
        case S_COVER:
            if (confirm || ev.kind == KEY_SPACE) {
                ui::beep(660, 40);
                startWeigh();
            } else if (ev.kind == KEY_CHAR && (ev.ch == '1' || ev.ch == '2')) {
                openName(ev.ch == '1' ? 0 : 1);
            } else if (ev.kind == KEY_ESC || ev.kind == KEY_BACKSPACE) {
                // 封面不做事
            }
            break;

        case S_NAME:
            if (confirm) {
                if (inputLen > 0) {
                    if (nameTarget == 0) snprintf(labelA, sizeof(labelA), "%s", inputBuf);
                    else snprintf(labelB, sizeof(labelB), "%s", inputBuf);
                    saveLabels();
                }
                state = S_COVER;
                ui::beep(880, 30);
            } else if (ev.kind == KEY_ESC) {
                state = S_COVER;
            } else if (ev.kind == KEY_BACKSPACE) {
                if (inputLen > 0) inputBuf[--inputLen] = 0;
            } else if (ev.kind == KEY_SPACE) {
                if (inputLen < (int)sizeof(inputBuf) - 1) {
                    inputBuf[inputLen++] = ' ';
                    inputBuf[inputLen] = 0;
                }
            } else if (ev.kind == KEY_CHAR) {
                if (inputLen < (int)sizeof(inputBuf) - 1) {
                    inputBuf[inputLen++] = ev.ch;
                    inputBuf[inputLen] = 0;
                    ui::beep(1400, 8);
                }
            }
            break;

        case S_WEIGH:
            // 权衡中忽略按键，避免打断
            break;

        case S_RESULT:
            if (confirm) {
                ui::beep(660, 40);
                startWeigh();
            } else if (ev.kind == KEY_ESC || ev.kind == KEY_BACKSPACE) {
                state = S_COVER;
            }
            break;
    }
}

// ---------------- Arduino ----------------
void setup() {
    if (!ui::begin("decider")) return;
    keyboard.begin();
    bootAt = millis();

    String la = ui::prefs.getString("la", "选项一");
    String lb = ui::prefs.getString("lb", "选项二");
    snprintf(labelA, sizeof(labelA), "%s", la.c_str());
    snprintf(labelB, sizeof(labelB), "%s", lb.c_str());

    Serial.printf("[decider] rot=%d %dx%d kb=%d\n",
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
        Serial.println("[decider] shake");
        ui::beep(660, 40);
        startWeigh();
        dirty = true;
    }

    if (state == S_WEIGH && now - weighStart > 1800) {
        state = S_RESULT;
        ui::beep(880, 70);
        delay(50);
        ui::beep(1320, 110);
        dirty = true;
    }

    const bool anim = (state == S_WEIGH);
    if (!dirty && now - lastDraw < (anim ? 16 : 200)) {
        delay(5);
        return;
    }

    switch (state) {
        case S_COVER:  drawCover(); break;
        case S_NAME:   drawName(); break;
        case S_WEIGH:  drawWeigh(now); break;
        case S_RESULT: drawResult(); break;
    }

    canvas.pushSprite(0, 0);
    lastDraw = now;
    dirty = false;
}
