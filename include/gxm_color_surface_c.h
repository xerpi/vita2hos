#ifndef GXM_COLOR_SURFACE_C_H
#define GXM_COLOR_SURFACE_C_H

#include <psp2/gxm.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for Vulkan types
typedef uint32_t VkFormat;
typedef uint32_t VkImageTiling;

// C wrapper functions for color surface operations
VkFormat gxm_translate_color_format(SceGxmColorFormat format);
VkImageTiling gxm_translate_surface_type(SceGxmColorSurfaceType type);
uint32_t gxm_get_bytes_per_pixel(SceGxmColorFormat format);

int gxm_init_color_surface(SceGxmColorSurface *surface,
                          SceGxmColorFormat colorFormat,
                          SceGxmColorSurfaceType surfaceType,
                          SceGxmColorSurfaceScaleMode scaleMode,
                          SceGxmOutputRegisterSize outputRegisterSize,
                          uint32_t width,
                          uint32_t height,
                          uint32_t strideInPixels,
                          void *data);

void* gxm_get_surface_data(const SceGxmColorSurface *surface);
int gxm_set_surface_data(SceGxmColorSurface *surface, void *data);
SceGxmColorFormat gxm_get_surface_format(const SceGxmColorSurface *surface);
int gxm_set_surface_format(SceGxmColorSurface *surface, SceGxmColorFormat format);
SceGxmColorSurfaceType gxm_get_surface_type(const SceGxmColorSurface *surface);
uint32_t gxm_get_stride_in_pixels(const SceGxmColorSurface *surface);
uint32_t gxm_get_stride_in_bytes(const SceGxmColorSurface *surface);
bool gxm_is_surface_disabled(const SceGxmColorSurface *surface);
void gxm_set_surface_disabled(SceGxmColorSurface *surface, bool disabled);
void gxm_get_surface_dimensions(const SceGxmColorSurface *surface,
                              uint32_t *width, uint32_t *height);

#ifdef __cplusplus
}
#endif

#endif // GXM_COLOR_SURFACE_C_H
