#include <stdlib.h>
#include <switch.h>
#include <deko3d.h>
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>
#include "gxm_surface.h"
#include <psp2/gxm.h>
#include "modules/SceGxm.h"
#include "modules/SceSysmem.h"
#include "circ_buf.h"
#include "config.h"
#include "dk_helpers.h"
#include "log.h"
#include "module.h"
#include "protected_bitset.h"
#include "uam_compiler_iface_c.h"
#include "util.h"
#include "vita3k_shader_recompiler_iface_c.h"
#include "vita_to_dk.h"
#include "gxm_color_surface_c.h"
#include <uam/gxm/color_surface.h>
#include "modules/SceGxmTypes.h"

#define DUMP_SHADER_SPIRV	0
#define DUMP_SHADER_GLSL	0
#define ENABLE_SHADER_DUMP_CB	0

#define SCE_GXM_NOTIFICATION_COUNT	512

#define SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_DISABLE_BIT   1
#define SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_STRIDE_OFFSET 3
#define SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_STRIDE_MASK   0xF
#define SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_TYPE_OFFSET   12
#define SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_TYPE_MASK     0xFF
#define SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_FORMAT_OFFSET 21
#define SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_FORMAT_MASK   0x7FF

#define SCE_GXM_DEPTH_STENCIL_BG_CTRL_STENCIL_MASK 0xFF
#define SCE_GXM_DEPTH_STENCIL_BG_CTRL_MASK_BIT	   0x100

#define MAX_GXM_MAPPED_MEMORY_BLOCKS	256

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

typedef struct SceGxmColorSurface {
    uint32_t disabled;
    uint32_t downscale;
    uint32_t gamma;
    uint32_t width;
    uint32_t height;
    uint32_t strideInPixels;
    void *data;
    SceGxmColorFormat colorFormat;
    SceGxmColorSurfaceType surfaceType;
    uint32_t outputRegisterSize;
    SceGxmTexture backgroundTex;
} SceGxmColorSurface;

typedef struct {
    union {
        struct {
            uint32_t disabled : 1;
            uint32_t downscale : 1;
            uint32_t pad : 30;
        };
        uint32_t width;
    };
    uint32_t height;
    uint32_t strideInPixels;
    void *data;
    SceGxmColorFormat colorFormat;
    SceGxmColorSurfaceType surfaceType;
    uint32_t outputRegisterSize;
    SceGxmTexture backgroundTex;
} SceGxmColorSurfaceInner;
static_assert(sizeof(SceGxmColorSurfaceInner) == sizeof(SceGxmColorSurface), "Incorrect size");

typedef struct SceGxmContext {
    DkCmdBuf cmdbuf;
    DkMemBlock memblock;
    const SceGxmRenderTarget *render_target;
    const SceGxmColorSurface *color_surface;
    const SceGxmDepthStencilSurface *depth_stencil_surface;
    DkImage color_image;
    struct {
        struct {
            uint32_t vertex_program : 1;
            uint32_t fragment_program : 1;
            uint32_t vertex_uniform_buffers : 1;
            uint32_t fragment_uniform_buffers : 1;
            uint32_t fragment_textures : 1;
            uint32_t depth_stencil : 1;
            uint32_t back_stencil : 1;
        } bit;
        uint32_t raw;
    } state;
} SceGxmContext;

typedef struct {
	DkFence *new_fence;
	DkFence *old_fence;
	void *callback_data;
} DisplayQueueEntry;

typedef struct {
	uint32_t tail;
	uint32_t head;
	uint32_t num_entries;
	DisplayQueueEntry *entries;
	uint32_t display_queue_max_pending_count;
	SceGxmDisplayQueueCallback *display_queue_callback;
	uint32_t display_queue_callback_data_size;
	uint32_t exit_thread;
	SceUID thid;
	UEvent ready_evflag;
	UEvent pending_evflag;
} DisplayQueueControlBlock;

/* Vita3K's shader recompiler */
struct GXMRenderVertUniformBlock {
	float viewport_flip[4];
	float viewport_flag;
	float screen_width;
	float screen_height;
	float z_offset;
	float z_scale;
};

struct GXMRenderFragUniformBlock {
	float back_disabled;
	float front_disabled;
	float writing_mask;
	float use_raw_image;
	float res_multiplier;
};

/* Global state */

static bool g_gxm_initialized;

/* Deko3D */

static DkDevice g_dk_device;
static DkQueue g_render_queue;
static DkMemBlock g_notification_region_memblock;
static DisplayQueueControlBlock *g_display_queue;
static DkMemBlock g_code_memblock;
static uint32_t g_code_mem_offset;

static int SceGxmDisplayQueue_thread(SceSize args, void *argp);

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

static inline int gxm_texture_get_type(const SceGxmTextureInner *texture)
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
	return (SceGxmTextureFormat)(texture->base_format << 24 |
				     texture->format0 << 31 |
				     texture->swizzle_format << 12);
}

static inline SceGxmTextureBaseFormat gxm_texture_get_base_format(SceGxmTextureFormat src)
{
	return (SceGxmTextureBaseFormat)(src & SCE_GXM_TEXTURE_BASE_FORMAT_MASK);
}

static inline size_t gxm_texture_get_stride_in_bytes(const SceGxmTextureInner *texture)
{
	return ((texture->mip_filter | (texture->min_filter << 1) |
	        (texture->mip_count << 3) | (texture->lod_bias << 7)) + 1) * 4;
}

static inline bool gxm_base_format_is_paletted_format(SceGxmTextureBaseFormat base_format)
{
	return base_format == SCE_GXM_TEXTURE_BASE_FORMAT_P8 ||
	       base_format == SCE_GXM_TEXTURE_BASE_FORMAT_P4;
}

#if DUMP_SHADER_SPIRV
static void dump_shader_spirv(const char *prefix, const uint32_t *spirv, uint32_t num_instr)
{
	static uint32_t cnt = 0;
	char name[128];
	snprintf(name, sizeof(name), VITA2HOS_DUMP_SHADER_PATH "/%s_%d.spv", prefix, cnt++);
	util_write_binary_file(name, spirv, num_instr * sizeof(uint32_t));
}
#endif

#if DUMP_SHADER_GLSL
static void dump_shader_glsl(const char *prefix, const char *glsl)
{
	static uint32_t cnt = 0;
	char name[128];
	snprintf(name, sizeof(name), VITA2HOS_DUMP_SHADER_PATH "/%s_%d.glsl", prefix, cnt++);
	util_write_text_file(name, glsl);
}
#endif

#if ENABLE_SHADER_DUMP_CB
static bool shader_dump_cb(const char *ext, const char *dump)
{
	LOG("Shader dumper CB: %s: %s", ext, dump);
	return true;
}
#define SHADER_DUMP_CB shader_dump_cb
#else
#define SHADER_DUMP_CB NULL
#endif

EXPORT(SceGxm, 0xB0F1E4EC, int, sceGxmInitialize, const SceGxmInitializeParams *params)
{
	DkQueueMaker queue_maker;
	uint32_t display_queue_num_entries;

	if (g_gxm_initialized)
		return SCE_GXM_ERROR_ALREADY_INITIALIZED;

	/* Create graphics queue */
	dkQueueMakerDefaults(&queue_maker, g_dk_device);
	queue_maker.flags = DkQueueFlags_Graphics;
	g_render_queue = dkQueueCreate(&queue_maker);

	/* Create memory block for the "notification region" */
	g_notification_region_memblock = dk_alloc_memblock(g_dk_device,
		SCE_GXM_NOTIFICATION_COUNT * sizeof(uint32_t),
		DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached);

	/* Allocate and initialize the display queue, and its worker thread */
	display_queue_num_entries = next_pow2(params->displayQueueMaxPendingCount + 1);
	g_display_queue = malloc(sizeof(DisplayQueueControlBlock) +
			         (sizeof(DisplayQueueEntry) + params->displayQueueCallbackDataSize) *
				 display_queue_num_entries);
	assert(g_display_queue);
	g_display_queue->head = 0;
	g_display_queue->tail = 0;
	g_display_queue->num_entries = display_queue_num_entries;
	g_display_queue->entries = (DisplayQueueEntry *)((char *)g_display_queue + sizeof(DisplayQueueControlBlock));
	g_display_queue->display_queue_max_pending_count = params->displayQueueMaxPendingCount;
	g_display_queue->display_queue_callback = params->displayQueueCallback;
	g_display_queue->display_queue_callback_data_size = params->displayQueueCallbackDataSize;
	g_display_queue->exit_thread = 0;
	g_display_queue->thid = sceKernelCreateThread("SceGxmDisplayQueue", SceGxmDisplayQueue_thread,
						      64, 0x1000, 0, 0, NULL);
	assert(g_display_queue->thid > 0);

	for (uint32_t i = 0; i < display_queue_num_entries; i++) {
		g_display_queue->entries[i].callback_data = (char *)g_display_queue->entries +
			(sizeof(DisplayQueueEntry) * display_queue_num_entries) +
			i * params->displayQueueCallbackDataSize;
	}

	ueventCreate(&g_display_queue->ready_evflag, true);
	ueventCreate(&g_display_queue->pending_evflag, true);

	sceKernelStartThread(g_display_queue->thid, sizeof(g_display_queue), &g_display_queue);

	/* Create memory block for the shader code */
	g_code_memblock = dk_alloc_memblock(g_dk_device, 64 * 1024,
		DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);

	g_code_mem_offset = 0;

	g_gxm_initialized = true;

	return 0;
}

