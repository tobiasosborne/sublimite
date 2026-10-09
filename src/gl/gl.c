/* P2.4b: mode selection only; the shared legacy/experimental renderer lives
 * with the candidates until the coordinator picks/promotes one. Keep the
 * shared include when force-adding variants/. Unset preserves the legacy path. */
#include "../../variants/P2.4b/renderer.inc"

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
