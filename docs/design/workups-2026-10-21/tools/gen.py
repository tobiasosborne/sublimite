#!/usr/bin/env python3
"""Mock-up generator for docs/design/workups-2026-10-21 (design-workups-s10).
Draws each workup once into a cairo SVGSurface and once into a 2880x1800 ImageSurface,
so SVG and PNG come from the same drawing code and glyphs are embedded as outlines.
Everything that is not a pixel-region element is drawn on the workup's cell grid."""
import cairo, math, re, os, sys, json

REPO = '/home/tobias/Projects/sublimite'
OUT = REPO + '/docs/design/workups-2026-10-21'
W, H = 2880, 1800
SRC = open(REPO + '/src/piece/piece.c').read().split('\n')
FIRST = 227  # 1-based first source line shown

# ---------- colour maths (P: WCAG 2.2 relative luminance) ----------
def rgb(h): h = h.lstrip('#'); return tuple(int(h[i:i+2], 16) for i in (0, 2, 4))
def hx(t): return '#%02X%02X%02X' % t
def lin(c):
    s = c / 255
    return s / 12.92 if s <= 0.04045 else ((s + 0.055) / 1.055) ** 2.4
def L(h): r, g, b = rgb(h); return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)
def cr(a, b):
    la, lb = L(a), L(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)
def mix(a, b, t):  # t of a over b, sRGB-space blend as the coverage blend does
    A, B = rgb(a), rgb(b)
    return hx(tuple(round(A[i] * t + B[i] * (1 - t)) for i in range(3)))

# ---------- palettes ----------
D = dict(bg='#0D0E10', line='#121314', chrome='#151618', hair='#252629', text='#DCDEE2',
         muted='#9C9EA4', comment='#7D8086', gutter='#56585D', gutter_act='#BCBEC2',
         accent='#6DB0F4', keyword='#BAA4E2', string='#99CE9A', number='#E9B082',
         function='#95CDE9', type='#BAA4E2', punct='#9C9EA4', amber='#E6B55D')
D['selection'] = mix(D['accent'], D['bg'], 0.27)
D['find'] = mix(D['amber'], D['bg'], 0.20)
D['minimap'] = '#3A3C41'
D['mm_view'] = '#1A1B1E'
LT = dict(bg='#FAFAF9', line='#F2F2EF', chrome='#F2F2EF', hair='#DFDEDB', text='#1F2227',
          muted='#5B5E63', comment='#6B6E74', gutter='#96989D', gutter_act='#383B40',
          accent='#1570D1', keyword='#7445B8', string='#2F7339', number='#9C5414',
          function='#1A6896', type='#7445B8', punct='#5B5E63', amber='#E6B55D')
LT['selection'] = mix(LT['accent'], LT['bg'], 0.20)
LT['find'] = mix(LT['amber'], LT['bg'], 0.35)
LT['minimap'] = '#C9CAC8'
LT['mm_view'] = '#EDEDEA'
# today's tokens (src/config/theme.c dark), for the "today" contrast row
TODAY = dict(bg='#20242C', chrome='#171B22', text='#D8DEE9', tab_act='#384356', muted='#8D98A9',
             comment='#A6ACB9', gutter='#78808E', keyword='#C695C6', string='#99C794',
             number='#F9AE58', function='#5FB4B4', minimap='#8091A8', selection='#425471')

# ---------- C tokenizer (by hand colouring, good enough for a mock) ----------
KW = set('static inline return if else for while do switch case break continue goto sizeof typedef struct union enum const volatile extern'.split())
TY = set('void int unsigned char size_t uint64_t uint32_t uint8_t uintptr_t node core node_pool slab_tail bool int64_t'.split())
TOK = re.compile(r'(/\*.*?\*/|/\*.*$|//.*$)|("(?:\\.|[^"])*")|(\b0x[0-9a-fA-F]+u?\b|\b\d+[uUlL]*\b)|(#\s*\w+)|([A-Za-z_]\w*)|(\s+)|(.)')
def tokens(line):
    out = []
    for m in TOK.finditer(line):
        t = m.group(0)
        if m.group(1): k = 'comment'
        elif m.group(2): k = 'string'
        elif m.group(3): k = 'number'
        elif m.group(4): k = 'keyword'
        elif m.group(5):
            rest = line[m.end():].lstrip()
            if t in KW: k = 'keyword'
            elif t in TY: k = 'type'
            elif rest.startswith('('): k = 'function'
            elif t.isupper() and len(t) > 1: k = 'number'  # macro constants read as constants
            else: k = 'text'
        elif m.group(6): k = 'ws'
        else: k = 'punct'
        out.append((t, k))
    return out

