#pragma once
#include <stdint.h>

// Nvidia Attributes (a[] space)

enum
{
	NvAttrib_TessLodLeft    = 0x000,
	NvAttrib_TessLodRight   = 0x004,
	NvAttrib_TessLodBottom  = 0x008,
	NvAttrib_TessLodTop     = 0x00c,
	NvAttrib_TessInteriorU  = 0x010,
	NvAttrib_TessInteriorV  = 0x014,
	NvAttrib_PatchN         = 0x020, // vec4[4]
	NvAttrib_PrimitiveId    = 0x060,
	NvAttrib_RtArrayIdx     = 0x064,
	NvAttrib_ViewportIdx    = 0x068,
	NvAttrib_PointSize      = 0x06c,
	NvAttrib_Position       = 0x070,
	NvAttrib_GenericN       = 0x080, // vec4[32]
	NvAttrib_FrontDiffuse   = 0x280,
	NvAttrib_FrontSpecular  = 0x290,
	NvAttrib_BackDiffuse    = 0x2a0,
	NvAttrib_BackSpecular   = 0x2b0,
	NvAttrib_ClipDistanceN  = 0x2c0, // float[8]
	NvAttrib_PointSpriteS   = 0x2e0,
	NvAttrib_PointSpriteT   = 0x2e4,
	NvAttrib_FogCoordinate  = 0x2e8,
	NvAttrib_TessEvalPointU = 0x2f0,
	NvAttrib_TessEvalPointV = 0x2f4,
	NvAttrib_InstanceId     = 0x2f8,
	NvAttrib_VertexId       = 0x2fc,
	NvAttrib_FixedFncTexN   = 0x300, // vec4[10]
	NvAttrib_ViewportMask   = 0x3a0,
};

#define NvAttrib_TessOuter(_n)    (NvAttrib_TessLodLeft   +    4*(_n))
#define NvAttrib_TessInner(_n)    (NvAttrib_TessInteriorU +    4*(_n))
#define NvAttrib_Patch(_n)        (NvAttrib_PatchN        + 0x10*(_n))
#define NvAttrib_Generic(_n)      (NvAttrib_GenericN      + 0x10*(_n))
#define NvAttrib_FrontColor(_n)   (NvAttrib_FrontDiffuse  + 0x10*(_n))
#define NvAttrib_BackColor(_n)    (NvAttrib_BackDiffuse   + 0x10*(_n))
#define NvAttrib_ClipDistance(_n) (NvAttrib_ClipDistanceN +    4*(_n))
#define NvAttrib_FixedFncTex(_n)  (NvAttrib_FixedFncTexN  + 0x10*(_n))
