/* Measured provider errors, on private NTFS fixtures: no WSL dependency in check. */
static void wsl_degraded_cases(void)
{
    char path[OS_PATH_MAX]; win_path(path, "wsl-provider");
    win_write(path, (const uint8_t *)"original", 8);
    r2.wsl = 1;
    file *f = win_open(path, 1); /* threshold crossed: still a private copy */
    CHECK(file_open_mode(f) == FILE_MODE_COPY);
    piece_tree *tree = win_tree(f);
    CHECK(piece_insert(tree, 0, (const uint8_t *)"edited-", 7) == 0);
    r2.arm_error = ERROR_INVALID_FUNCTION;
    CHECK(file_watch_start(f) == FILE_WATCH_UNSUPPORTED);
    CHECK(f->ifd < 0 && f->iwd < 0 && f->watch_slot == SIZE_MAX);
    file_path_watch named = {0};
    CHECK(file_watch_path_start(&named, path) == FILE_WATCH_UNSUPPORTED && !named.registered);
    CHECK(!r2_has_timer());
    r2.arm_error = 0;
    r2.directory_opens = r2.rename_calls = r2.created = 0;
    r2.rename_error = ERROR_INVALID_PARAMETER; /* measured FileRenameInfoEx error: never reached */
    CHECK(file_save_begin(f, tree, 0, 1) == FILE_ERR_UNSUPPORTED);
    piece_snapshot *snapshot = piece_snapshot_take(tree); CHECK(snapshot);
    file_save_args args = {.path = path, .snap = snapshot, .step = r2_temp_step};
    CHECK(file_save_write(&args) == FILE_ERR_UNSUPPORTED);
    CHECK(args.written == 0 && args.err_no == ERROR_NOT_SUPPORTED);
    CHECK(!r2.directory_opens && !r2.created && !r2.rename_calls);
    r2.rename_error = 0; r2.wsl = 0;
    win_read_equals(path, (const uint8_t *)"original", 8); file_os_no_temps(root);
    /* A share source's private edited snapshot can be saved to a Windows target. */
    char target[OS_PATH_MAX]; win_path(target, "save-as-windows");
    args = (file_save_args){.path = target, .snap = snapshot};
    CHECK(file_save_write(&args) == FILE_OK);
    win_read_equals(target, (const uint8_t *)"edited-original", 15);
    CHECK(os_unlink(target) == OS_OK);
    /* Explicit identity checks do not require successful registration. */
    win_write(path, (const uint8_t *)"external", 8);
    r2.wsl = 1;
    uint64_t deadline = os_now_ns() + UINT64_C(5000000000); /* existing worker fixture allowance */
    do {
        (void)file_watch_poll(f);
        file_os_messages ignored = {0};
        (void)work_mailbox_drain(&pool, file_os_receive, &ignored);
        if (file_changed(f)) break;
        os_sleep_ns(1000000);
    } while (os_now_ns() < deadline);
    CHECK(file_changed(f) && file_check(f, NULL)); r2.wsl = 0;
    uint8_t retained[15]; CHECK(piece_read(tree, 0, retained, sizeof retained) == 0);
    CHECK(!memcmp(retained, "edited-original", sizeof retained));
    piece_snapshot_release(snapshot); piece_destroy(tree); file_close(f);
    /* File is above the existing copy allocation bound: fail before allocation/map. */
    r2.wsl = 1; r2.wsl_oversize = 1;
    file_open_opts opts = {.copy_threshold = 1}; file_msg message = {0}; f = NULL;
    CHECK(file_open_begin(&pool, path, &opts, &f) == FILE_OK);
    CHECK(file_os_terminal(&pool, &message) && message.kind == FILE_MSG_OPEN_FAILED &&
          message.status == FILE_ERR_NOMEM && message.err_no == ERROR_FILE_TOO_LARGE);
    file_close(f); r2.wsl_oversize = 0; r2.wsl = 0;
    /* The unresolved provider symlink error remains an acquisition error. */
    r2.open_error = ERROR_CANT_RESOLVE_FILENAME; f = NULL;
    CHECK(file_open_begin(&pool, path, NULL, &f) == FILE_OK);
    CHECK(file_os_terminal(&pool, &message) && message.kind == FILE_MSG_OPEN_FAILED &&
          message.status == FILE_ERR_IO && message.err_no == ERROR_CANT_RESOLVE_FILENAME);
    file_close(f); r2.open_error = 0;
    win_read_equals(path, (const uint8_t *)"external", 8);
    /* A non-WSL provider returning native 1 remains an ordinary watch error. */
    f = win_open(path, UINT64_MAX); r2.arm_error = ERROR_INVALID_FUNCTION;
    CHECK(file_watch_start(f) == -1 && GetLastError() == ERROR_INVALID_FUNCTION);
    r2.arm_error = 0; file_close(f); CHECK(os_unlink(path) == OS_OK);
}
