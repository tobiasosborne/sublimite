/* Startup only. (P) structural numbers denote empty/one, wire/schema sizes
 * come from journal.h; permissions are owner read/write (POSIX 0600). */
#include "journal/journal.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct artifact {
    char path[sizeof ((journal_base *)0)->captured_path];
    struct stat identity;
    int lease;
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
static bool same(const struct stat *a, const struct stat *b)
{
    return a->st_dev==b->st_dev && a->st_ino==b->st_ino && a->st_size==b->st_size &&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec && a->st_mtim.tv_nsec==b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
typedef struct rotation_name {
    uintmax_t device, inode, ctime_sec, ctime_nsec;
    char owner[sizeof ((struct dirent *)0)->d_name];
    bool source, aside;
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
    const char *p=name+sizeof prefix-1u; *out=(rotation_name){0}; uintmax_t ignored;
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
static DIR *rescan(DIR *dir)
{
    int fd=openat(dirfd(dir),".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(fd<0) return NULL;
    DIR *scan=fdopendir(fd); if(!scan) close(fd); return scan;
}
/* Legacy names cannot identify their owner. A busy existing session lease
 * therefore protects all of them, except this launch's fresh destination.
 * Do not create any lease just to perform this liveness check. */
static bool legacy_owner_live(DIR *dir, const char *current)
{
    DIR *scan=rescan(dir); if(!scan) return true;
    char own[sizeof ((struct dirent *)0)->d_name]="";
    const char *leaf=strrchr(current,'/');
    if(*current) {
        int n=snprintf(own,sizeof own,".%s.lock",leaf?leaf+1:current);
        if(n<0 || (size_t)n>=sizeof own) *own=0;
    }
    bool live=false; struct dirent *entry;
    for(;;) {
        errno=0; entry=readdir(scan); if(!entry) { live=errno!=0; break; }
        size_t len=strlen(entry->d_name);
        if(len<sizeof ".lock" || strcmp(entry->d_name+len-(sizeof ".lock"-1u),".lock") ||
           !strncmp(entry->d_name,"..sublimite-journal-",strlen("..sublimite-journal-")) ||
           !strcmp(entry->d_name,own)) continue;
        int fd=openat(dirfd(dir),entry->d_name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
        if(fd<0) { live=true; break; }
        live=flock(fd,LOCK_EX|LOCK_NB)!=0; close(fd); if(live) break;
    }
    closedir(scan); return live;
}
static bool rotation_source_exists(DIR *dir, const rotation_name *name)
{
    struct stat st;
    return !fstatat(dirfd(dir),name->owner,&st,AT_SYMLINK_NOFOLLOW) && S_ISREG(st.st_mode) &&
        (uintmax_t)st.st_dev==name->device && (uintmax_t)st.st_ino==name->inode &&
        (uintmax_t)st.st_ctim.tv_sec==name->ctime_sec && (uintmax_t)st.st_ctim.tv_nsec==name->ctime_nsec;
}
/* Existing stray lock files are litter only. Never O_CREAT for temp leases;
 * verify that the locked inode still has this name before removing it. */
static bool remove_rotation_lock(DIR *dir, const char *name)
{
    int fd=openat(dirfd(dir),name,O_RDWR|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0) return false;
    struct stat held,named; bool removed=false;
    if(!flock(fd,LOCK_EX|LOCK_NB) && !fstat(fd,&held) &&
       !fstatat(dirfd(dir),name,&named,AT_SYMLINK_NOFOLLOW) && S_ISREG(named.st_mode) &&
       held.st_dev==named.st_dev && held.st_ino==named.st_ino) removed=unlinkat(dirfd(dir),name,0)==0;
    close(fd); return removed;
}
static int sweep_rotations(DIR *dir, const char *current)
{
    bool legacy_live=legacy_owner_live(dir,current), changed=false; int rc=0;
    rewinddir(dir); struct dirent *entry;
    while((entry=readdir(dir))) {
        rotation_name name;
        if(!rotation_parse(entry->d_name,&name) || name.aside || (!name.source && legacy_live)) continue;
        int fd=openat(dirfd(dir),entry->d_name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
        if(fd<0) continue;
        struct stat held,named;
        bool valid=!flock(fd,LOCK_EX|LOCK_NB) && !fstat(fd,&held) && S_ISREG(held.st_mode) &&
            !fstatat(dirfd(dir),entry->d_name,&named,AT_SYMLINK_NOFOLLOW) && same(&held,&named);
        if(valid) {
            if(name.source && rotation_source_exists(dir,&name)) {
                if(unlinkat(dirfd(dir),entry->d_name,0)) rc=JOURNAL_IO; else changed=true;
            } else {
                /* Missing original, or legacy's unknown original: possible
                 * sole copy. Same set-aside convention as recovery; no copy,
                 * content read, age test, size limit, or replacement. */
                struct timespec now; char aside[sizeof entry->d_name];
                if(clock_gettime(CLOCK_REALTIME,&now)) rc=JOURNAL_IO;
                else {
                    /* (P) timespec nanoseconds occupy nine decimal digits,
                     * matching editor_set_aside's existing naming convention. */
                    int n=snprintf(aside,sizeof aside,"%s.set-aside-%lld%09ld",entry->d_name,(long long)now.tv_sec,now.tv_nsec);
                    if(n<0 || (size_t)n>=sizeof aside) rc=JOURNAL_INVALID;
                    else if(renameat2(dirfd(dir),entry->d_name,dirfd(dir),aside,RENAME_NOREPLACE)) rc=JOURNAL_IO;
                    else changed=true;
                }
            }
        }
        close(fd);
    }
    /* This separate pass also removes lock litter whose temp disappeared
     * before this launch. Live/malformed names are preserved. */
    rewinddir(dir);
    while((entry=readdir(dir))) {
        size_t len=strlen(entry->d_name); static const char suffix[]=".lock";
        if(strncmp(entry->d_name,"..sublimite-journal-",strlen("..sublimite-journal-")) ||
           len<=sizeof suffix || strcmp(entry->d_name+len-(sizeof suffix-1u),suffix)) continue;
        char temp[sizeof entry->d_name]; size_t size=len-1u-(sizeof suffix-1u);
        memcpy(temp,entry->d_name+1,size); temp[size]=0; rotation_name name;
        if(!rotation_parse(temp,&name) || (!name.source && legacy_live)) continue;
        struct stat st;
        if(fstatat(dirfd(dir),temp,&st,AT_SYMLINK_NOFOLLOW) && errno==ENOENT)
            changed=remove_rotation_lock(dir,entry->d_name) || changed;
    }
    if(changed && fsync(dirfd(dir))) rc=JOURNAL_IO;
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
static int lease(artifact *a)
{
    char path[sizeof a->path], source[sizeof a->path]; strcpy(source,a->path);
    char *suffix=strstr(source,".set-aside-"); if(!suffix) suffix=strstr(source,".retired");
    if(suffix) *suffix=0;
    char *slash=strrchr(source,'/'); if(!slash) return JOURNAL_INVALID;
    *slash=0;
    int n=snprintf(path,sizeof path,"%s/.%s.lock",source,slash+1);
    if(n<0 || (size_t)n>=sizeof path) return JOURNAL_INVALID;
    a->lease=open(path,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(a->lease<0 || flock(a->lease,LOCK_EX|LOCK_NB)) return JOURNAL_BUSY;
    return 0;
}
static int remove_verified(int dfd, artifact *a)
{
    struct stat now;
    if(lstat(a->path,&now) || !same(&now,&a->identity)) return JOURNAL_BUSY;
    char retired[sizeof a->path];
    int n=snprintf(retired,sizeof retired,"%s%s",a->path,a->retired?"":".retired");
    if(n<0 || (size_t)n>=sizeof retired) return JOURNAL_INVALID;
    if(!a->retired && renameat2(AT_FDCWD,a->path,AT_FDCWD,retired,RENAME_NOREPLACE)) return JOURNAL_IO;
    /* A crash at either barrier leaves only already-proven discardable bytes.
     * The dotted name is excluded from session discovery on the next launch. */
    if(fsync(dfd)) return JOURNAL_IO;
    if(lstat(retired,&now) || now.st_ino!=a->identity.st_ino || now.st_dev!=a->identity.st_dev || now.st_size!=a->identity.st_size) return JOURNAL_BUSY;
    if(unlink(retired) || fsync(dfd)) return JOURNAL_IO;
    return 0;
}
static int oldest(const void *left, const void *right)
{
    const artifact *a=left, *b=right;
    if(a->identity.st_mtim.tv_sec!=b->identity.st_mtim.tv_sec) return a->identity.st_mtim.tv_sec<b->identity.st_mtim.tv_sec?-1:1;
    if(a->identity.st_mtim.tv_nsec!=b->identity.st_mtim.tv_nsec) return a->identity.st_mtim.tv_nsec<b->identity.st_mtim.tv_nsec?-1:1;
    return strcmp(a->path,b->path);
}
/* edit-cxo/oo3: a generated lease file whose journal (and every dotted
 * variant sharing its lease) is gone is litter. Runs after this call's own
 * leases are closed, still under the directory guard. Unlink only while
 * holding the lease, after rechecking absence and that the name is still the
 * locked inode; journal_session_create picks a new name if it lost that race.
 * One namespace scan per lease file: startup only. */
static void sweep_leases(DIR *dir)
{
    static const char prefix[]=".session-", suffix[]=".lock";
    rewinddir(dir); struct dirent *entry;
    while((entry=readdir(dir))) {
        size_t len=strlen(entry->d_name);
        if(len<=sizeof prefix-1u+sizeof suffix-1u || strncmp(entry->d_name,prefix,sizeof prefix-1u) ||
           strcmp(entry->d_name+len-(sizeof suffix-1u),suffix)) continue;
        char leaf[sizeof entry->d_name];
        memcpy(leaf,entry->d_name+1,len-1u-(sizeof suffix-1u)); leaf[len-1u-(sizeof suffix-1u)]=0;
        if(journal_session_absent(dirfd(dir),leaf)) continue;
        int fd=openat(dirfd(dir),entry->d_name,O_RDWR|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
        if(fd<0) continue;
        struct stat held,named;
        if(!flock(fd,LOCK_EX|LOCK_NB) && !journal_session_absent(dirfd(dir),leaf) &&
           !fstat(fd,&held) && !fstatat(dirfd(dir),entry->d_name,&named,AT_SYMLINK_NOFOLLOW) &&
           S_ISREG(named.st_mode) && held.st_dev==named.st_dev && held.st_ino==named.st_ino)
            (void)unlinkat(dirfd(dir),entry->d_name,0);
        close(fd);
    }
}
int journal_retire_directory(const char *directory, const char *current)
{
    char canonical[sizeof ((journal_base *)0)->captured_path];
    if(!directory || !realpath(directory,canonical)) return JOURNAL_IO;
    DIR *dir=opendir(canonical); if(!dir) return JOURNAL_IO;
    /* Serialize housekeeping on the directory inode itself: no additional
     * persistent artifact and no replaceable lock-file name. */
    int guard=fcntl(dirfd(dir),F_DUPFD_CLOEXEC,0);
    if(guard<0 || flock(guard,LOCK_EX|LOCK_NB)) { if(guard>=0) close(guard); closedir(dir); return JOURNAL_BUSY; }
    char current_path[sizeof canonical]=""; if(current && !realpath(current,current_path)) current_path[0]=0;
    /* Sweep before inventory/scanning/retirement can remove an original.
     * The exclusive directory lease excludes every live new rotation. */
    int rotation_rc=sweep_rotations(dir,current_path);
    rewinddir(dir);
    size_t count=0; struct dirent *entry;
    while((entry=readdir(dir))) count++;
    if(count>SIZE_MAX/sizeof(artifact)) { close(guard); closedir(dir); return JOURNAL_NOMEM; }
    edit_arena arena;
    if(edit_arena_init(&arena,(count?count:1)*sizeof(artifact))) { close(guard); closedir(dir); return JOURNAL_NOMEM; }
    artifact *items=edit_arena_alloc(&arena,count*sizeof *items,_Alignof(artifact));
    inventory inv={.items=items}; int rc=rotation_rc; uint64_t total=0;
    rewinddir(dir);
    while((entry=readdir(dir))) {
        /* Never content-probe or lease a rotation temp, including its pinned
         * set-aside. Unknown reserved names also cannot prove orphan BASEs. */
        if(!strncmp(entry->d_name,".sublimite-journal-",strlen(".sublimite-journal-"))) { inv.uncertain=true; continue; }
        if(!strncmp(entry->d_name,"..sublimite-journal-",strlen("..sublimite-journal-"))) continue;
        bool base=!strncmp(entry->d_name,".sublimite-base-",strlen(".sublimite-base-"));
        bool named=!strncmp(entry->d_name,"session-",strlen("session-"));
        bool session=named;
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        if(!base && !named) {
            struct stat other;
            if(fstatat(dirfd(dir),entry->d_name,&other,AT_SYMLINK_NOFOLLOW)) { inv.uncertain=true; continue; }
            /* A fallback retained original may be referenced by a journal in
             * a child directory. Do not assume this shallow inventory proves
             * orphanhood in that case. */
            if(S_ISDIR(other.st_mode) || S_ISLNK(other.st_mode)) { inv.uncertain=true; continue; }
            if(!S_ISREG(other.st_mode)) continue;
            int probe=openat(dirfd(dir),entry->d_name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
            if(probe<0) { inv.uncertain=true; continue; }
            /* (P) wire magic is the ASCII byte string JNL1. This includes
             * explicitly named journals, not just generated session names. */
            uint8_t magic[sizeof "JNL1"-1u]; ssize_t got=pread(probe,magic,sizeof magic,0); close(probe);
            if(got<0) { inv.uncertain=true; continue; }
            if(got!=(ssize_t)sizeof magic || memcmp(magic,"JNL1",sizeof magic)) continue;
            session=true;
        }
        /* Directory growth while inventorying is uncertain: preserve all bases. */
        if(inv.count==count) { inv.uncertain=true; break; }
        artifact *a=&items[inv.count++]; memset(a,0,sizeof *a); a->lease=-1; a->base=base; a->generated=named;
        int n=snprintf(a->path,sizeof a->path,"%s/%s",canonical,entry->d_name);
        if(n<0 || (size_t)n>=sizeof a->path || lstat(a->path,&a->identity) || !S_ISREG(a->identity.st_mode) || a->identity.st_size<0) { inv.uncertain=true; continue; }
        a->aside=strstr(entry->d_name,".set-aside-")!=NULL;
        a->retired=strstr(entry->d_name,".retired")!=NULL;
        a->session=session && (!named || !strchr(entry->d_name,'.') || a->aside || a->retired);
        if((base || a->aside) && (uint64_t)a->identity.st_size<=UINT64_MAX-total) total+=(uint64_t)a->identity.st_size;
        else if(base || a->aside) inv.uncertain=true;
    }
    /* Gather the entire namespace before marking BASE/SAVE references. A
     * base entry may occur after its journal in arbitrary readdir order. */
    for(size_t i=0;i<inv.count;i++) {
        artifact *a=&items[i];
        if(a->base) continue;
        if(!a->session) { inv.uncertain=true; continue; }
        bool own=*current_path && !strcmp(current_path,a->path);
        if(!own && lease(a)) { inv.uncertain=true; continue; }
        a->clean=!own && (a->generated || a->aside || a->retired); inv.scanning=a;
        /* edit-fm4: never read a GB-scale artifact on the startup path. An
         * unscanned artifact is pinned, and its BASE references are unknown. */
        if((uint64_t)a->identity.st_size>JOURNAL_STARTUP_SCAN_BYTES) { a->clean=false; inv.uncertain=true; continue; }
        /* (P) every BASE consumes at least JOURNAL_HEADER wire bytes. The
         * file-size/header quotient bounds identity nodes without a guessed
         * buffer count. Pool pages commit only for actual BASE records. */
        uint64_t slots=(uint64_t)a->identity.st_size/JOURNAL_HEADER;
        if(slots>SIZE_MAX/sizeof(saved_identity) ||
           edit_pool_init(&inv.identities,sizeof(saved_identity),_Alignof(saved_identity),slots?(size_t)slots:1)) {
            a->clean=false; inv.uncertain=true; continue;
        }
        inv.bases=NULL;
        journal_replay_result result;
        if(journal_scan_file(a->path,record,&inv,&result) || result.corrupt) { a->clean=false; inv.uncertain=true; }
        edit_pool_free(&inv.identities); inv.bases=NULL;
        struct stat after;
        if(lstat(a->path,&after) || !same(&a->identity,&after)) { a->clean=false; inv.uncertain=true; }
        /* An empty placeholder contains no bytes that could encode edits. */
    }
    qsort(items,inv.count,sizeof *items,oldest);
    for(size_t i=0;i<inv.count;i++) {
        artifact *a=&items[i];
        bool eligible=a->base?!inv.uncertain && !a->referenced:a->session && a->clean;
        bool capped=a->base || a->aside;
        if(!eligible || (capped && !a->retired && total<=JOURNAL_ARTIFACT_BYTES)) continue;
        int removed=remove_verified(dirfd(dir),a);
        if(!removed && capped) total-=(uint64_t)a->identity.st_size;
        else if(removed && removed!=JOURNAL_BUSY) rc=removed;
    }
    for(size_t i=0;i<inv.count;i++) if(items[i].lease>=0) close(items[i].lease);
    edit_arena_free(&arena);
    sweep_leases(dir);
    close(guard); closedir(dir); return rc;
}
