#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <switch.h>
#include <psp2/kernel/error.h>
#include <psp2/kernel/sysmem.h>
#include "SceSysmem.h"
#include "module.h"
#include "protected_bitset.h"
#include "utils.h"
#include "log.h"

#define MAX_MEMBLOCKS	256

typedef struct {
	uint32_t index;
	SceUID uid;
	void *base;
} VitaMemBlockInfo;

static _Atomic SceUID g_last_uid = 1;

DECL_PROTECTED_BITSET(VitaMemBlockInfo, vita_memblock_infos, MAX_MEMBLOCKS)
DECL_PROTECTED_BITSET_ALLOC(memblock_info_alloc, vita_memblock_infos, VitaMemBlockInfo)
DECL_PROTECTED_BITSET_RELEASE(memblock_info_release, vita_memblock_infos, VitaMemBlockInfo)
DECL_PROTECTED_BITSET_GET_FOR_UID(get_memblock_info_for_uid, vita_memblock_infos, VitaMemBlockInfo)

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

void SceSysmem_register(void)
{
	static const export_entry_t exports[] = {
		{0xB9D5EBDE, sceKernelAllocMemBlock},
		{0xA91E15EE, sceKernelFreeMemBlock},
		{0xB8EF5818, sceKernelGetMemBlockBase},
	};

	module_register_exports(exports, ARRAY_SIZE(exports));
}

int SceSysmem_init(void)
{
	return 0;
}

SceUID SceSysmem_get_next_uid(void)
{
	return atomic_fetch_add(&g_last_uid, 1);
}
