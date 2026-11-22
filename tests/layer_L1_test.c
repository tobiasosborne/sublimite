/* check-layers L1: linked --whole-archive against lower layers only. */
#include <stdio.h>
#include <string.h>

#include "piece/piece.h"

int main(void)
{
	size_t want = strlen(EDIT_APP_ID);
	size_t got = piece_app_id_len();
	if (got != want) {
		fprintf(stderr, "layer_L1_test: piece_app_id_len() returned %zu, want %zu\n", got, want);
		return 1;
	}
	return 0;
}
