/* Wire constants: upstream Xcursor(3), CURSOR FILES; little-endian CARD32.
 * https://www.x.org/releases/current/doc/man/man3/Xcursor.3.xhtml */
#include "x11/xcursor.h"
#include <xcb/render.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define IMAGE_TYPE UINT32_C(0xfffd0002)
static uint32_t word(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
bool xcursor_decode(const uint8_t *b, size_t n, uint32_t wanted, xcursor_image *out)
{
    if (!b || !out || !wanted || n<16 || memcmp(b,"Xcur",4) || word(b+8)>>16 != 1u) return false;
    size_t header=word(b+4); uint32_t count=word(b+12);
    if (header<16 || header>n || count>(n-header)/12u) return false;
    size_t table_end=header+(size_t)count*12u, chosen=0; uint32_t best=UINT32_MAX, nominal=0;
    bool found=false;
    for (uint32_t i=0;i<count;i++) {
        const uint8_t *t=b+header+(size_t)i*12u;
        if (word(t)!=IMAGE_TYPE || !word(t+4)) continue;
        uint32_t size=word(t+4), distance=size>wanted ? size-wanted : wanted-size;
        if (!found || distance<best) { chosen=word(t+8); nominal=size; best=distance; found=true; }
    }
    if (!found || chosen<table_end || chosen>n || n-chosen<36) return false;
    const uint8_t *p=b+chosen; size_t chunk=word(p);
    if (chunk<36 || chunk>n-chosen || word(p+4)!=IMAGE_TYPE || word(p+8)!=nominal || word(p+12)!=1u) return false;
    uint32_t w=word(p+16), h=word(p+20), x=word(p+24), y=word(p+28);
    /* (P) Xcursor images are at most 0x7fff on either axis. Hotspots must
     * also be usable by Render CreateCursor: strictly inside the image. */
    if (!w || !h || w>0x7fffu || h>0x7fffu || x>=w || y>=h ||
        (size_t)w>(n-chosen-chunk)/4u/h) return false;
    *out=(xcursor_image){nominal,w,h,x,y,p+chunk}; return true;
}
static uint8_t *read_file(const char *path, size_t *length)
{
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NONBLOCK); if (fd<0) return NULL;
    struct stat st;
    if (fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<0 || (uint64_t)st.st_size>=SIZE_MAX) { close(fd); return NULL; }
    size_t n=(size_t)st.st_size; uint8_t *b=malloc(n+1u);
    if (!b) { close(fd); return NULL; }
    size_t at=0;
    while (at<n) {
        ssize_t got=read(fd,b+at,n-at>SSIZE_MAX ? (size_t)SSIZE_MAX : n-at);
        if (got<0 && errno==EINTR) continue;
        if (got<=0) { free(b); close(fd); return NULL; }
        at+=(size_t)got;
    }
    close(fd); b[n]=0; *length=n; return b;
}
typedef struct theme_node { struct theme_node *next; char name[]; } theme_node;
static bool enqueue(theme_node **head, theme_node **tail, const char *name, size_t n)
{
    if (!n || memchr(name,'/',n) || memchr(name,'\0',n)) return true;
    for (theme_node *p=*head;p;p=p->next) if (strlen(p->name)==n && !memcmp(p->name,name,n)) return true;
    if (n>SIZE_MAX-sizeof(theme_node)-1u) return false;
    theme_node *p=malloc(sizeof *p+n+1u); if (!p) return false;
    p->next=NULL; memcpy(p->name,name,n); p->name[n]=0;
    if (*tail) (*tail)->next=p; else *head=p;
    *tail=p; return true;
}
static bool path_for(char *out, size_t cap, const char *home_dir, unsigned root, const char *theme, const char *tail)
{
    const char *roots[]={".local/share/icons",".icons","/usr/share/icons","/usr/share/pixmaps"};
    if (root<2 && (!home_dir || !*home_dir)) return false;
    int n=root<2 ? snprintf(out,cap,"%s/%s/%s/%s",home_dir,roots[root],theme,tail) :
                   snprintf(out,cap,"%s/%s/%s",roots[root],theme,tail);
    return n>=0 && (size_t)n<cap;
}
bool xcursor_load(const char *home_dir, const char *theme, const char *name,
                  uint32_t wanted, xcursor_image *out, uint8_t **storage)
{
    if (!theme || !name || !out || !storage || strchr(name,'/') || !wanted) return false;
    *storage=NULL; theme_node *head=NULL,*tail=NULL; bool ok=false;
    if (!enqueue(&head,&tail,theme,strlen(theme))) return false;
    for (theme_node *p=head;p && !ok;p=p->next) {
        char path[PATH_MAX], file[PATH_MAX];
        int m=snprintf(file,sizeof file,"cursors/%s",name);
        if (m<0 || (size_t)m>=sizeof file) break;
        for (unsigned root=0;root<4;root++) {
            if (!path_for(path,sizeof path,home_dir,root,p->name,file)) continue;
            size_t n; uint8_t *b=read_file(path,&n);
            if (b && xcursor_decode(b,n,wanted,out)) { *storage=b; ok=true; break; }
            free(b);
        }
        if (ok) break;
        /* Inheritance is breadth-first and visited once, including cycles.
         * The first installed index.theme supplies this theme's parents. */
        for (unsigned root=0;root<4;root++) {
            if (!path_for(path,sizeof path,home_dir,root,p->name,"index.theme")) continue;
            size_t n; uint8_t *b=read_file(path,&n); if (!b) continue;
            const char *at=(const char *)b, *end=at+n;
            while (at<end) {
                const char *line_end=memchr(at,'\n',(size_t)(end-at)); if (!line_end) line_end=end;
                while (at<line_end && (*at==' ' || *at=='\t')) at++;
                if ((size_t)(line_end-at)>=9u && !memcmp(at,"Inherits=",9)) {
                    at+=9;
                    while (at<line_end) {
                        while (at<line_end && (*at==',' || *at==';' || *at==' ' || *at=='\t' || *at=='\r')) at++;
                        const char *from=at;
                        while (at<line_end && *at!=',' && *at!=';' && *at!=' ' && *at!='\t' && *at!='\r') at++;
                        if (!enqueue(&head,&tail,from,(size_t)(at-from))) { free(b); goto done; }
                    }
                }
                at=line_end<end ? line_end+1 : end;
            }
            free(b); break;
        }
    }
done:
    while (head) { theme_node *next=head->next; free(head); head=next; }
    return ok;
}
static bool resource(const char *r, size_t n, const char *key, char *value, size_t cap)
{
    size_t keylen=strlen(key);
    while (n) {
        const char *end=memchr(r,'\n',n); size_t line=end ? (size_t)(end-r) : n;
        const char *at=r; size_t len=line;
        while (len && (*at==' ' || *at=='\t')) { at++; len--; }
        if (len>keylen && !memcmp(at,key,keylen) && at[keylen]==':') {
            at+=keylen+1u; len-=keylen+1u;
            while (len && (*at==' ' || *at=='\t')) { at++; len--; }
            while (len && (at[len-1u]==' ' || at[len-1u]=='\t' || at[len-1u]=='\r')) len--;
            if (!len || len>=cap || memchr(at,0,len)) return false;
            memcpy(value,at,len); value[len]=0; return true;
        }
        if (!end) break;
        r+=line+1u; n-=line+1u;
    }
    return false;
}
static uint32_t positive(const char *s)
{
    if (!s || !*s) return 0;
    char *end; errno=0; unsigned long v=strtoul(s,&end,10);
    return !errno && !*end && v && v<=UINT16_MAX ? (uint32_t)v : 0;
}
void xcursor_settings(const char *r, size_t n, const char *theme_env, const char *size_env,
                      char *theme, size_t cap, uint32_t *size)
{
    if (!theme || !cap || !size) return;
    char value[NAME_MAX+1u]; /* (P) one filesystem component plus NUL */
    const char *name=theme_env;
    if ((!name || !*name) && r && resource(r,n,"Xcursor.theme",value,sizeof value)) name=value;
    if (!name || !*name || strlen(name)>=cap || strchr(name,'/')) name="default";
    if (strlen(name)>=cap) { theme[0]=0; *size=0; return; }
    memcpy(theme,name,strlen(name)+1u);
    *size=positive(size_env);
    if (!*size && r && resource(r,n,"Xcursor.size",value,sizeof value)) *size=positive(value);
    if (!*size) {
        double dpi=96.0;
        if (r && resource(r,n,"Xft.dpi",value,sizeof value)) {
            char *end; double d=strtod(value,&end);
            if (!*end && isfinite(d) && d>0) dpi=d;
        }
        /* (P) Xcursor's 24 px at the 96 dpi reference; nearest pixel. */
        double scaled=24.0*dpi/96.0;
        *size=scaled>UINT16_MAX ? UINT16_MAX : scaled<1.0 ? 1u : (uint32_t)(scaled+0.5);
    }
}
static bool request_ok(xcb_connection_t *c, xcb_void_cookie_t cookie)
{ xcb_generic_error_t *e=xcb_request_check(c,cookie); bool ok=!e; free(e); return ok; }
xcb_cursor_t xcursor_upload(xcb_connection_t *c, xcb_window_t root, const xcursor_image *im)
{
    if (!c || !im || !im->pixels || !im->width || !im->height || im->width>0x7fffu || im->height>0x7fffu) return 0;
    const xcb_query_extension_reply_t *ext=xcb_get_extension_data(c,&xcb_render_id);
    if (!ext || !ext->present) return 0;
    xcb_render_query_version_reply_t *version=xcb_render_query_version_reply(c,xcb_render_query_version(c,0,5),NULL);
    bool capable=version && (version->major_version || version->minor_version>=5u); free(version);
    if (!capable) return 0;
    xcb_render_query_pict_formats_reply_t *formats=xcb_render_query_pict_formats_reply(c,xcb_render_query_pict_formats(c),NULL);
    if (!formats) return 0;
    xcb_render_pictformat_t format=0;
    for (xcb_render_pictforminfo_iterator_t i=xcb_render_query_pict_formats_formats_iterator(formats);i.rem;xcb_render_pictforminfo_next(&i)) {
        const xcb_render_directformat_t *d=&i.data->direct;
        if (i.data->type==XCB_RENDER_PICT_TYPE_DIRECT && i.data->depth==32 &&
            d->red_shift==16 && d->green_shift==8 && d->blue_shift==0 && d->alpha_shift==24 &&
            d->red_mask==255 && d->green_mask==255 && d->blue_mask==255 && d->alpha_mask==255) { format=i.data->id; break; }
    }
    free(formats); if (!format) return 0;
    size_t row=(size_t)im->width*4u;
    uint32_t *pixels=malloc(row*im->height); if (!pixels) return 0;
    bool lsb=xcb_get_setup(c)->image_byte_order==XCB_IMAGE_ORDER_LSB_FIRST;
    for (size_t i=0;i<(size_t)im->width*im->height;i++) {
        uint32_t v=word(im->pixels+i*4u);
        uint8_t *p=(uint8_t *)(pixels+i);
        for (unsigned k=0;k<4;k++) p[k]=(uint8_t)(v>>(8u*(lsb ? k : 3u-k)));
    }
    xcb_pixmap_t pix=xcb_generate_id(c); xcb_gcontext_t gc=0; xcb_render_picture_t picture=0; xcb_cursor_t cursor=0;
    if (!request_ok(c,xcb_create_pixmap_checked(c,32,pix,root,(uint16_t)im->width,(uint16_t)im->height))) { pix=0; goto done; }
    gc=xcb_generate_id(c);
    if (!request_ok(c,xcb_create_gc_checked(c,gc,pix,0,NULL))) { gc=0; goto done; }
    /* (P) request byte limit less fixed PutImage header and maximum padding. */
    size_t max=(size_t)xcb_get_maximum_request_length(c)*4u;
    size_t rows=max>sizeof(xcb_put_image_request_t)+4u ? (max-sizeof(xcb_put_image_request_t)-4u)/row : 0;
    if (!rows) goto done;
    for (uint32_t y=0;y<im->height;) {
        uint32_t h=im->height-y; if (h>rows) h=(uint32_t)rows;
        if (!request_ok(c,xcb_put_image_checked(c,XCB_IMAGE_FORMAT_Z_PIXMAP,pix,gc,(uint16_t)im->width,(uint16_t)h,
            0,(int16_t)y,0,32,(uint32_t)(row*h),(const uint8_t *)pixels+(size_t)y*row))) goto done;
        y+=h;
    }
    picture=xcb_generate_id(c);
    if (!request_ok(c,xcb_render_create_picture_checked(c,picture,pix,format,0,NULL))) { picture=0; goto done; }
    cursor=xcb_generate_id(c);
    if (!request_ok(c,xcb_render_create_cursor_checked(c,cursor,picture,(uint16_t)im->xhot,(uint16_t)im->yhot))) cursor=0;
done:
    if (picture) xcb_render_free_picture(c,picture);
    if (gc) xcb_free_gc(c,gc);
    if (pix) xcb_free_pixmap(c,pix);
    free(pixels); return cursor;
}
