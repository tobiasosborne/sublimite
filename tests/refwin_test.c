#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "editor/editor.h"

/* Compile the exact tool sources into the sanitizer test too. make check
 * does not depend on release tools and must work from a fresh build tree. */
#define main refwin_tool_main
#include "../tools/refwin.c"
#undef main
#define main keyinject_tool_main
#include "../tools/keyinject.c"
#undef main

static int test_rows(const char *path, bool paired)
{
    FILE *f = fopen(path, "r");
    if (!f) return 1;
    char line[512];
    int rc = 1;
    uint64_t prev_inject[2] = {0}, prev_t4[2] = {0}, prev_msc[2] = {0};
    uint32_t counts[2] = {0}, prev_frame[2] = {0};
    if (!fgets(line, sizeof line, f) ||
        strcmp(line, "pair_id,target,inject_ns,msc,t4_ns,t5_ns,t6_ns,frame_id,phase_ns,period_ns,actual_phase_ns\n") != 0) goto done;
    for (uint32_t i = 0; i < (paired ? 24u : 12u); i++) {
        uint32_t pair, frame;
        uint64_t inject, msc, t4, t5, t6, phase, period, actual;
        char target[32], extra;
        if (!fgets(line, sizeof line, f) ||
            sscanf(line, "%" SCNu32 ",%31[^,],%" SCNu64 ",%" SCNu64 ",%" SCNu64 ",%" SCNu64 ",%" SCNu64
                   ",%" SCNu32 ",%" SCNu64 ",%" SCNu64 ",%" SCNu64 "%c",
                   &pair, target, &inject, &msc, &t4, &t5, &t6, &frame, &phase, &period, &actual, &extra) != 12) goto done;
        uint32_t which = strcmp(target,"reference") == 0 ? 0u : 1u;
        if ((which && (!paired || strcmp(target,"editor"))) || extra != '\n' || pair != counts[which]+1u ||
            (!which && frame != pair+1u) || frame <= prev_frame[which] ||
            inject <= prev_inject[which] || msc < prev_msc[which] || !msc || t4 < inject || t4 <= prev_t4[which] ||
            t5 < t4 || t6 < t4 || !period || phase >= period || actual >= period) goto done;
        counts[which]++; prev_frame[which] = frame;
        prev_inject[which] = inject; prev_t4[which] = t4; prev_msc[which] = msc;
    }
    if (!fgets(line, sizeof line, f) && !ferror(f)) rc = 0;
done:
    fclose(f);
    return rc;
}

static int test_editor(int ready_fd, const char *trace_path)
{
    trace_init(); (void)trace_thread_register();
    render_backend backend = {0};
    editor *e = NULL;
    editor_config config = {.cols=40,.rows=4,.max_cols=40,.max_rows=4};
    int rc = render_cpu_backend(&backend);
    if (!rc) rc = editor_open(&e,&config,&backend);
    uint64_t deadline = trace_now_ns()+UINT64_C(10000000000);
    while (!rc && (backend.active || editor_get_stats(e).pending)) {
        int step = editor_step(e,10);
        if (step < 0 || trace_now_ns()>deadline) rc = 1;
    }
    if (!rc && dprintf(ready_fd,"%" PRIu32 "\n",backend.config.platform->win) < 0) rc = 1;
    close(ready_fd);
    if (!rc) rc = editor_run(e);
    uint8_t bytes[12];
    if (!rc && (editor_length(e) != sizeof bytes || editor_read(e,0,bytes,sizeof bytes))) rc = 1;
    for (size_t i = 0; !rc && i < sizeof bytes; i++) if (bytes[i] != 'a') rc = 1;
    if (e) editor_close(e);
    FILE *dump = fopen(trace_path,"wb");
    if (!dump) return 1;
    if (trace_dump(dump) || fclose(dump)) rc = 1;
    return rc ? 1 : 0;
}

