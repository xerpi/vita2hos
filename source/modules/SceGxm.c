#include <stdlib.h>
#include <switch.h>
#include <deko3d.h>
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/gxm.h>
#include "SceGxm.h"
#include "SceSysmem.h"
#include "dk_helpers.h"
#include "circ_buf.h"
#include "vita_to_dk.h"
#include "module.h"
#include "protected_bitset.h"
#include "util.h"
#include "log.h"
#include "vita3k_shader_recompiler_iface_c.h"
#include "uam_compiler_iface_c.h"

#include "basic_vsh_dksh.h"
#include "color_fsh_dksh.h"

#define DUMP_SHADER_SPIRV	1
#define DUMP_SHADER_GLSL	1
#define ENABLE_SHADER_DUMP_CB	0

#define SCE_GXM_NOTIFICATION_COUNT	512

#define MAX_GXM_MAPPED_MEMORY_BLOCKS	256

typedef struct SceGxmContext {
	SceGxmContextParams params;
	DkMemBlock cmdbuf_memblock;
	DkCmdBuf cmdbuf;
	DkMemBlock vertex_rb_memblock;
	DkMemBlock fragment_rb_memblock;
	DkMemBlock gxm_vert_unif_block_memblock;
	DkMemBlock gxm_frag_unif_block_memblock;
	/* State */
	struct {
		struct {
			uint32_t head;
			uint32_t tail;
			uint32_t size;
		} vertex_rb, fragment_rb;
		const SceGxmVertexProgram *vertex_program;
		const SceGxmFragmentProgram *fragment_program;
		bool in_scene;
		DkRasterizerState rasterizer_state;
		DkColorState color_state;
		DkColorWriteState color_write_state;
		DkDepthStencilState depth_stencil_state;
		struct {
			uint8_t ref;
			uint8_t compare_mask;
			uint8_t write_mask;
		} front_stencil_state, back_stencil_state;
		union {
			struct {
				uint32_t shaders : 1;
				uint32_t depth_stencil : 1;
				uint32_t front_stencil : 1;
				uint32_t back_stencil : 1;
			} bit;
			uint32_t raw;
		} dirty;
	};
} SceGxmContext;
static_assert(sizeof(SceGxmContext) <= SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE, "Oversized SceGxmContext");

typedef struct SceGxmSyncObject {
	DkFence fence;
} SceGxmSyncObject;

typedef struct SceGxmRegisteredProgram {
	const SceGxmProgram *programHeader;
} SceGxmRegisteredProgram;

typedef struct SceGxmShaderPatcher {
	SceGxmShaderPatcherParams params;
	SceGxmShaderPatcherId *registered_programs;
	uint32_t registered_count;
} SceGxmShaderPatcher;

typedef struct SceGxmVertexProgram {
	SceGxmShaderPatcherId programId;
	SceGxmVertexAttribute *attributes;
	unsigned int attributeCount;
	SceGxmVertexStream *streams;
	unsigned int streamCount;
	DkShader dk_shader;
} SceGxmVertexProgram;

typedef struct SceGxmFragmentProgram {
	SceGxmShaderPatcherId programId;
	SceGxmOutputRegisterFormat outputFormat;
	SceGxmMultisampleMode multisampleMode;
	SceGxmBlendInfo blendInfo;
	DkShader dk_shader;
} SceGxmFragmentProgram;

typedef struct SceGxmRenderTarget {
	SceGxmRenderTargetParams params;
} SceGxmRenderTarget;

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

	uint8_t major_version; //min 1
	uint8_t minor_version; //min 4
	uint16_t sdk_version; // 0x350 - 3.50

	uint32_t size; //size of file - ignoring padding bytes at the end after SceGxmProgramParameter table

	uint32_t binary_guid;
	uint32_t source_guid;

	uint32_t program_flags;

	uint32_t buffer_flags; // Buffer flags. 2 bits per buffer. 0x1 - loaded into registers. 0x2 - read from memory

	uint32_t texunit_flags[2]; // Tex unit flags. 4 bits per tex unit. 0x1 is non dependent read, 0x2 is dependent.

	uint32_t parameter_count;
	uint32_t parameters_offset; // Number of bytes from the start of this field to the first parameter.
	uint32_t varyings_offset; // offset to vertex outputs / fragment inputs, relative to this field

	uint16_t primary_reg_count; // (PAs)
	uint16_t secondary_reg_count; // (SAs)
	uint32_t temp_reg_count1;
	uint16_t temp_reg_count2; //Temp reg count in selective rate(programmable blending) phase

	uint16_t primary_program_phase_count;
	uint32_t primary_program_instr_count;
	uint32_t primary_program_offset;

	uint32_t secondary_program_instr_count;
	uint32_t secondary_program_offset; // relative to the beginning of this field
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
		uint16_t type : 4; // SceGxmParameterType - applicable for constants, not applicable for samplers (select type like float, half, fixed ...)
		uint16_t component_count : 4; // applicable for constants, not applicable for samplers (select size like float2, float3, float3 ...)
		uint16_t container_index : 4; // applicable for constants, not applicable for samplers (buffer, default, texture)
	};
	uint8_t semantic; // applicable only for for vertex attributes, for everything else it's 0
	uint8_t semantic_index;
	uint32_t array_size;
	int32_t resource_index;
} SceGxmProgramParameter;

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
};

