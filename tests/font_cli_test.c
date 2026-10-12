/* (P) argument counts derive from each argv fixture; no workload budget. */
#include "ipc/ipc.h"
#include <stdio.h>
#include <string.h>
#define T(c) do { if (!(c)) { fprintf(stderr, "font_cli_test: FAIL %s\n", #c); return 1; } } while (0)
#define PARSE(a, out) ipc_parse_args((int)(sizeof(a) / sizeof((a)[0])), (a), (out))
int main(void)
{
    ipc_args args = {0};
    char *family[] = {"sublimite", "--font-family", "Go Mono"};
    T(PARSE(family, &args) == IPC_OK && !args.request.count);
    T(!strcmp(args.font_family, "Go Mono") && !args.font_file); ipc_args_fini(&args);
    char *file[] = {"sublimite", "--font-file=vendor/DejaVuSansMono.ttf#0"};
    T(PARSE(file, &args) == IPC_OK && !args.request.count);
    T(!strcmp(args.font_file, "vendor/DejaVuSansMono.ttf#0") && !args.font_family); ipc_args_fini(&args);
    char *both[] = {"sublimite", "--font-family=absent", "--font-file", "vendor/DejaVuSansMono.ttf#0", "--font-family=Go Mono"};
    T(PARSE(both, &args) == IPC_OK && !args.request.count);
    T(!strcmp(args.font_family, "Go Mono") && !strcmp(args.font_file, "vendor/DejaVuSansMono.ttf#0"));
    ipc_args_fini(&args); T(!args.font_family && !args.font_file);
    char *empty[] = {"sublimite", "--font-family="};
    T(PARSE(empty, &args) == IPC_OK && args.font_family && !*args.font_family); ipc_args_fini(&args);
    char *missing[] = {"sublimite", "--font-family"};
    T(PARSE(missing, &args) == IPC_INVALID);
    char *missing_file[] = {"sublimite", "--font-file", "--wait"};
    T(PARSE(missing_file, &args) == IPC_INVALID);
    char *unknown[] = {"sublimite", "--font-family-other=Go Mono"};
    T(PARSE(unknown, &args) == IPC_INVALID);
    char *terminated[] = {"sublimite", "--", "--font-family=Go Mono"};
    T(PARSE(terminated, &args) == IPC_OK && args.request.count);
    T(!args.font_family && !args.font_file);
    ipc_args_fini(&args);
    puts("font_cli_test: family/file options, required values and option terminator passed");
    return 0;
}
