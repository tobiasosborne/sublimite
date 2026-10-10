#include "editor/editor.h"
#include "raster/raster.h"
#include "gl/gl.h"
#include <string.h>

int editor_backend_select(render_backend *backend, const char *name)
{
    if (!name || !strcmp(name, "gl")) return render_gl_backend(backend);
    if (!strcmp(name, "raster")) return render_cpu_backend(backend);
    return EDITOR_ERR_ARG;
}
