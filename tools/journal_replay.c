/* Manual recovery only; (P) argc/argv indices count the executable, optional
 * flags and the source/destination pair. Buffer IDs parse as unsigned decimal. */
#include "journal/journal.h"
#include <errno.h>
#include <inttypes.h>
#include <string.h>
int main(int argc, char **argv)
{
    /* edit-fi0p F12: explicit acknowledgement is separate from replay, so
     * selecting one buffer never implicitly discards a multi-buffer source.
     * (P) argc counts program, acknowledgement flag and source pathname. */
    if(argc==3 && !strcmp(argv[1],"--acknowledge")) {
        int rc=journal_acknowledge(argv[2]);
        if(rc) fprintf(stderr,"journal_replay: acknowledgement error=%d; source retained unless unlink completed\n",rc);
        else printf("ACKNOWLEDGED %s\n",argv[2]);
        return rc;
    }
    bool apply=false; uint64_t id=0; int arg=1;
    while(arg<argc && !strncmp(argv[arg],"--",strlen("--"))) {
        if(!strcmp(argv[arg],"--apply")) { apply=true; arg++; }
        else if(!strcmp(argv[arg],"--buffer") && arg+1<argc) {
            char *end; errno=0;
            unsigned long long value=strtoull(argv[arg+1],&end,10); /* (P) decimal radix */
            if(errno || *end || !value || argv[arg+1][0]=='-' || value>UINT64_MAX) return JOURNAL_INVALID;
            id=(uint64_t)value; arg+=2;
        } else break;
    }
    if(argc-arg!=2) {
        fprintf(stderr,"usage: %s [--apply] [--buffer ID] JOURNAL FILE\n"
            "       %s --acknowledge RESERVED_SET_ASIDE\n",argv[0],argv[0]); return JOURNAL_INVALID;
    }
    int rc=journal_manual_replay(argv[arg],argv[arg+1],id,apply,stdout);
    if(rc) fprintf(stderr,"journal_replay: error=%d; source journal retained\n",rc);
    return rc;
}
