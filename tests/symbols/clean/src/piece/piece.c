#include <string.h>

extern void trace_smoke(void);
size_t piece_smoke(const char *text) { trace_smoke(); return strlen(text); }
