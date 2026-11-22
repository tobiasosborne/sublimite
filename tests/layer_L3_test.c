/* check-layers L3: linked --whole-archive against lower layers only. */
#include <stdio.h>
#include <string.h>

#include "render/render.h"

int main(void)
{
	size_t want = strlen(EDIT_APP_ID);
	size_t got = render_smoke();
	if (got != want) {
		fprintf(stderr, "layer_L3_test: render_smoke() returned %zu, want %zu\n", got, want);
		return 1;
	}
	return 0;
}
