#include <stddef.h>
#include <string.h>

#include "app/app.h"

/* Smoke entry: exits 0 iff every layer's link chain resolved to the v2 app id. */
int main(void)
{
	return app_smoke() == strlen(EDIT_APP_ID) ? 0 : 1;
}
