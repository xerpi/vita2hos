#include "compiler_iface.h"

namespace
{
	constexpr unsigned s_shaderStartOffset = 0x80 - sizeof(NvShaderHeader);

	template <typename T>
	constexpr T Align256(T x)
	{
		return (x + 0xFF) &~ 0xFF;
	}

	void FileWritePadding(FILE* f, uint32_t req, uint32_t dummy = 0)
	{
		while (req >= sizeof(dummy))
		{
			fwrite(&dummy, 1, sizeof(dummy), f);
			req -= sizeof(dummy);
		}
		if (req)
			fwrite(&dummy, 1, req, f);
	}

	void FileAlign256(FILE* f)
	{
		long pos = ftell(f);
		FileWritePadding(f, Align256(pos) - pos);
	}
}

/* NOTE: Using a[0x270] in FP may cause an error even if we're using less than
 * 124 scalar varying values.
 */
static uint32_t
nvc0_shader_input_address(unsigned sn, unsigned si)
{
	switch (sn) {
	case TGSI_SEMANTIC_TESSOUTER:     return NvAttrib_TessOuter(si);
	case TGSI_SEMANTIC_TESSINNER:     return NvAttrib_TessInner(si);
	case TGSI_SEMANTIC_PATCH:         return NvAttrib_Patch(si);
	case TGSI_SEMANTIC_PRIMID:        return NvAttrib_PrimitiveId;
	case TGSI_SEMANTIC_LAYER:         return NvAttrib_RtArrayIdx;
	case TGSI_SEMANTIC_VIEWPORT_INDEX:return NvAttrib_ViewportIdx;
	case TGSI_SEMANTIC_PSIZE:         return NvAttrib_PointSize;
	case TGSI_SEMANTIC_POSITION:      return NvAttrib_Position;
	case TGSI_SEMANTIC_GENERIC:       return NvAttrib_Generic(si);
	case TGSI_SEMANTIC_FOG:           return NvAttrib_FogCoordinate;
	case TGSI_SEMANTIC_COLOR:         return NvAttrib_FrontColor(si);
	case TGSI_SEMANTIC_BCOLOR:        return NvAttrib_BackColor(si);
	case TGSI_SEMANTIC_CLIPDIST:      return NvAttrib_ClipDistance(4*si); // TGSI treats this field as vec4[2] instead of float[8]
	case TGSI_SEMANTIC_CLIPVERTEX:    return NvAttrib_Generic(31);
	case TGSI_SEMANTIC_PCOORD:        return NvAttrib_PointSpriteS;
	case TGSI_SEMANTIC_TESSCOORD:     return NvAttrib_TessEvalPointU;
	case TGSI_SEMANTIC_INSTANCEID:    return NvAttrib_InstanceId;
	case TGSI_SEMANTIC_VERTEXID:      return NvAttrib_VertexId;
	case TGSI_SEMANTIC_TEXCOORD:      return NvAttrib_FixedFncTex(si);
	default:
		assert(!"invalid TGSI input semantic");
		return ~0;
	}
}

static uint32_t
nvc0_shader_output_address(unsigned sn, unsigned si)
{
	switch (sn) {
	case TGSI_SEMANTIC_TESSOUTER:     return NvAttrib_TessOuter(si);
	case TGSI_SEMANTIC_TESSINNER:     return NvAttrib_TessInner(si);
	case TGSI_SEMANTIC_PATCH:         return NvAttrib_Patch(si);
	case TGSI_SEMANTIC_PRIMID:        return NvAttrib_PrimitiveId;
	case TGSI_SEMANTIC_LAYER:         return NvAttrib_RtArrayIdx;
	case TGSI_SEMANTIC_VIEWPORT_INDEX:return NvAttrib_ViewportIdx;
	case TGSI_SEMANTIC_PSIZE:         return NvAttrib_PointSize;
	case TGSI_SEMANTIC_POSITION:      return NvAttrib_Position;
	case TGSI_SEMANTIC_GENERIC:       return NvAttrib_Generic(si);
	case TGSI_SEMANTIC_FOG:           return NvAttrib_FogCoordinate;
	case TGSI_SEMANTIC_COLOR:         return NvAttrib_FrontColor(si);
	case TGSI_SEMANTIC_BCOLOR:        return NvAttrib_BackColor(si);
	case TGSI_SEMANTIC_CLIPDIST:      return NvAttrib_ClipDistance(4*si); // TGSI treats this field as vec4[2] instead of float[8]
	case TGSI_SEMANTIC_CLIPVERTEX:    return NvAttrib_Generic(31);
	case TGSI_SEMANTIC_TEXCOORD:      return NvAttrib_FixedFncTex(si);
	case TGSI_SEMANTIC_VIEWPORT_MASK: return NvAttrib_ViewportMask;
	case TGSI_SEMANTIC_EDGEFLAG:      return ~0;
	default:
		assert(!"invalid TGSI output semantic");
		return ~0;
	}
}

