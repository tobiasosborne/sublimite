#include <stdio.h>
#include <string.h>

#include "os/os.h"

/* GCC and Clang fold literal __builtin_strcmp in a C11 static assertion. */
_Static_assert(sizeof ("" EDIT_APP_ID) == sizeof "sublimite-v2" &&
	       __builtin_strcmp(EDIT_APP_ID, "sublimite-v2") == 0,
	       "EDIT_APP_ID must be sublimite-v2");

int main(void)
{
	if (strcmp(os_app_id(), "sublimite-v2") != 0) {
		fputs("isolation_test: os_app_id() must return sublimite-v2\n", stderr);
		return 1;
	}
	return 0;
}
