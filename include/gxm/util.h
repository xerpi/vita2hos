#ifndef GXM_UTIL_H
#define GXM_UTIL_H

#include <psp2/gxm.h>
#include <switch.h>

#define SCE_GXM_MAX_SCENES_PER_RENDERTARGET 8

#define SCE_GXM_NOTIFICATION_COUNT 512

#define SCE_GXM_COLOR_BASE_FORMAT_MASK   0xF1800000U
#define SCE_GXM_TEXTURE_BASE_FORMAT_MASK 0x9f000000U

#define SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_DISABLE_BIT   1
#define SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_STRIDE_OFFSET 3
#define SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_STRIDE_MASK   0xF
#define SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_TYPE_OFFSET   12
#define SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_TYPE_MASK     0xFF
#define SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_FORMAT_OFFSET 21
#define SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_FORMAT_MASK   0x7FF

#define SCE_GXM_DEPTH_STENCIL_BG_CTRL_STENCIL_MASK 0xFF
#define SCE_GXM_DEPTH_STENCIL_BG_CTRL_MASK_BIT     0x100

typedef struct {
    // Control Word 0
    uint32_t unk0 : 3;
    uint32_t vaddr_mode : 3;
    uint32_t uaddr_mode : 3;
    uint32_t mip_filter : 1;
    uint32_t min_filter : 2;
    uint32_t mag_filter : 2;
    uint32_t unk1 : 3;
    uint32_t mip_count : 4;
    uint32_t lod_bias : 6;
    uint32_t gamma_mode : 2;
    uint32_t unk2 : 2;
    uint32_t format0 : 1;
    // Control Word 1
    union {
        struct {
            uint32_t height : 12;
            uint32_t width : 12;
        };

        struct {
            uint32_t height_base2 : 4;
            uint32_t unknown1 : 12;
            uint32_t width_base2 : 4;
            uint32_t unknown2 : 4;
        };

        struct {
            uint32_t whblock : 24;
            uint32_t base_format : 5;
            uint32_t type : 3;
        };
    };
    // Control Word 2
    uint32_t lod_min0 : 2;
    uint32_t data_addr : 30;
    // Control Word 3
    uint32_t palette_addr : 26;
    uint32_t lod_min1 : 2;
    uint32_t swizzle_format : 3;
    uint32_t normalize_mode : 1;
} SceGxmTextureInner;
static_assert(sizeof(SceGxmTextureInner) == sizeof(SceGxmTexture), "Incorrect size");

typedef struct {
    // opaque start
    uint32_t disabled : 1;
    uint32_t downscale : 1;
    uint32_t pad : 30;
    uint32_t width;
    uint32_t height;
    uint32_t strideInPixels;
    void *data;
    SceGxmColorFormat colorFormat;
    SceGxmColorSurfaceType surfaceType;
    // opaque end
    uint32_t outputRegisterSize;
    SceGxmTexture backgroundTex;
} SceGxmColorSurfaceInner;
static_assert(sizeof(SceGxmColorSurfaceInner) == sizeof(SceGxmColorSurface), "Incorrect size");

typedef struct SceGxmProgram {
    uint32_t magic; // should be "GXP\0"

    uint8_t major_version; // min 1
    uint8_t minor_version; // min 4
    uint16_t sdk_version;  // 0x350 - 3.50

    uint32_t
        size; // size of file - ignoring padding bytes at the end after SceGxmProgramParameter table

    uint32_t binary_guid;
    uint32_t source_guid;

    uint32_t program_flags;

    uint32_t buffer_flags; // Buffer flags. 2 bits per buffer. 0x1 - loaded into registers. 0x2 -
                           // read from memory

    uint32_t texunit_flags[2]; // Tex unit flags. 4 bits per tex unit. 0x1 is non dependent read,
                               // 0x2 is dependent.

    uint32_t parameter_count;
    uint32_t
        parameters_offset; // Number of bytes from the start of this field to the first parameter.
    uint32_t varyings_offset; // offset to vertex outputs / fragment inputs, relative to this field

    uint16_t primary_reg_count;   // (PAs)
    uint16_t secondary_reg_count; // (SAs)
    uint32_t temp_reg_count1;
    uint16_t temp_reg_count2; // Temp reg count in selective rate(programmable blending) phase

    uint16_t primary_program_phase_count;
    uint32_t primary_program_instr_count;
    uint32_t primary_program_offset;

    uint32_t secondary_program_instr_count;
    uint32_t secondary_program_offset;     // relative to the beginning of this field
    uint32_t secondary_program_offset_end; // relative to the beginning of this field

    uint32_t scratch_buffer_count;
    uint32_t thread_buffer_count;
    uint32_t literal_buffer_count;

    uint32_t data_buffer_count;
    uint32_t texture_buffer_count;
    uint32_t default_uniform_buffer_count;

    uint32_t literal_buffer_data_offset;

    uint32_t compiler_version; // The version is shifted 4 bits to the left.

    uint32_t literals_count;
    uint32_t literals_offset;
    uint32_t uniform_buffer_count;
    uint32_t uniform_buffer_offset;

    uint32_t dependent_sampler_count;
    uint32_t dependent_sampler_offset;
    uint32_t texture_buffer_dependent_sampler_count;
    uint32_t texture_buffer_dependent_sampler_offset;
    uint32_t container_count;
    uint32_t container_offset;
    uint32_t sampler_query_info_offset; // Offset to array of uint16_t
} SceGxmProgram;

