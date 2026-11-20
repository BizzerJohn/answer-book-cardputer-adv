#include "cjk_font.h"

#include <esp_system.h>

#include "font_data.h"

namespace cf {

const FontSet& small() { return font16; }
const FontSet& large() { return font24; }

// ---------- UTF-8 ----------

static int utf8Decode(const char* s, uint32_t& cp) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
    if (p[0] < 0x80) {
        cp = p[0];
        return 1;
    }
    if ((p[0] & 0xE0) == 0xC0) {
        cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        return 2;
    }
    if ((p[0] & 0xF0) == 0xE0) {
        cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return 3;
    }
    if ((p[0] & 0xF8) == 0xF0) {
        cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
             ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return 4;
    }
    cp = '?';
    return 1;
}

// ---------- 字形查找 ----------

static const FontGlyph* findGlyph(uint32_t cp, const FontSet& fs) {
    int lo = 0, hi = (int)fs.count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint16_t v = fs.glyphs[mid].cp;
        if (v == cp) return &fs.glyphs[mid];
        if (v < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

int charAdvance(uint32_t cp, const FontSet& fs) {
    if (const FontGlyph* g = findGlyph(cp, fs)) return g->w;
    // 缺字：用一个半角宽度的占位，避免整行错位
    return fs.height / 2;
}

int textWidth(const char* utf8, const FontSet& fs) {
    int w = 0;
    const char* p = utf8;
    while (*p) {
        uint32_t cp;
        p += utf8Decode(p, cp);
        w += charAdvance(cp, fs);
    }
    return w;
}

// ---------- 绘制 ----------

void drawGlyph(M5Canvas& g, uint32_t cp, int x, int y, uint16_t color,
               const FontSet& fs) {
    const FontGlyph* gl = findGlyph(cp, fs);
    if (!gl) gl = findGlyph('?', fs);
    if (!gl) return;

    const int bytesPerRow = (gl->w + 7) >> 3;
    const uint8_t* base = fs.bits + gl->offset;

    for (int row = 0; row < fs.height; ++row) {
        const uint8_t* line = base + row * bytesPerRow;
        int col = 0;
        while (col < gl->w) {
            // 跳过空白段
            while (col < gl->w && !(line[col >> 3] & (0x80 >> (col & 7)))) ++col;
            if (col >= gl->w) break;
            int runStart = col;
            while (col < gl->w && (line[col >> 3] & (0x80 >> (col & 7)))) ++col;
            g.drawFastHLine(x + runStart, y + row, col - runStart, color);
        }
    }
}

int drawText(M5Canvas& g, int x, int y, const char* utf8, uint16_t color,
             const FontSet& fs) {
    int cx = x;
    const char* p = utf8;
    while (*p) {
        uint32_t cp;
        p += utf8Decode(p, cp);
        drawGlyph(g, cp, cx, y, color, fs);
        cx += charAdvance(cp, fs);
    }
    return cx - x;
}

int drawCentered(M5Canvas& g, int cx, int y, const char* utf8, uint16_t color,
                 const FontSet& fs) {
    int w = textWidth(utf8, fs);
    drawText(g, cx - w / 2, y, utf8, color, fs);
    return w;
}

int nextChar(const char* s, uint32_t& cp) {
    return utf8Decode(s, cp);
}

uint32_t randomGlyph(uint32_t like, const FontSet& fs) {
    if (like < 0x2E80) {
        static const char pool[] = "abcdefghijklmnopqrstuvwxyz0123456789";
        return (uint32_t)pool[esp_random() % (sizeof(pool) - 1)];
    }
    for (int i = 0; i < 6; ++i) {
        uint32_t cp = glyphCodeAt((int)(esp_random() % fs.count), fs);
        if (cp >= 0x4E00 && cp <= 0x9FFF) return cp;
    }
    return 0x7684;  // “的”
}

uint32_t glyphCodeAt(int index, const FontSet& fs) {
    if (index < 0 || index >= (int)fs.count) return '?';
    return fs.glyphs[index].cp;
}

// ---------- 折行 ----------

bool isNoLineStart(uint32_t cp) {
    switch (cp) {
        case 0xFF0C:  // ，
        case 0x3002:  // 。
        case 0x3001:  // 、
        case 0xFF1B:  // ；
        case 0xFF1A:  // ：
        case 0xFF1F:  // ？
        case 0xFF01:  // ！
        case 0xFF09:  // ）
        case 0x300D:  // 」
        case 0x300F:  // 』
        case 0x3011:  // 】
        case 0xFF5D:  // ｝
        case 0x2026:  // …
            return true;
        default:
            return false;
    }
}

int wrapText(const char* utf8, const FontSet& fs, int maxWidth, LineSpan* lines,
             int maxLines) {
    int lineCount = 0;
    const char* p = utf8;

    while (*p && lineCount < maxLines) {
        const char* lineStart = p;
        const char* lastBreak = nullptr;  // 上一处可断行位置（字节指针）
        int width = 0;

        while (*p) {
            uint32_t cp;
            int n = utf8Decode(p, cp);
            int adv = charAdvance(cp, fs);

            if (width + adv > maxWidth && p > lineStart) {
                // 优先在“不是避头标点”的字符前断开
                const char* breakAt = p;
                if (isNoLineStart(cp) && lastBreak && lastBreak > lineStart) {
                    breakAt = lastBreak;
                }
                p = breakAt;
                break;
            }

            width += adv;
            p += n;

            uint32_t nextCp = 0;
            if (*p) {
                utf8Decode(p, nextCp);
                // 下一个字可以开新行时，记录断点
                if (!isNoLineStart(nextCp)) lastBreak = p;
            }
        }

        lines[lineCount].start = (uint16_t)(lineStart - utf8);
        lines[lineCount].len = (uint16_t)(p - lineStart);
        ++lineCount;
    }

    return lineCount;
}

}  // namespace cf
