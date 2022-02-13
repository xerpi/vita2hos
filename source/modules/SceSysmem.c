#include <stdbool.h>
#include <stdlib.h>
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
static VitaMemBlockInfo g_vita_memblock_infos[MAX_MEMBLOCKS];
static SceUID g_last_uid = 1;

SceUID SceSysmem_get_next_uid(void)
{
	return g_last_uid++;
}

SceUID sceKernelAllocMemBlock(const char *name, SceKernelMemBlockType type, SceSize size, SceKernelAllocMemBlockOpt *opt)
{
	uint32_t index, alignment;

	LOG("sceKernelAllocMemBlock: name: %s, size: 0x%x", name, size);

	index = bitset_find_first_clear_and_set(g_vita_memblock_infos_valid);
	if (index == UINT32_MAX)
		return SCE_KERNEL_ERROR_NO_MEMORY;

	if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW)
		alignment = 256 * 1024;
	else
		alignment = 4 * 1024;

	g_vita_memblock_infos[index].uid = SceSysmem_get_next_uid();
	g_vita_memblock_infos[index].base = aligned_alloc(alignment, size);

	return g_vita_memblock_infos[index].uid;
}

int sceKernelGetMemBlockBase(SceUID uid, void **base)
{
	bitset_for_each_bit_set(g_vita_memblock_infos_valid, index) {
		if (g_vita_memblock_infos[index].uid == uid) {
			*base = g_vita_memblock_infos[index].base;
			return 0;
		}
	}

	return SCE_KERNEL_ERROR_INVALID_UID;
}
