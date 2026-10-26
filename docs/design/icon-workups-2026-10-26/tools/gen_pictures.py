"""Pictorial icon workups for sublimité (2026-10-26, round 2: "pictures of things").

Usage: python3 gen_pictures.py <out_dir>   (pycairo only)
Same tile and palette as gen.py; a gold is added for the salon objects. All
coordinates are drawing values on a 256-unit tile, not budgets.
"""
import sys, os, math
import cairo

TILE, CREAM, ACCENT, GOLD, ROCK = "#222226", "#efeae0", "#1f9ede", "#d9b45a", "#3a3a41"
out = sys.argv[1]
os.makedirs(out, exist_ok=True)

def rgb(h, a=1.0):
    h = h.lstrip("#"); return (int(h[0:2], 16) / 255, int(h[2:4], 16) / 255, int(h[4:6], 16) / 255, a)

def src(cr, colour, a=1.0): cr.set_source_rgba(*rgb(colour, a))

def rrect(cr, x, y, w, h, r):
    cr.new_sub_path()
    cr.arc(x + w - r, y + r, r, -math.pi / 2, 0); cr.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    cr.arc(x + r, y + h - r, r, math.pi / 2, math.pi); cr.arc(x + r, y + r, r, math.pi, 3 * math.pi / 2)
    cr.close_path()

def smooth(cr, pts, start=True):
    """Catmull-Rom through pts as cubic Béziers (move_to first point when start)."""
    if start: cr.move_to(*pts[0])
    else: cr.line_to(*pts[0])
    for i in range(len(pts) - 1):
        p0 = pts[max(i - 1, 0)]; p1 = pts[i]; p2 = pts[i + 1]; p3 = pts[min(i + 2, len(pts) - 1)]
        cr.curve_to(p1[0] + (p2[0] - p0[0]) / 6, p1[1] + (p2[1] - p0[1]) / 6,
                    p2[0] - (p3[0] - p1[0]) / 6, p2[1] - (p3[1] - p1[1]) / 6, p2[0], p2[1])

def tile(cr):
    rrect(cr, 8, 8, 240, 240, 52); src(cr, TILE); cr.fill()

def clip_tile(cr):
    rrect(cr, 8, 8, 240, 240, 52); cr.clip()

def border(cr):
    rrect(cr, 8.5, 8.5, 239, 239, 51.5); cr.set_source_rgba(1, 1, 1, .08); cr.set_line_width(1); cr.stroke()

# Left profile, top to bottom: forehead, brow, eye, nose tip, nose base, lips, chin, throat.
PROFILE = [(84, 44), (88, 64), (97, 80), (93, 90), (99, 99), (119, 117), (117, 122), (103, 127),
           (109, 134), (103, 140), (110, 147), (100, 157), (104, 169), (92, 181), (86, 198)]

def profiles(cr):
    """Two people gaze deep into each other's eyes; the space between them is the exclamation."""
    cr.save(); clip_tile(cr)
    right = [(256 - x, y) for x, y in PROFILE]
    smooth(cr, PROFILE); smooth(cr, right[::-1], start=False); cr.close_path()
    src(cr, ACCENT); cr.fill()                                         # the gap, seen as a figure
    for pts, back in ((PROFILE, -1), (right, 1)):
        x0 = pts[0][0]
        smooth(cr, pts); cr.line_to(128 + back * 140, 198)
        cr.line_to(128 + back * 140, 70)
        cr.curve_to(128 + back * 120, 30, x0 + back * 40, 26, x0, 44)  # crown
        cr.close_path(); src(cr, CREAM); cr.fill()
    cr.arc(128, 222, 11, 0, 2 * math.pi); src(cr, ACCENT); cr.fill()  # the dot
    cr.restore()

def wanderer(cr):
    """The sublime: a figure on a peak above a sea of fog; the fog banks are lines of text."""
    cr.save(); clip_tile(cr)
    cr.arc(156, 96, 46, 0, 2 * math.pi); src(cr, CREAM, .92); cr.fill()  # pale sun
    # far ridge
    smooth(cr, [(8, 150), (60, 128), (96, 140), (140, 120), (200, 138), (248, 126)])
    cr.line_to(248, 248); cr.line_to(8, 248); cr.close_path(); src(cr, "#2c2c32"); cr.fill()
    # fog banks, left-aligned like code
    for y, x, w in ((150, 120, 110), (164, 140, 96), (178, 112, 128), (192, 150, 80), (206, 128, 108)):
        rrect(cr, x, y, w, 6, 3); src(cr, CREAM, .5); cr.fill()
    # foreground rock up to the peak
    smooth(cr, [(8, 196), (50, 186), (92, 176), (128, 160), (146, 152), (158, 156)])
    cr.line_to(176, 186); cr.line_to(150, 248); cr.line_to(8, 248); cr.close_path(); src(cr, ROCK); cr.fill()
    # the wanderer: coat, head, a stick; in the accent so he reads against sun and rock
    src(cr, ACCENT)
    cr.move_to(140, 153); cr.line_to(144, 120); cr.curve_to(146, 112, 156, 112, 158, 120)
    cr.line_to(162, 153); cr.close_path(); cr.fill()
    cr.arc(151, 106, 7.5, 0, 2 * math.pi); cr.fill()
    cr.set_line_width(3); cr.move_to(163, 126); cr.line_to(170, 156); cr.stroke()
    cr.restore()

