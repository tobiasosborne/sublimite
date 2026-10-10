#include "savectl.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <unistd.h>

/* Epoch and the prepare-return acknowledgement are shared atomic fields.
 * Frozen task input is borrowed by a worker; a separate result lease transfers ownership only via
 * its generation-validated terminal mailbox message. */
typedef enum operation { OP_SAVE, OP_FINISH, OP_RECOVER_FINISH, OP_CHECK, OP_KEEP, OP_RELOAD, OP_DISCARD } operation;
typedef struct reload_bytes {
    _Atomic size_t refs;
    uint8_t *bytes;
    size_t len;
    bool mapped;
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
        if (b->mapped) (void)munmap(b->bytes,b->len);
        else free(b->bytes);
        free(b);
    }
}
typedef struct result {
    int file_error, journal_error, err_no, stop_error;
    bool replaced;
    file_id id;
    journal_base base;
    reload_bytes *reload;
} result;
typedef struct completion {
    result value;
    journal_save token;
    uint32_t generation;
    bool prepared, handoff_pending;
    bool pending; /* worker-owned until terminal message transfers the lease */
} completion;
typedef struct notification { savectl *owner; completion *lease; bool handoff; } notification;
struct savectl {
    savectl_options options;
    char path[4097];
    file_id baseline;
    savectl_state state;
    bool modified, active, event, external, ready_reload, needs_finish, closing;
    uint64_t revision, saved_revision, content_id, clean_id, saved_content_id;
    bool content_known, saved_content_known;
    uint32_t generation;
    savectl_view view;
    piece_tree *reload_tree;
    size_t reload_mark;
    bool reload_reserved;
    int file_error, journal_error, err_no;
    _Atomic uint64_t epoch;
    _Atomic bool prepare_returned;
    bool journal_leased, saving_frame_pending;
    uint64_t request_id, request_ns, saving_submitted_ns;
    bool received;
    completion *completion;
    work_handle handle;
    struct {
        operation op;
        uint64_t epoch, revision;
        file_id expect;
        piece_snapshot *snapshot;
        savectl_request request;
        bool request_mode;
        journal_base previous;
        const journal_record *checkpoint;
        size_t count;
        bool guarded;
        journal_save token;
        journal_base base;
        reload_bytes *discard;
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
    if (rc) s->completion->value.stop_error=rc;
    return rc!=FILE_OK;
}
static void reload_worker(savectl *s)
{
    result *r=&s->completion->value;
    int fd=open(s->path,O_RDONLY|O_CLOEXEC|O_NONBLOCK);
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
    /* Freeze the replacement in a private file-backed snapshot. No anonymous
     * file-size reservation overlaps the old tree or any retained snapshot. */
    if (len) {
        char directory[4097]; strcpy(directory,s->path);
        char *slash=strrchr(directory,'/');
        if (slash==directory) slash[1]=0; else *slash=0;
        int backing=open(directory,O_TMPFILE|O_RDWR|O_CLOEXEC,0600);
        if (backing<0) { r->file_error=FILE_ERR_IO; goto end; }
        int cloned=-1;
        uint64_t threshold=s->options.reload_copy_threshold;
        if (!threshold) threshold=file_default_copy_threshold();
        if (len>=threshold) cloned=ioctl(backing,FICLONE,fd);
        if (cloned<0) {
            uint8_t *scratch=malloc(FILE_PREFIX_MAX);
            if (!scratch) { (void)close(backing); r->file_error=FILE_ERR_NOMEM; goto end; }
            size_t off=0;
            while (off<(size_t)len) {
                if (atomic_load_explicit(&s->epoch,memory_order_acquire)!=s->task.epoch) {
                    r->file_error=FILE_ERR_CHANGED; break;
                }
                size_t chunk=(size_t)len-off;
                if (chunk>FILE_PREFIX_MAX) chunk=FILE_PREFIX_MAX;
                ssize_t n=pread(fd,scratch,chunk,(off_t)off);
                if (n<0 && errno==EINTR) continue;
                if (n<=0) { r->file_error=n<0 ? FILE_ERR_IO : FILE_ERR_CHANGED; break; }
                size_t written=0;
                while (written<(size_t)n) {
                    ssize_t w=pwrite(backing,scratch+written,(size_t)n-written,(off_t)(off+written));
                    if (w<0 && errno==EINTR) continue;
                    if (w<=0) { r->file_error=FILE_ERR_IO; break; }
                    written+=(size_t)w;
                }
                if (r->file_error) break;
                off+=(size_t)n;
            }
            int saved_errno=errno; free(scratch); errno=saved_errno;
        }
        if (!r->file_error) {
            void *mapping=mmap(NULL,(size_t)len,PROT_READ,MAP_PRIVATE,backing,0);
            if (mapping==MAP_FAILED) r->file_error=FILE_ERR_NOMEM;
            else { copy->bytes=mapping; copy->mapped=true; }
        }
        int saved_errno=errno; (void)close(backing); errno=saved_errno;
        if (r->file_error) goto end;
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
    completion *lease=s->completion;
    if (lease->pending) goto publish;
    if (lease->handoff_pending) goto handoff;
    if (lease->prepared && !atomic_load_explicit(&s->prepare_returned,memory_order_acquire)) {
        (void)work_continue(ctx); return;
    }
    result *r=&lease->value;
    if (!lease->prepared) {
        lease->token=s->task.token; lease->generation=ctx->generation;
    }
    operation op=s->task.op;
    if (op==OP_DISCARD) { *r=(result){0}; bytes_release(s->task.discard); }
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
            if (!r->file_error && s->options.journal && !lease->prepared) {
                savectl_checkpoint checkpoint={s->task.previous,s->task.checkpoint,s->task.count};
                if (s->task.request_mode)
                    r->journal_error=s->task.request.prepare(s->task.request.ctx,s->task.snapshot,&checkpoint);
                if (!r->journal_error)
                    r->journal_error=journal_save_prepare(s->options.journal,s->options.buffer_id,
                        &checkpoint.previous,checkpoint.records,checkpoint.count,&lease->token);
            }
            if (!r->file_error && !r->journal_error && s->options.journal && !lease->prepared) {
                lease->prepared=true; lease->handoff_pending=true; goto handoff;
            }
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
            if (s->task.request_mode) s->task.request.release(s->task.request.ctx);
        } else if (op==OP_FINISH || op==OP_RECOVER_FINISH) {
            if (op==OP_RECOVER_FINISH)
                r->journal_error=journal_rotate(s->options.journal,s->task.checkpoint,s->task.count);
            if (!r->journal_error)
                r->journal_error=journal_save_finish(s->options.journal,&lease->token,&s->task.base,
                    s->task.checkpoint,s->task.count);
        }
    }
    lease->pending=true;
publish:;
    work_msg msg={.kind=SAVECTL_MESSAGE,.generation=ctx->generation};
    notification note={s,lease,false}; memcpy(msg.data,&note,sizeof note);
    /* Successful publication seals/transfers the lease; touch no output or
     * controller storage afterwards. Saturation retries as a FIFO continuation
     * so this terminal obligation never monopolizes the bulk lane. */
    if (!work_publish(ctx,&msg)) (void)work_continue(ctx);
    return;
handoff:;
    work_msg phase={.kind=SAVECTL_MESSAGE,.generation=ctx->generation};
    notification transfer={s,lease,true}; memcpy(phase.data,&transfer,sizeof transfer);
    /* Only the token is published here. No further journal call occurs until
     * a new explicit finish lease. The UI copies it before acknowledging. */
    lease->handoff_pending=false;
    if (!work_publish(ctx,&phase)) lease->handoff_pending=true;
    (void)work_continue(ctx);
}
static void controller_message(const work_msg *message, void *ctx)
{ (void)savectl_receive(ctx,message); }

