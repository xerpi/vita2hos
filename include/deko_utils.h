#ifndef DEKO_UTILS_H
#define DEKO_UTILS_H

#include <deko3d.h>
#include <psp2/display.h>
#include <stddef.h>

#include "util.h"

#include "gxm/util.h"

typedef struct {
    uint32_t size;
    uint16_t width;
    uint16_t height;
    DkMemBlock memblock;
    DkImage image;
    DkImageView view;
} dk_surface_t;

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

static inline ptrdiff_t dk_memblock_cpu_addr_offset(const DkMemBlock memblock, const void *cpu_addr)
{
    ptrdiff_t offset = (uintptr_t)cpu_addr - (uintptr_t)dkMemBlockGetCpuAddr(memblock);
    assert(offset >= 0);
    return offset;
}

void dk_surface_create(DkDevice device, dk_surface_t *surface, uint32_t width, uint32_t height,
                       DkImageFormat format, uint32_t flags);
void dk_surface_destroy(dk_surface_t *surface);

void dk_cmdbuf_copy_image(DkCmdBuf cmdbuf, DkImage const *src_image, uint32_t src_width,
                          uint32_t src_height, DkImage const *dst_image, uint32_t dst_width,
                          uint32_t dst_height);

bool dk_image_for_gxm_color_surface(DkDevice device, DkImage *image,
                                    const SceGxmColorSurfaceInner *surface);
bool dk_image_for_gxm_ds_surface(DkDevice device, DkImage *image, uint32_t width, uint32_t height,
                                 const SceGxmDepthStencilSurface *surface);
bool dk_image_for_existing_framebuffer(DkDevice device, DkImage *image, const void *addr,
                                       uint32_t width, uint32_t height, uint32_t stride,
                                       SceDisplayPixelFormat pixelfmt);

#endif