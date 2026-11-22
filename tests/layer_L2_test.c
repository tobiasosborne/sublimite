/* check-layers L2: linked --whole-archive against lower layers only. */
#include <stdio.h>
#include <string.h>

#include "edit/edit.h"

int main(void)
{
	size_t want = strlen(EDIT_APP_ID);
	size_t got = edit_smoke();
	if (got != want) {
		fprintf(stderr, "layer_L2_test: edit_smoke() returned %zu, want %zu\n", got, want);
		return 1;
	}
	return 0;
}
