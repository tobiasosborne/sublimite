/* Startup only. (P) structural numbers denote empty/one, wire/schema sizes
 * come from journal.h; permissions are owner read/write (POSIX 0600). */
#include "journal/private.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct artifact {
    char path[sizeof ((journal_base *)0)->captured_path];
    char name[OS_NAME_MAX];
    os_stat identity;
    os_file lease;
    bool base, session, generated, aside, retired, clean, referenced;
} artifact;
typedef struct saved_identity {
    struct saved_identity *next;
    uint64_t id, size, mtime_ns, inode, device, ctime_ns;
    uint32_t prefix_crc, prefix_len;
} saved_identity;
typedef struct inventory {
    artifact *items, *scanning;
    size_t count;
    bool uncertain;
    edit_pool identities;
    saved_identity *bases;
} inventory;
static bool same(const os_stat *a, const os_stat *b)
{ return journal_same_stat(a,b); }
/* Iterators own their cursors; the independently pinned directory owns the
 * namespace and its lock through every rewind and nested liveness scan. */
typedef struct journal_entry { char d_name[OS_NAME_MAX]; } journal_entry;
typedef struct journal_listing {
    os_dir dir;
    os_dir_iter it;
    journal_entry entry;
    os_err error;
    const journal_retire_io *io;
    journal_inventory_pass pass;
} journal_listing;
static bool listing_open(journal_listing *out, const char *path, const os_dir *parent)
{
    out->io=NULL;
    out->pass=JOURNAL_LIST_COUNT;
    out->dir=(os_dir)OS_DIR_INIT;
    os_err e=parent?os_dir_open_at(&out->dir,parent,"."):os_dir_open(&out->dir,path);
    if(e!=OS_OK) return false;
    e=os_dir_iter_open(&out->it,&out->dir);
    if(e!=OS_OK) { os_dir_close(&out->dir); return false; }
    out->error=OS_OK; return true;
}
static journal_entry *listing_next(journal_listing *scan)
{
    const char *name=NULL;
    scan->error=scan->io && scan->io->next?scan->io->next(scan->io->ctx,&scan->it,&name,scan->pass):os_dir_next(&scan->it,&name);
    if(scan->error!=OS_OK || !name) return NULL;
    strcpy(scan->entry.d_name,name); return &scan->entry;
}
static void listing_rewind(journal_listing *scan, journal_inventory_pass pass) { scan->pass=pass; os_dir_rewind(&scan->it); scan->error=OS_OK; }
static void listing_close(journal_listing *scan) { os_dir_iter_close(&scan->it); os_dir_close(&scan->dir); }
typedef struct rotation_name {
    uintmax_t device, inode, ctime_sec, ctime_nsec;
    char owner[sizeof ((journal_entry *)0)->d_name];
    bool source, aside, exceeds_source;
} rotation_name;
/* (P) Hexadecimal names use radix sixteen; overflow is bounded by uintmax_t,
 * not by a guessed pathname component length. Accept lowercase components. */
