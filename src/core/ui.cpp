#include "ui.h"

#include <Arduino.h>
#include <math.h>

namespace ui {

M5Canvas canvas(&M5.Display);
Preferences prefs;
bool soundOn = true;
uint8_t brightPct = 70;

bool begin(const char* ns) {
    auto cfg = M5.config();
    M5.begin(cfg);

    // ADV 面板在 M5GFX 里带 offset_rotation = 1：0 才是自然横屏。
    M5.Display.setRotation(0);
    M5.Display.setColorDepth(16);
    M5.Display.fillScreen(TFT_BLACK);

    prefs.begin(ns, false);
    soundOn = prefs.getBool("sound", true);
    brightPct = prefs.getUChar("bright", 70);
    if (brightPct < 20 || brightPct > 100) brightPct = 70;
    applyBrightness();

    M5.Speaker.setVolume(soundOn ? 55 : 0);

    // ADV 没有 PSRAM，M5Canvas 默认会去申请 SPIRAM，必须先关掉。
    canvas.setPsram(false);
    canvas.setColorDepth(16);
    if (!canvas.createSprite(W, H)) {
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_RED);
        M5.Display.setCursor(10, 10);
        M5.Display.println("canvas alloc failed");
        while (true) delay(1000);
    }
    canvas.fillScreen(C_BG);
    return true;
}

void applyBrightness() {
    M5.Display.setBrightness((uint8_t)(brightPct * 255 / 100));
}

void beep(float hz, uint32_t ms) {
    if (!soundOn) return;
    M5.Speaker.setVolume(55);
    M5.Speaker.tone(hz, ms);
}

void saveSoundBright() {
    prefs.putBool("sound", soundOn);
    prefs.putUChar("bright", brightPct);
}

int drawAscii(M5Canvas& g, int x, int y, const char* s, uint16_t color,
              int size) {
    g.setFont(&fonts::Font0);
    g.setTextSize(size);
    g.setTextColor(color);
    g.drawString(s, x, y);
    return g.textWidth(s);
}

int drawAsciiCentered(M5Canvas& g, int cx, int y, const char* s, uint16_t color,
                      int size) {
    g.setFont(&fonts::Font0);
    g.setTextSize(size);
    int w = g.textWidth(s);
    drawAscii(g, cx - w / 2, y, s, color, size);
    return w;
}

int utf8Count(const char* s) {
    int n = 0;
    const uint8_t* p = (const uint8_t*)s;
    while (*p) {
        if (*p < 0x80) p += 1;
        else if ((*p & 0xE0) == 0xC0) p += 2;
        else if ((*p & 0xF0) == 0xE0) p += 3;
        else p += 4;
        ++n;
    }
    return n;
}

bool ShakeDetector::update() {
    float ax, ay, az;
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return false;

    const float mag = sqrtf(ax * ax + ay * ay + az * az);
    const float dev = fabsf(mag - 1.0f);
    const uint32_t now = millis();

    if (dev > 0.55f) {
        if (now - _window > 700) {
            _window = now;
            _cross = 0;
        }
        if (now - _lastCross > 110) {
            _lastCross = now;
            ++_cross;
        }
    }

    if (_cross >= 3 && now - _lastShake > 900) {
        _lastShake = now;
        _cross = 0;
        return true;
    }
    return false;
}

}  // namespace ui
