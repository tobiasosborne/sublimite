#include "journal/private.h"

int journal_default_dir(char *out, size_t cap)
{
    if(!out || !cap) return JOURNAL_INVALID;
    return os_app_dir(OS_APP_DATA,out,cap)==OS_OK?JOURNAL_OK:JOURNAL_INVALID;
}
/* Journal names and encoded paths use '/', including drive/UNC paths on
 * Windows. Win32 accepts that separator; POSIX paths are left byte exact. */
char *journal_canonical(const char *path, char *out, size_t cap)
{
    if(os_path_canonical(path,out,cap)!=OS_OK) return NULL;
    journal_slashes(out);
    return out;
}
/* (P) Preserve mkstemp's six-character schema and exclusive creation. Each
 * collision draws fresh random bytes; no retry limit or overwrite. */
os_err journal_temp(os_file *out, char *path, size_t suffix)
{
    static const char alphabet[]="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t len=strlen(path), count=sizeof "XXXXXX"-1u;
    if(len<suffix+count) return OS_E_INVAL;
    char *at=path+len-suffix-count;
    for(;;) {
        uint8_t random[sizeof "XXXXXX"-1u];
        os_err e=os_random(random,sizeof random); if(e!=OS_OK) return e;
        for(size_t i=0;i<count;i++) at[i]=alphabet[random[i]%(sizeof alphabet-1u)];
        e=os_file_open(out,path,OS_OPEN_RDWR|OS_OPEN_CREATE|OS_OPEN_EXCL);
        if(e!=OS_E_EXIST) return e;
    }
}
