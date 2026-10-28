/* (P) Xcursor wire format: 16-byte file header, 12-byte TOC entries,
 * 36-byte image header and width*height*4 bytes of little-endian ARGB. */
#include "x11/xcursor.h"
#include "xcursor_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int failures;
#define T(x) do { if (!(x)) { fprintf(stderr,"xcursor_test:%d: %s\n",__LINE__,#x); failures++; } } while (0)
static void parser(void)
{
    uint8_t b[256]; size_t n=test_xcursor_fixture(b); xcursor_image im={0};
    T(xcursor_decode(b,n,24,&im)); T(im.nominal==16); /* equal distance: first TOC image */
    T(xcursor_decode(b,n,31,&im)); T(im.nominal==32 && im.width==2 && im.height==2 && im.xhot==1 && im.yhot==1);
    if (im.pixels) T(im.pixels[0]==1); /* first frame, not the second size-32 chunk */
    for (size_t i=0;i<16u+3u*12u+2u*(36u+2u*2u*4u);i++) T(!xcursor_decode(b,i,32,&im));
    b[0]='x'; T(!xcursor_decode(b,n,32,&im)); (void)test_xcursor_fixture(b);
    test_xcursor_word(b+12,UINT32_MAX); T(!xcursor_decode(b,n,32,&im)); (void)test_xcursor_fixture(b);
    test_xcursor_word(b+24+12,UINT32_MAX); T(!xcursor_decode(b,n,32,&im)); (void)test_xcursor_fixture(b);
    size_t image=16u+3u*12u+36u+2u*2u*4u;
    test_xcursor_word(b+image+16,UINT32_MAX); T(!xcursor_decode(b,n,32,&im)); (void)test_xcursor_fixture(b);
    test_xcursor_word(b+image+24,2); T(!xcursor_decode(b,n,32,&im)); (void)test_xcursor_fixture(b);
    test_xcursor_word(b+image,35); T(!xcursor_decode(b,n,32,&im));
}
static void inheritance(void)
{
    char root[]="/tmp/xcursor-test-XXXXXX"; T(mkdtemp(root));
    const char *dirs[]={".icons",".icons/child",".icons/parent",".icons/parent/cursors"};
    char path[4096];
    for (size_t i=0;i<sizeof dirs/sizeof *dirs;i++) { (void)snprintf(path,sizeof path,"%s/%s",root,dirs[i]); T(mkdir(path,0700)==0); }
    (void)snprintf(path,sizeof path,"%s/.icons/child/index.theme",root);
    FILE *f=fopen(path,"w"); T(f); if (f) { fputs("[Icon Theme]\nInherits=child, parent\n",f); fclose(f); }
    uint8_t b[256]; size_t n=test_xcursor_fixture(b);
    (void)snprintf(path,sizeof path,"%s/.icons/parent/cursors/xterm",root);
    f=fopen(path,"wb"); T(f); if (f) { T(fwrite(b,1,n,f)==n); fclose(f); }
    uint8_t *storage=NULL; xcursor_image im={0};
    T(xcursor_load(root,"child","xterm",32,&im,&storage)); T(storage && im.nominal==32); free(storage);
    T(!xcursor_load(root,"child","absent",32,&im,&storage));
    unlink(path); (void)snprintf(path,sizeof path,"%s/.icons/child/index.theme",root); unlink(path);
    for (size_t i=sizeof dirs/sizeof *dirs;i;i--) { (void)snprintf(path,sizeof path,"%s/%s",root,dirs[i-1]); T(rmdir(path)==0); }
    T(rmdir(root)==0);
}
int main(void)
{
    parser(); inheritance();
    char theme[256]; uint32_t size;
    const char resources[]="Xcursor.theme: resource-theme\nXcursor.size: 36\nXft.dpi: 192\n";
    xcursor_settings(resources,sizeof resources-1u,"env-theme","48",theme,sizeof theme,&size);
    T(!strcmp(theme,"env-theme") && size==48);
    xcursor_settings(resources,sizeof resources-1u,NULL,NULL,theme,sizeof theme,&size);
    T(!strcmp(theme,"resource-theme") && size==36);
    const char dpi[]="Xft.dpi: 192\n";
    xcursor_settings(dpi,sizeof dpi-1u,NULL,NULL,theme,sizeof theme,&size);
    T(!strcmp(theme,"default") && size==48); /* (P) 24*192/96 */
    xcursor_settings(NULL,0,NULL,NULL,theme,sizeof theme,&size); T(size==24);
    xcursor_settings("Xft.dpi: nan",sizeof "Xft.dpi: nan"-1u,NULL,"bad",theme,sizeof theme,&size); T(size==24);
    if (failures) return 1;
    puts("xcursor_test: size selection, first frame, truncation/bounds and theme inheritance passed"); return 0;
}
