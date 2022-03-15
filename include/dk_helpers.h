#ifndef DK_HELPERS_H
#define DK_HELPERS_H

#include <deko3d.h>
#include "util.h"

static inline DkMemBlock dk_alloc_memblock(DkDevice device, uint32_t size, uint32_t flags)
{
	DkMemBlockMaker memblock_maker;
	dkMemBlockMakerDefaults(&memblock_maker, device, ALIGN(size, DK_MEMBLOCK_ALIGNMENT));
	memblock_maker.flags = flags;
	return dkMemBlockCreate(&memblock_maker);
}

static inline DkMemBlock dk_map_memblock(DkDevice device, void *addr, uint32_t size, uint32_t flags)
{
	DkMemBlockMaker memblock_maker;
	dkMemBlockMakerDefaults(&memblock_maker, device, ALIGN(size, DK_MEMBLOCK_ALIGNMENT));
	memblock_maker.flags = flags;
        memblock_maker.storage = addr;
	return dkMemBlockCreate(&memblock_maker);
}

#endif