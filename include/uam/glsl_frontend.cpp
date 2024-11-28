#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glsl/ast.h"
#include "glsl/glsl_parser_extras.h"
#include "glsl/ir_optimization.h"
#include "glsl/program.h"
#include "glsl/loop_analysis.h"
#include "glsl/standalone_scaffolding.h"
#include "glsl/string_to_uint_map.h"
#include "util/set.h"
#include "glsl/linker.h"
#include "glsl/glsl_parser_extras.h"
#include "glsl/builtin_functions.h"
#include "glsl/opt_add_neg_to_sub.h"
#include "main/mtypes.h"
#include "program/program.h"
#include "state_tracker/st_glsl_to_tgsi.h"
#include "tgsi/tgsi_from_mesa.h"
#include "glsl/ir_uniform.h"
#include "pipe/p_state.h"

extern "C"
{
#include "tgsi/tgsi_parse.h"
}

#include "glsl_frontend.h"

class dead_variable_visitor : public ir_hierarchical_visitor {
public:
	dead_variable_visitor()
	{
		variables = _mesa_set_create(NULL,
									_mesa_hash_pointer,
									_mesa_key_pointer_equal);
	}

	virtual ~dead_variable_visitor()
	{
		_mesa_set_destroy(variables, NULL);
	}

	virtual ir_visitor_status visit(ir_variable *ir)
	{
		/* If the variable is auto or temp, add it to the set of variables that
		* are candidates for removal.
		*/
		if (ir->data.mode != ir_var_auto && ir->data.mode != ir_var_temporary)
			return visit_continue;

		_mesa_set_add(variables, ir);

		return visit_continue;
	}

	virtual ir_visitor_status visit(ir_dereference_variable *ir)
	{
		struct set_entry *entry = _mesa_set_search(variables, ir->var);

		/* If a variable is dereferenced at all, remove it from the set of
		* variables that are candidates for removal.
		*/
		if (entry != NULL)
			_mesa_set_remove(variables, entry);

		return visit_continue;
	}

	void remove_dead_variables()
	{
		set_foreach(variables, entry) {
			ir_variable *ir = (ir_variable *) entry->key;

			assert(ir->ir_type == ir_type_variable);
			ir->remove();
		}
	}

private:
	set *variables;
};

struct gl_program_with_tgsi : public gl_program
{
	struct glsl_to_tgsi_visitor *glsl_to_tgsi;
	const tgsi_token *tgsi_tokens;
	unsigned int tgsi_num_tokens;
	int8_t vtx_in_locations[PIPE_MAX_ATTRIBS];

	void cleanup()
	{
		if (glsl_to_tgsi)
		{
			free_glsl_to_tgsi_visitor(glsl_to_tgsi);
			glsl_to_tgsi = NULL;
		}
		if (tgsi_tokens)
		{
			tgsi_free_tokens(tgsi_tokens);
			tgsi_tokens = NULL;
			tgsi_num_tokens = 0;
		}
	}

	static gl_program_with_tgsi* from_ptr(void* p)
	{
		return static_cast<gl_program_with_tgsi*>(p);;
	}
};

static void
destroy_gl_program_with_tgsi(void* p)
{
	gl_program_with_tgsi::from_ptr(p)->cleanup();
}

static void
init_gl_program(struct gl_program *prog, GLenum target, bool is_arb_asm)
{
	prog->RefCount = 1;
	prog->Target = target;
	prog->Format = GL_PROGRAM_FORMAT_ASCII_ARB;
	prog->info.stage = (gl_shader_stage)_mesa_program_enum_to_shader_stage(target);
	prog->is_arb_asm = is_arb_asm;
}

static struct gl_program *
new_program(UNUSED struct gl_context *ctx, GLenum target,
            UNUSED GLuint id, bool is_arb_asm)
{
	switch (target) {
	case GL_VERTEX_PROGRAM_ARB: /* == GL_VERTEX_PROGRAM_NV */
	case GL_GEOMETRY_PROGRAM_NV:
	case GL_TESS_CONTROL_PROGRAM_NV:
	case GL_TESS_EVALUATION_PROGRAM_NV:
	case GL_FRAGMENT_PROGRAM_ARB:
	case GL_COMPUTE_PROGRAM_NV: {
		struct gl_program_with_tgsi *prog = rzalloc(NULL, struct gl_program_with_tgsi);
		ralloc_set_destructor(prog, destroy_gl_program_with_tgsi);
		init_gl_program(prog, target, is_arb_asm);
		return prog;
	}
	default:
		printf("bad target in new_program\n");
		return NULL;
	}
}

