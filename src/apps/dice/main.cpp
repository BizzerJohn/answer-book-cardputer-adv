// 骰子与硬币 · Cardputer ADV
// 摇一摇或按 Enter 掷骰子 / 抛硬币；支持 1~3 颗骰子、统计与持久化。

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_system.h>
#include <math.h>

#include "cjk_font.h"
#include "keyboard_adv.h"
#include "ui.h"

using ui::canvas;

enum Screen { S_HOME, S_STATS, S_SETTINGS };
enum Mode { M_DICE = 0, M_COIN = 1 };

static AdvKeyboard keyboard;
static ui::ShakeDetector shake;

static Screen screen = S_HOME;
static Mode mode = M_DICE;

static uint8_t diceCount = 2;      // 1..3
static uint8_t faces[3] = {1, 1, 1};
static bool coinZhi = true;        // true 字 / false 花
static uint32_t diceRolls = 0;
static uint32_t faceCount[6] = {0};
static uint32_t zhiCount = 0, huaCount = 0;

static bool rolling = false;
static uint32_t rollStart = 0;
static uint32_t rollDur = 800;
static uint32_t lastTick = 0;
static float coinAngle = 0;
static uint8_t animFaces[3];

static uint8_t statsSel = 0;
static uint8_t settingsSel = 0;
static uint32_t bootAt = 0;

// ---------------- 存档 ----------------
static void saveStats() {
    ui::prefs.putUChar("mode", (uint8_t)mode);
    ui::prefs.putUChar("count", diceCount);
    ui::prefs.putUInt("total", diceRolls);
    ui::prefs.putUInt("zhi", zhiCount);
    ui::prefs.putUInt("hua", huaCount);
    for (int i = 0; i < 6; ++i) {
        char key[8];
        snprintf(key, sizeof(key), "f%d", i + 1);
        ui::prefs.putUInt(key, faceCount[i]);
    }
}

static void loadStats() {
    mode = (Mode)ui::prefs.getUChar("mode", M_DICE);
    diceCount = ui::prefs.getUChar("count", 2);
    if (diceCount < 1 || diceCount > 3) diceCount = 2;
    diceRolls = ui::prefs.getUInt("total", 0);
    zhiCount = ui::prefs.getUInt("zhi", 0);
    huaCount = ui::prefs.getUInt("hua", 0);
    for (int i = 0; i < 6; ++i) {
        char key[8];
        snprintf(key, sizeof(key), "f%d", i + 1);
        faceCount[i] = ui::prefs.getUInt(key, 0);
    }
}

// ---------------- 绘制基础 ----------------
static void drawDie(int x, int y, int size, int face) {
    canvas.fillRoundRect(x, y, size, size, size / 7, ui::C_PAPER);
    canvas.drawRoundRect(x, y, size, size, size / 7, ui::rgb(30, 40, 60));
    canvas.drawFastHLine(x + 5, y + 2, size - 10, ui::rgb(220, 208, 190));

    const int a = size / 4, b = size / 2, c = size * 3 / 4;
    const int r = size / 12 < 2 ? 2 : size / 12;
    const int px[7] = {a, a, a, b, c, c, c};
    const int py[7] = {a, b, c, b, a, b, c};

    // 每个点数用到的位置索引（0 左上 / 1 左中 / 2 左下 / 3 中心 / 4 右上 / 5 右中 / 6 右下）
    static const uint8_t masks[7][6] = {
        {0}, {3}, {0, 6}, {0, 3, 6}, {0, 2, 4, 6}, {0, 2, 3, 4, 6}, {0, 1, 2, 4, 5, 6},
    };
    static const uint8_t counts[7] = {0, 1, 2, 3, 4, 5, 6};
    for (int i = 0; i < counts[face]; ++i) {
        int idx = masks[face][i];
        canvas.fillCircle(x + px[idx], y + py[idx], r, ui::rgb(30, 40, 60));
    }
}

static void drawCoinFace(int cx, int cy, int r, bool zhi) {
    canvas.fillCircle(cx, cy, r, ui::C_GOLD_D);
    canvas.fillCircle(cx, cy, r - 2, ui::C_GOLD);
    canvas.fillCircle(cx, cy, r - 6, ui::rgb(250, 242, 214));
    canvas.drawCircle(cx, cy, r - 4, ui::rgb(170, 120, 40));
    cf::drawCentered(canvas, cx, cy - 12, zhi ? "字" : "花",
                     ui::rgb(120, 80, 20), cf::font24);
}