# ---------- drawing helpers ----------
class G:
    def __init__(s, ctx, cw, ch, face, em, x0=0, y0=0):
        s.c, s.cw, s.ch, s.face, s.em, s.x0, s.y0 = ctx, cw, ch, face, em, x0, y0
        s.setfont(face, em)
        fe = ctx.font_extents()
        s.asc, s.dsc = fe[0], fe[1]
    def setfont(s, face, em, bold=False):
        s.c.select_font_face(face, cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_BOLD if bold else cairo.FONT_WEIGHT_NORMAL)
        s.c.set_font_size(em)
        fo = cairo.FontOptions(); fo.set_antialias(cairo.ANTIALIAS_GRAY); fo.set_hint_style(cairo.HINT_STYLE_NONE)
        fo.set_hint_metrics(cairo.HINT_METRICS_OFF); s.c.set_font_options(fo)
    def col(s, h): r, g, b = rgb(h); s.c.set_source_rgb(r / 255, g / 255, b / 255)
    def rect(s, x, y, w, h, colr): s.col(colr); s.c.rectangle(x, y, w, h); s.c.fill()
    def cells(s, c0, r0, c1, r1, colr):  # half-open cell rectangle
        s.rect(s.x0 + c0 * s.cw, s.y0 + r0 * s.ch, (c1 - c0) * s.cw, (r1 - r0) * s.ch, colr)
    def base(s, row, rows=1):  # baseline for a glyph centred in `rows` cell rows
        return s.y0 + row * s.ch + (rows * s.ch - (s.asc + s.dsc)) / 2 + s.asc
    def text(s, col, row, t, colr, rows=1, bold=False):
        s.col(colr); y = s.base(row, rows)
        if bold: s.setfont(s.face, s.em, True)
        for i, chh in enumerate(t):
            if chh != ' ':
                s.c.move_to(s.x0 + (col + i) * s.cw, y); s.c.show_text(chh)
        if bold: s.setfont(s.face, s.em)
    def free(s, x, y, t, colr, face, em, bold=False):  # proportional text (pixel positioned)
        s.setfont(face, em, bold); s.col(colr); s.c.move_to(x, y); s.c.show_text(t)
        adv = s.c.text_extents(t)[4]; s.setfont(s.face, s.em); return adv
    def dot(s, col, row, colr, rows=1, r=None):
        r = r or max(6.0, s.cw * 0.3); s.col(colr)
        s.c.arc(s.x0 + (col + 0.5) * s.cw, s.y0 + (row + rows / 2) * s.ch, r, 0, 2 * math.pi); s.c.fill()

