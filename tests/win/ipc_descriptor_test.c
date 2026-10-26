#if defined(_WIN32)
#include "ipc/ipc.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
_CRTIMP _invalid_parameter_handler __cdecl _set_thread_local_invalid_parameter_handler(_invalid_parameter_handler handler);
static unsigned validations;
static int synthetic_fd=-1;
static bool synthetic_error;
static size_t synthetic_remaining;
static void __cdecl caller_fatal(const wchar_t *expression, const wchar_t *function,
    const wchar_t *file, unsigned line, uintptr_t reserved)
{
    (void)expression; (void)function; (void)file; (void)line; (void)reserved;
    fputs("ipc descriptor validation escaped to caller's fatal handler\n",stderr); ExitProcess(1);
}
/* This host's UCRT returns errors without invoking the handler on several
 * invalid descriptors. Model the documented validation callback as well, so
 * the regression does not depend on that CRT implementation detail. */
static void validation(void)
{
    _invalid_parameter_handler handler=_set_thread_local_invalid_parameter_handler(NULL);
    (void)_set_thread_local_invalid_parameter_handler(handler);
    validations++;
    if(handler) handler(NULL,NULL,NULL,0,0);
}
static intptr_t descriptor_handle(int fd)
{
    intptr_t handle=_get_osfhandle(fd);
    if(handle==-1) validation();
    return handle;
}
static int descriptor_mode(int fd, int mode)
{
    int rc=_setmode(fd,mode);
    if(rc<0) validation();
    return rc;
}
static int descriptor_read(int fd, void *data, unsigned size)
{
    if(fd==synthetic_fd) {
        if(synthetic_error) { errno=EIO; return -1; }
        size_t n=synthetic_remaining<size?synthetic_remaining:size;
        memset(data,0,n); synthetic_remaining-=n; return (int)n;
    }
    int rc=_read(fd,data,size);
    if(rc<0) validation();
    return rc;
}
#define _get_osfhandle descriptor_handle
#define _setmode descriptor_mode
#define _read descriptor_read
#include "../../src/ipc/ipc_win.c"
#include "../../src/ipc/args.c"
#undef _get_osfhandle
#undef _setmode
#undef _read
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"ipc_descriptor_test:%d: %s\n",__LINE__,#x); exit(1); } } while(0)
int main(void)
{
    _invalid_parameter_handler previous=_set_thread_local_invalid_parameter_handler(caller_fatal);
    int closed=_dup(1); CHECK(closed>=0 && _close(closed)==0);
    const int fds[]={-1,closed,INT_MAX};
    ipc_server server={0}; CHECK(ipc_server_init_isolated(&server)==IPC_OK);
    for(size_t i=0;i<sizeof fds/sizeof fds[0];i++) {
        char *av[]={"sublimite","-"}; ipc_args args={0};
        CHECK(ipc_parse_args(2,av,&args)==IPC_OK);
        CHECK(ipc_args_read_stdin(&args,fds[i])==IPC_IO); ipc_args_fini(&args);
        ipc_token token;
        CHECK(ipc_server_adopt_wait(&server,fds[i],&token)==IPC_INVALID);
        CHECK(ipc_launcher_wait(fds[i])==IPC_INVALID);
        CHECK(_set_thread_local_invalid_parameter_handler(caller_fatal)==caller_fatal);
    }
    int pipe[2]; CHECK(_pipe(pipe,IPC_RX_SMALL_SIZE,_O_TEXT)==0);
    synthetic_fd=pipe[0];
    for(unsigned failure=0;failure<2u;failure++) {
        char *av[]={"sublimite","-"}; ipc_args args={0};
        CHECK(ipc_parse_args(2,av,&args)==IPC_OK);
        synthetic_error=failure!=0;
        /* (P) One byte beyond the entire wire limit exceeds stdin capacity. */
        synthetic_remaining=IPC_MAX_WIRE+1u;
        CHECK(ipc_args_read_stdin(&args,pipe[0])==(failure?IPC_IO:IPC_LIMIT));
        CHECK(_setmode(pipe[0],_O_TEXT)==_O_TEXT);
        CHECK(_set_thread_local_invalid_parameter_handler(caller_fatal)==caller_fatal);
        ipc_args_fini(&args);
    }
    synthetic_fd=-1; CHECK(_close(pipe[0])==0 && _close(pipe[1])==0);
    CHECK(validations>0); ipc_server_fini(&server);
    (void)_set_thread_local_invalid_parameter_handler(previous);
    puts("ipc invalid CRT validation callback scoped; error returns and caller handler restored: ok");
    puts("ipc stdin mode restored after read failure and wire limit: ok");
    return 0;
}
#endif
