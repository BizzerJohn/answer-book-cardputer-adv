#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""口袋宠物界面 1:1 预览（用固件同一份点阵和同一套坐标）。

    "E:/13.miniconda/envs/mylearning/python.exe" tools/mock_pet.py
"""

import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_font as g  # noqa: E402
from mock_ui import BG, BG2, GOLD, GOLD_D, MUTED, PAPER, SCALE, W, H, Font  # noqa: E402

GREEN = (74, 222, 128)
RED = (248, 113, 113)
BLUE = (147, 197, 253)
LINE = (40, 52, 72)
INK = (25, 32, 48)
OUTLINE = (60, 72, 92)


def ellipse(d, cx, cy, rx, ry, fill, outline=None):
    d.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=fill, outline=outline)


def tri(d, p, fill):
    d.polygon(p, fill=fill)


def bar(d, x, y, w, h, v, col):
    d.rounded_rectangle([x, y, x + w, y + h], radius=2, outline=LINE)
    fw = int((w - 2) * max(0, min(100, v)) / 100)
    if fw > 0:
        d.rectangle([x + 1, y + 1, x + fw, y + h - 1], fill=col)


def pet(canvas, d, cx, footY, stage, mood, scale=1.0, t=0, sleeping=False):
    bob = 0 if sleeping else int(math.sin(t * 0.003) * 2)
    w = int((40 + stage * 8) * scale)
    h = int((36 + stage * 12) * scale)
    cy = footY - h // 2 + bob

    ellipse(d, cx, footY + 3, w // 2 + 3, 4, (17, 25, 40))

    if stage >= 1:
        tx, ty = cx + w // 2 - 2, cy + h // 5
        d.line([cx + w // 2 - 6, cy + h // 4, tx + 8, ty - 6], fill=GOLD_D)
        d.ellipse([tx + 9 - (3 + stage), ty - 7 - (3 + stage),
                   tx + 9 + (3 + stage), ty - 7 + (3 + stage)], fill=(28, 38, 58))

    if stage == 0:
        ex2, ey2 = w // 3, cy - h // 2 + 4
        ellipse(d, cx - ex2, ey2, 8, 8, PAPER, OUTLINE)
        ellipse(d, cx + ex2, ey2, 8, 8, PAPER, OUTLINE)
    elif stage == 1:
        tri(d, [(cx - w // 3, cy - h // 2 + 4), (cx - w // 3 - 6, cy - h // 2 - 16),
                (cx - w // 3 + 8, cy - h // 2 - 6)], PAPER)
        tri(d, [(cx + w // 3, cy - h // 2 + 4), (cx + w // 3 + 6, cy - h // 2 - 16),
                (cx + w // 3 - 8, cy - h // 2 - 6)], PAPER)
    else:
        tri(d, [(cx - w // 3, cy - h // 2 + 6), (cx - w // 3 - 4, cy - h // 2 - 18),
                (cx - w // 3 + 8, cy - h // 2 - 4)], GOLD)
        tri(d, [(cx + w // 3, cy - h // 2 + 6), (cx + w // 3 + 4, cy - h // 2 - 18),
                (cx + w // 3 - 8, cy - h // 2 - 4)], GOLD)

    ellipse(d, cx, cy, w // 2, h // 2, PAPER, OUTLINE)
    ellipse(d, cx - w // 6, cy - h // 2 + h // 5, w // 6, h // 8, (30, 40, 60))
    if stage >= 1:
        ellipse(d, cx + w // 4, cy + h // 5, w // 7, h // 9, (30, 40, 60))

    ex, ey = w // 5, cy - h // 12
    if sleeping:
        d.line([cx - ex - 5, ey, cx - ex + 5, ey], fill=(30, 40, 60))
        d.line([cx + ex - 5, ey, cx + ex + 5, ey], fill=(30, 40, 60))
    else:
        ellipse(d, cx - ex, ey, 6, 7, INK)
        ellipse(d, cx + ex, ey, 6, 7, INK)
        d.ellipse([cx - ex - 3, ey - 4, cx - ex + 1, ey], fill=(250, 248, 240))
        d.ellipse([cx + ex - 3, ey - 4, cx + ex + 1, ey], fill=(250, 248, 240))

    my = cy + h // 5
    if mood >= 55:
        d.line([cx - 4, my - 1, cx - 1, my + 2], fill=OUTLINE)
        d.line([cx - 1, my + 2, cx + 1, my + 2], fill=OUTLINE)
        d.line([cx + 1, my + 2, cx + 4, my - 1], fill=OUTLINE)
    else:
        d.line([cx - 6, my + 2, cx, my - 2], fill=OUTLINE)
        d.line([cx, my - 2, cx + 6, my + 2], fill=OUTLINE)

    ellipse(d, cx - w // 4, footY - 1, int(8 * scale) + 1, int(4 * scale) + 1, PAPER)
    ellipse(d, cx + w // 4, footY - 1, int(8 * scale) + 1, int(4 * scale) + 1, PAPER)


def ascii(d, x, y, s, color, small):
    d.text((x, y), s, fill=color, font=small)


def home(f16, f24, stage, mood):
    canvas = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(canvas)
    for y in range(0, H, 4):
        d.line([0, y, W, y], fill=BG2)
    small = ImageFont.load_default()

    f16.draw(canvas, 8, 2, "墨团", PAPER)
    ascii(d, 150, 6, "Lv.5", GOLD, small)
    ascii(d, 206, 6, "G12", MUTED, small)

    bar(d, 8, 24, 64, 6, 78, GOLD)
    bar(d, 88, 24, 64, 6, 64, GREEN)
    bar(d, 168, 24, 64, 6, 41, BLUE)
    ascii(d, 22, 32, "FOOD", MUTED, small)
    ascii(d, 104, 32, "MOOD", MUTED, small)
    ascii(d, 178, 32, "ENERGY", MUTED, small)

    pet(canvas, d, 120, 112, stage, mood, 1.0, 0)

    for i in range(2):
        x = 206 + i * 10
        tri(d, [(x, 120), (x - 5, 126), (x + 5, 126)], (90, 70, 40))

    ascii(d, 66, 126, "ENTER:MENU   ,/.:PET   S:SAVE", (72, 86, 108), small)
    return canvas


def battle(f16, f24):
    canvas = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(canvas)
    small = ImageFont.load_default()

    f16.draw(canvas, 8, 4, "野生的草团", PAPER)
    ascii(d, 120, 8, "Lv.5", MUTED, small)
    bar(d, 8, 22, 100, 5, 80, RED)

    f16.draw(canvas, 8, 42, "墨团", PAPER)
    ascii(d, 120, 46, "Lv.5", MUTED, small)
    bar(d, 8, 60, 100, 5, 100, GREEN)

    # 敌
    ex, ey = 190, 44
    ellipse(d, ex, ey + 2, 18, 3, (17, 25, 40))
    ellipse(d, ex, ey - 14, 18, 16, (90, 170, 90), (30, 40, 60))
    tri(d, [(ex - 12, ey - 26), (ex - 16, ey - 40), (ex - 4, ey - 29)], (90, 170, 90))
    tri(d, [(ex + 12, ey - 26), (ex + 16, ey - 40), (ex + 4, ey - 29)], (90, 170, 90))
    ellipse(d, ex - 6, ey - 16, 3, 3, INK)
    ellipse(d, ex + 6, ey - 16, 3, 3, INK)

    pet(canvas, d, 190, 96, 0, 70, 0.5, 0)

    f16.draw(canvas, 8, 70, "撞击！造成8点伤害", GOLD)

    moves = ["撞击", "墨弹", "休息", "逃跑"]
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
    f24 = Font(24, *g.build(24, chars, path))
    out = os.path.dirname(g.PREVIEW)

    for stage, name in [(0, "screen_pet_home.png"), (2, "screen_pet_stage2.png")]:
        img = home(f16, f24, stage, 70)
        img.resize((W * SCALE, H * SCALE), Image.NEAREST).save(os.path.join(out, name))

    img = battle(f16, f24)
    img.resize((W * SCALE, H * SCALE), Image.NEAREST).save(
        os.path.join(out, "screen_pet_battle.png"))
    print("ok")


if __name__ == "__main__":
    main()
