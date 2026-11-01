/* Exercise attachment without creating a console or touching a live window.
 * Native inherited pipe/file descriptors remain real. */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <string.h>
static unsigned input_repairs;
static BOOL test_attach(DWORD pid) { (void)pid; return TRUE; }
static FILE *test_reopen(const char *path, const char *mode, FILE *stream)
{
    if(!strcmp(path,"CONIN$")) { input_repairs++; return freopen("NUL",mode,stream); }
    return freopen(path,mode,stream);
}
static WCHAR **test_argv(const WCHAR *line, int *argc)
{ (void)line; return CommandLineToArgvW(L"sublimite.exe --help",argc); }
#define AttachConsole test_attach
#define freopen test_reopen
#define CommandLineToArgvW test_argv
#define main editor_program_main
#include "../../src/main.c"
#undef main
#undef CommandLineToArgvW
#undef freopen
#undef AttachConsole
#define T(x) do { if(!(x)) { fprintf(stderr,"editor_console:%d FAIL %s\n",__LINE__,#x); return 1; } } while(0)
int main(void)
{
    int original=_dup(_fileno(stdin));
    fclose(stdin);
    T(editor_program_main(0,NULL)==0);
    T(input_repairs==1 && _fileno(stdin)>=0);
    int pipes[2]; T(_pipe(pipes,256,_O_BINARY)==0);
    T(_write(pipes[1],"pipe",4)==4); _close(pipes[1]);
    T(_dup2(pipes[0],_fileno(stdin))==0); _close(pipes[0]);
    T(editor_program_main(0,NULL)==0 && input_repairs==1);
    char bytes[4]; T(_read(_fileno(stdin),bytes,4)==4 && !memcmp(bytes,"pipe",4));
    FILE *file=tmpfile(); T(file); T(fwrite("file",1,4,file)==4 && !fflush(file)); rewind(file);
    T(_dup2(_fileno(file),_fileno(stdin))==0);
    T(editor_program_main(0,NULL)==0 && input_repairs==1);
    T(_read(_fileno(stdin),bytes,4)==4 && !memcmp(bytes,"file",4)); fclose(file);
    if(original>=0) { _dup2(original,_fileno(stdin)); _close(original); }
    puts("editor_console: attached missing stdin reconnects to CONIN$; inherited pipe/file preserved PASS");
    puts("SKIP (needs live desktop: physical console input and shell attachment)"); return 0;
}
