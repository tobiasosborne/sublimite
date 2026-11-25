/* L2 file: acquisition, fault registration, change detection, the save
 * transaction's file half, and reload (bead L2-1;
 * docs/decisions/L2-1-file.md; signed errata 3-5 of
 * docs/decisions/wave1-app-spec.md).
 *
 * ACQUISITION. file_open_begin captures the path and submits one bulk job.
 * The job canonicalizes, opens, validates descriptor against path, reads
 * the prefix, then either reads the whole file (copy mode) or maps it and
 * registers the mapping with the fault service BEFORE anything is
 * published (wave1-os-fault-lease.md, L2 row). On registration refusal
 * (OS_E_NOSPC) the unpublished map and fd are released and the open fails
 * with FILE_ERR_NOSPC: refused visibly, never a silent copy fallback (Q12).
 * The job posts FILE_MSG_PREFIX_READY, then FILE_MSG_OPEN_READY or
 * FILE_MSG_OPEN_FAILED. The prefix the UI sees at PREFIX_READY stays in
 * the job's record, which only the UI frees: a failed open withdraws it
 * at OPEN_FAILED (file_prefix then returns NULL), so no reader outlives
 * its bytes. Reload is the same acquisition into a separate slot
 * (file_reload_begin / file_reload_tree): it builds a new registration
 * and leaves the old tree, registration and baseline intact until the UI
 * adopts the new tree.
 *
 * IDENTITY. One comparison type, file_expectation: ABSENT or
 * PRESENT(os_stat) (Q14). A failed observation is a separate result
 * (FILE_ERR_IO), never an absent or a zero-filled stat. One classifier,
 * file_expectation_diff, serves path checks and fd checks. Change
 * detection (file_check) combines the path observation, the held fd's
 * observation against the backing's baseline (ctime included: a
 * same-size in-place overwrite with mtime restored raises no SIGBUS and
 * moves only ctime), and one closing fault poll (os_fault_poll != OS_OK
 * or epoch != 0 means changed). Poll zeroes are never adopted as text.
 * A backing's baseline moves at exactly one point: our own rename over
 * that inode (file_temp_publish; the before/after fstat pair differs in
 * ctime and a falling link count only). Nothing else re-baselines.
 *
 * SAVE, FILE HALF. The caller owns the cutoff: file_save_begin takes a
 * caller-owned piece_snapshot (retained for the prepare job), never the
 * live tree. Preparation writes a temp in the destination directory,
 * computes the new-file witness while writing (volume + inode + size +
 * CRC32C over all of S, journal.h SAVE(PREPARED)), gives the temp the
 * expected destination's metadata (or, for a new name, 0666 & ~the
 * startup creation mask), and syncs it. Custody (file_temp) then moves
 * through EMPTY, SYNCED, INSTALLED_UNSYNCED, HELD and DONE; each state
 * says what it owns (file_temp_state). The caller writes the journal's
 * PREPARED between prepare and publish. Publish renames at most once
 * (OS_RENAME_NOREPLACE when the expected destination is absent;
 * OS_E_EXIST is FILE_ERR_CHANGED; OS_E_NOTSUP refuses, never a racy
 * replace), syncs the directory, and verifies the path against the
 * written fd. The outcome is classified (file_pub). No retention, clone
 * or link of the replaced file (A23 rule 1). The same steps run
 * synchronously through the file_temp_* core, which the kill-at-every-
 * step tests, the bench and savectl drive directly.
 *
 * STATE. Every lifecycle is one enum under -Wswitch-enum with no
 * coordinating booleans: file_open_state, file_reload_state,
 * file_save_phase, file_temp_state, file_watch_state. Close is two-phase
 * and non-blocking: file_close_begin cancels every job this file ever
 * submitted, file_close_ready reports their physical completion
 * (work_handle_finished per handle), file_close_finish releases. No
 * UI-thread sleep or spin exists in this module, and no const query
 * performs I/O. A worker whose work_publish meets a full mailbox retains
 * its immutable result and retries through work_continue.
 *
 * MESSAGES. file_msg_decode accepts a message only from the job its
 * slot currently names (work slot, epoch and generation), so a message
 * of a superseded job or of reused storage is refused. After
 * file_close_begin every message of this file is stale in the pool
 * (each handle was cancelled), so storage may be freed once
 * file_close_finish returns even with messages still undrained.
 *
 * STORAGE. The file object is caller storage, zeroed before
 * file_open_begin (the work_pool pattern). Backings and buffers come
 * from src/base arenas (one reservation per backing, released by its
 * final owner on that thread); this module calls no malloc at all, so
 * the Law-2 window (snapshot retain, validator, foreground queries) is
 * allocation-free by construction.
 *
 * THREADS. file_* calls are UI-thread only except the file_temp_*
 * synchronous core and file_save_write (any one thread at a time per
 * temp), file_source_* (any thread), file_backing_validate with a null
 * ctx (any thread holding the backing), and the backing acquire/release
 * behind piece_backing. Errors are return codes. */