static int
nvc0_vp_assign_input_slots(struct nv50_ir_prog_info *info)
{
	unsigned i, c;

	int8_t const* in_locations = glsl_program_vertex_get_in_locations((glsl_program)info->driverPriv);

	for (i = 0; i < info->numInputs; ++i) {
		switch (info->in[i].sn) {
		case TGSI_SEMANTIC_INSTANCEID: /* for SM4 only, in TGSI they're SVs */
		case TGSI_SEMANTIC_VERTEXID:
			info->in[i].mask = 0x1;
			info->in[i].slot[0] =
				nvc0_shader_input_address(info->in[i].sn, 0) / 4;
			continue;
		default:
			break;
		}

		int8_t location = in_locations[info->in[i].si];
		if (location >= 0) {
			for (c = 0; c < 4; ++c)
				info->in[i].slot[c] = (NvAttrib_Generic(location) + c * 0x4) / 4;
		}
	}

	return 0;
}

static int
nvc0_sp_assign_input_slots(struct nv50_ir_prog_info *info)
{
	unsigned offset;
	unsigned i, c;

	for (i = 0; i < info->numInputs; ++i) {
		offset = nvc0_shader_input_address(info->in[i].sn, info->in[i].si);

		for (c = 0; c < 4; ++c)
			info->in[i].slot[c] = (offset + c * 0x4) / 4;
	}

	return 0;
}

static int
nvc0_fp_assign_output_slots(struct nv50_ir_prog_info *info)
{
	unsigned count = info->prop.fp.numColourResults * 4;
	unsigned i, c;

	/* Compute the relative position of each color output, since skipped MRT
		* positions will not have registers allocated to them.
		*/
	unsigned colors[8] = {0};
	for (i = 0; i < info->numOutputs; ++i)
		if (info->out[i].sn == TGSI_SEMANTIC_COLOR)
			colors[info->out[i].si] = 1;
	for (i = 0, c = 0; i < 8; i++)
		if (colors[i])
			colors[i] = c++;
	for (i = 0; i < info->numOutputs; ++i)
		if (info->out[i].sn == TGSI_SEMANTIC_COLOR)
			for (c = 0; c < 4; ++c)
				info->out[i].slot[c] = colors[info->out[i].si] * 4 + c;

	if (info->io.sampleMask < PIPE_MAX_SHADER_OUTPUTS)
		info->out[info->io.sampleMask].slot[0] = count++;
	else
	if (info->target >= 0xe0)
		count++; /* on Kepler, depth is always last colour reg + 2 */

	if (info->io.fragDepth < PIPE_MAX_SHADER_OUTPUTS)
		info->out[info->io.fragDepth].slot[2] = count;

	return 0;
}

static int
nvc0_sp_assign_output_slots(struct nv50_ir_prog_info *info)
{
	unsigned offset;
	unsigned i, c;

	for (i = 0; i < info->numOutputs; ++i) {
		offset = nvc0_shader_output_address(info->out[i].sn, info->out[i].si);

		for (c = 0; c < 4; ++c)
			info->out[i].slot[c] = (offset + c * 0x4) / 4;
	}

	return 0;
}

static int
nvc0_program_assign_varying_slots(struct nv50_ir_prog_info *info)
{
	int ret;

	if (info->type == PIPE_SHADER_VERTEX)
		ret = nvc0_vp_assign_input_slots(info);
	else
		ret = nvc0_sp_assign_input_slots(info);
	if (ret)
		return ret;

	if (info->type == PIPE_SHADER_FRAGMENT)
		ret = nvc0_fp_assign_output_slots(info);
	else
		ret = nvc0_sp_assign_output_slots(info);
	return ret;
}

