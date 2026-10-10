/* Same production kernels, with the work poll redirected to a caller-owned
 * CPU meter. No alternate algorithm and no public/header instrumentation. */
#define work_should_stop find_bench_should_stop
#define scratch find_bench_scratch
#define find_lit_init find_bench_lit_init
#define find_lit_seek find_bench_lit_seek
#define find_literal_mode find_bench_literal_mode
#define find_literal find_bench_literal
#define find_literal_next find_bench_literal_next
#define find_literal_visit find_bench_literal_visit
#define find_regex_bytes find_bench_regex_bytes
#define find_regex_compile find_bench_regex_compile
#define find_regex_prefix find_bench_regex_prefix
#define find_regex_group_count find_bench_regex_group_count
#define find_regex_scratch_bytes find_bench_regex_scratch_bytes
#define find_regex_captures find_bench_regex_captures
#define find_regex_next find_bench_regex_next
#define find_regex_search find_bench_regex_search
#define find_regex_visit find_bench_regex_visit
#define find_regex_work_budget find_bench_regex_work_budget
#define find_regex_next_budget find_bench_regex_next_budget
#include "../src/find/literal.c"
#include "../src/find/find.c"
#undef work_should_stop
#undef scratch
#undef find_lit_init
#undef find_lit_seek
#undef find_literal_mode
#undef find_literal
#undef find_literal_next
#undef find_literal_visit
#undef find_regex_bytes
#undef find_regex_compile
#undef find_regex_prefix
#undef find_regex_group_count
#undef find_regex_scratch_bytes
#undef find_regex_captures
#undef find_regex_next
#undef find_regex_search
#undef find_regex_visit
#undef find_regex_work_budget
#undef find_regex_next_budget
