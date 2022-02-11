#include <stdbool.h>
#include <stdlib.h>
#include <psp2/kernel/threadmgr.h>
#include <switch.h>
#include "SceKernelThreadMgr.h"
#include "SceSysmem.h"
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

int sceKernelExitProcess(int res)
{
	LOG("sceKernelExitProcess called! Return value %d", res);

	svcExitProcess();

	return 0;
}
