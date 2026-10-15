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
    /* edit-6e2: --font-fallbacks takes a comma-separated chain, repeats keep
     * the last value, empty is an explicit empty chain. */
    char *chain[] = {"sublimite", "--font-fallbacks", "Noto Emoji,file:/x.ttf", "--font-fallbacks=Noto Emoji"};
    T(PARSE(chain, &args) == IPC_OK && !args.request.count);
    T(!strcmp(args.font_fallbacks, "Noto Emoji") && !args.font_family && !args.font_file);
    ipc_args_fini(&args); T(!args.font_fallbacks);
    char *empty_chain[] = {"sublimite", "--font-fallbacks="};
    T(PARSE(empty_chain, &args) == IPC_OK && args.font_fallbacks && !*args.font_fallbacks);
    ipc_args_fini(&args);
    char *missing_chain[] = {"sublimite", "--font-fallbacks"};
    T(PARSE(missing_chain, &args) == IPC_INVALID);
    puts("font_cli_test: family/file/fallbacks options, required values and option terminator passed");
    return 0;
}
