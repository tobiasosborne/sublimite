#include "gl/gl_driver.h"
#include <stdio.h>

#define T(c) do { if (!(c)) { fprintf(stderr,"gl_bench_test:%d: FAIL %s\n",__LINE__,#c); return 1; } } while (0)

/* Script only native admission. Real worker acquisition and benchmark owner
 * cleanup still run, without requiring an X socket or an EGL implementation. */
static int gl_bench_test_prepare(gl_driver *d,render_backend *b,render_config *cfg)
{
    memset(d,0,sizeof *d); d->backend=b;
    int rc=render_gl_backend(b);
    if (rc!=RENDER_OK) return rc;
    /* The lanes match gl_driver_prepare: bulk plus independent completion. */
    if (work_pool_init(&d->workers,1,1)!=0) return RENDER_ERR_INIT;
    d->pool_live=true; d->config=*cfg;
    const char *stage=getenv("EDIT_GL_TEST_DRIVER");
    return stage!=NULL && strcmp(stage,"prepare-error")==0 ? RENDER_ERR_INIT : RENDER_OK;
}
static int gl_bench_test_init(gl_driver *d)
{
    (void)d;
    const char *stage=getenv("EDIT_GL_TEST_DRIVER");
    return stage!=NULL && strcmp(stage,"init-error")==0 ? RENDER_ERR_INIT : RENDER_ERR_UNSUPPORTED;
}
#define main gl_bench_test_main
#define gl_driver_prepare gl_bench_test_prepare
#define gl_driver_init gl_bench_test_init
#include "../bench/gl_bench.c"
#undef gl_driver_init
#undef gl_driver_prepare
#undef main

int main(void)
{
    (void)gl_driver_prepare; (void)gl_driver_init;
    trace_init(); T(trace_thread_register()>=0);
    T(unsetenv("EDIT_GL_BENCH_FAIL")==0);
    const char *errors[]={"prepare-error","init-error"};
    for (size_t stage=0;stage<sizeof errors/sizeof errors[0];stage++) {
        T(setenv("EDIT_GL_TEST_DRIVER",errors[stage],1)==0);
        int rc=gl_bench_run(15,true,false,false,true);
        printf("gl_bench_test: stage=%s result=%d\n",errors[stage],rc);
        /* Existing benchmark convention: failure is EXIT_FAILURE; native
         * unsupported is the separate skip code checked below. */
        T(rc==EXIT_FAILURE);
    }
    T(unsetenv("EDIT_GL_TEST_DRIVER")==0);
    T(gl_bench_run(15,true,false,false,true)==2); /* Existing native skip code. */
    T(setenv("EDIT_GL_BENCH_FAIL","prepare",1)==0);
    T(gl_bench_run(15,true,false,false,true)==EXIT_FAILURE);
    T(unsetenv("EDIT_GL_BENCH_FAIL")==0);
    puts("gl_bench_test: PASS (driver errors fail, unsupported native skips, prepared worker cleanup)");
    return 0;
}