static bool hex_component(const char **text, uintmax_t *out, bool separator)
{
    const char *p=*text; uintmax_t value=0; bool any=false;
    for(;;p++) {
        unsigned digit;
        if(*p>='0' && *p<='9') digit=(unsigned)(*p-'0');
        else if(*p>='a' && *p<='f') digit=(unsigned)(*p-'a')+10u;
        else break;
        if(value>(UINTMAX_MAX-digit)/16u) return false;
        value=value*16u+digit; any=true;
    }
    if(!any || (separator && *p++!='-')) return false;
    *text=p; *out=value; return true;
}
static bool rotation_parse(const char *name, rotation_name *out)
{
    static const char prefix[]=".sublimite-journal-", source[]="from-", aside[]=".set-aside-";
    if(strncmp(name,prefix,sizeof prefix-1u)) return false;
    const char *p=name+sizeof prefix-1u; *out=(rotation_name){.exceeds_source=true}; uintmax_t ignored;
    /* edit-dr9: a failed original cannot prove the temp's redundancy even
     * when its generation still matches. Only an explicit clean flag proves
     * otherwise; old unflagged names are uncertain. Both flags are non-hex. */
    if(*p=='!' || *p=='+') { out->exceeds_source=*p!='+'; p++; }
    if(!strncmp(p,source,sizeof source-1u)) {
        p+=sizeof source-1u; out->source=true;
        if(!hex_component(&p,&out->device,true) || !hex_component(&p,&out->inode,true) ||
           !hex_component(&p,&out->ctime_sec,true) || !hex_component(&p,&out->ctime_nsec,true)) return false;
    }
    /* (P) Historical names have PID/nonce/attempt; new names prefix the
     * original device/inode/ctime, preserving independent long-basename rotation. */
    if(!hex_component(&p,&ignored,true) || !hex_component(&p,&ignored,true) ||
       !hex_component(&p,&ignored,out->source)) return false;
    if(out->source) {
        static const char hex[]="0123456789abcdef"; size_t size=0;
        while(*p && strchr(hex,*p)) {
            const char *high=strchr(hex,p[0]), *low=p[1]?strchr(hex,p[1]):NULL;
            if(!low || size+1u>=sizeof out->owner) return false;
            char byte=(char)((high-hex)*16+(low-hex));
            if(!byte || byte=='/') return false;
            out->owner[size++]=byte; p+=2;
        }
        if(!size || !strcmp(out->owner,".") || !strcmp(out->owner,"..") ||
           !strncmp(out->owner,".sublimite-journal-",strlen(".sublimite-journal-")) ||
           !strncmp(out->owner,"..sublimite-journal-",strlen("..sublimite-journal-"))) return false;
    }
    if(!*p) return true;
    if(strncmp(p,aside,sizeof aside-1u)) return false;
    p+=sizeof aside-1u; if(!*p) return false;
    while(*p>='0' && *p<='9') p++;
    out->aside=true; return !*p;
}
/* edit-fi0p F12: only the operator can release a possible sole checkpoint.
 * Pin its directory and descriptor, scan through the pinned parent, and
 * verify that the name still selects the same complete source before unlink.
 * (P) path capacities follow journal_base; zero/one are structural values. */
int journal_acknowledge(const char *source)
{
    if(!source || !*source || strlen(source)>=sizeof ((journal_base *)0)->captured_path) return JOURNAL_INVALID;
    char parent[sizeof ((journal_base *)0)->captured_path], leaf[sizeof parent];
    strcpy(parent,source); journal_slashes(parent); char *slash=strrchr(parent,'/');
    strcpy(leaf,slash?slash+1:parent);
    rotation_name name;
    if(!rotation_parse(leaf,&name) || !name.aside) return JOURNAL_INVALID;
    journal_parent(parent,slash);
    os_dir dfd=OS_DIR_INIT;
    if(os_dir_open(&dfd,parent)!=OS_OK) return JOURNAL_IO;
    os_err locked=os_dir_try_lock(&dfd,OS_LOCK_EXCLUSIVE);
    int rc=locked==OS_OK?0:locked==OS_E_BUSY?JOURNAL_BUSY:JOURNAL_IO;
    os_file fd=OS_FILE_INIT;
    if(!rc) {
        os_err opened=os_file_open_at(&fd,&dfd,leaf,OS_OPEN_READ|OS_OPEN_NOFOLLOW|OS_OPEN_NONBLOCK);
        if(opened!=OS_OK) rc=opened==OS_E_ISDIR?JOURNAL_BUSY:JOURNAL_IO;
    }
    if(!rc) {
        locked=os_file_try_lock(&fd,OS_LOCK_EXCLUSIVE);
        if(locked!=OS_OK) rc=locked==OS_E_BUSY?JOURNAL_BUSY:JOURNAL_IO;
    }
    os_stat held,named,after;
    if(!rc && (os_file_stat(&fd,&held) || held.kind!=OS_KIND_FILE ||
        os_stat_at(&dfd,leaf,OS_STAT_NOFOLLOW,&named) || !same(&held,&named))) rc=JOURNAL_BUSY;
    journal_replay_result result;
    if(!rc) rc=journal_scan_open(fd,NULL,NULL,&result);
    if(!rc && (result.corrupt || result.valid_bytes!=(uint64_t)held.size)) rc=JOURNAL_INVALID;
    if(!rc && (os_file_stat(&fd,&after) || !same(&held,&after) ||
        os_stat_at(&dfd,leaf,OS_STAT_NOFOLLOW,&named) || !same(&held,&named))) rc=JOURNAL_BUSY;
    if(!rc && (os_unlink_at(&dfd,leaf) || os_dir_sync(&dfd))) rc=JOURNAL_IO;
    os_file_close(&fd);
    os_dir_close(&dfd); return rc;
}
/* Legacy names cannot identify their owner. A busy existing session lease
 * therefore protects all of them, except this launch's fresh destination.
 * Do not create any lease just to perform this liveness check. */
