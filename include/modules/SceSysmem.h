#ifndef SCE_SYSMEM_H
#define SCE_SYSMEM_H

#include <deko3d.h>

typedef struct VitaMemBlockInfo {
	uint32_t index;
	SceUID uid;
	void *base;
	uint32_t size;
	DkMemBlock dk_memblock;
} VitaMemBlockInfo;

void SceSysmem_register(void);
int SceSysmem_init(DkDevice dk_device);
SceUID SceSysmem_get_next_uid(void);
VitaMemBlockInfo *SceSysmem_get_vita_memblock_info_for_addr(const void *addr);
DkMemBlock SceSysmem_get_dk_memblock_for_addr(const void *addr);

#endif
