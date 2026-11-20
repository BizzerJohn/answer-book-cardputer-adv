#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
扫描 src/ 下所有源码里出现过的字符，生成 16px / 24px 两套 1-bit 点阵字体。

为什么不用 M5GFX 自带的 efontCN：
    efontCN 编译后体积以 MB 计，而 Launcher 初始给应用的分区只有 1.375MB，
    这个 App 只需要答案库里出现的那几百个字，子集化后两张字号合计 < 80KB。

用法（必须用 conda 环境里的 python，别用系统 python）：
    "E:/13.miniconda/envs/mylearning/python.exe" tools/gen_font.py
"""

import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
OUT = os.path.join(SRC, "core", "font_data.h")
PREVIEW = os.path.join(ROOT, "tools", "preview.png")

FONT_CANDIDATES = [
    r"C:\Windows\Fonts\msyh.ttc",    # 微软雅黑
    r"C:\Windows\Fonts\simhei.ttf",  # 黑体
    r"C:\Windows\Fonts\simsun.ttc",  # 宋体
]

SIZES = (16, 24)
THRESHOLD = 96  # 1-bit 二值化阈值


def pick_font():
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            return path
    sys.exit("找不到中文字体，请修改 FONT_CANDIDATES")


def collect_chars():
    chars = set(chr(c) for c in range(0x20, 0x7F))  # 可打印 ASCII
    pattern = re.compile(r"[\u0080-\uffff]")
    for root, _dirs, files in os.walk(SRC):
        for name in files:
            if not name.endswith((".c", ".cpp", ".h", ".hpp")):
                continue
            if name == "font_data.h":
                continue
            with open(os.path.join(root, name), encoding="utf-8") as f:
                text = f.read()
            # 跳过 // 注释和 /* */ 注释，避免把注释里的字也打进字库
            text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
            text = re.sub(r"//[^\n]*", "", text)
            chars.update(pattern.findall(text))
    # \u4e3b\u9898\u7b54\u6848\u5305\u662f\u7528\u6237\u5728 SD \u5361\u4e0a\u5199\u7684\uff0c\u5b57\u5e93\u5fc5\u987b\u8986\u76d6\u5e38\u7528\u5b57\u3002
    # GB2312 \u4e00\u7ea7\u5b57\u5e93 = 3755 \u4e2a\u6700\u5e38\u7528\u6c49\u5b57\uff0c\u8db3\u4ee5\u5e94\u4ed8\u7edd\u5927\u591a\u6570\u81ea\u5199\u7b54\u6848\u3002
    for hi in range(0xB0, 0xD8):
        for lo in range(0xA1, 0xFF):
            try:
                chars.add(bytes([hi, lo]).decode("gb2312"))
            except UnicodeDecodeError:
                pass

    chars.discard("\ufeff")
    return sorted(chars, key=ord)


def render_glyph(ch, size, font_path, cjk):
    cell_w = size if cjk else size // 2
    cell_h = size
    # 基线取 0.93em：微软雅黑的 ascent 约为 0.95em，取 0.87 会把字头切掉。
    # 汉字没有下伸部，所以可以放心把基线压到格子底部。
    baseline = int(round(size * 0.93))
    if cjk:
        font_size = size
    else:
        font_size = max(8, int(round(size * 0.74)))

    font = ImageFont.truetype(font_path, font_size)
    img = Image.new("L", (cell_w, cell_h), 0)
    draw = ImageDraw.Draw(img)
    # "ms" = 水平居中 + 基线对齐
    draw.text((cell_w / 2, baseline), ch, font=font, fill=255, anchor="ms")
    bits = bytearray((cell_w + 7) // 8 * cell_h)
    for y in range(cell_h):
        for x in range(cell_w):
            if img.getpixel((x, y)) >= THRESHOLD:
                bits[(y * ((cell_w + 7) // 8)) + (x // 8)] |= 0x80 >> (x % 8)
    # 空格没有墨迹，也保留一个空字形，保证排版宽度正确
    return cell_w, bytes(bits)


def build(size, chars, font_path):
    glyphs = []
    blob = bytearray()
    for ch in chars:
        cp = ord(ch)
        cjk = cp >= 0x2E80
        w, bits = render_glyph(ch, size, font_path, cjk)
        off = len(blob)
        blob.extend(bits)
        glyphs.append((cp, off, w))
    return glyphs, bytes(blob)


def fmt_bytes(data, per_line=16):
    lines = []
    for i in range(0, len(data), per_line):
        chunk = ", ".join(f"0x{b:02X}" for b in data[i:i + per_line])
        lines.append("    " + chunk + ",")
    return "\n".join(lines)


def write_header(fonts):
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("// 自动生成，请勿手改。生成脚本：tools/gen_font.py\n")
        f.write("// 1-bit 点阵，行优先，MSB 在前。\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("namespace cf {\n\n")
        for name, size, glyphs, blob in fonts:
            f.write(f"// {name}: {len(glyphs)} 个字形, {len(blob)} 字节\n")
            f.write(f"static const uint8_t {name}_BITS[] = {{\n")
            f.write(fmt_bytes(blob))
            f.write("\n};\n\n")
            f.write(f"static const FontGlyph {name}_GLYPHS[] = {{\n")
            for cp, off, w in glyphs:
                f.write(f"    {{{cp}, {off}, {w}}},\n")
            f.write("};\n\n")
            f.write(f"const FontSet {name.lower()} = {{{name}_BITS, {name}_GLYPHS, "
                    f"{len(glyphs)}, {size}}};\n\n")
        f.write("}  // namespace cf\n")


def bitmap_to_image(blob, off, w, h):
    img = Image.new("1", (w, h), 0)
    bpr = (w + 7) // 8
    for y in range(h):
        for x in range(w):
            if blob[off + y * bpr + (x // 8)] & (0x80 >> (x % 8)):
                img.putpixel((x, y), 1)
    return img


def make_preview(font_path, size, glyphs, blob):
    """用真正写进固件的那份点阵拼一张图，肉眼检查阈值和基线。"""
    chars = {cp: (off, w) for cp, off, w in glyphs}
    lines = ["答案之书", "值得一试 ABCdef123", "答案在别处，再想想。"]
    pad = 4
    scale = 4
    canvas = Image.new("RGB", (240 * scale, (len(lines) * (size + 4) + 8) * scale),
                       (11, 18, 32))
    y = 4
    for line in lines:
        x = pad
        for ch in line:
            cp = ord(ch)
            if cp not in chars:
                continue
            off, w = chars[cp]
            g = bitmap_to_image(blob, off, w, size)
            g = g.point(lambda v: 255 if v else 0).convert("L")
            g = g.resize((w * scale, size * scale), Image.NEAREST)
            tint = Image.merge("RGB", [g, g.point(lambda v: int(v * 0.92)),
                                       g.point(lambda v: int(v * 0.78))])
            canvas.paste(tint, (x * scale, y * scale))
            x += w
        y += size + 4
    canvas.save(PREVIEW)


def main():
    font_path = pick_font()
    chars = collect_chars()
    print(f"字体: {font_path}")
    print(f"字符数: {len(chars)}  其中 CJK: {sum(1 for c in chars if ord(c) >= 0x2E80)}")
    fonts = []
    for size in SIZES:
        glyphs, blob = build(size, chars, font_path)
        fonts.append((f"FONT{size}", size, glyphs, blob))
        print(f"  {size}px: 位图 {len(blob)/1024:.1f} KB, 字形表 {len(glyphs)*6/1024:.1f} KB")
    write_header(fonts)
    size, glyphs, blob = fonts[0][1], fonts[0][2], fonts[0][3]
    make_preview(font_path, size, glyphs, blob)
    print(f"已写入 {OUT}")
    print(f"预览图   {PREVIEW}")


if __name__ == "__main__":
    main()