#ifndef FILE_FILE_H
#define FILE_FILE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "base/base.h"
#include "os/os.h"
#include "piece/piece.h"
#include "work/work.h"

typedef struct file file;

typedef enum file_err {
    FILE_OK = 0,
    FILE_ERR_IO,       /* see the os_err out-param / file_os_err */
    FILE_ERR_NOMEM,    /* a base reservation or piece storage was refused */
    FILE_ERR_NOTREG,   /* not a regular file */
    FILE_ERR_BUSY,     /* a job of that kind is in flight */
    FILE_ERR_CHANGED,  /* observed identity differs from the expectation */
    FILE_ERR_CANCELLED,
    FILE_ERR_STATE,    /* not ready, already attached, no temp in custody */
    FILE_ERR_POOL,     /* work_submit refused */
    FILE_ERR_DIRSYNC,  /* installed, but the directory barrier failed */
    FILE_ERR_NOSPC,    /* fault-registry slots exhausted (Q12) */
    FILE_ERR_NOTSUP,   /* atomic NOREPLACE unavailable for an absent target */
    FILE_ERR_CONFLICT  /* post-rename foreign or unverifiable identity */
} file_err;

/* Path diagnosis (A15: a name never reaches an ASSERT). An empty path is
 * FILE_ERR_IO with OS_E_INVAL; one ending in '/' (the root included)
 * names a directory: OS_E_ISDIR (FILE_ERR_NOTREG on open); a path of
 * OS_PATH_MAX bytes or more, or a leaf longer than NAME_MAX, is
 * OS_E_NAMETOOLONG. A relative path resolves against the working
 * directory (a bare leaf names "."); targets are canonical. */

/* (G) v1's prefix bound (v1 file.h FILE_PREFIX_MAX): the first viewport
 * source and the EOL/BOM scan window, 1 MiB (P: 2^20). */
#define FILE_PREFIX_MAX (1u << 20)

/* (G) The journal's GENERATION prefix witness length (journal.h
 * JOURNAL_PREFIX_BYTES, 4096: one block capture already reads). File is
 * below journal, so the value is restated here; file_temp_verify
 * returns the CRC32C over the first min(size, FILE_WIRE_PREFIX_BYTES). */
#define FILE_WIRE_PREFIX_BYTES 4096u

/* Copy below the threshold, map at or above it (a zero-length file is a
 * copy: there is nothing to map). (G) v1 policy carried (v1 file.c:10,
 * :137-142): min(256 MiB, phys/32), so a mapped open never holds more
 * than a thirty-second of RAM in page cache commitment terms and small
 * files never take a fault-registry slot. OS_FAULT_SLOTS is derived
 * from this constant (os.h): only files at or above it register, so
 * whoever lowers this threshold re-derives the slot count (A22/Q12). */
uint64_t file_default_copy_threshold(void);

typedef enum file_mode { FILE_MODE_COPY = 1, FILE_MODE_MMAP = 2 } file_mode;

typedef enum file_eol {
    FILE_EOL_NONE = 0,
    FILE_EOL_LF = 1,
    FILE_EOL_CRLF = 2,
    FILE_EOL_MIXED = 3
} file_eol;

typedef struct file_prefix_info {
    uint64_t lf;       /* '\n' not preceded by '\r' */
    uint64_t crlf;     /* "\r\n" */
    uint64_t cr;       /* '\r' not followed by '\n' (a final '\r' counts) */
    file_eol kind;
    file_eol dominant; /* FILE_EOL_LF or FILE_EOL_CRLF: style for new lines */
    bool has_bom;      /* starts with EF BB BF */
} file_prefix_info;

/* Pure scan; any bytes. Counts are exact over [p, p+n). */
void file_prefix_scan(const uint8_t *p, size_t n, file_prefix_info *out);

