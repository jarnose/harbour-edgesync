#!/usr/bin/env python3
"""Draws the app icon at the four Sailfish launcher sizes.

A bike computer (rounded rectangle with a screen) with a route across its
screen. Drawn at 8x and scaled down so the edges are smooth.
"""
from PIL import Image, ImageDraw
import os

SIZES = [86, 108, 128, 172]
BIG = 1376  # 172 * 8


def draw():
    im = Image.new("RGBA", (BIG, BIG), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # background tile
    d.rounded_rectangle((0, 0, BIG - 1, BIG - 1), radius=BIG // 5, fill=(27, 45, 66, 255))
    # the device body
    bx0, by0, bx1, by1 = int(BIG * .27), int(BIG * .12), int(BIG * .73), int(BIG * .88)
    d.rounded_rectangle((bx0, by0, bx1, by1), radius=BIG // 14, fill=(236, 240, 244, 255))
    # the screen
    sx0, sy0, sx1, sy1 = bx0 + int(BIG * .035), by0 + int(BIG * .045), bx1 - int(BIG * .035), by1 - int(BIG * .17)
    d.rounded_rectangle((sx0, sy0, sx1, sy1), radius=BIG // 30, fill=(18, 32, 48, 255))
    # the route
    w = sx1 - sx0
    h = sy1 - sy0
    pts = [(.18, .80), (.30, .62), (.22, .45), (.45, .38), (.62, .52), (.55, .25), (.80, .18)]
    line = [(sx0 + int(w * x), sy0 + int(h * y)) for x, y in pts]
    d.line(line, fill=(80, 200, 255, 255), width=BIG // 36, joint="curve")
    r = BIG // 34
    for (x, y), col in ((line[0], (76, 175, 80, 255)), (line[-1], (229, 57, 53, 255))):
        d.ellipse((x - r, y - r, x + r, y + r), fill=col)
    # the buttons under the screen
    cy = by1 - int(BIG * .08)
    for cx in (.40, .50, .60):
        x = int(BIG * cx)
        rr = BIG // 40
        d.ellipse((x - rr, cy - rr, x + rr, cy + rr), fill=(120, 130, 140, 255))
    return im


big = draw()
for s in SIZES:
    out = os.path.join("icons", f"{s}x{s}")
    os.makedirs(out, exist_ok=True)
    big.resize((s, s), Image.LANCZOS).save(os.path.join(out, "harbour-edgesync.png"))
print("wrote", SIZES)
