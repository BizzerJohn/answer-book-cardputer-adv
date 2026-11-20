#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""口袋宠物界面 1:1 预览（同一份 32x32 精灵与点阵字库）。

    "E:/13.miniconda/envs/mylearning/python.exe" tools/mock_pet.py
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_font as g  # noqa: E402
import gen_sprites as gs  # noqa: E402
from mock_ui import BG, BG2, GOLD, GOLD_D, MUTED, PAPER, SCALE, W, H, Font  # noqa: E402

GREEN = (74, 222, 128)
RED = (248, 113, 113)
BLUE = (147, 197, 253)
LINE = (40, 52, 72)

TINTS = {
    "GRASS": {1: (90, 170, 90), 2: (66, 132, 70), 6: (170, 220, 120)},
    "FIRE": {1: (232, 120, 60), 2: (190, 84, 44), 6: (250, 210, 90)},
    "WATER": {1: (90, 150, 230), 2: (64, 112, 186), 6: (150, 220, 240)},
}


def sprite(canvas, name, x, y, scale, tint=None):
    grid, _ = gs.SPRITES[name]
    tint = tint or {}
    for row in range(32):
        for col in range(32):
            ch = grid[row][col]
            if ch == ".":
                continue
            idx = gs.CH[ch]
            color = tint.get(idx, gs.PREVIEW_COLORS[idx])
            canvas.paste(color, (x + col * scale, y + row * scale,
                                 x + (col + 1) * scale, y + (row + 1) * scale))


def bar(d, x, y, w, h, v, col):
    d.rounded_rectangle([x, y, x + w, y + h], radius=2, outline=LINE)
    fw = int((w - 2) * max(0, min(100, v)) / 100)
    if fw > 0:
        d.rectangle([x + 1, y + 1, x + fw, y + h - 1], fill=col)


def ascii(d, x, y, s, color, small):
    d.text((x, y), s, fill=color, font=small)


def home(f16, stage_sprite):
    canvas = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(canvas)
    for y in range(0, H, 4):
        d.line([0, y, W, y], fill=BG2)
    small = ImageFont.load_default()

    f16.draw(canvas, 8, 2, "墨丸", PAPER)
    ascii(d, 150, 6, "Lv.5", GOLD, small)
    ascii(d, 206, 6, "G12", MUTED, small)

    bar(d, 8, 24, 64, 6, 78, GOLD)
    bar(d, 88, 24, 64, 6, 64, GREEN)
    bar(d, 168, 24, 64, 6, 41, BLUE)
    ascii(d, 22, 32, "FOOD", MUTED, small)
    ascii(d, 104, 32, "MOOD", MUTED, small)
    ascii(d, 178, 32, "ENERGY", MUTED, small)

    sprite(canvas, stage_sprite, 120 - 32, 112 - 64, 2)

    for i in range(2):
        x = 206 + i * 10
        d.polygon([(x, 120), (x - 5, 126), (x + 5, 126)], fill=(90, 70, 40))

    ascii(d, 66, 126, "ENTER:MENU   ,/.:PET   S:SAVE", (72, 86, 108), small)
    return canvas


def battle(f16):
    canvas = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(canvas)
    small = ImageFont.load_default()

    f16.draw(canvas, 8, 4, "野生的野草团", PAPER)
    ascii(d, 120, 8, "Lv.7", MUTED, small)
    bar(d, 8, 22, 100, 5, 80, RED)
    ascii(d, 150, 4, "x2", (251, 191, 36), small)

    f16.draw(canvas, 8, 42, "墨丸", PAPER)
    ascii(d, 120, 46, "Lv.5", MUTED, small)
    bar(d, 8, 60, 100, 5, 100, GREEN)

    sprite(canvas, "GRASS", 200 - 16, 48 - 32, 1, TINTS["GRASS"])
    sprite(canvas, "MOWAN", 200 - 16, 100 - 32, 1)

    f16.draw(canvas, 8, 70, "会心！墨点！-13点", GOLD)

    moves = ["墨点", "泼墨", "静心", "逃跑"]
    for i, m in enumerate(moves):
        x = 6 + i * 58
        if i == 0:
            d.rounded_rectangle([x, 104, x + 54, 126], radius=4, fill=(24, 36, 56),
                                outline=GOLD_D)
        else:
            d.rounded_rectangle([x, 104, x + 54, 126], radius=4, outline=LINE)
        f16.centered(canvas, x + 27, 107, m, GOLD if i == 0 else MUTED)

    ascii(d, 74, 126, "< > SELECT  ENTER:GO", MUTED, small)
    return canvas


def main():
    path = g.pick_font()
    chars = g.collect_chars()
    f16 = Font(16, *g.build(16, chars, path))
    out = os.path.dirname(g.PREVIEW)

    for name, sprite_name in [("screen_pet_home.png", "MOWAN"),
                              ("screen_pet_stage2.png", "MOLIN")]:
        img = home(f16, sprite_name)
        img.resize((W * SCALE, H * SCALE), Image.NEAREST).save(os.path.join(out, name))

    img = battle(f16)
    img.resize((W * SCALE, H * SCALE), Image.NEAREST).save(
        os.path.join(out, "screen_pet_battle.png"))
    print("ok")


if __name__ == "__main__":
    main()