void
attach_visitor_to_program(struct gl_program *prog, struct glsl_to_tgsi_visitor *v)
{
	gl_program_with_tgsi* prg = gl_program_with_tgsi::from_ptr(prog);
	prg->cleanup();
	prg->glsl_to_tgsi = v;
}

struct glsl_to_tgsi_visitor*
_glsl_program_get_tgsi_visitor(struct gl_program *prog)
{
	return gl_program_with_tgsi::from_ptr(prog)->glsl_to_tgsi;
}

void
_glsl_program_attach_tgsi_tokens(struct gl_program *prog, const tgsi_token *tokens, unsigned int num)
{
	gl_program_with_tgsi* prg = gl_program_with_tgsi::from_ptr(prog);
	prg->cleanup();
	prg->tgsi_tokens = tokens;
	prg->tgsi_num_tokens = num;
}

static void
initialize_context(struct gl_context *ctx, gl_api api)
{
	initialize_context_to_defaults(ctx, api);

	ctx->Const.MaxPatchVertices = MAX_PATCH_VERTICES;

	// Adapted from st_init_extensions
	ctx->Const.GLSLVersion = 460;
	ctx->Const.NativeIntegers = GL_TRUE;
	ctx->Const.MaxClipPlanes = 8;
	ctx->Const.UniformBooleanTrue = ~0U;
	ctx->Const.MaxSamples = 8;
	ctx->Const.MaxImageSamples = 8;
	ctx->Const.MaxColorTextureSamples = 8;
	ctx->Const.MaxDepthTextureSamples = 8;
	ctx->Const.MaxIntegerSamples = 8;
	ctx->Const.MaxFramebufferSamples = 8;
	ctx->Const.MaxColorFramebufferSamples = 8;
	ctx->Const.MaxColorFramebufferStorageSamples = 8;
	ctx->Const.MaxDepthStencilFramebufferSamples = 8;
	ctx->Const.MinMapBufferAlignment = 64;
	ctx->Const.MaxTextureBufferSize = 128 * 1024 * 1024;
	ctx->Const.TextureBufferOffsetAlignment = 16;
	ctx->Const.MaxViewports = 16;
	ctx->Const.ViewportBounds.Min = -32768.0;
	ctx->Const.ViewportBounds.Max = 32767.0;
	ctx->Const.MaxComputeWorkGroupInvocations = 1024;
	ctx->Const.MaxComputeSharedMemorySize = 64 << 10;
	ctx->Const.MaxComputeWorkGroupCount[0] = 0x7fffffff;
	ctx->Const.MaxComputeWorkGroupCount[1] = 65535;
	ctx->Const.MaxComputeWorkGroupCount[2] = 65535;
	ctx->Const.MaxComputeWorkGroupSize[0] = 1024;
	ctx->Const.MaxComputeWorkGroupSize[1] = 1024;
	ctx->Const.MaxComputeWorkGroupSize[2] = 64;
	ctx->Const.MaxComputeVariableGroupSize[0] = 1024;
	ctx->Const.MaxComputeVariableGroupSize[1] = 1024;
	ctx->Const.MaxComputeVariableGroupSize[2] = 64;
	ctx->Const.MaxComputeVariableGroupInvocations = 1024;
	ctx->Const.NoPrimitiveBoundingBoxOutput = true;

	// Adapted from st_init_limits
	ctx->Const.MaxTextureLevels = 15;
	ctx->Const.Max3DTextureLevels = 12;
	ctx->Const.MaxCubeTextureLevels = 15;
	ctx->Const.MaxTextureRectSize = 16384;
	ctx->Const.MaxArrayTextureLayers = 2048;
	ctx->Const.MaxViewportWidth =
	ctx->Const.MaxViewportHeight =
	ctx->Const.MaxRenderbufferSize = ctx->Const.MaxTextureRectSize;
	ctx->Const.SubPixelBits = 8;
	ctx->Const.ViewportSubpixelBits = 8;
	ctx->Const.MaxDrawBuffers = ctx->Const.MaxColorAttachments = 8;
	ctx->Const.MaxDualSourceDrawBuffers = 1;
	ctx->Const.MaxLineWidth = 10.0f;
	ctx->Const.MaxLineWidthAA = 10.0f;
	ctx->Const.MaxPointSize = 63.0f;
	ctx->Const.MaxPointSizeAA = 63.375f;
	ctx->Const.MinPointSize = 1.0f;
	ctx->Const.MinPointSizeAA = 0.0f;
	ctx->Const.MaxTextureMaxAnisotropy = 16.0f;
	ctx->Const.MaxTextureLodBias = 15.0f;
	ctx->Const.QuadsFollowProvokingVertexConvention = GL_TRUE;
	ctx->Const.MaxUniformBlockSize = 65536;
	for (unsigned sh = 0; sh < PIPE_SHADER_TYPES; ++sh)
	{
		gl_program_constants *pc = &ctx->Const.Program[sh];
		gl_shader_compiler_options *options = &ctx->Const.ShaderCompilerOptions[tgsi_processor_to_shader_stage(sh)];
		pc->MaxTextureImageUnits = 32;
		pc->MaxInstructions = pc->MaxNativeInstructions = 16384;
		pc->MaxAluInstructions = pc->MaxNativeAluInstructions = 16384;
		pc->MaxTexInstructions = pc->MaxNativeTexInstructions = 16384;
		pc->MaxTexIndirections = pc->MaxNativeTexIndirections = 16384;
		pc->MaxAttribs = pc->MaxNativeAttribs = sh == PIPE_SHADER_VERTEX ? 16 : (sh == PIPE_SHADER_FRAGMENT ? (0x1f0 / 16) : (0x200 / 16));
		pc->MaxTemps = pc->MaxNativeTemps = 128;
		pc->MaxAddressRegs = pc->MaxNativeAddressRegs = sh == PIPE_SHADER_VERTEX ? 1 : 0;
		pc->MaxUniformComponents = 65536/4;
		pc->MaxParameters = pc->MaxNativeParameters = pc->MaxUniformComponents / 4;
		pc->MaxInputComponents = pc->MaxAttribs*4;
		pc->MaxOutputComponents = 32*4;
		pc->MaxUniformBlocks = 16; // fincs-note: this is custom - also this doesn't count driver ubos or blockless uniforms
		pc->MaxCombinedUniformComponents = pc->MaxUniformComponents + uint64_t(ctx->Const.MaxUniformBlockSize) / 4 * pc->MaxUniformBlocks;
		pc->MaxShaderStorageBlocks = 16; // fincs-note: this is also custom
		pc->MaxAtomicCounters = 0; // fincs-note: we don't support atomic counters
		pc->MaxAtomicBuffers = 0; // same here
		pc->MaxImageUniforms = 8;
		pc->MaxLocalParams = 4096;
		pc->MaxEnvParams = 4096;
		pc->LowInt.RangeMin = 31;
		pc->LowInt.RangeMax = 30;
		pc->LowInt.Precision = 0;
		pc->MediumInt = pc->HighInt = pc->LowInt;
		options->MaxIfDepth = 16;
		options->EmitNoIndirectOutput = sh == PIPE_SHADER_FRAGMENT ? GL_TRUE : GL_FALSE;
		options->MaxUnrollIterations = 16384;
		options->LowerCombinedClipCullDistance = GL_TRUE;
		options->LowerBufferInterfaceBlocks = GL_TRUE;
	}

	ctx->Const.MaxUserAssignableUniformLocations =
		ctx->Const.Program[MESA_SHADER_VERTEX].MaxUniformComponents +
		ctx->Const.Program[MESA_SHADER_TESS_CTRL].MaxUniformComponents +
		ctx->Const.Program[MESA_SHADER_TESS_EVAL].MaxUniformComponents +
		ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxUniformComponents +
		ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxUniformComponents;

	ctx->Const.LowerTessLevel = GL_TRUE;
	ctx->Const.LowerCsDerivedVariables = GL_TRUE;
	ctx->Const.PrimitiveRestartForPatches = GL_TRUE;

	ctx->Const.MaxCombinedTextureImageUnits =
		ctx->Const.Program[MESA_SHADER_VERTEX].MaxTextureImageUnits; // fincs-note: this is custom (only 1)

	ctx->Const.MaxVarying = ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxAttribs;
	ctx->Const.MaxGeometryOutputVertices = 1024;
	ctx->Const.MaxGeometryTotalOutputComponents = 1024;
	ctx->Const.MaxGeometryShaderInvocations = 32;
	ctx->Const.MaxTessPatchComponents = 30*4;
	ctx->Const.MinProgramTexelOffset = -8;
	ctx->Const.MaxProgramTexelOffset = 7;
	ctx->Const.MaxProgramTextureGatherComponents = 4;
	ctx->Const.MinProgramTextureGatherOffset = -32;
	ctx->Const.MaxProgramTextureGatherOffset = 31;
	ctx->Const.MaxTransformFeedbackBuffers = 4;
	ctx->Const.MaxTransformFeedbackSeparateComponents = 128;
	ctx->Const.MaxTransformFeedbackInterleavedComponents = 128;
	ctx->Const.MaxVertexStreams = 4;
	ctx->Const.MaxVertexAttribStride = 2048;
	ctx->Const.UniformBufferOffsetAlignment = 256;
	ctx->Const.MaxCombinedUniformBlocks = ctx->Const.MaxUniformBufferBindings =
		ctx->Const.Program[MESA_SHADER_VERTEX].MaxUniformBlocks; // fincs-note: this is custom (only 1)
	ctx->Const.GLSLFrontFacingIsSysVal = GL_TRUE;
	ctx->Const.MaxCombinedShaderOutputResources = ctx->Const.MaxDrawBuffers;
	ctx->Const.ShaderStorageBufferOffsetAlignment = 16;
	ctx->Const.MaxCombinedShaderStorageBlocks = ctx->Const.MaxShaderStorageBufferBindings =
		ctx->Const.Program[MESA_SHADER_VERTEX].MaxShaderStorageBlocks; // fincs-note: this is custom (only 1)
	ctx->Const.MaxCombinedShaderOutputResources += ctx->Const.MaxCombinedShaderStorageBlocks;
	ctx->Const.MaxShaderStorageBlockSize = 1 << 27;
	ctx->Const.MaxCombinedImageUniforms =
		ctx->Const.Program[MESA_SHADER_VERTEX].MaxImageUniforms; // fincs-note: this is custom (only 1)
	ctx->Const.MaxCombinedShaderOutputResources += ctx->Const.MaxCombinedImageUniforms;
	ctx->Const.MaxImageUnits = ctx->Const.Program[MESA_SHADER_VERTEX].MaxImageUniforms; // fincs-note: this is also custom
	ctx->Const.MaxFramebufferWidth = ctx->Const.MaxViewportWidth;
	ctx->Const.MaxFramebufferHeight = ctx->Const.MaxViewportHeight;
	ctx->Const.MaxFramebufferLayers = 2048;
	ctx->Const.MaxWindowRectangles = 8;
	ctx->Const.AllowMappedBuffersDuringExecution = GL_TRUE;
	ctx->Const.MaxSubpixelPrecisionBiasBits = 8;
	ctx->Const.ConservativeRasterDilateRange[0] = 0.0f;
	ctx->Const.ConservativeRasterDilateRange[1] = 0.75f;
	ctx->Const.ConservativeRasterDilateGranularity = 0.25f;
	// end

	ctx->Driver.NewProgram = new_program;
}