/* ---- expectation: ABSENT or PRESENT(os_stat) (Q14) ------------------- */

typedef enum file_presence { FILE_ABSENT = 0, FILE_PRESENT = 1 } file_presence;

/* An immutable binding expectation. Retain and release by copying the
 * value; it owns nothing. stat is meaningful only when PRESENT. */
typedef struct file_expectation {
    file_presence presence;
    os_stat stat;
} file_expectation;

/* A borrowed path/expectation value (erratum 3): the canonical,
 * NUL-terminated name and what it is expected to be. A named absent
 * target carries its name and FILE_ABSENT. Copy it to retain it. */
typedef struct file_target {
    char path[OS_PATH_MAX];          /* canonical, NUL-terminated */
    file_expectation expectation;
} file_target;

enum {
    FILE_CHG_NONE      = 0u,
    FILE_CHG_MTIME     = 1u << 0,
    FILE_CHG_SIZE      = 1u << 1,
    FILE_CHG_INODE     = 1u << 2, /* volume or either inode word */
    FILE_CHG_GONE      = 1u << 3, /* presence differs */
    FILE_CHG_TRUNCATED = 1u << 4, /* mapped inode shorter than the mapping */
    FILE_CHG_METADATA  = 1u << 5, /* kind, perm, owner, group or ctime */
    FILE_CHG_FAULTED   = 1u << 6  /* fault poll failed or epoch != 0 */
};

/* The one classifier: presence first, then identity, size, times, and
 * the remaining stat fields. Both-sides-ABSENT compares equal. The link
 * count is observed, never compared: every link change moves ctime. */
uint32_t file_expectation_diff(const file_expectation *want,
                               const file_expectation *got);

/* Observe a path (symlinks not followed at the leaf: a symlink swapped
 * for the target is a kind change). A missing path or missing parent
 * component is ABSENT with FILE_OK; any other failure is FILE_ERR_IO
 * with *oserr set and *out untouched: a failed observation is never
 * reported as absence. Off the typing path (one stat). */
int file_expectation_observe_path(const char *path, file_expectation *out,
                                  os_err *oserr);
/* Observe path's leaf inside a pinned directory. Same contract. */
int file_expectation_observe_at(const os_dir *dir, const char *name,
                                file_expectation *out, os_err *oserr);
/* Observe an open fd. Always PRESENT on success. */
int file_expectation_observe_fd(const os_file *fd, file_expectation *out,
                                os_err *oserr);

/* The new-file witness of SAVE(PREPARED) (journal.h: volume u64, inode
 * low/high u64, size u64, content CRC32C u32 over all of S, from the
 * written temp's fd; no times, because the rename changes ctime). File
 * computes it while writing the temp and never calls the journal: the
 * caller copies these fields into its PREPARED record. */
typedef struct file_witness {
    uint64_t volume;
    uint64_t index_low;
    uint64_t index_high;
    uint64_t size;
    uint32_t content_crc32c;
} file_witness;

/* CRC-32/ISCSI over [data, data+len), seeded with crc (pass 0 first).
 * File-local because file < journal: the journal's codec is above this
 * module. Agrees bit for bit with journal_crc32c. */
uint32_t file_crc32c(uint32_t crc, const uint8_t *data, size_t len);

/* ---- backing source lease --------------------------------------------- */

/* The nullable owned-backing type (erratum 3): a reference to one
 * acquisition's descriptor and, for a mapped file, mapping and fault
 * registration. */
typedef struct file_backing_impl file_source;

/* Retain the file's actual opened original independently of the file
 * object. Requires full acquisition (FILE_OPEN_READY or ATTACHED). Any
 * thread may retain and release; the final release retires the
 * registration before unmapping, exactly once. */
int file_source_acquire(struct file *f, file_source **out);
file_source *file_source_retain(file_source *source);
void file_source_release(file_source *source);
/* The backing's current baseline: the fd-captured acquisition stat with
 * the ctime our own rename moved it to (the one re-baselining point). */
os_stat file_source_stat(const file_source *source);
file_mode file_source_mode(const file_source *source);
/* Off-path full check (any thread): the held fd still matches the
 * baseline in every compared field (ctime included) and the
 * registration is pristine. FILE_OK, or FILE_ERR_CHANGED / FILE_ERR_IO. */
int file_source_validate(file_source *source, os_err *oserr);