DekoCompiler::DekoCompiler(pipeline_stage stage, int optLevel) :
	m_stage{stage}, m_glsl{}, m_tgsi{}, m_tgsiNumTokens{}, m_info{}, m_code{}, m_codeSize{},
	m_nvsh{}, m_dkph{}
{
	m_nvsh.version = 3;
	m_nvsh.sass_version = 3;
	m_nvsh.store_req_start = 0xff;
	m_nvsh.store_req_end = 0x00;

	uint16_t resbase;
	switch (stage)
	{
		default:
		case pipeline_stage_vertex:
			m_info.type = PIPE_SHADER_VERTEX;
			m_nvsh.sph_type = NvSphType_VTG;
			m_nvsh.shader_type = NvShaderType_Vertex;
			m_dkph.type = DkshProgramType_Vertex;
			resbase = 0x010;
			break;
		case pipeline_stage_tess_ctrl:
			m_info.type = PIPE_SHADER_TESS_CTRL;
			m_nvsh.sph_type = NvSphType_VTG;
			m_nvsh.shader_type = NvShaderType_TessellationInit;
			m_dkph.type = DkshProgramType_TessCtrl;
			resbase = 0x1b0;
			break;
		case pipeline_stage_tess_eval:
			m_info.type = PIPE_SHADER_TESS_EVAL;
			m_nvsh.sph_type = NvSphType_VTG;
			m_nvsh.shader_type = NvShaderType_Tessellation;
			m_dkph.type = DkshProgramType_TessEval;
			resbase = 0x350;
			break;
		case pipeline_stage_geometry:
			m_info.type = PIPE_SHADER_GEOMETRY;
			m_nvsh.sph_type = NvSphType_VTG;
			m_nvsh.shader_type = NvShaderType_Geometry;
			m_dkph.type = DkshProgramType_Geometry;
			resbase = 0x4f0;
			break;
		case pipeline_stage_fragment:
			m_info.type = PIPE_SHADER_FRAGMENT;
			m_nvsh.sph_type = NvSphType_PS;
			m_nvsh.shader_type = NvShaderType_Pixel;
			m_dkph.type = DkshProgramType_Fragment;
			resbase = 0x690;
			break;
		case pipeline_stage_compute:
			m_info.type = PIPE_SHADER_COMPUTE;
			m_dkph.type = DkshProgramType_Compute;
			resbase = 0x0a0;
			break;
	}
	m_info.target = 0x12b;
	m_info.bin.sourceRep = PIPE_SHADER_IR_TGSI;

	m_info.optLevel = optLevel;

	m_info.io.auxCBSlot      = 17;            // Driver constbuf c[0x0]. Note that codegen was modified to transform constbuf ids like such: final_id = (raw_id + 1) % 18
	m_info.io.drawInfoBase   = 0x000;         // This is used for gl_BaseVertex, gl_BaseInstance and gl_DrawID (in that order)
	m_info.io.bufInfoBase    = resbase+0x0a0; // This is used to load SSBO information (u64 iova / u32 size / u32 padding)
	m_info.io.texBindBase    = resbase+0x000; // Start of bound texture handles (32) + images (right after). 32-bit instead of 64-bit.
	m_info.io.fbtexBindBase  = 0x00c;         // This is used for implementing TGSI_OPCODE_FBFETCH, itself used for KHR/NV_blend_equation_advanced and EXT_shader_framebuffer_fetch.
	m_info.io.sampleInfoBase = 0x830;         // This is a LUT needed to implement gl_SamplePosition, it contains MSAA base sample positions.
	m_info.io.uboInfoBase    = 0x020;         // Similar to bufInfoBase, but for UBOs. Compute shaders need this because there aren't enough hardware constbufs.
	m_info.prop.cp.gridInfoBase = 0x000;      // Compute dimension parameters (gl_LocalGroupSizeARB and gl_NumWorkGroups)

	// The following fields are unused in our case, but are kept here for reference's sake:
	//m_info.io.genUserClip  = prog->vp.num_ucps;             // This is used for old-style clip plane handling (gl_ClipVertex).
	//m_info.io.msInfoCBSlot = 17;                            // This is used for msInfoBase (which is unused, see below)
	//m_info.io.ucpBase      = NVC0_CB_AUX_UCP_INFO;          // This is also for old-style clip plane handling.
	//m_info.io.msInfoBase   = NVC0_CB_AUX_MS_INFO;           // This points to a LUT used to calculate dx/dy from the sample id in NVC0LoweringPass::adjustCoordinatesMS. I replaced it with bitwise operations, so this is now unused.
	//m_info.io.suInfoBase   = NVC0_CB_AUX_SU_INFO(0);        // Surface information. On Maxwell, nouveau only uses it during NVC0LoweringPass::processSurfaceCoordsGM107 bound checking (which I disabled)
	//m_info.io.bindlessBase = NVC0_CB_AUX_BINDLESS_INFO(0);  // Like suInfoBase, but for bindless textures (pre-Kepler?).

	m_info.assignSlots = nvc0_program_assign_varying_slots;

	glsl_frontend_init();
}