struct GXMRenderFragUniformBlock {
	float back_disabled;
	float front_disabled;
	float writing_mask;
};

/* Global state */

static bool g_gxm_initialized;

/* Deko3D */

#define CODEMEMSIZE	(64*1024)
#define CMDMEMSIZE	(16*1024)
#define DYNCMDMEMSIZE	(128*1024*1024)
#define DATAMEMSIZE	(1*1024*1024)

static DkDevice g_dk_device;
static DkQueue g_render_queue;
static DkMemBlock g_notification_region_memblock;
static DisplayQueueControlBlock *g_display_queue;
static DkMemBlock g_code_memblock;
static uint32_t g_code_mem_offset;
static DkShader g_vertexShader;
static DkShader g_fragmentShader;

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

static void load_shader_memory(DkMemBlock memblock, DkShader *shader, uint32_t *offset, const void *data, uint32_t size)
{
	DkShaderMaker shader_maker;

	memcpy((char *)dkMemBlockGetCpuAddr(g_code_memblock) + *offset, data, size);
	dkShaderMakerDefaults(&shader_maker, memblock, *offset);
	dkShaderInitialize(shader, &shader_maker);

	*offset += ALIGN(size, DK_SHADER_CODE_ALIGNMENT);
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

int sceGxmInitialize(const SceGxmInitializeParams *params)
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
	g_code_memblock = dk_alloc_memblock(g_dk_device, CODEMEMSIZE,
		DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);

	/* Load shaders */
	g_code_mem_offset = 0;
	load_shader_memory(g_code_memblock, &g_vertexShader, &g_code_mem_offset, basic_vsh_dksh, basic_vsh_dksh_size);
	load_shader_memory(g_code_memblock, &g_fragmentShader, &g_code_mem_offset, color_fsh_dksh, color_fsh_dksh_size);

	g_gxm_initialized = true;
	return 0;
}

int sceGxmTerminate()
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

