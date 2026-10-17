/* display_guard.h - force-included (-include) into every test and bench object by the
 * Makefile. Tests and benches must never open a window on the user's real display
 * (Tobias, 2026-10-09). Before main(): if DISPLAY names display 0 (":0", ":0.N",
 * "unix:0", "localhost:0") and EDIT_ALLOW_REAL_DISPLAY is unset, DISPLAY is redirected
 * to EDIT_DISPLAY (default ":99", the session's private Xvfb). Set
 * EDIT_ALLOW_REAL_DISPLAY=1 only for an explicit real-display bench pass.
 * Wayland names have no private-number convention: inherited WAYLAND_DISPLAY
 * and WAYLAND_SOCKET are cleared unless allowed; EDIT_WAYLAND_DISPLAY names
 * an explicitly selected private compositor. Always export
 * EDIT_REFUSE_REAL_DISPLAY for spawned production launchers. */
#ifndef EDIT_DISPLAY_GUARD_H
#define EDIT_DISPLAY_GUARD_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor/runtime.h"
#include "wayland/plat.h"
/* Use the production policy for both input and alternate-display validation.
 * It normalizes the numeric display after the final colon, independent of
 * host/transport spelling; zero and invalid/missing displays are refused. */
static int edit_display_is_real(const char *d)
{
    return editor_runtime_display(d, NULL, "") != d;
}

__attribute__((constructor)) static void edit_display_guard(void)
{
    /* (P) Presence enables refusal in production children, independently of
     * this constructor's retained explicit real-display bench opt-in. */
    (void)setenv("EDIT_REFUSE_REAL_DISPLAY", "1", 1);
    const char *wayland = getenv("WAYLAND_DISPLAY");
    const char *allow = getenv("EDIT_ALLOW_REAL_DISPLAY");
    if (!allow) {
        const char *safe = wayland_display_select(wayland, getenv("EDIT_WAYLAND_DISPLAY"), NULL);
        if (safe) (void)setenv("WAYLAND_DISPLAY", safe, 1);
        else (void)unsetenv("WAYLAND_DISPLAY");
        /* Inherited descriptor bypasses WAYLAND_DISPLAY, so clear it too.
         * Headless mocks install their private socketpair after this guard. */
        (void)unsetenv("WAYLAND_SOCKET");
    }
    const char *d = getenv("DISPLAY");
    if (d == NULL || getenv("EDIT_ALLOW_REAL_DISPLAY") != NULL) return;
    if (!edit_display_is_real(d)) return;
    const char *alt = getenv("EDIT_DISPLAY");
    if (alt == NULL || alt[0] == '\0' || edit_display_is_real(alt)) alt = ":99";
    (void)setenv("DISPLAY", alt, 1);
    (void)fprintf(stderr, "display guard: real display %s refused, using %s\n", d, alt);
}
#endif
