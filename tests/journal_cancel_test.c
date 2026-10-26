/* White-box cancellation exception: the frozen clone wrapper retries EINTR
 * internally, without the work epoch. The journal must use cancellable copy.
 * (P) two chunks = the existing 65536-byte copy buffer plus one byte. */
#include "journal/private.h"
#include <stdio.h>
static work_slot cancelled_slot;
static unsigned clone_retries;
static os_err interrupted_clone(os_file *to, const os_file *from)
{
    (void)to; (void)from;
    atomic_store(&cancelled_slot.epoch,2);
    /* Model EINTR then the wrapper's retry after cancellation. */
    clone_retries++; return OS_E_IO;
}
#define os_file_clone interrupted_clone
#include "../src/journal/journal.c"
#undef os_file_clone
#define T(c) do { if(!(c)) { fprintf(stderr,"journal_cancel_test:%d FAIL %s\n",__LINE__,#c); return 1; } } while(0)
static ssize_t cancel_copy(void *ctx, int fd, const uint8_t *p, size_t n, uint64_t off)
{
    (void)ctx; atomic_store(&cancelled_slot.epoch,2);
    os_file f=os_file_from_fd(fd); size_t done=0;
    return os_file_pwrite(&f,p,n,off,&done)==OS_OK?(ssize_t)done:-1;
}
int main(void)
{
    (void)interrupted_clone;
    char root[]="/tmp/sublimite-journal-cancel-XXXXXX"; T(mkdtemp(root));
    char source[OS_PATH_MAX], copy[OS_PATH_MAX];
    T(snprintf(source,sizeof source,"%s/source",root)>0);
    T(snprintf(copy,sizeof copy,"%s/copy",root)>0);
    os_file f=OS_FILE_INIT; T(os_file_open(&f,source,OS_OPEN_RDWR|OS_OPEN_CREATE|OS_OPEN_EXCL)==OS_OK);
    T(os_file_set_size(&f,65536u+1u)==OS_OK); os_file_close(&f);
    journal_base base; T(journal_capture_base(source,&base)==0);
    os_dir dir=OS_DIR_INIT; T(os_dir_open(&dir,root)==OS_OK);
    T(os_file_open_at(&f,&dir,"copy",OS_OPEN_RDWR|OS_OPEN_CREATE|OS_OPEN_EXCL)==OS_OK);
    journal_io io={0}; journal_retention r={.previous=&base,.path=copy,.directory=&dir,.name="copy",.fd=f,.io=&io};
    atomic_store(&cancelled_slot.epoch,2);
    work_ctx ctx={.slot=&cancelled_slot,.epoch=1};
    T(retain_generation(&ctx,&r)==JOURNAL_IO); T(clone_retries==0);
    atomic_store(&cancelled_slot.epoch,1); io.write=cancel_copy;
    T(retain_generation(&ctx,&r)==JOURNAL_IO);
    os_stat st; T(os_file_stat(&f,&st)==OS_OK && st.size==65536);
    os_file_close(&f); os_dir_close(&dir);
    T(os_unlink(copy)==OS_OK && os_unlink(source)==OS_OK && os_rmdir(root)==OS_OK);
    puts("journal_cancel_test: no opaque EINTR clone retries; cancellation before and between copy chunks passed"); return 0;
}
