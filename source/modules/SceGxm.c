#include <stdlib.h>
#include <switch.h>
#include <deko3d.h>
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/gxm.h>
#include "SceGxm.h"
#include "SceSysmem.h"
#include "circ_buf.h"
#include "vita_to_dk.h"
#include "module.h"
#include "protected_bitset.h"
#include "util.h"
#include "log.h"

#include "basic_vsh_dksh.h"
#include "color_fsh_dksh.h"
#include <assert.h>
#define SCE_GXM_NOTIFICATION_COUNT	512

#define MAX_GXM_MAPPED_MEMORY_BLOCKS	256

typedef struct SceGxmContext {
	SceGxmContextParams params;
	DkMemBlock cmdbuf_memblock;
	DkCmdBuf cmdbuf;
	struct {
		DkMemBlock memblock;
		uint32_t head;
		uint32_t tail;
		uint32_t size;
	} vertex_ringbuf;
	struct {
		DkMemBlock memblock;
		uint32_t head;
		uint32_t tail;
		uint32_t size;
	} fragment_ringbuf;
	const SceGxmVertexProgram *vertex_program;
	const SceGxmFragmentProgram *fragment_program;
	struct {
		bool valid;
	} scene;
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
} SceGxmVertexProgram;

typedef struct SceGxmFragmentProgram {
	SceGxmShaderPatcherId programId;
	SceGxmOutputRegisterFormat outputFormat;
	SceGxmMultisampleMode multisampleMode;
	SceGxmBlendInfo blendInfo;
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

static bool g_gxm_initialized;

/* Deko3D */

typedef struct {
	float position[3];
	float color[3];
} Vertex;

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

int sceGxmInitialize(const SceGxmInitializeParams *params)
{
	DkQueueMaker queue_maker;
	DkMemBlockMaker memblock_maker;
	uint32_t display_queue_num_entries;

	if (g_gxm_initialized)
		return SCE_GXM_ERROR_ALREADY_INITIALIZED;

	/* Create graphics queue */
	dkQueueMakerDefaults(&queue_maker, g_dk_device);
	queue_maker.flags = DkQueueFlags_Graphics;
	g_render_queue = dkQueueCreate(&queue_maker);

	/* Create memory block for the "notification region" */
	dkMemBlockMakerDefaults(&memblock_maker, g_dk_device,
			       ALIGN(SCE_GXM_NOTIFICATION_COUNT * sizeof(uint32_t), DK_MEMBLOCK_ALIGNMENT));
	memblock_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached;
	g_notification_region_memblock = dkMemBlockCreate(&memblock_maker);

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
	dkMemBlockMakerDefaults(&memblock_maker, g_dk_device, CODEMEMSIZE);
	memblock_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code;
	g_code_memblock = dkMemBlockCreate(&memblock_maker);

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

static bool created = false;
static DkImageLayout depth_layout;
static DkImage depth_image;
static DkImageView depth_view;
static DkMemBlock depth_memblock;

static void ensure_shadow_depth_stencil_buffer(SceGxmContext *context, uint32_t width, uint32_t height)
{
	uint64_t size;
	uint32_t align;
	void *addr;
	DkImageLayoutMaker layout_maker;

	LOG("ensure_shadow_depth_stencil_buffer 1");

	if (created)
		return;

	assert(width > 0 && height > 0);

	LOG("  creating shadow depth/stencil buffer!");
	dkImageLayoutMakerDefaults(&layout_maker, g_dk_device);
	layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_HwCompression;
	layout_maker.format = DkImageFormat_Z24S8;
	layout_maker.dimensions[0] = width;
	layout_maker.dimensions[1] = height;
	dkImageLayoutInitialize(&depth_layout, &layout_maker);

	size  = dkImageLayoutGetSize(&depth_layout);
	align = dkImageLayoutGetAlignment(&depth_layout);
	addr = aligned_alloc(align, size);
	assert(size > 0 && size <= UINT32_MAX && addr != NULL);

	LOG("Shadow size: 0x%llx, align: 0x%x, addr: %p", size, align, addr);

	DkMemBlockMaker memblock_maker;
	dkMemBlockMakerDefaults(&memblock_maker, g_dk_device, (uint32_t)size);
	//memblock_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
	memblock_maker.flags |= DkMemBlockFlags_Image;
	//memblock_maker.storage = addr;
	depth_memblock = dkMemBlockCreate(&memblock_maker);
	assert(depth_memblock);

	dkImageInitialize(&depth_image, &depth_layout, depth_memblock, 0);
	dkImageViewDefaults(&depth_view, &depth_image);

	created = true;
}

static inline DkMemBlock dk_alloc_memblock(DkDevice device, uint32_t size, uint32_t flags)
{
	DkMemBlockMaker memblock_maker;
	dkMemBlockMakerDefaults(&memblock_maker, device, ALIGN(size, DK_MEMBLOCK_ALIGNMENT));
	memblock_maker.flags = flags;
	return dkMemBlockCreate(&memblock_maker);
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
	//ctx->cmdbuf_memblock = SceSysmem_get_dk_memblock_for_addr(params->vdmRingBufferMem);
	ctx->cmdbuf_memblock = dk_alloc_memblock(g_dk_device, 16*1024*1024, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
	assert(ctx->cmdbuf_memblock);

	/* Create the command buffer */
	dkCmdBufMakerDefaults(&cmdbuf_maker, g_dk_device);
	ctx->cmdbuf = dkCmdBufCreate(&cmdbuf_maker);
	assert(ctx->cmdbuf);

	/* Assing the backing storage buffer to the main command buffer */
	dkCmdBufAddMemory(ctx->cmdbuf, ctx->cmdbuf_memblock, 0, 16*1024*1024);

	/* Get the passed vertex ringbuffer for vertex default uniform buffer reservations */
	//ctx->vertex_ringbuf.memblock = SceSysmem_get_dk_memblock_for_addr(params->vertexRingBufferMem);
	ctx->vertex_ringbuf.memblock = dk_alloc_memblock(g_dk_device, 16*1024*1024, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
	assert(ctx->vertex_ringbuf.memblock);
	//assert(params->vertexRingBufferMem == dkMemBlockGetCpuAddr(ctx->vertex_ringbuf.memblock));
	ctx->vertex_ringbuf.head = 0;
	ctx->vertex_ringbuf.tail = 0;
	ctx->vertex_ringbuf.size = 16*1024*1024;

	/* Get the passed fragment ringbuffer for fragment default uniform buffer reservations */
	ctx->fragment_ringbuf.memblock = dk_alloc_memblock(g_dk_device, 16*1024*1024, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
	assert(ctx->fragment_ringbuf.memblock);
	//assert(params->fragmentRingBufferMem == dkMemBlockGetCpuAddr(ctx->fragment_ringbuf.memblock));
	ctx->fragment_ringbuf.head = 0;
	ctx->fragment_ringbuf.tail = 0;
	ctx->fragment_ringbuf.size = 16*1024*1024;

	ensure_shadow_depth_stencil_buffer(ctx, 960, 544);

	*context = ctx;

	return 0;
}

int sceGxmDestroyContext(SceGxmContext *context)
{
	dkQueueWaitIdle(g_render_queue);
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
	uint32_t offset = (uintptr_t)notification->address -
			  (uintptr_t)dkMemBlockGetCpuAddr(g_notification_region_memblock);

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

int sceGxmShaderPatcherCreateVertexProgram(SceGxmShaderPatcher *shaderPatcher,
					   SceGxmShaderPatcherId programId,
					   const SceGxmVertexAttribute *attributes,
					   unsigned int attributeCount,
					   const SceGxmVertexStream *streams,
					   unsigned int streamCount,
					   SceGxmVertexProgram **vertexProgram)
{
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
	SceGxmFragmentProgram *fragment_program;

	fragment_program = malloc(sizeof(*fragment_program));
	if (!fragment_program)
		return SCE_KERNEL_ERROR_NO_MEMORY;

	memset(fragment_program, 0, sizeof(*fragment_program));
	fragment_program->programId = programId;
	fragment_program->outputFormat = outputFormat;
	fragment_program->multisampleMode = multisampleMode;
	if (blendInfo)
		fragment_program->blendInfo = *blendInfo;

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

static inline void dk_image_view_for_gxm_color_surface(DkImage *image, DkImageView *view, DkMemBlock block,
						       const SceGxmColorSurfaceInner *surface)
{
	DkImageLayoutMaker maker;
	DkImageLayout layout;
	uint32_t offset;

	dkImageLayoutMakerDefaults(&maker, g_dk_device);
	maker.flags = gxm_color_surface_type_to_dk_image_flags(surface->surfaceType) |
		      DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine | DkImageFlags_HwCompression;
	maker.format = gxm_color_format_to_dk_image_format(surface->colorFormat);
	maker.dimensions[0] = surface->width;
	maker.dimensions[1] = surface->height;
	maker.pitchStride = surface->strideInPixels * gxm_color_format_bytes_per_pixel(surface->colorFormat);
	dkImageLayoutInitialize(&layout, &maker);

	offset = (uintptr_t)surface->data - (uintptr_t)dkMemBlockGetCpuAddr(block);
	dkImageInitialize(image, &layout, block, offset);
	dkImageViewDefaults(view, image);
}

#define FB_NUM 2
#define FB_WIDTH  960
#define FB_HEIGHT 544
static int inited = 0;
static DkMemBlock g_depthMemBlock;
static DkImage g_depth;

static DkMemBlock g_framebufferMemBlock;
static DkMemBlock g_depthMemBlock;
static DkImage g_framebuffers[FB_NUM];

static DkMemBlock g_cmdbufMemBlock;
static DkCmdBuf g_cmdbuf;
static DkCmdList g_cmdsRender;

void init()
{
	DkMemBlockMaker memBlockMaker;

	if (inited)
		return;

	// Calculate layout for the framebuffers
	DkImageLayoutMaker imageLayoutMaker;
	dkImageLayoutMakerDefaults(&imageLayoutMaker, g_dk_device);
	imageLayoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_PitchLinear;
	imageLayoutMaker.format = DkImageFormat_RGBA8_Unorm;
	imageLayoutMaker.dimensions[0] = FB_WIDTH;
	imageLayoutMaker.dimensions[1] = FB_HEIGHT;
	imageLayoutMaker.pitchStride = FB_WIDTH * 4;

	// Calculate layout for the framebuffers
	DkImageLayout framebufferLayout;
	dkImageLayoutInitialize(&framebufferLayout, &imageLayoutMaker);

	// Retrieve necessary size and alignment for the framebuffers
	uint32_t framebufferSize  = dkImageLayoutGetSize(&framebufferLayout);
	uint32_t framebufferAlign = dkImageLayoutGetAlignment(&framebufferLayout);
	framebufferSize = (framebufferSize + framebufferAlign - 1) &~ (framebufferAlign - 1);

	LOG("FB %d x %d, size: 0x%x, align: 0x%x", FB_WIDTH, FB_HEIGHT, framebufferSize, framebufferAlign);

	// Create a memory block that will host the framebuffers
	dkMemBlockMakerDefaults(&memBlockMaker, g_dk_device, FB_NUM*framebufferSize);
	memBlockMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
	g_framebufferMemBlock = dkMemBlockCreate(&memBlockMaker);

	for (unsigned i = 0; i < FB_NUM; i++) {
		dkImageInitialize(&g_framebuffers[i], &framebufferLayout, g_framebufferMemBlock, i*framebufferSize);
	}

	// Calculate layout for the depths
	DkImageLayoutMaker depthImageLayoutMaker;
	dkImageLayoutMakerDefaults(&depthImageLayoutMaker, g_dk_device);
	depthImageLayoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_PitchLinear;
	depthImageLayoutMaker.format = DkImageFormat_Z24S8;
	depthImageLayoutMaker.dimensions[0] = FB_WIDTH;
	depthImageLayoutMaker.dimensions[1] = FB_HEIGHT;
	depthImageLayoutMaker.pitchStride = FB_WIDTH * 4;

	// Calculate layout for the depths
	DkImageLayout depthLayout;
	dkImageLayoutInitialize(&depthLayout, &depthImageLayoutMaker);

	// Retrieve necessary size and alignment for the depths
	uint32_t depthSize  = dkImageLayoutGetSize(&depthLayout);
	uint32_t depthAlign = dkImageLayoutGetAlignment(&depthLayout);
	depthSize = (depthSize + depthAlign - 1) &~ (depthAlign - 1);

	LOG("DS %d x %d, size: 0x%x, align: 0x%x", FB_WIDTH, FB_HEIGHT, depthSize, depthAlign);

	// Create a memory block that will host the depths
	dkMemBlockMakerDefaults(&memBlockMaker, g_dk_device, depthSize);
	memBlockMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
	g_depthMemBlock = dkMemBlockCreate(&memBlockMaker);

	dkImageInitialize(&g_depth, &depthLayout, g_depthMemBlock, 0);

	// Create a memory block which will be used for recording command lists using a command buffer
	dkMemBlockMakerDefaults(&memBlockMaker, g_dk_device, CMDMEMSIZE);
	memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
	g_cmdbufMemBlock = dkMemBlockCreate(&memBlockMaker);

	// Create a command buffer object
	DkCmdBufMaker cmdbufMaker;
	dkCmdBufMakerDefaults(&cmdbufMaker, g_dk_device);
	g_cmdbuf = dkCmdBufCreate(&cmdbufMaker);

	// Feed our memory to the command buffer so that we can start recording commands
	dkCmdBufAddMemory(g_cmdbuf, g_cmdbufMemBlock, 0, CMDMEMSIZE);


	inited = 1;
}

int sceGxmBeginScene(SceGxmContext *context, unsigned int flags,
		     const SceGxmRenderTarget *renderTarget, const SceGxmValidRegion *validRegion,
		     SceGxmSyncObject *vertexSyncObject, SceGxmSyncObject *fragmentSyncObject,
		     const SceGxmColorSurface *colorSurface,
		     const SceGxmDepthStencilSurface *depthStencil)
{
	/*DkMemBlock color_surface_block;
	DkImage color_surface_image;
	DkImageView color_surface_view;*/
	/*DkRasterizerState rasterizer_state;
	DkColorState color_state;
	DkColorWriteState color_write_state;
	DkViewport viewport = { 0.0f, 0.0f, (float)renderTarget->params.width, (float)renderTarget->params.height, 0.0f, 1.0f };
	DkScissor scissor = { 0, 0, renderTarget->params.width, renderTarget->params.height };
	DkShader const* shaders[] = { &g_vertexShader, &g_fragmentShader };*/
	//SceGxmColorSurfaceInner *color_surface_inner = (SceGxmColorSurfaceInner *)colorSurface;

	if (context->scene.valid)
		return SCE_GXM_ERROR_WITHIN_SCENE;

	/*color_surface_block = SceSysmem_get_dk_memblock_for_addr(color_surface_inner->data);
	if (!color_surface_block)
		return SCE_GXM_ERROR_INVALID_VALUE;

	LOG("sceGxmBeginScene to renderTarget %p, fragmentSyncObject: %p, w: %" PRId32 ", h: %" PRId32 ", stride: %" PRId32 ", CPU addr: %p",
		renderTarget, fragmentSyncObject, color_surface_inner->width, color_surface_inner->height,
		color_surface_inner->strideInPixels,
		dkMemBlockGetCpuAddr(color_surface_block));

	dk_image_view_for_gxm_color_surface(&color_surface_image, &color_surface_view,
					    color_surface_block, color_surface_inner);*/

	init();

	//dkQueueWaitIdle(g_render_queue);

	//dkCmdBufClear(context->cmdbuf);

	DkImageView imageView;
	static uint32_t frame = 0;
	dkImageViewDefaults(&imageView, &g_framebuffers[frame]);
	frame = (frame + 1) % FB_NUM;

	DkImageView depthView;
	dkImageViewDefaults(&depthView, &g_depth);
	dkCmdBufBindRenderTarget(g_cmdbuf, &imageView, &depthView);

	// Declare structs that will be used for binding state
	DkViewport viewport = { 0.0f, 0.0f, (float)FB_WIDTH, (float)FB_HEIGHT, 0.0f, 1.0f };
	DkScissor scissor = { 0, 0, FB_WIDTH, FB_HEIGHT };
	DkShader const* shaders[] = { &g_vertexShader, &g_fragmentShader };
	DkRasterizerState rasterizerState;
	DkColorState colorState;
	DkColorWriteState colorWriteState;

	// Initialize state structs with the deko3d defaults
	dkRasterizerStateDefaults(&rasterizerState);
	dkColorStateDefaults(&colorState);
	dkColorWriteStateDefaults(&colorWriteState);

	// Generate the main rendering command list
	dkCmdBufSetViewports(g_cmdbuf, 0, &viewport, 1);
	dkCmdBufSetScissors(g_cmdbuf, 0, &scissor, 1);
	dkCmdBufClearColorFloat(g_cmdbuf, 0, DkColorMask_RGBA, 0.125f, 0.294f, 0.478f, 1.0f);
	dkCmdBufBindShaders(g_cmdbuf, DkStageFlag_GraphicsMask, shaders, sizeof(shaders)/sizeof(shaders[0]));
	dkCmdBufBindRasterizerState(g_cmdbuf, &rasterizerState);
	dkCmdBufBindColorState(g_cmdbuf, &colorState);
	dkCmdBufBindColorWriteState(g_cmdbuf, &colorWriteState);
	dkCmdBufDraw(g_cmdbuf, DkPrimitive_Triangles, 3, 1, 0, 0);
	g_cmdsRender = dkCmdBufFinishList(g_cmdbuf);

	dkQueueSubmitCommands(g_render_queue, g_cmdsRender);

	dkQueueWaitIdle(g_render_queue);
	dkCmdBufClear(g_cmdbuf);

	svcSleepThread(16 * 1000 * 1000ull);

	context->vertex_ringbuf.head = 0;
	context->fragment_ringbuf.head = 0;
	context->scene.valid = true;

	return 0;
}

int sceGxmEndScene(SceGxmContext *context, const SceGxmNotification *vertexNotification,
		   const SceGxmNotification *fragmentNotification)
{
	DkCmdList cmd_list;
	DkVariable variable;
	uint32_t offset;

	LOG("sceGxmEndScene");

	if (!context->scene.valid)
		return SCE_GXM_ERROR_NOT_WITHIN_SCENE;

	// Fragment barrier, to make sure we finish previous work before discarding the depth buffer
	//dkCmdBufBarrier(context->cmdbuf, DkBarrier_Fragments, 0);

	// Discard the depth buffer since we don't need it anymore
	//dkCmdBufDiscardDepthStencil(context->cmdbuf);

	/*cmd_list = dkCmdBufFinishList(context->cmdbuf);
	dkQueueSubmitCommands(g_render_queue, cmd_list);

	dkQueueWaitIdle(g_render_queue);*/

	//svcSleepThread(100 * 1000 * 1000ull);
	//dkCmdBufClear(context->cmdbuf);

	context->scene.valid = false;

	return 0;
}

static int SceGxmDisplayQueue_thread(SceSize args, void *argp)
{
	DisplayQueueControlBlock *queue = *(DisplayQueueControlBlock **)argp;
	DisplayQueueEntry *entry;

	ueventSignal(&queue->ready_evflag);

	while (1)
		svcSleepThread(1000 * 1000 * 100ull);

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

	return 0;

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
	context->vertex_program = vertexProgram;
}

void sceGxmSetFragmentProgram(SceGxmContext *context, const SceGxmFragmentProgram *fragmentProgram)
{
	context->fragment_program = fragmentProgram;
}

int sceGxmSetVertexStream(SceGxmContext *context, unsigned int streamIndex, const void *streamData)
{
	DkVtxAttribState vertex_attrib_state[SCE_GXM_MAX_VERTEX_ATTRIBUTES];
	DkVtxBufferState vertex_buffer_state;
	VitaMemBlockInfo *stream_block;
	ptrdiff_t stream_offset;
	SceGxmVertexAttribute *attributes = context->vertex_program->attributes;
	uint32_t attribute_count = context->vertex_program->attributeCount;
	SceGxmVertexStream *streams = context->vertex_program->streams;

	return 0;

	stream_block = SceSysmem_get_vita_memblock_info_for_addr(streamData);
	if (!stream_block)
		return SCE_GXM_ERROR_INVALID_VALUE;

	stream_offset = (uintptr_t)streamData - (uintptr_t)stream_block->base;
	assert(stream_offset >= 0);
	dkCmdBufBindVtxBuffer(context->cmdbuf, streamIndex,
			      dkMemBlockGetGpuAddr(stream_block->dk_memblock),
			      dkMemBlockGetSize(stream_block->dk_memblock));

	memset(vertex_attrib_state, 0, attribute_count * sizeof(DkVtxAttribState));
	for (uint32_t i = 0; i < attribute_count; i++) {
		vertex_attrib_state[i].bufferId = streamIndex;
		vertex_attrib_state[i].isFixed = 0;
		vertex_attrib_state[i].offset = attributes[i].offset;
		vertex_attrib_state[i].size = gxm_to_dk_vtx_attrib_size(attributes[i].format,
								        attributes[i].componentCount);
		vertex_attrib_state[i].type = gxm_to_dk_vtx_attrib_type(attributes[i].format);
		vertex_attrib_state[i].isBgra = 0;
	}

	vertex_buffer_state.stride = streams[streamIndex].stride;
	vertex_buffer_state.divisor = 0;

	dkCmdBufBindVtxAttribState(context->cmdbuf, vertex_attrib_state, attribute_count);
	dkCmdBufBindVtxBufferState(context->cmdbuf, &vertex_buffer_state, 1);

	return 0;
}

int sceGxmReserveVertexDefaultUniformBuffer(SceGxmContext *context, void **uniformBuffer)
{
	DkBufExtents buf_extents;
	uint32_t default_uniform_buffer_count;
	uint32_t uniform_buf_size;
	const SceGxmProgram *program;

	if (!context->scene.valid)
		return SCE_GXM_ERROR_NOT_WITHIN_SCENE;
	else if (!context->vertex_program)
		return SCE_GXM_ERROR_NULL_PROGRAM;

	return 0;

	program = context->vertex_program->programId->programHeader;

	default_uniform_buffer_count = program->default_uniform_buffer_count;
	if (default_uniform_buffer_count == 0) {
		*uniformBuffer = NULL;
		return 0;
	}

	uniform_buf_size = ALIGN(default_uniform_buffer_count * sizeof(float), DK_UNIFORM_BUF_ALIGNMENT);

	buf_extents.addr = dkMemBlockGetGpuAddr(context->vertex_ringbuf.memblock) + context->vertex_ringbuf.head;
	buf_extents.size = uniform_buf_size;

	dkCmdBufBindUniformBuffers(context->cmdbuf, DkStage_Vertex, 0, &buf_extents, 1);

	*uniformBuffer = dkMemBlockGetCpuAddr(context->vertex_ringbuf.memblock) + context->vertex_ringbuf.head;
	context->vertex_ringbuf.head += uniform_buf_size;

	return 0;
}

int sceGxmReserveFragmentDefaultUniformBuffer(SceGxmContext *context, void **uniformBuffer)
{
	DkBufExtents buf_extents;
	uint32_t default_uniform_buffer_count;
	uint32_t uniform_buf_size;
	const SceGxmProgram *program;

	return 0;

	if (!context->scene.valid)
		return SCE_GXM_ERROR_NOT_WITHIN_SCENE;
	else if (!context->fragment_program)
		return SCE_GXM_ERROR_NULL_PROGRAM;

	program = context->fragment_program->programId->programHeader;

	default_uniform_buffer_count = program->default_uniform_buffer_count;
	if (default_uniform_buffer_count == 0) {
		*uniformBuffer = NULL;
		return 0;
	}

	uniform_buf_size = ALIGN(default_uniform_buffer_count * sizeof(float), DK_UNIFORM_BUF_ALIGNMENT);

	buf_extents.addr = dkMemBlockGetGpuAddr(context->fragment_ringbuf.memblock) + context->fragment_ringbuf.head;
	buf_extents.size = uniform_buf_size;

	dkCmdBufBindUniformBuffers(context->cmdbuf, DkStage_Fragment, 0, &buf_extents, 1);

	*uniformBuffer = dkMemBlockGetCpuAddr(context->fragment_ringbuf.memblock) + context->fragment_ringbuf.head;
	context->fragment_ringbuf.head += uniform_buf_size;

	return 0;
}

int sceGxmSetUniformDataF(void *uniformBuffer, const SceGxmProgramParameter *parameter,
			  unsigned int componentOffset, unsigned int componentCount,
			  const float *sourceData)
{
	uint32_t component_size = gxm_parameter_type_size(parameter->type);
	uint32_t dst_offset = component_size * (parameter->resource_index + componentOffset);
	uint32_t copy_size = component_size * componentCount;

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

int sceGxmDraw(SceGxmContext *context, SceGxmPrimitiveType primType, SceGxmIndexFormat indexType,
	       const void *indexData, unsigned int indexCount)
{
	VitaMemBlockInfo *index_block;
	ptrdiff_t index_offset;

	return 0;

	LOG("sceGxmDraw: primType: 0x%x, indexCount: %d", primType, indexCount);

	index_block = SceSysmem_get_vita_memblock_info_for_addr(indexData);
	if (!index_block)
		return SCE_GXM_ERROR_INVALID_VALUE;

	index_offset = (uintptr_t)indexData - (uintptr_t)index_block->base;
	assert(index_offset >= 0);
	LOG("  index offset: 0x%x", index_offset);
	dkCmdBufBindIdxBuffer(context->cmdbuf, gxm_to_dk_idx_format(indexType),
			      dkMemBlockGetGpuAddr(index_block->dk_memblock));
	dkCmdBufDrawIndexed(context->cmdbuf, gxm_to_dk_primitive(primType), indexCount, 1, 0, index_offset, 0);

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
