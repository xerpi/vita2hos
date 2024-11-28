#include <psp2/gxm.h>
#include <uam/gxm/color_surface.h>

extern "C" {

int gxm_init_color_surface(SceGxmColorSurface *surface,
                          SceGxmColorFormat colorFormat,
                          SceGxmColorSurfaceType surfaceType,
                          SceGxmColorSurfaceScaleMode scaleMode,
                          SceGxmOutputRegisterSize outputRegisterSize,
                          uint32_t width,
                          uint32_t height,
                          uint32_t strideInPixels,
                          void *data) {
    return uam::gxm::init_color_surface(surface, colorFormat, surfaceType, scaleMode,
                                    outputRegisterSize, width, height, strideInPixels, data);
}

void* gxm_get_surface_data(const SceGxmColorSurface *surface) {
    return uam::gxm::get_surface_data(surface);
}

SceGxmColorFormat gxm_get_surface_format(const SceGxmColorSurface *surface) {
    return uam::gxm::get_surface_format(surface);
}

SceGxmColorSurfaceType gxm_get_surface_type(const SceGxmColorSurface *surface) {
    return uam::gxm::get_surface_type(surface);
}

bool gxm_is_surface_disabled(const SceGxmColorSurface *surface) {
    return uam::gxm::is_surface_disabled(surface);
}

void gxm_get_surface_dimensions(const SceGxmColorSurface *surface,
                              uint32_t *width, uint32_t *height) {
    uam::gxm::get_surface_dimensions(surface, width, height);
}

} // extern "C"
