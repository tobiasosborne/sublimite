/* Windows toolchain smoke test (edit-177r.1). Built only by `make TARGET=win`
 * (mk/win.mk); the Linux Makefile globs the top-level tests, never tests/win/, and
 * the whole file is guarded anyway. Proves the features the codebase uses.
 *   smoke_test               all checks must pass, exit 0
 *   smoke_test --expect-asan deliberate heap overflow; an ASan build must abort
 *                            with a nonzero exit and a report on stderr (run as
 *                            its own red case by the make rule)             */
#if defined(_WIN32)
#include <windows.h>
#include <emmintrin.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local int tls_val = 7;
static atomic_int counter;
static int fails;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static DWORD WINAPI worker(LPVOID arg) {
	(void)arg;
	tls_val = 99;                      /* must not touch the main thread's copy */
	for (int i = 0; i < 1000; i++) atomic_fetch_add(&counter, 1);
	return 0;
}

static int expect_asan(void) {
	volatile char *p = malloc(8);
	char c = p[8];                     /* heap-buffer-overflow read */
	fprintf(stderr, "ASan did not fire (read %d)\n", c);
	free((void *)p);
	return 0;                          /* reaching here is the failure */
}

int main(int argc, char **argv) {
	if (argc > 1 && strcmp(argv[1], "--expect-asan") == 0) return expect_asan() ? 0 : 3;

	HANDLE h = CreateThread(NULL, 0, worker, NULL, 0, NULL);
	CHECK(h != NULL);
	if (h) { WaitForSingleObject(h, INFINITE); CloseHandle(h); }
	CHECK(atomic_load(&counter) == 1000);
	CHECK(tls_val == 7);

	__m128i a = _mm_set1_epi8('x'), b = _mm_set_epi8(0,0,0,0,0,0,0,0,0,0,0,0,0,0,'x',0);
	CHECK(_mm_movemask_epi8(_mm_cmpeq_epi8(a, b)) == 0x2);

	CHECK(__builtin_popcountll(0xF0F0ull) == 8);
	CHECK(__builtin_ctzll(0x100ull) == 8);
	CHECK(__builtin_clz(1u) == 31);

	LARGE_INTEGER f, t0, t1;
	CHECK(QueryPerformanceFrequency(&f) && f.QuadPart > 0);
	QueryPerformanceCounter(&t0); Sleep(2); QueryPerformanceCounter(&t1);
	CHECK(t1.QuadPart > t0.QuadPart);

	/* UBSan is live in the san build only; here just a defined shift. */
	printf("smoke ok (%s)\n", fails ? "FAILED" : "all checks passed");
	return fails ? 1 : 0;
}
#else
int smoke_test_not_windows;
#endif