static void drawCoinSpin(int cx, int cy, int r, float angle) {
    float c = cosf(angle);
    int rx = (int)(r * fabsf(c));
    if (rx < 3) {
        canvas.fillRect(cx - 3, cy - r, 6, r * 2, ui::C_GOLD_D);
        return;
    }
    canvas.fillEllipse(cx, cy, rx, r, ui::C_GOLD_D);
    int inner = rx - 2 < 2 ? 2 : rx - 2;
    canvas.fillEllipse(cx, cy, inner, r - 2, ui::C_GOLD);
    int core = rx - 6 < 2 ? 2 : rx - 6;
    canvas.fillEllipse(cx, cy, core, r - 6, ui::rgb(250, 242, 214));
    if (rx > r * 3 / 5) {
        cf::drawCentered(canvas, cx, cy - 12, c > 0 ? "字" : "花",
                         ui::rgb(120, 80, 20), cf::font24);
    }
}

static void drawTabs() {
    cf::drawText(canvas, 58, 2, "骰子", mode == M_DICE ? ui::C_GOLD : ui::C_MUTED,
                 cf::font16);
    cf::drawText(canvas, 142, 2, "硬币", mode == M_COIN ? ui::C_GOLD : ui::C_MUTED,
                 cf::font16);
    if (mode == M_DICE)
        canvas.drawFastHLine(56, 20, 36, ui::C_GOLD);
    else
        canvas.drawFastHLine(140, 20, 36, ui::C_GOLD);
    canvas.drawFastHLine(8, 24, ui::W - 16, ui::C_LINE);
}

// ---------------- 主界面 ----------------
static void drawHome() {
    canvas.fillScreen(ui::C_BG);
    for (int y = 0; y < ui::H; y += 4) canvas.drawFastHLine(0, y, ui::W, ui::C_BG2);
    drawTabs();

    if (mode == M_DICE) {
        const int size = diceCount == 1 ? 46 : (diceCount == 2 ? 42 : 34);
        const int gap = diceCount == 3 ? 8 : 12;
        int totalW = diceCount * size + (diceCount - 1) * gap;
        int x = (ui::W - totalW) / 2;
        const int y = 54 - size / 2;

        for (int i = 0; i < diceCount; ++i) {
            int face = rolling ? animFaces[i] : faces[i];
            int bob = rolling ? (int)(sinf(millis() * 0.03f + i) * 3) : 0;
            drawDie(x + i * (size + gap), y + bob, size, face);
        }

        if (!rolling) {
            int sum = 0;
            for (int i = 0; i < diceCount; ++i) sum += faces[i];
            char buf[24];
            if (diceCount == 1) {
                snprintf(buf, sizeof(buf), "%d", faces[0]);
                cf::drawCentered(canvas, ui::W / 2, 96, buf, ui::C_PAPER, cf::font24);
            } else {
                snprintf(buf, sizeof(buf), "合计 %d", sum);
                cf::drawCentered(canvas, ui::W / 2, 94, buf, ui::C_PAPER, cf::font24);
            }
        }
        ui::drawAscii(canvas, 8, 6, "DICE", ui::C_MUTED, 1);
    } else {
        const int r = 27;
        const int cy = 62;
        if (rolling) {
            drawCoinSpin(ui::W / 2, cy, r, coinAngle);
        } else {
            drawCoinFace(ui::W / 2, cy, r, coinZhi);
        }
        ui::drawAscii(canvas, 8, 6, "COIN", ui::C_MUTED, 1);
    }

    char info[24];
    if (mode == M_DICE) {
        snprintf(info, sizeof(info), "x%d", diceCount);
        ui::drawAscii(canvas, ui::W - 22, 6, info, ui::C_MUTED, 1);
    }

    ui::drawAsciiCentered(canvas, ui::W / 2, 124,
                          rolling ? "..." : "ENTER/SHAKE   < >:MODE   H:STAT",
                          ui::C_MUTED, 1);
}

