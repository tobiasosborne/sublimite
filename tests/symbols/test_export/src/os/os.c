/* A shipped archive must not define a test entry point (tools/check_symbols.py [test]). */
int os_test_probe(void);
int os_test_probe(void) { return 0; }
