#include <stdbool.h>
#include <stdlib.h>
#include <psp2/kernel/threadmgr.h>
#include <switch.h>
#include "SceKernelThreadMgr.h"
#include "SceSysmem.h"
#include "utils.h"
#include "log.h"

static UEvent *g_process_exit_event_ptr;
static int *g_process_exit_res_ptr;

int SceLibKernel_init(UEvent *process_exit_event_ptr, int *process_exit_res_ptr)
{
	g_process_exit_event_ptr = process_exit_event_ptr;
	g_process_exit_res_ptr = process_exit_res_ptr;
	return 0;
}

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

	*g_process_exit_res_ptr = res;
	ueventSignal(g_process_exit_event_ptr);

	threadExit();

	return 0;
}