DekoCompiler::~DekoCompiler()
{
	if (m_glsl)
		glsl_program_free(m_glsl);

	glsl_frontend_exit();
}

bool DekoCompiler::CompileGlsl(const char* glsl)
{
	m_glsl = glsl_program_create(glsl, m_stage);
	if (!m_glsl) return false;

	m_tgsi = glsl_program_get_tokens(m_glsl, m_tgsiNumTokens);
	m_info.bin.source = m_tgsi;
	m_info.bin.smemSize = glsl_program_compute_get_shared_size(m_glsl); // Total size of glsl shared variables. (translation process doesn't actually need this, but for the sake of consistency with nouveau, we keep this value here too)
	m_info.driverPriv = m_glsl;
	int ret = nv50_ir_generate_code(&m_info);
	if (ret < 0)
	{
		fprintf(stderr, "Error compiling program: %d\n", ret);
		return false;
	}

	if (m_info.io.fp64_rcprsq)
		fprintf(stderr, "warning: program uses 64-bit floating point reciprocal/square root, for which only a rough approximation with 20 bits of mantissa is supported by hardware\n");
	if (m_info.io.int_divmod)
		fprintf(stderr, "warning: program uses non-constant integer division/modulo, which is unsupported by hardware; floating point emulation with resulting loss of precision has been applied\n");

	m_data = glsl_program_get_constant_buffer(m_glsl, m_dataSize);
	RetrieveAndPadCode();
	GenerateHeaders();
	return true;
}

void DekoCompiler::RetrieveAndPadCode()
{
	uint32_t numInsns = m_info.bin.codeSize/8;
	uint64_t* insns = (uint64_t*)m_info.bin.code;
	uint32_t totalNumInsns = (numInsns + 8) &~ 7;

	bool emittedBRA = false;
	for (uint32_t i = numInsns; i < totalNumInsns; i ++)
	{
		uint64_t& schedInsn = insns[i &~ 3];
		uint32_t ipos = i & 3;
		if (ipos == 0)
		{
			schedInsn = 0;
			continue;
		}
		uint64_t insn = UINT64_C(0x50b0000000070f00); // NOP
		uint32_t sched = 0x7e0;
		if (!emittedBRA)
		{
			emittedBRA = true;
			insn = ipos==1 ? UINT64_C(0xe2400fffff07000f) : UINT64_C(0xe2400fffff87000f); // BRA $;
			sched = 0x7ff;
		}

		insns[i] = insn;
		schedInsn &= ~(((UINT64_C(1)<<21)-1) << (21*(ipos-1)));
		schedInsn |= uint64_t(sched) << (21*(ipos-1));
	}

	m_code = insns;
	m_codeSize = 8*totalNumInsns;
}

