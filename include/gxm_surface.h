#ifndef GXM_SURFACE_H
#define GXM_SURFACE_H

#include <psp2/gxm.h>

#ifdef __cplusplus
extern "C" {
#endif

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
SceGxmColorFormat gxm_get_surface_format(const SceGxmColorSurface *surface);
SceGxmColorSurfaceType gxm_get_surface_type(const SceGxmColorSurface *surface);
bool gxm_is_surface_disabled(const SceGxmColorSurface *surface);
void gxm_get_surface_dimensions(const SceGxmColorSurface *surface,
                              uint32_t *width, uint32_t *height);

#ifdef __cplusplus
}
#endif

#endif // GXM_SURFACE_H