EXPORT(SceGxm, 0xB627DE66, int, sceGxmTerminate)
{
	g_display_queue->exit_thread = 1;
	ueventSignal(&g_display_queue->pending_evflag);
	sceKernelWaitThreadEnd(g_display_queue->thid, NULL, NULL);

	free(g_display_queue);
	dkMemBlockDestroy(g_code_memblock);
	dkMemBlockDestroy(g_notification_region_memblock);
	dkQueueDestroy(g_render_queue);

	g_gxm_initialized = false;
	return 0;
}

EXPORT(SceGxm, 0xE84CE5B4, int, sceGxmCreateContext, const SceGxmContextParams *params, SceGxmContext **context)
{
	DkCmdBufMaker cmdbuf_maker;
	SceGxmContext *ctx = params->hostMem;

	if (params->hostMemSize < sizeof(SceGxmContext))
		return SCE_GXM_ERROR_INVALID_VALUE;

	memset(ctx, 0, sizeof(*ctx));
	ctx->params = *params;

	/* Get the passed backing storage buffer for the main command buffer */
	ctx->cmdbuf_memblock = SceSysmem_get_dk_memblock_for_addr(params->vdmRingBufferMem);
	assert(ctx->cmdbuf_memblock);

	/* Create the command buffer */
	dkCmdBufMakerDefaults(&cmdbuf_maker, g_dk_device);
	ctx->cmdbuf = dkCmdBufCreate(&cmdbuf_maker);
	assert(ctx->cmdbuf);

	/* Assing the backing storage buffer to the main command buffer */
	dkCmdBufAddMemory(ctx->cmdbuf, ctx->cmdbuf_memblock, 0, ctx->params.vdmRingBufferMemSize);

	/* Get the passed vertex ringbuffer for vertex default uniform buffer reservations */
	ctx->vertex_rb.memblock = SceSysmem_get_dk_memblock_for_addr(params->vertexRingBufferMem);
	assert(ctx->vertex_rb.memblock);
	assert(params->vertexRingBufferMem == dkMemBlockGetCpuAddr(ctx->vertex_rb.memblock));
	ctx->vertex_rb.size = params->vertexRingBufferMemSize;

	/* Get the passed fragment ringbuffer for fragment default uniform buffer reservations */
	ctx->fragment_rb.memblock =
	    SceSysmem_get_dk_memblock_for_addr(params->fragmentRingBufferMem);
	assert(ctx->fragment_rb.memblock);
	assert(params->fragmentRingBufferMem == dkMemBlockGetCpuAddr(ctx->fragment_rb.memblock));
	ctx->fragment_rb.size = params->fragmentRingBufferMemSize;

	ctx->gxm_vert_unif_block_memblock = dk_alloc_memblock(g_dk_device,
		ALIGN(sizeof(struct GXMRenderVertUniformBlock), DK_UNIFORM_BUF_ALIGNMENT),
		DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);

	ctx->gxm_frag_unif_block_memblock = dk_alloc_memblock(g_dk_device,
		ALIGN(sizeof(struct GXMRenderFragUniformBlock), DK_UNIFORM_BUF_ALIGNMENT),
		DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);

	ctx->fragment_tex_descriptor_set_memblock = dk_alloc_memblock(g_dk_device,
		(sizeof(struct DkImageDescriptor) + sizeof(DkSamplerDescriptor)) * SCE_GXM_MAX_TEXTURE_UNITS,
		DkMemBlockFlags_GpuCached);

	/* Init default state */
	memset(&ctx->state, 0, sizeof(ctx->state));

	dkRasterizerStateDefaults(&ctx->state.rasterizer);
	ctx->state.rasterizer.cullMode = DkFace_None;
	ctx->state.rasterizer.frontFace = DkFrontFace_CW;

	dkColorStateDefaults(&ctx->state.color);
	dkColorWriteStateDefaults(&ctx->state.color_write);

	ctx->state.depth_stencil.depthTestEnable = true;
	ctx->state.depth_stencil.depthWriteEnable = true;
	ctx->state.depth_stencil.stencilTestEnable = true;
	ctx->state.depth_stencil.depthCompareOp = DkCompareOp_Lequal;

	ctx->state.depth_stencil.stencilFrontFailOp = DkStencilOp_Keep;
	ctx->state.depth_stencil.stencilFrontPassOp = DkStencilOp_Keep;
	ctx->state.depth_stencil.stencilFrontDepthFailOp = DkStencilOp_Keep;
	ctx->state.depth_stencil.stencilFrontCompareOp = DkCompareOp_Always;

	ctx->state.depth_stencil.stencilBackFailOp = DkStencilOp_Keep;
	ctx->state.depth_stencil.stencilBackPassOp = DkStencilOp_Keep;
	ctx->state.depth_stencil.stencilBackDepthFailOp = DkStencilOp_Keep;
	ctx->state.depth_stencil.stencilBackCompareOp = DkCompareOp_Always;

	ctx->state.front_stencil.ref = 0;
	ctx->state.front_stencil.compare_mask = 0;
	ctx->state.front_stencil.write_mask = 0;

	ctx->state.back_stencil.ref = 0;
	ctx->state.back_stencil.compare_mask = 0;
	ctx->state.back_stencil.write_mask = 0;

	*context = ctx;

	return 0;
}

EXPORT(SceGxm, 0xEDDC5FB2, int, sceGxmDestroyContext, SceGxmContext *context)
{
	dkQueueWaitIdle(g_render_queue);
	dkMemBlockDestroy(context->gxm_vert_unif_block_memblock);
	dkMemBlockDestroy(context->gxm_frag_unif_block_memblock);
	dkMemBlockDestroy(context->fragment_tex_descriptor_set_memblock);
	dkCmdBufDestroy(context->cmdbuf);

	if (context->state.shadow_ds_surface.memblock)
		dkMemBlockDestroy(context->state.shadow_ds_surface.memblock);

	return 0;
}

EXPORT(SceGxm, 0x0733D8AE, void, sceGxmFinish, SceGxmContext *context)
{
	dkQueueWaitIdle(g_render_queue);
}

EXPORT(SceGxm, 0x8BDE825A, volatile unsigned int *, sceGxmGetNotificationRegion, void)
{
	return dkMemBlockGetCpuAddr(g_notification_region_memblock);
}

EXPORT(SceGxm, 0x6A6013E1, int, sceGxmSyncObjectCreate, SceGxmSyncObject **syncObject)
{
	SceGxmSyncObject *sync_object;

	sync_object = malloc(sizeof(*sync_object));
	if (!sync_object)
		return SCE_KERNEL_ERROR_NO_MEMORY;

	memset(sync_object, 0, sizeof(*sync_object));
	*syncObject = sync_object;

	return 0;
}

EXPORT(SceGxm, 0x889AE88C, int, sceGxmSyncObjectDestroy, SceGxmSyncObject *syncObject)
{
	free(syncObject);
	return 0;
}

EXPORT(SceGxm, 0x9F448E79, int, sceGxmNotificationWait, const SceGxmNotification *notification)
{
	DkVariable variable;
	uint32_t offset = dk_memblock_cpu_addr_offset(g_notification_region_memblock,
						      (void *)notification->address);
	assert(offset < SCE_GXM_NOTIFICATION_COUNT * sizeof(uint32_t));

	dkVariableInitialize(&variable, g_notification_region_memblock, offset);

	while (dkVariableRead(&variable) != notification->value)
		dkQueueWaitIdle(g_render_queue);

	return 0;
}

EXPORT(SceGxm, 0x05032658, int, sceGxmShaderPatcherCreate, const SceGxmShaderPatcherParams *params,
			      SceGxmShaderPatcher **shaderPatcher)
{
	SceGxmShaderPatcher *shader_patcher;

	shader_patcher = malloc(sizeof(*shader_patcher));
	if (!shader_patcher)
		return SCE_KERNEL_ERROR_NO_MEMORY;

	memset(shader_patcher, 0, sizeof(*shader_patcher));
	shader_patcher->params = *params;
	*shaderPatcher = shader_patcher;

	return 0;
}

EXPORT(SceGxm, 0xEAA5B100, int, sceGxmShaderPatcherDestroy, SceGxmShaderPatcher *shaderPatcher)
{
	free(shaderPatcher->registered_programs);
	free(shaderPatcher);
	return 0;
}

EXPORT(SceGxm, 0x2B528462, int, sceGxmShaderPatcherRegisterProgram, SceGxmShaderPatcher *shaderPatcher,
				       const SceGxmProgram *programHeader,
				       SceGxmShaderPatcherId *programId)
{
	SceGxmRegisteredProgram *shader_patcher_id;

	shader_patcher_id = malloc(sizeof(*shader_patcher_id));
	if (!shader_patcher_id)
		return SCE_KERNEL_ERROR_NO_MEMORY;

	memset(shader_patcher_id, 0, sizeof(*shader_patcher_id));
	shader_patcher_id->programHeader = programHeader;

	shaderPatcher->registered_programs = reallocarray(shaderPatcher->registered_programs,
							  shaderPatcher->registered_count + 1,
							  sizeof(SceGxmShaderPatcherId));
	shaderPatcher->registered_programs[shaderPatcher->registered_count] = shader_patcher_id;
	shaderPatcher->registered_count++;

	*programId = shader_patcher_id;

	return 0;
}

EXPORT(SceGxm, 0xF103AF8A, int, sceGxmShaderPatcherUnregisterProgram, SceGxmShaderPatcher *shaderPatcher,
					 SceGxmShaderPatcherId programId)
{
	free(programId);
	return 0;
}

