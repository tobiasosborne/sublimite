#include "savectl.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* Only epoch/done are shared mutable fields. Task input is frozen from submit
 * through done; task output is release-published once, including mailbox loss. */
typedef enum operation { OP_SAVE, OP_FINISH, OP_CHECK, OP_KEEP, OP_RELOAD, OP_DISCARD } operation;
typedef struct reload_bytes {
    _Atomic size_t refs;
    uint8_t *bytes;
    size_t len;
} reload_bytes;
static void bytes_acquire(void *ctx)
{
    reload_bytes *b=ctx;
    (void)atomic_fetch_add_explicit(&b->refs,1,memory_order_relaxed);
}
static void bytes_release(void *ctx)
{
    reload_bytes *b=ctx;
    if (atomic_fetch_sub_explicit(&b->refs,1,memory_order_acq_rel)==1) {
        free(b->bytes); free(b);
    }
}
typedef struct result {
    int file_error, journal_error, err_no, stop_error;
    bool replaced;
    file_id id;
    journal_base base;
    reload_bytes *reload;
} result;
struct savectl {
    savectl_options options;
    char path[4097];
    file_id baseline;
    savectl_state state;
    bool modified, active, event, external, ready_reload, needs_finish, closing;
    uint64_t revision, saved_revision;
    uint32_t generation;
    savectl_view view;
    int file_error, journal_error, err_no;
    _Atomic uint64_t epoch;
    _Atomic bool done;
    work_handle handle;
    struct {
        operation op;
        uint64_t epoch, revision;
        file_id expect;
        piece_snapshot *snapshot;
        journal_base previous;
        const journal_record *checkpoint;
        size_t count;
        bool guarded;
    } task;
    result result;
    journal_save token;
    journal_base saved_base;
};

static file_id identity(const struct stat *st)
{
    file_id id;
    file_id_from_stat(&id,st);
    return id;
}
static bool same(file_id a, file_id b)
{
    return file_id_diff(&a,&b)==FILE_CHG_NONE;
}
static int disk_id(const char *path, file_id *out)
{
    *out=(file_id){0};
    int rc=file_id_stat_path(path,out);
    if (rc) return rc;
    if (out->exists && (out->type!=S_IFREG || out->size>INT64_MAX)) return FILE_ERR_NOTREG;
    return FILE_OK;
}
static int validate(void *ctx)
{
    savectl *s=ctx;
    if (atomic_load_explicit(&s->epoch,memory_order_acquire)!=s->task.epoch)
        return FILE_ERR_CHANGED;
    if (s->task.guarded) return s->options.validate_source(s->options.source_ctx);
    return FILE_OK;
}
static int stop(void *ctx)
{
    savectl *s=ctx;
    int rc=validate(s);
    if (rc) s->result.stop_error=rc;
    return rc!=FILE_OK;
}
static void reload_worker(savectl *s)
{
    result *r=&s->result;
    int fd=open(s->path,O_RDONLY|O_CLOEXEC);
    if (fd<0) { r->file_error=FILE_ERR_IO; r->err_no=errno; return; }
    struct stat before,after;
    reload_bytes *copy=NULL;
    if (fstat(fd,&before)) { r->file_error=FILE_ERR_IO; goto end; }
    if (!S_ISREG(before.st_mode) || before.st_size<0) { r->file_error=FILE_ERR_NOTREG; goto end; }
    r->file_error=disk_id(s->path,&r->id);
    if (r->file_error) goto end;
    if (!same(identity(&before),r->id)) { r->file_error=FILE_ERR_CHANGED; goto end; }
    uint64_t len=(uint64_t)before.st_size;
    if (len>SIZE_MAX) { r->file_error=FILE_ERR_NOMEM; goto end; }
    copy=calloc(1,sizeof *copy);
    if (!copy) { r->file_error=FILE_ERR_NOMEM; goto end; }
    atomic_init(&copy->refs,1); copy->len=(size_t)len;
    copy->bytes=malloc(len ? (size_t)len : 1u);
    if (!copy->bytes) { r->file_error=FILE_ERR_NOMEM; goto end; }

    size_t off=0;
    while (off<(size_t)len) {
        if (atomic_load_explicit(&s->epoch,memory_order_acquire)!=s->task.epoch) {
            r->file_error=FILE_ERR_CHANGED; goto end;
        }
        size_t chunk=(size_t)len-off;
        if (chunk>FILE_PREFIX_MAX) chunk=FILE_PREFIX_MAX;
        ssize_t n=pread(fd,copy->bytes+off,chunk,(off_t)off);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) { r->file_error=n<0 ? FILE_ERR_IO : FILE_ERR_CHANGED; goto end; }
        off+=(size_t)n;
    }
    if (fstat(fd,&after)) { r->file_error=FILE_ERR_IO; goto end; }
    r->file_error=disk_id(s->path,&r->id);
    if (r->file_error) goto end;
    if (!same(identity(&before),identity(&after)) || !same(identity(&after),r->id))
        r->file_error=FILE_ERR_CHANGED;