def feather(cr):
    """A peacock feather, the Jugendstil eye: gaze and ornament."""
    cr.save(); clip_tile(cr)
    cr.translate(128, 128); cr.rotate(-0.55); cr.translate(-128, -128)
    # barbs: fine strokes fanning from the shaft
    cr.set_line_width(1.6)
    for i in range(46):
        t = i / 45; y = 52 + t * 170
        spread = 52 * math.sin(math.pi * min(1, (t * 1.15)))
        for side in (-1, 1):
            cr.move_to(128, y + 10); cr.curve_to(128 + side * spread * .4, y, 128 + side * spread * .9, y - 6, 128 + side * spread, y - 16)
        src(cr, "#5c8f6a", .55); cr.stroke()
    # the eye
    for rx, ry, colour in ((44, 56, GOLD), (32, 42, "#2a7f6f"), (22, 30, ACCENT), (12, 17, "#1a1a3a")):
        cr.save(); cr.translate(128, 92); cr.scale(rx, ry); cr.arc(0, 0, 1, 0, 2 * math.pi); cr.restore()
        src(cr, colour); cr.fill()
    # shaft
    cr.set_line_width(4); cr.move_to(128, 150); cr.curve_to(128, 190, 126, 220, 120, 250); src(cr, CREAM); cr.stroke()
    cr.restore()

def coupe(cr):
    """Quelle sublimité! a champagne coupe in the salon; the bubbles rise as an exclamation."""
    cr.save(); clip_tile(cr)
    # bowl: liquid, then a cream rim line
    cr.move_to(58, 112); cr.curve_to(66, 164, 190, 164, 198, 112); cr.close_path(); src(cr, GOLD); cr.fill()
    cr.set_line_width(5); cr.move_to(54, 110); cr.line_to(202, 110); src(cr, CREAM); cr.stroke()
    cr.set_line_width(3); cr.move_to(54, 110); cr.curve_to(62, 168, 194, 168, 202, 110); src(cr, CREAM); cr.stroke()
    # stem and foot
    cr.move_to(122, 152); cr.line_to(124, 206); cr.line_to(132, 206); cr.line_to(134, 152); cr.close_path(); cr.fill()
    cr.save(); cr.translate(128, 210); cr.scale(36, 7); cr.arc(0, 0, 1, 0, 2 * math.pi); cr.restore(); cr.fill()
    # bubbles: a rising column that ends as the exclamation's dot
    src(cr, ACCENT)
    for y, r in ((96, 4), (82, 5), (66, 6), (48, 7)):
        cr.arc(128, y, r, 0, 2 * math.pi); cr.fill()
    src(cr, CREAM, .8)
    for x, y in ((96, 124), (150, 130), (114, 138), (166, 120)):
        cr.arc(x, y, 3, 0, 2 * math.pi); cr.fill()
    cr.restore()

def lorgnette(cr):
    """A lorgnette: the salon looks; one lens catches a text line, the other the caret."""
    cr.save(); clip_tile(cr)
    # handle first, so the lens rim sits over its joint
    cr.set_line_cap(cairo.LINE_CAP_ROUND); src(cr, GOLD)
    cr.set_line_width(11); cr.move_to(190, 128); cr.line_to(214, 222); cr.stroke()
    cr.arc(215, 226, 8, 0, 2 * math.pi); cr.fill()
    for cx in (80, 164):
        cr.arc(cx, 96, 38, 0, 2 * math.pi); src(cr, "#1b2a33"); cr.fill_preserve()
        cr.set_line_width(9); src(cr, GOLD); cr.stroke()
        cr.arc(cx - 12, 84, 13, math.pi * 1.05, math.pi * 1.6); cr.set_line_width(4); src(cr, CREAM, .7); cr.stroke()
    cr.set_line_width(8); cr.move_to(112, 86); cr.curve_to(118, 72, 126, 72, 132, 86); src(cr, GOLD); cr.stroke()
    # what the lenses see
    rrect(cr, 60, 94, 30, 5, 2.5); src(cr, CREAM, .75); cr.fill()
    rrect(cr, 60, 104, 20, 5, 2.5); cr.fill()
    cr.rectangle(162, 84, 5, 26); src(cr, ACCENT); cr.fill()
    cr.restore()

CONCEPTS = [("06-profiles", profiles), ("07-wanderer", wanderer), ("08-peacock", feather),
            ("09-coupe", coupe), ("10-lorgnette", lorgnette)]

for name, draw in CONCEPTS:
    svg = cairo.SVGSurface(os.path.join(out, name + ".svg"), 256, 256)
    cr = cairo.Context(svg); tile(cr); draw(cr); border(cr); svg.finish()
    for px in (256, 64, 32, 16):
        img = cairo.ImageSurface(cairo.FORMAT_ARGB32, px, px)
        cr = cairo.Context(img); cr.scale(px / 256, px / 256); tile(cr); draw(cr); border(cr)
        img.write_to_png(os.path.join(out, f"{name}-{px}.png"))
print("wrote", len(CONCEPTS), "concepts to", out)
