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

/* Dry runs must not create CSVs or send keys, and must preserve focus. */
static int test_dry_command(bool injector, uint32_t window, const char *expected, int status_expected, const char *option, const char *value)
{
    FILE *log = tmpfile();
    if (!log) return 1;
    pid_t child = fork();
    if (!child) {
        if (dup2(fileno(log), STDERR_FILENO) < 0) _exit(127);
        char win[32]; (void)snprintf(win,sizeof win,"%" PRIu32,window);
        char *args[] = {injector ? "keyinject" : "refwin", "--dry-run", "--window", win, "--synthetic-clock", (char *)option, (char *)value, NULL, NULL};
        int count = option ? 7 : 5;
        if (option && !value) { args[6] = "--target"; args[7] = "reference"; count = 8; }
        exit(injector ? keyinject_tool_main(count,args) : refwin_tool_main(count,args));
    }
    int status = 0, rc = 1;
    if (child > 0 && waitpid(child,&status,0) == child && WIFEXITED(status)) {
        rewind(log); char message[8192];
        size_t n = fread(message,1,sizeof message-1u,log); message[n] = 0;
        if (WEXITSTATUS(status) == status_expected && strstr(message,expected) &&
            (status_expected || (strstr(message,"XTEST=available") && strstr(message,"NotifyMSC UST_ns=") &&
            strstr(message,"period=") && !strstr(message,"configured")))) rc = 0;
        else fprintf(stderr,"refwin_test: dry-run RED: exit=%d expected=%d diagnostic=%s expected=%s\n",
            WEXITSTATUS(status),status_expected,message,expected);
    }
    fclose(log); return rc;
}
static int test_preflight(void)
{
    xcb_connection_t *c = xcb_connect(NULL,NULL);
    if (!c || xcb_connection_has_error(c)) { if (c) xcb_disconnect(c); return 1; }
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    uint32_t values[] = {1, XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE};
    xcb_window_t w = xcb_generate_id(c);
    int rc = 1;
    if (!refproto_checked(c,xcb_create_window_checked(c,XCB_COPY_FROM_PARENT,w,screen->root,10,10,32,32,0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,screen->root_visual,XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,values))) goto done;
    if (test_dry_command(true,w,"window not viewable",1,NULL,NULL) ||
        test_dry_command(false,w,"window not viewable",1,NULL,NULL)) goto done;
    if (test_dry_command(true,UINT32_MAX,"GetWindowAttributes failed",1,NULL,NULL) ||
        test_dry_command(false,UINT32_MAX,"GetWindowAttributes failed",1,NULL,NULL)) goto done;
    if (!refproto_checked(c,xcb_map_window_checked(c,w))) goto done;
    char paired_window[32];
    (void)snprintf(paired_window,sizeof paired_window,"%" PRIu32,screen->root);
    if (test_dry_command(true,w,"reference readiness property missing",1,"--editor-window",paired_window)) goto done;
    /* XTest will not deliver a fresh press for a key already held down.
     * A dry run must diagnose this without releasing the existing key. */
    refproto_display input;
    if (refproto_display_open(&input,"held-key-fixture",true)) { refproto_display_close(&input); goto done; }
    if (refproto_request(&input,w,input.fake(input.conn,XCB_KEY_PRESS,38,XCB_CURRENT_TIME,input.root,0,0,0),"hold fixture key")) {
        refproto_display_close(&input); goto done;
    }
    int held = test_dry_command(true,w,"keyboard is not idle: keycode=38 down",1,NULL,NULL) ||
        test_dry_command(false,w,"keyboard is not idle: keycode=38 down",1,NULL,NULL);
    xcb_query_keymap_reply_t *held_map = xcb_query_keymap_reply(c,xcb_query_keymap(c),NULL);
    bool still_held = held_map && ((uint8_t)held_map->keys[38u/8u] & (1u << (38u%8u)));
    free(held_map);
    int released = refproto_request(&input,w,input.fake(input.conn,XCB_KEY_RELEASE,38,XCB_CURRENT_TIME,input.root,0,0,0),"release fixture key");
    refproto_display_close(&input);
    if (held || !still_held || released) goto done;
    xcb_generic_event_t *held_event;
    while ((held_event = xcb_poll_for_event(c))) free(held_event);
    if (test_dry_command(true,w,"keycode has no server mapping",1,"--keycode","8") ||
        test_dry_command(false,w,"keycode has no server mapping",1,"--keycode","8") ||
        test_dry_command(true,w,"reference readiness property missing",1,"--wait-reference",NULL)) goto done;
    xcb_atom_t marker = refproto_atom(c,"_EDIT_REF_READY");
    uint32_t ready_words[] = {1,39};
    if (!refproto_checked(c,xcb_change_property_checked(c,XCB_PROP_MODE_REPLACE,w,marker,XCB_ATOM_CARDINAL,32,2,ready_words)) ||
        test_dry_command(true,w,"reference readiness version/keycode mismatch",1,"--wait-reference",NULL)) goto done;
    ready_words[1] = 38;
    if (!refproto_checked(c,xcb_change_property_checked(c,XCB_PROP_MODE_REPLACE,w,marker,XCB_ATOM_CARDINAL,32,2,ready_words)) ||
        test_dry_command(true,w,"dry-run PASS",0,"--wait-reference",NULL) ||
        test_dry_command(true,w,"dry-run PASS",0,"--editor-window",paired_window)) goto done;
    char csv_path[] = "/tmp/edit-refwin-dry-csv-XXXXXX";
    int csv_fd = mkstemp(csv_path);
    if (csv_fd < 0) goto done;
    if (write(csv_fd,"sentinel",8) != 8) { close(csv_fd); unlink(csv_path); goto done; }
    close(csv_fd);
    xcb_get_input_focus_reply_t *before = xcb_get_input_focus_reply(c,xcb_get_input_focus(c),NULL);
    if (!before) goto done;
    int dry = test_dry_command(true,w,"dry-run PASS",0,"--csv",csv_path) || test_dry_command(false,w,"dry-run PASS",0,"--csv",csv_path);
    int assertion = test_dry_command(true,w,"--period-ns assertion failed",1,"--period-ns","11111111");
    FILE *csv = fopen(csv_path,"r"); char contents[16] = {0};
    bool untouched = csv && fread(contents,1,sizeof contents,csv) == 8 && !memcmp(contents,"sentinel",8);
    if (csv) fclose(csv);
    unlink(csv_path);
    xcb_get_input_focus_reply_t *after = xcb_get_input_focus_reply(c,xcb_get_input_focus(c),NULL);
    bool restored = after && before->focus == after->focus;
    free(before); free(after);
    xcb_generic_event_t *event;
    bool key = false;
    while ((event = xcb_poll_for_event(c))) {
        uint8_t type = event->response_type & 0x7fu;
        if (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE) key = true;
        free(event);
    }
    if (!dry && !assertion && untouched && restored && !key) rc = 0;
    if (!rc) puts("refwin_test: dry-run checks mapped/focusable targets, preserves focus/CSV, rejects missing targets/wrong rates, injects no keys PASS");
done:
    xcb_destroy_window(c,w); xcb_disconnect(c); return rc;
}

