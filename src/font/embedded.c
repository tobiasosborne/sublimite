#include "font/font.h"
#include "font/embedded.h"

const unsigned char *font_embedded_data(size_t *len)
{
    if (len) *len = sizeof font_embedded - 1u;
    return font_embedded;
}