/* The live backing-identity validator undo requires for a mutable
 * mapped document (piece.h piece_validate_fn; this exact signature). A
 * non-null ctx is the struct file, whose installed backing must be the
 * supplied one. A null ctx selects the source of the supplied
 * file-created mapped backing: its piece_backing.ctx is the file_source
 * for that mapping, and the backing is validated against its own
 * acquisition (erratum 5); a backing file did not create is refused. The
 * tree or snapshot reference pins that context through the last read
 * and physical retirement. It neither edits nor blocks and allocates
 * nothing: one fault poll and one fd stat, so a same-size in-place
 * overwrite (pristine epoch) still fails. Runs inside the Law-2 window. */
bool file_backing_validate(void *ctx, const piece_backing *backing);

/* ---- the save file half: temp custody ---------------------------------- */

/* Custody (erratum 4). EMPTY owns nothing. Prepare hands off a synced,
 * unpublished temp in SYNCED. Publish may rename only from SYNCED and at
 * most once. PRE_RENAME may clean up a definitely unpublished temp and
 * finish DONE. INSTALLED_UNSYNCED retains the written fd and original
 * directory for barrier-only retry. VERIFIED, RENAME_UNCERTAIN and
 * CONFLICT retain both descriptors in HELD for verification,
 * reconciliation or discard. DONE owns nothing again. A temp in any
 * owning state also holds its retained file_source, if prepared with
 * one. */
typedef enum file_temp_state {
    FILE_TEMP_EMPTY = 0,
    FILE_TEMP_SYNCED,
    FILE_TEMP_INSTALLED_UNSYNCED,
    FILE_TEMP_HELD,
    FILE_TEMP_DONE
} file_temp_state;

/* Classified publication evidence (savectl seam table, file row 3). */
typedef enum file_pub {
    FILE_PUB_NONE = 0,       /* no publication attempted */
    FILE_PUB_PRE_RENAME,     /* definitely before rename: nothing installed */
    FILE_PUB_RENAME_UNCERTAIN, /* the rename's effect is unknown */
    FILE_PUB_INSTALLED_UNSYNCED, /* renamed; directory barrier failed */
    FILE_PUB_VERIFIED,       /* renamed, barrier done, path == written fd */
    FILE_PUB_CONFLICT        /* post-rename path is foreign or unverifiable,
                              * or the displaced source moved beyond our
                              * own rename */
} file_pub;

typedef struct file_save_outcome {
    file_pub pub;
    file_expectation installed; /* PRESENT fd stat when pub is VERIFIED
                                 * or INSTALLED_UNSYNCED, else untouched */
    os_err oserr;
} file_save_outcome;

/* The displaced source's rename pair (file_temp_publish): NONE when no
 * displaced source was lent or nothing was renamed yet; OURS when the
 * pair showed our own rename only; FOREIGN when it showed more. A
 * FOREIGN pair makes every later verification of this custody CONFLICT,
 * retries included. */
typedef enum file_source_pair {
    FILE_PAIR_NONE = 0,
    FILE_PAIR_OURS,
    FILE_PAIR_FOREIGN
} file_source_pair;

/* Custody of one temp across threads or jobs: fd, directory, temp and
 * destination names, the expectation it was prepared against, its
 * witness and source, all in caller storage. Zero-initialize before
 * first use. Moving custody is a struct copy plus file_temp_forget on
 * the source. */
typedef struct file_temp {
    file_temp_state state;
    os_file fd;              /* RDWR: verification reads it back */
    os_dir dir;
    char tmp[OS_NAME_MAX];   /* "" once nothing at a temp name is ours */
    char base[OS_NAME_MAX];
    file_expectation expect;
    file_witness witness;
    os_stat fd_stat;         /* the temp fd's stat after the sync: the
                              * rename moves only its ctime, so size and
                              * mtime still match at verification */
    file_source *source;     /* retained; NULL for private text */
    file_source_pair pair;
    unsigned flags;          /* FILE_SAVE_FORCE */
} file_temp;

#define FILE_SAVE_FORCE 1u /* publish even if the destination changed */