// ---------------- 统计 ----------------
static void drawStats() {
    canvas.fillScreen(ui::C_BG);
    cf::drawText(canvas, 8, 6, "统计", ui::C_PAPER, cf::font16);
    ui::drawAscii(canvas, ui::W - 52, 10, "ESC:BACK", ui::C_MUTED, 1);
    canvas.drawFastHLine(8, 28, ui::W - 16, ui::C_LINE);

    char buf[32];
    if (mode == M_DICE) {
        uint32_t maxC = 1;
        for (int i = 0; i < 6; ++i)
            if (faceCount[i] > maxC) maxC = faceCount[i];

        for (int i = 0; i < 6; ++i) {
            int y = 32 + i * 13;
            snprintf(buf, sizeof(buf), "%d", i + 1);
            ui::drawAscii(canvas, 14, y, buf, ui::C_PAPER, 1);
            int w = (int)(140 * faceCount[i] / maxC);
            if (w > 0) canvas.fillRect(36, y, w, 7, ui::C_GOLD);
            canvas.drawRect(36, y, 140, 7, ui::C_LINE);
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)faceCount[i]);
            ui::drawAscii(canvas, 184, y, buf, ui::C_MUTED, 1);
        }
        snprintf(buf, sizeof(buf), "骰子 %lu 次  硬币 %lu 次",
                 (unsigned long)diceRolls,
                 (unsigned long)(zhiCount + huaCount));
        cf::drawText(canvas, 8, 114, buf, ui::C_MUTED, cf::font16);
    } else {
        uint32_t maxC = zhiCount > huaCount ? zhiCount : huaCount;
        if (maxC == 0) maxC = 1;
        const char* labels[2] = {"字", "花"};
        uint32_t vals[2] = {zhiCount, huaCount};
        for (int i = 0; i < 2; ++i) {
            int y = 44 + i * 40;
            cf::drawText(canvas, 20, y, labels[i], ui::C_PAPER, cf::font16);
            int w = (int)(120 * vals[i] / maxC);
            if (w > 0) canvas.fillRect(56, y + 4, w, 10, ui::C_GOLD);
            canvas.drawRect(56, y + 4, 120, 10, ui::C_LINE);
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)vals[i]);
            ui::drawAscii(canvas, 190, y + 5, buf, ui::C_MUTED, 1);
        }
        snprintf(buf, sizeof(buf), "共 %lu 次",
                 (unsigned long)(zhiCount + huaCount));
        cf::drawText(canvas, 8, 112, buf, ui::C_MUTED, cf::font16);
    }
}

// ---------------- 设置 ----------------
static void drawSettings() {
    canvas.fillScreen(ui::C_BG);
    cf::drawText(canvas, 8, 4, "设置", ui::C_PAPER, cf::font16);
    ui::drawAscii(canvas, ui::W - 52, 8, "ESC:BACK", ui::C_MUTED, 1);
    canvas.drawFastHLine(8, 22, ui::W - 16, ui::C_LINE);

    static const char* labels[2] = {"音效", "亮度"};
    for (int i = 0; i < 2; ++i) {
        int y = 44 + i * 30;
        if (i == settingsSel) {
            canvas.fillRoundRect(6, y - 4, ui::W - 12, 26, 4, ui::rgb(24, 36, 56));
            canvas.drawRoundRect(6, y - 4, ui::W - 12, 26, 4, ui::C_GOLD_D);
        }
        cf::drawText(canvas, 16, y, labels[i],
                     i == settingsSel ? ui::C_GOLD : ui::C_PAPER, cf::font16);
    }
    char buf[12];
    snprintf(buf, sizeof(buf), "%s", ui::soundOn ? "ON" : "OFF");
    ui::drawAscii(canvas, ui::W - 40, 48, buf, ui::C_MUTED, 1);
    snprintf(buf, sizeof(buf), "%d%%", ui::brightPct);
    ui::drawAscii(canvas, ui::W - 46, 78, buf, ui::C_MUTED, 1);

    ui::drawAsciiCentered(canvas, ui::W / 2, 126, "ENTER:CHANGE", ui::C_MUTED, 1);
}

// ---------------- 掷 / 抛 ----------------
static void startRoll() {
    if (rolling) return;
    rolling = true;
    rollStart = millis();
    coinAngle = 0;
    lastTick = 0;

    if (mode == M_DICE) {
        rollDur = 800;
        for (int i = 0; i < 3; ++i)
            faces[i] = (uint8_t)(1 + esp_random() % 6);
        animFaces[0] = faces[0];
        animFaces[1] = faces[1];
        animFaces[2] = faces[2];
    } else {
        rollDur = 1000;
        coinZhi = (esp_random() & 1) != 0;
    }
    ui::beep(900, 30);
}

