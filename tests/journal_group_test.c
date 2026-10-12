/* edit-pgr: crash at action boundaries using the existing append-write seam.
 * (P) Fixture spans one wire slice plus strlen("tail") to require a final
 * INSERT record. IDs, bool/return sentinels and wire field offsets derive
 * from the existing journal/editor APIs; no timing budgets are introduced. */
#include "editor/private.h"
#include "trace/trace.h"
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#define T(c) do { if (!(c)) { fprintf(stderr, "journal_group_test:%d FAIL %s\n", __LINE__, #c); return 1; } } while (0)
/* (P) New wire types follow the last legacy enum value without renumbering it. */
enum { GROUP_BEGIN = JOURNAL_TARGET + 1, GROUP_COMMIT = GROUP_BEGIN + 1 };
typedef enum boundary { AFTER_DELETE, BEFORE_COMMIT, COMPLETE } boundary;
typedef enum action { UNDO, REDO, REPLACE, PASTE } action;
typedef struct hook { boundary at; size_t markers, inserts; } hook;
static uint32_t type_of(const uint8_t *p)
{
    /* (P) type32 follows magic32 and length32 in JOURNAL_HEADER. */
    uint32_t value = 0;
    for (size_t i = 0; i < sizeof value; i++) value |= (uint32_t)p[sizeof value * 2u + i] << (CHAR_BIT * i);
    return value;
}
static ssize_t crash_append(void *ctx, int fd, const uint8_t *p, size_t n, uint64_t off)
{
    hook *h = ctx;
    if (type_of(p)==GROUP_BEGIN || type_of(p)==GROUP_COMMIT) h->markers++;
    if (type_of(p)==JOURNAL_INSERT) h->inserts++;
    if (h->at == BEFORE_COMMIT && type_of(p) == GROUP_COMMIT) (void)kill(getpid(), SIGKILL);
    ssize_t written = pwrite(fd, p, n, (off_t)off);
    if (h->at == AFTER_DELETE && type_of(p) == JOURNAL_DELETE && written == (ssize_t)n)
        (void)kill(getpid(), SIGKILL);
    return written;
}
static int settle(editor *e)
{
    uint64_t end = trace_now_ns() + EDITOR_IDLE_NS; /* existing loop watchdog */
    do {
        int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE);
        T(trace_now_ns() < end);
    } while (editor_get_stats(e).pending || editor_get_stats(e).render_active);
    return 0;
}
static int key(editor *e, uint32_t symbol, uint16_t mods, const char *text)
{
    plat_event ev = {.kind=PLAT_EV_KEY, .press=true, .keysym=symbol, .mods=mods};
    if (text) { ev.utf8_len=(uint8_t)strlen(text); memcpy(ev.utf8,text,ev.utf8_len); }
    T(editor_inject(e,&ev)==0); return settle(e);
}
static int open_editor(editor **e, render_backend *b, const char *path)
{
    T(render_null_backend(b)==0);
    editor_config cfg={.start_empty=true,.journal_path=path,
        .cols=(uint32_t)strlen("file changed on disk; R reload / K keep") + 1u,
        .rows=4}; /* (P) tab + text + status + spare row */
    T(editor_open(e,&cfg,b)==0); return settle(*e);
}
static int child_run(const char *path, boundary at, action command)
{
    render_backend b={0}; editor *e; T(open_editor(&e,&b,path)==0);
    size_t len=JOURNAL_INSERT_SLICE_BYTES+strlen("tail");
    uint8_t *text=malloc(len); T(text); memset(text,'a',len);
    uint64_t id; T(editor_add_buffer(e,NULL,text,len,&id)==0); T(settle(e)==0); free(text);
    T(key(e,'a',PLAT_MOD_CTRL,NULL)==0);
    if (command==PASTE) {
        T(key(e,'c',PLAT_MOD_CTRL,NULL)==0);
        T(editor_add_buffer(e,NULL,(const uint8_t *)"x",strlen("x"),&id)==0);
        T(settle(e)==0); T(key(e,'a',PLAT_MOD_CTRL,NULL)==0);
    } else if (command!=REPLACE) T(key(e,'x',0,"x")==0);
    if (command==REDO) T(key(e,'z',PLAT_MOD_CTRL,NULL)==0);
    T(editor_flush(e)==0); /* pre-action image settled before arming crash seam */
    hook h={.at=at}; journal_io io={.ctx=&h,.append_write=crash_append};
    T(journal_set_io(e->journal,&io)==0);
    if (command==REPLACE) T(key(e,'x',0,"x")==0);
    else T(key(e,command==REDO?'y':command==PASTE?'v':'z',PLAT_MOD_CTRL,NULL)==0);
    if (at != COMPLETE) return 1; /* missing crash boundary is a failed exercise */
    (void)kill(getpid(),SIGKILL); return 1;
}
static int crash_case(boundary at, action command)
{
    char path[]="build/group-session-XXXXXX"; int fd=mkstemp(path); T(fd>=0); T(close(fd)==0);
    pid_t child=fork(); T(child>=0);
    if (!child) _exit(child_run(path,at,command));
    int status; T(waitpid(child,&status,0)==child); T(WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL);
    render_backend b={0}; editor *e; T(open_editor(&e,&b,path)==0);
    size_t large=JOURNAL_INSERT_SLICE_BYTES+strlen("tail");
    bool inserts_large=command==UNDO || command==PASTE;
    size_t want=(at==COMPLETE)==inserts_large ? large : strlen("x");
    uint64_t actual=editor_length(e);
    fprintf(stderr,"journal_group_test: boundary=%d action=%d recovered=%llu expected=%zu\n",at,command,(unsigned long long)actual,want);
    T(actual==want);
    uint8_t *bytes=malloc(want); T(bytes); T(editor_read(e,0,bytes,want)==0);
    for (size_t i=0;i<want;i++) T(bytes[i]==(want==large?'a':'x'));
    free(bytes); editor_close(e); T(unlink(path)==0);
    return 0;
}
/* Byte-image replay checks every possible torn byte, including CRC/header
 * cuts in COMMIT. Streaming replay checks those same commit cuts and the
 * DELETE/INSERT boundary; an explicit flush puts a PAD inside the group. */
