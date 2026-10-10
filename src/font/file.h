/* Internal stream seam for deterministic short-read tests. INIT/worker only.
 * Caller owns/closes fp. Same rollback/length contract as font_load_file. */
#ifndef EDIT_FONT_FILE_H
#define EDIT_FONT_FILE_H
#include <stdio.h>
#include "font/font.h"
unsigned char *font_load_stream(FILE *fp, edit_arena *arena, size_t *len);
#endif