static bool legacy_owner_live(journal_listing *dir, const char *current)
{
    journal_listing storage, *scan=&storage; if(!listing_open(scan,NULL,&dir->dir)) return true;
    char own[sizeof ((journal_entry *)0)->d_name]="";
    const char *leaf=strrchr(current,'/');
    if(*current) {
        int n=snprintf(own,sizeof own,".%s.lock",leaf?leaf+1:current);
        if(n<0 || (size_t)n>=sizeof own) *own=0;
    }
    bool live=false; journal_entry *entry;
    for(;;) {
        entry=listing_next(scan); if(!entry) { live=scan->error!=OS_OK; break; }
        size_t len=strlen(entry->d_name);
        if(len<sizeof ".lock" || strcmp(entry->d_name+len-(sizeof ".lock"-1u),".lock") ||
           !strncmp(entry->d_name,"..sublimite-journal-",strlen("..sublimite-journal-")) ||
           !strcmp(entry->d_name,own)) continue;
        os_file fd=OS_FILE_INIT; os_err opened=os_file_open_at(&fd,&dir->dir,entry->d_name,OS_OPEN_READ|OS_OPEN_NOFOLLOW|OS_OPEN_NONBLOCK);
        if(opened!=OS_OK) { live=true; break; }
        live=os_file_try_lock(&fd,OS_LOCK_EXCLUSIVE)!=0; os_file_close(&fd); if(live) break;
    }
    listing_close(scan); return live;
}
static bool rotation_source_exists(journal_listing *dir, const rotation_name *name)
{
    os_stat st;
    return !os_stat_at(&dir->dir,name->owner,OS_STAT_NOFOLLOW,&st) && st.kind==OS_KIND_FILE && st.id.index[1]==0 &&
        (uintmax_t)st.id.volume==name->device && (uintmax_t)st.id.index[0]==name->inode &&
        (uintmax_t)(st.ctime_ns/1000000000)==name->ctime_sec && (uintmax_t)(st.ctime_ns%1000000000)==name->ctime_nsec;
}
/* Existing stray lock files are litter only. Never O_CREAT for temp leases;
 * verify that the locked inode still has this name before removing it. */
static bool remove_rotation_lock(journal_listing *dir, const char *name)
{
    os_file fd=OS_FILE_INIT; os_err opened=os_file_open_at(&fd,&dir->dir,name,OS_OPEN_RDWR|OS_OPEN_NOFOLLOW|OS_OPEN_NONBLOCK);
    if(opened!=OS_OK) return false;
    os_stat held,named; bool removed=false;
    if(!os_file_try_lock(&fd,OS_LOCK_EXCLUSIVE) && !os_file_stat(&fd,&held) &&
       !os_stat_at(&dir->dir,name,OS_STAT_NOFOLLOW,&named) && named.kind==OS_KIND_FILE &&
       journal_same_id(&held,&named)) removed=os_unlink_at(&dir->dir,name)==0;
    os_file_close(&fd); return removed;
}
/* edit-7cj8: these names never appear in a durable journal. The caller's
 * exclusive directory lease excludes every live asynchronous copy. No
 * content scan, reference inventory, age rule or byte cap is needed. */
