#include <stdbool.h>
#include <stdlib.h>
#include <switch.h>
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>
#include "SceKernelThreadMgr.h"
#include "SceSysmem.h"
#include "utils.h"
#include "log.h"

#define SCE_KERNEL_STACK_SIZE_USER_MAIN		(256 * 1024)
#define SCE_KERNEL_STACK_SIZE_USER_DEFAULT	(4 * 1024)

#define MAX_THREADS	32
#define KERNEL_TLS_SIZE 0x800

static VitaThreadInfo g_vita_thread_infos[MAX_THREADS];
static bool g_vita_thread_infos_valid[MAX_THREADS];
static s32 g_vita_thread_info_tls_slot_id;

static VitaThreadInfo *thread_info_alloc(void)
{
	for (int i = 0; i < ARRAY_SIZE(g_vita_thread_infos); i++) {
		if (!g_vita_thread_infos_valid[i]) {
			g_vita_thread_infos_valid[i] = true;
			return &g_vita_thread_infos[i];
		}
	}

	return NULL;
}

static VitaThreadInfo *get_thread_info_for_uid(SceUID thid)
{
	for (int i = 0; i < ARRAY_SIZE(g_vita_thread_infos); i++) {
		if (g_vita_thread_infos_valid[i] && g_vita_thread_infos[i].thid == thid) {
			return &g_vita_thread_infos[i];
		}
	}

	return NULL;
}

int SceKernelThreadMgr_init(void)
{
	g_vita_thread_info_tls_slot_id = threadTlsAlloc(NULL);

	return 0;
}

VitaThreadInfo *SceKernelThreadMgr_get_thread_info(void)
{
	return threadTlsGet(g_vita_thread_info_tls_slot_id);
}

static void NORETURN thread_entry_wrapper(void *arg)
{
	VitaThreadInfo *ti = arg;
	int ret;

	threadTlsSet(g_vita_thread_info_tls_slot_id, ti);

	ret = ti->entry(ti->args, ti->argp);

	LOG("Thread 0x%x returned with: 0x%x", ti->thid, ret);

	threadExit();
}

static SceUID create_thread(const char *name, SceKernelThreadEntry entry, SceSize stackSize)
{
	VitaThreadInfo *ti;
	Result res;

	ti = thread_info_alloc();
	if (!ti) {
		LOG("Could not allocate thread info for thread \"%s\"", name);
		return SCE_KERNEL_ERROR_NO_MEMORY;
	}

	memset(ti, 0, sizeof(*ti));
	ti->thid = SceSysmem_get_next_uid();
	strncpy(ti->name, name, sizeof(ti->name));
	ti->entry = entry;
	ti->vita_tls = malloc(KERNEL_TLS_SIZE);

	res = threadCreate(&ti->thread, thread_entry_wrapper, ti, NULL, stackSize, 0x3B, -2);
	if (R_FAILED(res)) {
		LOG("Error creating thread: 0x%lx", res);
		free(ti->vita_tls);
		return SCE_KERNEL_ERROR_THREAD_ERROR;
	}

	LOG("Created thread \"%s\", thid: 0x%x", name, ti->thid);

	return ti->thid;
}

static int start_thread(SceUID thid, SceSize arglen, void *argp)
{
	VitaThreadInfo *ti = get_thread_info_for_uid(thid);
	Result res;

	if (!ti)
		return SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID;

	ti->args = arglen;
	ti->argp = argp;

	res = threadStart(&ti->thread);
	if (R_FAILED(res)) {
		LOG("Error starting thread 0x%x: 0x%lx", thid, res);
		return SCE_KERNEL_ERROR_THREAD_ERROR;
	}

	return 0;
}

SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int initPriority,
			     SceSize stackSize, SceUInt attr, int cpuAffinityMask,
			     const SceKernelThreadOptParam *option)
{
	return create_thread(name, entry, stackSize);
}

int sceKernelDeleteThread(SceUID thid)
{
	VitaThreadInfo *ti = get_thread_info_for_uid(thid);
	Result res;

	if (!ti)
		return SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID;

	res = threadClose(&ti->thread);
	if (R_FAILED(res)) {
		LOG("Error closing thread 0x%x exit: 0x%lx", thid, res);
		return SCE_KERNEL_ERROR_THREAD_ERROR;
	}

	free(ti->vita_tls);
	// put(ti)

	return 0;
}

int sceKernelStartThread(SceUID thid, SceSize arglen, void *argp)
{
	return start_thread(thid, arglen, argp);
}

int NORETURN sceKernelExitThread(int status)
{
	VitaThreadInfo *ti = SceKernelThreadMgr_get_thread_info();

	ti->return_status = status;
	threadExit();
}

int NORETURN sceKernelExitDeleteThread(int status)
{
	sceKernelExitThread(status);
	// TODO: Delete
}

int sceKernelWaitThreadEnd(SceUID thid, int *stat, SceUInt *timeout)
{
	VitaThreadInfo *ti = get_thread_info_for_uid(thid);
	uint64_t ns;
	Result res;

	if (!ti)
		return SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID;

	if (!timeout)
		ns = UINT64_MAX;
	else
		ns = *timeout * 1000ull;

	res = waitSingle(waiterForThread(&ti->thread), ns);
	if (R_FAILED(res)) {
		LOG("Error waiting for thread 0x%x exit: 0x%lx", thid, res);
		return SCE_KERNEL_ERROR_THREAD_ERROR;
	}

	if (stat)
		*stat = ti->return_status;

	return 0;
}

int SceKernelThreadMgr_main_entry(SceKernelThreadEntry entry, int args, void *argp)
{
	SceUID thid;
	int ret;

	thid = create_thread("<main>", entry, SCE_KERNEL_STACK_SIZE_USER_MAIN);
	if (thid < 0)
		return thid;

	ret = start_thread(thid, args, argp);
	if (ret < 0) {
		sceKernelDeleteThread(thid);
		return SCE_KERNEL_ERROR_THREAD_ERROR;
	}

	// TODO: Also wait for all threads to finish?
	sceKernelWaitThreadEnd(thid, NULL, NULL);
	sceKernelDeleteThread(thid);

	return 0;
}

int sceKernelDelayThread(SceUInt delay)
{
	svcSleepThread((s64)delay * 1000);
	return 0;
}
