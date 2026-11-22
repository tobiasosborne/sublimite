#include "os/os.h"

#ifndef EDIT_APP_ID
#error "EDIT_APP_ID must be defined by the build (Makefile APP_ID)"
#endif

const char *os_app_id(void)
{
	return EDIT_APP_ID;
}
