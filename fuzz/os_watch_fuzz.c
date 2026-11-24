/* Model-based fuzzer for the directory-change watch (L2-0a,
 * docs/decisions/L2-0a.md): the hold table under interleaved add and remove,
 * and the record decoder under arbitrary kernel bytes. The fuzz link wraps
 * read(2) beneath os (Makefile OS_WATCH_FUZZ_WRAPS): a read of the watch's
 * descriptor returns the bytes this input forged (truncated records, names
 * without a NUL, lengths past the end, overflow among real events, records
 * of removed or ended watches) or EAGAIN; every other descriptor reads the
 * kernel. inotify_add_watch and inotify_rm_watch are wrapped only to observe
 * the kernel's wd and count removals; they still reach the kernel, on real
 * directories that always exist. Each input checks, against a model:
 *   - add returns the held id of a live wd, else the first free slot, else
 *     OS_E_NOSPC with the new kernel watch withdrawn;
 *   - remove ends the kernel watch exactly at the last hold of a live slot;
 *   - the events of a read sequence, flattened, equal a reference decoding
 *     of the forged bytes (ids, kinds, names), with OS_E_OVERFLOW and OS_E_IO
 *     at the right places, and OS_OK with *n < cap leaves nothing in the
 *     object (the next call reads). Misuse is an ASSERT (A15), so the model
 *     issues only valid calls. */
#include "os/os.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#define CHECK(x) ((x) ? (void)0 : check_fail(#x, __LINE__))
static _Noreturn void check_fail(const char *x, int line)
{
    fprintf(stderr, "os_watch_fuzz.c:%d: FAIL %s\n", line, x);
    abort();
}

/* ---- the wrapped kernel ------------------------------------------------- */
static int watch_fd = -1;
static unsigned char forged[OS_WATCH_BUF];
static size_t forged_len;
static bool forged_ready;
static unsigned reads;    /* reads of the watch descriptor */
static int last_wd = -1;  /* the kernel's answer to the last add */
static unsigned removals; /* inotify_rm_watch calls */

ssize_t __real_read(int, void *, size_t);
int __real_inotify_add_watch(int, const char *, uint32_t);
int __real_inotify_rm_watch(int, int);
ssize_t __wrap_read(int, void *, size_t);
int __wrap_inotify_add_watch(int, const char *, uint32_t);
int __wrap_inotify_rm_watch(int, int);

ssize_t __wrap_read(int fd, void *buf, size_t n)
{
    if (watch_fd < 0 || fd != watch_fd) return __real_read(fd, buf, n);
    reads++;
    if (!forged_ready) {
        errno = EAGAIN;
        return -1;
    }
    forged_ready = false;
    CHECK(n >= forged_len);
    memcpy(buf, forged, forged_len);
    return (ssize_t)forged_len;
}
int __wrap_inotify_add_watch(int fd, const char *path, uint32_t mask)
{
    last_wd = __real_inotify_add_watch(fd, path, mask);
    return last_wd;
}
int __wrap_inotify_rm_watch(int fd, int wd)
{
    removals++;
    return __real_inotify_rm_watch(fd, wd);
}

/* ---- input cursor ------------------------------------------------------- */
static const uint8_t *in;
static size_t in_len, in_at;
static bool more(void) { return in_at < in_len; }
static uint8_t byte(void) { return in_at < in_len ? in[in_at++] : 0; }
static uint32_t word(void)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v = v << 8 | byte();
    return v;
}

/* ---- the model ---------------------------------------------------------- */
/* Directories every Linux system has; "." may alias one of them, which the
 * model handles because it keys holds by the kernel's wd, as os does. */
static const char *const dirs[] = {".", "/", "/usr", "/etc"};
#define DIRS (sizeof dirs / sizeof dirs[0])
/* (P) Three slots for four directories, so both a free slot and exhaustion
 * are reachable; the forged records below also name unheld wds. */
#define SLOTS 3u
typedef struct model_slot { int wd; uint32_t holds; } model_slot;
static model_slot model[SLOTS];

/* A record matches only a live kernel watch: an ended slot's -1 is no wd. */
static uint32_t model_slot_of(int wd)
{
    if (wd < 0) return SLOTS;
    for (uint32_t i = 0; i < SLOTS; i++)
        if (model[i].holds && model[i].wd == wd) return i;
    return SLOTS;
}

/* Reference decoding of one forged buffer: the events os must deliver,
 * then how the sequence ends. */
typedef struct ref_event { os_watch_id id; os_watch_what what; char name[OS_NAME_MAX]; } ref_event;
enum { REF_EVENT, REF_OVERFLOW };
typedef struct ref_item { int kind; ref_event ev; } ref_item;
/* (P) A record is at least 16 bytes, so one buffer holds at most 4096/16. */
#define REF_CAP (OS_WATCH_BUF / sizeof(struct inotify_event) + 1)
static ref_item ref[REF_CAP];
static size_t ref_n;
static bool ref_malformed;