static int translate_shader(DkShader *shader, const SceGxmProgram *program, pipeline_stage stage,
			    const char *prefix, DkMemBlock code_memblock, uint32_t *code_offset,
			    const SceGxmVertexAttribute *attributes, unsigned int attributeCount)
{
	bool ret;
	char *glsl;
	uint32_t shader_size;
	DkShaderMaker shader_maker;
	void *shader_load_addr = dkMemBlockGetCpuAddr(code_memblock) + *code_offset;

#if DUMP_SHADER_SPIRV
	uint32_t *spirv;
	uint32_t num_instr;

	ret = convert_gxp_to_spirv_c(&spirv, &num_instr, program, prefix,
				     false, false, false, false, attributes, attributeCount,
				     false, true, SHADER_DUMP_CB);
	if (ret) {
		dump_shader_spirv(prefix, spirv, num_instr);
		free(spirv);
	}
#endif

	LOG("Converting shader (%s) to GLSL...", prefix);
	ret = convert_gxp_to_glsl_c(&glsl, program, prefix, false, false, false,
				    false, attributes, attributeCount, false, true, SHADER_DUMP_CB);
	LOG("  ret: %d", ret);
#if DUMP_SHADER_GLSL
	if (ret)
		dump_shader_glsl(prefix, glsl);
#endif
	if (!ret)
		return SCE_GXM_ERROR_INVALID_VALUE;

	LOG("UAM compiling shader (%s)...", prefix);
	ret = uam_compiler_compile_glsl(stage, glsl, shader_load_addr, &shader_size);
	LOG("  compile ret: %d, size: 0x%x", ret, shader_size);
	free(glsl);

	dkShaderMakerDefaults(&shader_maker, code_memblock, *code_offset);
	dkShaderInitialize(shader, &shader_maker);
	*code_offset += ALIGN(shader_size, DK_SHADER_CODE_ALIGNMENT);

	return 0;
}

EXPORT(SceGxm, 0xB7BBA6D5, int, sceGxmShaderPatcherCreateVertexProgram, SceGxmShaderPatcher *shaderPatcher,
					   SceGxmShaderPatcherId programId,
					   const SceGxmVertexAttribute *attributes,
					   unsigned int attributeCount,
					   const SceGxmVertexStream *streams,
					   unsigned int streamCount,
					   SceGxmVertexProgram **vertexProgram)
{
	int ret;
	SceGxmVertexProgram *vertex_program;

	vertex_program = malloc(sizeof(*vertex_program));
	if (!vertex_program)
		return SCE_KERNEL_ERROR_NO_MEMORY;

	memset(vertex_program, 0, sizeof(*vertex_program));
	vertex_program->programId = programId;
	vertex_program->attributes = calloc(attributeCount, sizeof(SceGxmVertexAttribute));
	memcpy(vertex_program->attributes, attributes, attributeCount * sizeof(SceGxmVertexAttribute));
	vertex_program->attributeCount = attributeCount;
	vertex_program->streams = calloc(streamCount, sizeof(SceGxmVertexStream));
	memcpy(vertex_program->streams, streams, streamCount * sizeof(SceGxmVertexStream));
	vertex_program->streamCount = streamCount;

	ret = translate_shader(&vertex_program->dk_shader, programId->programHeader,
			       pipeline_stage_vertex, "vert",
			       g_code_memblock, &g_code_mem_offset,
			       attributes, attributeCount);
	if (ret != 0) {
		free(vertex_program);
		return ret;
	}

	*vertexProgram = vertex_program;

	return 0;
}

EXPORT(SceGxm, 0xAC1FF2DA, int, sceGxmShaderPatcherReleaseVertexProgram, SceGxmShaderPatcher *shaderPatcher,
					    SceGxmVertexProgram *vertexProgram)
{
	free(vertexProgram->attributes);
	free(vertexProgram->streams);
	free(vertexProgram);
	return 0;
}

EXPORT(SceGxm, 0x4ED2E49D, int, sceGxmShaderPatcherCreateFragmentProgram, SceGxmShaderPatcher *shaderPatcher,
					     SceGxmShaderPatcherId programId,
					     SceGxmOutputRegisterFormat outputFormat,
					     SceGxmMultisampleMode multisampleMode,
					     const SceGxmBlendInfo *blendInfo,
					     const SceGxmProgram *vertexProgram,
					     SceGxmFragmentProgram **fragmentProgram)
{
	int ret;
	SceGxmFragmentProgram *fragment_program;

	fragment_program = malloc(sizeof(*fragment_program));
	if (!fragment_program)
		return SCE_KERNEL_ERROR_NO_MEMORY;

	memset(fragment_program, 0, sizeof(*fragment_program));
	fragment_program->programId = programId;
	fragment_program->outputFormat = outputFormat;
	fragment_program->multisampleMode = multisampleMode;
	if (blendInfo) {
		fragment_program->blendInfo = *blendInfo;
	} else {
		fragment_program->blendInfo = (SceGxmBlendInfo){
			SCE_GXM_COLOR_MASK_ALL,
			SCE_GXM_BLEND_FUNC_NONE,
			SCE_GXM_BLEND_FUNC_NONE,
			SCE_GXM_BLEND_FACTOR_ONE,
			SCE_GXM_BLEND_FACTOR_ZERO,
			SCE_GXM_BLEND_FACTOR_ONE,
			SCE_GXM_BLEND_FACTOR_ZERO
		};
	}

	ret = translate_shader(&fragment_program->dk_shader, programId->programHeader,
			       pipeline_stage_fragment, "frag",
			       g_code_memblock, &g_code_mem_offset,
			       NULL, 0);
	if (ret != 0) {
		free(fragment_program);
		return ret;
	}

	*fragmentProgram = fragment_program;

	return 0;
}

EXPORT(SceGxm, 0xBE2743D1, int, sceGxmShaderPatcherReleaseFragmentProgram, SceGxmShaderPatcher *shaderPatcher,
					      SceGxmFragmentProgram *fragmentProgram)
{
	free(fragmentProgram);
	return 0;
}

EXPORT(SceGxm, 0xA949A803, const SceGxmProgram *, sceGxmShaderPatcherGetProgramFromId, SceGxmShaderPatcherId programId)
{
	if (programId)
		return programId->programHeader;

	return NULL;
}

EXPORT(SceGxm, 0xB291C959, int, sceGxmGetRenderTargetMemSize, const SceGxmRenderTargetParams *params, unsigned int *driverMemSize)
{
	*driverMemSize = sizeof(SceGxmRenderTarget);
	return 0;
}

EXPORT(SceGxm, 0x207AF96B, int, sceGxmCreateRenderTarget, const SceGxmRenderTargetParams *params, SceGxmRenderTarget **renderTarget)
{
	SceGxmRenderTarget *render_target;

	render_target = malloc(sizeof(*render_target));
	if (!render_target)
		return SCE_KERNEL_ERROR_NO_MEMORY;

	render_target->params = *params;
	*renderTarget = render_target;

	return 0;
}

EXPORT(SceGxm, 0x0B94C50A, int, sceGxmDestroyRenderTarget, SceGxmRenderTarget *renderTarget)
{
	free(renderTarget);
	return 0;
}

EXPORT(SceGxm, 0xED0F6E25, int, sceGxmColorSurfaceInit, SceGxmColorSurface *surface, SceGxmColorFormat colorFormat,
			   SceGxmColorSurfaceType surfaceType,
			   SceGxmColorSurfaceScaleMode scaleMode,
			   SceGxmOutputRegisterSize outputRegisterSize,
			   unsigned int width, unsigned int height,
			   unsigned int strideInPixels, void *data)
{
    return uam_gxm_init_color_surface(surface, colorFormat, surfaceType, scaleMode,
                                    outputRegisterSize, width, height, strideInPixels, data);
}

EXPORT(SceGxm, 0xCA9D41D1, int, sceGxmDepthStencilSurfaceInit, SceGxmDepthStencilSurface *surface,
				  SceGxmDepthStencilFormat depthStencilFormat,
				  SceGxmDepthStencilSurfaceType surfaceType,
				  unsigned int strideInSamples,
				  void *depthData,
				  void *stencilData)

{
	if (!surface)
		return SCE_GXM_ERROR_INVALID_POINTER;
	else if ((strideInSamples == 0) || ((strideInSamples % SCE_GXM_TILE_SIZEX) != 0))
		return SCE_GXM_ERROR_INVALID_VALUE;

	memset(surface, 0, sizeof(*surface));
	surface->zlsControl =
	    SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_DISABLED | SCE_GXM_DEPTH_STENCIL_FORCE_STORE_DISABLED |
	    ((strideInSamples >> 5) - 1) << SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_STRIDE_OFFSET |
	    (surfaceType & (SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_TYPE_MASK
			    << SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_TYPE_OFFSET)) |
	    (depthStencilFormat & (SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_FORMAT_MASK
				   << SCE_GXM_DEPTH_STENCIL_ZLS_CTRL_FORMAT_OFFSET));
	surface->depthData = depthData;
	surface->stencilData = stencilData;
	surface->backgroundDepth = 1.0f;
	surface->backgroundControl = SCE_GXM_DEPTH_STENCIL_BG_CTRL_MASK_BIT;

	return 0;
}

EXPORT(SceGxm, 0x14BD831F, void, sceGxmSetFrontDepthFunc, SceGxmContext *context, SceGxmDepthFunc depthFunc)
{
	context->state.depth_stencil.depthCompareOp = gxm_depth_func_to_dk_compare_op(depthFunc);
	context->state.dirty.bit.depth_stencil = true;

	if (!context->state.two_sided_mode)
		sceGxmSetBackDepthFunc(context, depthFunc);
}

EXPORT(SceGxm, 0xF32CBF34, void, sceGxmSetFrontDepthWriteEnable, SceGxmContext *context, SceGxmDepthWriteMode enable)
{
	context->state.depth_stencil.depthWriteEnable =
	    (enable == SCE_GXM_DEPTH_WRITE_ENABLED) ? 1 : 0;
	context->state.dirty.bit.depth_stencil = true;

	if (!context->state.two_sided_mode)
		sceGxmSetBackDepthWriteEnable(context, enable);
}