/* Prepare: refuse a PRESENT expectation that is not a regular file
 * (FILE_ERR_NOTREG, before any open), create the temp beside dest_path,
 * write the snapshot's whole text (computing the witness), give it the
 * destination's metadata when the destination still is the expected
 * regular file (identity-checked through a non-blocking open; a
 * mismatch is FILE_ERR_CHANGED unless FORCE), else the new-file mode
 * 0666 & ~os_creation_mask(), sync, and capture the fd stat. The temp is
 * created 0600 & ~mask, so it is never wider than a new file's result
 * (P: (0600 & ~m) is a subset of (0666 & ~m)) and grants group and other
 * nothing an existing destination's mode lacks. source (nullable,
 * borrowed for the call) is the original the snapshot reads; custody
 * retains it for publish. It is validated after the write only when the
 * snapshot reads a mapped backing: a copy's bytes are private. Source
 * validation protects bytes read from the source; destination
 * expectation protects what is overwritten. On
 * FILE_OK the temp is SYNCED and nothing has been renamed. A fault, a
 * changed source or a cancel discards the temp. Any thread, one at a
 * time per temp. snap is borrowed for the call. */
int file_temp_prepare(file_temp *t, const char *dest_path,
                      const piece_snapshot *snap, file_source *source,
                      const file_expectation *expect, unsigned flags,
                      os_err *oserr);
/* Publish a SYNCED temp: re-observe the destination against the stored
 * expectation (unless FORCE), rename once (NOREPLACE when the
 * expectation is ABSENT and not FORCE), sync the directory, verify the
 * path against the written fd. When the custody's source is the
 * displaced inode, its fd is stat'ed immediately before and after the
 * rename: a pair differing in ctime and a falling link count only
 * re-baselines that backing (so it keeps validating its intact bytes);
 * any other difference is FILE_PUB_CONFLICT. Fills *out on every path
 * after the state check; the return is FILE_OK only for
 * FILE_PUB_VERIFIED. Custody afterwards: DONE on PRE_RENAME,
 * INSTALLED_UNSYNCED on a failed barrier, HELD otherwise. */
int file_temp_publish(file_temp *t, file_save_outcome *out);
/* Barrier-only retry: sync the original directory again and re-verify;
 * never renames. From INSTALLED_UNSYNCED, and from HELD (after an
 * uncertain rename) once the named path is proven to be the held fd:
 * the proof is checked first, and without it nothing is synced. Same
 * outcome contract; VERIFIED and CONFLICT leave custody HELD. */
int file_temp_retry_barrier(file_temp *t, file_save_outcome *out);
/* Compare the named destination to the held fd and verify the full
 * prepared witness (identity, size and content CRC32C, read back from
 * the fd) on a stable observation: the fd stat and the path's identity
 * are re-taken after the read and must be unchanged. On success return
 * the post-rename fd expectation and the CRC32C over the first
 * min(size, FILE_WIRE_PREFIX_BYTES) bytes. A mismatch reports
 * FILE_ERR_CHANGED, an observation failure its actual error. Failure
 * preserves custody. INSTALLED_UNSYNCED or HELD only (FILE_ERR_STATE);
 * off the input path (it reads the whole file). */
int file_temp_verify(const file_temp *t, file_expectation *installed,
                     uint32_t *prefix_crc32c, os_err *oserr);
/* Take an EMPTY (or DONE) temp and a selected open PREPARED's path and
 * witness. Open the named candidate (non-blocking, leaf symlinks not
 * followed) and its parent; on a stable fd/path match to the whole
 * witness, transfer the descriptors, destination name and witness to
 * the temp in INSTALLED_UNSYNCED. Finish with file_temp_retry_barrier.
 * On mismatch (FILE_ERR_CHANGED: absent, unreadable or different;
 * FILE_ERR_NOTREG: a symlink or directory) or I/O failure release every
 * partial acquisition and leave the temp EMPTY. */
int file_temp_recover(file_temp *t, const char *path,
                      const file_witness *prepared, os_err *oserr);
/* Release custody and reach DONE. A SYNCED temp is still ours and
 * definitely unpublished: it is unlinked, but only while its name still
 * names the held fd. INSTALLED_UNSYNCED and HELD only close: an
 * uncertain rename grants no authority to unlink the destination or a
 * reused temp name. */
void file_temp_discard(file_temp *t);
/* Forget custody without touching descriptors or the source reference
 * (after a struct copy). Leaves the temp EMPTY. */
void file_temp_forget(file_temp *t);
/* One-shot synchronous save: prepare, then publish, then discard the
 * retained custody before returning. *witness (optional) receives the
 * temp's witness once preparation succeeds, whatever publication then
 * does. source as for file_temp_prepare. */
