#include <stdlib.h>
#include <switch.h>
#include <deko3d.h>
#include <psp2/kernel/error.h>
#include <psp2/gxm.h>
#include "SceGxm.h"
#include "gxm_to_dk.h"
#include "module.h"
#include "protected_bitset.h"
#include "utils.h"
#include "log.h"

#include "basic_vsh_dksh.h"
#include "color_fsh_dksh.h"

#define MAX_GXM_MAPPED_MEMORY_BLOCKS	256

typedef struct SceGxmContext {
	SceGxmContextParams params;
	DkQueue render_queue;
	DkMemBlock cmdbuf_memblock;
	DkCmdBuf cmdbuf;
	struct {
		DkMemBlock memblock;
		uint32_t head;
	} vertex_ringbuf;
	const SceGxmVertexProgram *vertex_program;
	const SceGxmFragmentProgram *fragment_program;
	struct {
		bool valid;
	} scene;
} SceGxmContext;
static_assert(sizeof(SceGxmContext) <= SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE, "Oversized SceGxmContext");

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

typedef struct {
	uint32_t index;
	DkMemBlock memblock;
	void *base;
	SceSize size;
} GxmMappedMemoryBlock;

DECL_PROTECTED_BITSET(GxmMappedMemoryBlock, gxm_mapped_memory_blocks, MAX_GXM_MAPPED_MEMORY_BLOCKS)
DECL_PROTECTED_BITSET_ALLOC(gxm_mapped_memory_block_alloc, gxm_mapped_memory_blocks, GxmMappedMemoryBlock)
DECL_PROTECTED_BITSET_RELEASE(gxm_mapped_memory_block_release, gxm_mapped_memory_blocks, GxmMappedMemoryBlock)
DECL_PROTECTED_BITSET_GET_CMP(gxm_mapped_memory_block_get, gxm_mapped_memory_blocks, GxmMappedMemoryBlock, const void *, base,
			      base >= g_gxm_mapped_memory_blocks[index].base &&
			      base < (g_gxm_mapped_memory_blocks[index].base + g_gxm_mapped_memory_blocks[index].size))

static SceGxmInitializeParams g_gxm_init_params;
static bool g_gxm_initialized;

/* Deko3D */

typedef struct {
	float position[3];
	float color[3];
} Vertex;

#define FB_NUM		2
#define FB_WIDTH	960
#define FB_HEIGHT	544
#define CODEMEMSIZE	(64*1024)
#define CMDMEMSIZE	(16*1024)
#define DYNCMDMEMSIZE	(128*1024*1024)
#define DATAMEMSIZE	(1*1024*1024)

static DkDevice g_device;
static DkMemBlock g_codeMemBlock;
static uint32_t g_codeMemOffset;
static DkShader g_vertexShader;
static DkShader g_fragmentShader;

static void load_shader_memory(DkMemBlock memblock, DkShader *shader, uint32_t *offset, const void *data, uint32_t size)
{
	DkShaderMaker shader_maker;

	memcpy((char *)dkMemBlockGetCpuAddr(g_codeMemBlock) + *offset, data, size);
	dkShaderMakerDefaults(&shader_maker, memblock, *offset);
	dkShaderInitialize(shader, &shader_maker);

	*offset += ALIGN(size, DK_SHADER_CODE_ALIGNMENT);
}

int sceGxmInitialize(const SceGxmInitializeParams *params)
{
	DkDeviceMaker deviceMaker;
	DkMemBlockMaker memBlockMaker;

	if (g_gxm_initialized)
		return SCE_GXM_ERROR_ALREADY_INITIALIZED;

	dkDeviceMakerDefaults(&deviceMaker);
	g_device = dkDeviceCreate(&deviceMaker);

	dkMemBlockMakerDefaults(&memBlockMaker, g_device, CODEMEMSIZE);
	memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code;
	g_codeMemBlock = dkMemBlockCreate(&memBlockMaker);

	g_codeMemOffset = 0;
	load_shader_memory(g_codeMemBlock, &g_vertexShader, &g_codeMemOffset, basic_vsh_dksh, basic_vsh_dksh_size);
	load_shader_memory(g_codeMemBlock, &g_fragmentShader, &g_codeMemOffset, color_fsh_dksh, color_fsh_dksh_size);

	g_gxm_init_params = *params;
	g_gxm_initialized = true;
	return 0;
}

