#pragma once
#include <stdint.h>
#include "nv_attributes.h"

// Nvidia Shader Program Header
// See https://nvidia.github.io/open-gpu-doc/Shader-Program-Header/Shader-Program-Header.html

enum
{
	NvSphType_VTG = 1,
	NvSphType_PS  = 2,
};

enum
{
	NvShaderType_Vertex           = 1,
	NvShaderType_TessellationInit = 2,
	NvShaderType_Tessellation     = 3,
	NvShaderType_Geometry         = 4,
	NvShaderType_Pixel            = 5,
};

enum
{
	NvOutputTopology_PointList     = 1,
	NvOutputTopology_LineStrip     = 6,
	NvOutputTopology_TriangleStrip = 7,
};

enum
{
	// SystemValuesA
	NvSysval_TessLodLeft      = 4,
	NvSysval_TessLodRight     = 5,
	NvSysval_TessLodBottom    = 6,
	NvSysval_TessLodTop       = 7,
	NvSysval_TessLodInteriorU = 8,
	NvSysval_TessLodInteriorV = 9,

	// SystemValuesB
	NvSysval_PrimitiveId      = 24,
	NvSysval_RtArrayIdx       = 25,
	NvSysval_ViewportIdx      = 26,
	NvSysval_PointSize        = 27,
	NvSysval_PositionX        = 28,
	NvSysval_PositionY        = 29,
	NvSysval_PositionZ        = 30,
	NvSysval_PositionW        = 31,

	// SystemValuesC
	NvSysval_ClipDistance0    = 32,
	NvSysval_ClipDistance1    = 33,
	NvSysval_ClipDistance2    = 34,
	NvSysval_ClipDistance3    = 35,
	NvSysval_ClipDistance4    = 36,
	NvSysval_ClipDistance5    = 37,
	NvSysval_ClipDistance6    = 38,
	NvSysval_ClipDistance7    = 39,
	NvSysval_PointSpriteS     = 40,
	NvSysval_PointSpriteT     = 41,
	NvSysval_FogCoordinate    = 42,
	NvSysval_TessEvalPointU   = 44,
	NvSysval_TessEvalPointV   = 45,
	NvSysval_InstanceId       = 46,
	NvSysval_VertexId         = 47,
};

#define NvSysval_TessLodInterior(_n) (NvSysval_TessLodInteriorU+(_n))
#define NvSysval_Position(_n)        (NvSysval_PositionX+(_n))
#define NvSysval_ClipDistance(_n)    (NvSysval_ClipDistance0+(_n))
#define NvSysval_PointSprite(_n)     (NvSysval_PointSpriteS+(_n))
#define NvSysval_TessEvalPoint(_n)   (NvSysval_TessEvalPointU+(_n))

enum
{
	NvPixelImap_Unused       = 0,
	NvPixelImap_Constant     = 1,
	NvPixelImap_Perspective  = 2,
	NvPixelImap_ScreenLinear = 3,
};

struct NvShaderHeader
{
	// CommonWord0
	uint32_t sph_type             : 5;
	uint32_t version              : 5;
	uint32_t shader_type          : 4;
	uint32_t mrt_enable           : 1;
	uint32_t kills_pixels         : 1;
	uint32_t does_global_store    : 1;
	uint32_t sass_version         : 4;
	uint32_t _reserved0_1         : 1;
	uint32_t _reserved0_2         : 1;
	uint32_t _reserved0_3         : 1;
	uint32_t is_fast_gs           : 1;
	uint32_t vsh_unk_flag         : 1;
	uint32_t does_load_or_store   : 1;
	uint32_t does_fp64            : 1;
	uint32_t stream_out_mask      : 4;

	// CommonWord1
	uint32_t sh_local_mem_lo_sz   : 24;
	uint32_t per_patch_attrib_cnt : 8;

	// CommonWord2
	uint32_t sh_local_mem_hi_sz   : 24;
	uint32_t thr_per_input_prim   : 8;

	// CommonWord3
	uint32_t sh_local_mem_crs_sz  : 24;
	uint32_t output_topology      : 4;
	uint32_t _reserved3           : 4;

	// CommonWord4
	uint32_t max_out_vtx_cnt      : 12;
	uint32_t store_req_start      : 8;
	uint32_t _reserved4           : 4;
	uint32_t store_req_end        : 8;

	// ImapSystemValuesA/ImapSystemValuesB
	uint32_t imap_sysvals_ab;

	union
	{
		struct
		{
			uint8_t  imap_generic_vector[16];
			uint16_t imap_color;
			uint16_t imap_sysvals_c;
			uint8_t  imap_fixed_fnc_tex[5];
			uint8_t  _reserved0;
			uint32_t omap_sysvals_ab;
			uint8_t  omap_generic_vector[16];
			uint16_t omap_color;
			uint16_t omap_sysvals_c;
			uint8_t  omap_fixed_fnc_tex[5];
			uint8_t  omap_extra;
		} __attribute__((packed)) vtg;

		struct
		{
			uint8_t  imap_generic_vector[32];
			uint16_t imap_color;
			uint16_t imap_sysvals_c;
			uint8_t  imap_fixed_fnc_tex[10];
			uint16_t _reserved0;
			uint32_t omap_target;
			uint32_t omap_sample_mask : 1;
			uint32_t omap_depth       : 1;
			uint32_t _reserved1       : 30;
		} ps;
	};

