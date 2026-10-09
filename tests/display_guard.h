/* display_guard.h - force-included (-include) into every test and bench object by the
 * Makefile. Tests and benches must never open a window on the user's real display
 * (Tobias, 2026-10-09). Before main(): if DISPLAY names display 0 (":0", ":0.N",
 * "unix:0", "localhost:0") and EDIT_ALLOW_REAL_DISPLAY is unset, DISPLAY is redirected
 * to EDIT_DISPLAY (default ":99", the session's private Xvfb). Set
 * EDIT_ALLOW_REAL_DISPLAY=1 only for an explicit real-display bench pass. */
#ifndef EDIT_DISPLAY_GUARD_H
#define EDIT_DISPLAY_GUARD_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int edit_display_is_real(const char *d)
{
    const char *colon = strchr(d, ':');
    if (colon == NULL) return 0;
    size_t host = (size_t)(colon - d);
    if (host != 0 && !(host == 4 && strncmp(d, "unix", 4) == 0) &&
        !(host == 9 && strncmp(d, "localhost", 9) == 0)) return 0;
    return colon[1] == '0' && (colon[2] == '\0' || colon[2] == '.');
}

__attribute__((constructor)) static void edit_display_guard(void)
{
    const char *d = getenv("DISPLAY");
    if (d == NULL || getenv("EDIT_ALLOW_REAL_DISPLAY") != NULL) return;
    if (!edit_display_is_real(d)) return;
    const char *alt = getenv("EDIT_DISPLAY");
    if (alt == NULL || alt[0] == '\0' || edit_display_is_real(alt)) alt = ":99";
    (void)setenv("DISPLAY", alt, 1);
    (void)fprintf(stderr, "display guard: real display %s refused, using %s\n", d, alt);
}
#endif