int file_save_write(const char *dest_path, const piece_snapshot *snap,
                    file_source *source, const file_expectation *expect,
                    unsigned flags, file_witness *witness,
                    file_save_outcome *out);

/* ---- the file object ---------------------------------------------------- */

/* Worker-private progress of one job across continuation invocations:
 * the result record is complete and only its message is outstanding. */
typedef enum file_job_progress {
    FILE_JOB_WORKING = 0,
    FILE_JOB_RESULT_READY
} file_job_progress;

typedef struct file_job_slot {
    work_handle h;           /* the job this slot names; epoch 0: none */
    uint32_t generation;     /* the generation it was submitted with */
    _Atomic int progress;    /* file_job_progress */
} file_job_slot;

/* The open lifecycle (UI). */
typedef enum file_open_state {
    FILE_OPEN_NONE = 0,  /* zeroed, or acquisition in flight: nothing visible */
    FILE_OPEN_PREFIX,    /* prefix fields visible, bytes borrowed from the
                          * job record (which only the UI frees) */
    FILE_OPEN_READY,     /* acquisition installed; prefix and backing owned */
    FILE_OPEN_ATTACHED,  /* READY, and file_open_tree built the tree */
    FILE_OPEN_FAILED     /* refused; the borrowed prefix was withdrawn */
} file_open_state;

typedef enum file_reload_state {
    FILE_RELOAD_NONE = 0,
    FILE_RELOAD_ACQUIRING, /* job submitted, result not yet installed */
    FILE_RELOAD_READY      /* acquired, not yet taken or discarded */
} file_reload_state;

typedef enum file_watch_state { FILE_WATCH_OFF = 0, FILE_WATCH_ON } file_watch_state;

typedef enum file_save_phase {
    FILE_SAVE_IDLE = 0,    /* no job, no custody */
    FILE_SAVE_PREPARING,   /* prepare job in flight */
    FILE_SAVE_PREPARED,    /* SYNCED temp in custody, awaiting publish */
    FILE_SAVE_PUBLISHING,  /* publish or barrier-retry job in flight */
    FILE_SAVE_UNSYNCED,    /* installed; barrier retry still owed */
    FILE_SAVE_HELD,        /* VERIFIED, RENAME_UNCERTAIN or CONFLICT:
                            * descriptors held until abandon */
    FILE_SAVE_CANCELLING   /* abandoned while a job was in flight */
} file_save_phase;

typedef enum file_save_op {
    FILE_SAVE_OP_PREPARE = 0,
    FILE_SAVE_OP_PUBLISH,
    FILE_SAVE_OP_RETRY
} file_save_op;

typedef struct file_open_opts {
    uint64_t copy_threshold; /* 0 = file_default_copy_threshold() */
    uint32_t generation;     /* copied into every open message */
} file_open_opts;

/* Message kinds (work_msg.kind). Payload: file_msg_decode. */
enum {
    FILE_MSG_OPEN_READY    = 0x46490001u,
    FILE_MSG_OPEN_FAILED   = 0x46490002u,
    FILE_MSG_SAVE_DONE     = 0x46490003u,
    FILE_MSG_PREFIX_READY  = 0x46490004u,
    FILE_MSG_SAVE_PREPARED = 0x46490005u,
    FILE_MSG_RELOAD_READY  = 0x46490006u,
    FILE_MSG_RELOAD_FAILED = 0x46490007u
};

typedef struct file_msg {
    uint32_t kind;
    uint32_t generation;
    struct file *f;
    int32_t status;   /* FILE_OK or a file_err */
    uint32_t mode;    /* file_mode (open/reload) or file_pub (save) */
    uint64_t size;    /* bytes acquired (open) / witness size (save) */
    int32_t oserr;    /* os_err behind FILE_ERR_IO */
} file_msg;

/* Worker result records. The prefix part (prefix_arena through size) is
 * written before PREFIX_READY and never again by the job; backing,
 * status and oserr are written before the final message. Only the UI
 * frees the record (decode or close). */
struct file_acquired {
    base_arena prefix_arena;
    uint8_t *prefix;
    size_t prefix_len;
    file_prefix_info info;
    file_expectation baseline;
    char path[OS_PATH_MAX];
    file_mode mode;
    uint64_t size;
    file_source *backing;
    int status;
    os_err oserr;
};

