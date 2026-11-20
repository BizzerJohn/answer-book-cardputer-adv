#include "keyboard_adv.h"

#include <M5Unified.h>

// ---- TCA8418 寄存器 ----
static constexpr uint8_t TCA_ADDR      = 0x34;
static constexpr uint8_t REG_CFG       = 0x01;
static constexpr uint8_t REG_INT_STAT  = 0x02;
static constexpr uint8_t REG_KEY_LCK   = 0x03;
static constexpr uint8_t REG_KEY_EVENT = 0x04;
static constexpr uint8_t REG_KP_GPIO1  = 0x1D;
static constexpr uint8_t REG_KP_GPIO2  = 0x1E;
static constexpr uint8_t REG_KP_GPIO3  = 0x1F;
static constexpr uint8_t REG_GPI_EM1   = 0x20;
static constexpr uint8_t REG_GPI_EM2   = 0x21;
static constexpr uint8_t REG_GPI_EM3   = 0x22;
static constexpr uint8_t REG_GPIO_DIR1 = 0x23;
static constexpr uint8_t REG_GPIO_DIR2 = 0x24;
static constexpr uint8_t REG_GPIO_DIR3 = 0x25;
static constexpr uint8_t REG_GPIO_LVL1 = 0x26;
static constexpr uint8_t REG_GPIO_LVL2 = 0x27;
static constexpr uint8_t REG_GPIO_LVL3 = 0x28;
static constexpr uint8_t REG_GPIO_IEN1 = 0x1A;
static constexpr uint8_t REG_GPIO_IEN2 = 0x1B;
static constexpr uint8_t REG_GPIO_IEN3 = 0x1C;

static constexpr uint32_t I2C_HZ = 100000;

// ADV 的 7x8 物理矩阵换算到初代 Cardputer 的 4x14 逻辑矩阵后，键位与初代一致。
static const char KEYMAP[4][14] = {
    {'`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b'},
    {'\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\'},
    {0x01, 0x02, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '\n'},
    {0x03, 0x04, 0x05, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', ' '},
};

static inline void wr(uint8_t reg, uint8_t val) {
    m5::In_I2C.writeRegister8(TCA_ADDR, reg, val, I2C_HZ);
}

static inline uint8_t rd(uint8_t reg) {
    return m5::In_I2C.readRegister8(TCA_ADDR, reg, I2C_HZ);
}

bool AdvKeyboard::begin() {
    if (!m5::In_I2C.isEnabled()) {
        // M5.begin() 通常已经初始化了内部 I2C；这里只是兜底。
        if (!m5::In_I2C.begin(I2C_NUM_0, 8, 9)) return false;
    }

    if (!m5::In_I2C.scanID(TCA_ADDR)) return false;

    // 与 Adafruit_TCA8418::begin() 相同的初始化序列
    wr(REG_GPIO_DIR1, 0x00);
    wr(REG_GPIO_DIR2, 0x00);
    wr(REG_GPIO_DIR3, 0x00);
    wr(REG_GPI_EM1, 0xFF);
    wr(REG_GPI_EM2, 0xFF);
    wr(REG_GPI_EM3, 0xFF);
    wr(REG_GPIO_LVL1, 0x00);
    wr(REG_GPIO_LVL2, 0x00);
    wr(REG_GPIO_LVL3, 0x00);
    wr(REG_GPIO_IEN1, 0xFF);
    wr(REG_GPIO_IEN2, 0xFF);
    wr(REG_GPIO_IEN3, 0xFF);

    // 7 行 x 8 列
    wr(REG_KP_GPIO1, 0x7F);
    wr(REG_KP_GPIO2, 0xFF);
    wr(REG_KP_GPIO3, 0x00);

    // 清空上电遗留事件
    for (int i = 0; i < 16 && rd(REG_KEY_EVENT) != 0; ++i) {}
    wr(REG_INT_STAT, 0x03);

    // 打开 key-event 中断位（轮询时依然有效，FIFO 才会持续装事件）
    wr(REG_CFG, 0x01);

    _ok = true;
    return true;
}

void AdvKeyboard::emit(KeyEvent ev) {
    uint8_t next = (uint8_t)((_head + 1) % (sizeof(_queue) / sizeof(_queue[0])));
    if (next == _tail) return;  // 队列满，丢弃最旧的手感反而更稳
    _queue[_head] = ev;
    _head = next;
}

void AdvKeyboard::poll() {
    if (!_ok) return;

    for (int i = 0; i < 16; ++i) {
        uint8_t raw = rd(REG_KEY_EVENT);
        if (raw == 0) break;

        const bool pressed = (raw & 0x80) != 0;
        uint8_t key = (raw & 0x7F);
        if (key == 0) continue;
        --key;  // TCA8418 的键号从 1 开始

        const uint8_t hwRow = key / 10;
        const uint8_t hwCol = key % 10;
        if (hwRow > 6 || hwCol > 7) continue;

        // 换算到 4x14 逻辑矩阵（与 M5 官方库的 remap 一致）
        const uint8_t col = hwRow * 2 + (hwCol > 3 ? 1 : 0);
        const uint8_t row = (hwCol + 4) % 4;

        if (!pressed) continue;  // 只处理按下沿

        char c = KEYMAP[row][col];
        KeyEvent ev;
        switch (c) {
            case '\n': ev.kind = KEY_ENTER; break;
            case '`': ev.kind = KEY_ESC; break;
            case ';': ev.kind = KEY_UP; break;
            case '.': ev.kind = KEY_DOWN; break;
            case ',': ev.kind = KEY_LEFT; break;
            case '/': ev.kind = KEY_RIGHT; break;
            case '\b': ev.kind = KEY_BACKSPACE; break;
            case ' ': ev.kind = KEY_SPACE; break;
            case '\t': continue;
            default:
                if (c >= 0x20 && c < 0x7F) {
                    ev.kind = KEY_CHAR;
                    ev.ch = c;
                } else {
                    continue;  // fn/shift/ctrl 等修饰键
                }
        }
        emit(ev);
    }
}

bool AdvKeyboard::pop(KeyEvent& out) {
    if (_tail == _head) return false;
    out = _queue[_tail];
    _tail = (uint8_t)((_tail + 1) % (sizeof(_queue) / sizeof(_queue[0])));
    return true;
}
