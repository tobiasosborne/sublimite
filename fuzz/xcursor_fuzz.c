#include "x11/xcursor.h"
/* Decode only: fuzz bytes never become filesystem paths or server requests. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    xcursor_image image;
    uint32_t wanted=size ? (uint32_t)data[0]+1u : 24u;
    if (xcursor_decode(data,size,wanted,&image)) {
        volatile uint8_t first=image.pixels[0];
        volatile uint8_t last=image.pixels[(size_t)image.width*image.height*4u-1u];
        (void)first; (void)last;
    }
    char theme[128]; uint32_t pixels;
    xcursor_settings((const char *)data,size,NULL,NULL,theme,sizeof theme,&pixels);
    return 0;
}
