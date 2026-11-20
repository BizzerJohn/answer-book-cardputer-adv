#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""易经结果页的 1:1 排版检查（用固件同一份点阵）。

    "E:/13.miniconda/envs/mylearning/python.exe" tools/mock_iching.py
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_font as g  # noqa: E402
from mock_ui import BG, GOLD, MUTED, PAPER, SCALE, W, H, Font  # noqa: E402

GOLD_D = (122, 90, 34)
LINE = (40, 52, 72)


def draw_hex(d, x, y_top, width, lines):
    for i in range(6):
        y = y_top + (5 - i) * 12
        if lines[i]:
            d.rectangle([x, y, x + width, y + 5], fill=PAPER)
        else:
            d.rectangle([x, y, x + width // 2 - 3, y + 5], fill=PAPER)
            d.rectangle([x + width // 2 + 3, y, x + width, y + 5], fill=PAPER)


def wrap(text, font, max_w, max_lines=4):
    lines, cur = [], ""
    for ch in text:
        if font.width(cur + ch) <= max_w:
            cur += ch
        else:
            lines.append(cur)
            cur = ch
            if len(lines) >= max_lines:
                break
    if cur and len(lines) < max_lines:
        lines.append(cur)
    return lines


def main():
    path = g.pick_font()
    chars = g.collect_chars()
    f16 = Font(16, *g.build(16, chars, path))
    f24 = Font(24, *g.build(24, chars, path))

    canvas = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(canvas)
    small = ImageFont.load_default()

    f16.draw(canvas, 8, 4, "易经 · 六爻", MUTED)
    d.text((W - 8 - 30, 8), "第 4 卦", fill=MUTED, font=small)
    d.line([0, 0, W, 0], fill=(14, 22, 38))
    d.line([8, 22, W - 8, 22], fill=LINE)

    draw_hex(d, 14, 38, 56, [0, 1, 0, 0, 0, 1])

    f24.draw(canvas, 88, 28, "山水蒙", PAPER)
    f16.draw(canvas, 88, 56, "上山 下水", MUTED)

    reading = "蒙昧待启，宜求教；忌自作聪明。"
    lines = wrap(reading, f16, W - 94)
    y = 78
    for ln in lines:
        f16.draw(canvas, 88, y, ln, MUTED)
        y += 18
    print("reading lines:", lines)

    d.text((W // 2 + 30 - len("NO CHANGE  ENTER:NEW") * 3, 124),
           "NO CHANGE  ENTER:NEW", fill=(72, 86, 108), font=small)

    canvas.resize((W * SCALE, H * SCALE), Image.NEAREST).save(
        os.path.join(os.path.dirname(g.PREVIEW), "screen_iching.png"))
    print("ok")


if __name__ == "__main__":
    main()