int sceGxmTerminate()
{
	dkMemBlockDestroy(g_codeMemBlock);
	dkDeviceDestroy(g_device);
	g_gxm_initialized = false;
	return 0;
}

int sceGxmCreateContext(const SceGxmContextParams *params, SceGxmContext **context)
{
	DkMemBlockMaker memblock_maker;
	DkCmdBufMaker cmdbuf_maker;
	DkQueueMaker queue_maker;
	SceGxmContext *ctx = params->hostMem;

	if (params->hostMemSize < sizeof(SceGxmContext))
		return SCE_GXM_ERROR_INVALID_VALUE;

	memset(ctx, 0, sizeof(*ctx));
	ctx->params = *params;

	/* Create graphics queue */
	dkQueueMakerDefaults(&queue_maker, g_device);
	queue_maker.flags = DkQueueFlags_Graphics;
	ctx->render_queue = dkQueueCreate(&queue_maker);

	/* Create backing storage buffer for the main command buffer */
	dkMemBlockMakerDefaults(&memblock_maker, g_device, params->vdmRingBufferMemSize);
	memblock_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
	memblock_maker.storage = params->vdmRingBufferMem;
	ctx->cmdbuf_memblock = dkMemBlockCreate(&memblock_maker);
	assert(ctx->cmdbuf_memblock);

	/* Create the command buffer */
	dkCmdBufMakerDefaults(&cmdbuf_maker, g_device);
	ctx->cmdbuf = dkCmdBufCreate(&cmdbuf_maker);
	assert(ctx->cmdbuf);

	/* Assing the backing storage buffer to the main command buffer */
	dkCmdBufAddMemory(ctx->cmdbuf, ctx->cmdbuf_memblock, 0, ctx->params.vdmRingBufferMemSize);

	/* Create ringbuffer for generic vertex data and vertex default uniform buffer reservations */
	dkMemBlockMakerDefaults(&memblock_maker, g_device, params->vertexRingBufferMemSize);
	memblock_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
	memblock_maker.storage = params->vertexRingBufferMem;
	ctx->vertex_ringbuf.memblock = dkMemBlockCreate(&memblock_maker);
	assert(ctx->vertex_ringbuf.memblock);
	assert(params->vertexRingBufferMem == dkMemBlockGetCpuAddr(ctx->vertex_ringbuf.memblock));

	ctx->vertex_ringbuf.head = 0;
	*context = ctx;

	return 0;
}

int sceGxmDestroyContext(SceGxmContext *context)
{
	dkQueueWaitIdle(context->render_queue);

	dkMemBlockDestroy(context->vertex_ringbuf.memblock);
	dkCmdBufDestroy(context->cmdbuf);
	dkMemBlockDestroy(context->cmdbuf_memblock);
	dkQueueDestroy(context->render_queue);

	return 0;
}

void sceGxmFinish(SceGxmContext *context)
{
	dkQueueWaitIdle(context->render_queue);
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
	return 0;
}

