#ifndef DISPLAY_TO_DK_H
#define DISPLAY_TO_DK_H

#include <assert.h>
#include <deko3d.h>
#include <psp2/display.h>

static inline uint32_t display_pixelformat_bytes_per_pixel(SceDisplayPixelFormat format)
{
    switch (format) {
    case SCE_DISPLAY_PIXELFORMAT_A8B8G8R8:
        return 4;
    default:
        UNREACHABLE("Unsupported SceDisplayPixelFormat");
    }
}

static inline DkImageFormat display_pixelformat_to_dk_image_format(SceDisplayPixelFormat format)
{
    switch (format) {
    case SCE_DISPLAY_PIXELFORMAT_A8B8G8R8:
        return DkImageFormat_RGBA8_Unorm;
    default:
        UNREACHABLE("Unsupported SceDisplayPixelFormat");
    }
}

#endif