EXPORT(SceGxm, 0x8FA6FE44, void, sceGxmSetFrontStencilRef, SceGxmContext *context, unsigned int sref)
{
	context->state.front_stencil.ref = sref;
	context->state.dirty.bit.front_stencil = true;

	if (!context->state.two_sided_mode)
		sceGxmSetBackStencilRef(context, sref);
}

EXPORT(SceGxm, 0xB042A4D2, void, sceGxmSetBackDepthFunc, SceGxmContext *context, SceGxmDepthFunc depthFunc)
{
	/* Unsupported */
}

EXPORT(SceGxm, 0xC18B706B, void, sceGxmSetBackDepthWriteEnable, SceGxmContext *context, SceGxmDepthWriteMode enable)
{
	/* Unsupported */
}

EXPORT(SceGxm, 0x866A0517, void, sceGxmSetBackStencilRef, SceGxmContext *context, unsigned int sref)
{
	context->state.back_stencil.ref = sref;
	context->state.dirty.bit.back_stencil = true;
}

EXPORT(SceGxm, 0xB8645A9A, void, sceGxmSetFrontStencilFunc, SceGxmContext *context, SceGxmStencilFunc func,
			       SceGxmStencilOp stencilFail, SceGxmStencilOp depthFail,
			       SceGxmStencilOp depthPass, unsigned char compareMask,
			       unsigned char writeMask)
{

	context->state.depth_stencil.stencilFrontFailOp =
	    gxm_stencil_op_to_dk_stencil_op(stencilFail);
	context->state.depth_stencil.stencilFrontDepthFailOp =
	    gxm_stencil_op_to_dk_stencil_op(depthFail);
	context->state.depth_stencil.stencilFrontCompareOp =
	    gxm_stencil_func_to_dk_compare_op(func);
	context->state.front_stencil.compare_mask = compareMask;
	context->state.front_stencil.write_mask = writeMask;
	context->state.dirty.bit.depth_stencil = true;
	context->state.dirty.bit.front_stencil = true;

	if (!context->state.two_sided_mode) {
		sceGxmSetBackStencilFunc(context, func, stencilFail, depthFail, depthPass,
					 compareMask, writeMask);
	}
}

EXPORT(SceGxm, 0x1A68C8D2, void, sceGxmSetBackStencilFunc, SceGxmContext *context, SceGxmStencilFunc func,
			      SceGxmStencilOp stencilFail, SceGxmStencilOp depthFail,
			      SceGxmStencilOp depthPass, unsigned char compareMask,
			      unsigned char writeMask)
{
	context->state.depth_stencil.stencilBackFailOp =
	    gxm_stencil_op_to_dk_stencil_op(stencilFail);
	context->state.depth_stencil.stencilBackDepthFailOp =
	    gxm_stencil_op_to_dk_stencil_op(depthFail);
	context->state.depth_stencil.stencilBackCompareOp = gxm_stencil_func_to_dk_compare_op(func);
	context->state.back_stencil.compare_mask = compareMask;
	context->state.back_stencil.write_mask = writeMask;
	context->state.dirty.bit.depth_stencil = true;
	context->state.dirty.bit.back_stencil = true;
}

static inline void dk_image_view_for_gxm_color_surface(DkImage *image, DkImageView *view, DkMemBlock block,
                                     const SceGxmColorSurfaceInner *surface)
{
    uint32_t width, height;
    DkImageLayout layout;
    DkImageLayoutMaker layout_maker;

    uam_gxm_get_surface_dimensions((const SceGxmColorSurface *)surface, &width, &height);

    dkImageLayoutMakerDefaults(&layout_maker, g_dk_device);
    dkImageLayoutMakerSetType(&layout_maker, DkImageType_2D);
    dkImageLayoutMakerSetFlags(&layout_maker, gxm_color_surface_type_to_dk_image_flags(uam_gxm_get_surface_type((const SceGxmColorSurface *)surface)));
    dkImageLayoutMakerSetFormat(&layout_maker, gxm_color_format_to_dk_image_format(uam_gxm_get_surface_format((const SceGxmColorSurface *)surface)));
    dkImageLayoutMakerSetDimensions(&layout_maker, width, height, 1);
    dkImageLayoutInitialize(&layout, &layout_maker);

    dkImageInitialize(image, &layout, block, 0);

    dkImageViewDefaults(view, image);
    dkImageViewSetSwizzle(view, DkComponentSwizzle_R, DkComponentSwizzle_G, DkComponentSwizzle_B, DkComponentSwizzle_A);
}

static inline void dk_image_view_for_gxm_depth_stencil_surface(DkImage *image, DkImageView *view,
                                     DkMemBlock block,
                                     uint32_t width, uint32_t height,
                                     const SceGxmDepthStencilSurface *surface)
{
	DkImageLayoutMaker maker;
	DkImageLayout layout;

	dkImageLayoutMakerDefaults(&maker, g_dk_device);
	maker.flags = DkImageFlags_UsageRender;
	maker.format = DkImageFormat_Z24S8;
	maker.dimensions[0] = width;
	maker.dimensions[1] = height;
	dkImageLayoutInitialize(&layout, &maker);
	dkImageInitialize(&image, &layout, block, dk_memblock_cpu_addr_offset(block, surface->depthData));
	dkImageViewDefaults(&view, &image);
}

static void set_vita3k_gxm_uniform_blocks(SceGxmContext *context, const DkViewport *viewport)
{
	struct GXMRenderVertUniformBlock vert_unif = {
		.viewport_flip = {1.0f, 1.0f, 1.0f, 1.0f},
		.viewport_flag = (0) ? 0.0f : 1.0f,
		.z_offset = 0.0f,
		.z_scale = 1.0f,
		.screen_width = viewport->width,
		.screen_height = viewport->height
	};

	struct GXMRenderFragUniformBlock frag_unif = {
		.back_disabled = 0.0f,
		.front_disabled = 0.0f,
		.writing_mask = 0.0f,
		.use_raw_image = 0.0f,
		.res_multiplier = 0
	};

	memcpy(dkMemBlockGetCpuAddr(context->gxm_vert_unif_block_memblock), &vert_unif, sizeof(vert_unif));
	dkCmdBufBindUniformBuffer(context->cmdbuf, DkStage_Vertex, 2 /* hardcoded */,
				  dkMemBlockGetGpuAddr(context->gxm_vert_unif_block_memblock),
				  ALIGN(sizeof(vert_unif), DK_UNIFORM_BUF_ALIGNMENT));

	memcpy(dkMemBlockGetCpuAddr(context->gxm_frag_unif_block_memblock), &frag_unif, sizeof(frag_unif));
	dkCmdBufBindUniformBuffer(context->cmdbuf, DkStage_Fragment, 3 /* hardcoded */,
				  dkMemBlockGetGpuAddr(context->gxm_frag_unif_block_memblock),
				  ALIGN(sizeof(frag_unif), DK_UNIFORM_BUF_ALIGNMENT));
}

static void ensure_shadow_ds_surface(SceGxmContext *context, uint32_t width, uint32_t height)
{
	DkImageLayoutMaker maker;
	DkImageLayout layout;
	uint32_t ds_surface_size, ds_surface_align;

	dkImageLayoutMakerDefaults(&maker, g_dk_device);
	maker.flags = DkImageFlags_UsageRender;
	maker.format = DkImageFormat_Z24S8;
	maker.dimensions[0] = width;
	maker.dimensions[1] = height;
	dkImageLayoutInitialize(&layout, &maker);

	ds_surface_size  = dkImageLayoutGetSize(&layout);
	ds_surface_align = dkImageLayoutGetAlignment(&layout);
	ds_surface_size  = ALIGN(ds_surface_size, ds_surface_align);

	LOG("Creating D/S surface: %d x %d, size %d, align: %d", width, height, ds_surface_size, ds_surface_align);

	if (ds_surface_size > context->state.shadow_ds_surface.size) {
		if (context->state.shadow_ds_surface.memblock) {
			/* TODO: Wait until previous depth/stencil buffer is not used anymore before deallocating it */
			dkMemBlockDestroy(context->state.shadow_ds_surface.memblock);
		}

		context->state.shadow_ds_surface.memblock =
		    dk_alloc_memblock(g_dk_device, ds_surface_size,
				      DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached |
					  DkMemBlockFlags_Image);
	}

	dkImageInitialize(&context->state.shadow_ds_surface.image, &layout,
			  context->state.shadow_ds_surface.memblock, 0);
	dkImageViewDefaults(&context->state.shadow_ds_surface.view,
			    &context->state.shadow_ds_surface.image);

	context->state.shadow_ds_surface.width = width;
	context->state.shadow_ds_surface.height = height;
	context->state.shadow_ds_surface.size = ds_surface_size;
}