struct file {
    work_pool *pool;
    uint64_t threshold;
    uint32_t open_generation;
    char requested_path[OS_PATH_MAX]; /* immutable job input */
    char path[OS_PATH_MAX];           /* canonical, valid from PREFIX */
    os_err oserr;                     /* behind the last FILE_ERR_IO */

    /* Installed acquisition (UI-owned after decode). */
    file_open_state open_state;
    file_source *backing;
    base_arena prefix_arena;          /* owned from READY */
    const uint8_t *prefix;
    size_t prefix_len;
    file_prefix_info info;
    file_expectation baseline;
    file_mode mode;
    uint64_t size;
    bool changed;          /* sticky, set by file_check / poll detection */
    uint32_t last_reasons; /* FILE_CHG_* of the latest check */

    /* The owned watch object: one kernel queue and one slot per file,
     * no process global (harvest §0.3). Borrow its fd for a waitset. */
    os_watch watch;
    os_watch_slot watch_slot;
    os_watch_id watch_id;
    file_watch_state watch_state;
    char watch_name[OS_NAME_MAX];

    file_job_slot open_job, save_job, reload_job;
    struct file_acquired open_result;
    struct file_acquired reload_result;
    file_reload_state reload_state;

    /* Save pipeline. Inputs are captured at file_save_begin; custody
     * and outcome cross the prepare/publish job boundary and are
     * touched only by the slot's job while it runs. */
    file_save_phase save_phase;
    file_save_op save_op;
    piece_snapshot *save_snap;     /* retained until the prepare settles */
    file_source *save_source;      /* same lifetime; custody keeps its own */
    char save_dest[OS_PATH_MAX];
    file_expectation save_expect;
    unsigned save_flags;
    file_temp save_temp;
    file_save_outcome save_outcome;
    int save_status;
    uint32_t save_msg_kind; /* the outstanding save message to republish */

    /* Set only through file_test.h under FILE_TEST_HOOKS; no shipped
     * code reads these fields (the hook calls compile only there). */
    void (*step_hook)(void *ctx, int step);
    void *step_ctx;
};

/* UI-only decode of every pool notification. Returns 0 and fills *out
 * for a message of the job a slot of a live file currently names,
 * installing state before exposing it; returns 1 for unrelated, stale or
 * superseded messages. Single consumer: the one UI dispatcher decodes
 * each message once. The file must be alive: storage may be freed only
 * after file_close_finish (whose begin made every message stale). */
int file_msg_decode(const work_msg *m, file_msg *out);

/* ---- open / close / reload ---- */

/* Begin acquiring path into zeroed caller storage *f. Submits the
 * acquisition job; FILE_ERR_POOL leaves *f safe to close. */
int file_open_begin(file *f, work_pool *pool, const char *path,
                    const file_open_opts *opts);
/* Non-blocking two-phase close: begin cancels every job this file
 * submitted (making their messages stale) and drops the watch; ready
 * reports physical completion of every handle (work_handle_finished
 * each), whether or not its result was ever decoded; finish settles a
 * cancelled save (an unpublished temp is unlinked, held descriptors are
 * closed) and releases the backing reference, any untaken reload or
 * undelivered result, and the prefix arena. Finish ASSERTs readiness.
 * Snapshots and trees keep their own backing references. */
void file_close_begin(file *f);
bool file_close_ready(file *f);
void file_close_finish(file *f);

const uint8_t *file_prefix(const file *f, size_t *len); /* NULL, 0 unless visible */
bool file_prefix_ready(const file *f);
const file_prefix_info *file_prefix_info_of(const file *f);
uint64_t file_size(const file *f);
file_mode file_open_mode(const file *f);
const char *file_path(const file *f); /* canonical; "" while pending */
os_err file_os_err(const file *f);
bool file_open_ready(const file *f);
/* The acquired baseline expectation (borrowed; copy to retain). Valid
 * from PREFIX_READY. This is the identity a one-buffer-per-identity
 * index keys on: stat.id of a PRESENT baseline (A23 rule 6). */
const file_expectation *file_baseline(const file *f);

/* Build the tree from the acquired content: piece_open_copy below the
 * threshold, piece_open_mapped at or above it, over the backing that
 * was registered before publication. Valid once, at OPEN_READY. */
int file_open_tree(file *f, piece_reserve *reserve, piece_tree **out);

