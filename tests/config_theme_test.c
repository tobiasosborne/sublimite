/* edit-cxho.24: the palette keeps the tonal ladder of docs/design/rubric.md
 * C2/C3/C7 in both themes. Ratios are WCAG 2.2 contrast (P): relative
 * luminance of linearised sRGB, (L1 + 0.05) / (L2 + 0.05). Values are derived
 * in tools/mkpalette.py. */
#include "config/config.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define T(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

static double channel(uint32_t v)
{
    double c = (double)v / 255.0;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
static double luminance(uint32_t rgb)
{
    return 0.2126 * channel(rgb >> 16 & 0xffu) + 0.7152 * channel(rgb >> 8 & 0xffu) + 0.0722 * channel(rgb & 0xffu);
}
static double ratio(uint32_t a, uint32_t b)
{
    double x = luminance(a), y = luminance(b);
    return x > y ? (x + 0.05) / (y + 0.05) : (y + 0.05) / (x + 0.05);
}
static uint32_t c(config_theme t, const char *name) { return config_theme_color(t, name); }
/* A neutral: (P) channels within 12/255 of each other, so no grey carries hue. */
static int neutral(uint32_t rgb)
{
    int r = (int)(rgb >> 16 & 0xffu), g = (int)(rgb >> 8 & 0xffu), b = (int)(rgb & 0xffu);
    int hi = r > g ? (r > b ? r : b) : (g > b ? g : b), lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
    return hi - lo <= 12;
}

static void ladder(config_theme t)
{
    uint32_t bg = c(t, "editor.background");
    double text = ratio(c(t, "editor.foreground"), bg);
    double comment = ratio(c(t, "syntax.comment"), bg);
    double gutter = ratio(c(t, "gutter.foreground"), bg);
    double map = ratio(c(t, "minimap.foreground"), bg);
    double chrome = ratio(c(t, "chrome.foreground"), c(t, "chrome.background"));
    /* (P) WCAG 2.2 AA body text 4.5:1; rubric C2 anchor 5 asks comments too. */
    T(text >= 7.0 && comment >= 4.5 && chrome >= 4.5);
    /* Rubric C2 ladder: text > comment > gutter > minimap; minimap below
     * 2:1 and gutter between 2.2 and 3.2 (workups README §01 targets 2.7). */
    T(text > comment && comment > gutter && gutter > map);
    T(map < 2.0 && gutter >= 2.2 && gutter <= 3.2);
    /* The active tab is continuous with the page (rubric C5 anchor 5). */
    T(c(t, "tab.active.background") == bg && c(t, "gutter.background") == bg);
    /* Every syntax hue is readable and the five hues sit at one lightness:
     * (P) luminance spread within a factor 1.6 of each other. */
    static const char *const hues[] = {"syntax.keyword", "syntax.string", "syntax.number", "syntax.function", "syntax.type"};
    double lo = 1.0, hi = 0.0;
    for (size_t i = 0; i < sizeof hues / sizeof *hues; i++) {
        T(ratio(c(t, hues[i]), bg) >= 4.5);
        double l = luminance(c(t, hues[i]));
        lo = l < lo ? l : lo; hi = l > hi ? l : hi;
    }
    T(hi <= lo * 1.6);
    /* Greys share one family and carry no hue: rubric C3 anchor 5. */
    static const char *const greys[] = {"editor.background", "editor.foreground", "chrome.background", "chrome.foreground",
                                        "gutter.foreground", "minimap.foreground", "syntax.comment", "syntax.punctuation", "syntax.operator"};
    for (size_t i = 0; i < sizeof greys / sizeof *greys; i++) T(neutral(c(t, greys[i])));
    /* Selection and find match keep text readable (rubric C9). */
    T(ratio(c(t, "editor.foreground"), c(t, "selection.background")) >= 7.0);
    T(ratio(c(t, "editor.foreground"), c(t, "bracket.background")) >= 7.0);
    /* (P) round 3: AAA body ink and AA chrome on both panel surfaces. */
    const char *surfaces[] = {"panel.background", "hover.background"};
    for (size_t i = 0; i < sizeof surfaces / sizeof *surfaces; i++) {
        T(c(t, surfaces[i]) != 0 && c(t, surfaces[i]) != bg);
        T(neutral(c(t, surfaces[i])));
        T(ratio(c(t, "editor.foreground"), c(t, surfaces[i])) >= 7.0);
        T(ratio(c(t, "chrome.foreground"), c(t, surfaces[i])) >= 4.5);
    }
    double hover = ratio(c(t, "tab.hover.foreground"), bg);
    T(hover > ratio(c(t, "chrome.foreground"), bg) && hover < text);
}

int main(void)
{
    ladder(CONFIG_DARK);
    ladder(CONFIG_LIGHT);
    if (failures) { fprintf(stderr, "config_theme_test: %d failures\n", failures); return 1; }
    puts("config_theme_test: dark and light ladders passed");
    return 0;
}
