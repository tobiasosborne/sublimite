/* check-layers L0: linked --whole-archive against lower layers only. */
#include <stdio.h>
#include <string.h>

#include "os/os.h"

int main(void)
{
	size_t want = strlen(EDIT_APP_ID);
	size_t got = strlen(os_app_id());
	if (got != want) {
		fprintf(stderr, "layer_L0_test: os_app_id() returned %zu, want %zu\n", got, want);
		return 1;
	}
	return 0;
}
