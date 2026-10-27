#!/usr/bin/env python3
"""Derive the sublimité palette from OKLCH (design hill-climb round 2).

Every token is (L, C, h) in OKLCH, converted to sRGB (P: Ottosson 2020 OKLab
matrices, sRGB transfer function, nearest integer of channel * 255). Prints the
C table for src/config/theme.c and the WCAG 2.2 contrast ratio of each token
against its background (P: (L1 + 0.05) / (L2 + 0.05) on relative luminance).
Run: python3 tools/mkpalette.py
"""
import math

# One neutral tint for every grey in both themes: (M) the window manager's
# dark title bar #222226 (edit-29y2, Mint-Y-Dark wm_bg) is OKLCH
# (0.2536, 0.0075, 285.9); the dark page keeps that exact colour so title bar,
# tab strip and page read as one surface, and every grey shares its hue.
NEUTRAL = {"dark": (285.9, 0.0075), "light": (285.9, 0.0060)}

# Lightness ladders (OKLCH L, 0..1). Order is the hierarchy of rubric C2:
# text > comment/muted > gutter > minimap > hairline.
DARK = {
    "editor.background": 0.2536, "chrome.background": 0.2536,
    "tab.active.background": 0.2536, "gutter.background": 0.2536,
    "editor.foreground": 0.915, "tab.active.foreground": 0.915,
    "chrome.foreground": 0.680, "syntax.comment": 0.690,
    "syntax.punctuation": 0.760, "syntax.operator": 0.760,
    "syntax.variable": 0.915, "gutter.foreground": 0.540,
    "minimap.foreground": 0.390,
}
LIGHT = {
    "editor.background": 0.990, "chrome.background": 0.968,
    "tab.active.background": 0.990, "gutter.background": 0.990,
    "editor.foreground": 0.230, "tab.active.foreground": 0.230,
    "chrome.foreground": 0.500, "syntax.comment": 0.520,
    "syntax.punctuation": 0.420, "syntax.operator": 0.420,
    "syntax.variable": 0.230, "gutter.foreground": 0.700,
    "minimap.foreground": 0.850,
}
# The one accent (caret, focus, current match edge, selection tint) and the
# syntax hues: five at equal lightness and low chroma (rubric C3).
ACCENT = {"dark": (0.730, 0.110, 250.0), "light": (0.530, 0.150, 255.0)}
SYNTAX_L = {"dark": 0.810, "light": 0.500}
SYNTAX_C = {"dark": 0.060, "light": 0.085}
SYNTAX_H = {"syntax.keyword": 300.0, "syntax.type": 300.0,
            "syntax.attribute": 300.0, "syntax.string": 145.0,
            "syntax.number": 70.0, "syntax.constant": 70.0,
            "syntax.function": 200.0}
# Tints over the page: (P) mix fraction of the accent (selection) or of a warm
# ochre (find match / bracket) into the background in OKLab.
SELECTION_MIX = {"dark": 0.30, "light": 0.22}
MATCH = {"dark": (0.780, 0.090, 85.0), "light": (0.700, 0.110, 85.0)}
MATCH_MIX = {"dark": 0.22, "light": 0.30}
ERROR = {"dark": (0.680, 0.120, 25.0), "light": (0.520, 0.150, 25.0)}
WARN = {"dark": (0.800, 0.100, 80.0), "light": (0.560, 0.110, 70.0)}

ORDER = ["editor.background", "editor.foreground", "gutter.background",
         "gutter.foreground", "caret.background", "caret.foreground",
         "selection.background", "selection.foreground", "chrome.background",
         "chrome.foreground", "tab.active.background", "tab.active.foreground",
         "minimap.foreground", "warning.foreground", "bracket.background",
         "error.foreground", "error.background", "syntax.comment",
         "syntax.keyword", "syntax.string", "syntax.number", "syntax.constant",
         "syntax.function", "syntax.type", "syntax.variable", "syntax.operator",
         "syntax.punctuation", "syntax.attribute"]


def oklch_to_oklab(L, C, h):
    r = math.radians(h)
    return L, C * math.cos(r), C * math.sin(r)


def oklab_to_srgb(L, a, b):
    l_ = L + 0.3963377774 * a + 0.2158037573 * b
    m_ = L - 0.1055613458 * a - 0.0638541728 * b
    s_ = L - 0.0894841775 * a - 1.2914855480 * b
    l, m, s = l_ ** 3, m_ ** 3, s_ ** 3
    rgb = (4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
           -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
           -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)

    def enc(x):
        x = min(max(x, 0.0), 1.0)
        return 12.92 * x if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055
    return tuple(round(enc(x) * 255) for x in rgb)


def srgb_to_oklab(rgb):
    def dec(c):
        c /= 255
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (dec(c) for c in rgb)
    l = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b) ** (1 / 3)
    m = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b) ** (1 / 3)
    s = (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b) ** (1 / 3)
    return (0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s)


def lum(rgb):
    def dec(c):
        c /= 255
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (dec(c) for c in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def ratio(x, y):
    a, b = sorted((lum(x), lum(y)), reverse=True)
    return (a + 0.05) / (b + 0.05)


def mix(fg, bg, t):
    p, q = srgb_to_oklab(fg), srgb_to_oklab(bg)
    return oklab_to_srgb(*(q[i] + (p[i] - q[i]) * t for i in range(3)))


def theme(name):
    ladder = DARK if name == "dark" else LIGHT
    h, c = NEUTRAL[name]
    t = {k: oklab_to_srgb(*oklch_to_oklab(L, c, h)) for k, L in ladder.items()}
    accent = oklab_to_srgb(*oklch_to_oklab(*ACCENT[name]))
    bg = t["editor.background"]
    t["caret.background"] = accent
    t["caret.foreground"] = bg
    t["selection.background"] = mix(accent, bg, SELECTION_MIX[name])
    t["selection.foreground"] = t["editor.foreground"]
    t["bracket.background"] = mix(oklab_to_srgb(*oklch_to_oklab(*MATCH[name])),
                                  bg, MATCH_MIX[name])
    for k, hue in SYNTAX_H.items():
        t[k] = oklab_to_srgb(*oklch_to_oklab(SYNTAX_L[name], SYNTAX_C[name], hue))
    t["error.foreground"] = oklab_to_srgb(*oklch_to_oklab(*ERROR[name]))
    t["error.background"] = mix(t["error.foreground"], bg, 0.18)
    t["warning.foreground"] = oklab_to_srgb(*oklch_to_oklab(*WARN[name]))
    return t


def hexs(rgb):
    return "0x%02x%02x%02x" % rgb


def main():
    for name in ("dark", "light"):
        t = theme(name)
        bg = t["editor.background"]
        print(f"/* {name} */")
        for k in ORDER:
            ref = t["chrome.background"] if k.startswith("chrome.f") else bg
            if k.endswith("background") or k == "caret.foreground":
                note = ""
            else:
                note = f"{ratio(t[k], ref):.2f}:1"
            if k in ("selection.background", "bracket.background"):
                note = f"text on it {ratio(t['editor.foreground'], t[k]):.2f}:1"
            print(f"    {{\"{k}\", {hexs(t[k])}}},  /* {note} */")


if __name__ == "__main__":
    main()
