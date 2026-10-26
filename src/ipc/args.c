#include "ipc.h"
#include "os/os.h"
#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#endif
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *copy_string(edit_arena *arena, const char *s)
{
    size_t n=strlen(s)+1; char *p=edit_arena_alloc(arena,n,1);
    if(p!=NULL) memcpy(p,s,n);
    return p;
}
/* Resolve each existing prefix before interpreting the next '..'. This also
 * canonicalizes the existing parent of a file that will be created later. */
#if defined(_WIN32)
static void separators(char *s)
{
    for(;*s;s++) if(*s=='\\') *s='/';
}
static size_t root_size(const char *s)
{
    if(s[0] && s[1]==':' && s[2]=='/') return 3;
    if(s[0]=='/' && s[1]=='/') {
        const char *server=strchr(s+2,'/');
        if(!server || server==s+2 || !server[1]) return 0;
        const char *share=strchr(server+1,'/');
        return share ? (size_t)(share-s)+1u : strlen(s);
    }
    return 0;
}
static ipc_result canonical(const char *cwd, const char *path, char *out)
{
    char full[IPC_PATH_CAP], resolved[IPC_PATH_CAP];
    if(!path[0]) return IPC_INVALID;
    int n;
    if(os_path_is_absolute(path)) n=snprintf(full,sizeof full,"%s",path);
    else {
        /* Do not silently reinterpret drive-relative or root-relative paths. */
        if(path[0]=='/' || path[0]=='\\' || (path[1]==':' && path[0])) return IPC_INVALID;
        n=snprintf(full,sizeof full,"%s/%s",cwd,path);
    }
    if(n<0 || (size_t)n>=sizeof full) return IPC_LIMIT;
    separators(full);
    size_t root=root_size(full), used=root;
    if(!root) return IPC_INVALID;
    memcpy(out,full,root); out[root]=0;
    bool known=true, isdir=true;
    char *cursor=full+root;
    while(*cursor) {
        while(*cursor=='/') cursor++;
        if(!*cursor) break;
        char *part=cursor;
        while(*cursor && *cursor!='/') cursor++;
        char saved=*cursor; *cursor=0;
        if(known && !isdir) return IPC_INVALID;
        if(!strcmp(part,"..")) {
            while(used>root && out[used-1]!='/') used--;
            if(used>root) used--;
            out[used]=0;
            if(known) isdir=true;
        } else if(strcmp(part,".")) {
            size_t len=strlen(part), sep=used && out[used-1]!='/' ? 1u : 0u;
            if(used+sep+len+1>IPC_PATH_CAP) return IPC_LIMIT;
            if(sep) out[used++]='/';
            memcpy(out+used,part,len+1); used+=len;
            if(known) {
                os_err rc=os_path_canonical(out,resolved,sizeof resolved);
                if(rc==OS_OK) {
                    separators(resolved); used=strlen(resolved); memcpy(out,resolved,used+1);
                    root=root_size(out);
                    os_stat st; isdir=os_stat_path(out,0,&st)==OS_OK && st.kind==OS_KIND_DIR;
                } else if(rc==OS_E_NOENT) known=false;
                else return rc==OS_E_NOTDIR || rc==OS_E_INVAL ? IPC_INVALID : IPC_IO;
            }
        }
        *cursor=saved;
    }
    if(strlen(full)>root && full[strlen(full)-1]=='/' && !(known && isdir)) return IPC_INVALID;
    return IPC_OK;
}
#else
static ipc_result canonical(const char *cwd, const char *path, char *out)
{
    char full[IPC_PATH_CAP], resolved[IPC_PATH_CAP];
    size_t cn=strlen(cwd), pn=strlen(path);
    if(pn==0) return IPC_INVALID;
    if(path[0]=='/') {
        if(pn>=sizeof full) return IPC_LIMIT;
        memcpy(full,path,pn+1);
    } else {
        if(cn+pn+2>sizeof full) return IPC_LIMIT;
        memcpy(full,cwd,cn); full[cn]='/'; memcpy(full+cn+1,path,pn+1);
    }
    out[0]='/'; out[1]='\0'; size_t used=1;
    /* known: every component so far exists and out is its real path. A regular
     * file prefix followed by anything else is ENOTDIR, exactly as the kernel
     * resolves it (edit-457.21 review §3); lexical rules apply only after the
     * first genuinely missing component. */
    bool known=true, isdir=true;
    char *cursor=full;
    while(*cursor) {
        while(*cursor=='/') cursor++;
        if(!*cursor) break;
        char *part=cursor;
        while(*cursor && *cursor!='/') cursor++;
        char saved=*cursor; *cursor='\0';
        if(known && !isdir) return IPC_INVALID;
        if(strcmp(part,"..")==0) {
            while(used>1 && out[used-1]!='/') used--;
            if(used>1) used--;
            out[used]='\0';
            if(known) isdir=true;
        } else if(strcmp(part,".")!=0) {
            size_t n=strlen(part), sep=used>1?1u:0u;
            if(used+sep+n+1>IPC_PATH_CAP) return IPC_LIMIT;
            if(sep) out[used++]='/';
            memcpy(out+used,part,n+1); used+=n;
            if(known) {
                struct stat st;
                if(realpath(out,resolved)!=NULL) {
                    used=strlen(resolved); memcpy(out,resolved,used+1);
                    isdir=stat(out,&st)==0 && S_ISDIR(st.st_mode);
                } else if(errno==ENOENT) known=false;
                else if(errno==ENOTDIR) return IPC_INVALID;
                else return IPC_IO;
            }
        }
        *cursor=saved;
    }
    /* A trailing slash demands a directory. */
    if(strlen(full)>1 && full[strlen(full)-1]=='/' && !(known && isdir)) return IPC_INVALID;
    return IPC_OK;
}
#endif
static int coordinate(const char *s, uint32_t *out)
{
    if(*s=='\0') return 0;
    uint64_t n=0; bool overflow=false;
    for(const char *p=s;*p;p++) {
        if(*p<'0' || *p>'9') return 0;
        if(n>UINT32_MAX/10u) overflow=true;
        if(!overflow) { n=n*10u+(uint64_t)(*p-'0'); if(n>UINT32_MAX) overflow=true; }
    }
    if(overflow || n==0) return -1;
    *out=(uint32_t)n; return 1;
}
void ipc_args_fini(ipc_args *args)
{
    if(args==NULL) return;
    edit_arena_free(&args->arena); memset(args,0,sizeof *args);
}
ipc_result ipc_parse_args(int argc, char *const argv[], ipc_args *out)
{
    if(out==NULL) return IPC_INVALID;
    memset(out,0,sizeof *out);
    if(argc<1 || argv==NULL) return IPC_INVALID;
    if(edit_arena_init(&out->arena,2u*IPC_MAX_WIRE)!=0) return IPC_IO;
    ipc_result rc=IPC_OK; char cwd[IPC_PATH_CAP];
    if(os_cwd(cwd,sizeof cwd)!=OS_OK) { rc=IPC_IO; goto fail; }
    out->request.cwd=copy_string(&out->arena,cwd);
    if(out->request.cwd==NULL) { rc=IPC_LIMIT; goto fail; }
    bool options=true;
    for(int i=1;i<argc;i++) {
        const char *arg=argv[i];
        if(arg==NULL) { rc=IPC_INVALID; goto fail; }
        if(options && strcmp(arg,"--")==0) { options=false; continue; }
        if(options && strcmp(arg,"--wait")==0) { out->request.wait=true; continue; }
        if(options && strcmp(arg,"--new-instance")==0) { out->request.new_instance=true; continue; }
        if(options) {
            const char *family_flag="--font-family", *file_flag="--font-file", *chain_flag="--font-fallbacks";
            size_t family_len=strlen(family_flag), file_len=strlen(file_flag), chain_len=strlen(chain_flag);
            bool family=!strncmp(arg,family_flag,family_len) && (arg[family_len]=='=' || !arg[family_len]);
            bool file=!strncmp(arg,file_flag,file_len) && (arg[file_len]=='=' || !arg[file_len]);
            bool chain=!strncmp(arg,chain_flag,chain_len) && (arg[chain_len]=='=' || !arg[chain_len]);
            if(family || file || chain) {
                size_t flag_len=family?family_len:file?file_len:chain_len;
                const char *value=arg+flag_len;
                if(*value=='=') value++;
                else {
                    if(i+1>=argc || !argv[i+1] || !strncmp(argv[i+1],"--",strlen("--"))) { rc=IPC_INVALID; goto fail; }
                    value=argv[++i];
                }
                if(strlen(value)>=IPC_PATH_CAP) { rc=IPC_LIMIT; goto fail; }
                const char *stored=copy_string(&out->arena,value);
                if(!stored) { rc=IPC_LIMIT; goto fail; }
                if(family) out->font_family=stored; else if(file) out->font_file=stored; else out->font_fallbacks=stored;
                continue;
            }
        }
        if(strcmp(arg,"-")==0) {
            if(out->request.has_stdin) { rc=IPC_INVALID; goto fail; }
            out->request.has_stdin=true; continue;
        }
        if(options && arg[0]=='-') { rc=IPC_INVALID; goto fail; }
        if(out->request.count==IPC_MAX_PATHS || strlen(arg)>=IPC_PATH_CAP) { rc=IPC_LIMIT; goto fail; }
        char name[IPC_PATH_CAP], path[IPC_PATH_CAP]; memcpy(name,arg,strlen(arg)+1);
        uint32_t line=1,col=1;
#if defined(_WIN32)
        os_stat st;
#else
        struct stat st;
#endif
        /* A directory entry of that exact name (even a dangling symlink) wins over
         * coordinate parsing; only true absence enables suffixes, and every other
         * lookup failure is an error, never "absent" (edit-457.21 review §4). */
#if defined(_WIN32)
        os_err lookup=os_stat_path(name,OS_STAT_NOFOLLOW,&st);
        if(lookup!=OS_OK) {
            if(lookup!=OS_E_NOENT && lookup!=OS_E_INVAL) { rc=lookup==OS_E_NOTDIR?IPC_INVALID:IPC_IO; goto fail; }
#else
        if(lstat(name,&st)!=0) {
            if(errno!=ENOENT) { rc=errno==ENOTDIR?IPC_INVALID:IPC_IO; goto fail; }
#endif
            char *last=strrchr(name,':'); uint32_t value=1;
            int numeric=last!=NULL?coordinate(last+1,&value):0;
            if(numeric<0) { rc=IPC_INVALID; goto fail; }
            if(numeric>0) {
                *last='\0'; line=value;
                char *prev=strrchr(name,':'); uint32_t earlier=1;
                numeric=prev!=NULL?coordinate(prev+1,&earlier):0;
                if(numeric<0) { rc=IPC_INVALID; goto fail; }
                if(numeric>0) { *prev='\0'; line=earlier; col=value; }
            }
        }
        rc=canonical(cwd,name,path); if(rc!=IPC_OK) goto fail;
        char *stored=copy_string(&out->arena,path);
        if(stored==NULL) { rc=IPC_LIMIT; goto fail; }
        out->request.paths[out->request.count++]=(ipc_path){stored,line,col};
    }
    return IPC_OK;
fail:
    ipc_args_fini(out); return rc;
}
ipc_result ipc_args_read_stdin(ipc_args *args, int fd)
{
    if(args==NULL || !args->request.has_stdin || args->request.stdin_data!=NULL) return IPC_INVALID;
    size_t cap=IPC_MAX_WIRE-24u-strlen(args->request.cwd)-1u;
    for(size_t i=0;i<args->request.count;i++) cap-=12u+strlen(args->request.paths[i].path)+1u;
    uint8_t *data=edit_arena_alloc(&args->arena,cap,1);
    if(data==NULL) return IPC_LIMIT;
#if defined(_WIN32)
    int mode=_setmode(fd,_O_BINARY);
    if(mode<0) return IPC_IO;
    ipc_result rc=IPC_OK;
#endif
    size_t n=0;
    for(;;) {
        uint8_t extra;
#if defined(_WIN32)
        int got=_read(fd,n<cap?data+n:&extra,(unsigned)(n<cap?cap-n:1u));
#else
        ssize_t got=read(fd,n<cap?data+n:&extra,n<cap?cap-n:1u);
#endif
        if(got<0) {
            if(errno==EINTR) continue;
#if defined(_WIN32)
            rc=IPC_IO; break;
#else
            return IPC_IO;
#endif
        }
        if(got==0) break;
        if(n==cap) {
#if defined(_WIN32)
            rc=IPC_LIMIT; break;
#else
            return IPC_LIMIT;
#endif
        }
        n+=(size_t)got;
    }
#if defined(_WIN32)
    if(_setmode(fd,mode)<0) return IPC_IO;
    if(rc!=IPC_OK) return rc;
#endif
    args->request.stdin_data=data; args->request.stdin_size=n; return IPC_OK;
}