static int submit(savectl *s, operation op)
{
    s->task.op=op;
    s->task.epoch=atomic_load_explicit(&s->epoch,memory_order_relaxed);
    s->task.revision=s->revision;
    s->task.expect=s->baseline;
    s->task.guarded=s->options.source_mode==FILE_MODE_MMAP || s->options.validate_source!=NULL;
    s->task.token=s->token; s->task.base=s->saved_base;
    s->task.base.path=s->task.base.captured_path;
    s->task.discard=op==OP_DISCARD ? s->result.reload : NULL;
    s->received=false; s->completion->pending=false;
    s->completion->prepared=false; s->completion->handoff_pending=false;
    atomic_store_explicit(&s->prepare_returned,false,memory_order_relaxed);
    if (s->handle.epoch)
        (void)work_mailbox_bind(s->options.pool,s->handle,s->generation,NULL,NULL);
    ++s->generation;
    s->handle=work_submit(s->options.pool,(work_job){worker,s,s->generation,WORK_BULK});
    if (!s->handle.epoch) return SAVECTL_POOL;
    s->active=true;
    s->journal_leased=s->options.journal && (op==OP_SAVE || op==OP_FINISH || op==OP_RECOVER_FINISH);
    if (op==OP_DISCARD) s->result.reload=NULL;
    (void)work_mailbox_bind(s->options.pool,s->handle,s->generation,controller_message,s);
    return SAVECTL_OK;
}
int savectl_create(savectl **out, const savectl_options *o, bool modified)
{
    if (!out || !o || !o->pool || !o->path || o->path[0]!='/' ||
        strlen(o->path)>4096 || (o->source_mode!=FILE_MODE_COPY && o->source_mode!=FILE_MODE_MMAP))
        return SAVECTL_INVALID;
    if (o->journal && (!o->journal_pool || o->journal_pool==o->pool)) return SAVECTL_INVALID;
    if ((o->reload_mark!=NULL)!=(o->reload_reset!=NULL)) return SAVECTL_INVALID;
    savectl *s=calloc(1,sizeof *s);
    if (!s) return SAVECTL_NOMEM;
    s->completion=calloc(1,sizeof *s->completion);
    if (!s->completion) { free(s); return SAVECTL_NOMEM; }
    s->options=*o; strcpy(s->path,o->path); s->options.path=s->path;
    s->baseline=o->baseline; s->modified=modified;
    if (o->source) {
        s->options.source=file_source_retain(o->source);
        s->options.source_mode=file_source_mode(o->source);
        s->options.validate_source=file_source_validate;
        s->options.source_ctx=o->source;
        s->baseline=*file_source_identity(o->source);
    }
    atomic_init(&s->epoch,0); atomic_init(&s->prepare_returned,false);
    *out=s; return SAVECTL_OK;
}
int savectl_destroy(savectl *s)
{
    if (!s) return SAVECTL_INVALID;
    if (s->active || s->ready_reload || s->result.reload || s->reload_tree ||
        (s->handle.epoch && !work_handle_finished(s->options.pool,s->handle)))
        return SAVECTL_BUSY;
    if (s->handle.epoch)
        (void)work_mailbox_bind(s->options.pool,s->handle,s->generation,NULL,NULL);
    free(s->completion);
    file_source_release(s->options.source);
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
    s->content_known=false;
    if (s->external) s->state=SAVECTL_EXTERNAL_MODIFIED;
    else if (!s->active && !s->needs_finish) s->state=SAVECTL_IDLE;
}
void savectl_content_identity(savectl *s, uint64_t current, uint64_t saved_id,
                              bool saved_valid)
{
    ++s->revision;
    s->content_id=current; s->clean_id=saved_id;
    s->content_known=true;
    s->modified=!saved_valid || current!=saved_id;
    if (s->external)
        s->state=s->modified ? SAVECTL_EXTERNAL_MODIFIED : SAVECTL_EXTERNAL_UNMODIFIED;
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
    if (s->saving_frame_pending) status="saving";
    return (savectl_model){.state=s->state,.status=status,
        .saving_frame_pending=s->saving_frame_pending,.request_id=s->request_id,
        .request_ns=s->request_ns,.saving_submitted_ns=s->saving_submitted_ns,
        .banner=s->external ? "File changed on disk. Reload or keep edits." : NULL,
        .modified=s->modified,.can_reload=s->external && !s->closing && !s->saving_frame_pending && !s->active && !s->ready_reload && !s->result.reload && !s->needs_finish && !s->token.prepared && !s->token.previous_path[0],
        .can_keep=s->external && !s->closing && !s->saving_frame_pending && !s->active && !s->ready_reload && !s->result.reload && !s->needs_finish && !s->token.prepared && !s->token.previous_path[0] &&
            (s->options.source_mode!=FILE_MODE_MMAP || s->options.validate_source!=NULL),
        .busy=s->active,.needs_finish=s->needs_finish,
        .journal_leased=s->journal_leased,
        .file_error=s->file_error,.journal_error=s->journal_error,.err_no=s->err_no};
}
int savectl_save(savectl *s, piece_tree *tree, const journal_base *previous,
                 const journal_record *checkpoint, size_t count)
{
    if (!s || !tree) return SAVECTL_INVALID;
    if (s->closing || s->active || s->saving_frame_pending || s->ready_reload || s->result.reload || s->needs_finish || s->token.prepared || s->token.previous_path[0]) return SAVECTL_BUSY;
    if (s->external) return FILE_ERR_CHANGED;
    if (s->options.source_mode==FILE_MODE_MMAP && !s->options.validate_source) return SAVECTL_SOURCE_GUARD;
    if (s->options.journal && (!previous || !checkpoint || !count)) return SAVECTL_INVALID;
    s->task.request_mode=false;
    s->task.snapshot=piece_snapshot_take(tree);
    if (!s->task.snapshot) return SAVECTL_NOMEM;
    if (previous) s->task.previous=*previous;
    s->task.checkpoint=checkpoint; s->task.count=count;
    int rc=submit(s,OP_SAVE);
    if (rc) { piece_snapshot_release(s->task.snapshot); return rc; }
    s->saved_revision=s->revision;
    s->saved_content_id=s->content_id; s->saved_content_known=s->content_known;
    s->state=SAVECTL_SAVING;
    s->file_error=0; s->journal_error=0; s->err_no=0;
    return SAVECTL_OK;
}
int savectl_save_request(savectl *s, const savectl_request *request)
{
    if (!s || !request || !request->snapshot || !request->release || !request->request_ns ||
        (s->options.journal && !request->prepare) || s->request_id==UINT64_MAX) return SAVECTL_INVALID;
    if (s->closing || s->active || s->saving_frame_pending || s->ready_reload || s->result.reload ||
        s->needs_finish || s->token.prepared || s->token.previous_path[0]) return SAVECTL_BUSY;
    if (s->external) return FILE_ERR_CHANGED;
    if (s->options.source_mode==FILE_MODE_MMAP && !s->options.validate_source) return SAVECTL_SOURCE_GUARD;
    s->task.snapshot=request->snapshot; s->task.request=*request; s->task.request_mode=true;
    int rc=submit(s,OP_SAVE);
    if (rc) return rc; /* caller still owns both transferred references */
    s->saved_revision=s->revision;
    s->saved_content_id=s->content_id; s->saved_content_known=s->content_known;
    s->state=SAVECTL_SAVING;
    s->file_error=0; s->journal_error=0; s->err_no=0;
    ++s->request_id; s->request_ns=request->request_ns; s->saving_submitted_ns=0;
    s->saving_frame_pending=true;
    return SAVECTL_OK;
}
int savectl_status_frame_submitted(savectl *s, uint64_t request_id, uint64_t submitted_ns)
{
    if (!s || !s->saving_frame_pending || request_id!=s->request_id || submitted_ns<s->request_ns)
        return SAVECTL_INVALID;
    s->saving_submitted_ns=submitted_ns; s->saving_frame_pending=false;
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
int savectl_recover_finish(savectl *s, const journal_record *checkpoint, size_t count)
{
    if (!s || !checkpoint || !count) return SAVECTL_INVALID;
    if (s->closing || s->active || !s->needs_finish) return SAVECTL_BUSY;
    s->task.checkpoint=checkpoint; s->task.count=count;
    int rc=submit(s,OP_RECOVER_FINISH);
    if (!rc) s->state=SAVECTL_SAVING;
    return rc;
}
const journal_base *savectl_saved_base(const savectl *s) { return s->needs_finish ? &s->saved_base : NULL; }
const journal_save *savectl_save_token(const savectl *s) { return s->journal_leased ? NULL : &s->token; }
void savectl_file_event(savectl *s)
{
    if (s->closing) return;
    s->event=true;
    (void)atomic_fetch_add_explicit(&s->epoch,1,memory_order_release);
}
int savectl_reload(savectl *s)
{
    if (!s || !s->options.reload_allocator.alloc || !s->options.reload_allocator.free) return SAVECTL_INVALID;
    if (s->closing || s->active || s->saving_frame_pending || s->ready_reload || s->result.reload || s->needs_finish || s->token.prepared || s->token.previous_path[0]) return SAVECTL_BUSY;
    int rc=submit(s,OP_RELOAD);
    if (!rc) { s->state=SAVECTL_RELOADING; s->event=false; }
    return rc;
}
int savectl_keep(savectl *s)
{
    if (!s) return SAVECTL_INVALID;
    if (s->closing || s->active || s->saving_frame_pending || s->ready_reload || s->result.reload || s->needs_finish || s->token.prepared || s->token.previous_path[0]) return SAVECTL_BUSY;
    if (!s->external) return SAVECTL_INVALID;
    if (s->options.source_mode==FILE_MODE_MMAP && !s->options.validate_source) return SAVECTL_SOURCE_GUARD;
    return submit(s,OP_KEEP);
}
bool savectl_receive(savectl *s, const work_msg *m)
{
    if (!s || !m || m->kind!=SAVECTL_MESSAGE) return false;
    notification note; memcpy(&note,m->data,sizeof note);
    if (note.owner!=s || note.lease!=s->completion) return false;
    if (m->generation==s->generation && s->active && !s->received &&
        note.lease->generation==s->generation) {
        if (note.handoff) {
            s->token=note.lease->token;
            s->journal_leased=false;
            atomic_store_explicit(&s->prepare_returned,true,memory_order_release);
        } else {
            s->result=note.lease->value; s->token=note.lease->token;
            s->received=true; savectl_tick(s);
        }
    }
    return true;
}
static void changed(savectl *s)
{
    s->external=true;
    s->state=s->modified ? SAVECTL_EXTERNAL_MODIFIED : SAVECTL_EXTERNAL_UNMODIFIED;
}
static void saved(savectl *s)
{
    if (s->saved_content_known) {
        s->clean_id=s->saved_content_id;
        s->modified=!s->content_known || s->content_id!=s->clean_id;
    } else if (s->revision==s->saved_revision) s->modified=false;
    s->state=SAVECTL_SAVED;
}
static void reload_rollback(savectl *s)
{
    if (s->reload_tree) { piece_destroy(s->reload_tree); s->reload_tree=NULL; }
    if (s->reload_reserved) {
        if (s->options.reload_reset)
            s->options.reload_reset(s->options.reload_allocator.ctx,s->reload_mark);
        s->reload_reserved=false;
    }
}
static int reload_install_failed(savectl *s)
{
    reload_rollback(s);
    s->ready_reload=false; s->external=true;
    s->state=SAVECTL_FAILED; s->file_error=FILE_ERR_NOMEM;
    /* Backing retirement is maintenance work, never an implicit install retry. */
    if (!s->active && s->result.reload) (void)submit(s,OP_DISCARD);
    return SAVECTL_NOMEM;
}
void savectl_tick(savectl *s)
{
    if (s->active)
        (void)work_mailbox_receive(s->options.pool,s->handle,s->generation,controller_message,s);
    if (s->ready_reload && (s->revision!=s->task.revision ||
        atomic_load_explicit(&s->epoch,memory_order_relaxed)!=s->task.epoch)) {
        s->ready_reload=false; changed(s);
    }
    if (s->active && s->received) {
        operation op=s->task.op;
        result *r=&s->result;
        s->active=false; s->journal_leased=false;
        if (op!=OP_DISCARD) {
            s->file_error=r->file_error; s->err_no=r->err_no;
            if (op==OP_SAVE || op==OP_FINISH || op==OP_RECOVER_FINISH) s->journal_error=r->journal_error;
            if (r->file_error || r->journal_error) {
                if (r->replaced) s->baseline=r->id;
                if (r->file_error==FILE_ERR_CHANGED || r->journal_error==JOURNAL_BASE_CHANGED) changed(s);
                else s->state=SAVECTL_FAILED;
            } else if (op==OP_SAVE) {
                s->baseline=r->id;
                if (s->options.journal) {
                    s->saved_base=r->base;
                    s->saved_base.path=s->saved_base.captured_path;
                    s->needs_finish=true;
                } else saved(s);
            } else if (op==OP_FINISH || op==OP_RECOVER_FINISH) {
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
    reload_rollback(s);
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
    if (!s->reload_tree) {
        if (!s->reload_reserved) {
            s->reload_mark=s->options.reload_mark ?
                s->options.reload_mark(s->options.reload_allocator.ctx) : 0;
            s->reload_reserved=true;
        }
        s->reload_tree=piece_create(&s->options.reload_allocator);
        if (!s->reload_tree) return reload_install_failed(s);
        piece_map_hooks hooks={copy,bytes_acquire,bytes_release};
        if (piece_init_mapped_begin(s->reload_tree,copy->bytes,copy->len,&hooks)) {
            return reload_install_failed(s);
        }
    }
    int rc=piece_init_mapped_step(s->reload_tree,64);
    if (rc==PIECE_MORE) return SAVECTL_BUSY;
    if (rc) return reload_install_failed(s);
    piece_tree *replacement=s->reload_tree; s->reload_tree=NULL;
    s->reload_reserved=false;
    uint64_t len=copy->len;
    *tree=replacement; s->result.reload=NULL;
    bytes_release(copy); /* controller owner; tree now retains the private copy */
    *view=s->view;
    view->cursor=clamp(view->cursor,len); view->anchor=clamp(view->anchor,len);
    view->scroll_byte=clamp(view->scroll_byte,len);
    s->baseline=s->result.id; s->ready_reload=false; s->external=false;
    s->modified=false; s->state=SAVECTL_IDLE;
    s->content_known=false;
    s->options.source_mode=FILE_MODE_COPY;
    s->options.validate_source=NULL;
    return SAVECTL_OK;
}
