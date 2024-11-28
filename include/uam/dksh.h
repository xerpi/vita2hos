#pragma once
#include <stdint.h>

// deko3d shader module file format
// File extension: .dksh
// The file is composed of two sections, both whose size is a multiple of 256 bytes:
// - Control section, containing shader information/metadata
// - Code section, containing actual shader code and compiler-generated constbufs
// Layout of the control section:
// - DkshHeader
// - DkshProgramHeader[]

#define DKSH_MAGIC UINT32_C(0x48534B44) // DKSH

struct DkshHeader
{
	uint32_t magic; // DKSH_MAGIC
	uint32_t header_sz; // sizeof(DkshHeader)
	uint32_t control_sz;
	uint32_t code_sz;
	uint32_t programs_off;
	uint32_t num_programs;
};

enum
{
	DkshProgramType_Vertex   = 0,
	DkshProgramType_Fragment = 1,
	DkshProgramType_Geometry = 2,
	DkshProgramType_TessCtrl = 3,
	DkshProgramType_TessEval = 4,
	DkshProgramType_Compute  = 5,
};

struct DkshProgramHeader
{
	uint32_t type;
	uint32_t entrypoint;
	uint32_t num_gprs;
	uint32_t constbuf1_off;
	uint32_t constbuf1_sz;
	uint32_t per_warp_scratch_sz;
	union
	{
		struct
		{
			uint32_t alt_entrypoint;
			uint32_t alt_num_gprs;
		} vert;
		struct
		{
			bool has_table_3d1;
			bool early_fragment_tests;
			bool post_depth_coverage;
			bool per_sample_invocation;
			uint32_t table_3d1[4];
			uint32_t param_d8;
			uint16_t param_65b;
			uint16_t param_489;
		} frag;
		struct
		{
			bool flag_47c;
			bool has_table_490;
			bool _padding[2];
			uint32_t table_490[8];
		} geom;
		struct
		{
			uint32_t param_c8;
		} tess_eval;
		struct
		{
			uint32_t block_dims[3];
			uint32_t shared_mem_sz;
			uint32_t local_pos_mem_sz;
			uint32_t local_neg_mem_sz;
			uint32_t crs_sz;
			uint32_t num_barriers;
		} comp;
	};
	uint32_t reserved;
};

static_assert(sizeof(DkshProgramHeader)==64, "Wrong size for DkshProgramHeader");