end:
    if (r->file_error==FILE_ERR_IO) r->err_no=errno;
    if (r->file_error) { if (copy) bytes_release(copy); }
    else r->reload=copy;
    (void)close(fd);
}
static void worker(work_ctx *ctx)
{
    savectl *s=ctx->arg;
    result *r=&s->result;
    operation op=s->task.op;
    if (op==OP_DISCARD) { bytes_release(r->reload); r->reload=NULL; }
    else {
        *r=(result){0};
        if (op==OP_RELOAD) reload_worker(s);
        else if (op==OP_CHECK || op==OP_KEEP) {
            r->file_error=disk_id(s->path,&r->id);
            if (!r->file_error && op==OP_KEEP) r->file_error=validate(s);
            if (!r->file_error && op==OP_CHECK && s->task.guarded)
                r->file_error=s->options.validate_source(s->options.source_ctx);
            if (r->file_error==FILE_ERR_IO) r->err_no=errno;
        } else if (op==OP_SAVE) {
            r->file_error=validate(s);
            if (!r->file_error && s->options.journal)
                r->journal_error=journal_save_prepare(s->options.journal,s->options.buffer_id,
                    &s->task.previous,s->task.checkpoint,s->task.count,&s->token);
            if (!r->file_error && !r->journal_error) {
                file_save_args args={.path=s->path,.snap=s->task.snapshot,
                    .mode=s->options.create_mode_valid ? s->options.create_mode : s->task.expect.mode,
                    .mode_valid=s->options.create_mode_valid,.expect=&s->task.expect,
                    .stop=stop,.stop_ctx=s,.validate=validate,.validate_ctx=s,
                    .out_id=&r->id,.step=s->options.step,.step_ctx=s->options.step_ctx};
                r->file_error=file_save_write(&args); r->err_no=args.err_no;
                if (r->file_error==FILE_ERR_CANCELLED && r->stop_error)
                    r->file_error=r->stop_error;
                r->replaced=r->file_error==FILE_OK || r->file_error==FILE_ERR_DIRSYNC;
                if (!r->file_error && s->options.journal) {
                    r->journal_error=journal_capture_base(s->path,&r->base);
                    /* Journal BASE has only these identity fields. The save
                     * result retains the complete file identity separately. */
                    if (!r->journal_error && (r->base.device!=r->id.dev ||
                        r->base.inode!=r->id.ino || r->base.size!=r->id.size ||
                        r->base.mtime_ns!=r->id.mtime_ns)) r->file_error=FILE_ERR_CHANGED;
                }
            }
            piece_snapshot_release(s->task.snapshot);
        } else if (op==OP_FINISH)
            r->journal_error=journal_save_finish(s->options.journal,&s->token,&s->saved_base,
                s->task.checkpoint,s->task.count);
    }
    work_msg msg={.kind=SAVECTL_MESSAGE,.generation=ctx->generation};
    memcpy(msg.data,&s,sizeof s);
    /* Result must precede the wake. After this release, use only ctx/local msg:
     * UI ticks can retire the controller while this publication completes. */
    atomic_store_explicit(&s->done,true,memory_order_release);
    (void)work_publish(ctx,&msg);
}
static int submit(savectl *s, operation op)
{
    s->task.op=op;
    s->task.epoch=atomic_load_explicit(&s->epoch,memory_order_relaxed);
    s->task.revision=s->revision;
    s->task.expect=s->baseline;
    s->task.guarded=s->options.source_mode==FILE_MODE_MMAP || s->options.validate_source!=NULL;
    atomic_store_explicit(&s->done,false,memory_order_relaxed);
    ++s->generation;
    s->handle=work_submit(s->options.pool,(work_job){worker,s,s->generation,WORK_BULK});
    if (!s->handle.epoch) return SAVECTL_POOL;
    s->active=true; return SAVECTL_OK;
}
int savectl_create(savectl **out, const savectl_options *o, bool modified)
{
    if (!out || !o || !o->pool || !o->path || o->path[0]!='/' ||
        strlen(o->path)>4096 || (o->source_mode!=FILE_MODE_COPY && o->source_mode!=FILE_MODE_MMAP))
        return SAVECTL_INVALID;
    if (o->journal && (!o->journal_pool || o->journal_pool==o->pool)) return SAVECTL_INVALID;
    savectl *s=calloc(1,sizeof *s);
    if (!s) return SAVECTL_NOMEM;
    s->options=*o; strcpy(s->path,o->path); s->options.path=s->path;
    s->baseline=o->baseline; s->modified=modified;
    atomic_init(&s->epoch,0); atomic_init(&s->done,false);
    *out=s; return SAVECTL_OK;
}
int savectl_destroy(savectl *s)
{
    if (!s) return SAVECTL_INVALID;
    if (s->active || s->ready_reload || s->result.reload ||
        (s->handle.epoch && atomic_load_explicit(&s->options.pool->slots[s->handle.slot].busy,memory_order_acquire)))
        return SAVECTL_BUSY;
    free(s); return SAVECTL_OK;
}
void savectl_close_begin(savectl *s)
{
    s->closing=true; s->event=false; s->ready_reload=false;
    (void)atomic_fetch_add_explicit(&s->epoch,1,memory_order_release);
}
void savectl_modified(savectl *s)
{
    ++s->revision; s->modified=true;
    if (s->external) s->state=SAVECTL_EXTERNAL_MODIFIED;
    else if (!s->active && !s->needs_finish) s->state=SAVECTL_IDLE;
}
void savectl_set_view(savectl *s, savectl_view view) { s->view=view; }
savectl_model savectl_get_model(const savectl *s)
{
    const char *status="";
    switch (s->state) {
    case SAVECTL_SAVING: status="saving"; break;
    case SAVECTL_SAVED: status="saved"; break;
    case SAVECTL_FAILED: status="save or reload failed"; break;
    case SAVECTL_EXTERNAL_MODIFIED: case SAVECTL_EXTERNAL_UNMODIFIED: status="source changed on disk"; break;
    case SAVECTL_RELOADING: status="reloading"; break;
    case SAVECTL_IDLE: break;
    }
    return (savectl_model){.state=s->state,.status=status,
        .banner=s->external ? "File changed on disk. Reload or keep edits." : NULL,
        .modified=s->modified,.can_reload=s->external && !s->closing && !s->active && !s->ready_reload && !s->result.reload && !s->needs_finish && !s->token.prepared && !s->token.previous_path[0],
        .can_keep=s->external && !s->closing && !s->active && !s->ready_reload && !s->result.reload && !s->needs_finish && !s->token.prepared && !s->token.previous_path[0] &&
            (s->options.source_mode!=FILE_MODE_MMAP || s->options.validate_source!=NULL),
        .busy=s->active,.needs_finish=s->needs_finish,
        .journal_leased=s->active && (s->task.op==OP_SAVE || s->task.op==OP_FINISH) && s->options.journal!=NULL,
        .file_error=s->file_error,.journal_error=s->journal_error,.err_no=s->err_no};
}
int savectl_save(savectl *s, piece_tree *tree, const journal_base *previous,
                 const journal_record *checkpoint, size_t count)
{
    if (!s || !tree) return SAVECTL_INVALID;
    if (s->closing || s->active || s->ready_reload || s->result.reload || s->needs_finish || s->token.prepared || s->token.previous_path[0]) return SAVECTL_BUSY;
    if (s->external) return FILE_ERR_CHANGED;
    if (s->options.source_mode==FILE_MODE_MMAP && !s->options.validate_source) return SAVECTL_SOURCE_GUARD;
    if (s->options.journal && (!previous || !checkpoint || !count)) return SAVECTL_INVALID;
    s->task.snapshot=piece_snapshot_take(tree);
    if (!s->task.snapshot) return SAVECTL_NOMEM;
    if (previous) s->task.previous=*previous;
    s->task.checkpoint=checkpoint; s->task.count=count;
    int rc=submit(s,OP_SAVE);
    if (rc) { piece_snapshot_release(s->task.snapshot); return rc; }
    s->saved_revision=s->revision; s->state=SAVECTL_SAVING;
    s->file_error=0; s->journal_error=0; s->err_no=0;
    return SAVECTL_OK;
}
int savectl_finish(savectl *s, const journal_record *checkpoint, size_t count)
{
    if (!s || !checkpoint || !count) return SAVECTL_INVALID;
    if (s->closing || s->active || !s->needs_finish) return SAVECTL_BUSY;
    s->task.checkpoint=checkpoint; s->task.count=count;
    int rc=submit(s,OP_FINISH);
    if (!rc) s->state=SAVECTL_SAVING;
    return rc;
}
const journal_base *savectl_saved_base(const savectl *s) { return s->needs_finish ? &s->saved_base : NULL; }
const journal_save *savectl_save_token(const savectl *s) { return s->active ? NULL : &s->token; }
void savectl_file_event(savectl *s)
{
    if (s->closing) return;
    s->event=true;
    (void)atomic_fetch_add_explicit(&s->epoch,1,memory_order_release);
}
int savectl_reload(savectl *s)
{
    if (!s || !s->options.reload_allocator.alloc || !s->options.reload_allocator.free) return SAVECTL_INVALID;
    if (s->closing || s->active || s->ready_reload || s->result.reload || s->needs_finish || s->token.prepared || s->token.previous_path[0]) return SAVECTL_BUSY;
    int rc=submit(s,OP_RELOAD);
    if (!rc) { s->state=SAVECTL_RELOADING; s->event=false; }
    return rc;
}
int savectl_keep(savectl *s)
{
    if (!s) return SAVECTL_INVALID;
    if (s->closing || s->active || s->ready_reload || s->result.reload || s->needs_finish || s->token.prepared || s->token.previous_path[0]) return SAVECTL_BUSY;
    if (!s->external) return SAVECTL_INVALID;
    if (s->options.source_mode==FILE_MODE_MMAP && !s->options.validate_source) return SAVECTL_SOURCE_GUARD;
    return submit(s,OP_KEEP);
}
bool savectl_receive(savectl *s, const work_msg *m)
{
    if (!s || !m || m->kind!=SAVECTL_MESSAGE) return false;
    savectl *owner=NULL; memcpy(&owner,m->data,sizeof owner);
    if (owner!=s) return false;
    if (m->generation==s->generation) savectl_tick(s);
    return true;
}
static void changed(savectl *s)
{
    s->external=true;
    s->state=s->modified ? SAVECTL_EXTERNAL_MODIFIED : SAVECTL_EXTERNAL_UNMODIFIED;
}
static void saved(savectl *s)
{
    if (s->revision==s->saved_revision) s->modified=false;
    s->state=SAVECTL_SAVED;
}
void savectl_tick(savectl *s)
{
    if (s->ready_reload && (s->revision!=s->task.revision ||
        atomic_load_explicit(&s->epoch,memory_order_relaxed)!=s->task.epoch)) {
        s->ready_reload=false; changed(s);
    }
    if (s->active && atomic_load_explicit(&s->done,memory_order_acquire)) {
        operation op=s->task.op;
        result *r=&s->result;
        s->active=false;
        if (op!=OP_DISCARD) {
            s->file_error=r->file_error; s->journal_error=r->journal_error; s->err_no=r->err_no;
            if (r->file_error || r->journal_error) {
                if (r->replaced) s->baseline=r->id;
                if (r->file_error==FILE_ERR_CHANGED) changed(s);
                else s->state=SAVECTL_FAILED;
            } else if (op==OP_SAVE) {
                s->baseline=r->id;
                if (s->options.journal) {
                    s->saved_base=r->base;
                    s->saved_base.path=s->saved_base.captured_path;
                    s->needs_finish=true;
                } else saved(s);
            } else if (op==OP_FINISH) {
                s->needs_finish=false; s->token.previous_path[0]=0; saved(s);
            } else if (op==OP_KEEP) {
                if (atomic_load_explicit(&s->epoch,memory_order_relaxed)==s->task.epoch) {
                    s->baseline=r->id; s->external=false; s->state=SAVECTL_IDLE;
                    /* Kept text now differs from the adopted disk generation. */
                    s->modified=true;
                }
            } else if (op==OP_CHECK) {
                if (!same(r->id,s->baseline)) changed(s);
            } else if (op==OP_RELOAD) {
                if (s->revision!=s->task.revision ||
                    atomic_load_explicit(&s->epoch,memory_order_relaxed)!=s->task.epoch) {
                    changed(s);
                } else s->ready_reload=true;
            }
        }
    }
    if (s->closing) s->ready_reload=false;
    if (s->active || s->ready_reload || (!s->closing && s->needs_finish)) return;
    if (s->result.reload) { (void)submit(s,OP_DISCARD); return; }
    if (s->closing) return;
    if (s->event) {
        if (s->options.source_mode==FILE_MODE_MMAP && !s->options.validate_source) {
            s->event=false; changed(s); return;
        }
        if (submit(s,OP_CHECK)==SAVECTL_OK) s->event=false;
    } else if (s->external && !s->modified && !s->token.prepared && !s->token.previous_path[0] &&
               s->file_error!=FILE_ERR_IO && s->file_error!=FILE_ERR_NOTREG && s->file_error!=FILE_ERR_NOMEM)
        (void)savectl_reload(s);
}
static uint64_t clamp(uint64_t off, uint64_t len) { return off<len ? off : len; }
int savectl_take_reload(savectl *s, piece_tree **tree, savectl_view *view)
{
    if (!s || !tree || !view) return SAVECTL_INVALID;
    if (!s->ready_reload || s->active) return SAVECTL_BUSY;
    /* A mutation/event can land between tick and this explicit UI install. */
    if (s->revision!=s->task.revision ||
        atomic_load_explicit(&s->epoch,memory_order_relaxed)!=s->task.epoch) {
        s->ready_reload=false; changed(s); return SAVECTL_BUSY;
    }
    reload_bytes *copy=s->result.reload;
    piece_tree *replacement=piece_create(&s->options.reload_allocator);
    if (!replacement) return SAVECTL_NOMEM;
    piece_map_hooks hooks={copy,bytes_acquire,bytes_release};
    if (piece_init_mapped(replacement,copy->bytes,copy->len,&hooks)) {
        piece_destroy(replacement); return SAVECTL_NOMEM;
    }
    uint64_t len=copy->len;
    *tree=replacement; s->result.reload=NULL;
    bytes_release(copy); /* controller owner; tree now retains the private copy */
    *view=s->view;
    view->cursor=clamp(view->cursor,len); view->anchor=clamp(view->anchor,len);
    view->scroll_byte=clamp(view->scroll_byte,len);
    s->baseline=s->result.id; s->ready_reload=false; s->external=false;
    s->modified=false; s->state=SAVECTL_IDLE;
    s->options.source_mode=FILE_MODE_COPY;
    s->options.validate_source=NULL;
    return SAVECTL_OK;
}
