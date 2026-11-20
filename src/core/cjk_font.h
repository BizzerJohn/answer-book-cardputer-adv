// 子集中文字库的绘制层：UTF-8 解码、1-bit 点阵绘制、按宽度折行。
#pragma once
#include <M5Unified.h>
#include <stdint.h>

struct FontGlyph {
    uint16_t cp;
    uint32_t offset;  // 在 bits 中的字节偏移
    uint8_t w;        // 字宽（也是步进）
};

struct FontSet {
    const uint8_t* bits;
    const FontGlyph* glyphs;
    uint16_t count;
    uint8_t height;
};

namespace cf {

// 两张字号，定义在 font_data.h（由 tools/gen_font.py 生成）
extern const FontSet font16;
extern const FontSet font24;

const FontSet& small();  // 16px
const FontSet& large();  // 24px

int charAdvance(uint32_t cp, const FontSet& fs);

// 解码一个 UTF-8 字符，返回消耗的字节数。
int nextChar(const char* s, uint32_t& cp);

// 洗牌动画用：按“同类字符”生成一个随机字形（CJK→CJK，ASCII→ASCII）。
uint32_t randomGlyph(uint32_t like, const FontSet& fs);
int textWidth(const char* utf8, const FontSet& fs);

// 以 (x, y) 为左上角绘制，返回绘制宽度。
int drawText(M5Canvas& g, int x, int y, const char* utf8, uint16_t color,
             const FontSet& fs);

// 以 cx 为水平中心绘制。
int drawCentered(M5Canvas& g, int cx, int y, const char* utf8, uint16_t color,
                 const FontSet& fs);

// 只画前 len 个字节（用于折行后的单行，避免把后面的内容一起画出来）。
int textWidthN(const char* utf8, int len, const FontSet& fs);
int drawTextN(M5Canvas& g, int x, int y, const char* utf8, int len,
              uint16_t color, const FontSet& fs);
int drawCenteredN(M5Canvas& g, int cx, int y, const char* utf8, int len,
                  uint16_t color, const FontSet& fs);

void drawGlyph(M5Canvas& g, uint32_t cp, int x, int y, uint16_t color,
               const FontSet& fs);

// 取字库中第 index 个字形的码点，用于洗牌动画。
uint32_t glyphCodeAt(int index, const FontSet& fs);

// 折行。lines 里存的是原字符串的字节区间。返回行数（最多 maxLines）。
struct LineSpan {
    uint16_t start;
    uint16_t len;
};
int wrapText(const char* utf8, const FontSet& fs, int maxWidth, LineSpan* lines,
             int maxLines);

// 某个码点是否以“避头”标点开头（不许出现在行首）。
bool isNoLineStart(uint32_t cp);

}  // namespace cf