static bool ref_what(uint32_t mask, os_watch_what *what)
{
    if (mask & (IN_UNMOUNT | IN_DELETE | IN_DELETE_SELF)) *what = OS_WATCH_DELETE;
    else if (mask & IN_CREATE) *what = OS_WATCH_CREATE;
    else if (mask & (IN_MOVED_FROM | IN_MOVE_SELF)) *what = OS_WATCH_MOVED_OUT;
    else if (mask & IN_MOVED_TO) *what = OS_WATCH_MOVED_IN;
    else if (mask & (IN_MODIFY | IN_CLOSE_WRITE)) *what = OS_WATCH_MODIFY;
    else if (mask & IN_ATTRIB) *what = OS_WATCH_ATTRIB;
    else return false;
    return true;
}
/* Decodes forged[] against the model, applying IN_IGNORED to it as os does. */
static void reference(void)
{
    ref_n = 0;
    ref_malformed = forged_len == 0; /* (P) the kernel never returns 0 */
    size_t at = 0;
    while (!ref_malformed && at < forged_len) {
        struct inotify_event h;
        size_t left = forged_len - at;
        if (left < sizeof h) {
            ref_malformed = true;
            break;
        }
        memcpy(&h, forged + at, sizeof h);
        const char *name = (const char *)forged + at + sizeof h;
        if (h.len > left - sizeof h || (h.len && strnlen(name, h.len) == h.len)) {
            ref_malformed = true;
            break;
        }
        at += sizeof h + h.len;
        if (h.mask & IN_Q_OVERFLOW) {
            ref[ref_n++].kind = REF_OVERFLOW;
            continue;
        }
        uint32_t i = model_slot_of(h.wd);
        os_watch_what what = OS_WATCH_ENDED;
        if (i == SLOTS) continue;
        if (h.mask & IN_IGNORED) model[i].wd = -1;
        else if (!ref_what(h.mask, &what)) continue;
        ref_item *r = &ref[ref_n++];
        r->kind = REF_EVENT;
        r->ev.id = i;
        r->ev.what = what;
        const char *shown = h.len && !(h.mask & IN_UNMOUNT) ? name : "";
        memcpy(r->ev.name, shown, strlen(shown) + 1);
    }
}

/* ---- forging records ---------------------------------------------------- */
static const uint32_t masks[] = {
    IN_CREATE, IN_DELETE, IN_MOVED_FROM, IN_MOVED_TO, IN_MODIFY, IN_CLOSE_WRITE,
    IN_ATTRIB, IN_DELETE_SELF, IN_MOVE_SELF, IN_IGNORED, IN_UNMOUNT, IN_Q_OVERFLOW,
    IN_CREATE | IN_ISDIR, IN_OPEN /* never asked for */, IN_DELETE_SELF | IN_IGNORED,
};
/* A wd: one the model holds, one it does not, or any. */
static int forge_wd(void)
{
    uint8_t k = byte();
    switch (k % 4) {
    case 0: case 1: return model[k / 4 % SLOTS].wd;
    case 2: return last_wd;
    default: return (int)word();
    }
}
/* Appends one record; the length byte chooses an exact padded name, an empty
 * one, a name without its NUL, or a length past the end of the buffer. */
static void forge_record(void)
{
    struct inotify_event h;
    memset(&h, 0, sizeof h);
    h.wd = forge_wd();
    uint8_t m = byte();
    h.mask = m < 240 ? masks[m % (sizeof masks / sizeof masks[0])] : word();
    h.cookie = 0;
    uint8_t shape = byte(), nlen = byte() % 24;
    char name[32];
    memset(name, 0, sizeof name);
    for (uint8_t i = 0; i < nlen; i++) name[i] = (char)(byte() | 1); /* no early NUL */
    size_t body;
    switch (shape % 4) {
    case 0: body = 0; break;                             /* no name */
    case 1: body = (nlen + 1u + 7u) & ~(size_t)7u; break; /* padded, NUL-ended */
    case 2: body = nlen; break;                          /* no NUL (empty when nlen 0) */
    default: body = 32u + shape; break;                  /* past what follows */
    }
    h.len = (uint32_t)body;
    if (forged_len + sizeof h > sizeof forged) return;
    memcpy(forged + forged_len, &h, sizeof h);
    forged_len += sizeof h;
    size_t copy = shape % 4 == 3 ? (size_t)(byte() % 32u) : body;
    if (copy > sizeof name) copy = sizeof name;
    if (forged_len + copy > sizeof forged) copy = sizeof forged - forged_len;
    memcpy(forged + forged_len, name, copy);
    forged_len += copy;
}

/* ---- operations ---------------------------------------------------------- */
static os_watch w = OS_WATCH_INIT;
static os_watch_slot slots[SLOTS];

