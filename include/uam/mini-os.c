#include <stdio.h>
#include <stdlib.h>
#include "util/os_misc.h"

void os_log_message(const char *message)
{
	fputs(message, stderr);
}

const char* os_get_option(const char *name)
{
	return NULL;
}
