#ifndef GXM_TO_DK_H
#define GXM_TO_DK_H

#include <assert.h>
#include <deko3d.h>
#include <psp2/gxm.h>

#include "gxm/util.h"

static inline uint32_t gxm_color_surface_type_to_dk_image_flags(SceGxmColorSurfaceType type)
{
    switch (type) {
    case SCE_GXM_COLOR_SURFACE_LINEAR:
        return DkImageFlags_PitchLinear;
    case SCE_GXM_COLOR_SURFACE_TILED:
    case SCE_GXM_COLOR_SURFACE_SWIZZLED:
        return DkImageFlags_BlockLinear | DkImageFlags_HwCompression;
    default:
        UNREACHABLE("Unsupported SceGxmColorSurfaceType");
    }
}

static inline size_t gxm_color_base_format_bits_per_pixel(SceGxmColorBaseFormat base_format)
{
    switch (base_format) {
    case SCE_GXM_COLOR_BASE_FORMAT_U8:
    case SCE_GXM_COLOR_BASE_FORMAT_S8:
        return 8;
    case SCE_GXM_COLOR_BASE_FORMAT_U5U6U5:
    case SCE_GXM_COLOR_BASE_FORMAT_U1U5U5U5:
    case SCE_GXM_COLOR_BASE_FORMAT_U4U4U4U4:
    case SCE_GXM_COLOR_BASE_FORMAT_U8U3U3U2:
    case SCE_GXM_COLOR_BASE_FORMAT_F16:
    case SCE_GXM_COLOR_BASE_FORMAT_S16:
    case SCE_GXM_COLOR_BASE_FORMAT_U16:
    case SCE_GXM_COLOR_BASE_FORMAT_S5S5U6:
    case SCE_GXM_COLOR_BASE_FORMAT_U8U8:
    case SCE_GXM_COLOR_BASE_FORMAT_S8S8:
        return 16;
    case SCE_GXM_COLOR_BASE_FORMAT_U8U8U8:
        return 24;
    case SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8:
    case SCE_GXM_COLOR_BASE_FORMAT_F16F16:
    case SCE_GXM_COLOR_BASE_FORMAT_F32:
    case SCE_GXM_COLOR_BASE_FORMAT_S16S16:
    case SCE_GXM_COLOR_BASE_FORMAT_U16U16:
    case SCE_GXM_COLOR_BASE_FORMAT_U2U10U10U10:
    case SCE_GXM_COLOR_BASE_FORMAT_U8S8S8U8:
    case SCE_GXM_COLOR_BASE_FORMAT_S8S8S8S8:
    case SCE_GXM_COLOR_BASE_FORMAT_F11F11F10:
    case SCE_GXM_COLOR_BASE_FORMAT_SE5M9M9M9:
    case SCE_GXM_COLOR_BASE_FORMAT_U2F10F10F10:
        return 32;
    case SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16:
    case SCE_GXM_COLOR_BASE_FORMAT_F32F32:
        return 64;
    default:
        UNREACHABLE("Unsupported SceGxmColorBaseFormat");
    }
}

static inline size_t gxm_color_base_format_bytes_per_pixel(SceGxmColorBaseFormat base_format)
{
    return gxm_color_base_format_bits_per_pixel(base_format) >> 3;
}

static inline size_t gxm_color_format_bytes_per_pixel(SceGxmColorFormat format)
{
    return gxm_color_base_format_bytes_per_pixel(format & SCE_GXM_COLOR_BASE_FORMAT_MASK);
}

