/* Explicit real-provider acceptance, outside check. Reuse the measured probe's
 * private-station guard, marker protocol, deadlines and profile-only OS shim. */
#define main wsl_capability_probe_main
#include "wsl_share_probe.c"
#undef main
#include "savectl/savectl.h"
#include "journal/journal.h"

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "wsl-degraded:%d: FAIL %s native=%d\n", __LINE__, #c, os_native_error()); return 1; } } while (0)
static const char edited[] = "edited-original\n";
static int temps_created;
static void count_temps(void *ctx, int step)
{ (void)ctx; if (step == FILE_STEP_TEMP_CREATED) temps_created++; }
static int controller_wait(savectl *controller)
{
    uint64_t end = os_now_ns() + (uint64_t)HELPER_MS * 1000000u;
    do {
        savectl_tick(controller);
        if (!savectl_get_model(controller).busy) return 1;
        Sleep(1);
    } while (os_now_ns() < end);
    return 0;
}
static int controller_close(savectl *controller)
{
    savectl_close_begin(controller);
    uint64_t end = os_now_ns() + (uint64_t)HELPER_MS * 1000000u;
    do {
        savectl_tick(controller);
        if (savectl_destroy(controller) == SAVECTL_OK) return 1;
        Sleep(1);
    } while (os_now_ns() < end);
    return 0;
}
static int acquire(work_pool *workers, const char *path, file **out, piece_tree **tree)
{
    received result = {0}; file_open_opts options = {.copy_threshold = 1};
    REQUIRE(file_open_begin(workers, path, &options, out) == FILE_OK);
    REQUIRE(await_message(workers, NULL, &result) && result.message.kind == FILE_MSG_OPEN_READY && result.message.status == FILE_OK);
    REQUIRE(file_open_mode(*out) == FILE_MODE_COPY);
    piece_allocator allocator = piece_default_allocator(); *tree = piece_create(&allocator);
    REQUIRE(*tree && file_attach(*out, *tree) == FILE_OK); return 0;
}
static int real_case(const char *share, const char *marker, const char *rel, const char *private_root)
{
    REQUIRE(test_isolation_query() == TEST_ISOLATION_STATION);
    probe_root = share; ipc = marker; relative = rel; backend = rel; case_name = "degraded";
    REQUIRE(os_env_set("LOCALAPPDATA", private_root) == OS_OK);
    REQUIRE(os_env_set("APPDATA", private_root) == OS_OK);
    trace_init(); work_pool workers; REQUIRE(work_pool_init(&workers, 1, 1) == 0);
    char source[OS_PATH_MAX]; join(source, share, "source");
    file *original = NULL; piece_tree *tree = NULL;
    REQUIRE(acquire(&workers, source, &original, &tree) == 0);
    REQUIRE(file_watch_start(original) == FILE_WATCH_UNSUPPORTED);
    REQUIRE(piece_insert(tree, 0, (const uint8_t *)"edited-", sizeof "edited-" - 1) == 0);
    uint8_t bytes[sizeof edited] = {0};
    REQUIRE(piece_read(tree, 0, bytes, sizeof edited - 1) == 0);
    /* Publish the actual edited buffer before save, as a whole-text checkpoint.
     * The failed save uses the real journal prepare/handoff/controller path. */
    work_pool journal_workers; REQUIRE(work_pool_init(&journal_workers, 1, 0) == 0);
    char journal_path[OS_PATH_MAX]; join(journal_path, private_root, "recovery"); journal *log = NULL;
    REQUIRE(journal_open(&log, journal_path, &journal_workers, NULL) == JOURNAL_OK);
    journal_base empty = {.path = ""}; REQUIRE(journal_set_base(log, 1, &empty) == JOURNAL_OK);
    REQUIRE(journal_insert(log, 1, 0, bytes, sizeof edited - 1) == JOURNAL_OK);
    REQUIRE(journal_flush(log) == JOURNAL_OK);
    uint8_t base_record[JOURNAL_BASE_MAX], insert_record[sizeof(uint64_t) + sizeof edited - 1] = {0};
    size_t base_size = 0;
    REQUIRE(journal_encode_base(&empty, base_record, sizeof base_record, &base_size) == JOURNAL_OK);
    memcpy(insert_record + sizeof(uint64_t), bytes, sizeof edited - 1);
    journal_record checkpoint[] = {
        {.type = JOURNAL_BASE, .buffer_id = 1, .data = base_record, .size = base_size},
        {.type = JOURNAL_INSERT, .buffer_id = 1, .data = insert_record, .size = sizeof insert_record}
    };
    file_id baseline = *file_prefix_identity(original);
    savectl_options options = {.pool = &workers, .path = source, .baseline = baseline,
        .source_mode = FILE_MODE_COPY, .reload_allocator = piece_default_allocator(),
        .step = count_temps, .reload_cache_dir = private_root,
        .journal = log, .journal_pool = &journal_workers, .buffer_id = 1};
    savectl *controller = NULL; REQUIRE(savectl_create(&controller, &options, true) == SAVECTL_OK);
    REQUIRE(savectl_save(controller, tree, &empty, checkpoint, sizeof checkpoint / sizeof checkpoint[0]) == SAVECTL_OK);
    REQUIRE(controller_wait(controller)); savectl_model model = savectl_get_model(controller);
    REQUIRE(model.state == SAVECTL_FAILED && model.file_error == FILE_ERR_UNSUPPORTED &&
            model.err_no == ERROR_NOT_SUPPORTED && model.modified && !temps_created);
    REQUIRE(piece_read(tree, 0, bytes, sizeof edited - 1) == 0 && !memcmp(bytes, edited, sizeof edited - 1));
    puts("save refused: FILE_ERR_UNSUPPORTED, dirty kept, no temporary");
    REQUIRE(controller_close(controller));
    /* Save As existing and missing share targets must refuse identically. */
    piece_snapshot *snapshot = piece_snapshot_take(tree); REQUIRE(snapshot);
    char missing[OS_PATH_MAX], stream[OS_PATH_MAX];
    join(missing, share, "missing"); join(stream, share, "source:Zone.Identifier");
    const char *targets[] = {source, missing, stream};
    for (size_t i = 0; i < sizeof targets / sizeof targets[0]; i++) {
        file_save_args args = {.path = targets[i], .snap = snapshot, .step = count_temps};
        REQUIRE(file_save_write(&args) == FILE_ERR_UNSUPPORTED && args.err_no == ERROR_NOT_SUPPORTED && !args.written && !temps_created);
    }
    /* Linux is the metadata oracle, and Windows never creates a colon sibling. */
    REQUIRE(linux_action("inspect-save", "source"));
    REQUIRE(linux_action("inspect-streams", "source"));
    char ntfs[OS_PATH_MAX]; join(ntfs, private_root, "save-as");
    file_save_args args = {.path = ntfs, .snap = snapshot};
    REQUIRE(file_save_write(&args) == FILE_OK);
    os_file saved = OS_FILE_INIT; size_t got = 0;
    REQUIRE(os_file_open(&saved, ntfs, OS_OPEN_READ) == OS_OK);
    REQUIRE(os_file_read(&saved, bytes, sizeof bytes, &got) == OS_OK && got == sizeof edited - 1 && !memcmp(bytes, edited, got));
    os_file_close(&saved); puts("Save As to NTFS: edited bytes saved");
    piece_snapshot_release(snapshot);
    const char *mutations[] = {"write", "replace", "truncate"};
    for (size_t i = 0; i < sizeof mutations / sizeof mutations[0]; i++) {
        REQUIRE(linux_action(mutations[i], "source"));
        REQUIRE(file_check(original, NULL));
        REQUIRE(piece_read(tree, 0, bytes, sizeof edited - 1) == 0 && !memcmp(bytes, edited, sizeof edited - 1));
        REQUIRE(file_resolve_keep(original) == FILE_OK);
        printf("Linux %s: file_check detected, private edited bytes intact\n", mutations[i]);
    }
    /* Distinct Linux names and literal trailing characters survive acquisition. */
    const char *names[] = {"a", "A", "trailing.", "space "};
    const char *contents[] = {"lower", "UPPER", "dot", "space"};
    file_id identities[2] = {{0}};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char path[OS_PATH_MAX]; join(path, share, names[i]); file *f = NULL; piece_tree *t = NULL;
        REQUIRE(acquire(&workers, path, &f, &t) == 0);
        REQUIRE(piece_len(t) == strlen(contents[i]) && piece_read(t, 0, bytes, strlen(contents[i])) == 0 && !memcmp(bytes, contents[i], strlen(contents[i])));
        if (i < 2) identities[i] = *file_prefix_identity(f);
        REQUIRE(!strcmp(file_path(f) + strlen(file_path(f)) - strlen(names[i]), names[i]));
        piece_destroy(t); file_close(f);
    }
    REQUIRE(identities[0].ino != identities[1].ino || identities[0].ino_hi != identities[1].ino_hi);
    char link[OS_PATH_MAX]; join(link, share, "link"); file *unresolved = NULL; received result = {0};
    REQUIRE(file_open_begin(&workers, link, NULL, &unresolved) == FILE_OK);
    REQUIRE(await_message(&workers, NULL, &result) && result.message.kind == FILE_MSG_OPEN_FAILED && result.message.err_no == ERROR_CANT_RESOLVE_FILENAME);
    file_close(unresolved);
    /* Whole-text hot-exit record lives on NTFS, independent of the changed share.
     * Exit without closing any of these objects; parent replays after process death. */
    REQUIRE(journal_flush(log) == JOURNAL_OK);
    fflush(stdout); ExitProcess(77);
}
static int recover(void *ctx, const journal_record *record)
{
    size_t *n = ctx;
    if (record->type == JOURNAL_INSERT) {
        if (record->size != sizeof(uint64_t) + sizeof edited - 1 ||
            memcmp(record->data + sizeof(uint64_t), edited, sizeof edited - 1)) return 1;
        *n += sizeof edited - 1;
    }
    return 0;
}
int main(int argc, char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc == 6 && !strcmp(argv[1], "--case")) return real_case(argv[2], argv[3], argv[4], argv[5]);
    REQUIRE(argc == 4 && test_isolation_query() == TEST_ISOLATION_STATION);
    char temp[OS_PATH_MAX], scratch[OS_PATH_MAX], exe[OS_PATH_MAX], journal_path[OS_PATH_MAX];
    REQUIRE(os_temp_dir(temp, sizeof temp) == OS_OK);
    REQUIRE(snprintf(scratch, sizeof scratch, "%s\\sublimite-test-wsl-degraded-%llu-%llu", temp,
        (unsigned long long)os_proc_id(), (unsigned long long)os_now_ns()) > 0);
    REQUIRE(os_mkdir(scratch) == OS_OK && os_proc_self_path(exe, sizeof exe) == OS_OK);
    const char *arguments[] = {exe, "--case", argv[1], argv[2], argv[3], scratch, NULL};
    os_proc process; os_proc_status status;
    REQUIRE(os_proc_spawn(&process, arguments) == OS_OK);
    os_err waited = os_proc_wait(&process, (int64_t)CHILD_MS * 1000000, &status);
    if (waited == OS_E_TIMEDOUT) { (void)os_proc_kill(&process); (void)os_proc_wait(&process, -1, &status); }
    int ok = waited == OS_OK && status.how == OS_PROC_EXITED && status.code == 77;
    if (ok) {
        join(journal_path, scratch, "recovery"); size_t n = 0; journal_replay_result replay = {0};
        ok = journal_replay_file(journal_path, recover, &n, &replay) == JOURNAL_OK && n == sizeof edited - 1;
        if (ok) puts("hot exit: private NTFS journal replay retained edited bytes");
    }
    remove_tree(scratch); REQUIRE(ok); return 0;
}
