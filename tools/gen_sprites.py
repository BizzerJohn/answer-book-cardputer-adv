#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 32x32 像素精灵（原创形象，参考像素宝可梦的描边/明暗语言）。

规则（参考像素宝可梦教程）：
  * 32x32 画布，1px 描边，光源左上，右下压暗
  * 主色 + 阴影色 + 描边色 + 少量强调色，色数克制
  * 大头大眼、剪影优先、站姿略侧身

输出：src/apps/pocketpet/sprites.h
预览：tools/sprites_preview.png
"""

import os

from PIL import Image

W = H = 32
CH = {".": 0, "a": 1, "b": 2, "o": 3, "w": 4, "p": 5, "g": 6, "c": 7}
PREVIEW_COLORS = {
    0: (10, 16, 28),
    1: (242, 233, 216),
    2: (206, 192, 170),
    3: (30, 40, 60),
    4: (250, 248, 240),
    5: (22, 28, 42),
    6: (217, 164, 65),
    7: (40, 50, 72),
}

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "apps", "pocketpet", "sprites.h")
PREVIEW = os.path.join(ROOT, "tools", "sprites_preview.png")


def new():
    return [["." for _ in range(W)] for _ in range(H)]


def ell(g, cx, cy, rx, ry, c):
    for y in range(H):
        for x in range(W):
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                g[y][x] = c


def rect(g, x0, y0, w, h, c):
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            if 0 <= x < W and 0 <= y < H:
                g[y][x] = c


def tri(g, pts, c):
    (x0, y0), (x1, y1), (x2, y2) = pts
    minx, maxx = int(min(x0, x1, x2)), int(max(x0, x1, x2))
    miny, maxy = int(min(y0, y1, y2)), int(max(y0, y1, y2))
    for y in range(max(0, miny), min(H, maxy + 1)):
        for x in range(max(0, minx), min(W, maxx + 1)):
            d1 = (x - x1) * (y0 - y1) - (x0 - x1) * (y - y1)
            d2 = (x - x2) * (y1 - y2) - (x1 - x2) * (y - y2)
            d3 = (x - x0) * (y2 - y0) - (x2 - x0) * (y - y0)
            if (d1 >= 0 and d2 >= 0 and d3 >= 0) or (d1 <= 0 and d2 <= 0 and d3 <= 0):
                g[y][x] = c


def shade(g):
    mask = {(x, y) for y in range(H) for x in range(W) if g[y][x] in "ac"}
    for (x, y) in mask:
        if (x + 1, y + 1) not in mask:
            g[y][x] = "b"
    # 左上高光：贴边的一小段
    for (x, y) in list(mask):
        if g[y][x] == "a" and (x - 1, y - 1) not in mask:
            pass


def outline(g):
    mask = {(x, y) for y in range(H) for x in range(W) if g[y][x] != "."}
    border = set()
    for (x, y) in mask:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < W and 0 <= ny < H and g[ny][nx] == ".":
                border.add((nx, ny))
    for (x, y) in border:
        g[y][x] = "o"


def eye(g, cx, cy, wide=3, tall=4):
    # 宝可梦式的“大深色眼 + 左上一颗高光”，比白眼球更耐看
    ell(g, cx, cy, wide - 0.4, tall - 0.4, "p")
    g[cy - 1][cx - 1] = "w"
    g[cy - 1][cx - 2] = "w"


def mouth(g, x, y, smile=True):
    if smile:
        g[y][x] = "o"
        g[y][x + 1] = "o"
        g[y - 1][x - 1] = "o"
        g[y - 1][x + 2] = "o"
    else:
        g[y][x] = "o"
        g[y][x + 1] = "o"


# ---------------- 三阶：墨丸 / 砚兽 / 墨麟 ----------------
def mowan():
    g = new()
    ell(g, 7, 11, 4, 4, "a")
    ell(g, 25, 11, 4, 4, "a")
    tri(g, [(16, 9), (13, 2), (19, 3)], "g")  # 笔尖
    ell(g, 16, 20, 12, 11, "a")
    ell(g, 13, 12, 3.5, 2.5, "c")  # 墨斑
    shade(g)
    eye(g, 11, 18)
    eye(g, 21, 18)
    mouth(g, 15, 25)
    rect(g, 9, 29, 4, 3, "b")
    rect(g, 19, 29, 4, 3, "b")
    ell(g, 28, 26, 2, 3, "c")  # 墨滴尾巴
    outline(g)
    return g


def yanshou():
    g = new()
    ell(g, 17, 21, 12, 8, "a")          # 身体
    rect(g, 8, 26, 3, 5, "a")           # 腿
    rect(g, 13, 26, 3, 5, "a")
    rect(g, 20, 26, 3, 5, "a")
    rect(g, 25, 26, 3, 5, "a")
    ell(g, 23, 14, 7, 7, "a")           # 头
    tri(g, [(19, 9), (20, 4), (23, 8)], "a")   # 耳
    tri(g, [(27, 9), (29, 5), (30, 9)], "a")
    ell(g, 6, 19, 3, 2, "c")            # 尾巴
    shade(g)
    rect(g, 10, 12, 11, 6, "c")         # 背上的砚台
    rect(g, 10, 12, 11, 1, "g")
    eye(g, 22, 14, 2, 3)
    eye(g, 27, 14, 2, 3)
    mouth(g, 24, 18, False)
    g[6][20] = "g"                       # 金角
    g[6][29] = "g"
    outline(g)
    return g


def molin():
    g = new()
    ell(g, 16, 20, 8, 10, "a")           # 身体
    ell(g, 16, 9, 7, 7, "a")             # 头
    rect(g, 12, 28, 3, 4, "a")           # 腿
    rect(g, 17, 28, 3, 4, "a")
    ell(g, 7, 18, 3, 5, "a")             # 手臂
    ell(g, 25, 18, 3, 5, "a")
    tri(g, [(13, 3), (16, 0), (17, 5)], "g")   # 角
    tri(g, [(19, 3), (22, 0), (21, 5)], "g")
    rect(g, 9, 14, 14, 3, "c")           # 鬃毛
    ell(g, 16, 21, 3, 3, "g")            # 胸口金印
    ell(g, 16, 27, 5, 3, "b")            # 腹影
    shade(g)
    eye(g, 13, 9, 2, 3)
    eye(g, 19, 9, 2, 3)
    mouth(g, 15, 13)
    # 长尾
    for i, (x, y) in enumerate([(25, 22), (27, 24), (28, 27), (27, 30)]):
        rect(g, x, y, 2, 2, "c")
    outline(g)
    return g


# ---------------- 三个野生敌人 ----------------
def grass():
    g = new()
    ell(g, 16, 21, 10, 9, "a")
    ell(g, 16, 10, 2, 4, "a")
    tri(g, [(15, 9), (8, 4), (16, 6)], "a")
    tri(g, [(17, 9), (24, 4), (16, 6)], "a")
    shade(g)
    eye(g, 12, 20, 2, 3)
    eye(g, 20, 20, 2, 3)
    mouth(g, 15, 25)
    ell(g, 8, 26, 3, 2, "b")
    ell(g, 24, 26, 3, 2, "b")
    outline(g)
    return g


def fire():
    g = new()
    ell(g, 16, 22, 9, 8, "a")
    tri(g, [(16, 4), (9, 16), (16, 13)], "a")
    tri(g, [(16, 8), (11, 16), (21, 16)], "a")
    tri(g, [(16, 2), (13, 10), (19, 10)], "g")
    shade(g)
    eye(g, 12, 21, 2, 3)
    eye(g, 20, 21, 2, 3)
    mouth(g, 15, 26)
    ell(g, 9, 27, 3, 2, "b")
    ell(g, 23, 27, 3, 2, "b")
    outline(g)
    return g


def water():
    g = new()
    ell(g, 16, 19, 10, 10, "a")
    tri(g, [(6, 18), (2, 14), (7, 14)], "a")
    tri(g, [(26, 18), (30, 14), (25, 14)], "a")
    ell(g, 12, 14, 2, 2, "w")   # 泡泡高光
    shade(g)
    eye(g, 12, 19, 2, 3)
    eye(g, 20, 19, 2, 3)
    mouth(g, 15, 24)
    ell(g, 9, 27, 3, 2, "b")
    ell(g, 23, 27, 3, 2, "b")
    outline(g)
    return g


SPRITES = {
    "MOWAN": (mowan(), (7, 15, 19, 15, 4, 5)),
    "YANSHOU": (yanshou(), (20, 11, 25, 11, 3, 4)),
    "MOLIN": (molin(), (11, 6, 17, 6, 3, 4)),
    "GRASS": (grass(), (10, 17, 18, 17, 3, 4)),
    "FIRE": (fire(), (10, 18, 18, 18, 3, 4)),
    "WATER": (water(), (10, 16, 18, 16, 3, 4)),
}


def write_header():
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("// 自动生成，请勿手改。生成脚本：tools/gen_sprites.py\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("struct Sprite {\n")
        f.write("    const char* const* rows;\n")
        f.write("    uint8_t eyeX1, eyeY1, eyeX2, eyeY2, eyeW, eyeH;\n};\n\n")
        for name, (g, eye_box) in SPRITES.items():
            f.write(f"static const char* const {name}_ROWS[32] = {{\n")
            for row in g:
                f.write('    "' + "".join(row) + '",\n')
            f.write("};\n")
            f.write(f"static const Sprite {name} = {{{name}_ROWS, "
                    f"{eye_box[0]}, {eye_box[1]}, {eye_box[2]}, {eye_box[3]}, "
                    f"{eye_box[4]}, {eye_box[5]}}};\n\n")


TINTS = {
    "GRASS": {1: (90, 170, 90), 2: (66, 132, 70), 6: (170, 220, 120)},
    "FIRE": {1: (232, 120, 60), 2: (190, 84, 44), 6: (250, 210, 90)},
    "WATER": {1: (90, 150, 230), 2: (64, 112, 186), 6: (150, 220, 240)},
}


def write_preview():
    scale = 6
    cols = 3
    pad = 8
    cw = W * scale + pad
    chh = H * scale + pad
    img = Image.new("RGB", (cols * cw, 2 * chh), (10, 16, 28))
    for i, (name, (g, _)) in enumerate(SPRITES.items()):
        ox = (i % cols) * cw + pad // 2
        oy = (i // cols) * chh + pad // 2
        tint = TINTS.get(name, {})
        for y in range(H):
            for x in range(W):
                idx = CH[g[y][x]]
                c = tint.get(idx, PREVIEW_COLORS[idx])
                img.paste(c, (ox + x * scale, oy + y * scale,
                              ox + (x + 1) * scale, oy + (y + 1) * scale))
    img.save(PREVIEW)


def main():
    write_header()
    write_preview()
    print(f"已写入 {OUT}")


if __name__ == "__main__":
    main()