static inline size_t gxm_texture_base_format_bytes_per_pixel(SceGxmTextureBaseFormat base_format)
{
    switch (base_format) {
    case SCE_GXM_TEXTURE_BASE_FORMAT_U8:
    case SCE_GXM_TEXTURE_BASE_FORMAT_S8:
        return 8;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U4U4U4U4:
    case SCE_GXM_TEXTURE_BASE_FORMAT_U8U3U3U2:
    case SCE_GXM_TEXTURE_BASE_FORMAT_U1U5U5U5:
    case SCE_GXM_TEXTURE_BASE_FORMAT_U5U6U5:
    case SCE_GXM_TEXTURE_BASE_FORMAT_S5S5U6:
    case SCE_GXM_TEXTURE_BASE_FORMAT_U8U8:
    case SCE_GXM_TEXTURE_BASE_FORMAT_S8S8:
    case SCE_GXM_TEXTURE_BASE_FORMAT_U16:
    case SCE_GXM_TEXTURE_BASE_FORMAT_S16:
    case SCE_GXM_TEXTURE_BASE_FORMAT_F16:
        return 16;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8U8:
    case SCE_GXM_TEXTURE_BASE_FORMAT_S8S8S8S8:
    case SCE_GXM_TEXTURE_BASE_FORMAT_U2U10U10U10:
    case SCE_GXM_TEXTURE_BASE_FORMAT_U16U16:
    case SCE_GXM_TEXTURE_BASE_FORMAT_S16S16:
    case SCE_GXM_TEXTURE_BASE_FORMAT_F16F16:
    case SCE_GXM_TEXTURE_BASE_FORMAT_F32:
    case SCE_GXM_TEXTURE_BASE_FORMAT_F32M:
    case SCE_GXM_TEXTURE_BASE_FORMAT_X8S8S8U8:
    case SCE_GXM_TEXTURE_BASE_FORMAT_X8U24:
    case SCE_GXM_TEXTURE_BASE_FORMAT_U32:
    case SCE_GXM_TEXTURE_BASE_FORMAT_S32:
    case SCE_GXM_TEXTURE_BASE_FORMAT_SE5M9M9M9:
    case SCE_GXM_TEXTURE_BASE_FORMAT_F11F11F10:
        return 32;
    case SCE_GXM_TEXTURE_BASE_FORMAT_F16F16F16F16:
    case SCE_GXM_TEXTURE_BASE_FORMAT_U16U16U16U16:
    case SCE_GXM_TEXTURE_BASE_FORMAT_S16S16S16S16:
    case SCE_GXM_TEXTURE_BASE_FORMAT_F32F32:
    case SCE_GXM_TEXTURE_BASE_FORMAT_U32U32:
        return 64;
    case SCE_GXM_TEXTURE_BASE_FORMAT_PVRT2BPP:
        return 2;
    case SCE_GXM_TEXTURE_BASE_FORMAT_PVRT4BPP:
        return 4;
    case SCE_GXM_TEXTURE_BASE_FORMAT_PVRTII2BPP:
        return 2;
    case SCE_GXM_TEXTURE_BASE_FORMAT_PVRTII4BPP:
        return 4;
    case SCE_GXM_TEXTURE_BASE_FORMAT_UBC1:
        return 4;
    case SCE_GXM_TEXTURE_BASE_FORMAT_UBC2:
    case SCE_GXM_TEXTURE_BASE_FORMAT_UBC3:
        return 8;
    case SCE_GXM_TEXTURE_BASE_FORMAT_UBC4:
    case SCE_GXM_TEXTURE_BASE_FORMAT_SBC4:
        return 4;
    case SCE_GXM_TEXTURE_BASE_FORMAT_UBC5:
    case SCE_GXM_TEXTURE_BASE_FORMAT_SBC5:
        return 8;
    case SCE_GXM_TEXTURE_BASE_FORMAT_YUV420P2:
    case SCE_GXM_TEXTURE_BASE_FORMAT_YUV420P3:
        return 12;
    case SCE_GXM_TEXTURE_BASE_FORMAT_YUV422:
        return 16;
    case SCE_GXM_TEXTURE_BASE_FORMAT_P4:
        return 4;
    case SCE_GXM_TEXTURE_BASE_FORMAT_P8:
        return 8;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8:
    case SCE_GXM_TEXTURE_BASE_FORMAT_S8S8S8:
        return 24;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U2F10F10F10:
        return 32;
    default:
        UNREACHABLE("Unsupported SceGxmTextureBaseFormat");
    }
}

static inline size_t gxm_texture_format_bytes_per_pixel(SceGxmTextureFormat format)
{
    return gxm_texture_base_format_bytes_per_pixel(format & SCE_GXM_TEXTURE_BASE_FORMAT_MASK) >> 3;
}

static inline DkImageFormat gxm_color_format_to_dk_image_format(SceGxmColorFormat format)
{
    switch (format) {
    case SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR:
        return DkImageFormat_RGBA8_Unorm;
    default:
        UNREACHABLE("Unsupported SceGxmColorFormat");
    }
}

static inline DkVtxAttribType gxm_to_dk_vtx_attrib_type(SceGxmAttributeFormat format)
{
    switch (format) {
    case SCE_GXM_ATTRIBUTE_FORMAT_U8:
    case SCE_GXM_ATTRIBUTE_FORMAT_U16:
        return DkVtxAttribType_Uint;
    case SCE_GXM_ATTRIBUTE_FORMAT_S8:
    case SCE_GXM_ATTRIBUTE_FORMAT_S16:
        return DkVtxAttribType_Sint;
    case SCE_GXM_ATTRIBUTE_FORMAT_U8N:
    case SCE_GXM_ATTRIBUTE_FORMAT_U16N:
        return DkVtxAttribType_Unorm;
    case SCE_GXM_ATTRIBUTE_FORMAT_S8N:
    case SCE_GXM_ATTRIBUTE_FORMAT_S16N:
        return DkVtxAttribType_Snorm;
    case SCE_GXM_ATTRIBUTE_FORMAT_F16:
    case SCE_GXM_ATTRIBUTE_FORMAT_F32:
        return DkVtxAttribType_Float;
    default:
        UNREACHABLE("Unsupported SceGxmAttributeFormat");
    }
}