int sceGxmCreateContext(const SceGxmContextParams *params, SceGxmContext **context)
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
	ctx->vertex_rb_memblock = SceSysmem_get_dk_memblock_for_addr(params->vertexRingBufferMem);
	assert(ctx->vertex_rb_memblock);
	assert(params->vertexRingBufferMem == dkMemBlockGetCpuAddr(ctx->vertex_rb_memblock));

	/* Get the passed fragment ringbuffer for fragment default uniform buffer reservations */
	ctx->fragment_rb_memblock = SceSysmem_get_dk_memblock_for_addr(params->fragmentRingBufferMem);
	assert(ctx->fragment_rb_memblock);
	assert(params->fragmentRingBufferMem == dkMemBlockGetCpuAddr(ctx->fragment_rb_memblock));

	ctx->gxm_vert_unif_block_memblock = dk_alloc_memblock(g_dk_device,
		ALIGN(sizeof(struct GXMRenderVertUniformBlock), DK_UNIFORM_BUF_ALIGNMENT),
		DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);

	ctx->gxm_frag_unif_block_memblock = dk_alloc_memblock(g_dk_device,
		ALIGN(sizeof(struct GXMRenderFragUniformBlock), DK_UNIFORM_BUF_ALIGNMENT),
		DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);

	/* Init default state */
	ctx->vertex_rb.head = 0;
	ctx->vertex_rb.tail = 0;
	ctx->vertex_rb.size = params->vertexRingBufferMemSize;
	ctx->fragment_rb.head = 0;
	ctx->fragment_rb.tail = 0;
	ctx->fragment_rb.size = params->fragmentRingBufferMemSize;

	dkRasterizerStateDefaults(&ctx->rasterizer_state);
	ctx->rasterizer_state.cullMode = DkFace_None;

	dkColorStateDefaults(&ctx->color_state);
	dkColorWriteStateDefaults(&ctx->color_write_state);

	ctx->depth_stencil_state.depthTestEnable = true;
	ctx->depth_stencil_state.depthWriteEnable = true;
	ctx->depth_stencil_state.stencilTestEnable = false;
	ctx->depth_stencil_state.depthCompareOp = DkCompareOp_Lequal;

	ctx->depth_stencil_state.stencilFrontFailOp = DkStencilOp_Keep;
	ctx->depth_stencil_state.stencilFrontPassOp = DkStencilOp_Keep;
	ctx->depth_stencil_state.stencilFrontDepthFailOp = DkStencilOp_Keep;
	ctx->depth_stencil_state.stencilFrontCompareOp = DkCompareOp_Always;

	ctx->depth_stencil_state.stencilBackFailOp = DkStencilOp_Keep;
	ctx->depth_stencil_state.stencilBackPassOp = DkStencilOp_Keep;
	ctx->depth_stencil_state.stencilBackDepthFailOp = DkStencilOp_Keep;
	ctx->depth_stencil_state.stencilBackCompareOp = DkCompareOp_Always;

	ctx->front_stencil_state.ref = 0;
	ctx->front_stencil_state.compare_mask = 0;
	ctx->front_stencil_state.write_mask = 0;

	ctx->back_stencil_state.ref = 0;
	ctx->back_stencil_state.compare_mask = 0;
	ctx->back_stencil_state.write_mask = 0;

	ctx->dirty.raw = ~(uint32_t)0;

	*context = ctx;

	return 0;
}

int sceGxmDestroyContext(SceGxmContext *context)
{
	dkQueueWaitIdle(g_render_queue);
	dkMemBlockDestroy(context->gxm_vert_unif_block_memblock);
	dkMemBlockDestroy(context->gxm_frag_unif_block_memblock);
	dkCmdBufDestroy(context->cmdbuf);

	return 0;
}

void sceGxmFinish(SceGxmContext *context)
{
	dkQueueWaitIdle(g_render_queue);
}

volatile unsigned int *sceGxmGetNotificationRegion(void)
{
	return dkMemBlockGetCpuAddr(g_notification_region_memblock);
}

int sceGxmSyncObjectCreate(SceGxmSyncObject **syncObject)
{
	SceGxmSyncObject *sync_object;

	sync_object = malloc(sizeof(*sync_object));
	if (!sync_object)
		return SCE_KERNEL_ERROR_NO_MEMORY;

	memset(sync_object, 0, sizeof(*sync_object));
	*syncObject = sync_object;

	return 0;
}

int sceGxmSyncObjectDestroy(SceGxmSyncObject *syncObject)
{
	free(syncObject);
	return 0;
}

int sceGxmNotificationWait(const SceGxmNotification *notification)
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