/* Reload acquisition through the same registered construction. On
 * RELOAD_READY, file_reload_tree builds a fresh tree and adopts the new
 * backing and baseline; the old tree keeps reading its own backing.
 * RELOAD_FAILED (including FILE_ERR_NOSPC on registration) leaves the
 * old tree, registration and baseline exactly as they were.
 * file_reload_discard drops an untaken reload. */
int file_reload_begin(file *f, uint32_t generation);
int file_reload_tree(file *f, piece_reserve *reserve, piece_tree **out);
void file_reload_discard(file *f);

/* ---- change detection ---- */

/* Observe path + held fd + one closing fault poll against the
 * baselines. Returns true when changed (sticky); *reasons (optional)
 * receives this check's FILE_CHG_* bits. A failed observation (path or
 * fd) is a change: FILE_CHG_METADATA, and file_os_err says why. UI
 * thread; two stats. */
bool file_check(file *f, uint32_t *reasons);
bool file_changed(const file *f);
/* "Keep mine": adopt the observed disk state as the new path baseline,
 * but only while the original's bytes provably remain the acquired
 * ones. A copy backing can keep anything. A mapped backing can keep a
 * changed path only while it still validates in full (fd stat with
 * ctime, pristine poll): any move of the mapped inode's ctime, a dropped
 * link included, is indistinguishable from an in-place rewrite with
 * mtime restored through another link or a held descriptor, so Keep
 * refuses (FILE_ERR_CHANGED, baseline preserved) and the caller
 * reloads (A23 rule 7). */
int file_resolve_keep(file *f);
/* Watch the file's parent directory on the owned watch object.
 * file_watch_poll drains its queue (never waits); an event naming this
 * file (or the directory itself, or a queue overflow) runs file_check
 * and returns the sticky state. Refresh after a verified save. */
int file_watch_start(file *f);
void file_watch_stop(file *f);
int file_watch_fd(const file *f); /* borrowed: os_waitset_add, -1 if none */
bool file_watch_poll(file *f);

/* ---- asynchronous save ---- */

/* Begin preparing a save of *snap (borrowed; retained until the prepare
 * settles) to dest_path against *expect. Posts FILE_MSG_SAVE_PREPARED
 * with the synced temp in custody, or FILE_MSG_SAVE_DONE with the
 * refusal. The caller then writes its journal PREPARED from
 * file_save_witness and calls file_save_publish, or file_save_abandon.
 * A save over the file's own path passes file_path(f) and
 * file_baseline(f). FILE_ERR_BUSY while any custody is held. */
int file_save_begin(file *f, piece_snapshot *snap, const char *dest_path,
                    const file_expectation *expect, unsigned flags,
                    uint32_t generation);
/* Publish the prepared temp (FILE_SAVE_PREPARED only). Posts
 * FILE_MSG_SAVE_DONE; the phase follows custody: IDLE on PRE_RENAME,
 * UNSYNCED on FILE_ERR_DIRSYNC, HELD on VERIFIED, RENAME_UNCERTAIN or
 * CONFLICT. A VERIFIED save over file_path(f) adopts the installed
 * baseline at decode. */
int file_save_publish(file *f);
/* Barrier-only retry (file_temp_retry_barrier) from UNSYNCED, or from
 * HELD after an uncertain rename. Posts FILE_MSG_SAVE_DONE. */
int file_save_retry_barrier(file *f);
/* Cancel or discard. With a job in flight: cancel it (CANCELLING).
 * Once it is physically finished, the next non-const call on the file
 * (abandon again, begin, check, close) settles it exactly as its
 * message would have: exposed effects are kept (a verified baseline is
 * adopted; UNSYNCED or HELD custody stays for the caller), and only a
 * synced, unpublished temp is discarded. With custody and no job
 * (PREPARED, UNSYNCED, HELD): discard it and return to IDLE; the last
 * outcome stays readable. */
void file_save_abandon(file *f);
/* Pure queries: no I/O. busy is true while a job runs, including a
 * cancelled one not yet physically finished; phase_of reports the phase
 * a finished cancellation will settle into. */
bool file_save_busy(const file *f);
file_save_phase file_save_phase_of(const file *f);
/* The prepared temp's witness; valid in PREPARED, UNSYNCED and HELD. */
const file_witness *file_save_witness(const file *f);
/* The last save outcome; valid after SAVE_DONE, or once a cancelled job
 * is no longer busy. On a verified publish to file_path(f) the baseline
 * names the installed file once settled. */
const file_save_outcome *file_save_outcome_of(const file *f);

#endif
