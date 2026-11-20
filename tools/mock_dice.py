#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""骰子/硬币界面 1:1 预览。"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_font as g  # noqa: E402
from mock_ui import BG, BG2, GOLD, GOLD_D, MUTED, PAPER, SCALE, W, H, Font  # noqa: E402

LINE = (40, 52, 72)
INK = (30, 40, 60)


def die(d, x, y, size, face):
    d.rounded_rectangle([x, y, x + size, y + size], radius=size // 7, fill=PAPER,
                        outline=INK)
    d.line([x + 5, y + 2, x + size - 5, y + 2], fill=(220, 208, 190))
    a, b, c = size // 4, size // 2, size * 3 // 4
    pos = [(a, a), (a, b), (a, c), (b, b), (c, a), (c, b), (c, c)]
    masks = {1: [3], 2: [0, 6], 3: [0, 3, 6], 4: [0, 2, 4, 6], 5: [0, 2, 3, 4, 6],
             6: [0, 1, 2, 4, 5, 6]}
    r = max(2, size // 12)
    for i in masks[face]:
        px, py = pos[i]
        d.ellipse([x + px - r, y + py - r, x + px + r, y + py + r], fill=INK)


def home_dice(f16, f24, faces, count):
    canvas = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(canvas)
    for y in range(0, H, 4):
        d.line([0, y, W, y], fill=BG2)
    f16.draw(canvas, 58, 2, "骰子", GOLD)
    f16.draw(canvas, 142, 2, "硬币", MUTED)
    d.line([56, 20, 92, 20], fill=GOLD)
    d.line([8, 24, W - 8, 24], fill=LINE)
    small = ImageFont.load_default()
    d.text((8, 6), "DICE", fill=MUTED, font=small)
    d.text((W - 22, 6), f"x{count}", fill=MUTED, font=small)

    size = 46 if count == 1 else (42 if count == 2 else 34)
    gap = 8 if count == 3 else 12
    total = count * size + (count - 1) * gap
    x = (W - total) // 2
    y = 54 - size // 2
    for i in range(count):
        die(d, x + i * (size + gap), y, size, faces[i])

    if count == 1:
        f24.centered(canvas, W // 2, 96, str(faces[0]), PAPER)
    else:
        f24.centered(canvas, W // 2, 94, f"合计 {sum(faces)}", PAPER)

    d.text((W // 2 - 21 * 3, 124), "ENTER/SHAKE   < >:MODE   H:STAT",
           fill=MUTED, font=small)
    return canvas


def home_coin(f16, f24, zhi):
    canvas = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(canvas)
    for y in range(0, H, 4):
        d.line([0, y, W, y], fill=BG2)
    f16.draw(canvas, 58, 2, "骰子", MUTED)
    f16.draw(canvas, 142, 2, "硬币", GOLD)
    d.line([140, 20, 176, 20], fill=GOLD)
    d.line([8, 24, W - 8, 24], fill=LINE)
    small = ImageFont.load_default()
    d.text((8, 6), "COIN", fill=MUTED, font=small)

    cx, cy, r = W // 2, 62, 27
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=GOLD_D)
    d.ellipse([cx - r + 2, cy - r + 2, cx + r - 2, cy + r - 2], fill=GOLD)
    d.ellipse([cx - r + 6, cy - r + 6, cx + r - 6, cy + r - 6], fill=(250, 242, 214))
    d.ellipse([cx - r + 4, cy - r + 4, cx + r - 4, cy + r - 4], outline=(170, 120, 40))
    f24.centered(canvas, cx, cy - 12, "字" if zhi else "花", (120, 80, 20))
    d.text((W // 2 - 21 * 3, 124), "ENTER/SHAKE   < >:MODE   H:STAT",
           fill=MUTED, font=small)
    return canvas


def main():
    path = g.pick_font()
    chars = g.collect_chars()
    f16 = Font(16, *g.build(16, chars, path))
    f24 = Font(24, *g.build(24, chars, path))
    out = os.path.dirname(g.PREVIEW)

    home_dice(f16, f24, [3, 5], 2).resize((W * SCALE, H * SCALE), Image.NEAREST).save(
        os.path.join(out, "screen_dice.png"))
    home_coin(f16, f24, True).resize((W * SCALE, H * SCALE), Image.NEAREST).save(
        os.path.join(out, "screen_coin.png"))
    print("ok")


if __name__ == "__main__":
    main()