def code_rows(g, P, first_row, nrows, text_col, gut_digits_end, cols_end, caret, sel_word,
              line_fill=None, gutter_act=None, bar_caret=False, block_caret=False, hscroll=0, fill_from=0):
    """Draw source lines with syntax colours. caret=(row_index, col); sel_word highlights
    the first occurrence on the caret line as selection and the others as find matches."""
    maxc = cols_end - text_col
    for i in range(nrows):
        ln = FIRST + i; line = SRC[ln - 1] if ln - 1 < len(SRC) else ''
        line = line.replace('\t', '    ')
        r = first_row + i
        if line_fill and i == caret[0]:
            g.cells(fill_from, r, cols_end, r + 1, line_fill)
        # matches
        if sel_word:
            for m in re.finditer(r'\b%s\b' % re.escape(sel_word), line):
                if m.start() >= maxc: continue
                fill = P['selection'] if (i == caret[0] and m.end() == caret[1]) else P['find']
                g.cells(text_col + m.start(), r, text_col + min(m.end(), maxc), r + 1, fill)
        num = str(ln)
        gc = gutter_act if (gutter_act and i == caret[0]) else P['gutter']
        g.text(gut_digits_end - len(num), r, num, gc)
        x = 0
        for t, k in tokens(line):
            if k != 'ws':
                vis = t[:max(0, maxc - x)]
                if vis: g.text(text_col + x, r, vis, P.get(k, P['text']))
            x += len(t)
    cr_, cc = caret
    if block_caret:
        line = SRC[FIRST + cr_ - 1]
        chh = line[cc] if cc < len(line) else ' '
        g.cells(text_col + cc, first_row + cr_, text_col + cc + 1, first_row + cr_ + 1, P['accent'])
        g.text(text_col + cc, first_row + cr_, chh, P['bg'])
    if bar_caret:  # precomposed: 2 px bar at the cell's left edge in the accent colour
        g.rect(g.x0 + (text_col + cc) * g.cw, g.y0 + (first_row + cr_) * g.ch + 4, 2, g.ch - 8, P['accent'])

def minimap_today(g, P, x, y, w, h, first_line, nvis):
    g.rect(x, y, w, h, P['bg'])
    n = len(SRC); scale = h / n
    for i, line in enumerate(SRC):
        ln = len(line.replace('\t', '    ').rstrip())
        if ln and i % 2 == 0:
            g.rect(x, y + i * scale, min(ln, 80) * (73 / 80), max(1, scale), P['minimap'])
    vy = y + (first_line - 1) * scale
    c = g.c; g.col(P['mm_view']); c.rectangle(x, vy, w, nvis * scale); c.fill()

