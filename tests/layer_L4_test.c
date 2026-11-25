/* check-layers L4: linked --whole-archive against lower layers only. */
#include <stdio.h>

#include "viewport/viewport.h"

int main(void)
{
	if (!viewport_wrap_default("README") || viewport_wrap_default("main.c")) {
		fputs("layer_L4_test: wrong wrap default\n", stderr);
		return 1;
	}
	return 0;
}
