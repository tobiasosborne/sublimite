#include "piece/piece.h"

#include <string.h>

#include "os/os.h"

size_t piece_app_id_len(void)
{
	return strlen(os_app_id());
}