def minimap_texture(g, P, x, y, w, h, first_line, nvis, px_line=3):
    """W5: syntax-coloured text texture, 1 px per char, px_line per source line, 35 % over bg."""
    g.rect(x, y, w, h, P['bg'])
    span = int(h // px_line); start = max(0, first_line - 1 - (span - nvis) // 3)
    vy = y + (first_line - 1 - start) * px_line
    g.rect(x, vy, w, nvis * px_line, mix('#FFFFFF', P['bg'], 0.08))
    for j in range(span):
        i = start + j
        if i >= len(SRC): break
        cx = 0
        for t, k in tokens(SRC[i].replace('\t', '    ')):
            if k != 'ws':
                base = mix('#FFFFFF', P['bg'], 0.08) if vy <= y + j * px_line < vy + nvis * px_line else P['bg']
                g.rect(x + 4 + cx, y + j * px_line, min(len(t), max(0, w - 8 - cx)), px_line - 1,
                       mix(P.get(k, P['text']), base, 0.35))
            cx += len(t)
            if cx >= w - 8: break

def tabs_row(g, P, row, rows, tabs, active, chrome_bg, act_bg, x_col=0, pad=2, dot_inactive=None, hair=None, rounded=False, ui=None):
    """tabs: list of (name, dirty). Cell-aligned tabs; labels vertically centred across `rows`
    (split-glyph chrome labels when rows == 2)."""
    g.cells(0, row, 999, row + rows, chrome_bg)
    c = x_col; geo = []
    for i, (name, dirty) in enumerate(tabs):
        wcells = pad + len(name) + 1 + 2 + pad if not ui else None
        if ui:
            face, em = ui
            g.setfont(face, em); adv = g.c.text_extents(name)[4]; g.setfont(g.face, g.em)
            wcells = int(math.ceil((adv + 2 * pad * g.cw + 2.5 * g.cw) / g.cw))
        is_act = i == active
        if is_act:
            g.cells(c, row, c + wcells, row + rows, act_bg)
            if rounded:  # coverage glyphs: quarter-disc corners in the tab's first and last cell
                rr = 14; x0 = g.x0 + c * g.cw; x1 = g.x0 + (c + wcells) * g.cw; y0 = g.y0 + row * g.ch
                g.rect(x0, y0, rr, rr, chrome_bg); g.rect(x1 - rr, y0, rr, rr, chrome_bg)
                g.col(act_bg); g.c.arc(x0 + rr, y0 + rr, rr, math.pi, 1.5 * math.pi); g.c.line_to(x0 + rr, y0 + rr); g.c.fill()
                g.c.arc(x1 - rr, y0 + rr, rr, 1.5 * math.pi, 2 * math.pi); g.c.line_to(x1 - rr, y0 + rr); g.c.fill()
        elif hair:
            g.rect(g.x0 + c * g.cw, g.y0 + (row + rows) * g.ch - 2, wcells * g.cw, 2, hair)
        fg = P['text'] if is_act else P['muted']
        if ui:
            face, em = ui
            y = g.y0 + row * g.ch + rows * g.ch / 2 + em * 0.36
            g.free(g.x0 + (c + pad) * g.cw, y, name, fg, face, em)
        else:
            g.text(c + pad, row, name, fg, rows=rows)
        if dirty:
            g.dot(c + wcells - pad - 1, row, fg if is_act else (dot_inactive or P['muted']), rows=rows)
        geo.append((c, wcells)); c += wcells
    if hair:
        g.rect(g.x0 + c * g.cw, g.y0 + (row + rows) * g.ch - 2, W - (g.x0 + c * g.cw), 2, hair)
    return geo

def status_row(g, P, row, cols, left, right, bg, fg, sep_hair=None):
    g.cells(0, row, cols + 2, row + 1, bg)
    if sep_hair: g.rect(0, g.y0 + row * g.ch, W, 2, sep_hair)
    g.text(1, row, left, fg)
    g.text(cols - 1 - len(right), row, right, fg)

_l = SRC[FIRST + 9 - 1].replace('\t', '    ')
CARET = (9, re.search(r'\bslot\b', _l).end())
TABS = [('piece.c', True), ('piece.h', False), ('editor.c', False)]
STATUS_R = 'Ln 236, Col 31    Spaces: 4    UTF-8    LF    C'

# ---------- workups ----------
def w0_today(ctx, P):
    """Not used for output (baseline is a real screenshot); kept for reference."""

def w1(ctx, P):
    g = G(ctx, 20, 40, 'DejaVu Sans Mono', 34)
    g.rect(0, 0, W, H, P['bg'])
    # today's tab: one row, active tab uses tab.active.background, which W1 sets to bg
    g.cells(0, 0, 144, 1, P['chrome'])
    g.cells(0, 0, 16, 1, P['bg']); g.text(1, 0, 'piece.c', P['text'])
    g.text(15, 0, '|', P['text'])  # today's separator glyph, kept: tokens only
    code_rows(g, P, 1, 44, 5, 4, 136, CARET, 'slot', block_caret=True)
    minimap_today(g, P, 2720, 40, 160, 1760, FIRST, 44)

def w2(ctx, P):
    g = G(ctx, 20, 40, 'DejaVu Sans Mono', 34)
    g.rect(0, 0, W, H, P['bg'])
    tabs_row(g, P, 0, 2, TABS, 0, P['chrome'], P['bg'], x_col=0)
    code_rows(g, P, 2, 42, 7, 5, 144, CARET, 'slot', line_fill=P['line'],
              gutter_act=P['gutter_act'], bar_caret=True)
    status_row(g, P, 44, 144, 'src/piece/piece.c', STATUS_R, P['chrome'], P['muted'])

def w3(ctx, P):
    em, cw = 34, 17; ch = 49
    g = G(ctx, cw, ch, 'Iosevka', em)
    g.rect(0, 0, W, H, P['bg'])
    cols = W // cw; rows = H // ch
    tabs_row(g, P, 0, 2, TABS, 0, P['chrome'], P['bg'], pad=3, hair=P['hair'])
    code_rows(g, P, 2, rows - 3, 7, 5, cols, CARET, 'slot', line_fill=P['line'],
              gutter_act=P['gutter_act'], bar_caret=True)
    status_row(g, P, rows - 1, cols, 'src/piece/piece.c', STATUS_R, P['bg'], P['comment'], sep_hair=P['hair'])
    g.rect(0, rows * ch, W, H - rows * ch, P['bg'])  # surface_background margin

def w4(ctx, P):
    g = G(ctx, 20, 40, 'DejaVu Sans Mono', 34)
    g.rect(0, 0, W, H, P['bg'])
    RAIL = 22
    g.cells(0, 0, RAIL, 45, P['chrome'])
    g.text(2, 1, 'OPEN', P['comment'])
    files = [('piece.c', 'src/piece', True), ('piece.h', 'src/piece', False), ('editor.c', 'src/editor', False),
             ('render.h', 'src/render', False)]
    for i, (n, d, dirty) in enumerate(files):
        r = 2 + i
        if i == 0:
            g.cells(0, r, RAIL, r + 1, P['bg'])
            g.rect(0, r * 40 + 6, 4, 28, P['accent'])  # coverage glyph: 4 px bar in cell 0
        g.text(2, r, n, P['text'] if i == 0 else P['muted'])
        if dirty: g.dot(RAIL - 3, r, P['text'])
    g.text(2, 8, 'src/piece', P['comment'])
    for i, n in enumerate(['piece.c', 'piece.h', 'piece_test.h', 'STATUS.md']):
        g.text(4, 9 + i, n, P['muted'] if i else P['text'])
    g.text(2, 14, 'src/render', P['comment'])
    for i, n in enumerate(['render.c', 'render.h']):
        g.text(4, 15 + i, n, P['muted'])
    g.text(2, 43, 'Ctrl+P  switch', P['gutter'])
    # breadcrumb + status merged in row 0
    x0 = RAIL
    g.cells(x0, 0, 144, 1, P['bg'])
    crumb = 'src › piece › piece.c › pool_grow()'
    g.text(x0 + 2, 0, crumb, P['muted'])
    g.text(x0 + 2 + crumb.index('piece.c'), 0, 'piece.c', P['text'])
    g.dot(x0 + 2 + len(crumb) + 1, 0, P['text'])
    g.text(144 - 2 - len(STATUS_R), 0, STATUS_R, P['comment'])
    g.x0 = 0
    code_rows(g, P, 1, 44, RAIL + 7, RAIL + 5, 143, CARET, 'slot', line_fill=P['line'],
              gutter_act=P['gutter_act'], bar_caret=True, fill_from=RAIL)
    # overview ruler in the one pixel region: 1 cell wide, viewport + match ticks + caret tick
    x = 143 * 20; g.rect(x, 40, 20, 1760, P['bg'])
    n = len(SRC); sc = 1760 / n
    g.rect(x + 5, 40 + (FIRST - 1) * sc, 10, 44 * sc, '#34363B')
    for i, l in enumerate(SRC):
        if re.search(r'\bslot\b', l): g.rect(x + 4, 40 + i * sc, 12, 3, P['amber'])
    g.rect(x + 2, 40 + (FIRST + 8) * sc, 16, 2, P['accent'])

def w5(ctx, P):
    em, cw, ch = 34, 17, 49
    g = G(ctx, cw, ch, 'Iosevka', em)
    g.rect(0, 0, W, H, P['bg'])
    cols = W // cw; rows = H // ch
    UI = ('Inter', 27)
    tabs_row(g, P, 0, 2, TABS, 0, P['chrome'], P['bg'], pad=2, rounded=True, ui=UI)
    MMW = 110; mmc = int(math.ceil(MMW / cw))
    code_rows(g, P, 2, rows - 3, 7, 5, cols - mmc - 1, CARET, 'slot', line_fill=P['line'],
              gutter_act=P['gutter_act'], bar_caret=True)
    sy = (rows - 1) * ch
    g.cells(0, rows - 1, cols + 1, rows, P['bg']); g.rect(0, rows * ch, W, H - rows * ch, P['bg'])
    g.free(cw, sy + ch / 2 + 10, 'src/piece/piece.c', P['comment'], *UI)
    rx = W - cw
    for t in reversed(['Ln 236, Col 31', 'Spaces: 4', 'UTF-8', 'LF', 'C']):
        g.setfont(*UI); a = ctx.text_extents(t)[4]; g.setfont(g.face, g.em)
        rx -= a; g.free(rx, sy + ch / 2 + 10, t, P['comment'], *UI); rx -= 44
    minimap_texture(g, P, W - MMW, 2 * ch, MMW, (rows - 3) * ch, FIRST, rows - 3)
    # ---- overlay: command palette (new primitive: premultiplied RGBA overlay region) ----
    px, py, pw, ph, rad = 740, 150, 1400, 742, 18
    for k in range(28, 0, -1):  # soft shadow approximation
        a = 0.022 * (1 - k / 28) ** 1.4 * 1.6
        ctx.set_source_rgba(0, 0, 0, a)
        rr = rad + k; x, y, w_, h_ = px - k, py - k + 14, pw + 2 * k, ph + 2 * k
        ctx.new_path(); ctx.arc(x + rr, y + rr, rr, math.pi, 1.5 * math.pi); ctx.arc(x + w_ - rr, y + rr, rr, 1.5 * math.pi, 0)
        ctx.arc(x + w_ - rr, y + h_ - rr, rr, 0, .5 * math.pi); ctx.arc(x + rr, y + h_ - rr, rr, .5 * math.pi, math.pi); ctx.close_path(); ctx.fill()
    def rrect(x, y, w_, h_, r, colr):
        g.col(colr); ctx.new_path(); ctx.arc(x + r, y + r, r, math.pi, 1.5 * math.pi); ctx.arc(x + w_ - r, y + r, r, 1.5 * math.pi, 0)
        ctx.arc(x + w_ - r, y + h_ - r, r, 0, .5 * math.pi); ctx.arc(x + r, y + h_ - r, r, .5 * math.pi, math.pi); ctx.close_path(); ctx.fill()
    rrect(px - 2, py - 2, pw + 4, ph + 4, rad + 2, P['hair'])
    rrect(px, py, pw, ph, rad, '#18191C')
    g.free(px + 40, py + 70, 'pool', P['text'], 'Iosevka', 34)
    g.rect(px + 40 + 4 * 17 + 2, py + 38, 2, 44, P['accent'])
    g.rect(px, py + 108, pw, 2, P['hair'])
    items = [('pool_get', 'piece.c  247', 'function'), ('pool_grow', 'piece.c  227', 'function'),
             ('pool_free', 'piece.c  262', 'function'), ('pool_reserve_class', 'piece.c  242', 'function'),
             ('pool_trim_locked', 'piece.c  289', 'function'), ('pool_trim', 'piece.c  315', 'function'),
             ('pool_done', 'piece.c  320', 'function'), ('Go to Symbol in Project…', 'Ctrl+Shift+R', 'command')]
    for i, (n, where, kind) in enumerate(items):
        y = py + 130 + i * 74
        if i == 0: rrect(px + 16, y, pw - 32, 66, 10, '#22252B')
        x = px + 48
        if kind == 'function':
            x += g.free(x, y + 44, 'pool', P['accent'], 'Iosevka', 32)
            g.free(x, y + 44, n[4:], P['text'], 'Iosevka', 32)
        else:
            g.free(x, y + 44, n, P['muted'], *UI)
        g.setfont(*UI); a = ctx.text_extents(where)[4]; g.setfont(g.face, g.em)
        g.free(px + pw - 48 - a, y + 43, where, P['muted'], *UI)

WORKUPS = [('01-quiet-tokens', w1, True), ('02-calm-chrome', w2, True), ('03-type-and-air', w3, False),
           ('04-rail', w4, False), ('05-floating', w5, False)]

def render(slug, fn, P, name):
    d = OUT + '/' + slug; os.makedirs(d, exist_ok=True)
    svg = cairo.SVGSurface(d + '/%s.svg' % name, W, H)
    svg.set_document_unit(cairo.SVG_UNIT_PX)
    c = cairo.Context(svg); fn(c, P); svg.finish()
    img = cairo.ImageSurface(cairo.FORMAT_RGB24, W, H)
    c = cairo.Context(img); fn(c, P); img.write_to_png(d + '/%s.png' % name)

if __name__ == '__main__':
    only = sys.argv[1:] or None
    for slug, fn, light in WORKUPS:
        if only and slug[:2] not in only: continue
        render(slug, fn, D, 'dark')
        if light: render(slug, fn, LT, 'light')
        print('rendered', slug)