void DekoCompiler::GenerateHeaders()
{
	m_dkph.entrypoint = m_stage != pipeline_stage_compute ? s_shaderStartOffset : 0;
	m_dkph.num_gprs = m_info.bin.maxGPR + 1;
	if (m_dkph.num_gprs < 4) m_dkph.num_gprs = 4;

	if (m_dataSize)
	{
		m_dkph.constbuf1_off = Align256((m_stage != pipeline_stage_compute ? 0x80 : 0x00) + m_codeSize);
		m_dkph.constbuf1_sz  = m_dataSize;
	}

	unsigned local_pos_sz = (m_info.bin.tlsSpace + 0xF) &~ 0xF; // 16-byte aligned
	unsigned local_neg_sz = 0;
	unsigned crs_sz       = m_stage == pipeline_stage_compute ? 0x800 : 0; // 512-byte aligned; TODO: Proper logic

	m_dkph.per_warp_scratch_sz = (local_pos_sz + local_neg_sz) * 32 + crs_sz;

	if (m_stage == pipeline_stage_compute)
	{
		m_dkph.comp.block_dims[0]    = m_info.prop.cp.numThreads[0];
		m_dkph.comp.block_dims[1]    = m_info.prop.cp.numThreads[1];
		m_dkph.comp.block_dims[2]    = m_info.prop.cp.numThreads[2];
		m_dkph.comp.shared_mem_sz    = Align256(m_info.bin.smemSize);
		m_dkph.comp.local_pos_mem_sz = local_pos_sz;
		m_dkph.comp.local_neg_mem_sz = local_neg_sz;
		m_dkph.comp.crs_sz           = crs_sz;
		m_dkph.comp.num_barriers     = m_info.numBarriers;
	}
	else
	{
		m_nvsh.sh_local_mem_lo_sz  = local_pos_sz;
		m_nvsh.sh_local_mem_hi_sz  = local_neg_sz;
		m_nvsh.sh_local_mem_crs_sz = crs_sz;

		if (m_info.io.globalAccess & 2)
			m_nvsh.does_global_store = 1;
		if (m_info.io.globalAccess || m_info.bin.tlsSpace)
			m_nvsh.does_load_or_store = 1;
		if (m_info.io.fp64)
			m_nvsh.does_fp64 = 1;

		if (m_stage == pipeline_stage_fragment)
		{
			if (!m_info.prop.fp.separateFragData)
				m_nvsh.mrt_enable = 1;
			if (m_info.prop.fp.usesDiscard)
				m_nvsh.kills_pixels = 1;

			// Generate input map (Imap)
			//-----------------------------------------------------------------
			for (unsigned i = 0; i < m_info.numInputs; i ++)
			{
				auto& in = m_info.in[i];
				unsigned interp = NvPixelImap_Perspective;
				if (in.linear)
					interp = NvPixelImap_ScreenLinear;
				else if (in.flat)
					interp = NvPixelImap_Constant;
				for (unsigned j = 0; j < 4; j ++)
				{
					unsigned attr = 4*in.slot[j];
					if (in.mask & (1u<<j))
						m_nvsh.SetPsImap(attr, interp);
				}
			}

			if (m_info.prop.fp.readsFramebuffer)
				m_nvsh.SetImapSysval(NvSysval_RtArrayIdx); // mark layer as read

			// Reading sample locations implies using PositionX/Y
			if (m_info.prop.fp.readsFramebuffer || m_info.prop.fp.readsSampleLocations)
			{
				m_nvsh.SetImapSysval(NvSysval_PositionX);
				m_nvsh.SetImapSysval(NvSysval_PositionY);
			}

			// For simplicity and convenience's sake, let's assume gl_FragCoord.w will always be read.
			m_nvsh.SetImapSysval(NvSysval_PositionW);

			// Generate output map (Omap)
			//-----------------------------------------------------------------
			for (unsigned i = 0; i < m_info.numOutputs; i ++)
				if (m_info.out[i].sn == TGSI_SEMANTIC_COLOR)
					m_nvsh.SetPsOmapTarget(m_info.out[i].si, 0xf);

			// There are no "regular" attachments, but the shader still needs to be
			// executed. It seems like it wants to think that it has some color
			// outputs in order to actually run.
			if (m_info.prop.fp.numColourResults==0 && !m_info.prop.fp.writesDepth)
				m_nvsh.SetPsOmapTarget(0, 0xf);

			if (m_info.io.sampleMask < PIPE_MAX_SHADER_OUTPUTS)
				m_nvsh.ps.omap_sample_mask = 1;
			if (m_info.prop.fp.writesDepth)
				m_nvsh.ps.omap_depth = 1;

			// Miscellaneous
			//-----------------------------------------------------------------
			m_dkph.frag.early_fragment_tests  = m_info.prop.fp.earlyFragTests;
			m_dkph.frag.post_depth_coverage   = m_info.prop.fp.postDepthCoverage;
			m_dkph.frag.per_sample_invocation = m_info.prop.fp.persampleInvocation;
			m_dkph.frag.param_65b             = m_info.prop.fp.hasZcullTestMask ? m_info.prop.fp.zcullTestMask : (m_info.prop.fp.writesDepth ? 0x11 : 0x00);
			m_dkph.frag.param_489             = 0; // Fragment shader interlock layout (not supported by tgsi/nouveau anyway)
			if (m_info.prop.fp.writesDepth)
				m_dkph.frag.param_d8 = 0x087F6080;
			/*else if (TODO: figure out what triggers this)
				m_dkph.frag.param_d8 = 0x06164010;
			*/
			else if (m_info.prop.fp.numColourResults >= 3)
				m_dkph.frag.param_d8 = 0x20806080;
			else
				m_dkph.frag.param_d8 = 0x087F6080;
		}
		else
		{
			switch (m_stage)
			{
				default:
				case pipeline_stage_vertex:
					//m_nvsh.vsh_unk_flag = 1;
					break;

				case pipeline_stage_tess_ctrl:
				{
					unsigned num_patch_attribs = 6; // By default, allow shaders to write to TessLodLeft..TessInteriorV.
					if (m_info.numPatchConstants) // If there are per-patch attribs, expand the per-patch attrib section.
						num_patch_attribs = 8 + 4*m_info.numPatchConstants;
					m_nvsh.per_patch_attrib_cnt = num_patch_attribs;
					m_nvsh.thr_per_input_prim = m_info.prop.tp.outputPatchSize;
					// It is unknown whether the following "reserved" fields are actually used or not,
					// but both nouveau and official shaders emit it.
					m_nvsh._reserved3 = num_patch_attribs & 0xF;
					m_nvsh._reserved4 = num_patch_attribs >> 4;
					break;
				}

				case pipeline_stage_tess_eval:
				{
					switch (m_info.prop.tp.domain)
					{
						case PIPE_PRIM_LINES:
							m_dkph.tess_eval.param_c8 = 0; // NVC0_3D_TESS_MODE_PRIM_ISOLINES
							break;
						case PIPE_PRIM_TRIANGLES:
							m_dkph.tess_eval.param_c8 = 1; // NVC0_3D_TESS_MODE_PRIM_TRIANGLES
							break;
						default:
						case PIPE_PRIM_QUADS:
							m_dkph.tess_eval.param_c8 = 2; // NVC0_3D_TESS_MODE_PRIM_QUADS
							break;

					}
					switch (m_info.prop.tp.partitioning)
					{
						default:
						case PIPE_TESS_SPACING_EQUAL:
							m_dkph.tess_eval.param_c8 |= 0 << 4; // NVC0_3D_TESS_MODE_SPACING_EQUAL
							break;
						case PIPE_TESS_SPACING_FRACTIONAL_ODD:
							m_dkph.tess_eval.param_c8 |= 1 << 4; // NVC0_3D_TESS_MODE_SPACING_FRACTIONAL_ODD
							break;
						case PIPE_TESS_SPACING_FRACTIONAL_EVEN:
							m_dkph.tess_eval.param_c8 |= 2 << 4; // NVC0_3D_TESS_MODE_SPACING_FRACTIONAL_EVEN
							break;
					}
					if (m_info.prop.tp.outputPrim != PIPE_PRIM_POINTS)
					{
						if (m_info.prop.tp.domain == PIPE_PRIM_LINES)
							m_dkph.tess_eval.param_c8 |= 1 << 8; // NVC0_3D_TESS_MODE_CW
						else if (!m_info.prop.tp.winding) // counter-clockwise
							m_dkph.tess_eval.param_c8 |= 2 << 8; // NVC0_3D_TESS_MODE_CONNECTED
						else // clockwise
							m_dkph.tess_eval.param_c8 |= 3 << 8; // NVC0_3D_TESS_MODE_CW|NVC0_3D_TESS_MODE_CONNECTED
					}
					break;
				}

				case pipeline_stage_geometry:
				{
					unsigned num_threads = m_info.prop.gp.instanceCount;
					unsigned max_vertices = m_info.prop.gp.maxVertices;

					if (num_threads > 32) num_threads = 32; // Can't have more than the max num of threads in a warp, apparently...
					if (max_vertices < 1) max_vertices = 1;
					else if (max_vertices > 1024) max_vertices = 1024;

					m_nvsh.thr_per_input_prim = num_threads;
					m_nvsh.max_out_vtx_cnt = max_vertices;

					switch (m_info.prop.gp.outputPrim)
					{
						default:
						case PIPE_PRIM_POINTS:
							m_nvsh.output_topology = NvOutputTopology_PointList;
							m_nvsh.stream_out_mask = 0xf;
							break;
						case PIPE_PRIM_LINE_STRIP:
							m_nvsh.output_topology = NvOutputTopology_LineStrip;
							m_nvsh.stream_out_mask = 1;
							break;
						case PIPE_PRIM_TRIANGLE_STRIP:
							m_nvsh.output_topology = NvOutputTopology_TriangleStrip;
							m_nvsh.stream_out_mask = 1;
							break;
					}

					m_dkph.geom.flag_47c = m_info.io.layer_viewport_relative;
					break;
				}
			}

			// Generate input map (Imap)
			//-----------------------------------------------------------------
			for (unsigned i = 0; i < m_info.numInputs; i ++)
			{
				auto& in = m_info.in[i];
				if (in.patch)
					continue; // Per-patch attributes do not use Imap.
				for (unsigned j = 0; j < 4; j ++)
				{
					unsigned attr = 4*in.slot[j];
					if (in.mask & (1u<<j))
						m_nvsh.SetVtgImap(attr);
				}
			}

			for (unsigned i = 0; i < m_info.numSysVals; i ++)
			{
				switch (m_info.sv[i].sn)
				{
					case TGSI_SEMANTIC_PRIMID:
						m_nvsh.SetImapSysval(NvSysval_PrimitiveId);
						break;
					case TGSI_SEMANTIC_INSTANCEID:
						m_nvsh.SetImapSysval(NvSysval_InstanceId);
						break;
					case TGSI_SEMANTIC_VERTEXID:
						m_nvsh.SetImapSysval(NvSysval_VertexId);
						break;
					case TGSI_SEMANTIC_TESSCOORD:
						// TessEvalPointU/V are actually stored in the output ISBE, so treat them as such.
						m_nvsh.SetVtgOmapSysval(NvSysval_TessEvalPointU);
						m_nvsh.SetVtgOmapSysval(NvSysval_TessEvalPointV);
						m_nvsh.UpdateStoreReqRange(NvAttrib_TessEvalPointU);
						m_nvsh.UpdateStoreReqRange(NvAttrib_TessEvalPointV);
						break;
				}
			}

			// Generate output map (Omap)
			//-----------------------------------------------------------------
			for (unsigned i = 0; i < m_info.numOutputs; i ++)
			{
				auto& out = m_info.out[i];
				if (out.patch)
					continue; // Per-patch attributes do not use Omap.
				for (unsigned j = 0; j < 4; j ++)
				{
					unsigned attr = 4*out.slot[j];
					if (out.mask & (1u<<j))
					{
						m_nvsh.SetVtgOmap(attr);
						if (out.oread)
							m_nvsh.UpdateStoreReqRange(attr);
					}
				}
			}
		}
	}
}

