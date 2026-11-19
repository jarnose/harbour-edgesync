#!/usr/bin/env python3
"""Draws the app icon at the four Sailfish launcher sizes, in the style of Sailfish OS's own.

What makes a Sailfish launcher icon look like one (read off the system's own icons,
icon-launcher-*.png in the Silica theme, and Jolla's app-icon guidelines at
https://sailfishos.org/design/icons/):

  * the "leaf": a rounded square with three generous round corners and one sharp one;
  * a saturated diagonal gradient, light at the top left and deep at the bottom right;
  * one big, literal, flat glyph in cream or pale tints, filling most of the shape;
  * soft depth: a gentle gradient on the glyph, a blurred drop shadow, and a translucent
    lighter shape laid over the background.

Here: a bike computer showing a route, on a teal-to-green leaf. Drawn at 5x and scaled
down so the edges are smooth.

    python3 tools/make_icon.py        (needs Pillow)
"""
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageOps

SIZES = [86, 108, 128, 172]
N = 860  # 172 * 5


def gradient(size, c0, c1, angle_deg=135.0):
    """A linear gradient from c0 (top left) to c1 (bottom right) at the given angle.

    Built from two ramps (one along x, one along y) blended by the angle's cosine and
    sine, so that Pillow alone is enough.
    """
    import math
    h, w = size
    v = Image.linear_gradient("L").resize((w, h))      # 0 at the top, 255 at the bottom
    hz = v.transpose(Image.TRANSPOSE)                  # 0 at the left, 255 at the right
    wx, wy = math.cos(math.radians(angle_deg)), math.sin(math.radians(angle_deg))
    if wx < 0:
        hz, wx = ImageOps.invert(hz), -wx
    t = Image.blend(hz, v, wy / (wx + wy)) if wx + wy > 0 else v
    rgb = ImageOps.colorize(ImageOps.autocontrast(t), c0, c1)
    out = rgb.convert("RGBA")
    return out


def leaf_mask(size, radius, sharp="br"):
    """A rounded square with one sharp corner (tl, tr, br or bl)."""
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    corners = {"tl": (False, True, True, True), "tr": (True, False, True, True),
               "br": (True, True, False, True), "bl": (True, True, True, False)}[sharp]
    d.rounded_rectangle((0, 0, size - 1, size - 1), radius=radius, fill=255, corners=corners)
    return m


def shadowed(layer, offset, blur, opacity, color=(8, 40, 48)):
    """The layer over a blurred, offset copy of its own silhouette."""
    alpha = layer.getchannel("A")
    sh = Image.new("RGBA", layer.size, color + (0,))
    sh.putalpha(alpha.filter(ImageFilter.GaussianBlur(blur)).point(lambda v: int(v * opacity)))
    sh = ImageChops.offset(sh, 0, offset)
    out = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    out.alpha_composite(sh)
    out.alpha_composite(layer)
    return out


def draw():
    # ---- the leaf and its colour -------------------------------------------------
    bg = gradient((N, N), (52, 224, 168), (8, 112, 128), 125)

    # A lighter translucent circle over the top left, the way Sailfish's icons lay
    # one pale shape over another for depth.
    glow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((-int(N * .30), -int(N * .34), int(N * .62), int(N * .58)),
                                 fill=(255, 255, 255, 46))
    bg.alpha_composite(glow)
    # and a soft dark one at the bottom right, so the sharp corner sits in the shade
    dark = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(dark).ellipse((int(N * .45), int(N * .55), int(N * 1.3), int(N * 1.4)),
                                 fill=(0, 40, 60, 40))
    bg.alpha_composite(dark)

    # ---- the glyph: a bike computer ------------------------------------------------
    g = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    bx0, by0, bx1, by1 = int(N * .27), int(N * .12), int(N * .73), int(N * .88)
    body_mask = Image.new("L", (N, N), 0)
    ImageDraw.Draw(body_mask).rounded_rectangle((bx0, by0, bx1, by1), radius=int(N * .085), fill=255)
    body = gradient((N, N), (255, 252, 232), (236, 228, 190), 100)
    body.putalpha(body_mask)
    g.alpha_composite(body)

    # the screen: deep teal, with its own gentle gradient
    sx0, sy0, sx1, sy1 = bx0 + int(N * .04), by0 + int(N * .045), bx1 - int(N * .04), by0 + int(N * .545)
    screen_mask = Image.new("L", (N, N), 0)
    ImageDraw.Draw(screen_mask).rounded_rectangle((sx0, sy0, sx1, sy1), radius=int(N * .05), fill=255)
    screen = gradient((N, N), (16, 96, 108), (6, 56, 72), 110)
    screen.putalpha(screen_mask)
    g.alpha_composite(screen)

    # the route, drawn as a thick line that is paler than the screen and warmer at its end
    w, h = sx1 - sx0, sy1 - sy0
    pts = [(.14, .78), (.30, .50), (.20, .30), (.46, .26), (.60, .52), (.52, .74), (.80, .60), (.86, .22)]
    line = [(sx0 + int(w * x), sy0 + int(h * y)) for x, y in pts]
    route = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    rd = ImageDraw.Draw(route)
    rd.line(line, fill=(255, 250, 225, 255), width=int(N * .030), joint="curve")
    for (x, y) in line:  # round the joints
        r = int(N * .015)
        rd.ellipse((x - r, y - r, x + r, y + r), fill=(255, 250, 225, 255))
    # clip the route to the screen
    route.putalpha(ImageChops.multiply(route.getchannel("A"), screen_mask))
    g.alpha_composite(route)
    # start (cream) and finish (warm orange) marks
    for (x, y), col in ((line[0], (255, 250, 225, 255)), (line[-1], (255, 150, 60, 255))):
        r = int(N * .034)
        ImageDraw.Draw(g).ellipse((x - r, y - r, x + r, y + r), fill=col)
        r2 = int(N * .014)
        ImageDraw.Draw(g).ellipse((x - r2, y - r2, x + r2, y + r2), fill=(16, 96, 108, 255))

    # a speed readout under the screen, as three bars of different length
    ty = sy1 + int(N * .06)
    bars = [(.52, (120, 78, 160)), (.38, (120, 78, 160)), (.24, (120, 78, 160))]
    gd = ImageDraw.Draw(g)
    for i, (frac, col) in enumerate(bars):
        y0 = ty + i * int(N * .060)
        gd.rounded_rectangle((bx0 + int(N * .045), y0, bx0 + int(N * .045) + int((bx1 - bx0 - N * .09) * frac), y0 + int(N * .032)),
                             radius=int(N * .016), fill=col + (255,))
    # and a pair of buttons
    by = by1 - int(N * .068)
    for cx in (.40, .60):
        x = int(N * cx)
        r = int(N * .026)
        gd.ellipse((x - r, by - r, x + r, by + r), fill=(150, 140, 110, 255))

    g = shadowed(g, offset=int(N * .016), blur=int(N * .014), opacity=0.55)

    # ---- compose, then cut to the leaf --------------------------------------------
    bg.alpha_composite(g)
    out = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    out.paste(bg, (0, 0), leaf_mask(N, radius=int(N * .46), sharp="br"))
    return out


big = draw()
for s in SIZES:
    d = os.path.join("icons", f"{s}x{s}")
    os.makedirs(d, exist_ok=True)
    big.resize((s, s), Image.LANCZOS).save(os.path.join(d, "harbour-edgesync.png"))
print("wrote", SIZES)
