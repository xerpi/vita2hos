#include "gxm_color_surface_c.h"
#include "uam/gxm/color_surface.h"

extern "C" {

VkFormat gxm_translate_color_format(SceGxmColorFormat format) {
    return uam::gxm::translate_color_format(format);
}

VkImageTiling gxm_translate_surface_type(SceGxmColorSurfaceType type) {
    return uam::gxm::translate_surface_type(type);
}

uint32_t gxm_get_bytes_per_pixel(SceGxmColorFormat format) {
    return uam::gxm::get_bytes_per_pixel(format);
}

int gxm_init_color_surface(SceGxmColorSurface *surface,
                          SceGxmColorFormat colorFormat,
                          SceGxmColorSurfaceType surfaceType,
                          SceGxmColorSurfaceScaleMode scaleMode,
                          SceGxmOutputRegisterSize outputRegisterSize,
                          uint32_t width,
                          uint32_t height,
                          uint32_t strideInPixels,
                          void *data) {
    return uam::gxm::init_color_surface(surface, colorFormat, surfaceType,
                                      scaleMode, outputRegisterSize,
                                      width, height, strideInPixels, data);
}

void* gxm_get_surface_data(const SceGxmColorSurface *surface) {
    return uam::gxm::get_surface_data(surface);
}

int gxm_set_surface_data(SceGxmColorSurface *surface, void *data) {
    return uam::gxm::set_surface_data(surface, data);
}

SceGxmColorFormat gxm_get_surface_format(const SceGxmColorSurface *surface) {
    return uam::gxm::get_surface_format(surface);
}

int gxm_set_surface_format(SceGxmColorSurface *surface, SceGxmColorFormat format) {
    return uam::gxm::set_surface_format(surface, format);
}

SceGxmColorSurfaceType gxm_get_surface_type(const SceGxmColorSurface *surface) {
    return uam::gxm::get_surface_type(surface);
}

uint32_t gxm_get_stride_in_pixels(const SceGxmColorSurface *surface) {
    return uam::gxm::get_stride_in_pixels(surface);
}

uint32_t gxm_get_stride_in_bytes(const SceGxmColorSurface *surface) {
    return uam::gxm::get_stride_in_bytes(surface);
}

bool gxm_is_surface_disabled(const SceGxmColorSurface *surface) {
    return uam::gxm::is_surface_disabled(surface);
}

void gxm_set_surface_disabled(SceGxmColorSurface *surface, bool disabled) {
    uam::gxm::set_surface_disabled(surface, disabled);
}

void gxm_get_surface_dimensions(const SceGxmColorSurface *surface,
                              uint32_t *width, uint32_t *height) {
    uam::gxm::get_surface_dimensions(surface, width, height);
}

} // extern "C"