	static bool _Check(unsigned idx, unsigned first, unsigned last)
	{
		idx /= 4;
		first /= 4;
		last /= 4;
		return first <= idx && idx <= last;
	}

	static bool _CheckAndRemap(unsigned& idx, unsigned first, unsigned last, unsigned map)
	{
		bool rc = _Check(idx, first, last);
		if (rc) idx = (idx - first)/4 + map;
		return rc;
	}

	void UpdateStoreReqRange(unsigned idx)
	{
		idx /= 4;
		store_req_start = idx < store_req_start ? idx : store_req_start;
		store_req_end   = idx > store_req_end   ? idx : store_req_end;
	}

	void SetImapSysval(unsigned idx)
	{
		if (idx < 32)
			imap_sysvals_ab    |= UINT32_C(1) << idx;
		else if (sph_type == NvSphType_VTG)
			vtg.imap_sysvals_c |= UINT16_C(1) << (idx - 32);
		else if (sph_type == NvSphType_PS)
			ps.imap_sysvals_c  |= UINT16_C(1) << (idx - 32);
	}

	void SetVtgImap(unsigned idx)
	{
		if (_CheckAndRemap(idx, NvAttrib_TessLodLeft, NvAttrib_TessInteriorV, NvSysval_TessLodLeft))
			SetImapSysval(idx);
		else if (_CheckAndRemap(idx, NvAttrib_PrimitiveId, NvAttrib_Position+0xc, NvSysval_PrimitiveId))
			SetImapSysval(idx);
		else if (_CheckAndRemap(idx, NvAttrib_Generic(0), NvAttrib_Generic(31)+0xc, 0))
			vtg.imap_generic_vector[idx/8] |= 1u << (idx&7);
		else if (_CheckAndRemap(idx, NvAttrib_FrontDiffuse, NvAttrib_BackSpecular+0xc, 0))
			vtg.imap_color |= 1u << idx;
		else if (_CheckAndRemap(idx, NvAttrib_ClipDistance(0), NvAttrib_VertexId, NvSysval_ClipDistance0))
			SetImapSysval(idx);
		else if (_CheckAndRemap(idx, NvAttrib_FixedFncTex(0), NvAttrib_FixedFncTex(9)+0xc, 0))
			vtg.imap_fixed_fnc_tex[idx/8] |= 1u << (idx&7);
	}

	void SetVtgOmapSysval(unsigned idx)
	{
		if (idx < 32)
			vtg.omap_sysvals_ab |= UINT32_C(1) << idx;
		else
			vtg.omap_sysvals_c  |= UINT16_C(1) << (idx - 32);
	}

	void SetVtgOmap(unsigned idx)
	{
		if (_CheckAndRemap(idx, NvAttrib_TessLodLeft, NvAttrib_TessInteriorV, NvSysval_TessLodLeft))
			SetVtgOmapSysval(idx);
		else if (_CheckAndRemap(idx, NvAttrib_PrimitiveId, NvAttrib_Position+0xc, NvSysval_PrimitiveId))
			SetVtgOmapSysval(idx);
		else if (_CheckAndRemap(idx, NvAttrib_Generic(0), NvAttrib_Generic(31)+0xc, 0))
			vtg.omap_generic_vector[idx/8] |= 1u << (idx&7);
		else if (_CheckAndRemap(idx, NvAttrib_FrontDiffuse, NvAttrib_BackSpecular+0xc, 0))
			vtg.omap_color |= 1u << idx;
		else if (_CheckAndRemap(idx, NvAttrib_ClipDistance(0), NvAttrib_VertexId, NvSysval_ClipDistance0))
			SetVtgOmapSysval(idx);
		else if (_CheckAndRemap(idx, NvAttrib_FixedFncTex(0), NvAttrib_FixedFncTex(9)+0xc, 0))
			vtg.omap_fixed_fnc_tex[idx/8] |= 1u << (idx&7);
		else if (idx == NvAttrib_ViewportMask)
			vtg.omap_extra |= 1;
	}

	void SetPsImap(unsigned idx, unsigned interp = NvPixelImap_Constant)
	{
		if (_CheckAndRemap(idx, NvAttrib_TessLodLeft, NvAttrib_TessInteriorV, NvSysval_TessLodLeft))
			SetImapSysval(idx);
		else if (_CheckAndRemap(idx, NvAttrib_PrimitiveId, NvAttrib_Position+0xc, NvSysval_PrimitiveId))
			SetImapSysval(idx);
		else if (_CheckAndRemap(idx, NvAttrib_Generic(0), NvAttrib_Generic(31)+0xc, 0))
			ps.imap_generic_vector[idx/4] |= interp << (2*(idx&3));
		else if (_CheckAndRemap(idx, NvAttrib_FrontDiffuse, NvAttrib_FrontSpecular+0xc, 0))
			ps.imap_color |= interp << (2*idx);
		else if (_CheckAndRemap(idx, NvAttrib_ClipDistance(0), NvAttrib_VertexId, NvSysval_ClipDistance0))
			SetImapSysval(idx);
		else if (_CheckAndRemap(idx, NvAttrib_FixedFncTex(0), NvAttrib_FixedFncTex(9)+0xc, 0))
			ps.imap_fixed_fnc_tex[idx/4] |= interp << (2*(idx&3));
	}

	void SetPsOmapTarget(unsigned idx, unsigned mask)
	{
		ps.omap_target |= mask << (4*idx);
	}
};

static_assert(sizeof(NvShaderHeader)==80, "Wrong size for NvShaderHeader");
