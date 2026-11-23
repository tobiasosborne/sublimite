/* vendor_ts sits below hl: a grammar calling up into hl is an intra-layer violation. */
void hl_probe_callback(void);

void tree_sitter_probe_external_scanner_destroy(void *payload) { (void)payload; hl_probe_callback(); }
