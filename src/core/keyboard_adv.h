// Cardputer ADV 键盘：TCA8418 (I2C 0x34)，轮询方式读取，不依赖 INT 引脚。
// 事件坐标会被换算成初代 Cardputer 的 4x14 逻辑矩阵，方便复用官方键位表。
#pragma once
#include <stdint.h>

enum KeyKind : uint8_t {
    KEY_NONE = 0,
    KEY_CHAR,
    KEY_ENTER,
    KEY_ESC,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_BACKSPACE,
    KEY_SPACE,
};

struct KeyEvent {
    KeyKind kind = KEY_NONE;
    char ch = 0;
};

class AdvKeyboard {
public:
    bool begin();
    // 每次主循环调用；内部会一次性取空 FIFO。
    void poll();
    bool pop(KeyEvent& out);
    bool started() const { return _ok; }

    // 持续按住状态（小游戏需要平滑移动）
    bool upHeld() const { return _upHeld; }
    bool downHeld() const { return _downHeld; }
    bool leftHeld() const { return _leftHeld; }
    bool rightHeld() const { return _rightHeld; }

private:
    void emit(KeyEvent ev);
    bool _ok = false;
    bool _upHeld = false;
    bool _downHeld = false;
    bool _leftHeld = false;
    bool _rightHeld = false;
    uint8_t _head = 0;
    uint8_t _tail = 0;
    KeyEvent _queue[24];
};