static void finishRoll() {
    rolling = false;
    if (mode == M_DICE) {
        ++diceRolls;
        for (int i = 0; i < diceCount; ++i) ++faceCount[faces[i] - 1];
    } else {
        if (coinZhi) ++zhiCount;
        else ++huaCount;
        coinAngle = coinZhi ? 0.0f : (float)M_PI;  // 定格面与结果一致
    }
    saveStats();
    ui::beep(880, 70);
    delay(45);
    ui::beep(1320, 110);
}

// ---------------- 输入 ----------------
static void handleKey(const KeyEvent& ev) {
    const bool confirm = (ev.kind == KEY_ENTER || ev.kind == KEY_SPACE);

    switch (screen) {
        case S_HOME:
            if (rolling) break;
            if (confirm) {
                startRoll();
            } else if (ev.kind == KEY_LEFT || ev.kind == KEY_RIGHT) {
                mode = mode == M_DICE ? M_COIN : M_DICE;
                saveStats();
                ui::beep(700, 25);
            } else if (ev.kind == KEY_UP) {
                if (mode == M_DICE) {
                    diceCount = diceCount == 1 ? 3 : diceCount - 1;
                    saveStats();
                }
            } else if (ev.kind == KEY_DOWN) {
                if (mode == M_DICE) {
                    diceCount = diceCount == 3 ? 1 : diceCount + 1;
                    saveStats();
                }
            } else if (ev.kind == KEY_CHAR && ev.ch == 'h') {
                statsSel = 0;
                screen = S_STATS;
            } else if (ev.kind == KEY_CHAR && ev.ch == 's') {
                settingsSel = 0;
                screen = S_SETTINGS;
            }
            break;

        case S_STATS:
            if (ev.kind == KEY_ESC || ev.kind == KEY_BACKSPACE || confirm)
                screen = S_HOME;
            break;

        case S_SETTINGS:
            if (ev.kind == KEY_UP || ev.kind == KEY_DOWN) {
                settingsSel ^= 1;
            } else if (ev.kind == KEY_ESC || ev.kind == KEY_BACKSPACE) {
                ui::saveSoundBright();
                screen = S_HOME;
            } else if (confirm) {
                if (settingsSel == 0) {
                    ui::soundOn = !ui::soundOn;
                    ui::beep(900, 30);
                } else {
                    int b = ui::brightPct + 20;
                    if (b > 100) b = 20;
                    ui::brightPct = (uint8_t)b;
                    ui::applyBrightness();
                }
                ui::saveSoundBright();
            }
            break;
    }
}

// ---------------- Arduino ----------------
void setup() {
    if (!ui::begin("dice")) return;
    keyboard.begin();
    loadStats();
    bootAt = millis();

    Serial.printf("[dice] rot=%d %dx%d kb=%d mode=%d count=%d\n",
                  (int)M5.Display.getRotation(), (int)M5.Display.width(),
                  (int)M5.Display.height(), keyboard.started() ? 1 : 0,
                  (int)mode, diceCount);
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

    if (screen == S_HOME && !rolling && shake.update()) {
        Serial.println("[dice] shake");
        startRoll();
        dirty = true;
    }

    if (rolling) {
        if (now - lastTick > 70) {
            lastTick = now;
            if (mode == M_DICE) {
                for (int i = 0; i < 3; ++i)
                    animFaces[i] = (uint8_t)(1 + esp_random() % 6);
            } else {
                coinAngle += 0.9f;
            }
            ui::beep(1200 + esp_random() % 300, 6);
        }
        if (now - rollStart >= rollDur) {
            finishRoll();
            dirty = true;
        }
    }

    const uint32_t interval = rolling ? 33 : 200;
    if (!dirty && now - lastDraw < interval) {
        delay(6);
        return;
    }

    switch (screen) {
        case S_HOME:     drawHome(); break;
        case S_STATS:    drawStats(); break;
        case S_SETTINGS: drawSettings(); break;
    }

    canvas.pushSprite(0, 0);
    lastDraw = now;
    dirty = false;
}