typedef struct SceGxmProgramParameter {
    int32_t name_offset; // Number of bytes from the start of this structure to the name string.
    struct {
        uint16_t category : 4; // SceGxmParameterCategory
        uint16_t type : 4;     // SceGxmParameterType - applicable for constants, not applicable for
                               // samplers (select type like float, half, fixed ...)
        uint16_t component_count : 4; // applicable for constants, not applicable for samplers
                                      // (select size like float2, float3, float3 ...)
        uint16_t container_index : 4; // applicable for constants, not applicable for samplers
                                      // (buffer, default, texture)
    };
    uint8_t semantic; // applicable only for for vertex attributes, for everything else it's 0
    uint8_t semantic_index;
    uint32_t array_size;
    int32_t resource_index;
} SceGxmProgramParameter;

static inline uint32_t gxm_parameter_type_size(SceGxmParameterType type)
{
    switch (type) {
    case SCE_GXM_PARAMETER_TYPE_U8:
    case SCE_GXM_PARAMETER_TYPE_S8:
        return 1;
    case SCE_GXM_PARAMETER_TYPE_F16:
    case SCE_GXM_PARAMETER_TYPE_U16:
    case SCE_GXM_PARAMETER_TYPE_S16:
        return 2;
    case SCE_GXM_PARAMETER_TYPE_F32:
    case SCE_GXM_PARAMETER_TYPE_U32:
    case SCE_GXM_PARAMETER_TYPE_S32:
    default:
        return 4;
    }
}

static inline void *gxm_texture_get_data(const SceGxmTextureInner *texture)
{
    return (void *)(texture->data_addr << 2);
}

static inline uint32_t gxm_texture_get_type(const SceGxmTextureInner *texture)
{
    return texture->type << 29;
}

static inline size_t gxm_texture_get_width(const SceGxmTextureInner *texture)
{
    if (gxm_texture_get_type(texture) != SCE_GXM_TEXTURE_SWIZZLED &&
        gxm_texture_get_type(texture) != SCE_GXM_TEXTURE_CUBE)
        return texture->width + 1;
    return 1ull << (texture->width_base2 & 0xF);
}

static inline size_t gxm_texture_get_height(const SceGxmTextureInner *texture)
{
    if (gxm_texture_get_type(texture) != SCE_GXM_TEXTURE_SWIZZLED &&
        gxm_texture_get_type(texture) != SCE_GXM_TEXTURE_CUBE)
        return texture->height + 1;
    return 1ull << (texture->height_base2 & 0xF);
}

static inline SceGxmTextureFormat gxm_texture_get_format(const SceGxmTextureInner *texture)
{
    return (SceGxmTextureFormat)(texture->base_format << 24 | texture->format0 << 31 |
                                 texture->swizzle_format << 12);
}

static inline SceGxmTextureBaseFormat gxm_texture_get_base_format(SceGxmTextureFormat src)
{
    return (SceGxmTextureBaseFormat)(src & SCE_GXM_TEXTURE_BASE_FORMAT_MASK);
}

static inline size_t gxm_texture_get_stride_in_bytes(const SceGxmTextureInner *texture)
{
    return ((texture->mip_filter | (texture->min_filter << 1) | (texture->mip_count << 3) |
             (texture->lod_bias << 7)) +
            1) *
           4;
}

static inline bool gxm_base_format_is_paletted_format(SceGxmTextureBaseFormat base_format)
{
    return base_format == SCE_GXM_TEXTURE_BASE_FORMAT_P8 ||
           base_format == SCE_GXM_TEXTURE_BASE_FORMAT_P4;
}

#endif
