"""Icon workups for sublimité (2026-10-26): five concepts as SVG (glyphs as paths) and PNG.

Usage: FONTCONFIG_FILE=<fonts.conf naming a dir with Bodoni Moda> python3 gen.py <out_dir>
Needs only pycairo. Colours: tile #222226 = Mint-Y-Dark wm_bg (the editor's dark surface,
edit-29y2); cream #efeae0 = the title-font sample paper; accent #1f9ede = Mint-Y-Dark-Aqua
accent_color. All coordinates are drawing values on a 256-unit tile, not budgets.
"""
import sys, os, math
import cairo

TILE, CREAM, ACCENT, DIM, LIFT = "#222226", "#efeae0", "#1f9ede", "#5a5a62", "#2e2e33"
FAMILY = "Bodoni Moda"
out = sys.argv[1]
os.makedirs(out, exist_ok=True)

def rgb(h, a=1.0):
    h = h.lstrip("#"); return (int(h[0:2], 16) / 255, int(h[2:4], 16) / 255, int(h[4:6], 16) / 255, a)

def rrect(cr, x, y, w, h, r):
    cr.new_sub_path()
    cr.arc(x + w - r, y + r, r, -math.pi / 2, 0); cr.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    cr.arc(x + r, y + h - r, r, math.pi / 2, math.pi); cr.arc(x + r, y + r, r, math.pi, 3 * math.pi / 2)
    cr.close_path()

def fill(cr, colour, a=1.0):
    cr.set_source_rgba(*rgb(colour, a)); cr.fill()

def glyph(cr, ch, cx, cy, height, colour, weight=cairo.FONT_WEIGHT_NORMAL, italic=True):
    """Glyph centred on (cx, cy), scaled so its ink box is `height` tall."""
    cr.save()
    cr.select_font_face(FAMILY, cairo.FONT_SLANT_ITALIC if italic else cairo.FONT_SLANT_NORMAL, weight)
    cr.set_font_size(100); cr.new_path(); cr.move_to(0, 0); cr.text_path(ch)
    x0, y0, x1, y1 = cr.path_extents(); path = cr.copy_path(); cr.new_path()
    s = height / (y1 - y0)
    cr.translate(cx - (x0 + x1) / 2 * s, cy - (y0 + y1) / 2 * s); cr.scale(s, s)
    cr.append_path(path); cr.restore()
    fill(cr, colour)

def tile(cr):
    rrect(cr, 8, 8, 240, 240, 52); fill(cr, TILE)
    rrect(cr, 8.5, 8.5, 239, 239, 51.5); cr.set_source_rgba(1, 1, 1, .08); cr.set_line_width(1); cr.stroke()

def empty_quote(cr):
    """„|“ the quotation with nothing in it: the exclamation, emptied, waiting for text."""
    glyph(cr, "„", 72, 178, 38, CREAM, cairo.FONT_WEIGHT_BOLD)
    glyph(cr, "“", 184, 78, 38, CREAM, cairo.FONT_WEIGHT_BOLD)
    rrect(cr, 120, 66, 16, 124, 3); fill(cr, ACCENT)

def gaze(cr):
    """»´« two gazes deep into each other's eyes; the é's accent is the raised brow."""
    glyph(cr, "»", 82, 150, 56, CREAM, cairo.FONT_WEIGHT_BOLD)
    glyph(cr, "«", 174, 150, 56, CREAM, cairo.FONT_WEIGHT_BOLD)
    glyph(cr, "´", 134, 90, 40, ACCENT, cairo.FONT_WEIGHT_BOLD)

def caret_bang(cr):
    """! reduced to a caret and a full stop: it states, it does not exclaim."""
    rrect(cr, 114, 40, 28, 124, 4); fill(cr, ACCENT)
    cr.arc(128, 200, 19, 0, 2 * math.pi); fill(cr, CREAM)

def e_acute(cr):
    """é, the wordmark's signature letter; the accent in the system accent colour."""
    glyph(cr, "e", 122, 152, 124, CREAM)
    glyph(cr, "´", 152, 58, 46, ACCENT)

def glass(cr):
    """The whole product in one tile: one surface, text, the minimap, nothing else."""
    rrect(cr, 36, 30, 72, 16, 5); fill(cr, LIFT)
    rrect(cr, 48, 36, 44, 4, 2); fill(cr, CREAM, .8)
    widths = [92, 120, 64, 0, 104, 136, 80, 0, 112, 56]
    indents = [0, 16, 16, 0, 0, 16, 32, 0, 16, 0]
    y = 64
    for w, ind in zip(widths, indents):
        if w: rrect(cr, 40 + ind, y, min(w, 140 - ind), 8, 4); fill(cr, CREAM, .78)
        y += 16
    cr.rectangle(122, 125, 5, 14); fill(cr, ACCENT)          # caret after line 5
    cr.rectangle(186, 44, 1.5, 168); fill(cr, DIM)           # minimap edge
    rrect(cr, 192, 48, 44, 74, 6); cr.set_source_rgba(1, 1, 1, .09); cr.fill()  # viewport
    y = 52
    for w, ind in zip(widths * 2, indents * 2):
        if w: rrect(cr, 198 + ind / 4, y, w / 5, 3, 1.5); fill(cr, CREAM, .55)
        y += 7.5

CONCEPTS = [("01-empty-quote", empty_quote), ("02-gaze", gaze), ("03-caret-bang", caret_bang),
            ("04-e-acute", e_acute), ("05-glass", glass)]

for name, draw in CONCEPTS:
    svg = cairo.SVGSurface(os.path.join(out, name + ".svg"), 256, 256)
    cr = cairo.Context(svg); tile(cr); draw(cr); svg.finish()
    for px in (256, 64, 32, 16):
        img = cairo.ImageSurface(cairo.FORMAT_ARGB32, px, px)
        cr = cairo.Context(img); cr.scale(px / 256, px / 256); tile(cr); draw(cr)
        img.write_to_png(os.path.join(out, f"{name}-{px}.png"))
print("wrote", len(CONCEPTS), "concepts to", out)
