/* Law 2 over the persistence seams (L2-0a, docs/decisions/L2-0a.md): each
 * os_watch call and os_file_copy_metadata, in its own window under the live
 * release allocation guard, with real events and real user.* xattrs under
 * build/. Fails closed on an inert guard or a refused xattr fixture. */
#include "base/base.h"
#include "os/os.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/xattr.h>
#include <unistd.h>

static bool guard_live(void)
{
    /* Positive control: a volatile function pointer forces a real call. */
    void *(*volatile allocate)(size_t) = malloc;
    base_malloc_guard_begin();
    void *p = allocate(1);
    size_t count = base_malloc_guard_end();
    bool allocated = p != NULL;
    free(p);
    bool active = base_malloc_guard_active();
    printf("guard positive control: active=%d count=%zu\n", (int)active, count);
    return active && allocated && count > 0;
}

static bool all_ok = true;
static void window_end(const char *call, bool ok)
{
    size_t count = base_malloc_guard_end();
    bool active = base_malloc_guard_active();
    printf("Law-2 window %-22s active=%d count=%zu ok=%d\n", call, (int)active, count, (int)ok);
    all_ok = all_ok && active && count == 0 && ok;
}

static char root[OS_PATH_MAX];
static os_watch_slot slots[4];
static os_watch w = OS_WATCH_INIT;
static os_xattr_scratch scratch;

static int open_leaf(const char *leaf)
{
    char p[OS_PATH_MAX];
    int n = snprintf(p, sizeof p, "%s/%s", root, leaf);
    ASSERT(n > 0 && (size_t)n < sizeof p);
    int fd = open(p, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    ASSERT(fd >= 0);
    return fd;
}
static void xset(int fd, const char *name, const char *v)
{
    if (fsetxattr(fd, name, v, strlen(v), 0) != 0) {
        fprintf(stderr, "law2_os_persist_test: FAIL fsetxattr %s under %s: %s; this lane "
                        "requires a filesystem with user.* xattrs\n", name, root, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    if (!guard_live()) {
        fputs("law2_os_persist_test: FAIL the allocation guard is inert\n", stderr);
        return EXIT_FAILURE;
    }
    char cwd[OS_PATH_MAX];
    os_err rc = os_cwd(cwd, sizeof cwd);
    ASSERT(rc == OS_OK);
    int n = snprintf(root, sizeof root, "%s/build/law2-os-persist-XXXXXX", cwd);
    ASSERT(n > 0 && (size_t)n < sizeof root);
    ASSERT(mkdtemp(root) != NULL);

    base_malloc_guard_begin();
    bool ok = os_watch_init(&w, slots, 4) == OS_OK;
    window_end("os_watch_init", ok);
    os_watch_id id = 0, again = 0;
    base_malloc_guard_begin();
    ok = os_watch_add(&w, root, &id) == OS_OK && os_watch_add(&w, root, &again) == OS_OK &&
         again == id;
    window_end("os_watch_add", ok);

    int s = open_leaf("src"), d = open_leaf("dst"); /* events: CREATE, CREATE */
    xset(s, "user.a", "alpha");
    xset(s, "user.b", "beta");
    xset(d, "user.b", "old");
    xset(d, "user.stale", "x");

    os_watch_event ev[2];
    size_t got = 0, total = 0;
    base_malloc_guard_begin();
    os_err e = OS_OK;
    while ((e = os_watch_read(&w, ev, 2, &got)) == OS_OK) total += got;
    window_end("os_watch_read", e == OS_E_AGAIN && total >= 2);

    os_file src = os_file_from_fd(s), dst = os_file_from_fd(d);
    base_malloc_guard_begin();
    ok = os_file_copy_metadata(&dst, &src, &scratch) == OS_OK;
    window_end("os_file_copy_metadata", ok);
    char v[8];
    ok = fgetxattr(d, "user.b", v, sizeof v) == 4 && memcmp(v, "beta", 4) == 0 &&
         fgetxattr(d, "user.stale", v, sizeof v) < 0 && errno == ENODATA;
    printf("copy result checked: %d\n", (int)ok);
    all_ok = all_ok && ok;

    base_malloc_guard_begin();
    os_watch_remove(&w, id);
    os_watch_remove(&w, again);
    os_watch_close(&w);
    window_end("os_watch_remove+close", true);

    ASSERT(close(s) == 0 && close(d) == 0);
    os_dir dir = OS_DIR_INIT;
    rc = os_dir_open(&dir, root);
    ASSERT(rc == OS_OK);
    rc = os_unlink_at(&dir, "src");
    ASSERT(rc == OS_OK);
    rc = os_unlink_at(&dir, "dst");
    ASSERT(rc == OS_OK);
    os_dir_close(&dir);
    rc = os_rmdir(root);
    ASSERT(rc == OS_OK);
    puts(all_ok ? "law2_os_persist_test: ok" : "law2_os_persist_test: FAIL");
    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