static int test_event_diagnostics(void)
{
    FILE *log = tmpfile();
    if (!log) return 1;
    pid_t child = fork();
    if (!child) {
        if (dup2(fileno(log),STDERR_FILENO) < 0) _exit(127);
        refproto_display d;
        if (refproto_display_open(&d,"diagnostic-test",true)) _exit(1);
        d.timeout_ns = 1000000;
        refproto_clock clock = {0};
        int bad_window = refproto_msc(&d,UINT32_MAX,0,&clock);
        int timeout = refproto_wait(&d,d.root,255,123,&clock,NULL);
        int zero = refproto_clock_valid(&d,d.root,(refproto_clock){0,0});
        refproto_display_close(&d);
        exit(bad_window < 0 && timeout < 0 && zero < 0 ? 0 : 1);
    }
    int status = 0, rc = 1;
    if (child > 0 && waitpid(child,&status,0) == child && WIFEXITED(status) && !WEXITSTATUS(status)) {
        rewind(log); char message[4096];
        size_t n = fread(message,1,sizeof message-1u,log); message[n] = 0;
        if (strstr(message,"Present NotifyMSC: X error=3") &&
            strstr(message,"Present event timeout: expected kind=255 serial=123") &&
            strstr(message,"Present UST is zero: UST_ns=0 MSC=0")) rc = 0;
        else fprintf(stderr,"refwin_test: incorrect event diagnostics: %s",message);
    }
    if (rc) { rewind(log); char line[512]; while (fgets(line,sizeof line,log)) fputs(line,stderr); }
    fclose(log);
    if (!rc) puts("refwin_test: exact NotifyMSC X error, event timeout and zero-clock diagnostics PASS");
    return rc;
}

static int test_period_math(void)
{
    uint64_t period = 0;
    if (refproto_period((refproto_clock){1000000000,10},(refproto_clock){1050000000,13},&period) ||
        period != 16666666u || refproto_period((refproto_clock){10,1},(refproto_clock){10,2},&period) == 0 ||
        refproto_period((refproto_clock){10,2},(refproto_clock){20,2},&period) == 0 ||
        !refproto_period_matches(11111111,11111112) || refproto_period_matches(11111111,16666667)) return 1;
    pid_t child = fork();
    if (!child) {
        const char *script =
            "from tools.refwin_pairs import pair_rows\n"
            "a=dict(pair_id=1,target='reference',inject_ns=1,msc=1,t4_ns=2,t5_ns=3,t6_ns=3,frame_id=1,phase_ns=100,period_ns=11111111,actual_phase_ns=100)\n"
            "b=dict(a,target='editor',inject_ns=2,t4_ns=3,t5_ns=4,t6_ns=4,period_ns=11111112)\n"
            "pair_rows([a,b],1,1,0)\n"
            "b['period_ns']=16666667\n"
            "try: pair_rows([a,b],1,1,0)\n"
            "except ValueError: pass\n"
            "else: raise AssertionError('90/60 Hz pair accepted')\n";
        execlp("python3","python3","-B","-c",script,(char *)NULL); _exit(127);
    }
    int status = 0;
    if (child < 0 || waitpid(child,&status,0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) return 1;
    puts("refwin_test: measured period arithmetic and 90/60 Hz pair rejection PASS"); return 0;
}

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
            "--phase-tolerance-ns","16000000","--synthetic-clock",(char *)NULL);
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
    (void)setvbuf(stdout,NULL,_IONBF,0);
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
    if (test_period_math() || test_event_diagnostics() || test_preflight()) return 1;
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
        char *args[] = {"refwin", "--pairs", "12", "--csv", frames, "--ready-fd", fd, "--synthetic-clock", NULL};
        exit(refwin_tool_main(8, args));
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
                            "--editor-window",editor_win,"--synthetic-clock","--send-event",NULL};
            if (!fallback) args[15] = NULL;
            exit(keyinject_tool_main(fallback ? 16 : 15, args));
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