static inline DkVtxAttribSize gxm_to_dk_vtx_attrib_size(SceGxmAttributeFormat format,
                                                        uint8_t componentCount)
{
    switch (format) {
    case SCE_GXM_ATTRIBUTE_FORMAT_U8:
    case SCE_GXM_ATTRIBUTE_FORMAT_S8:
    case SCE_GXM_ATTRIBUTE_FORMAT_U8N:
    case SCE_GXM_ATTRIBUTE_FORMAT_S8N:
        switch (componentCount) {
        case 1:
            return DkVtxAttribSize_1x8;
        case 2:
            return DkVtxAttribSize_2x8;
        case 3:
            return DkVtxAttribSize_3x8;
        case 4:
            return DkVtxAttribSize_4x8;
        default:
            UNREACHABLE("Unsupported SceGxmAttributeFormat component count");
        }
    case SCE_GXM_ATTRIBUTE_FORMAT_U16:
    case SCE_GXM_ATTRIBUTE_FORMAT_S16:
    case SCE_GXM_ATTRIBUTE_FORMAT_U16N:
    case SCE_GXM_ATTRIBUTE_FORMAT_S16N:
    case SCE_GXM_ATTRIBUTE_FORMAT_F16:
        switch (componentCount) {
        case 1:
            return DkVtxAttribSize_1x16;
        case 2:
            return DkVtxAttribSize_2x16;
        case 3:
            return DkVtxAttribSize_3x16;
        case 4:
            return DkVtxAttribSize_4x16;
        default:
            UNREACHABLE("Unsupported SceGxmAttributeFormat component count");
        }
    case SCE_GXM_ATTRIBUTE_FORMAT_F32:
        switch (componentCount) {
        case 1:
            return DkVtxAttribSize_1x32;
        case 2:
            return DkVtxAttribSize_2x32;
        case 3:
            return DkVtxAttribSize_3x32;
        case 4:
            return DkVtxAttribSize_4x32;
        default:
            UNREACHABLE("Unsupported SceGxmAttributeFormat component count");
        }
    default:
        UNREACHABLE("Unsupported SceGxmAttributeFormat");
    }
}

static inline DkPrimitive gxm_to_dk_primitive(SceGxmPrimitiveType prim)
{
    switch (prim) {
    case SCE_GXM_PRIMITIVE_TRIANGLES:
        return DkPrimitive_Triangles;
    case SCE_GXM_PRIMITIVE_LINES:
        return DkPrimitive_Lines;
    case SCE_GXM_PRIMITIVE_POINTS:
        return DkPrimitive_Lines;
    case SCE_GXM_PRIMITIVE_TRIANGLE_STRIP:
        return DkPrimitive_TriangleStrip;
    case SCE_GXM_PRIMITIVE_TRIANGLE_FAN:
        return DkPrimitive_TriangleFan;
    case SCE_GXM_PRIMITIVE_TRIANGLE_EDGES:
        return DkPrimitive_Triangles;
    default:
        UNREACHABLE("Unsupported SceGxmPrimitiveType");
    }
}

static inline DkIdxFormat gxm_to_dk_idx_format(SceGxmIndexFormat format)
{
    switch (format) {
    case SCE_GXM_INDEX_FORMAT_U16:
        return DkIdxFormat_Uint16;
    case SCE_GXM_INDEX_FORMAT_U32:
        return DkIdxFormat_Uint32;
    default:
        UNREACHABLE("Unsupported SceGxmIndexFormat");
    }
}