EXPORT(SceGxm, 0x8734FF4E, int, sceGxmBeginScene, SceGxmContext *context, unsigned int flags,
                     const SceGxmRenderTarget *renderTarget, const SceGxmValidRegion *validRegion,
                     SceGxmSyncObject *vertexSyncObject, SceGxmSyncObject *fragmentSyncObject,
                     const SceGxmColorSurface *colorSurface,
                     const SceGxmDepthStencilSurface *depthStencil)
{
    if (!context || !renderTarget)
        return SCE_GXM_ERROR_INVALID_POINTER;

    if (uam_gxm_is_surface_disabled(colorSurface))
        return SCE_GXM_ERROR_INVALID_VALUE;

    // Store render target and surfaces
    context->render_target = renderTarget;
    context->color_surface = colorSurface;
    context->depth_stencil_surface = depthStencil;

    // Clear command buffer
    dkCmdBufClear(context->cmdbuf);

    // Set up render targets
    DkImageView color_view;
    if (colorSurface) {
        dk_image_view_for_gxm_color_surface(&context->color_image, &color_view, context->memblock,
                                          (const SceGxmColorSurfaceInner *)colorSurface);
        dkCmdBufBindRenderTargets(context->cmdbuf, 1, &color_view, NULL);
    }

    // Set viewport and scissor
    DkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)renderTarget->width,
        .height = (float)renderTarget->height,
        .near = 0.0f,
        .far = 1.0f
    };
    dkCmdBufSetViewport(context->cmdbuf, 0, &viewport);

    DkScissor scissor = {
        .x = 0,
        .y = 0,
        .width = renderTarget->width,
        .height = renderTarget->height
    };
    dkCmdBufSetScissor(context->cmdbuf, 0, &scissor);

    return SCE_OK;
}

EXPORT(SceGxm, 0xFE300E2F, int, sceGxmEndScene, SceGxmContext *context, const SceGxmNotification *vertexNotification,
		   const SceGxmNotification *fragmentNotification)
{
	DkCmdList cmd_list;
	DkVariable variable;
	uint32_t offset;

	LOG("sceGxmEndScene");

	if (!context->state.in_scene)
		return SCE_GXM_ERROR_NOT_WITHIN_SCENE;

	if (vertexNotification) {
		offset = dk_memblock_cpu_addr_offset(g_notification_region_memblock,
						     (void *)vertexNotification->address);
		assert(offset < SCE_GXM_NOTIFICATION_COUNT * sizeof(uint32_t));

		dkVariableInitialize(&variable, g_notification_region_memblock, offset);
		dkCmdBufSignalVariable(context->cmdbuf, &variable, DkVarOp_Set,
				       vertexNotification->value, DkPipelinePos_Rasterizer);
	}

	if (fragmentNotification) {
		offset = dk_memblock_cpu_addr_offset(g_notification_region_memblock,
						     (void *)fragmentNotification->address);
		assert(offset < SCE_GXM_NOTIFICATION_COUNT * sizeof(uint32_t));

		dkVariableInitialize(&variable, g_notification_region_memblock, offset);
		dkCmdBufSignalVariable(context->cmdbuf, &variable, DkVarOp_Set,
				       fragmentNotification->value, DkPipelinePos_Bottom);
	}

	if (context->state.discard_stencil) {
		/* Wait for fragments to be completed before discarding depth/stencil buffer */
		dkCmdBufBarrier(context->cmdbuf, DkBarrier_Fragments, 0);
		/* Discard the stencil buffer since we don't need it anymore */
		dkCmdBufDiscardDepthStencil(context->cmdbuf);
	}

	cmd_list = dkCmdBufFinishList(context->cmdbuf);
	dkQueueSubmitCommands(g_render_queue, cmd_list);

	context->state.in_scene = false;

	return 0;
}

static int SceGxmDisplayQueue_thread(SceSize args, void *argp)
{
	DisplayQueueControlBlock *queue = *(DisplayQueueControlBlock **)argp;
	DisplayQueueEntry *entry;

	ueventSignal(&queue->ready_evflag);

	while (!queue->exit_thread) {
		while (CIRC_CNT(queue->head, queue->tail, queue->num_entries) > 0) {
			if (queue->exit_thread)
				break;

			/* Get the first pending framebuffer swap */
			entry = &queue->entries[queue->tail];

			/* Wait until rendering finishes */
			dkFenceWait(entry->new_fence, -1);

			/* Call the user-specified callback: this sets the new framebuffer */
			queue->display_queue_callback(entry->callback_data);

			/* Signal that the old buffer has swapped out, so it can be rendered to again */
			// dkFenceSignal(entry->old_fence);

			queue->tail = (queue->tail + 1) & (queue->num_entries - 1);
			ueventSignal(&queue->ready_evflag);
		}

		if (queue->exit_thread)
			break;

		waitSingle(waiterForUEvent(&queue->pending_evflag), -1);
	}

	return 0;
}

EXPORT(SceGxm, 0xEC5C26B5, int, sceGxmDisplayQueueAddEntry, SceGxmSyncObject *oldBuffer, SceGxmSyncObject *newBuffer, const void *callbackData)
{
	DisplayQueueControlBlock *queue = g_display_queue;

	LOG("sceGxmDisplayQueueAddEntry: old: %p, new: %p", oldBuffer, newBuffer);

	/* Signal back buffer fence when rendering finishes */
	dkQueueSignalFence(g_render_queue, &newBuffer->fence, true);
	dkQueueFlush(g_render_queue);

	/* Throttle down if we already have enough pending display queue entries */
	while (CIRC_CNT(queue->head, queue->tail, queue->num_entries) ==
	       queue->display_queue_max_pending_count) {
		waitSingle(waiterForUEvent(&queue->ready_evflag), -1);
	}

	/* Push the fences and the callback data to the display queue */
	queue->entries[queue->head].new_fence = &newBuffer->fence;
	queue->entries[queue->head].old_fence = &oldBuffer->fence;
	memcpy(queue->entries[queue->head].callback_data, callbackData,
	       queue->display_queue_callback_data_size);
	queue->head = (queue->head + 1) & (queue->num_entries - 1);

	ueventSignal(&queue->pending_evflag);

	return 0;
}

EXPORT(SceGxm, 0xB98C5B0D, int, sceGxmDisplayQueueFinish, void)
{
	DisplayQueueControlBlock *queue = g_display_queue;

	while (CIRC_CNT(queue->head, queue->tail, queue->num_entries) > 0)
		waitSingle(waiterForUEvent(&queue->ready_evflag), -1);

	return 0;
}

EXPORT(SceGxm, 0x31FF8ABD, void, sceGxmSetVertexProgram, SceGxmContext *context, const SceGxmVertexProgram *vertexProgram)
{
	DkVtxAttribState vertex_attrib_state[SCE_GXM_MAX_VERTEX_ATTRIBUTES];
	DkVtxBufferState vertex_buffer_state[SCE_GXM_MAX_VERTEX_STREAMS];
	const SceGxmVertexAttribute *attributes = vertexProgram->attributes;
	const SceGxmVertexStream *streams = vertexProgram->streams;

	for (uint32_t i = 0; i < vertexProgram->attributeCount; i++) {
		vertex_attrib_state[i].bufferId = attributes[i].streamIndex;
		vertex_attrib_state[i].isFixed = 0;
		vertex_attrib_state[i].offset = attributes[i].offset;
		vertex_attrib_state[i].size = gxm_to_dk_vtx_attrib_size(attributes[i].format,
								        attributes[i].componentCount);
		vertex_attrib_state[i].type = gxm_to_dk_vtx_attrib_type(attributes[i].format);
		vertex_attrib_state[i].isBgra = 0;
	}

	for (uint32_t i = 0; i < vertexProgram->streamCount; i++) {
		vertex_buffer_state[i].stride = streams[i].stride;
		vertex_buffer_state[i].divisor = 0;
	}

	dkCmdBufBindVtxAttribState(context->cmdbuf, vertex_attrib_state, vertexProgram->attributeCount);
	dkCmdBufBindVtxBufferState(context->cmdbuf, vertex_buffer_state, vertexProgram->streamCount);

	context->state.vertex_program = vertexProgram;
	context->state.dirty.bit.shaders = true;
}

EXPORT(SceGxm, 0xAD2F48D9, void, sceGxmSetFragmentProgram, SceGxmContext *context, const SceGxmFragmentProgram *fragmentProgram)
{
	uint32_t mask;

	mask = DkColorMask_R * !!(fragmentProgram->blendInfo.colorMask & SCE_GXM_COLOR_MASK_R) |
	       DkColorMask_G * !!(fragmentProgram->blendInfo.colorMask & SCE_GXM_COLOR_MASK_G) |
	       DkColorMask_B * !!(fragmentProgram->blendInfo.colorMask & SCE_GXM_COLOR_MASK_B) |
	       DkColorMask_A * !!(fragmentProgram->blendInfo.colorMask & SCE_GXM_COLOR_MASK_A);

	dkColorWriteStateSetMask(&context->state.color_write, 0, mask);
	context->state.dirty.bit.color_write = true;

	context->state.fragment_program = fragmentProgram;
	context->state.dirty.bit.shaders = true;
}

EXPORT(SceGxm, 0x29C34DF5, int, sceGxmSetFragmentTexture, SceGxmContext *context, unsigned int textureIndex, const SceGxmTexture *texture)
{
	context->state.fragment_textures[textureIndex] = *(SceGxmTextureInner *)texture;
	context->state.dirty.bit.fragment_textures = true;
	return 0;
}

EXPORT(SceGxm, 0x895DF2E9, int, sceGxmSetVertexStream, SceGxmContext *context, unsigned int streamIndex, const void *streamData)
{
	VitaMemBlockInfo *stream_block;
	uint32_t stream_offset;

	stream_block = SceSysmem_get_vita_memblock_info_for_addr(streamData);
	if (!stream_block)
		return SCE_GXM_ERROR_INVALID_VALUE;

	stream_offset = (uintptr_t)streamData - (uintptr_t)stream_block->base;
	dkCmdBufBindVtxBuffer(context->cmdbuf, streamIndex,
			      dkMemBlockGetGpuAddr(stream_block->dk_memblock) + stream_offset,
			      stream_block->size - stream_offset);

	return 0;
}

