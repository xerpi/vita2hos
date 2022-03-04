#include <stdbool.h>
#include <stdlib.h>
#include <switch.h>
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>
#include "SceKernelThreadMgr.h"
#include "SceLibKernel.h"
#include "SceSysmem.h"
#include "module.h"
#include "protected_bitset.h"
#include "utils.h"
#include "log.h"

#define SCE_KERNEL_HIGHEST_PRIORITY_USER		64
#define SCE_KERNEL_LOWEST_PRIORITY_USER			191
#define SCE_KERNEL_DEFAULT_PRIORITY			((SceInt32)0x10000100)
#define SCE_KERNEL_DEFAULT_PRIORITY_GAME_APP		160
#define SCE_KERNEL_DEFAULT_PRIORITY_USER		SCE_KERNEL_DEFAULT_PRIORITY
#define SCE_KERNEL_THREAD_STACK_SIZE_DEFAULT_USER_MAIN	(256 * 1024)

#define HOS_HIGHEST_PRIORITY	28
#define HOS_LOWEST_PRIORITY	59

#define MAX_THREADS	32
#define KERNEL_TLS_SIZE 0x800

static s32 g_vita_thread_info_tls_slot_id;

DECL_PROTECTED_BITSET(VitaThreadInfo, vita_thread_infos, MAX_THREADS)
DECL_PROTECTED_BITSET_ALLOC(thread_info_alloc, vita_thread_infos, VitaThreadInfo)
DECL_PROTECTED_BITSET_RELEASE(thread_info_release, vita_thread_infos, VitaThreadInfo)
DECL_PROTECTED_BITSET_GET_FOR_UID(get_thread_info_for_uid, vita_thread_infos, VitaThreadInfo)

static inline int vita_priority_to_hos_priority(int priority)
{
	if ((priority & SCE_KERNEL_DEFAULT_PRIORITY) == SCE_KERNEL_DEFAULT_PRIORITY)
		priority = SCE_KERNEL_DEFAULT_PRIORITY_GAME_APP + (priority & ~SCE_KERNEL_DEFAULT_PRIORITY);

	if ((priority < SCE_KERNEL_HIGHEST_PRIORITY_USER) || (priority > SCE_KERNEL_LOWEST_PRIORITY_USER))
		return SCE_KERNEL_ERROR_ILLEGAL_PRIORITY;

	return HOS_HIGHEST_PRIORITY +
	       ((priority - SCE_KERNEL_HIGHEST_PRIORITY_USER) * (HOS_LOWEST_PRIORITY - HOS_HIGHEST_PRIORITY)) /
	       (SCE_KERNEL_LOWEST_PRIORITY_USER - SCE_KERNEL_HIGHEST_PRIORITY_USER);
}

static void NORETURN thread_entry_wrapper(void *arg)
{
	VitaThreadInfo *ti = arg;
	int ret;

	threadTlsSet(g_vita_thread_info_tls_slot_id, ti);

	ret = ti->entry(ti->arglen, ti->argp);

	LOG("Thread 0x%x returned with: 0x%x", ti->uid, ret);

	threadExit();
}

static SceUID create_thread(const char *name, SceKernelThreadEntry entry, int initPriority, SceSize stackSize)
{
	VitaThreadInfo *ti;
	Result res;
	int priority = vita_priority_to_hos_priority(initPriority);

	if (priority < 0)
		return priority;

	ti = thread_info_alloc();
	if (!ti) {
		LOG("Could not allocate thread info for thread \"%s\"", name);
		return SCE_KERNEL_ERROR_NO_MEMORY;
	}

	memset(ti, 0, sizeof(*ti));
	ti->uid = SceSysmem_get_next_uid();
	strncpy(ti->name, name, sizeof(ti->name) - 1);
	ti->entry = entry;
	ti->vita_tls = malloc(KERNEL_TLS_SIZE);

	res = threadCreate(&ti->thread, thread_entry_wrapper, ti, NULL, stackSize, priority, -2);
	if (R_FAILED(res)) {
		LOG("Error creating thread: 0x%lx", res);
		free(ti->vita_tls);
		return SCE_KERNEL_ERROR_THREAD_ERROR;
	}

	LOG("Created thread \"%s\", thid: 0x%x", name, ti->uid);

	return ti->uid;
}

static int start_thread(SceUID thid, SceSize arglen, void *argp)
{
	VitaThreadInfo *ti = get_thread_info_for_uid(thid);
	Result res;

	if (!ti)
		return SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID;

	if (arglen && argp) {
		ti->argp = malloc(arglen);
		if (!ti->argp)
			return SCE_KERNEL_ERROR_NO_MEMORY;

		memcpy(ti->argp, argp, arglen);
		ti->arglen = arglen;
	}

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
	return create_thread(name, entry, initPriority, stackSize);
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

	free(ti->argp);
	free(ti->vita_tls);
	thread_info_release(ti);

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
	UEvent *process_exit_event;
	int ret;
	Result res;

	thid = create_thread("<main>", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER,
			     SCE_KERNEL_THREAD_STACK_SIZE_DEFAULT_USER_MAIN);
	if (thid < 0)
		return thid;

	ret = start_thread(thid, args, argp);
	if (ret < 0) {
		sceKernelDeleteThread(thid);
		return SCE_KERNEL_ERROR_THREAD_ERROR;
	}

	process_exit_event = SceLibKernel_get_process_exit_uevent();

	res = waitSingle(waiterForUEvent(process_exit_event), -1);
	if (R_FAILED(res)) {
		LOG("Error waiting for the process to finish: 0x%lx", res);
		return -1;
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

void SceKernelThreadMgr_register(void)
{
	static const export_entry_t exports[] = {
		{0xC5C11EE7, sceKernelCreateThread},
		{0x1BBDE3D9, sceKernelDeleteThread},
		{0xF08DE149, sceKernelStartThread},
		{0x0C8A38E1, sceKernelExitThread},
		{0x1D17DECF, sceKernelExitDeleteThread},
		{0xDDB395A9, sceKernelWaitThreadEnd},
		{0x4B675D05, sceKernelDelayThread},
	};

	module_register_exports(exports, ARRAY_SIZE(exports));
}

int SceKernelThreadMgr_init(void)
{
	g_vita_thread_info_tls_slot_id = threadTlsAlloc(NULL);

	return 0;
}

int SceKernelThreadMgr_finish(void)
{
	threadTlsFree(g_vita_thread_info_tls_slot_id);

	return 0;
}

VitaThreadInfo *SceKernelThreadMgr_get_thread_info(void)
{
	return threadTlsGet(g_vita_thread_info_tls_slot_id);
}