static struct gl_context gl_ctx;

void glsl_frontend_init()
{
	initialize_context(&gl_ctx, API_OPENGL_CORE);
}

void glsl_frontend_exit()
{
	_mesa_glsl_release_types();
	_mesa_glsl_release_builtin_functions();
}

// Prototypes for translation functions
bool tgsi_translate_vertex(struct gl_context *ctx, struct gl_program *prog, int8_t *out_inlocations);
bool tgsi_translate_tessctrl(struct gl_context *ctx, struct gl_program *prog);
bool tgsi_translate_tesseval(struct gl_context *ctx, struct gl_program *prog);
bool tgsi_translate_geometry(struct gl_context *ctx, struct gl_program *prog);
bool tgsi_translate_fragment(struct gl_context *ctx, struct gl_program *prog);
bool tgsi_translate_compute(struct gl_context *ctx, struct gl_program *prog);

glsl_program glsl_program_create(const char* source, pipeline_stage stage)
{
	struct gl_shader_program *prg;

	prg = rzalloc (NULL, struct gl_shader_program);
	assert(prg != NULL);
	prg->data = rzalloc(prg, struct gl_shader_program_data);
	assert(prg->data != NULL);
	prg->data->InfoLog = ralloc_strdup(prg->data, "");
	prg->SeparateShader = true;
	exec_list_make_empty(&prg->EmptyUniformLocations);

	/* Created just to avoid segmentation faults */
	prg->AttributeBindings = new string_to_uint_map;
	prg->FragDataBindings = new string_to_uint_map;
	prg->FragDataIndexBindings = new string_to_uint_map;

	// Allocate a shader list
	prg->Shaders = reralloc(prg, prg->Shaders, struct gl_shader *, 1);

	// Allocate a shader and add it to the list
	struct gl_shader *shader = rzalloc(prg, gl_shader);
	prg->Shaders[prg->NumShaders] = shader;
	prg->NumShaders++;

	switch (stage)
	{
		case pipeline_stage_vertex:
			shader->Type = GL_VERTEX_SHADER;
			break;
		case pipeline_stage_tess_ctrl:
			shader->Type = GL_TESS_CONTROL_SHADER;
			break;
		case pipeline_stage_tess_eval:
			shader->Type = GL_TESS_EVALUATION_SHADER;
			break;
		case pipeline_stage_geometry:
			shader->Type = GL_GEOMETRY_SHADER;
			break;
		case pipeline_stage_fragment:
			shader->Type = GL_FRAGMENT_SHADER;
			break;
		case pipeline_stage_compute:
			shader->Type = GL_COMPUTE_SHADER;
			break;
		default:
			goto _fail;
	}
	shader->Stage = _mesa_shader_enum_to_shader_stage(shader->Type);
	shader->Source = source;

	// "Compile" the shader
	_mesa_glsl_compile_shader(&gl_ctx, shader, false, false, true);
	if (shader->CompileStatus != COMPILE_SUCCESS)
	{
		fprintf(stderr, "Shader failed to compile.\n");
		if (shader->InfoLog && shader->InfoLog[0])
			fprintf(stderr, "%s\n", shader->InfoLog);
		goto _fail;
	}
	_mesa_clear_shader_program_data(&gl_ctx, prg);

	// Link the shader
	link_shaders(&gl_ctx, prg);
	if (prg->data->LinkStatus != LINKING_SUCCESS)
	{
		fprintf(stderr, "Shader failed to link.\n");
		if (prg->data->InfoLog && prg->data->InfoLog[0])
			fprintf(stderr, "%s\n", prg->data->InfoLog);
		goto _fail;
	}
	else
	{
		struct gl_linked_shader *linked_shader = prg->_LinkedShaders[shader->Stage];

		// Do more optimizations
		add_neg_to_sub_visitor v;
		visit_list_elements(&v, linked_shader->ir);

		dead_variable_visitor dv;
		visit_list_elements(&dv, linked_shader->ir);
		dv.remove_dead_variables();

		// Print IR
		//_mesa_print_ir(stdout, linked_shader->ir, NULL);

		// Do the TGSI conversion
		if (!st_link_shader(&gl_ctx, prg))
		{
			fprintf(stderr, "st_link_shader failed\n");
			goto _fail;
		}

		// Force OriginUpperLeft
		if (linked_shader->Program->OriginUpperLeft)
			fprintf(stderr, "warning: origin_upper_left has no effect\n");
		linked_shader->Program->OriginUpperLeft = GL_TRUE;

		// Check for PixelCenterInteger (unsupported)
		if (linked_shader->Program->PixelCenterInteger == GL_TRUE) {
			fprintf(stderr, "error: pixel_center_integer is not supported\n");
			goto _fail;
		}

		// TGSI generation
		bool rc = false;
		switch (stage)
		{
			case pipeline_stage_vertex:
				rc = tgsi_translate_vertex(&gl_ctx, linked_shader->Program,
					gl_program_with_tgsi::from_ptr(linked_shader->Program)->vtx_in_locations);
				break;
			case pipeline_stage_tess_ctrl:
				rc = tgsi_translate_tessctrl(&gl_ctx, linked_shader->Program);
				break;
			case pipeline_stage_tess_eval:
				rc = tgsi_translate_tesseval(&gl_ctx, linked_shader->Program);
				break;
			case pipeline_stage_geometry:
				rc = tgsi_translate_geometry(&gl_ctx, linked_shader->Program);
				break;
			case pipeline_stage_fragment:
				rc = tgsi_translate_fragment(&gl_ctx, linked_shader->Program);
				break;
			case pipeline_stage_compute:
				rc = tgsi_translate_compute(&gl_ctx, linked_shader->Program);
				break;
			default:
				fprintf(stderr, "Unsupported stage\n");
				goto _fail;
		}

		if (!rc)
		{
			fprintf(stderr, "Translation failed\n");
			goto _fail;
		}

		gl_program_parameter_list *pl = linked_shader->Program->Parameters;
		unsigned last_location = ~0U;
		bool has_uniforms_in_driver_cbuf = false;
		for (unsigned i = 0; i < pl->NumParameters; i ++)
		{
			gl_program_parameter *p = &pl->Parameters[i];
			unsigned location = 0;
			if (!prg->UniformHash->get(location, p->Name))
				continue;
			gl_uniform_storage *storage = &prg->data->UniformStorage[location];
			if (storage->builtin || storage->hidden)
				continue;
			if (location != last_location)
			{
				last_location = location;
				fprintf(stderr, "error: uniform '%s' in driver constbuf (c[0x1][0x%03x]) not supported\n",
					p->Name,
					// "(type=%d dim=%ux%u size=%u)"
					//storage->type->base_type,
					//storage->type->matrix_columns, storage->type->vector_elements,
					//storage->array_elements,
					4*pl->ParameterValueOffset[i]);
				has_uniforms_in_driver_cbuf = true;
			}
		}
		if (has_uniforms_in_driver_cbuf)
			goto _fail;
	}

	return prg;

_fail:
	glsl_program_free(prg);
	return NULL;
}

