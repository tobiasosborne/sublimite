#ifndef EDIT_IPC_CRT_WIN_H
#define EDIT_IPC_CRT_WIN_H
#if defined(_WIN32)
#include <stdlib.h>
#include <io.h>
#include <fcntl.h>
#include <stdint.h>
/* UCRT exports this entry; mingw's stdlib.h declares only the process-wide
 * variant. Never change another thread's invalid-parameter policy. */
_CRTIMP _invalid_parameter_handler __cdecl _set_thread_local_invalid_parameter_handler(_invalid_parameter_handler handler);
static inline void __cdecl ipc_crt_invalid(const wchar_t *expression, const wchar_t *function,
    const wchar_t *file_name, unsigned line, uintptr_t reserved)
{
    (void)expression; (void)function; (void)file_name; (void)line; (void)reserved;
}
static inline intptr_t ipc_crt_handle(int fd)
{
    _invalid_parameter_handler previous=_set_thread_local_invalid_parameter_handler(ipc_crt_invalid);
    intptr_t handle=_get_osfhandle(fd);
    (void)_set_thread_local_invalid_parameter_handler(previous); return handle;
}
static inline int ipc_crt_mode(int fd, int mode)
{
    _invalid_parameter_handler previous=_set_thread_local_invalid_parameter_handler(ipc_crt_invalid);
    int result=_setmode(fd,mode);
    (void)_set_thread_local_invalid_parameter_handler(previous); return result;
}
static inline int ipc_crt_read(int fd, void *bytes, unsigned size)
{
    _invalid_parameter_handler previous=_set_thread_local_invalid_parameter_handler(ipc_crt_invalid);
    int result=_read(fd,bytes,size);
    (void)_set_thread_local_invalid_parameter_handler(previous); return result;
}
#endif
#endif
