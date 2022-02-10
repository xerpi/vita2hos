#include <stdbool.h>
#include <stdlib.h>
#include <psp2/kernel/threadmgr.h>
#include "SceKernelThreadMgr.h"
#include "utils.h"
#include "log.h"

void *sceKernelGetTLSAddr(int key)
{
	VitaThreadInfo *ti;

	if (key >= 0 && key <= 0x100) {
		ti = SceKernelThreadMgr_get_thread_info();
		return &ti->vita_tls[key];
	}

	return NULL;
}