static int sweep_copies(journal_listing *dir)
{
    const char prefix[]=".sublimite-copy-";
    bool changed=false; int rc=0;
    listing_rewind(dir,JOURNAL_LIST_COPY); journal_entry *entry;
    while((entry=listing_next(dir))) {
        /* (P) mkostemp's suffix schema is the literal XXXXXX. */
        if(strncmp(entry->d_name,prefix,sizeof prefix-1u) ||
           strlen(entry->d_name)!=(sizeof prefix-1u)+(sizeof "XXXXXX"-1u)) continue;
        os_file fd=OS_FILE_INIT; os_err opened=os_file_open_at(&fd,&dir->dir,entry->d_name,OS_OPEN_READ|OS_OPEN_NOFOLLOW|OS_OPEN_NONBLOCK);
        if(opened!=OS_OK) continue;
        os_stat held,named;
        if(!os_file_stat(&fd,&held) && held.kind==OS_KIND_FILE &&
           !os_stat_at(&dir->dir,entry->d_name,OS_STAT_NOFOLLOW,&named) && same(&held,&named)) {
            if(os_unlink_at(&dir->dir,entry->d_name)) rc=JOURNAL_IO; else changed=true;
        }
        os_file_close(&fd);
    }
    if(dir->error!=OS_OK) rc=JOURNAL_IO;
    if(changed && os_dir_sync(&dir->dir)) rc=JOURNAL_IO;
    return rc;
}
static int sweep_rotations(journal_listing *dir, const char *current)
{
    bool legacy_live=legacy_owner_live(dir,current), changed=false; int rc=0;
    listing_rewind(dir,JOURNAL_LIST_ROTATION); journal_entry *entry;
    while((entry=listing_next(dir))) {
        rotation_name name;
        if(!rotation_parse(entry->d_name,&name) || name.aside || (!name.source && legacy_live)) continue;
        os_file fd=OS_FILE_INIT; os_err opened=os_file_open_at(&fd,&dir->dir,entry->d_name,OS_OPEN_READ|OS_OPEN_NOFOLLOW|OS_OPEN_NONBLOCK);
        if(opened!=OS_OK) continue;
        os_stat held,named;
        bool valid=!os_file_try_lock(&fd,OS_LOCK_EXCLUSIVE) && !os_file_stat(&fd,&held) && held.kind==OS_KIND_FILE &&
            !os_stat_at(&dir->dir,entry->d_name,OS_STAT_NOFOLLOW,&named) && same(&held,&named);
        if(valid) {
            if(name.source && !name.exceeds_source && rotation_source_exists(dir,&name)) {
                if(os_unlink_at(&dir->dir,entry->d_name)) rc=JOURNAL_IO; else changed=true;
            } else {
                /* Missing/unknown/failed original: possible sole copy.
                 * Same set-aside convention as recovery; no copy,
                 * content read, age test, size limit, or replacement. */
                uint64_t now=dir->io && dir->io->wall_ns?dir->io->wall_ns(dir->io->ctx):os_wall_ns(); char aside[sizeof entry->d_name];
                /* (P) Nanoseconds occupy nine decimal digits, matching
                 * editor_set_aside's existing naming convention. */
                int n=snprintf(aside,sizeof aside,"%s.set-aside-%lld%09ld",entry->d_name,(long long)(now/1000000000u),(long)(now%1000000000u));
                if(n<0 || (size_t)n>=sizeof aside) rc=JOURNAL_INVALID;
                else if(os_rename_at(&dir->dir,entry->d_name,&dir->dir,aside,OS_RENAME_NOREPLACE)) rc=JOURNAL_IO;
                else changed=true;
            }
        }
        os_file_close(&fd);
    }
    /* This separate pass also removes lock litter whose temp disappeared
     * before this launch. Live/malformed names are preserved. */
    if(dir->error!=OS_OK) rc=JOURNAL_IO;
    listing_rewind(dir,JOURNAL_LIST_ROTATION_LOCK);
    while((entry=listing_next(dir))) {
        size_t len=strlen(entry->d_name); static const char suffix[]=".lock";
        if(strncmp(entry->d_name,"..sublimite-journal-",strlen("..sublimite-journal-")) ||
           len<=sizeof suffix || strcmp(entry->d_name+len-(sizeof suffix-1u),suffix)) continue;
        char temp[sizeof entry->d_name]; size_t size=len-1u-(sizeof suffix-1u);
        memcpy(temp,entry->d_name+1,size); temp[size]=0; rotation_name name;
        if(!rotation_parse(temp,&name) || (!name.source && legacy_live)) continue;
        os_stat st;
        if(os_stat_at(&dir->dir,temp,OS_STAT_NOFOLLOW,&st)==OS_E_NOENT)
            changed=remove_rotation_lock(dir,entry->d_name) || changed;
    }
    if(dir->error!=OS_OK) rc=JOURNAL_IO;
    if(changed && os_dir_sync(&dir->dir)) rc=JOURNAL_IO;
    return rc;
}
static int record(void *ctx, const journal_record *r)
{
    inventory *inv=ctx; artifact *a=inv->scanning;
    if(r->type==JOURNAL_TARGET) {
        saved_identity *saved=inv->bases;
        while(saved && saved->id!=r->buffer_id) saved=saved->next;
        journal_base target;
        if(!saved || journal_capture_base((const char *)r->data,&target) ||
           saved->size!=target.size || saved->mtime_ns!=target.mtime_ns ||
           saved->inode!=target.inode || saved->device!=target.device ||
           saved->ctime_ns!=target.ctime_ns || saved->prefix_crc!=target.prefix_crc ||
           saved->prefix_len!=target.prefix_len) a->clean=false;
        return 0;
    }
    if(r->type==JOURNAL_INSERT || r->type==JOURNAL_DELETE) a->clean=false;
    /* edit-28u: a BASE names a tab-set buffer. Checkpoints of text equal to
     * BASE carry no INSERT/DELETE, so a BASE alone is a clean tab set, which
     * is hot-exit state (PRD): discovery adopts it and retires the journal
     * after its own checkpoint. Only BASE-free, edit-free session journals
     * are empty. Dotted names (set-aside, .retired) are never discovered,
     * hold no restorable tab set, and keep the edit-rfq rule. */
    if(r->type==JOURNAL_BASE && a->generated && !a->aside && !a->retired) a->clean=false;
    /* (P) wire PREPARED phase = 1 at sequence64's following phase32.
     * FINISHED is metadata for a saved identity, not an unsaved edit. */
    if(r->type==JOURNAL_SAVE && r->data[sizeof(uint64_t)]==1) a->clean=false;
    if(r->type==JOURNAL_BASE || r->type==JOURNAL_SAVE) {
        journal_record decoded=*r; decoded.type=JOURNAL_BASE;
        if(r->type==JOURNAL_SAVE) {
            /* (P) SAVE prefix = sequence64 + phase32 + reserved32. */
            decoded.data+=sizeof(uint64_t)+sizeof(uint32_t)+sizeof(uint32_t);
            decoded.size-=sizeof(uint64_t)+sizeof(uint32_t)+sizeof(uint32_t);
        }
        journal_base base; char path[sizeof base.captured_path];
        if(journal_decode_base(&decoded,&base,path,sizeof path)) { inv->uncertain=true; return JOURNAL_INVALID; }
        if(r->type==JOURNAL_BASE) {
            saved_identity *saved=edit_pool_get(&inv->identities);
            if(!saved) { inv->uncertain=true; return JOURNAL_NOMEM; }
            *saved=(saved_identity){.next=inv->bases,.id=r->buffer_id,.size=base.size,
                .mtime_ns=base.mtime_ns,.inode=base.inode,.device=base.device,
                .ctime_ns=base.ctime_ns,.prefix_crc=base.prefix_crc,.prefix_len=base.prefix_len};
            inv->bases=saved;
        }
        for(size_t i=0;i<inv->count;i++) if(inv->items[i].base && !strcmp(path,inv->items[i].path)) inv->items[i].referenced=true;
        const char *leaf=strrchr(path,'/');
        if(!*path || (leaf && !strncmp(leaf+1,".sublimite-base-",strlen(".sublimite-base-"))) || journal_check_base(&base)) a->clean=false;
    }
    return 0;
}
static int lease(os_dir *dir, artifact *a)
{
    char name[OS_NAME_MAX], source[OS_NAME_MAX]; strcpy(source,a->name);
    char *suffix=strstr(source,".set-aside-"); if(!suffix) suffix=strstr(source,".retired");
    if(suffix) *suffix=0;
    int n=snprintf(name,sizeof name,".%s.lock",source);
    if(n<0 || (size_t)n>=sizeof name) return JOURNAL_INVALID;
    if(os_file_open_at(&a->lease,dir,name,OS_OPEN_CREATE|OS_OPEN_RDWR|OS_OPEN_NOFOLLOW)!=OS_OK ||
       os_file_try_lock(&a->lease,OS_LOCK_EXCLUSIVE)!=OS_OK) return JOURNAL_BUSY;
    return 0;
}
static int remove_verified(os_dir *dfd, artifact *a)
{
    os_stat now;
    if(os_stat_at(dfd,a->name,OS_STAT_NOFOLLOW,&now) || !same(&now,&a->identity)) return JOURNAL_BUSY;
    char retired[OS_NAME_MAX];
    int n=snprintf(retired,sizeof retired,"%s%s",a->name,a->retired?"":".retired");
    if(n<0 || (size_t)n>=sizeof retired) return JOURNAL_INVALID;
    if(!a->retired && os_rename_at(dfd,a->name,dfd,retired,OS_RENAME_NOREPLACE)) return JOURNAL_IO;
    /* A crash at either barrier leaves only already-proven discardable bytes. */
    if(os_dir_sync(dfd)) return JOURNAL_IO;
    if(os_stat_at(dfd,retired,OS_STAT_NOFOLLOW,&now) || !journal_same_id(&now,&a->identity) || now.size!=a->identity.size) return JOURNAL_BUSY;
    if(os_unlink_at(dfd,retired) || os_dir_sync(dfd)) return JOURNAL_IO;
    return 0;
}
static int oldest(const void *left, const void *right)
{
    const artifact *a=left, *b=right;
    if(a->identity.mtime_ns!=b->identity.mtime_ns) return a->identity.mtime_ns<b->identity.mtime_ns?-1:1;
    return strcmp(a->path,b->path);
}
/* edit-cxo/oo3: a generated lease file whose journal (and every dotted
 * variant sharing its lease) is gone is litter. Runs after this call's own
 * leases are closed, still under the directory guard. Unlink only while
 * holding the lease, after rechecking absence and that the name is still the
 * locked inode; journal_session_create picks a new name if it lost that race.
 * One namespace scan per lease file: startup only. */
