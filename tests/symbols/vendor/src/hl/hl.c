/* hl reaches the vendored runtime and a grammar on the vendor_ts < hl edge. */
void *ts_probe_parser_new(void);
const void *tree_sitter_probe(void);

void *hl_probe_parser(void) { return tree_sitter_probe() ? ts_probe_parser_new() : 0; }