EXPORT(SceGxm, 0x8FA3F9C3, unsigned int, sceGxmProgramGetDefaultUniformBufferSize, const SceGxmProgram *program)
{
	return program->default_uniform_buffer_count * sizeof(float);
}

EXPORT(SceGxm, 0xC697CAE5, int, sceGxmSetVertexDefaultUniformBuffer, SceGxmContext *context,
       const void *uniformBuffer)
{
	VitaMemBlockInfo *block;
	uint32_t offset;

	block = SceSysmem_get_vita_memblock_info_for_addr(uniformBuffer);
	if (!block)
		return SCE_GXM_ERROR_INVALID_VALUE;

	offset = (uintptr_t)uniformBuffer - (uintptr_t)block->base;

	context->state.vertex_default_uniform.cpu_addr =
	    dkMemBlockGetCpuAddr(block->dk_memblock) + offset;
	context->state.vertex_default_uniform.gpu_addr =
	    dkMemBlockGetGpuAddr(block->dk_memblock) + offset;
	context->state.dirty.bit.vertex_default_uniform = true;

	return 0;
}

EXPORT(SceGxm, 0x97118913, int, sceGxmReserveVertexDefaultUniformBuffer, SceGxmContext *context, void **uniformBuffer)
{
	const SceGxmProgram *program;
	uint32_t size;

	if (!context->state.in_scene)
		return SCE_GXM_ERROR_NOT_WITHIN_SCENE;
	else if (!context->state.vertex_program)
		return SCE_GXM_ERROR_NULL_PROGRAM;

	program = context->state.vertex_program->programId->programHeader;
	size = program->default_uniform_buffer_count * sizeof(float);
	if (size == 0) {
		*uniformBuffer = NULL;
		return 0;
	}

	if (context->state.vertex_default_uniform.allocated) {
		*uniformBuffer = context->state.vertex_default_uniform.cpu_addr;
		return 0;
	}

	if (context->state.vertex_rb.head + size > context->vertex_rb.size)
		context->state.vertex_rb.head = 0;

	*uniformBuffer = context->state.vertex_default_uniform.cpu_addr =
	    dkMemBlockGetCpuAddr(context->vertex_rb.memblock) + context->state.vertex_rb.head;
	context->state.vertex_default_uniform.gpu_addr =
	    dkMemBlockGetGpuAddr(context->vertex_rb.memblock) + context->state.vertex_rb.head;

	context->state.vertex_rb.head += size;
	context->state.vertex_default_uniform.allocated = true;
	context->state.dirty.bit.vertex_default_uniform = true;

	return 0;
}

EXPORT(SceGxm, 0xA824EB24, int, sceGxmSetFragmentDefaultUniformBuffer, SceGxmContext *context,
       const void *uniformBuffer)
{
	VitaMemBlockInfo *block;
	uint32_t offset;

	block = SceSysmem_get_vita_memblock_info_for_addr(uniformBuffer);
	if (!block)
		return SCE_GXM_ERROR_INVALID_VALUE;

	offset = (uintptr_t)uniformBuffer - (uintptr_t)block->base;

	context->state.fragment_default_uniform.cpu_addr =
	    dkMemBlockGetCpuAddr(block->dk_memblock) + offset;
	context->state.fragment_default_uniform.gpu_addr =
	    dkMemBlockGetGpuAddr(block->dk_memblock) + offset;
	context->state.dirty.bit.fragment_default_uniform = true;

	return 0;
}

EXPORT(SceGxm, 0x7B1FABB6, int, sceGxmReserveFragmentDefaultUniformBuffer, SceGxmContext *context, void **uniformBuffer)
{
	const SceGxmProgram *program;
	uint32_t size;

	if (!context->state.in_scene)
		return SCE_GXM_ERROR_NOT_WITHIN_SCENE;
	else if (!context->state.fragment_program)
		return SCE_GXM_ERROR_NULL_PROGRAM;

	program = context->state.fragment_program->programId->programHeader;
	size = program->default_uniform_buffer_count * sizeof(float);
	if (size == 0) {
		*uniformBuffer = NULL;
		return 0;
	}

	if (context->state.fragment_default_uniform.allocated) {
		*uniformBuffer = context->state.fragment_default_uniform.cpu_addr;
		return 0;
	}

	if (context->state.fragment_rb.head + size > context->fragment_rb.size)
		context->state.fragment_rb.head = 0;

	*uniformBuffer = context->state.fragment_default_uniform.cpu_addr =
	    dkMemBlockGetCpuAddr(context->fragment_rb.memblock) + context->state.fragment_rb.head;
	context->state.fragment_default_uniform.gpu_addr =
	    dkMemBlockGetGpuAddr(context->fragment_rb.memblock) + context->state.fragment_rb.head;

	context->state.fragment_rb.head += size;
	context->state.fragment_default_uniform.allocated = true;
	context->state.dirty.bit.fragment_default_uniform = true;

	return 0;
}

EXPORT(SceGxm, 0x65DD0C84, int, sceGxmSetUniformDataF, void *uniformBuffer, const SceGxmProgramParameter *parameter,
			  unsigned int componentOffset, unsigned int componentCount, const float *sourceData)
{
	const bool is_float = (parameter->type == SCE_GXM_PARAMETER_TYPE_F16) || (parameter->type == SCE_GXM_PARAMETER_TYPE_F32);

	if (!uniformBuffer || !sourceData)
		return SCE_GXM_ERROR_INVALID_POINTER;

	uint32_t alignment;
	if (is_float && (parameter->array_size > 1) && (parameter->component_count > 1))
		alignment = 8;
	else
		alignment = 4;

	uint32_t scalar_size = gxm_parameter_type_size(parameter->type);
	uint32_t vector_size = scalar_size * parameter->component_count;
	uint32_t vector_index = componentOffset / parameter->component_count;
	uint32_t scalar_index = componentOffset % parameter->component_count;
	uint32_t vector_mod_align = vector_size % alignment;
	uint32_t vector_padding_bytes = vector_mod_align ? alignment - vector_mod_align : 0;

	uint32_t offset = parameter->resource_index * sizeof(uint32_t) +
					  vector_index * (vector_size + vector_padding_bytes) +
					  scalar_index * scalar_size;

	/* Only floats are supported for now */
	assert(parameter->type == SCE_GXM_PARAMETER_TYPE_F32);

	uint32_t scalars_to_copy = MIN2(parameter->component_count - scalar_index, componentCount);

	while (componentCount > 0) {
		memcpy((char *)uniformBuffer + offset, sourceData, scalars_to_copy * scalar_size);

		offset += scalars_to_copy * scalar_size + vector_padding_bytes;
		sourceData += scalars_to_copy;

		componentCount -= scalars_to_copy;
		scalars_to_copy = MIN2(parameter->component_count, componentCount);
	}

	return 0;
}

static int init_texture_base(SceGxmTextureInner *texture, const void *data, SceGxmTextureFormat tex_format,
			     uint32_t width, uint32_t height, uint32_t mipCount, SceGxmTextureType texture_type)
{
	if (width > 4096 || height > 4096 || mipCount > 13)
		return SCE_GXM_ERROR_INVALID_VALUE;

	texture->mip_count = MIN2(15, mipCount - 1);
	texture->format0 = (tex_format & 0x80000000) >> 31;
	texture->lod_bias = 31;

	if ((texture_type == SCE_GXM_TEXTURE_SWIZZLED) || (texture_type == SCE_GXM_TEXTURE_CUBE)) {
		texture->uaddr_mode = texture->vaddr_mode = SCE_GXM_TEXTURE_ADDR_MIRROR;
		texture->height_base2 = highest_set_bit(height);
		texture->width_base2 = highest_set_bit(width);
	} else {
		texture->uaddr_mode = texture->vaddr_mode = SCE_GXM_TEXTURE_ADDR_CLAMP;
		texture->height = height - 1;
		texture->width = width - 1;
	}

	texture->base_format = (tex_format & 0x1F000000) >> 24;
	texture->type = texture_type >> 29;
	texture->data_addr = (uint32_t)data >> 2;
	texture->swizzle_format = (tex_format & 0x7000) >> 12;
	texture->normalize_mode = 1;
	texture->min_filter = SCE_GXM_TEXTURE_FILTER_POINT;
	texture->mag_filter = SCE_GXM_TEXTURE_FILTER_POINT;

	return 0;
}

EXPORT(SceGxm, 0x4811AECB, int, sceGxmTextureInitLinear, SceGxmTexture *texture, const void *data,
			    SceGxmTextureFormat texFormat,
			    unsigned int width, unsigned int height,
			    unsigned int mipCount)
{
	if (!texture)
		return SCE_GXM_ERROR_INVALID_POINTER;

	return init_texture_base((SceGxmTextureInner *)texture, data,
				 texFormat, width, height, mipCount,
				 SCE_GXM_TEXTURE_LINEAR);
}

EXPORT(SceGxm, 0x5341BD46, void *, sceGxmTextureGetData, const SceGxmTexture *texture)
{
	return (void *)(((SceGxmTextureInner *)texture)->data_addr << 2);
}

EXPORT(SceGxm, 0xE868D2B3, SceGxmTextureFormat, sceGxmTextureGetFormat, const SceGxmTexture *texture)
{
	return gxm_texture_get_format((SceGxmTextureInner *)texture);
}

EXPORT(SceGxm, 0xF23FCE81, SceGxmTextureGammaMode, sceGxmTextureGetGammaMode, const SceGxmTexture *texture)
{
	return ((SceGxmTextureInner *)texture)->gamma_mode << 27;
}

EXPORT(SceGxm, 0x5420A086, uint32_t, sceGxmTextureGetHeight, const SceGxmTexture *texture)
{
	return gxm_texture_get_height((SceGxmTextureInner *)texture);
}

