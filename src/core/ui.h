// 三个应用共用的显示/按键/声音/摇一摇基础设施。
#pragma once
#include <M5Unified.h>
#include <Preferences.h>
#include <stdint.h>

#include "cjk_font.h"

namespace ui {

// ---- 配色 ----
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t C_BG     = rgb(10, 16, 28);
constexpr uint16_t C_BG2    = rgb(14, 22, 38);
constexpr uint16_t C_PAPER  = rgb(242, 233, 216);
constexpr uint16_t C_MUTED  = rgb(122, 134, 153);
constexpr uint16_t C_GOLD   = rgb(217, 164, 65);
constexpr uint16_t C_GOLD_D = rgb(122, 90, 34);
constexpr uint16_t C_YES    = rgb(74, 222, 128);
constexpr uint16_t C_NO     = rgb(248, 113, 113);
constexpr uint16_t C_WAIT   = rgb(251, 191, 36);
constexpr uint16_t C_MAYBE  = rgb(147, 197, 253);
constexpr uint16_t C_LINE   = rgb(40, 52, 72);

constexpr int W = 240;
constexpr int H = 135;

// ---- 全局对象 ----
extern M5Canvas canvas;
extern Preferences prefs;
extern bool soundOn;
extern uint8_t brightPct;

// M5.begin + 屏幕旋转 + 读取通用设置 + 创建画布。
// ns 是 NVS 命名空间，三个应用各用一个，互不干扰。
bool begin(const char* ns);

void applyBrightness();
void beep(float hz, uint32_t ms);
void saveSoundBright();

// ---- 文本 ----
int drawAscii(M5Canvas& g, int x, int y, const char* s, uint16_t color,
              int size = 1);
int drawAsciiCentered(M5Canvas& g, int cx, int y, const char* s, uint16_t color,
                      int size = 1);
int utf8Count(const char* s);

// ---- 摇一摇 ----
class ShakeDetector {
public:
    bool update();  // 检测到一次摇晃返回 true（自带 900ms 冷却）
private:
    uint32_t _lastCross = 0;
    uint32_t _window = 0;
    uint32_t _lastShake = 0;
    int _cross = 0;
};

}  // namespace ui
