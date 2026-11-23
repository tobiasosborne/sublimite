extern void os_smoke(void);
extern void trace_leaf(void);
void trace_smoke(void) { os_smoke(); trace_leaf(); }