EXPORT(SceGxm, 0x2DE55DA5, uint32_t, sceGxmTextureGetLodBias, const SceGxmTexture *texture)
{
	SceGxmTextureInner *inner = (SceGxmTextureInner *)texture;

	if (!texture)
		return SCE_GXM_ERROR_INVALID_POINTER;

	if (gxm_texture_get_type(inner) == SCE_GXM_TEXTURE_LINEAR_STRIDED)
		return 0;

	return inner->lod_bias;
}

EXPORT(SceGxm, 0xBE524A2C, uint32_t, sceGxmTextureGetLodMin, const SceGxmTexture *texture)
{
	SceGxmTextureInner *inner = (SceGxmTextureInner *)texture;

	if (!texture)
		return SCE_GXM_ERROR_INVALID_POINTER;

	if (gxm_texture_get_type(inner) == SCE_GXM_TEXTURE_LINEAR_STRIDED)
		return 0;

	return inner->lod_min0 | (inner->lod_min1 << 2);
}

EXPORT(SceGxm, 0xAE7FBB51, SceGxmTextureFilter, sceGxmTextureGetMagFilter, const SceGxmTexture *texture)
{
	return ((SceGxmTextureInner *)texture)->mag_filter;
}

EXPORT(SceGxm, 0x920666C6, SceGxmTextureFilter, sceGxmTextureGetMinFilter, const SceGxmTexture *texture)
{
	SceGxmTextureInner *inner = (SceGxmTextureInner *)texture;

	if (gxm_texture_get_type(inner) == SCE_GXM_TEXTURE_LINEAR_STRIDED)
		return inner->mag_filter;
	return inner->min_filter;
}

EXPORT(SceGxm, 0xCE94CA15, SceGxmTextureMipFilter, sceGxmTextureGetMipFilter, const SceGxmTexture *texture)
{
	SceGxmTextureInner *inner = (SceGxmTextureInner *)texture;

	if (gxm_texture_get_type(inner) == SCE_GXM_TEXTURE_LINEAR_STRIDED)
		return SCE_GXM_TEXTURE_MIP_FILTER_DISABLED;
	return inner->mip_filter ? SCE_GXM_TEXTURE_MIP_FILTER_ENABLED :
				   SCE_GXM_TEXTURE_MIP_FILTER_DISABLED;
}

EXPORT(SceGxm, 0xF7B7B1E4, uint32_t, sceGxmTextureGetMipmapCount, const SceGxmTexture *texture)
{
	SceGxmTextureInner *inner = (SceGxmTextureInner *)texture;

	if (gxm_texture_get_type(inner) == SCE_GXM_TEXTURE_LINEAR_STRIDED)
		return 0;
	return (inner->mip_count + 1) & 0xf;
}

EXPORT(SceGxm, 0x4CC42929, uint32_t, sceGxmTextureGetMipmapCountUnsafe, const SceGxmTexture *texture)
{
	return (((SceGxmTextureInner *)texture)->mip_count + 1) & 0xf;
}

EXPORT(SceGxm, 0x512BB86C, int, sceGxmTextureGetNormalizeMode, const SceGxmTexture *texture)
{
	return ((SceGxmTextureInner *)texture)->normalize_mode << 31;
}

EXPORT(SceGxm, 0x0D189C30, void *, sceGxmTextureGetPalette, const SceGxmTexture *texture)
{
	SceGxmTextureBaseFormat base_format = gxm_texture_get_base_format(sceGxmTextureGetFormat(texture));

	return gxm_base_format_is_paletted_format(base_format) ? (void *)(texture->palette_addr << 6) : NULL;
}

EXPORT(SceGxm, 0xB0BD52F3, uint32_t, sceGxmTextureGetStride, const SceGxmTexture *texture)
{
	SceGxmTextureInner *inner = (SceGxmTextureInner *)texture;

	if (gxm_texture_get_type(inner) != SCE_GXM_TEXTURE_LINEAR_STRIDED)
		return 0;
	return gxm_texture_get_stride_in_bytes(inner);
}

EXPORT(SceGxm, 0xF65D4917, SceGxmTextureType, sceGxmTextureGetType, const SceGxmTexture *texture)
{
	return gxm_texture_get_type((SceGxmTextureInner *)texture);
}

EXPORT(SceGxm, 0x2AE22788, SceGxmTextureAddrMode, sceGxmTextureGetUAddrMode, const SceGxmTexture *texture)
{
	return ((SceGxmTextureInner *)texture)->uaddr_mode;
}

EXPORT(SceGxm, 0xC037DA83, int, sceGxmTextureGetUAddrModeSafe, const SceGxmTexture *texture)
{
	SceGxmTextureInner *inner = (SceGxmTextureInner *)texture;

	if (gxm_texture_get_type(inner) == SCE_GXM_TEXTURE_LINEAR_STRIDED)
		return SCE_GXM_TEXTURE_ADDR_CLAMP;
	return inner->uaddr_mode;
}

EXPORT(SceGxm, 0x46136CA9, SceGxmTextureAddrMode, sceGxmTextureGetVAddrMode,
       const SceGxmTexture *texture)
{
	return ((SceGxmTextureInner *)texture)->vaddr_mode;
}

EXPORT(SceGxm, 0xD2F0D9C1, int, sceGxmTextureGetVAddrModeSafe, const SceGxmTexture *texture)
{
	SceGxmTextureInner *inner = (SceGxmTextureInner *)texture;

	if (gxm_texture_get_type(inner) == SCE_GXM_TEXTURE_LINEAR_STRIDED)
		return SCE_GXM_TEXTURE_ADDR_CLAMP;
	return inner->vaddr_mode;
}

EXPORT(SceGxm, 0x126A3EB3, uint32_t, sceGxmTextureGetWidth, const SceGxmTexture *texture)
{
	return gxm_texture_get_width((SceGxmTextureInner *)texture);
}

EXPORT(SceGxm, 0x277794C4, const SceGxmProgramParameter *, sceGxmProgramFindParameterByName,
       const SceGxmProgram *program, const char *name)
{
	const uint8_t *parameter_bytes;
	const char *parameter_name;
	const SceGxmProgramParameter *const parameters =
	    (const SceGxmProgramParameter *)((char *)&program->parameters_offset +
					     program->parameters_offset);

	for (uint32_t i = 0; i < program->parameter_count; i++) {
		parameter_bytes = (const uint8_t *)&parameters[i];
		parameter_name = (const char *)(parameter_bytes + parameters[i].name_offset);
		if (strcmp(parameter_name, name) == 0)
			return (const SceGxmProgramParameter *)parameter_bytes;
	}

	return NULL;
}

EXPORT(SceGxm, 0xDBA8D061, uint32_t, sceGxmProgramParameterGetArraySize,
       const SceGxmProgramParameter *parameter)
{
	return parameter->array_size;
}

EXPORT(SceGxm, 0x1997DC17, SceGxmParameterCategory, sceGxmProgramParameterGetCategory,
       const SceGxmProgramParameter *parameter)
{
	return parameter->category;
}

EXPORT(SceGxm, 0xBD2998D1, uint32_t, sceGxmProgramParameterGetComponentCount,
       const SceGxmProgramParameter *parameter)
{
	return parameter->component_count;
}

EXPORT(SceGxm, 0xBB58267D, uint32_t, sceGxmProgramParameterGetContainerIndex,
       const SceGxmProgramParameter *parameter)
{
	return parameter->container_index;
}

EXPORT(SceGxm, 0x6E61DDF5, uint32_t, sceGxmProgramParameterGetIndex, const SceGxmProgram *program,
       const SceGxmProgramParameter *parameter)
{
	uint32_t parameter_offset = program->parameters_offset;

	if (parameter_offset > 0)
		parameter_offset += (uintptr_t)&program->parameters_offset;
	return (uint32_t)((uintptr_t)parameter - parameter_offset) >> 4;
}

EXPORT(SceGxm, 0x6AF88A5D, const char *, sceGxmProgramParameterGetName,
       const SceGxmProgramParameter *parameter)
{
	if (!parameter)
		return NULL;
	return (const char *)((uintptr_t)parameter + parameter->name_offset);
}

EXPORT(SceGxm, 0x5C79D59A, uint32_t, sceGxmProgramParameterGetResourceIndex,
       const SceGxmProgramParameter *parameter)
{
	return parameter->resource_index;
}

EXPORT(SceGxm, 0xE6D9C4CE, SceGxmParameterSemantic, sceGxmProgramParameterGetSemantic,
       const SceGxmProgramParameter *parameter)
{
	if (parameter->category != SCE_GXM_PARAMETER_CATEGORY_ATTRIBUTE)
		return SCE_GXM_PARAMETER_SEMANTIC_NONE;

	return parameter->semantic;
}

EXPORT(SceGxm, 0xB85CC13E, uint32_t, sceGxmProgramParameterGetSemanticIndex,
       const SceGxmProgramParameter *parameter)
{
	return parameter->semantic_index & 0xf;
}

EXPORT(SceGxm, 0x7B9023C3, SceGxmParameterType, sceGxmProgramParameterGetType,
       const SceGxmProgramParameter *parameter)
{
	return parameter->type;
}