static int sweep_leases(journal_listing *dir)
{
    static const char prefix[]=".session-", suffix[]=".lock";
    listing_rewind(dir,JOURNAL_LIST_LEASE); journal_entry *entry;
    while((entry=listing_next(dir))) {
        size_t len=strlen(entry->d_name);
        if(len<=sizeof prefix-1u+sizeof suffix-1u || strncmp(entry->d_name,prefix,sizeof prefix-1u) ||
           strcmp(entry->d_name+len-(sizeof suffix-1u),suffix)) continue;
        char leaf[sizeof entry->d_name];
        memcpy(leaf,entry->d_name+1,len-1u-(sizeof suffix-1u)); leaf[len-1u-(sizeof suffix-1u)]=0;
        if(journal_session_absent_at(&dir->dir,leaf)) continue;
        os_file fd=OS_FILE_INIT; os_err opened=os_file_open_at(&fd,&dir->dir,entry->d_name,OS_OPEN_RDWR|OS_OPEN_NOFOLLOW|OS_OPEN_NONBLOCK);
        if(opened!=OS_OK) continue;
        os_stat held,named;
        if(!os_file_try_lock(&fd,OS_LOCK_EXCLUSIVE) && !journal_session_absent_at(&dir->dir,leaf) &&
           !os_file_stat(&fd,&held) && !os_stat_at(&dir->dir,entry->d_name,OS_STAT_NOFOLLOW,&named) &&
           named.kind==OS_KIND_FILE && journal_same_id(&held,&named))
            (void)os_unlink_at(&dir->dir,entry->d_name);
        os_file_close(&fd);
    }
    return dir->error==OS_OK?0:JOURNAL_IO;
}
int journal_retire_with_io(const char *directory, const char *current, const journal_retire_io *io)
{
    char canonical[sizeof ((journal_base *)0)->captured_path];
    if(!directory || !journal_canonical(directory,canonical,sizeof canonical)) return JOURNAL_IO;
    journal_listing storage, *dir=&storage; if(!listing_open(dir,canonical,NULL)) return JOURNAL_IO;
    /* Serialize housekeeping on the directory inode itself: no additional
     * persistent artifact and no replaceable lock-file name. */
    if(os_dir_try_lock(&dir->dir,OS_LOCK_EXCLUSIVE)!=OS_OK) { listing_close(dir); return JOURNAL_BUSY; }
    dir->io=io;
    if(io && io->locked) io->locked(io->ctx);
    char current_path[sizeof canonical]=""; if(current && !journal_canonical(current,current_path,sizeof current_path)) current_path[0]=0;
    /* Sweep before inventory/scanning/retirement can remove an original.
     * The exclusive directory lease excludes every live new rotation. */
    int copy_rc=sweep_copies(dir);
    int rotation_rc=copy_rc?0:sweep_rotations(dir,current_path);
    if(copy_rc || rotation_rc) { listing_close(dir); return copy_rc?copy_rc:rotation_rc; }
    listing_rewind(dir,JOURNAL_LIST_COUNT);
    size_t count=0; journal_entry *entry;
    while((entry=listing_next(dir))) count++;
    if(dir->error!=OS_OK) { listing_close(dir); return JOURNAL_IO; }
    if(count>SIZE_MAX/sizeof(artifact)) { listing_close(dir); return JOURNAL_NOMEM; }
    edit_arena arena;
    if(edit_arena_init(&arena,(count?count:1)*sizeof(artifact))) { listing_close(dir); return JOURNAL_NOMEM; }
    artifact *items=edit_arena_alloc(&arena,count*sizeof *items,_Alignof(artifact));
    inventory inv={.items=items}; int rc=copy_rc?copy_rc:rotation_rc; uint64_t total=0;
    listing_rewind(dir,JOURNAL_LIST_INVENTORY);
    while((entry=listing_next(dir))) {
        /* Never content-probe or lease a rotation temp, including its pinned
         * set-aside. Unknown reserved names also cannot prove orphan BASEs. */
        if(!strncmp(entry->d_name,".sublimite-journal-",strlen(".sublimite-journal-"))) { inv.uncertain=true; continue; }
        if(!strncmp(entry->d_name,"..sublimite-journal-",strlen("..sublimite-journal-"))) continue;
        bool base=!strncmp(entry->d_name,".sublimite-base-",strlen(".sublimite-base-"));
        bool named=!strncmp(entry->d_name,"session-",strlen("session-"));
        bool session=named;
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        if(!base && !named) {
            os_stat other;
            if(os_stat_at(&dir->dir,entry->d_name,OS_STAT_NOFOLLOW,&other)) { inv.uncertain=true; continue; }
            /* A fallback retained original may be referenced by a journal in
             * a child directory. Do not assume this shallow inventory proves
             * orphanhood in that case. */
            if(other.kind==OS_KIND_DIR || other.kind==OS_KIND_LINK) { inv.uncertain=true; continue; }
            if(other.kind!=OS_KIND_FILE) continue;
            os_file probe=OS_FILE_INIT;
            if(os_file_open_at(&probe,&dir->dir,entry->d_name,OS_OPEN_READ|OS_OPEN_NOFOLLOW|OS_OPEN_NONBLOCK)!=OS_OK) { inv.uncertain=true; continue; }
            /* (P) wire magic is the ASCII byte string JNL1. This includes
             * explicitly named journals, not just generated session names. */
            uint8_t magic[sizeof "JNL1"-1u]; size_t got=0; os_err read=os_file_pread(&probe,magic,sizeof magic,0,&got); os_file_close(&probe);
            if(read!=OS_OK) { inv.uncertain=true; continue; }
            if(got!=sizeof magic || memcmp(magic,"JNL1",sizeof magic)) continue;
            session=true;
        }
        /* Directory growth while inventorying is uncertain: preserve all bases. */
        if(inv.count==count) { inv.uncertain=true; break; }
        artifact *a=&items[inv.count++]; memset(a,0,sizeof *a); a->lease=(os_file)OS_FILE_INIT; a->base=base; a->generated=named;
        strcpy(a->name,entry->d_name);
        int n=snprintf(a->path,sizeof a->path,"%s/%s",canonical,entry->d_name);
        if(n<0 || (size_t)n>=sizeof a->path || os_stat_at(&dir->dir,a->name,OS_STAT_NOFOLLOW,&a->identity) || a->identity.kind!=OS_KIND_FILE) { inv.uncertain=true; continue; }
        a->aside=strstr(entry->d_name,".set-aside-")!=NULL;
        a->retired=strstr(entry->d_name,".retired")!=NULL;
        a->session=session && (!named || !strchr(entry->d_name,'.') || a->aside || a->retired);
        if((base || a->aside) && (uint64_t)a->identity.size<=UINT64_MAX-total) total+=(uint64_t)a->identity.size;
        else if(base || a->aside) inv.uncertain=true;
    }
    if(dir->error!=OS_OK) { inv.uncertain=true; rc=JOURNAL_IO; }
    /* Gather the entire namespace before marking BASE/SAVE references. A
     * base entry may occur after its journal in arbitrary readdir order. */
    for(size_t i=0;!rc && i<inv.count;i++) {
        artifact *a=&items[i];
        if(a->base) continue;
        if(!a->session) { inv.uncertain=true; continue; }
        bool own=*current_path && !strcmp(current_path,a->path);
        if(!own && lease(&dir->dir,a)) { inv.uncertain=true; continue; }
        a->clean=!own && (a->generated || a->aside || a->retired); inv.scanning=a;
        /* edit-fm4: never read a GB-scale artifact on the startup path. An
         * unscanned artifact is pinned, and its BASE references are unknown. */
        if((uint64_t)a->identity.size>JOURNAL_STARTUP_SCAN_BYTES) { a->clean=false; inv.uncertain=true; continue; }
        /* (P) every BASE consumes at least JOURNAL_HEADER wire bytes. The
         * file-size/header quotient bounds identity nodes without a guessed
         * buffer count. Pool pages commit only for actual BASE records. */
        uint64_t slots=(uint64_t)a->identity.size/JOURNAL_HEADER;
        if(slots>SIZE_MAX/sizeof(saved_identity) ||
           edit_pool_init(&inv.identities,sizeof(saved_identity),_Alignof(saved_identity),slots?(size_t)slots:1)) {
            a->clean=false; inv.uncertain=true; continue;
        }
        inv.bases=NULL;
        journal_replay_result result;
        os_file scan=OS_FILE_INIT;
        os_stat held;
        if(os_file_open_at(&scan,&dir->dir,a->name,OS_OPEN_READ|OS_OPEN_NOFOLLOW|OS_OPEN_NONBLOCK)!=OS_OK ||
           os_file_stat(&scan,&held) || !same(&a->identity,&held) ||
           journal_scan_open(scan,record,&inv,&result) || result.corrupt) { a->clean=false; inv.uncertain=true; }
        os_file_close(&scan);
        edit_pool_free(&inv.identities); inv.bases=NULL;
        os_stat after;
        if(os_stat_at(&dir->dir,a->name,OS_STAT_NOFOLLOW,&after) || !same(&a->identity,&after)) { a->clean=false; inv.uncertain=true; }
        /* An empty placeholder contains no bytes that could encode edits. */
    }
    qsort(items,inv.count,sizeof *items,oldest);
    for(size_t i=0;!rc && i<inv.count;i++) {
        artifact *a=&items[i];
        bool eligible=a->base?!inv.uncertain && !a->referenced:a->session && a->clean;
        bool capped=a->base || a->aside;
        if(!eligible || (capped && !a->retired && total<=JOURNAL_ARTIFACT_BYTES)) continue;
        int removed=remove_verified(&dir->dir,a);
        if(!removed && capped) total-=(uint64_t)a->identity.size;
        else if(removed && removed!=JOURNAL_BUSY) rc=removed;
    }
    for(size_t i=0;i<inv.count;i++) os_file_close(&items[i].lease);
    edit_arena_free(&arena);
    if(!rc) rc=sweep_leases(dir);
    listing_close(dir); return rc;
}

int journal_retire_directory(const char *directory, const char *current)
{ return journal_retire_with_io(directory,current,NULL); }
