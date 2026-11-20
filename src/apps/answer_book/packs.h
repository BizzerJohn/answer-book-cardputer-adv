// SD 卡主题答案包。目录 /answerbook/*.txt，格式见 README。
// 没有 SD 卡或没有目录时自动退回内置答案库，不影响启动。
#pragma once
#include <stdint.h>

#include "answers.h"

namespace packs {

constexpr int MAX_ENTRIES = 96;
constexpr int TEXT_MAX    = 48;
constexpr int PACK_NAME_MAX    = 20;
constexpr int MAX_PACKS   = 8;

struct Entry {
    char zh[TEXT_MAX];
    char why[TEXT_MAX];
    Tone tone;
    uint16_t cats;
};

struct Info {
    char name[PACK_NAME_MAX];
};

extern Entry entries[MAX_ENTRIES];
extern int count;                              // 0 = 使用内置
extern char loadedName[PACK_NAME_MAX];
extern Info list[MAX_PACKS];
extern int listCount;

// 扫描 SD 卡上的 /answerbook/*.txt。可重复调用。
void scan();

// index = -1 表示卸载，回到内置答案库。
bool load(int index);

const char* name(int index);  // index -1 → "内置"

}  // namespace packs
