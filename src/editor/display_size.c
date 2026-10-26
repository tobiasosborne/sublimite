#include "editor/runtime.h"
#include "editor/editor.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

uint32_t editor_runtime_em_px(double dpi, double points)
{
    if (!isfinite(dpi) || !isfinite(points) || dpi <= 0 || points <= 0) return 0;
    /* (P) round points * pixels/inch / 72 points/inch to nearest pixel.
     * (G) one positive whole pixel is the minimum accepted by the font sizing API. */
    double em = points * (dpi / 72.0);
    if (em >= EDITOR_FONT_MAX_PX) return EDITOR_FONT_MAX_PX;
    if (em < 1.0) return 1;
    return (uint32_t)floor(em + 0.5);
}
double editor_runtime_dpi(double xft, uint32_t width, uint32_t height,
                          uint32_t mm_width, uint32_t mm_height)
{
    if (isfinite(xft) && xft > 0) return xft;
    /* (P) diagonal DPI = diagonal pixels / (diagonal mm / 25.4 mm/inch).
     * Diagonals also preserve DPI for a rotated output. */
    if (width && height && mm_width && mm_height)
        return hypot((double)width, (double)height) * 25.4 /
               hypot((double)mm_width, (double)mm_height);
    return 96.0; /* (E) conventional Xft fallback when physical size is absent. */
}
double editor_runtime_xft_dpi(const char *resources, size_t len)
{
    if (!resources) return 0;
    double result = 0;
    size_t at = 0;
    while (at < len) {
        size_t end = at;
        while (end < len && resources[end] != '\n') end++;
        size_t key = at;
        while (key < end && (resources[key] == ' ' || resources[key] == '\t')) key++;
        size_t colon = key;
        while (colon < end && resources[colon] != ':') colon++;
        size_t key_end = colon;
        while (key_end > key && (resources[key_end-1] == ' ' || resources[key_end-1] == '\t')) key_end--;
        const char *names[] = {"Xft.dpi", "Xft* dpi", "Xft*dpi", "Xft.Dpi"};
        bool matches = false;
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
            if (key_end - key == strlen(names[i]) && !memcmp(resources + key, names[i], key_end - key)) matches = true;
        if (matches && colon < end) {
            /* A double's text fits DBL_DECIMAL_DIG significant digits plus
             * sign/exponent; this also accepts padded xrdb values. */
            char value[sizeof(double) * 8];
            size_t start = colon + 1;
            while (start < end && (resources[start] == ' ' || resources[start] == '\t')) start++;
            size_t stop = end;
            while (stop > start && (resources[stop-1] == ' ' || resources[stop-1] == '\t' || resources[stop-1] == '\r')) stop--;
            if (stop - start < sizeof value) {
                memcpy(value, resources + start, stop - start); value[stop - start] = '\0';
                char *tail = NULL; double dpi = strtod(value, &tail);
                if (tail != value && !*tail && isfinite(dpi) && dpi > 0) result = dpi;
            }
        }
        at = end < len ? end + 1 : end;
    }
    return result;
}
void editor_runtime_window(uint32_t work_width, uint32_t work_height,
                            uint32_t *width, uint32_t *height)
{
    /* (E) bead's launch policy: 60% width and 70% height, floor to pixels. */
    if (width) *width = (uint32_t)((uint64_t)work_width * 60u / 100u);
    if (height) *height = (uint32_t)((uint64_t)work_height * 70u / 100u);
}
