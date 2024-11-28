#include "uam/gxm/color_surface.h"

namespace uam {
namespace gxm {

// Convert GXM color format to Vulkan format
VkFormat translate_color_format(SceGxmColorFormat format) {
    // Extract base format
    SceGxmColorBaseFormat base_format = static_cast<SceGxmColorBaseFormat>(format & SCE_GXM_COLOR_BASE_FORMAT_MASK);
    
    switch (base_format) {
    case SCE_GXM_COLOR_BASE_FORMAT_U8:
        return VK_FORMAT_R8_UNORM;
    case SCE_GXM_COLOR_BASE_FORMAT_S8:
        return VK_FORMAT_R8_SNORM;
    case SCE_GXM_COLOR_BASE_FORMAT_U4U4U4U4:
        return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
    case SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case SCE_GXM_COLOR_BASE_FORMAT_U8U8U8:
        return VK_FORMAT_R8G8B8_UNORM;
    case SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case SCE_GXM_COLOR_BASE_FORMAT_F32F32:
        return VK_FORMAT_R32G32_SFLOAT;
    // Add more format translations as needed
    default:
        return VK_FORMAT_UNDEFINED;
    }
}

// Convert GXM surface type to Vulkan image tiling
VkImageTiling translate_surface_type(SceGxmColorSurfaceType type) {
    switch (type) {
    case SCE_GXM_COLOR_SURFACE_LINEAR:
        return VK_IMAGE_TILING_LINEAR;
    case SCE_GXM_COLOR_SURFACE_TILED:
        return VK_IMAGE_TILING_OPTIMAL;
    default:
        return VK_IMAGE_TILING_LINEAR;
    }
}

// Get bytes per pixel for a given color format
uint32_t get_bytes_per_pixel(SceGxmColorFormat format) {
    SceGxmColorBaseFormat base_format = static_cast<SceGxmColorBaseFormat>(format & SCE_GXM_COLOR_BASE_FORMAT_MASK);
    
    switch (base_format) {
    case SCE_GXM_COLOR_BASE_FORMAT_U8:
    case SCE_GXM_COLOR_BASE_FORMAT_S8:
        return 1;
    case SCE_GXM_COLOR_BASE_FORMAT_U4U4U4U4:
    case SCE_GXM_COLOR_BASE_FORMAT_U5U6U5:
    case SCE_GXM_COLOR_BASE_FORMAT_U1U5U5U5:
    case SCE_GXM_COLOR_BASE_FORMAT_U8U8:
    case SCE_GXM_COLOR_BASE_FORMAT_S8S8:
        return 2;
    case SCE_GXM_COLOR_BASE_FORMAT_U8U8U8:
        return 3;
    case SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8:
    case SCE_GXM_COLOR_BASE_FORMAT_F32:
    case SCE_GXM_COLOR_BASE_FORMAT_U2U10U10U10:
        return 4;
    case SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16:
    case SCE_GXM_COLOR_BASE_FORMAT_F32F32:
        return 8;
    default:
        return 0;
    }
}

// Initialize a color surface
int init_color_surface(SceGxmColorSurface *surface,
                      SceGxmColorFormat colorFormat,
                      SceGxmColorSurfaceType surfaceType,
                      SceGxmColorSurfaceScaleMode scaleMode,
                      SceGxmOutputRegisterSize outputRegisterSize,
                      uint32_t width,
                      uint32_t height,
                      uint32_t strideInPixels,
                      void *data) {
    
    if (!surface) {
        return SCE_GXM_ERROR_INVALID_POINTER;
    }

    if (width == 0 || height == 0) {
        return SCE_GXM_ERROR_INVALID_VALUE;
    }

    // Initialize the surface
    surface->disabled = 0;
    surface->downscale = (scaleMode == SCE_GXM_COLOR_SURFACE_SCALE_DOWNSAMPLE) ? 1 : 0;
    surface->gamma = 0; // Default gamma mode
    surface->width = width;
    surface->height = height;
    surface->strideInPixels = strideInPixels;
    surface->data = data;
    surface->colorFormat = colorFormat;
    surface->surfaceType = surfaceType;

    return SCE_GXM_OK;
}

// Get surface data pointer
void* get_surface_data(const SceGxmColorSurface *surface) {
    return surface ? surface->data : nullptr;
}

// Set surface data pointer
int set_surface_data(SceGxmColorSurface *surface, void *data) {
    if (!surface) {
        return SCE_GXM_ERROR_INVALID_POINTER;
    }
    surface->data = data;
    return SCE_GXM_OK;
}

// Get surface format
SceGxmColorFormat get_surface_format(const SceGxmColorSurface *surface) {
    return surface ? surface->colorFormat : SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR;
}

// Set surface format
int set_surface_format(SceGxmColorSurface *surface, SceGxmColorFormat format) {
    if (!surface) {
        return SCE_GXM_ERROR_INVALID_POINTER;
    }
    surface->colorFormat = format;
    return SCE_GXM_OK;
}

// Get surface type
SceGxmColorSurfaceType get_surface_type(const SceGxmColorSurface *surface) {
    return surface ? surface->surfaceType : SCE_GXM_COLOR_SURFACE_LINEAR;
}

// Get stride in pixels
uint32_t get_stride_in_pixels(const SceGxmColorSurface *surface) {
    return surface ? surface->strideInPixels : 0;
}

// Get stride in bytes
uint32_t get_stride_in_bytes(const SceGxmColorSurface *surface) {
    if (!surface) {
        return 0;
    }
    return surface->strideInPixels * get_bytes_per_pixel(surface->colorFormat);
}

// Check if surface is disabled
bool is_surface_disabled(const SceGxmColorSurface *surface) {
    return surface ? surface->disabled : true;
}

// Set surface disabled state
void set_surface_disabled(SceGxmColorSurface *surface, bool disabled) {
    if (surface) {
        surface->disabled = disabled ? 1 : 0;
    }
}

// Get surface dimensions
void get_surface_dimensions(const SceGxmColorSurface *surface,
                          uint32_t *width, uint32_t *height) {
    if (surface && width && height) {
        *width = surface->width;
        *height = surface->height;
    }
}

} // namespace gxm
} // namespace uam