static int test_close_editor(uint32_t window)
{
    xcb_connection_t *c = xcb_connect(NULL,NULL);
    if (!c || xcb_connection_has_error(c)) { if (c) xcb_disconnect(c); return 1; }
    xcb_client_message_event_t event = {.response_type=XCB_CLIENT_MESSAGE,.format=32,.window=window,
        .type=refproto_atom(c,"WM_PROTOCOLS")};
    event.data.data32[0] = refproto_atom(c,"WM_DELETE_WINDOW");
    int rc = !refproto_checked(c,xcb_send_event_checked(c,0,window,0,(const char *)&event));
    xcb_disconnect(c); return rc;
}

static int test_join(const char *injections, const char *frames, const char *trace,
                     const char *joined, const char *differences, const char *pairs, bool quiet)
{
    pid_t child = fork();
    if (child == 0) {
        if (quiet) {
            int fd = open("/dev/null",O_WRONLY);
            if (fd < 0 || dup2(fd,STDOUT_FILENO) < 0) _exit(127);
            close(fd);
        }
        execlp("python3","python3","tools/refwin_pairs.py",injections,"--reference-frames",frames,
            "--editor-trace",trace,"--csv",joined,"--differences",differences,"--pairs",pairs,
            "--phase-tolerance-ns","16000000",(char *)NULL);
        _exit(127);
    }
    int status = 0;
    if (child < 0 || waitpid(child,&status,0) != child || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

static int test_wrong_frame(const char *source, const char *dest)
{
    FILE *in = fopen(source,"r"), *out = fopen(dest,"w");
    if (!in || !out) { if (in) fclose(in); if (out) fclose(out); return 1; }
    char line[512]; bool changed = false; int rc = 0;
    while (fgets(line,sizeof line,in)) {
        if (!changed && strstr(line,",editor,")) {
            char *frame = line;
            for (int i = 0; i < 7 && frame; i++) { frame = strchr(frame,','); if (frame) frame++; }
            char *end = frame ? strchr(frame,',') : NULL;
            if (!end || fwrite(line,1,(size_t)(frame-line),out) != (size_t)(frame-line) ||
                fputs("4294967295",out) == EOF || fputs(end,out) == EOF) { rc = 1; break; }
            changed = true;
        } else if (fputs(line,out) == EOF) { rc = 1; break; }
    }
    if (ferror(in) || !changed) rc = 1;
    if (fclose(in)) rc = 1;
    if (fclose(out)) rc = 1;
    return rc;
}

int main(int argc, char **argv)
{
    bool fallback = argc == 2 && !strcmp(argv[1],"--send-event");
    bool track = argc == 2 && !strcmp(argv[1],"--track");
    if (argc != 1 && !fallback && !track) return 2;
    char *bad_args[] = {"keyinject","--window","1","--csv","-","--pairs","0",NULL};
    if (keyinject_tool_main(7,bad_args) != 2) return 1;
    uint64_t number = 0;
    if (refproto_number("-1",UINT64_MAX,&number) == 0 || refproto_number("\t1",UINT64_MAX,&number) == 0 ||
        refproto_number("18446744073709551616",UINT64_MAX,&number) == 0 ||
        refproto_number("18446744073709551615",UINT64_MAX,&number) || number != UINT64_MAX) return 1;
    /* This test deliberately uses the already running, shared Xvfb only. */
    if (setenv("DISPLAY", ":99", 1) || setenv("EDIT_DISPLAY", ":99", 1)) return 1;
    char dir[] = "/tmp/edit-refwin-test-XXXXXX";
    if (!mkdtemp(dir)) return 1;
    char frames[256], injections[256], trace[256], joined[256], differences[256], wrong_frame[256];
    (void)snprintf(frames, sizeof frames, "%s/frames.csv", dir);
    (void)snprintf(injections, sizeof injections, "%s/injections.csv", dir);
    (void)snprintf(trace,sizeof trace,"%s/editor.trace",dir);
    (void)snprintf(joined,sizeof joined,"%s/joined.csv",dir);
    (void)snprintf(differences,sizeof differences,"%s/differences.csv",dir);
    (void)snprintf(wrong_frame,sizeof wrong_frame,"%s/wrong-frame.csv",dir);
    int pipefd[2];
    if (pipe(pipefd)) return 1;
    pid_t ref = fork();
    if (ref == 0) {
        close(pipefd[0]);
        char fd[32]; (void)snprintf(fd, sizeof fd, "%d", pipefd[1]);
        char *args[] = {"refwin", "--pairs", "12", "--csv", frames, "--ready-fd", fd, NULL};
        exit(refwin_tool_main(7, args));
    }
    close(pipefd[1]);
    FILE *ready = fdopen(pipefd[0], "r");
    uint32_t window = 0;
    int rc = 1;
    pid_t editor_pid = -1;
    uint32_t editor_window = 0;
    if (ref > 0 && ready && fscanf(ready, "%" SCNu32, &window) == 1 && window) {
        int editor_pipe[2];
        if (pipe(editor_pipe)) goto cleanup;
        editor_pid = fork();
        if (editor_pid == 0) {
            fclose(ready); close(editor_pipe[0]);
            exit(test_editor(editor_pipe[1],trace));
        }
        close(editor_pipe[1]);
        FILE *editor_ready = fdopen(editor_pipe[0],"r");
        if (editor_pid < 0 || !editor_ready || fscanf(editor_ready,"%" SCNu32,&editor_window) != 1 || !editor_window) {
            if (editor_ready) fclose(editor_ready); else close(editor_pipe[0]);
            goto cleanup;
        }
        fclose(editor_ready);
        pid_t injector = fork();
        if (injector == 0) {
            fclose(ready);
            char win[32]; (void)snprintf(win, sizeof win, "%" PRIu32, window);
            char editor_win[32]; (void)snprintf(editor_win,sizeof editor_win,"%" PRIu32,editor_window);
            char *args[] = {"keyinject", "--window", win, "--pairs", "12", "--csv", injections,
                            "--target", "reference", "--wait-reference", "--phase-ns", "1000000",
                            "--editor-window",editor_win,"--send-event",NULL};
            if (!fallback) args[14] = NULL;
            exit(keyinject_tool_main(fallback ? 15 : 14, args));
        }
        int status = 0;
        if (injector > 0 && waitpid(injector, &status, 0) == injector && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
            test_rows(frames,false) == 0 && test_close_editor(editor_window) == 0 &&
            waitpid(editor_pid,&status,0) == editor_pid && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            editor_pid = -1;
            if (test_join(injections,frames,trace,joined,differences,"12",!track) == 0 && test_rows(joined,true) == 0 &&
                test_join(injections,frames,trace,joined,differences,"13",true) == 1 &&
                test_wrong_frame(injections,wrong_frame) == 0 &&
                test_join(wrong_frame,frames,trace,joined,differences,"12",true) == 1) rc = 0;
        }
    }
cleanup:
    if (ready) fclose(ready); else close(pipefd[0]);
    if (ref > 0) {
        if (rc) (void)kill(ref, SIGTERM);
        int status = 0;
        if (waitpid(ref, &status, 0) != ref || !WIFEXITED(status) || WEXITSTATUS(status)) rc = 1;
    }
    if (editor_pid > 0) { (void)kill(editor_pid,SIGTERM); (void)waitpid(editor_pid,NULL,0); }
    if (!rc && !track) {
        (void)unlink(frames); (void)unlink(injections); (void)unlink(trace); (void)unlink(joined); (void)unlink(differences);
        (void)unlink(wrong_frame);
        (void)rmdir(dir);
    } else fprintf(stderr,"refwin_test: %s artifacts in %s\n",rc ? "failure" : "TRACK",dir);
    puts(rc ? "refwin_test: FAIL headless CSV completeness/monotonicity" :
              "refwin_test: PASS 12 editor/reference pairs, complete monotonic CSV, incomplete/wrong-frame runs rejected (Xvfb phase approximate)");
    return rc;
}