static void op_add(void)
{
    const char *path = dirs[byte() % DIRS];
    unsigned removed = removals;
    last_wd = -1;
    os_watch_id id = 99;
    os_err e = os_watch_add(&w, path, &id);
    if (last_wd < 0) { /* the kernel refused: nothing changes */
        CHECK(e != OS_OK && removals == removed);
        return;
    }
    uint32_t held = model_slot_of(last_wd), free_slot = SLOTS;
    for (uint32_t i = 0; i < SLOTS && free_slot == SLOTS; i++)
        if (!model[i].holds) free_slot = i;
    if (held < SLOTS) {
        CHECK(e == OS_OK && id == held && removals == removed);
        model[held].holds++;
    } else if (free_slot < SLOTS) {
        CHECK(e == OS_OK && id == free_slot && removals == removed);
        model[free_slot].wd = last_wd;
        model[free_slot].holds = 1;
    } else {
        CHECK(e == OS_E_NOSPC && removals == removed + 1); /* withdrawn */
    }
}
static void op_remove(void)
{
    uint32_t i = byte() % SLOTS;
    if (!model[i].holds) return;
    unsigned removed = removals;
    bool last = model[i].holds == 1, live = model[i].wd >= 0;
    os_watch_remove(&w, i);
    model[i].holds--;
    if (last) model[i].wd = -1;
    CHECK(removals == removed + (last && live ? 1u : 0u));
}
/* Forges one buffer, then reads it out with the input's caps, comparing with
 * the reference until os reports the object empty (OS_E_AGAIN) or drained
 * (OS_E_IO). */
static void op_read(void)
{
    forged_len = 0;
    uint8_t records = byte() % 10;
    if (byte() & 1) { /* raw bytes, verbatim */
        size_t n = byte();
        while (n-- && more() && forged_len < sizeof forged) forged[forged_len++] = byte();
    } else {
        for (uint8_t r = 0; r < records; r++) forge_record();
        size_t cut = byte() % 8; /* truncate the tail */
        if (cut < 4 && forged_len > cut) forged_len -= cut;
    }
    forged_ready = true;
    reference();

    size_t next = 0; /* next ref item to match */
    bool must_read = false, done = false;
    for (int calls = 0; !done; calls++) {
        CHECK(calls <= 2 * (int)REF_CAP + 4);
        os_watch_event ev[8];
        size_t cap = 1u + byte() % 8u, n = 99;
        unsigned before = reads;
        os_err e = os_watch_read(&w, ev, cap, &n);
        if (must_read) CHECK(reads == before + 1);
        CHECK(n <= cap);
        for (size_t k = 0; k < n; k++) {
            CHECK(next < ref_n && ref[next].kind == REF_EVENT);
            const ref_event *r = &ref[next++].ev;
            CHECK(ev[k].id == r->id && ev[k].what == r->what && ev[k].name &&
                  strcmp(ev[k].name, r->name) == 0);
        }
        switch (e) {
        case OS_OK:
            CHECK(n > 0 || forged_len > 0);
            must_read = n < cap;
            if (must_read) /* nothing left here: the reference has nothing undelivered */
                CHECK(next == ref_n && !ref_malformed);
            break;
        case OS_E_OVERFLOW:
            CHECK(next < ref_n && ref[next].kind == REF_OVERFLOW);
            next++;
            must_read = false;
            break;
        case OS_E_IO:
            CHECK(ref_malformed && next == ref_n && errno == 0);
            must_read = true; /* drained */
            break;
        case OS_E_AGAIN:
            CHECK(n == 0 && next == ref_n && !forged_ready);
            done = true;
            break;
        case OS_E_NOENT: case OS_E_EXIST: case OS_E_ACCESS: case OS_E_BUSY: case OS_E_LOOP:
        case OS_E_NOTDIR: case OS_E_ISDIR: case OS_E_XDEV: case OS_E_NOSPC:
        case OS_E_NAMETOOLONG: case OS_E_INVAL: case OS_E_NOTSUP: case OS_E_TIMEDOUT:
        case OS_E_NOMEM: case OS_E_RANGE: case OS_E_INTR: case OS_E_REFUSED:
            check_fail("unexpected result", __LINE__);
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len)
{
    in = data;
    in_len = len;
    in_at = 0;
    for (uint32_t i = 0; i < SLOTS; i++) model[i] = (model_slot){-1, 0};
    forged_ready = false;
    if (os_watch_init(&w, slots, SLOTS) != OS_OK) abort(); /* fixture, not input */
    watch_fd = os_watch_fd(&w);
    while (more()) {
        switch (byte() % 3) {
        case 0: op_add(); break;
        case 1: op_remove(); break;
        default: op_read(); break;
        }
    }
    /* Holds left are released by the close, which ends every kernel watch. */
    watch_fd = -1;
    os_watch_close(&w);
    return 0;
}
