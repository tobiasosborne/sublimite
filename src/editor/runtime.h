#ifndef EDIT_EDITOR_RUNTIME_H
#define EDIT_EDITOR_RUNTIME_H
#include <stddef.h>
/* Pure selection: any non-NULL allow opts in, matching the test guard.
 * NULL display stays NULL when allowed. Caller applies the returned setting. */
const char *editor_runtime_display(const char *display, const char *safe, const char *allow);
/* Resolve without creating: absolute XDG_CONFIG_HOME or HOME/.config.
 * Setup only; returns EDITOR_OK / EDITOR_ERR_ARG. */
int editor_config_dir(char *out, size_t cap);
#endif