static void upload_fragment_texture_descriptors(SceGxmContext *context)
{
	VitaMemBlockInfo *tex_block;
	void *tex_data;
	DkSampler sampler;
	DkImageLayoutMaker image_layout_maker;
	DkImageLayout image_layout;
	DkImage image;
	DkImageView image_view;
	DkGpuAddr desc_addr;
	struct {
		DkImageDescriptor images[SCE_GXM_MAX_TEXTURE_UNITS];
		DkSamplerDescriptor samplers[SCE_GXM_MAX_TEXTURE_UNITS];
	} descriptors;

	for (int i = 0; i < SCE_GXM_MAX_TEXTURE_UNITS; i++) {
		const SceGxmTextureInner *texture = &context->state.fragment_textures[i];
		if (!texture->data_addr)
			continue;

		tex_data = (void *)(texture->data_addr << 2);
		tex_block = SceSysmem_get_vita_memblock_info_for_addr(tex_data);
		if (!tex_block)
			continue;

		dkSamplerDefaults(&sampler);
		sampler.wrapMode[0] = gxm_texture_addr_mode_to_dk_wrap_mode(texture->uaddr_mode);
		sampler.wrapMode[1] = gxm_texture_addr_mode_to_dk_wrap_mode(texture->vaddr_mode);
		sampler.minFilter = gxm_texture_filter_to_dk_filter(texture->min_filter);
		sampler.magFilter = gxm_texture_filter_to_dk_filter(texture->mag_filter);
		dkSamplerDescriptorInitialize(&descriptors.samplers[i], &sampler);

		dkImageLayoutMakerDefaults(&image_layout_maker, g_dk_device);
		image_layout_maker.flags = DkImageFlags_PitchLinear;
		image_layout_maker.type = DkImageType_2D;
		image_layout_maker.format = DkImageFormat_RGBA8_Unorm;
		image_layout_maker.dimensions[0] = gxm_texture_get_width(texture);
		image_layout_maker.dimensions[1] = gxm_texture_get_height(texture);
		image_layout_maker.pitchStride =
		    gxm_texture_get_width(texture) *
		    gxm_color_format_bytes_per_pixel(gxm_texture_get_format(texture));
		dkImageLayoutInitialize(&image_layout, &image_layout_maker);

		dkImageInitialize(&image, &image_layout, tex_block->dk_memblock,
				  dk_memblock_cpu_addr_offset(tex_block->dk_memblock, tex_data));
		dkImageViewDefaults(&image_view, &image);
		dkImageDescriptorInitialize(&descriptors.images[i], &image_view, false, false);
		dkCmdBufBindTexture(context->cmdbuf, DkStage_Fragment, i,
				    dkMakeTextureHandle(i, i));
	}

	desc_addr = dkMemBlockGetGpuAddr(context->fragment_tex_descriptor_set_memblock);
	dkCmdBufPushData(context->cmdbuf, desc_addr, &descriptors, sizeof(descriptors));
	dkCmdBufBindImageDescriptorSet(context->cmdbuf, desc_addr, SCE_GXM_MAX_TEXTURE_UNITS);
	dkCmdBufBindSamplerDescriptorSet(context->cmdbuf,
					 desc_addr + SCE_GXM_MAX_TEXTURE_UNITS * sizeof(DkImageDescriptor),
					 SCE_GXM_MAX_TEXTURE_UNITS);
}

static void context_flush_dirty_state(SceGxmContext *context)
{
	const DkShader *shaders[2];
	const SceGxmVertexProgram *vertex_program = context->state.vertex_program;
	const SceGxmFragmentProgram *fragment_program = context->state.fragment_program;

	if (context->state.dirty.bit.shaders) {
		shaders[0] = &vertex_program->dk_shader;
		shaders[1] = &fragment_program->dk_shader;
		dkCmdBufBindShaders(context->cmdbuf, DkStageFlag_GraphicsMask, shaders,
				    ARRAY_SIZE(shaders));
		context->state.dirty.bit.shaders = false;
	}

	if (context->state.dirty.bit.depth_stencil) {
		dkCmdBufBindDepthStencilState(context->cmdbuf, &context->state.depth_stencil);
		context->state.dirty.bit.depth_stencil = false;
	}

	if (context->state.dirty.bit.front_stencil) {
		dkCmdBufSetStencil(
		    context->cmdbuf, DkFace_Front, context->state.front_stencil.write_mask,
		    context->state.front_stencil.ref, context->state.front_stencil.compare_mask);
		context->state.dirty.bit.front_stencil = false;
	}

	if (context->state.dirty.bit.back_stencil) {
		dkCmdBufSetStencil(
		    context->cmdbuf, DkFace_Back, context->state.back_stencil.write_mask,
		    context->state.back_stencil.ref, context->state.back_stencil.compare_mask);
		context->state.dirty.bit.back_stencil = false;
	}

	if (context->state.dirty.bit.color_write) {
		dkCmdBufBindColorWriteState(context->cmdbuf, &context->state.color_write);
		context->state.dirty.bit.color_write = false;
	}

	if (context->state.dirty.bit.fragment_textures) {
		upload_fragment_texture_descriptors(context);
		context->state.dirty.bit.fragment_textures = false;
	}

	if (context->state.dirty.bit.vertex_default_uniform) {
		dkCmdBufBindStorageBuffer(
		    context->cmdbuf, DkStage_Vertex, 0,
		    context->state.vertex_default_uniform.gpu_addr,
		    vertex_program->programId->programHeader->default_uniform_buffer_count *
			sizeof(float));

		context->state.vertex_default_uniform.allocated = false;
		context->state.dirty.bit.vertex_default_uniform = false;
	}

	if (context->state.dirty.bit.fragment_default_uniform) {
		dkCmdBufBindStorageBuffer(
		    context->cmdbuf, DkStage_Fragment, 1,
		    context->state.fragment_default_uniform.gpu_addr,
		    fragment_program->programId->programHeader->default_uniform_buffer_count *
			sizeof(float));

		context->state.fragment_default_uniform.allocated = false;
		context->state.dirty.bit.fragment_default_uniform = false;
	}
}

EXPORT(SceGxm, 0xBC059AFC, int, sceGxmDraw, SceGxmContext *context, SceGxmPrimitiveType primType, SceGxmIndexFormat indexType,
	       const void *indexData, unsigned int indexCount)
{
	VitaMemBlockInfo *index_block;
	uint32_t index_offset;

	LOG("sceGxmDraw: primType: 0x%x, indexCount: %d", primType, indexCount);

	index_block = SceSysmem_get_vita_memblock_info_for_addr(indexData);
	if (!index_block)
		return SCE_GXM_ERROR_INVALID_VALUE;

	context_flush_dirty_state(context);

	index_offset = (uintptr_t)indexData - (uintptr_t)index_block->base;
	dkCmdBufBindIdxBuffer(context->cmdbuf, gxm_to_dk_idx_format(indexType),
			      dkMemBlockGetGpuAddr(index_block->dk_memblock) + index_offset);
	dkCmdBufDrawIndexed(context->cmdbuf, gxm_to_dk_primitive(primType), indexCount, 1, 0, 0, 0);

	return 0;
}

EXPORT(SceGxm, 0xC61E34FC, int, sceGxmMapMemory, void *base, SceSize size, SceGxmMemoryAttribFlags attr)
{
	VitaMemBlockInfo *block;

	/* By default, SceSysmem maps all the allocated blocks to the GPU.
	 * Here we just check if it exists */
	block = SceSysmem_get_vita_memblock_info_for_addr(base);
	if (!block)
		return SCE_GXM_ERROR_INVALID_POINTER;

	return 0;
}

EXPORT(SceGxm, 0x828C68E8, int, sceGxmUnmapMemory, void *base)
{
	VitaMemBlockInfo *block = SceSysmem_get_vita_memblock_info_for_addr(base);
	if (!block)
		return SCE_GXM_ERROR_INVALID_POINTER;

	return 0;
}

EXPORT(SceGxm, 0xFA437510, int, sceGxmMapVertexUsseMemory, void *base, SceSize size, unsigned int *offset)
{
	return 0;
}

EXPORT(SceGxm, 0x099134F5, int, sceGxmUnmapVertexUsseMemory, void *base)
{
	return 0;
}

EXPORT(SceGxm, 0x008402C6, int, sceGxmMapFragmentUsseMemory, void *base, SceSize size, unsigned int *offset)
{
	return 0;
}

EXPORT(SceGxm, 0x80CCEDBB, int, sceGxmUnmapFragmentUsseMemor, void *base)
{
	return 0;
}

DECLARE_LIBRARY(SceGxm, 0xf76b66bd);

int SceGxm_init(DkDevice dk_device)
{
	g_dk_device = dk_device;
	return 0;
}

int SceGxm_finish(void)
{
	return 0;
}

// C wrapper functions for C++ color surface functions
int uam_gxm_init_color_surface(SceGxmColorSurface *surface,
                           SceGxmColorFormat colorFormat,
                           SceGxmColorSurfaceType surfaceType,
                           SceGxmColorSurfaceScaleMode scaleMode,
                           SceGxmOutputRegisterSize outputRegisterSize,
                           uint32_t width,
                           uint32_t height,
                           uint32_t strideInPixels,
                           void *data) {
    return gxm_init_color_surface(surface, colorFormat, surfaceType, scaleMode,
                                outputRegisterSize, width, height, strideInPixels, data);
}

void* uam_gxm_get_surface_data(const SceGxmColorSurface *surface) {
    return gxm_get_surface_data(surface);
}

SceGxmColorFormat uam_gxm_get_surface_format(const SceGxmColorSurface *surface) {
    return gxm_get_surface_format(surface);
}

SceGxmColorSurfaceType uam_gxm_get_surface_type(const SceGxmColorSurface *surface) {
    return gxm_get_surface_type(surface);
}

bool uam_gxm_is_surface_disabled(const SceGxmColorSurface *surface) {
    return gxm_is_surface_disabled(surface);
}

void uam_gxm_get_surface_dimensions(const SceGxmColorSurface *surface,
                                uint32_t *width, uint32_t *height) {
    gxm_get_surface_dimensions(surface, width, height);
}
