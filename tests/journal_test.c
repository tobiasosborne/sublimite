#include "journal/journal.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "journal_test:%d FAIL %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct model { uint8_t text[32768]; size_t len; uint64_t records; uint32_t views, tabs, windows; } model;
static uint64_t get64(const uint8_t *p) { uint64_t x = 0; for (unsigned i=0;i<8;i++) x |= (uint64_t)p[i] << (8*i); return x; }
static int apply(void *ctx, const journal_record *r)
{
    model *m = ctx;
    if (r->type == JOURNAL_INSERT) {
        uint64_t off = get64(r->data); size_t n = r->size - 8;
        if (off > m->len || n > sizeof m->text - m->len) return 1;
        memmove(m->text + (size_t)off + n, m->text + (size_t)off, m->len - (size_t)off);
        memcpy(m->text + (size_t)off, r->data + 8, n); m->len += n;
    } else if (r->type == JOURNAL_DELETE) {
        uint64_t off = get64(r->data), n = get64(r->data + 8);
        if (off > m->len || n > m->len - off) return 1;
        memmove(m->text + (size_t)off, m->text + (size_t)(off+n), m->len - (size_t)(off+n)); m->len -= (size_t)n;
    } else if (r->type == JOURNAL_VIEW) m->views++;
    else if (r->type == JOURNAL_TABS) m->tabs++;
    else if (r->type == JOURNAL_WINDOW) m->windows++;
    m->records++; return 0;
}
static int reject(void *ctx, const journal_record *r) { (void)ctx; (void)r; return 1; }
static void pause_ms(void) { struct timespec t = {0, 1000000}; nanosleep(&t, NULL); }
static void other_message(work_ctx *c)
{ work_msg msg={.kind=42}; (void)work_publish(c,&msg); while(!work_should_stop(c)) pause_ms(); }
static void other_route(const work_msg *m, void *ctx)
{ if(m->kind==42) (*(unsigned *)ctx)++; }
static int apply_tree(void *ctx, const journal_record *r)
{ return r->type==JOURNAL_BASE?0:journal_apply_piece(ctx,r); }
static void blocked(work_ctx *c) { while (!work_should_stop(c)) pause_ms(); }
static void route(const work_msg *m, void *ctx) { (void)journal_receive(ctx, m); }
static int temp(char *p) { int fd = mkstemp(p); if (fd >= 0) close(fd); return fd; }
int main(void)
{
    char path[] = "/tmp/journal-test-XXXXXX", basepath[] = "/tmp/journal-base-XXXXXX";
    CHECK(temp(path) >= 0); CHECK(temp(basepath) >= 0);
    work_pool pool; CHECK(work_pool_init(&pool, 1, 1) == 0);
    journal *j = NULL; CHECK(journal_open(&j, path, &pool, NULL) == 0);
    journal_base empty = {.path=""}; CHECK(journal_set_base(j, 7, &empty) == 0);
    uint8_t big[4100]; memset(big, 'a', sizeof big);
    CHECK(journal_insert(j, 7, 0, big, sizeof big) == 0); /* straddles page */
    CHECK(journal_delete(j, 7, 1, 3) == 0);
    journal_view v = {17, 4, 200, 9}; CHECK(journal_set_view(j, 7, &v) == 0);
    uint64_t ids[] = {7, 19}; CHECK(journal_set_tabs(j, ids, 2, 1) == 0);
    CHECK(journal_set_window(j, 1200, 800) == 0);
    CHECK(journal_flush(j) == 0);
    edit_malloc_guard_begin();
    for (unsigned i=0;i<10000;i++) CHECK(journal_insert(j, 7, 0, (const uint8_t *)"x", 1) == JOURNAL_OK || journal_get_stats(j).error == JOURNAL_FULL);
    CHECK(edit_malloc_guard_end() == 0);
    printf("journal_test: malloc_guard=%s append_allocations=0\n",edit_malloc_guard_active()?"active":"ASan-inactive (release run required)");
    /* FULL is sticky and accepted prefix is retained. */
    CHECK(journal_insert(j, 7, 0, (const uint8_t *)"x", 1) == JOURNAL_FULL);
    CHECK(journal_flush(j) == 0);
    journal_stats st = journal_get_stats(j); CHECK(st.durable_sequence == st.accepted_sequence);
    journal_close(j);
    model m = {0}; journal_replay_result rr;
    CHECK(journal_replay_file(path, apply, &m, &rr) == 0);
    CHECK(m.len > 4097 && m.views == 1 && m.tabs == 1 && m.windows == 1 && !rr.corrupt);
    int fd = open(path, O_RDONLY); CHECK(fd >= 0);
    size_t size = (size_t)st.file_bytes;
    uint8_t *bytes = malloc(size), *copy = malloc(size); CHECK(bytes && copy);
    CHECK(read(fd, bytes, size) == (ssize_t)size); close(fd);
    /* Smaller fixture ends at first batch; mutate every byte, including CRC/padding. */
    size_t small = 8192;
    CHECK(small > 4100);
    size_t record_start=0;
    for (size_t i=0;i<small;i++) {
        size_t record_len=(size_t)((uint32_t)bytes[record_start+4] | (uint32_t)bytes[record_start+5]<<8 | (uint32_t)bytes[record_start+6]<<16 | (uint32_t)bytes[record_start+7]<<24);
        if(i>=record_start+record_len) record_start+=record_len;
        memcpy(copy, bytes, small); copy[i] ^= 1; model a = {0};
        CHECK(journal_replay_bytes(copy, small, apply, &a, &rr) == 0);
        CHECK(rr.corrupt && rr.valid_bytes <= record_start);
    }
    char tornpath[] = "/tmp/journal-torn-XXXXXX"; CHECK(temp(tornpath) >= 0);
    for (size_t n=small-4096;n<small;n++) {
        model a = {0}; CHECK(journal_replay_bytes(bytes, n, apply, &a, &rr) == 0); CHECK(rr.valid_bytes <= n);
        fd=open(tornpath,O_WRONLY|O_TRUNC); CHECK(fd>=0); CHECK(write(fd,bytes,n)==(ssize_t)n); close(fd);
        uint64_t expected_valid=rr.valid_bytes;
        a=(model){0}; CHECK(journal_replay_file(tornpath,apply,&a,&rr)==0);
        CHECK(rr.valid_bytes==expected_valid); struct stat tornstat; CHECK(stat(tornpath,&tornstat)==0 && (uint64_t)tornstat.st_size==expected_valid);
    }
    unlink(tornpath);
    /* Actual truncation, callback failure does not truncate. */
    fd = open(path, O_RDWR); CHECK(fd >= 0); CHECK(ftruncate(fd, (off_t)(size-1)) == 0); close(fd);
    m = (model){0}; CHECK(journal_replay_file(path, apply, &m, &rr) == 0 && rr.corrupt);
    struct stat sb; CHECK(stat(path, &sb) == 0 && (uint64_t)sb.st_size == rr.valid_bytes);
    uint64_t prior = rr.valid_bytes;
    CHECK(journal_replay_file(path, reject, NULL, &rr) == JOURNAL_CALLBACK);
    CHECK(stat(path, &sb) == 0 && (uint64_t)sb.st_size == prior);
    free(copy); free(bytes);
    /* Base conflict, including same size mutation with restored mtime. */
    fd = open(basepath, O_WRONLY); CHECK(fd >= 0 && write(fd, "abc", 3) == 3); close(fd);
    journal_base base; CHECK(journal_capture_base(basepath, &base) == 0); CHECK(journal_check_base(&base) == 0);
    fd = open(basepath, O_WRONLY); CHECK(fd >= 0 && write(fd, "xyz", 3) == 3); close(fd);
    struct timespec times[2] = {{0,UTIME_OMIT},{(time_t)(base.mtime_ns/1000000000u),(long)(base.mtime_ns%1000000000u)}};
    CHECK(utimensat(AT_FDCWD, basepath, times, 0) == 0); CHECK(journal_check_base(&base) == JOURNAL_BASE_CHANGED);
    /* Saturation while worker falls behind; no later edits after gap. */
    fd = open(path, O_TRUNC | O_RDWR); CHECK(fd >= 0); close(fd);
    journal_options opts = {8192, 16384};
    CHECK(journal_open(&j, path, &pool, &opts) == 0);
    work_handle blocker = work_submit(&pool, (work_job){blocked, NULL, 0, WORK_BULK}); CHECK(blocker.epoch != 0);
    CHECK(journal_insert(j, 7, 0, big, 4000) == 0);
    CHECK(journal_pump(j, 1, true) == 0);
    CHECK(journal_insert(j, 7, 0, big, 4000) == 0);
    CHECK(journal_insert(j, 7, 0, big, 4000) == 0);
    CHECK(journal_insert(j, 7, 0, big, 4000) == JOURNAL_FULL);
    work_cancel(&pool, blocker); CHECK(journal_flush(j) == 0);
    st = journal_get_stats(j); CHECK(st.durable_sequence == 3 && st.file_bytes == 12288);
    /* Rotation removes pre-save operations, preserving a complete session. */
    uint8_t op[11] = {0}; memcpy(op+8, "new", 3);
    journal_record cp = {JOURNAL_INSERT, 7, 0, op, sizeof op};
    CHECK(journal_rotate(j, &cp, 1) == 0);
    m = (model){0}; CHECK(journal_replay_file(path, apply, &m, &rr) == 0); CHECK(m.len == 3 && memcmp(m.text, "new", 3) == 0);
    CHECK(journal_get_stats(j).error == 0);
    CHECK(journal_insert(j, 7, 3, (const uint8_t *)"!", 1) == 0); CHECK(journal_flush(j) == 0);
    journal_close(j);
    m = (model){0}; CHECK(journal_replay_file(path, apply, &m, &rr) == 0); CHECK(m.len == 4 && memcmp(m.text, "new!", 4) == 0);
    CHECK(journal_open(&j, path, &pool, NULL) == JOURNAL_INVALID);
    fd=open(path,O_RDWR|O_TRUNC); CHECK(fd>=0); close(fd);
    opts=(journal_options){8192,4096}; CHECK(journal_open(&j,path,&pool,&opts)==0);
    CHECK(journal_insert(j,7,0,(const uint8_t *)"x",1)==0); CHECK(journal_flush(j)==0);
    CHECK(journal_insert(j,7,1,(const uint8_t *)"y",1)==JOURNAL_FULL);
    CHECK(journal_flush(j)==0); CHECK(journal_get_stats(j).file_bytes==4096);
    journal_record invalid={99,7,0,op,sizeof op};
    CHECK(journal_rotate(j,&invalid,1)==JOURNAL_INVALID);
    m=(model){0}; CHECK(journal_replay_file(path,apply,&m,&rr)==0 && m.len==1 && m.text[0]=='x');
    journal_close(j);
    /* Unread completion keeps its epoch alive while other jobs are queued.
     * Blocking operations preserve other modules' mailbox messages. */
    fd=open(path,O_RDWR|O_TRUNC); CHECK(fd>=0); close(fd);
    CHECK(journal_open(&j,path,&pool,NULL)==0);
    unsigned other_count=0; journal_set_message_handler(j,other_route,&other_count);
    work_handle other_handle=work_submit(&pool,(work_job){other_message,NULL,0,WORK_RASTER});
    CHECK(other_handle.epoch!=0);
    CHECK(journal_insert(j,7,0,(const uint8_t *)"x",1)==0);
    CHECK(journal_pump(j,journal_get_stats(j).last_sync_ns,false)==0);
    CHECK(journal_pump(j,1,true)==0);
    unsigned waiting=0;
    while((atomic_load(&pool.mb[0].head)==atomic_load(&pool.mb[0].tail) || atomic_load(&pool.mb[1].head)==atomic_load(&pool.mb[1].tail)) && waiting++<2000) pause_ms();
    CHECK(waiting<2000);
    work_handle handles[WORK_MAX_JOBS]; size_t submitted=0;
    for(size_t i=0;i<WORK_MAX_JOBS;i++) { handles[i]=work_submit(&pool,(work_job){other_message,NULL,0,WORK_BULK}); if(handles[i].epoch) submitted++; }
    CHECK(submitted==WORK_MAX_JOBS-2); /* journal job still reserves one slot */
    for(size_t i=0;i<WORK_MAX_JOBS;i++) work_cancel(&pool,handles[i]);
    CHECK(journal_flush(j)==0 && other_count==1);
    work_cancel(&pool,other_handle);
    journal_close(j);
    /* A changed base stops file replay, with no journal truncation. */
    CHECK(journal_capture_base(basepath,&base)==0);
    fd=open(path,O_RDWR|O_TRUNC); CHECK(fd>=0); close(fd);
    CHECK(journal_open(&j,path,&pool,NULL)==0); CHECK(journal_set_base(j,7,&base)==0);
    CHECK(journal_insert(j,7,3,(const uint8_t *)"!",1)==0); CHECK(journal_flush(j)==0); journal_close(j);
    piece_allocator allocator=piece_default_allocator(); piece_tree *tree=piece_create(&allocator); CHECK(tree!=NULL);
    CHECK(piece_init_copy(tree,(const uint8_t *)"xyz",3)==0);
    CHECK(journal_replay_file(path,apply_tree,tree,&rr)==0);
    uint8_t restored[4]; CHECK(piece_len(tree)==4 && piece_read(tree,0,restored,sizeof restored)==0 && memcmp(restored,"xyz!",4)==0); piece_destroy(tree);
    CHECK(stat(path,&sb)==0); off_t before_size=sb.st_size;
    fd=open(basepath,O_WRONLY); CHECK(fd>=0 && write(fd,"123",3)==3); close(fd);
    m=(model){0}; CHECK(journal_replay_file(path,apply,&m,&rr)==JOURNAL_BASE_CHANGED && m.records==0);
    CHECK(stat(path,&sb)==0 && sb.st_size==before_size);
    work_mailbox_drain(&pool, route, NULL);
    work_pool_shutdown(&pool); unlink(path); unlink(basepath);
    puts("journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)");
    return 0;
}
