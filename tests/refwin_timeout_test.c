#include <signal.h>
#include <sys/wait.h>
#ifndef REFWIN_SOURCE
#define REFWIN_SOURCE "../tools/refwin.c"
#endif
#define main refwin_tool_main
#include REFWIN_SOURCE
#undef main

#define T(c) do { if (!(c)) { fprintf(stderr,"refwin_timeout_test:%d: RED %s\n",__LINE__,#c); return 1; } } while (0)
int main(void)
{
    char csv_path[] = "build/refwin-timeout-csv-XXXXXX";
    char log_path[] = "build/refwin-timeout-log-XXXXXX";
    int csv_fd = mkstemp(csv_path), log_fd = mkstemp(log_path);
    T(csv_fd >= 0 && log_fd >= 0); close(csv_fd);
    pid_t pid = fork(); T(pid >= 0);
    if (!pid) {
        alarm(20);
        if (dup2(log_fd,STDERR_FILENO) < 0 || dup2(log_fd,STDOUT_FILENO) < 0) _exit(127);
        close(log_fd);
#ifdef REFWIN_BASELINE
        char *args[] = {"refwin","--csv",csv_path,"--pairs","1",NULL};
        exit(refwin_tool_main(5,args));
#else
        char *args[] = {"refwin","--csv",csv_path,"--pairs","1","--timeout-ms","1000","--synthetic-clock",NULL};
        exit(refwin_tool_main(8,args));
#endif
    }
    int status = 0; T(waitpid(pid,&status,0) == pid);
    T(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    T(lseek(log_fd,0,SEEK_SET) == 0);
    char log[8192]; ssize_t n = read(log_fd,log,sizeof log-1u); T(n >= 0);
    log[(size_t)n] = 0; close(log_fd); T(unlink(log_path) == 0);
    if (!strstr(log,"input timeout: no matching KeyPress received")) {
        fprintf(stderr,"refwin_timeout_test: RED missing input-stage diagnosis; actual log:\n%s",log);
        unlink(csv_path); return 1;
    }
    FILE *csv = fopen(csv_path,"r"); T(csv != NULL);
    char header[256]; T(fgets(header,sizeof header,csv) != NULL && !strcmp(header,REFPROTO_HEADER));
    T(fgetc(csv) == EOF && !ferror(csv)); T(fclose(csv) == 0); T(unlink(csv_path) == 0);
    puts("refwin_timeout_test: no-input timeout names missing KeyPress; CSV contains only header PASS");
    return 0;
}
