/* check-layers L4: linked --whole-archive against lower layers only. */
#include <stdio.h>
#include <string.h>

#include "viewport/viewport.h"

int main(void)
{
	size_t want = strlen(EDIT_APP_ID);
	size_t got = viewport_smoke();
	if (got != want) {
		fprintf(stderr, "layer_L4_test: viewport_smoke() returned %zu, want %zu\n", got, want);
		return 1;
	}
	return 0;
}
