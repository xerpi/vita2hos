#include <stdbool.h>
#include <stdlib.h>
#include <switch.h>
#include <psp2/kernel/threadmgr.h>
#include "utils.h"
#include "log.h"

int sceKernelDelayThread(SceUInt delay)
{
	svcSleepThread((s64)delay * 1000);

	return 0;
}

