#include <stdbool.h>
#include <stdlib.h>
#include <psp2/kernel/sysmem.h>
#include "utils.h"
#include "log.h"

static SceUID g_last_uid = 1;

static struct {
	bool valid;
	SceUID uid;
	void *base;
} memblock_table[32];

SceUID SceSysmem_get_next_uid(void)
{
	return g_last_uid++;
}

SceUID sceKernelAllocMemBlock(const char *name, SceKernelMemBlockType type, SceSize size, SceKernelAllocMemBlockOpt *opt)
{
	uint32_t alignment;

	LOG("sceKernelAllocMemBlock: name: \"%s\", size: 0x%x", name, size);

	if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW)
		alignment = 256 * 1024;
	else
		alignment = 4 * 1024;

	for (int i = 0; i < ARRAY_SIZE(memblock_table); i++) {
		if (!memblock_table[i].valid) {
			memblock_table[i].uid = SceSysmem_get_next_uid();
			memblock_table[i].base = aligned_alloc(alignment, size);
			memblock_table[i].valid = true;
			return memblock_table[i].uid;
		}
	}

	return -1;
}

int sceKernelGetMemBlockBase(SceUID uid, void **base)
{
	LOG("sceKernelGetMemBlockBase: uid: 0x%x", uid);

	for (int i = 0; i < ARRAY_SIZE(memblock_table); i++) {
		if (memblock_table[i].valid && memblock_table[i].uid == uid) {
			*base = memblock_table[i].base;
			return 0;
		}
	}

	return -1;
}