static struct gl_linked_shader *_glsl_program_get_linked_shader(glsl_program prg)
{
	struct gl_linked_shader *linked_shader = NULL;
	for (int i = 0; !linked_shader && i < MESA_SHADER_STAGES; i ++)
		linked_shader = prg->_LinkedShaders[i];
	return linked_shader;
}

const tgsi_token* glsl_program_get_tokens(glsl_program prg, unsigned int& num_tokens)
{
	struct gl_linked_shader *linked_shader = _glsl_program_get_linked_shader(prg);
	if (!linked_shader)
	{
		num_tokens = 0;
		return NULL;
	}

	gl_program_with_tgsi* prog = gl_program_with_tgsi::from_ptr(linked_shader->Program);
	num_tokens = prog->tgsi_num_tokens;
	return prog->tgsi_tokens;
}

void* glsl_program_get_constant_buffer(glsl_program prg, unsigned int& out_size)
{
	struct gl_linked_shader *linked_shader = _glsl_program_get_linked_shader(prg);
	if (!linked_shader)
	{
		out_size = 0;
		return NULL;
	}

	gl_program_parameter_list *pl = linked_shader->Program->Parameters;
	out_size = 4*pl->NumParameterValues;
	return pl->ParameterValues;
}

int8_t const* glsl_program_vertex_get_in_locations(glsl_program prg)
{
	struct gl_linked_shader *linked_shader = _glsl_program_get_linked_shader(prg);
	if (!linked_shader)
		return nullptr;

	return gl_program_with_tgsi::from_ptr(linked_shader->Program)->vtx_in_locations;
}

unsigned glsl_program_compute_get_shared_size(glsl_program prg)
{
	struct gl_linked_shader *linked_shader = _glsl_program_get_linked_shader(prg);
	if (!linked_shader)
		return 0;

	return linked_shader->Program->info.cs.shared_size;
}

void glsl_program_free(glsl_program prg)
{
	for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
		if (prg->_LinkedShaders[i])
			ralloc_free(prg->_LinkedShaders[i]->Program);
	}

	delete prg->AttributeBindings;
	delete prg->FragDataBindings;
	delete prg->FragDataIndexBindings;

	ralloc_free(prg);
}
