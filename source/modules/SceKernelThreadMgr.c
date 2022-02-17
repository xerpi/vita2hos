#include <stdbool.h>
#include <stdlib.h>
#include <switch.h>
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>
#include "SceKernelThreadMgr.h"
#include "SceLibKernel.h"
#include "SceSysmem.h"
#include "bitset.h"
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

static BITSET_DEFINE(g_vita_thread_infos_valid, MAX_THREADS);
static Mutex g_vita_thread_infos_mutex;
static VitaThreadInfo g_vita_thread_infos[MAX_THREADS];

static s32 g_vita_thread_info_tls_slot_id;

static VitaThreadInfo *thread_info_alloc(void)
{
	uint32_t index;

	mutexLock(&g_vita_thread_infos_mutex);
	index = bitset_find_first_clear_and_set(g_vita_thread_infos_valid);
	mutexUnlock(&g_vita_thread_infos_mutex);

	if (index == UINT32_MAX)
		return NULL;

	g_vita_thread_infos[index].index = index;

	return &g_vita_thread_infos[index];
}

static void thread_info_release(VitaThreadInfo *thread)
{
	mutexLock(&g_vita_thread_infos_mutex);
	BITSET_CLEAR(g_vita_thread_infos_valid, thread->index);
	mutexUnlock(&g_vita_thread_infos_mutex);
}

static VitaThreadInfo *get_thread_info_for_uid(SceUID thid)
{
	mutexLock(&g_vita_thread_infos_mutex);
	bitset_for_each_bit_set(g_vita_thread_infos_valid, index) {
		if (g_vita_thread_infos[index].thid == thid) {
			mutexUnlock(&g_vita_thread_infos_mutex);
			return &g_vita_thread_infos[index];
		}
	}
	mutexUnlock(&g_vita_thread_infos_mutex);
	return NULL;
}

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

static void NORETURN thread_entry_wrapper(void *arg)
{
	VitaThreadInfo *ti = arg;
	int ret;

	threadTlsSet(g_vita_thread_info_tls_slot_id, ti);

	ret = ti->entry(ti->arglen, ti->argp);

	LOG("Thread 0x%x returned with: 0x%x", ti->thid, ret);

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
	ti->thid = SceSysmem_get_next_uid();
	strncpy(ti->name, name, sizeof(ti->name) - 1);
	ti->entry = entry;
	ti->vita_tls = malloc(KERNEL_TLS_SIZE);

	res = threadCreate(&ti->thread, thread_entry_wrapper, ti, NULL, stackSize, priority, -2);
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