int sceGxmShaderPatcherCreate(const SceGxmShaderPatcherParams *params,
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

int sceGxmShaderPatcherDestroy(SceGxmShaderPatcher *shaderPatcher)
{
	free(shaderPatcher->registered_programs);
	free(shaderPatcher);
	return 0;
}

int sceGxmShaderPatcherRegisterProgram(SceGxmShaderPatcher *shaderPatcher,
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

int sceGxmShaderPatcherUnregisterProgram(SceGxmShaderPatcher *shaderPatcher,
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

int sceGxmShaderPatcherCreateVertexProgram(SceGxmShaderPatcher *shaderPatcher,
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

int sceGxmShaderPatcherReleaseVertexProgram(SceGxmShaderPatcher *shaderPatcher,
					    SceGxmVertexProgram *vertexProgram)
{
	free(vertexProgram->attributes);
	free(vertexProgram->streams);
	free(vertexProgram);
	return 0;
}

int sceGxmShaderPatcherCreateFragmentProgram(SceGxmShaderPatcher *shaderPatcher,
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

int sceGxmShaderPatcherReleaseFragmentProgram(SceGxmShaderPatcher *shaderPatcher,
					      SceGxmFragmentProgram *fragmentProgram)
{
	free(fragmentProgram);
	return 0;
}

const SceGxmProgram *sceGxmShaderPatcherGetProgramFromId(SceGxmShaderPatcherId programId)
{
	if (programId)
		return programId->programHeader;

	return NULL;
}

int sceGxmGetRenderTargetMemSize(const SceGxmRenderTargetParams *params, unsigned int *driverMemSize)
{
	*driverMemSize = sizeof(SceGxmRenderTarget);
	return 0;
}

int sceGxmCreateRenderTarget(const SceGxmRenderTargetParams *params, SceGxmRenderTarget **renderTarget)
{
	SceGxmRenderTarget *render_target;

	render_target = malloc(sizeof(*render_target));
	if (!render_target)
		return SCE_KERNEL_ERROR_NO_MEMORY;

	render_target->params = *params;
	*renderTarget = render_target;

	return 0;
}

int sceGxmDestroyRenderTarget(SceGxmRenderTarget *renderTarget)
{
	free(renderTarget);
	return 0;
}

int sceGxmColorSurfaceInit(SceGxmColorSurface *surface, SceGxmColorFormat colorFormat,
			   SceGxmColorSurfaceType surfaceType,
			   SceGxmColorSurfaceScaleMode scaleMode,
			   SceGxmOutputRegisterSize outputRegisterSize,
			   unsigned int width, unsigned int height,
			   unsigned int strideInPixels, void *data)
{
	SceGxmColorSurfaceInner *inner = (SceGxmColorSurfaceInner *)surface;

	memset(inner, 0, sizeof(*inner));
	inner->disabled = 0;
	inner->downscale = scaleMode == SCE_GXM_COLOR_SURFACE_SCALE_MSAA_DOWNSCALE;
	inner->width = width;
	inner->height = height;
	inner->strideInPixels = strideInPixels;
	inner->data = data;
	inner->colorFormat = colorFormat;
	inner->surfaceType = surfaceType;
	inner->outputRegisterSize = outputRegisterSize;

	return 0;
}

int sceGxmDepthStencilSurfaceInit(SceGxmDepthStencilSurface *surface,
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

	surface->zlsControl = 0;
	surface->depthData = depthData;
	surface->stencilData = stencilData;
	surface->backgroundDepth = 0.0f;
	surface->backgroundControl = 0;

	return 0;
}

void sceGxmSetFrontDepthFunc(SceGxmContext *context, SceGxmDepthFunc depthFunc)
{
	context->depth_stencil_state.depthCompareOp = gxm_depth_func_to_dk_compare_op(depthFunc);
	context->dirty.bit.depth_stencil = true;
}

void sceGxmSetFrontDepthWriteEnable(SceGxmContext *context, SceGxmDepthWriteMode enable)
{
	context->depth_stencil_state.depthWriteEnable =
		(enable == SCE_GXM_DEPTH_WRITE_ENABLED) ? 1 : 0;
	context->dirty.bit.depth_stencil = true;
}

void sceGxmSetFrontStencilRef(SceGxmContext *context, unsigned int sref)
{
	context->front_stencil_state.ref = sref;
	context->dirty.bit.front_stencil = true;
}

void sceGxmSetBackStencilRef(SceGxmContext *context, unsigned int sref)
{
	context->back_stencil_state.ref = sref;
	context->dirty.bit.back_stencil = true;
}

void sceGxmSetFrontStencilFunc(SceGxmContext *context, SceGxmStencilFunc func,
			       SceGxmStencilOp stencilFail, SceGxmStencilOp depthFail,
			       SceGxmStencilOp depthPass, unsigned char compareMask,
			       unsigned char writeMask)
{

	context->depth_stencil_state.stencilFrontFailOp = gxm_stencil_op_to_dk_stencil_op(stencilFail);
	context->depth_stencil_state.stencilFrontDepthFailOp = gxm_stencil_op_to_dk_stencil_op(depthFail);
	context->depth_stencil_state.stencilFrontCompareOp = gxm_stencil_func_to_dk_compare_op(func);
	context->front_stencil_state.compare_mask = compareMask;
	context->front_stencil_state.write_mask = writeMask;
	context->dirty.bit.depth_stencil = true;
	context->dirty.bit.front_stencil = true;
}

void sceGxmSetBackStencilFunc(SceGxmContext *context, SceGxmStencilFunc func,
			      SceGxmStencilOp stencilFail, SceGxmStencilOp depthFail,
			      SceGxmStencilOp depthPass, unsigned char compareMask,
			      unsigned char writeMask)
{
	context->depth_stencil_state.stencilBackFailOp = gxm_stencil_op_to_dk_stencil_op(stencilFail);
	context->depth_stencil_state.stencilBackDepthFailOp = gxm_stencil_op_to_dk_stencil_op(depthFail);
	context->depth_stencil_state.stencilBackCompareOp = gxm_stencil_func_to_dk_compare_op(func);
	context->back_stencil_state.compare_mask = compareMask;
	context->back_stencil_state.write_mask = writeMask;
	context->dirty.bit.depth_stencil = true;
	context->dirty.bit.back_stencil = true;
}

static inline void dk_image_view_for_gxm_color_surface(DkImage *image, DkImageView *view, DkMemBlock block,
						       const SceGxmColorSurfaceInner *surface)
{
	DkImageLayoutMaker maker;
	DkImageLayout layout;

	dkImageLayoutMakerDefaults(&maker, g_dk_device);
	maker.flags = gxm_color_surface_type_to_dk_image_flags(surface->surfaceType) |
		      DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
	maker.format = gxm_color_format_to_dk_image_format(surface->colorFormat);
	maker.dimensions[0] = surface->width;
	maker.dimensions[1] = surface->height;
	maker.pitchStride = surface->strideInPixels * gxm_color_format_bytes_per_pixel(surface->colorFormat);
	dkImageLayoutInitialize(&layout, &maker);
	dkImageInitialize(image, &layout, block, dk_memblock_cpu_addr_offset(block, surface->data));
	dkImageViewDefaults(view, image);
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
	dkImageInitialize(image, &layout, block, dk_memblock_cpu_addr_offset(block, surface->depthData));
	dkImageViewDefaults(view, image);
}

static void set_vita3k_gxm_uniform_blocks(SceGxmContext *context, const DkViewport *viewport)
{
	struct GXMRenderVertUniformBlock vert_unif = {
		.viewport_flip = {1.0f, 1.0f, 1.0f, 1.0f},
		.viewport_flag = (0) ? 0.0f : 1.0f,
		.screen_width = viewport->width,
		.screen_height = viewport->height
	};

	struct GXMRenderFragUniformBlock frag_unif = {
		.back_disabled = 0.0f,
		.front_disabled = 0.0f,
		.writing_mask = 1.0f
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

int sceGxmBeginScene(SceGxmContext *context, unsigned int flags,
		     const SceGxmRenderTarget *renderTarget, const SceGxmValidRegion *validRegion,
		     SceGxmSyncObject *vertexSyncObject, SceGxmSyncObject *fragmentSyncObject,
		     const SceGxmColorSurface *colorSurface,
		     const SceGxmDepthStencilSurface *depthStencil)
{
	DkMemBlock color_surface_block;
	DkImage color_surface_image;
	DkImageView color_surface_view;
	DkMemBlock depth_stencil_surface_block;
	DkImage depth_stencil_surface_image;
	DkImageView depth_stencil_surface_view;
	DkViewport viewport = { 0.0f, 0.0f, (float)renderTarget->params.width, (float)renderTarget->params.height, 0.0f, 1.0f };
	DkScissor scissor = { 0, 0, renderTarget->params.width, renderTarget->params.height };
	SceGxmColorSurfaceInner *color_surface_inner = (SceGxmColorSurfaceInner *)colorSurface;

	if (context->in_scene)
		return SCE_GXM_ERROR_WITHIN_SCENE;

	color_surface_block = SceSysmem_get_dk_memblock_for_addr(color_surface_inner->data);
	if (!color_surface_block)
		return SCE_GXM_ERROR_INVALID_VALUE;

	if (depthStencil) {
		depth_stencil_surface_block = SceSysmem_get_dk_memblock_for_addr(depthStencil->depthData);
		if (!depth_stencil_surface_block)
			return SCE_GXM_ERROR_INVALID_VALUE;

		dk_image_view_for_gxm_depth_stencil_surface(&depth_stencil_surface_image,
							    &depth_stencil_surface_view,
							    depth_stencil_surface_block,
							    renderTarget->params.width,
							    renderTarget->params.height,
							    depthStencil);
	}

	LOG("sceGxmBeginScene to renderTarget %p, fragmentSyncObject: %p, "
	    "w: %" PRId32 ", h: %" PRId32 ", stride: %" PRId32 ", CPU addr: %p",
		renderTarget, fragmentSyncObject,
		color_surface_inner->width, color_surface_inner->height,
		color_surface_inner->strideInPixels,
		dkMemBlockGetCpuAddr(color_surface_block));

	dk_image_view_for_gxm_color_surface(&color_surface_image, &color_surface_view,
					    color_surface_block, color_surface_inner);

	dkCmdBufClear(context->cmdbuf);

	dkCmdBufBindRenderTarget(context->cmdbuf, &color_surface_view,
				 depthStencil ? &depth_stencil_surface_view : NULL);

	dkCmdBufSetViewports(context->cmdbuf, 0, &viewport, 1);
	dkCmdBufSetScissors(context->cmdbuf, 0, &scissor, 1);
	dkCmdBufBindRasterizerState(context->cmdbuf, &context->rasterizer_state);
	dkCmdBufBindColorState(context->cmdbuf, &context->color_state);
	dkCmdBufBindColorWriteState(context->cmdbuf, &context->color_write_state);
	set_vita3k_gxm_uniform_blocks(context, &viewport);

	context->vertex_rb.head = 0;
	context->fragment_rb.head = 0;

	/* Wait until the framebuffer is swapped out before writing to it */
	if (fragmentSyncObject)
		dkCmdBufWaitFence(context->cmdbuf, &fragmentSyncObject->fence);

	dkCmdBufClearDepthStencil(context->cmdbuf, true, 1.0f, 0xFF, 0);

	context->in_scene = true;

	return 0;
}

int sceGxmEndScene(SceGxmContext *context, const SceGxmNotification *vertexNotification,
		   const SceGxmNotification *fragmentNotification)
{
	DkCmdList cmd_list;
	DkVariable variable;
	uint32_t offset;

	LOG("sceGxmEndScene");

	if (!context->in_scene)
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

	cmd_list = dkCmdBufFinishList(context->cmdbuf);
	dkQueueSubmitCommands(g_render_queue, cmd_list);

	context->in_scene = false;

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

int sceGxmDisplayQueueAddEntry(SceGxmSyncObject *oldBuffer, SceGxmSyncObject *newBuffer, const void *callbackData)
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

int sceGxmDisplayQueueFinish(void)
{
	DisplayQueueControlBlock *queue = g_display_queue;

	while (CIRC_CNT(queue->head, queue->tail, queue->num_entries) > 0)
		waitSingle(waiterForUEvent(&queue->ready_evflag), -1);

	return 0;
}

void sceGxmSetVertexProgram(SceGxmContext *context, const SceGxmVertexProgram *vertexProgram)
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

	context->vertex_program = vertexProgram;
	context->dirty.bit.shaders = true;
}

void sceGxmSetFragmentProgram(SceGxmContext *context, const SceGxmFragmentProgram *fragmentProgram)
{
	DkColorWriteState color_write_state;
	uint32_t mask;

	mask = DkColorMask_R * !!(fragmentProgram->blendInfo.colorMask & SCE_GXM_COLOR_MASK_R) |
	       DkColorMask_G * !!(fragmentProgram->blendInfo.colorMask & SCE_GXM_COLOR_MASK_G) |
	       DkColorMask_B * !!(fragmentProgram->blendInfo.colorMask & SCE_GXM_COLOR_MASK_B) |
	       DkColorMask_A * !!(fragmentProgram->blendInfo.colorMask & SCE_GXM_COLOR_MASK_A);

	dkColorWriteStateDefaults(&color_write_state);
	dkColorWriteStateSetMask(&color_write_state, 0, mask);
	dkCmdBufBindColorWriteState(context->cmdbuf, &color_write_state);

	context->fragment_program = fragmentProgram;
	context->dirty.bit.shaders = true;
}

int sceGxmSetVertexStream(SceGxmContext *context, unsigned int streamIndex, const void *streamData)
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

int sceGxmReserveVertexDefaultUniformBuffer(SceGxmContext *context, void **uniformBuffer)
{
	DkBufExtents buf_extents;
	uint32_t default_uniform_buffer_count;
	const SceGxmProgram *program;

	if (!context->in_scene)
		return SCE_GXM_ERROR_NOT_WITHIN_SCENE;
	else if (!context->vertex_program)
		return SCE_GXM_ERROR_NULL_PROGRAM;

	program = context->vertex_program->programId->programHeader;

	default_uniform_buffer_count = program->default_uniform_buffer_count;
	if (default_uniform_buffer_count == 0) {
		*uniformBuffer = NULL;
		return 0;
	}

	buf_extents.addr = dkMemBlockGetGpuAddr(context->vertex_rb_memblock) + context->vertex_rb.head;
	buf_extents.size = default_uniform_buffer_count * sizeof(float);

	dkCmdBufBindStorageBuffers(context->cmdbuf, DkStage_Vertex, 0, &buf_extents, 1);

	*uniformBuffer = dkMemBlockGetCpuAddr(context->vertex_rb_memblock) + context->vertex_rb.head;
	context->vertex_rb.head += buf_extents.size;

	return 0;
}

int sceGxmReserveFragmentDefaultUniformBuffer(SceGxmContext *context, void **uniformBuffer)
{
	DkBufExtents buf_extents;
	uint32_t default_uniform_buffer_count;
	const SceGxmProgram *program;

	if (!context->in_scene)
		return SCE_GXM_ERROR_NOT_WITHIN_SCENE;
	else if (!context->fragment_program)
		return SCE_GXM_ERROR_NULL_PROGRAM;

	program = context->fragment_program->programId->programHeader;

	default_uniform_buffer_count = program->default_uniform_buffer_count;
	if (default_uniform_buffer_count == 0) {
		*uniformBuffer = NULL;
		return 0;
	}

	buf_extents.addr = dkMemBlockGetGpuAddr(context->fragment_rb_memblock) + context->fragment_rb.head;
	buf_extents.size = default_uniform_buffer_count * sizeof(float);

	dkCmdBufBindStorageBuffers(context->cmdbuf, DkStage_Fragment, 1, &buf_extents, 1);

	*uniformBuffer = dkMemBlockGetCpuAddr(context->fragment_rb_memblock) + context->fragment_rb.head;
	context->fragment_rb.head += buf_extents.size;

	return 0;
}

int sceGxmSetUniformDataF(void *uniformBuffer, const SceGxmProgramParameter *parameter,
			  unsigned int componentOffset, unsigned int componentCount,
			  const float *sourceData)
{
	uint32_t component_size, dst_offset, copy_size;

	if (!uniformBuffer || !sourceData)
		return SCE_GXM_ERROR_INVALID_POINTER;

	component_size = gxm_parameter_type_size(parameter->type);
	dst_offset = component_size * (parameter->resource_index + componentOffset);
	copy_size = component_size * componentCount;

	memcpy((char *)uniformBuffer + dst_offset, sourceData, copy_size);

	return 0;
}

const SceGxmProgramParameter *sceGxmProgramFindParameterByName(const SceGxmProgram *program, const char *name)
{
	const uint8_t *parameter_bytes;
	const char *parameter_name;
	const SceGxmProgramParameter *const parameters =
		(const SceGxmProgramParameter *)((char *)&program->parameters_offset + program->parameters_offset);

	for (uint32_t i = 0; i < program->parameter_count; i++) {
		parameter_bytes = (const uint8_t *)&parameters[i];
		parameter_name = (const char *)(parameter_bytes + parameters[i].name_offset);
		if (strcmp(parameter_name, name) == 0)
			return (const SceGxmProgramParameter *)parameter_bytes;
	}

	return NULL;
}

static void context_flush_dirty_state(SceGxmContext *context)
{
	const DkShader *shaders[2];

	if (context->dirty.bit.shaders) {
		shaders[0] = &context->vertex_program->dk_shader;
		shaders[1] = &context->fragment_program->dk_shader;
		dkCmdBufBindShaders(context->cmdbuf, DkStageFlag_GraphicsMask, shaders, ARRAY_SIZE(shaders));
		context->dirty.bit.shaders = false;
	}

	if (context->dirty.bit.depth_stencil) {
		dkCmdBufBindDepthStencilState(context->cmdbuf, &context->depth_stencil_state);
		context->dirty.bit.depth_stencil = false;
	}

	if (context->dirty.bit.front_stencil) {
		dkCmdBufSetStencil(context->cmdbuf, DkFace_Front,
				context->front_stencil_state.write_mask,
				context->front_stencil_state.ref,
				context->front_stencil_state.compare_mask);
		context->dirty.bit.front_stencil = false;
	}

	if (context->dirty.bit.back_stencil) {
		dkCmdBufSetStencil(context->cmdbuf, DkFace_Back,
				context->back_stencil_state.write_mask,
				context->back_stencil_state.ref,
				context->back_stencil_state.compare_mask);
		context->dirty.bit.back_stencil = false;
	}
}

int sceGxmDraw(SceGxmContext *context, SceGxmPrimitiveType primType, SceGxmIndexFormat indexType,
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

int sceGxmMapMemory(void *base, SceSize size, SceGxmMemoryAttribFlags attr)
{
	VitaMemBlockInfo *block;

	/* By default, SceSysmem maps all the allocated blocks to the GPU.
	 * Here we just check if it exists */
	block = SceSysmem_get_vita_memblock_info_for_addr(base);
	if (!block)
		return SCE_GXM_ERROR_INVALID_POINTER;

	return 0;
}

int sceGxmUnmapMemory(void *base)
{
	VitaMemBlockInfo *block = SceSysmem_get_vita_memblock_info_for_addr(base);
	if (!block)
		return SCE_GXM_ERROR_INVALID_POINTER;

	return 0;
}

int sceGxmMapVertexUsseMemory(void *base, SceSize size, unsigned int *offset)
{
	return 0;
}

int sceGxmUnmapVertexUsseMemory(void *base)
{
	return 0;
}

int sceGxmMapFragmentUsseMemory(void *base, SceSize size, unsigned int *offset)
{
	return 0;
}

int sceGxmUnmapFragmentUsseMemory(void *base)
{
	return 0;
}

void SceGxm_register(void)
{
	static const export_entry_t exports[] = {
		{0xB0F1E4EC, sceGxmInitialize},
		{0xB627DE66, sceGxmTerminate},
		{0xE84CE5B4, sceGxmCreateContext},
		{0xEDDC5FB2, sceGxmDestroyContext},
		{0x0733D8AE, sceGxmFinish},
		{0x8BDE825A, sceGxmGetNotificationRegion},
		{0x9F448E79, sceGxmNotificationWait},
		{0x6A6013E1, sceGxmSyncObjectCreate},
		{0x889AE88C, sceGxmSyncObjectDestroy},
		{0x05032658, sceGxmShaderPatcherCreate},
		{0xEAA5B100, sceGxmShaderPatcherDestroy},
		{0x2B528462, sceGxmShaderPatcherRegisterProgram},
		{0xF103AF8A, sceGxmShaderPatcherUnregisterProgram},
		{0xB7BBA6D5, sceGxmShaderPatcherCreateVertexProgram},
		{0xAC1FF2DA, sceGxmShaderPatcherReleaseVertexProgram},
		{0x4ED2E49D, sceGxmShaderPatcherCreateFragmentProgram},
		{0xBE2743D1, sceGxmShaderPatcherReleaseFragmentProgram},
		{0xA949A803, sceGxmShaderPatcherGetProgramFromId},
		{0xB291C959, sceGxmGetRenderTargetMemSize},
		{0x207AF96B, sceGxmCreateRenderTarget},
		{0x0B94C50A, sceGxmDestroyRenderTarget},
		{0xED0F6E25, sceGxmColorSurfaceInit},
		{0xCA9D41D1, sceGxmDepthStencilSurfaceInit},
		{0x14BD831F, sceGxmSetFrontDepthFunc},
		{0xF32CBF34, sceGxmSetFrontDepthWriteEnable},
		{0x8FA6FE44, sceGxmSetFrontStencilRef},
		{0x866A0517, sceGxmSetBackStencilRef},
		{0xB8645A9A, sceGxmSetFrontStencilFunc},
		{0x1A68C8D2, sceGxmSetBackStencilFunc},
		{0x8734FF4E, sceGxmBeginScene},
		{0xFE300E2F, sceGxmEndScene},
		{0xEC5C26B5, sceGxmDisplayQueueAddEntry},
		{0xB98C5B0D, sceGxmDisplayQueueFinish},
		{0x31FF8ABD, sceGxmSetVertexProgram},
		{0xAD2F48D9, sceGxmSetFragmentProgram},
		{0x895DF2E9, sceGxmSetVertexStream},
		{0x97118913, sceGxmReserveVertexDefaultUniformBuffer},
		{0x7B1FABB6, sceGxmReserveFragmentDefaultUniformBuffer},
		{0x65DD0C84, sceGxmSetUniformDataF},
		{0x277794C4, sceGxmProgramFindParameterByName},
		{0xBC059AFC, sceGxmDraw},
		{0xC61E34FC, sceGxmMapMemory},
		{0x828C68E8, sceGxmUnmapMemory},
		{0xFA437510, sceGxmMapVertexUsseMemory},
		{0x099134F5, sceGxmUnmapVertexUsseMemory},
		{0x008402C6, sceGxmMapFragmentUsseMemory},
		{0x80CCEDBB, sceGxmUnmapFragmentUsseMemory},
	};

	module_register_exports(exports, ARRAY_SIZE(exports));
}

int SceGxm_init(DkDevice dk_device)
{
	g_dk_device = dk_device;
	return 0;
}

int SceGxm_finish(void)
{
	return 0;
}
