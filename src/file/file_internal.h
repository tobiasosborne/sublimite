/* Private shared state for file's platform implementations. */
#ifndef EDITOR_FILE_INTERNAL_H
#define EDITOR_FILE_INTERNAL_H
#include "file/file.h"
#include <stdatomic.h>

typedef struct file_map {
    _Atomic int refs;
    void *addr;
    size_t len;
    os_map view;
    os_file fd;
    int slot;                     /* SIGBUS guard slot */
    file_id identity;             /* identity of the actual snapshot backing */
    int private_copy;             /* malloc backing, using the same lifetime hooks */
} file_map;

typedef struct save_transaction {
    file_save_args args;
    os_file fd, metadata_fd;
    os_dir dfd;
    file_id metadata_id;
    char base[OS_NAME_MAX + 1], tmp[OS_NAME_MAX + 1];
    uint64_t done;
} save_transaction;
struct file {
    work_pool *pool;
    char path[OS_PATH_MAX];
    char requested_path[OS_PATH_MAX]; /* immutable worker input */
    uint32_t generation;
    uint64_t threshold;
    void (*before_map)(void *);
    void *before_map_ctx;
    os_file fd;                       /* source fd until attach/close */
    int err_no;

    uint8_t *prefix;
    size_t prefix_len;
    int prefix_owned;
    file_prefix_info info;
    file_mode mode;
    uint64_t size;

    /* UI-owned content. Workers publish separate immutable result records. */
    file_map *map;                /* owned reference of the file object */
    _Atomic int ready;            /* 1 = content ready for attach */
    int attached;
    work_handle open_h;
    work_handle prefix_h;
    _Atomic int prefix_fin;
    struct {
        char path[OS_PATH_MAX];
        file_id id;
        uint8_t *bytes;
        size_t len;
        file_prefix_info info;
        file_mode mode;
        os_file fd;
        int status, err_no;
    } prefix_result;
    _Atomic int prefix_result_ready;
    int prefix_installed;
    int open_status;
    _Atomic int open_fin;         /* 1 when no open job is pending */
    struct {
        file_map *map;
        int status, err_no;
    } open_result;
    _Atomic int open_result_ready;
    int open_installed;
    file_id open_identity;        /* immutable open-job input */

    file_id id;
    int changed;
    int ifd, iwd;
    size_t watch_slot;            /* index in the UI-owned process registry */
    int watch_refresh;
    char watch_name[OS_NAME_MAX + 1];
    uint64_t source_generation;
    void (*step)(void *, int);    /* UI-owned hooks, captured at submit */
    void *step_ctx;
    work_handle jobs[WORK_MAX_JOBS]; /* includes completed, undrained jobs */
    work_handle check_h;
    _Atomic int check_fin, check_result_ready;
    int check_installed, check_requested;
    struct { file_id observed; uint32_t map_reasons; uint64_t generation; int status, err_no; } check_result;
    struct { file_map *map; uint64_t generation; } check;

    struct {
        piece_snapshot *snap;
        unsigned flags;
        uint32_t generation;
        void (*step)(void *, int);
        void *step_ctx;
        file_id expect, source_id;
        uint32_t mode;
        uint64_t source_generation;
        uint64_t source_faults;
        file_map *source_map;
        int authorized;          /* immutable commit input, decided by UI */
    } save;
    save_transaction transaction;
    work_handle prepare_h;
    _Atomic int prepare_fin;
    int prepare_status, commit_pending;
    int save_aborted, abort_pending;
    work_handle abort_h;
    _Atomic int abort_fin;
    work_handle save_h;
    _Atomic int save_fin;
    struct {
        file_id id;
        int status, err_no;
    } save_result;
    _Atomic int save_result_ready;
    int save_installed;
    _Atomic int save_replaced_ready;
    int save_replaced_installed;
};


void file_split_path(const char *, char *, size_t, char *, size_t);
void file_watch_check_pump(file *);
void file_watch_detach(file *);
int file_native_error(os_err);
os_err file_pread_once(os_file *, void *, size_t, uint64_t, size_t *);
int file_save_observer(const os_file *, os_file *);
int file_metadata_prepare(save_transaction *);
int file_save_temp(save_transaction *);
int file_metadata_check(const os_file *);
int file_metadata_copy(const os_file *, os_file *, const file_id *);
#endif