static inline DkCompareOp gxm_depth_func_to_dk_compare_op(SceGxmDepthFunc func)
{
    switch (func) {
    case SCE_GXM_DEPTH_FUNC_NEVER:
        return DkCompareOp_Never;
    case SCE_GXM_DEPTH_FUNC_LESS:
        return DkCompareOp_Less;
    case SCE_GXM_DEPTH_FUNC_EQUAL:
        return DkCompareOp_Equal;
    case SCE_GXM_DEPTH_FUNC_LESS_EQUAL:
        return DkCompareOp_Lequal;
    case SCE_GXM_DEPTH_FUNC_GREATER:
        return DkCompareOp_Greater;
    case SCE_GXM_DEPTH_FUNC_NOT_EQUAL:
        return DkCompareOp_NotEqual;
    case SCE_GXM_DEPTH_FUNC_GREATER_EQUAL:
        return DkCompareOp_Gequal;
    case SCE_GXM_DEPTH_FUNC_ALWAYS:
        return DkCompareOp_Always;
    default:
        UNREACHABLE("Unsupported SceGxmDepthFunc");
    }
}

static inline DkCompareOp gxm_stencil_func_to_dk_compare_op(SceGxmStencilFunc func)
{
    switch (func) {
    case SCE_GXM_STENCIL_FUNC_NEVER:
        return DkCompareOp_Never;
    case SCE_GXM_STENCIL_FUNC_LESS:
        return DkCompareOp_Less;
    case SCE_GXM_STENCIL_FUNC_EQUAL:
        return DkCompareOp_Equal;
    case SCE_GXM_STENCIL_FUNC_LESS_EQUAL:
        return DkCompareOp_Lequal;
    case SCE_GXM_STENCIL_FUNC_GREATER:
        return DkCompareOp_Greater;
    case SCE_GXM_STENCIL_FUNC_NOT_EQUAL:
        return DkCompareOp_NotEqual;
    case SCE_GXM_STENCIL_FUNC_GREATER_EQUAL:
        return DkCompareOp_Gequal;
    case SCE_GXM_STENCIL_FUNC_ALWAYS:
        return DkCompareOp_Always;
    default:
        UNREACHABLE("Unsupported SceGxmStencilFunc");
    }
}

static inline DkStencilOp gxm_stencil_op_to_dk_stencil_op(SceGxmStencilOp op)
{
    switch (op) {
    case SCE_GXM_STENCIL_OP_KEEP:
        return DkStencilOp_Keep;
    case SCE_GXM_STENCIL_OP_ZERO:
        return DkStencilOp_Zero;
    case SCE_GXM_STENCIL_OP_REPLACE:
        return DkStencilOp_Replace;
    case SCE_GXM_STENCIL_OP_INCR:
        return DkStencilOp_Incr;
    case SCE_GXM_STENCIL_OP_DECR:
        return DkStencilOp_Decr;
    case SCE_GXM_STENCIL_OP_INVERT:
        return DkStencilOp_Invert;
    case SCE_GXM_STENCIL_OP_INCR_WRAP:
        return DkStencilOp_IncrWrap;
    case SCE_GXM_STENCIL_OP_DECR_WRAP:
        return DkStencilOp_DecrWrap;
    default:
        UNREACHABLE("Unsupported SceGxmStencilOp");
    }
}

static inline DkWrapMode gxm_texture_addr_mode_to_dk_wrap_mode(SceGxmTextureAddrMode mode)
{
    switch (mode) {
    case SCE_GXM_TEXTURE_ADDR_REPEAT:
        return DkWrapMode_Repeat;
    case SCE_GXM_TEXTURE_ADDR_MIRROR:
        return DkWrapMode_MirroredRepeat;
    case SCE_GXM_TEXTURE_ADDR_CLAMP:
        return DkWrapMode_Clamp;
    case SCE_GXM_TEXTURE_ADDR_MIRROR_CLAMP:
        return DkWrapMode_MirrorClamp;
    case SCE_GXM_TEXTURE_ADDR_REPEAT_IGNORE_BORDER:
        UNREACHABLE("Unsupported SceGxmTextureAddrMode SCE_GXM_TEXTURE_ADDR_REPEAT_IGNORE_BORDER");
    case SCE_GXM_TEXTURE_ADDR_CLAMP_FULL_BORDER:
        return DkWrapMode_ClampToBorder;
    case SCE_GXM_TEXTURE_ADDR_CLAMP_IGNORE_BORDER:
    case SCE_GXM_TEXTURE_ADDR_CLAMP_HALF_BORDER:
    default:
        UNREACHABLE("Unsupported SceGxmTextureAddrMode");
    }
}

static inline DkFilter gxm_texture_filter_to_dk_filter(SceGxmTextureFilter filter)
{
    switch (filter) {
    case SCE_GXM_TEXTURE_FILTER_POINT:
        return DkFilter_Nearest;
    case SCE_GXM_TEXTURE_FILTER_LINEAR:
        return DkFilter_Linear;
    default:
        UNREACHABLE("Unsupported SceGxmTextureFilter");
    }
}

#endif
