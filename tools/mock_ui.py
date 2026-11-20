#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""用同一份点阵数据，在电脑上把界面 1:1 画出来，用来检查排版。

这不是固件的一部分，只在改 UI 时用来「先看一眼」：

    "E:/13.miniconda/envs/mylearning/python.exe" tools/mock_ui.py
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_font as g  # noqa: E402

W, H = 240, 135
SCALE = 3


def c(r, gg, b):
    return (r, gg, b)


BG = c(10, 16, 28)
BG2 = c(14, 22, 38)
PAPER = c(242, 233, 216)
MUTED = c(122, 134, 153)
GOLD = c(217, 164, 65)
GOLD_D = c(122, 90, 34)
YES = c(74, 222, 128)


class Font:
    def __init__(self, size, glyphs, blob):
        self.size = size
        self.map = {cp: (off, w) for cp, off, w in glyphs}
        self.blob = blob
        self.img = {}

    def glyph(self, ch):
        cp = ord(ch)
        if cp not in self.map:
            cp = ord("?")
        if cp in self.img:
            return self.img[cp]
        off, w = self.map[cp]
        bpr = (w + 7) // 8
        im = Image.new("1", (w, self.size), 0)
        for y in range(self.size):
            for x in range(w):
                if self.blob[off + y * bpr + (x // 8)] & (0x80 >> (x % 8)):
                    im.putpixel((x, y), 1)
        self.img[cp] = im
        return im

    def width(self, s):
        return sum(self.glyph(ch).width for ch in s)

    def draw(self, canvas, x, y, s, color):
        for ch in s:
            im = self.glyph(ch).convert("L")
            im = im.point(lambda v: 255 if v else 0)
            tint = Image.merge("RGB", [
                im.point(lambda v: int(v * color[0] / 255)),
                im.point(lambda v: int(v * color[1] / 255)),
                im.point(lambda v: int(v * color[2] / 255)),
            ])
            canvas.paste(tint, (x, y), im)
            x += self.glyph(ch).width

    def centered(self, canvas, cx, y, s, color):
        self.draw(canvas, cx - self.width(s) // 2, y, s, color)


def draw_ascii(d, x, y, s, color, small=None):
    d.text((x, y), s, fill=color, font=small)


def cover(canvas, d, f16, f24):
    d.rectangle([0, 0, W, H], fill=BG)
    for y in range(0, H, 4):
        d.line([0, y, W, y], fill=BG2)
    d.rounded_rectangle([28, 8, 28 + 184, 8 + 119], radius=9, fill=c(24, 33, 50),
                        outline=GOLD_D)
    d.rounded_rectangle([29, 9, 29 + 182, 9 + 117], radius=8, outline=GOLD)
    d.line([50, 11, 50, 124], fill=GOLD_D)
    d.line([52, 11, 52, 124], fill=GOLD_D)

    f24.centered(canvas, 132, 26, "答案之书", PAPER)
    small = ImageFont.load_default()
    draw_ascii(d, 132 - len("THE BOOK OF ANSWERS") * 3, 58, "THE BOOK OF ANSWERS", MUTED, small)
    d.line([132 - 34, 74, 132 + 34, 74], fill=GOLD_D)
    f16.centered(canvas, 132, 82, "默念你的问题", MUTED)
    f16.centered(canvas, 132, 104, "按 ENTER 翻开", GOLD)
    draw_ascii(d, 6, 124, "70%", MUTED, small)


def answer(canvas, d, f16, f24, text, page, tone_word, tone_color):
    d.rectangle([0, 0, W, H], fill=BG)
    d.line([0, 0, W, 0], fill=BG2)
    f16.draw(canvas, 8, 4, "答案之书", MUTED)
    small = ImageFont.load_default()
    draw_ascii(d, W - 8 - 30, 8, f"P.{page:03d}", MUTED, small)
    d.line([8, 22, W - 8, 22], fill=c(40, 52, 72))
    d.rounded_rectangle([8, 28, W - 8, 28 + 82], radius=5, outline=c(30, 42, 60))

    lines = [text]
    line_h = 24 + 2
    start_y = (H - len(lines) * line_h) // 2 + 3
    if start_y < 30:
        start_y = 30
    for i, line in enumerate(lines):
        f24.centered(canvas, W // 2, start_y + i * line_h, line, tone_color)

    draw_ascii(d, 8, 124, tone_word, tone_color, small)
    hint = "ENTER:ASK   ,/.:HISTORY"
    draw_ascii(d, W // 2 + 8 - len(hint) * 3, 124, hint, c(72, 86, 108), small)


def main():
    path = g.pick_font()
    chars = g.collect_chars()
    sets = {}
    for size in (16, 24):
        glyphs, blob = g.build(size, chars, path)
        sets[size] = Font(size, glyphs, blob)

    canvas = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(canvas)

    # 封面
    cover(canvas, d, sets[16], sets[24])
    canvas.resize((W * SCALE, H * SCALE), Image.NEAREST).save(
        os.path.join(os.path.dirname(PREVIEW := g.PREVIEW), "screen_cover.png"))

    # 答案页
    canvas = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(canvas)
    answer(canvas, d, sets[16], sets[24], "值得一试", 37, "YES", YES)
    canvas.resize((W * SCALE, H * SCALE), Image.NEAREST).save(
        os.path.join(os.path.dirname(g.PREVIEW), "screen_answer.png"))

    print("ok")


if __name__ == "__main__":
    main()