int sceGxmShaderPatcherReleaseFragmentProgram(SceGxmShaderPatcher *shaderPatcher,
					      SceGxmFragmentProgram *fragmentProgram)
{
	return 0;
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

int sceGxmBeginScene(SceGxmContext *context, unsigned int flags,
		     const SceGxmRenderTarget *renderTarget, const SceGxmValidRegion *validRegion,
		     SceGxmSyncObject *vertexSyncObject, SceGxmSyncObject *fragmentSyncObject,
		     const SceGxmColorSurface *colorSurface,
		     const SceGxmDepthStencilSurface *depthStencil)
{
	DkImageLayoutMaker image_layout_maker;
	DkImageLayout image_layout;
	DkImageView image_view;
	DkImage framebuffer;
	DkViewport viewport = { 0.0f, 0.0f, (float)renderTarget->params.width, (float)renderTarget->params.height, 0.0f, 1.0f };
	DkScissor scissor = { 0, 0, renderTarget->params.width, renderTarget->params.height };
	DkShader const* shaders[] = { &g_vertexShader, &g_fragmentShader };
	SceGxmColorSurfaceInner *color_surface_inner = (SceGxmColorSurfaceInner *)colorSurface;
	GxmMappedMemoryBlock *color_surface_block;
	uint32_t image_block_offset;
	DkRasterizerState rasterizerState;
	DkColorState colorState;
	DkColorWriteState colorWriteState;

	if (context->scene.valid)
		return SCE_GXM_ERROR_WITHIN_SCENE;

	color_surface_block = gxm_mapped_memory_block_get(color_surface_inner->data);
	if (!color_surface_block)
		return SCE_GXM_ERROR_INVALID_VALUE;

	LOG("sceGxmBeginScene to renderTarget %p, w: %ld, h: %ld, stride: %ld, CPU addr: %p",
		renderTarget, color_surface_inner->width, color_surface_inner->height,
		color_surface_inner->strideInPixels,
		dkMemBlockGetCpuAddr(color_surface_block->memblock));

	// memset(dkMemBlockGetCpuAddr(color_surface_block->memblock), 0, color_surface_inner->strideInPixels * 32);

	dkImageLayoutMakerDefaults(&image_layout_maker, g_device);
	image_layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_PitchLinear;
	image_layout_maker.format = DkImageFormat_RGBA8_Unorm;
	image_layout_maker.dimensions[0] = color_surface_inner->width;
	image_layout_maker.dimensions[1] = color_surface_inner->height;
	image_layout_maker.pitchStride = color_surface_inner->strideInPixels *
					 gxm_color_format_bytes_per_pixel(color_surface_inner->colorFormat);
	dkImageLayoutInitialize(&image_layout, &image_layout_maker);
	image_block_offset = (uintptr_t)color_surface_inner->data - (uintptr_t)color_surface_block->base;
	dkImageInitialize(&framebuffer, &image_layout, color_surface_block->memblock ,
			  image_block_offset);
	dkImageViewDefaults(&image_view, &framebuffer);

	dkRasterizerStateDefaults(&rasterizerState);
	dkColorStateDefaults(&colorState);
	dkColorWriteStateDefaults(&colorWriteState);

	dkCmdBufClear(context->cmdbuf);

	dkCmdBufBindRenderTarget(context->cmdbuf, &image_view, NULL);

	dkCmdBufSetViewports(context->cmdbuf, 0, &viewport, 1);
	dkCmdBufSetScissors(context->cmdbuf, 0, &scissor, 1);
	dkCmdBufClearColorFloat(context->cmdbuf, 0, DkColorMask_RGBA, 0.125f, 0.294f, 0.478f, 1.0f);
	dkCmdBufBindShaders(context->cmdbuf, DkStageFlag_GraphicsMask, shaders, ARRAY_SIZE(shaders));
	dkCmdBufBindRasterizerState(context->cmdbuf, &rasterizerState);
	dkCmdBufBindColorState(context->cmdbuf, &colorState);
	dkCmdBufBindColorWriteState(context->cmdbuf, &colorWriteState);

	context->vertex_ringbuf.head = 0;
	context->scene.valid = true;

	return 0;
}

int sceGxmEndScene(SceGxmContext *context, const SceGxmNotification *vertexNotification,
		   const SceGxmNotification *fragmentNotification)
{
	DkCmdList cmd_list;

	LOG("sceGxmEndScene");

	if (!context->scene.valid)
		return SCE_GXM_ERROR_NOT_WITHIN_SCENE;

	cmd_list = dkCmdBufFinishList(context->cmdbuf);
	dkQueueSubmitCommands(context->render_queue, cmd_list);

	//DkFence fence;
	//dkQueueWaitFence(context->render_queue, &fence);
	//dkFenceWait(&fence, INT64_MAX);
	//svcSleepThread(100 * 1000 * 1000);

	//dkQueueFlush(context->render_queue);
	dkQueueWaitIdle(context->render_queue);

	//dkQueuePresentImage(context->render_queue, swapchain, slot);
	//dkSwapchainDestroy(swapchain);

	context->scene.valid = false;

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
	GxmMappedMemoryBlock *stream_block;
	uint32_t stream_offset;
	SceGxmVertexAttribute *attributes = context->vertex_program->attributes;
	uint32_t attribute_count = context->vertex_program->attributeCount;
	SceGxmVertexStream *streams = context->vertex_program->streams;

	stream_block = gxm_mapped_memory_block_get(streamData);
	if (!stream_block)
		return SCE_GXM_ERROR_INVALID_VALUE;

	stream_offset = (uintptr_t)streamData - (uintptr_t)stream_block->base;
	dkCmdBufBindVtxBuffer(context->cmdbuf, streamIndex,
			      dkMemBlockGetGpuAddr(stream_block->memblock) + stream_offset,
			      stream_block->size - stream_offset);

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

int sceGxmDraw(SceGxmContext *context, SceGxmPrimitiveType primType, SceGxmIndexFormat indexType,
	       const void *indexData, unsigned int indexCount)
{
	GxmMappedMemoryBlock *index_block;
	uint32_t index_offset;

	LOG("sceGxmDraw: primType: 0x%x, indexCount: %d", primType, indexCount);

	index_block = gxm_mapped_memory_block_get(indexData);
	if (!index_block)
		return SCE_GXM_ERROR_INVALID_VALUE;

	index_offset = (uintptr_t)indexData - (uintptr_t)index_block->base;
	dkCmdBufBindIdxBuffer(context->cmdbuf, gxm_to_dk_idx_format(indexType),
			      dkMemBlockGetGpuAddr(index_block->memblock) + index_offset);
	dkCmdBufDrawIndexed(context->cmdbuf, gxm_to_dk_primitive(primType), indexCount, 1, 0, 0, 0);

	return 0;
}

int sceGxmMapMemory(void *base, SceSize size, SceGxmMemoryAttribFlags attr)
{
	GxmMappedMemoryBlock *block;
	DkMemBlockMaker memblock_maker;

	/* It is not valid to map a memory range where all or part
	   of that range has already been mapped */
	block = gxm_mapped_memory_block_get(base);
	if (block)
		return SCE_GXM_ERROR_INVALID_POINTER;

	block = gxm_mapped_memory_block_alloc();
	if (!block)
		return SCE_KERNEL_ERROR_NO_MEMORY;

	dkMemBlockMakerDefaults(&memblock_maker, g_device, size);
	memblock_maker.device = g_device;
	memblock_maker.size = size;
	memblock_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
	memblock_maker.storage = base;

	block->memblock = dkMemBlockCreate(&memblock_maker);
	if (!block->memblock) {
		gxm_mapped_memory_block_release(block);
		return SCE_GXM_ERROR_INVALID_POINTER;
	}

	assert(base == dkMemBlockGetCpuAddr(block->memblock));
	block->base = base;
	block->size = size;

	return 0;
}

int sceGxmUnmapMemory(void *base)
{
	GxmMappedMemoryBlock *block = gxm_mapped_memory_block_get(base);
	if (!block)
		return SCE_GXM_ERROR_INVALID_POINTER;

	gxm_mapped_memory_block_release(block);

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
		{0x05032658, sceGxmShaderPatcherCreate},
		{0xEAA5B100, sceGxmShaderPatcherDestroy},
		{0x2B528462, sceGxmShaderPatcherRegisterProgram},
		{0xF103AF8A, sceGxmShaderPatcherUnregisterProgram},
		{0xB7BBA6D5, sceGxmShaderPatcherCreateVertexProgram},
		{0xAC1FF2DA, sceGxmShaderPatcherReleaseVertexProgram},
		{0x4ED2E49D, sceGxmShaderPatcherCreateFragmentProgram},
		{0xBE2743D1, sceGxmShaderPatcherReleaseFragmentProgram},
		{0xB291C959, sceGxmGetRenderTargetMemSize},
		{0x207AF96B, sceGxmCreateRenderTarget},
		{0x0B94C50A, sceGxmDestroyRenderTarget},
		{0xED0F6E25, sceGxmColorSurfaceInit},
		{0x8734FF4E, sceGxmBeginScene},
		{0xFE300E2F, sceGxmEndScene},
		{0x31FF8ABD, sceGxmSetVertexProgram},
		{0xAD2F48D9, sceGxmSetFragmentProgram},
		{0x895DF2E9, sceGxmSetVertexStream},
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

int SceGxm_init(void)
{
	return 0;
}

int SceGxm_finish(void)
{
	return 0;
}
