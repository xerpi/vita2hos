#include <deko3d.h>
#include <m-dict.h>
#include <psp2/kernel/error.h>
#include <psp2/kernel/sysmem.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <switch.h>

#include "modules/SceSysmem.h"
#include "dk_helpers.h"
#include "log.h"
#include "module.h"
#include "util.h"

static _Atomic SceUID g_last_uid = 1;
static DkDevice g_dk_device;

DICT_DEF2(vita_memblock_info_dict, SceUID, M_DEFAULT_OPLIST, VitaMemBlockInfo *, M_POD_OPLIST)
static vita_memblock_info_dict_t g_vita_memblock_infos;
static RwLock g_vita_memblock_infos_lock;

static VitaMemBlockInfo *get_memblock_info_for_uid(SceUID uid)
{
    VitaMemBlockInfo *block;

    rwlockReadLock(&g_vita_memblock_infos_lock);
    block = *vita_memblock_info_dict_get(g_vita_memblock_infos, uid);
    rwlockReadUnlock(&g_vita_memblock_infos_lock);

    return block;
}

EXPORT(SceSysmem, 0xB9D5EBDE, SceUID, sceKernelAllocMemBlock, const char *name,
       SceKernelMemBlockType type, SceSize size, SceKernelAllocMemBlockOpt *opt)
{
    VitaMemBlockInfo *block;
    uint32_t alignment;
    void *base;
    uint32_t memblock_flags;

    LOG("sceKernelAllocMemBlock: name: %s, size: 0x%x", name, size);

    block = malloc(sizeof(*block));
    if (!block)
        return SCE_KERNEL_ERROR_NO_MEMORY;

    if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW)
        alignment = 256 * 1024;
    else
        alignment = 4 * 1024;

    base = aligned_alloc(alignment, size);
    if (!base) {
        free(block);
        return SCE_KERNEL_ERROR_NO_MEMORY;
    }

    memset(block, 0, sizeof(*block));
    block->uid  = SceSysmem_get_next_uid();
    block->base = base;
    block->size = size;

    switch (type) {
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_RW:
        memblock_flags = DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuCached;
        break;
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE:
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW:
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW:
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW:
    default:
        memblock_flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
        break;
    }

    /* We also map all the allocated memory blocks to the GPU */
    block->dk_memblock = dk_map_memblock(g_dk_device, block->base, block->size,
                                         memblock_flags | DkMemBlockFlags_Image);

    rwlockWriteLock(&g_vita_memblock_infos_lock);
    vita_memblock_info_dict_set_at(g_vita_memblock_infos, block->uid, block);
    rwlockWriteUnlock(&g_vita_memblock_infos_lock);

    return block->uid;
}

EXPORT(SceSysmem, 0xA91E15EE, int, sceKernelFreeMemBlock, SceUID uid)
{
    VitaMemBlockInfo *block = get_memblock_info_for_uid(uid);

    if (!block)
        return SCE_KERNEL_ERROR_INVALID_UID;

    dkMemBlockDestroy(block->dk_memblock);
    free(block->base);
    rwlockWriteLock(&g_vita_memblock_infos_lock);
    vita_memblock_info_dict_erase(g_vita_memblock_infos, uid);
    rwlockWriteUnlock(&g_vita_memblock_infos_lock);

    return 0;
}

EXPORT(SceSysmem, 0xB8EF5818, int, sceKernelGetMemBlockBase, SceUID uid, void **base)
{
    VitaMemBlockInfo *block = get_memblock_info_for_uid(uid);

    if (!block)
        return SCE_KERNEL_ERROR_INVALID_UID;

    if (!base)
        return SCE_KERNEL_ERROR_ILLEGAL_ADDR;

    *base = block->base;

    return 0;
}

DECLARE_LIBRARY(SceSysmem, 0x37fe725a);

int SceSysmem_init(DkDevice dk_device)
{
    g_dk_device = dk_device;
    vita_memblock_info_dict_init(g_vita_memblock_infos);
    rwlockInit(&g_vita_memblock_infos_lock);

    return 0;
}

SceUID SceSysmem_get_next_uid(void)
{
    return atomic_fetch_add(&g_last_uid, 1);
}

VitaMemBlockInfo *SceSysmem_get_vita_memblock_info_for_addr(const void *addr)
{
    VitaMemBlockInfo *block = NULL;
    vita_memblock_info_dict_it_t it;
    struct vita_memblock_info_dict_pair_s *pair;

    rwlockReadLock(&g_vita_memblock_infos_lock);
    for (vita_memblock_info_dict_it(it, g_vita_memblock_infos); !vita_memblock_info_dict_end_p(it);
         vita_memblock_info_dict_next(it)) {
        pair = vita_memblock_info_dict_ref(it);
        if (addr >= pair->value->base && addr < (pair->value->base + pair->value->size)) {
            block = pair->value;
            break;
        }
    }
    rwlockReadUnlock(&g_vita_memblock_infos_lock);

    return block;
}

DkMemBlock SceSysmem_get_dk_memblock_for_addr(const void *addr)
{
    VitaMemBlockInfo *info = SceSysmem_get_vita_memblock_info_for_addr(addr);
    if (!info)
        return NULL;
    return info->dk_memblock;
}