void DekoCompiler::OutputDksh(const char* dkshFile)
{
	DkshHeader hdr = {};
	hdr.magic        = DKSH_MAGIC;
	hdr.header_sz    = sizeof(DkshHeader);
	hdr.control_sz   = Align256(sizeof(DkshHeader) + sizeof(DkshProgramHeader));
	hdr.code_sz      = Align256((m_stage != pipeline_stage_compute ? 0x80 : 0x00) + m_codeSize) + Align256(m_dataSize);
	hdr.programs_off = sizeof(DkshHeader);
	hdr.num_programs = 1;

	FILE* f = fopen(dkshFile, "wb");
	if (f)
	{
		fwrite(&hdr, 1, sizeof(hdr), f);
		fwrite(&m_dkph, 1, sizeof(m_dkph), f);
		FileAlign256(f);

		if (m_stage != pipeline_stage_compute)
		{
			static const char s_padding[s_shaderStartOffset] = "lol nvidia why did you make us waste space here";
			fwrite(s_padding, 1, sizeof(s_padding), f);
			fwrite(&m_nvsh, 1, sizeof(m_nvsh), f);
		}

		fwrite(m_code, 1, m_codeSize, f);
		FileAlign256(f);

		if (m_dataSize)
		{
			fwrite(m_data, 1, m_dataSize, f);
			FileAlign256(f);
		}

		fclose(f);
	}
}

void DekoCompiler::OutputRawCode(const char* rawFile)
{
	FILE* f = fopen(rawFile, "wb");
	if (f)
	{
		fwrite(m_code, 1, m_codeSize, f);
		fclose(f);
	}
}

void DekoCompiler::OutputTgsi(const char* tgsiFile)
{
	FILE* f = fopen(tgsiFile, "w");
	if (f)
	{
		tgsi_dump_to_file(m_tgsi, TGSI_DUMP_FLOAT_AS_HEX, f);
		fclose(f);
	}
}
