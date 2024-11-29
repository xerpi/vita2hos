#ifndef SCE_GXM_TYPES_H
#define SCE_GXM_TYPES_H

#include <psp2/gxm.h>
#include <deko3d.h>

// Forward declarations
typedef struct SceGxmVertexAttribute SceGxmVertexAttribute;
typedef struct SceGxmVertexStream SceGxmVertexStream;
typedef uint32_t SceGxmShaderPatcherId;

typedef struct SceGxmSyncObject {
    DkFence fence;
} SceGxmSyncObject;

typedef struct SceGxmVertexProgram {
    const SceGxmVertexAttribute *attributes;
    const SceGxmVertexStream *streams;
    uint32_t attributeCount;
    uint32_t streamCount;
    SceGxmShaderPatcherId programId;
    void *defaultUniformBuffer;
    size_t defaultUniformBufferSize;
    void *shaderModule;
} SceGxmVertexProgram;

typedef struct SceGxmFragmentProgram {
    struct {
        uint32_t colorMask;
    } blendInfo;
    SceGxmShaderPatcherId programId;
    void *defaultUniformBuffer;
    size_t defaultUniformBufferSize;
    void *shaderModule;
} SceGxmFragmentProgram;

typedef struct SceGxmProgram {
    uint32_t magic;
    uint8_t major_version;
    uint8_t minor_version;
    uint16_t padding;
    uint32_t size;
    uint32_t default_uniform_buffer_count;
    uint32_t parameter_count;
    uint32_t attribute_count;
    uint32_t varying_count;
    uint32_t code_offset;
    uint32_t code_size;
} SceGxmProgram;

typedef struct SceGxmProgramParameter {
    uint32_t name_offset;
    uint32_t category : 4;
    uint32_t type : 4;
    uint32_t component_count : 4;
    uint32_t container_index : 4;
    uint32_t semantic : 4;
    uint32_t semantic_index : 4;
    uint32_t array_size : 8;
    uint32_t resource_index;
} SceGxmProgramParameter;

typedef struct SceGxmTextureInner {
    union {
        uint32_t control_words[4];
        struct {
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
            uint32_t unk2 : 3;

            // Control Word 1
            uint32_t data_addr;

            // Control Word 2
            uint32_t palette_addr;

            // Control Word 3
            uint32_t type : 4;
            uint32_t format : 6;
            uint32_t width : 11;
            uint32_t height : 11;
        };
    };
} SceGxmTextureInner;

typedef struct SceGxmColorSurfaceInner {
    union {
        uint32_t flags;
        struct {
            uint32_t disabled : 1;
            uint32_t downscale : 1;
            uint32_t width : 30;
        };
    };
    uint32_t height;
    uint32_t strideInPixels;
    void *data;
    SceGxmColorFormat colorFormat;
    SceGxmColorSurfaceType surfaceType;
    uint32_t outputRegisterSize;
    SceGxmColorSurfaceScaleMode scaleMode;
    uint32_t driverMemBlock;
} SceGxmColorSurfaceInner;

typedef struct SceGxmDepthStencilSurface {
    union {
        uint32_t zlsControl;
        struct {
            uint32_t disabled : 1;
            uint32_t reserved : 2;
            uint32_t strideScale : 4;
            uint32_t reserved2 : 5;
            uint32_t type : 8;
            uint32_t format : 11;
            uint32_t reserved3 : 1;
        };
    };
    void *depthData;
    void *stencilData;
    uint32_t backgroundDepth;
    union {
        uint32_t backgroundControl;
        struct {
            uint32_t stencil : 8;
            uint32_t mask : 1;
            uint32_t reserved4 : 23;
        };
    };
} SceGxmDepthStencilSurface;

typedef struct SceGxmContextState {
    struct {
        struct {
            uint32_t shaders : 1;
            uint32_t vertex_default_uniform : 1;
            uint32_t fragment_default_uniform : 1;
            uint32_t fragment_textures : 1;
            uint32_t color_write : 1;
        } bit;
    } dirty;

    bool in_scene;
    
    struct {
        uint32_t head;
        DkMemBlock memblock;
        size_t size;
    } vertex_rb;

    struct {
        uint32_t head;
        DkMemBlock memblock;
        size_t size;
    } fragment_rb;

    struct {
        void *cpu_addr;
        DkGpuAddr gpu_addr;
        bool allocated;
    } vertex_default_uniform;

    struct {
        void *cpu_addr;
        DkGpuAddr gpu_addr;
        bool allocated;
    } fragment_default_uniform;

    SceGxmVertexProgram *vertex_program;
    SceGxmFragmentProgram *fragment_program;
    SceGxmTextureInner fragment_textures[16];
    DkColorWriteState color_write;
} SceGxmContextState;

typedef struct SceGxmContext {
    SceGxmContextState state;
    DkCmdBuf cmdbuf;
    DkCmdList cmdlist;
    DkQueue queue;
    DkMemBlock vertex_rb_memblock;
    DkMemBlock fragment_rb_memblock;
    DkMemBlock vertex_uniform_memblock;
    DkMemBlock fragment_uniform_memblock;
    DkMemBlock notification_region_memblock;
    DkMemBlock vertex_default_uniform_buffer;
    DkMemBlock fragment_default_uniform_buffer;
    DkMemBlock shadow_memory;
    DkImage shadow_image;
    DkImageView shadow_view;
    DkMemBlock shadow_descriptor_memblock;
    uint32_t notification_region_offset;
    uint32_t vertex_ring_offset;
    uint32_t fragment_ring_offset;
    uint32_t vertex_uniform_max_offset;
    uint32_t fragment_uniform_max_offset;
    uint32_t vertex_default_uniform_buffer_size;
    uint32_t fragment_default_uniform_buffer_size;
    uint32_t shadow_memory_size;
    bool initialized;
} SceGxmContext;

#endif // SCE_GXM_TYPES_H
