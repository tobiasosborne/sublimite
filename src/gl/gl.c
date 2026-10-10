/* Production EGL renderer, including the existing opt-in upload modes. */
#include "renderer.inc"

static int gl_select_upload(gl_state *s)
{
    const char *upload=getenv("EDIT_GL_UPLOAD");
#ifdef GL_EXPERIMENT_UPLOAD
    if (upload==NULL) upload=GL_EXPERIMENT_UPLOAD;
#endif
    if (upload==NULL) return RENDER_OK;
    if (strcmp(upload,"subdata")==0) s->upload=GL_UPLOAD_SUBDATA;
    else if (strcmp(upload,"orphan")==0) s->upload=GL_UPLOAD_ORPHAN;
    else if (strcmp(upload,"persistent")==0) s->upload=GL_UPLOAD_PERSISTENT;
    else return RENDER_ERR_ARG;
    return RENDER_OK;
}
