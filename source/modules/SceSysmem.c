#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <switch.h>
#include <psp2/kernel/error.h>
#include <psp2/kernel/sysmem.h>
#include "bitset.h"
#include "utils.h"
#include "log.h"

#define MAX_MEMBLOCKS	256

typedef struct {
	uint32_t index;
	SceUID uid;
	void *base;
} VitaMemBlockInfo;

static BITSET_DEFINE(g_vita_memblock_infos_valid, MAX_MEMBLOCKS);
static Mutex g_vita_memblock_infos_mutex;
static VitaMemBlockInfo g_vita_memblock_infos[MAX_MEMBLOCKS];

static _Atomic SceUID g_last_uid = 1;

static VitaMemBlockInfo *memblock_info_alloc(void)
{
	uint32_t index;

	mutexLock(&g_vita_memblock_infos_mutex);
	index = bitset_find_first_clear_and_set(g_vita_memblock_infos_valid);
	mutexUnlock(&g_vita_memblock_infos_mutex);

	if (index == UINT32_MAX)
		return NULL;

	g_vita_memblock_infos[index].index = index;

	return &g_vita_memblock_infos[index];
}

static void memblock_info_release(VitaMemBlockInfo *block)
{
	mutexLock(&g_vita_memblock_infos_mutex);
	BITSET_CLEAR(g_vita_memblock_infos_valid, block->index);
	mutexUnlock(&g_vita_memblock_infos_mutex);
}

static VitaMemBlockInfo *get_memblock_info_for_uid(SceUID uid)
{
	mutexLock(&g_vita_memblock_infos_mutex);
	bitset_for_each_bit_set(g_vita_memblock_infos_valid, index) {
		if (g_vita_memblock_infos[index].uid == uid) {
			mutexUnlock(&g_vita_memblock_infos_mutex);
			return &g_vita_memblock_infos[index];
		}
	}
	mutexUnlock(&g_vita_memblock_infos_mutex);
	return NULL;
}

SceUID SceSysmem_get_next_uid(void)
{
	return atomic_fetch_add(&g_last_uid, 1);
}

SceUID sceKernelAllocMemBlock(const char *name, SceKernelMemBlockType type, SceSize size, SceKernelAllocMemBlockOpt *opt)
{
	VitaMemBlockInfo *block;
	uint32_t alignment;

	LOG("sceKernelAllocMemBlock: name: %s, size: 0x%x", name, size);

	block = memblock_info_alloc();
	if (!block)
		return SCE_KERNEL_ERROR_NO_MEMORY;

	if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW)
		alignment = 256 * 1024;
	else
		alignment = 4 * 1024;

	block->uid = SceSysmem_get_next_uid();
	block->base = aligned_alloc(alignment, size);

	return block->uid;
}

int sceKernelFreeMemBlock(SceUID uid)
{
	VitaMemBlockInfo *block = get_memblock_info_for_uid(uid);
	if (!block)
		return SCE_KERNEL_ERROR_INVALID_UID;

	free(block->base);
	memblock_info_release(block);

	return 0;
}

int sceKernelGetMemBlockBase(SceUID uid, void **base)
{
	VitaMemBlockInfo *block = get_memblock_info_for_uid(uid);
	if (!block)
		return SCE_KERNEL_ERROR_INVALID_UID;

	*base = block->base;

	return 0;
}