static int apply(void *ctx, const journal_record *r)
{ return journal_apply_piece(ctx,r); }
static int read_image(const char *path, uint8_t **bytes, size_t *len)
{
    int fd=open(path,O_RDONLY); T(fd>=0); struct stat st; T(fstat(fd,&st)==0);
    T(st.st_size>=0 && (uint64_t)st.st_size<=SIZE_MAX); *len=(size_t)st.st_size;
    *bytes=malloc(*len); T(*bytes);
    size_t done=0;
    while (done<*len) { ssize_t n=read(fd,*bytes+done,*len-done); T(n>0); done+=(size_t)n; }
    T(close(fd)==0); return 0;
}
static int replay_is(const uint8_t *bytes, size_t size, const char *expected, const char *path)
{
    piece_allocator a=piece_default_allocator(); piece_tree *tree=piece_create(&a); T(tree);
    journal_replay_result rr;
    if (path) {
        int fd=open(path,O_WRONLY|O_TRUNC); T(fd>=0);
        T(write(fd,bytes,size)==(ssize_t)size); T(close(fd)==0);
        T(journal_replay_file(path,apply,tree,&rr)==0);
        struct stat st; T(stat(path,&st)==0); T((uint64_t)st.st_size==rr.valid_bytes);
    } else T(journal_replay_bytes(bytes,size,apply,tree,&rr)==0);
    T(rr.valid_bytes<=size); T(piece_len(tree)==strlen(expected));
    uint8_t actual[sizeof "after"]; T(piece_read(tree,0,actual,strlen(expected))==0);
    T(!memcmp(actual,expected,strlen(expected))); piece_destroy(tree); return 0;
}
static int parser_cases(void)
{
    char path[]="build/group-parser-XXXXXX", copy[]="build/group-copy-XXXXXX";
    int fd=mkstemp(path); T(fd>=0); T(close(fd)==0);
    fd=mkstemp(copy); T(fd>=0); T(close(fd)==0);
    work_pool pool; T(work_pool_init(&pool,1,0)==0); /* (P) one journal worker */
    journal *j; T(journal_open(&j,path,&pool,NULL)==0);
    hook h={.at=COMPLETE}; journal_io io={.ctx=&h,.append_write=crash_append};
    T(journal_set_io(j,&io)==0);
    uint64_t before=journal_get_stats(j).file_bytes;
    T(journal_insert(j,1,0,(const uint8_t *)"x",strlen("x"))==0);
    /* (P) unchanged keystroke wire cost: header + offset64 + UTF-8 bytes.
     * Exactly its INSERT syscall, no marker allocation or sync operation. */
    size_t legacy=JOURNAL_HEADER+sizeof(uint64_t)+strlen("x");
    T(journal_get_stats(j).file_bytes-before==legacy); T(h.inserts==1 && h.markers==0);
    T(journal_get_stats(j).durable_sequence==0);
    T(journal_append(j,GROUP_BEGIN,0,NULL,0)==0);
    T(journal_delete(j,1,0,strlen("x"))==0);
    size_t after_delete=(size_t)journal_get_stats(j).file_bytes;
    T(journal_flush(j)==0); /* PAD and a batch transition inside the group */
    T(journal_insert(j,1,0,(const uint8_t *)"after",strlen("after"))==0);
    size_t commit=(size_t)journal_get_stats(j).file_bytes;
    T(journal_append(j,GROUP_COMMIT,0,NULL,0)==0);
    T(h.markers==2); /* (P) BEGIN and COMMIT */
    size_t committed=(size_t)journal_get_stats(j).file_bytes;
    journal_close(j); work_pool_shutdown(&pool);
    uint8_t *bytes; size_t size; T(read_image(path,&bytes,&size)==0);
    for (size_t cut=0;cut<=size;cut++) {
        const char *want=cut<legacy ? "" : cut<committed ? "x" : "after";
        T(replay_is(bytes,cut,want,NULL)==0);
    }
    T(replay_is(bytes,legacy,"x",copy)==0); /* marker-free legacy journal */
    T(replay_is(bytes,after_delete,"x",copy)==0);
    for (size_t cut=commit;cut<committed;cut++) T(replay_is(bytes,cut,"x",copy)==0);
    T(replay_is(bytes,size,"after",copy)==0);
    /* (P) Flip each CRC-protected byte inside COMMIT: no torn group applies. */
    for (size_t i=commit;i<committed;i++) {
        bytes[i]^=1u; T(replay_is(bytes,size,"x",NULL)==0);
        T(replay_is(bytes,size,"x",copy)==0); bytes[i]^=1u;
    }
    free(bytes); T(unlink(path)==0); T(unlink(copy)==0);
    puts("journal_group_test: legacy, PAD, all byte cuts, corrupt commit, keystroke wire cost passed");
    return 0;
}
int main(void)
{
    trace_init(); T(trace_thread_register()>=0); T(parser_cases()==0);
    for (action command=UNDO;command<=PASTE;command++) {
        T(crash_case(AFTER_DELETE,command)==0);
        T(crash_case(BEFORE_COMMIT,command)==0);
        T(crash_case(COMPLETE,command)==0);
    }
    puts("journal_group_test: undo/redo/replacement/paste boundary crashes passed"); return 0;
}
