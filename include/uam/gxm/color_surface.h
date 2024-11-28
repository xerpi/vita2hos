#pragma once

// Include Vulkan headers first
#include "vulkan.h"

// Then include platform-specific headers
#include <psp2/gxm.h>

namespace uam {
namespace gxm {

// Convert GXM color format to Vulkan format
VkFormat translate_color_format(SceGxmColorFormat format);

// Convert GXM surface type to Vulkan image tiling
VkImageTiling translate_surface_type(SceGxmColorSurfaceType type);

// Get bytes per pixel for a given color format
uint32_t get_bytes_per_pixel(SceGxmColorFormat format);

// Initialize a color surface
int init_color_surface(SceGxmColorSurface *surface,
                      SceGxmColorFormat colorFormat,
                      SceGxmColorSurfaceType surfaceType, 
                      SceGxmColorSurfaceScaleMode scaleMode,
                      SceGxmOutputRegisterSize outputRegisterSize,
                      uint32_t width,
                      uint32_t height,
                      uint32_t strideInPixels,
                      void *data);

// Get surface data pointer
void* get_surface_data(const SceGxmColorSurface *surface);

// Set surface data pointer
int set_surface_data(SceGxmColorSurface *surface, void *data);

// Get surface format
SceGxmColorFormat get_surface_format(const SceGxmColorSurface *surface);

// Set surface format
int set_surface_format(SceGxmColorSurface *surface, SceGxmColorFormat format);

// Get surface type
SceGxmColorSurfaceType get_surface_type(const SceGxmColorSurface *surface);

// Get stride in pixels
uint32_t get_stride_in_pixels(const SceGxmColorSurface *surface);

// Get stride in bytes
uint32_t get_stride_in_bytes(const SceGxmColorSurface *surface);

// Check if surface is disabled
bool is_surface_disabled(const SceGxmColorSurface *surface);

// Set surface disabled state
void set_surface_disabled(SceGxmColorSurface *surface, bool disabled);

// Get surface dimensions
void get_surface_dimensions(const SceGxmColorSurface *surface, 
                          uint32_t *width, uint32_t *height);

} // namespace gxm
} // namespace uam
