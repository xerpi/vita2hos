/**************************************************************************
 * 
 * Copyright 2007 VMware, Inc.
 * Copyright (c) 2008-2010 VMware, Inc.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/


/**
 * Mesa / Gallium format conversion and format selection code.
 * \author Brian Paul
 */

#include "main/imports.h"
#include "main/context.h"
#include "main/enums.h"
#include "main/formats.h"
/*#include "main/glformats.h"
#include "main/texcompress.h"
#include "main/texgetimage.h"
#include "main/teximage.h"
#include "main/texstore.h"
#include "main/image.h"*/
#include "main/macros.h"
//#include "main/formatquery.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "util/u_format.h"
//#include "st_cb_texture.h"
//#include "st_context.h"
#include "st_format.h"
//#include "st_texture.h"


/**
 * Translate Mesa format to Gallium format.
 */
enum pipe_format
st_mesa_format_to_pipe_format(const struct st_context *st,
                              mesa_format mesaFormat)
{
   /* // fincs-edit
   struct pipe_screen *screen = st->pipe->screen;
   bool has_bgra_srgb = screen->is_format_supported(screen,
						    PIPE_FORMAT_B8G8R8A8_SRGB,
						    PIPE_TEXTURE_2D, 0, 0,
						    PIPE_BIND_SAMPLER_VIEW);
   */
   bool has_bgra_srgb = true;

   switch (mesaFormat) {
   case MESA_FORMAT_A8B8G8R8_UNORM:
      return PIPE_FORMAT_ABGR8888_UNORM;
   case MESA_FORMAT_R8G8B8A8_UNORM:
      return PIPE_FORMAT_RGBA8888_UNORM;
   case MESA_FORMAT_B8G8R8A8_UNORM:
      return PIPE_FORMAT_BGRA8888_UNORM;
   case MESA_FORMAT_A8R8G8B8_UNORM:
      return PIPE_FORMAT_ARGB8888_UNORM;
   case MESA_FORMAT_X8B8G8R8_UNORM:
      return PIPE_FORMAT_XBGR8888_UNORM;
   case MESA_FORMAT_R8G8B8X8_UNORM:
      return PIPE_FORMAT_RGBX8888_UNORM;
   case MESA_FORMAT_B8G8R8X8_UNORM:
      return PIPE_FORMAT_BGRX8888_UNORM;
   case MESA_FORMAT_X8R8G8B8_UNORM:
      return PIPE_FORMAT_XRGB8888_UNORM;
   case MESA_FORMAT_B5G5R5A1_UNORM:
      return PIPE_FORMAT_B5G5R5A1_UNORM;
   case MESA_FORMAT_A1B5G5R5_UNORM:
      return PIPE_FORMAT_A1B5G5R5_UNORM;
   case MESA_FORMAT_B4G4R4A4_UNORM:
      return PIPE_FORMAT_B4G4R4A4_UNORM;
   case MESA_FORMAT_A4B4G4R4_UNORM:
      return PIPE_FORMAT_A4B4G4R4_UNORM;
   case MESA_FORMAT_B5G6R5_UNORM:
      return PIPE_FORMAT_B5G6R5_UNORM;
   case MESA_FORMAT_B2G3R3_UNORM:
      return PIPE_FORMAT_B2G3R3_UNORM;
   case MESA_FORMAT_B10G10R10A2_UNORM:
      return PIPE_FORMAT_B10G10R10A2_UNORM;
   case MESA_FORMAT_R10G10B10A2_UNORM:
      return PIPE_FORMAT_R10G10B10A2_UNORM;
   case MESA_FORMAT_R10G10B10X2_UNORM:
      return PIPE_FORMAT_R10G10B10X2_UNORM;
   case MESA_FORMAT_L4A4_UNORM:
      return PIPE_FORMAT_L4A4_UNORM;
   case MESA_FORMAT_L8A8_UNORM:
      return PIPE_FORMAT_LA88_UNORM;
   case MESA_FORMAT_A8L8_UNORM:
      return PIPE_FORMAT_AL88_UNORM;
   case MESA_FORMAT_L16A16_UNORM:
      return PIPE_FORMAT_LA1616_UNORM;
   case MESA_FORMAT_A16L16_UNORM:
      return PIPE_FORMAT_AL1616_UNORM;
   case MESA_FORMAT_A_UNORM8:
      return PIPE_FORMAT_A8_UNORM;
   case MESA_FORMAT_A_UNORM16:
      return PIPE_FORMAT_A16_UNORM;
   case MESA_FORMAT_L_UNORM8:
      return PIPE_FORMAT_L8_UNORM;
   case MESA_FORMAT_L_UNORM16:
      return PIPE_FORMAT_L16_UNORM;
   case MESA_FORMAT_I_UNORM8:
      return PIPE_FORMAT_I8_UNORM;
   case MESA_FORMAT_I_UNORM16:
      return PIPE_FORMAT_I16_UNORM;
   case MESA_FORMAT_Z_UNORM16:
      return PIPE_FORMAT_Z16_UNORM;
   case MESA_FORMAT_Z_UNORM32:
      return PIPE_FORMAT_Z32_UNORM;
   case MESA_FORMAT_S8_UINT_Z24_UNORM:
      return PIPE_FORMAT_S8_UINT_Z24_UNORM;
   case MESA_FORMAT_Z24_UNORM_S8_UINT:
      return PIPE_FORMAT_Z24_UNORM_S8_UINT;
   case MESA_FORMAT_X8_UINT_Z24_UNORM:
      return PIPE_FORMAT_X8Z24_UNORM;
   case MESA_FORMAT_Z24_UNORM_X8_UINT:
      return PIPE_FORMAT_Z24X8_UNORM;
   case MESA_FORMAT_S_UINT8:
      return PIPE_FORMAT_S8_UINT;
   case MESA_FORMAT_Z_FLOAT32:
      return PIPE_FORMAT_Z32_FLOAT;
   case MESA_FORMAT_Z32_FLOAT_S8X24_UINT:
      return PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
   case MESA_FORMAT_YCBCR:
      return PIPE_FORMAT_UYVY;
   case MESA_FORMAT_YCBCR_REV:
      return PIPE_FORMAT_YUYV;
   case MESA_FORMAT_RGB_DXT1:
      return PIPE_FORMAT_DXT1_RGB;
   case MESA_FORMAT_RGBA_DXT1:
      return PIPE_FORMAT_DXT1_RGBA;
   case MESA_FORMAT_RGBA_DXT3:
      return PIPE_FORMAT_DXT3_RGBA;
   case MESA_FORMAT_RGBA_DXT5:
      return PIPE_FORMAT_DXT5_RGBA;
   case MESA_FORMAT_SRGB_DXT1:
      return PIPE_FORMAT_DXT1_SRGB;
   case MESA_FORMAT_SRGBA_DXT1:
      return PIPE_FORMAT_DXT1_SRGBA;
   case MESA_FORMAT_SRGBA_DXT3:
      return PIPE_FORMAT_DXT3_SRGBA;
   case MESA_FORMAT_SRGBA_DXT5:
      return PIPE_FORMAT_DXT5_SRGBA;
   case MESA_FORMAT_L8A8_SRGB:
      return PIPE_FORMAT_LA88_SRGB;
   case MESA_FORMAT_A8L8_SRGB:
      return PIPE_FORMAT_AL88_SRGB;
   case MESA_FORMAT_L_SRGB8:
      return PIPE_FORMAT_L8_SRGB;
   case MESA_FORMAT_R_SRGB8:
      return PIPE_FORMAT_R8_SRGB;
   case MESA_FORMAT_BGR_SRGB8:
      return PIPE_FORMAT_R8G8B8_SRGB;
   case MESA_FORMAT_A8B8G8R8_SRGB:
      return PIPE_FORMAT_ABGR8888_SRGB;
   case MESA_FORMAT_R8G8B8A8_SRGB:
      return PIPE_FORMAT_RGBA8888_SRGB;
   case MESA_FORMAT_B8G8R8A8_SRGB:
      return PIPE_FORMAT_BGRA8888_SRGB;
   case MESA_FORMAT_A8R8G8B8_SRGB:
      return PIPE_FORMAT_ARGB8888_SRGB;
   case MESA_FORMAT_RGBA_FLOAT32:
      return PIPE_FORMAT_R32G32B32A32_FLOAT;
   case MESA_FORMAT_RGBA_FLOAT16:
      return PIPE_FORMAT_R16G16B16A16_FLOAT;
   case MESA_FORMAT_RGB_FLOAT32:
      return PIPE_FORMAT_R32G32B32_FLOAT;
   case MESA_FORMAT_RGB_FLOAT16:
      return PIPE_FORMAT_R16G16B16_FLOAT;
   case MESA_FORMAT_LA_FLOAT32:
      return PIPE_FORMAT_L32A32_FLOAT;
   case MESA_FORMAT_LA_FLOAT16:
      return PIPE_FORMAT_L16A16_FLOAT;
   case MESA_FORMAT_L_FLOAT32:
      return PIPE_FORMAT_L32_FLOAT;
   case MESA_FORMAT_L_FLOAT16:
      return PIPE_FORMAT_L16_FLOAT;
   case MESA_FORMAT_A_FLOAT32:
      return PIPE_FORMAT_A32_FLOAT;
   case MESA_FORMAT_A_FLOAT16:
      return PIPE_FORMAT_A16_FLOAT;
   case MESA_FORMAT_I_FLOAT32:
      return PIPE_FORMAT_I32_FLOAT;
   case MESA_FORMAT_I_FLOAT16:
      return PIPE_FORMAT_I16_FLOAT;
   case MESA_FORMAT_R_FLOAT32:
      return PIPE_FORMAT_R32_FLOAT;
   case MESA_FORMAT_R_FLOAT16:
      return PIPE_FORMAT_R16_FLOAT;
   case MESA_FORMAT_RG_FLOAT32:
      return PIPE_FORMAT_R32G32_FLOAT;
   case MESA_FORMAT_RG_FLOAT16:
      return PIPE_FORMAT_R16G16_FLOAT;

   case MESA_FORMAT_R_UNORM8:
      return PIPE_FORMAT_R8_UNORM;
   case MESA_FORMAT_R_UNORM16:
      return PIPE_FORMAT_R16_UNORM;
   case MESA_FORMAT_R8G8_UNORM:
      return PIPE_FORMAT_RG88_UNORM;
   case MESA_FORMAT_G8R8_UNORM:
      return PIPE_FORMAT_GR88_UNORM;
   case MESA_FORMAT_R16G16_UNORM:
      return PIPE_FORMAT_RG1616_UNORM;
   case MESA_FORMAT_G16R16_UNORM:
      return PIPE_FORMAT_GR1616_UNORM;
   case MESA_FORMAT_RGBA_UNORM16:
      return PIPE_FORMAT_R16G16B16A16_UNORM;

   /* signed int formats */
   case MESA_FORMAT_A_UINT8:
      return PIPE_FORMAT_A8_UINT;
   case MESA_FORMAT_A_UINT16:
      return PIPE_FORMAT_A16_UINT;
   case MESA_FORMAT_A_UINT32:
      return PIPE_FORMAT_A32_UINT;

   case MESA_FORMAT_A_SINT8:
      return PIPE_FORMAT_A8_SINT;
   case MESA_FORMAT_A_SINT16:
      return PIPE_FORMAT_A16_SINT;
   case MESA_FORMAT_A_SINT32:
      return PIPE_FORMAT_A32_SINT;

   case MESA_FORMAT_I_UINT8:
      return PIPE_FORMAT_I8_UINT;
   case MESA_FORMAT_I_UINT16:
      return PIPE_FORMAT_I16_UINT;
   case MESA_FORMAT_I_UINT32:
      return PIPE_FORMAT_I32_UINT;

   case MESA_FORMAT_I_SINT8:
      return PIPE_FORMAT_I8_SINT;
   case MESA_FORMAT_I_SINT16:
      return PIPE_FORMAT_I16_SINT;
   case MESA_FORMAT_I_SINT32:
      return PIPE_FORMAT_I32_SINT;

   case MESA_FORMAT_L_UINT8:
      return PIPE_FORMAT_L8_UINT;
   case MESA_FORMAT_L_UINT16:
      return PIPE_FORMAT_L16_UINT;
   case MESA_FORMAT_L_UINT32:
      return PIPE_FORMAT_L32_UINT;

   case MESA_FORMAT_L_SINT8:
      return PIPE_FORMAT_L8_SINT;
   case MESA_FORMAT_L_SINT16:
      return PIPE_FORMAT_L16_SINT;
   case MESA_FORMAT_L_SINT32:
      return PIPE_FORMAT_L32_SINT;

   case MESA_FORMAT_LA_UINT8:
      return PIPE_FORMAT_L8A8_UINT;
   case MESA_FORMAT_LA_UINT16:
      return PIPE_FORMAT_L16A16_UINT;
   case MESA_FORMAT_LA_UINT32:
      return PIPE_FORMAT_L32A32_UINT;

   case MESA_FORMAT_LA_SINT8:
      return PIPE_FORMAT_L8A8_SINT;
   case MESA_FORMAT_LA_SINT16:
      return PIPE_FORMAT_L16A16_SINT;
   case MESA_FORMAT_LA_SINT32:
      return PIPE_FORMAT_L32A32_SINT;

   case MESA_FORMAT_R_SINT8:
      return PIPE_FORMAT_R8_SINT;
   case MESA_FORMAT_RG_SINT8:
      return PIPE_FORMAT_R8G8_SINT;
   case MESA_FORMAT_RGB_SINT8:
      return PIPE_FORMAT_R8G8B8_SINT;
   case MESA_FORMAT_RGBA_SINT8:
      return PIPE_FORMAT_R8G8B8A8_SINT;
   case MESA_FORMAT_R_SINT16:
      return PIPE_FORMAT_R16_SINT;
   case MESA_FORMAT_RG_SINT16:
      return PIPE_FORMAT_R16G16_SINT;
   case MESA_FORMAT_RGB_SINT16:
      return PIPE_FORMAT_R16G16B16_SINT;
   case MESA_FORMAT_RGBA_SINT16:
      return PIPE_FORMAT_R16G16B16A16_SINT;
   case MESA_FORMAT_R_SINT32:
      return PIPE_FORMAT_R32_SINT;
   case MESA_FORMAT_RG_SINT32:
      return PIPE_FORMAT_R32G32_SINT;
   case MESA_FORMAT_RGB_SINT32:
      return PIPE_FORMAT_R32G32B32_SINT;
   case MESA_FORMAT_RGBA_SINT32:
      return PIPE_FORMAT_R32G32B32A32_SINT;

   /* unsigned int formats */
   case MESA_FORMAT_R_UINT8:
      return PIPE_FORMAT_R8_UINT;
   case MESA_FORMAT_RG_UINT8:
      return PIPE_FORMAT_R8G8_UINT;
   case MESA_FORMAT_RGB_UINT8:
      return PIPE_FORMAT_R8G8B8_UINT;
   case MESA_FORMAT_RGBA_UINT8:
      return PIPE_FORMAT_R8G8B8A8_UINT;
   case MESA_FORMAT_R_UINT16:
      return PIPE_FORMAT_R16_UINT;
   case MESA_FORMAT_RG_UINT16:
      return PIPE_FORMAT_R16G16_UINT;
   case MESA_FORMAT_RGB_UINT16:
      return PIPE_FORMAT_R16G16B16_UINT;
   case MESA_FORMAT_RGBA_UINT16:
      return PIPE_FORMAT_R16G16B16A16_UINT;
   case MESA_FORMAT_R_UINT32:
      return PIPE_FORMAT_R32_UINT;
   case MESA_FORMAT_RG_UINT32:
      return PIPE_FORMAT_R32G32_UINT;
   case MESA_FORMAT_RGB_UINT32:
      return PIPE_FORMAT_R32G32B32_UINT;
   case MESA_FORMAT_RGBA_UINT32:
      return PIPE_FORMAT_R32G32B32A32_UINT;

   case MESA_FORMAT_R_RGTC1_UNORM:
      return PIPE_FORMAT_RGTC1_UNORM;
   case MESA_FORMAT_R_RGTC1_SNORM:
      return PIPE_FORMAT_RGTC1_SNORM;
   case MESA_FORMAT_RG_RGTC2_UNORM:
      return PIPE_FORMAT_RGTC2_UNORM;
   case MESA_FORMAT_RG_RGTC2_SNORM:
      return PIPE_FORMAT_RGTC2_SNORM;

   case MESA_FORMAT_L_LATC1_UNORM:
      return PIPE_FORMAT_LATC1_UNORM;
   case MESA_FORMAT_L_LATC1_SNORM:
      return PIPE_FORMAT_LATC1_SNORM;
   case MESA_FORMAT_LA_LATC2_UNORM:
      return PIPE_FORMAT_LATC2_UNORM;
   case MESA_FORMAT_LA_LATC2_SNORM:
      return PIPE_FORMAT_LATC2_SNORM;

   /* The destination RGBA format mustn't be changed, because it's also
    * a destination format of the unpack/decompression function. */
   case MESA_FORMAT_ETC1_RGB8:
      return true /*st->has_etc1*/ ? PIPE_FORMAT_ETC1_RGB8 : PIPE_FORMAT_R8G8B8A8_UNORM; // fincs-edit

   case MESA_FORMAT_BPTC_RGBA_UNORM:
      return PIPE_FORMAT_BPTC_RGBA_UNORM;
   case MESA_FORMAT_BPTC_SRGB_ALPHA_UNORM:
      return PIPE_FORMAT_BPTC_SRGBA;
   case MESA_FORMAT_BPTC_RGB_SIGNED_FLOAT:
      return PIPE_FORMAT_BPTC_RGB_FLOAT;
   case MESA_FORMAT_BPTC_RGB_UNSIGNED_FLOAT:
      return PIPE_FORMAT_BPTC_RGB_UFLOAT;

   /* signed normalized formats */
   case MESA_FORMAT_R_SNORM8:
      return PIPE_FORMAT_R8_SNORM;
   case MESA_FORMAT_R8G8_SNORM:
      return PIPE_FORMAT_RG88_SNORM;
   case MESA_FORMAT_G8R8_SNORM:
      return PIPE_FORMAT_GR88_SNORM;
   case MESA_FORMAT_R8G8B8A8_SNORM:
      return PIPE_FORMAT_RGBA8888_SNORM;
   case MESA_FORMAT_A8B8G8R8_SNORM:
      return PIPE_FORMAT_ABGR8888_SNORM;

   case MESA_FORMAT_A_SNORM8:
      return PIPE_FORMAT_A8_SNORM;
   case MESA_FORMAT_L_SNORM8:
      return PIPE_FORMAT_L8_SNORM;
   case MESA_FORMAT_L8A8_SNORM:
      return PIPE_FORMAT_LA88_SNORM;
   case MESA_FORMAT_A8L8_SNORM:
      return PIPE_FORMAT_AL88_SNORM;
   case MESA_FORMAT_I_SNORM8:
      return PIPE_FORMAT_I8_SNORM;

   case MESA_FORMAT_R_SNORM16:
      return PIPE_FORMAT_R16_SNORM;
   case MESA_FORMAT_R16G16_SNORM:
      return PIPE_FORMAT_RG1616_SNORM;
   case MESA_FORMAT_G16R16_SNORM:
      return PIPE_FORMAT_GR1616_SNORM;
   case MESA_FORMAT_RGBA_SNORM16:
      return PIPE_FORMAT_R16G16B16A16_SNORM;

   case MESA_FORMAT_A_SNORM16:
      return PIPE_FORMAT_A16_SNORM;
   case MESA_FORMAT_L_SNORM16:
      return PIPE_FORMAT_L16_SNORM;
   case MESA_FORMAT_LA_SNORM16:
      return PIPE_FORMAT_L16A16_SNORM;
   case MESA_FORMAT_I_SNORM16:
      return PIPE_FORMAT_I16_SNORM;

   case MESA_FORMAT_R9G9B9E5_FLOAT:
      return PIPE_FORMAT_R9G9B9E5_FLOAT;
   case MESA_FORMAT_R11G11B10_FLOAT:
      return PIPE_FORMAT_R11G11B10_FLOAT;
   case MESA_FORMAT_B10G10R10A2_UINT:
      return PIPE_FORMAT_B10G10R10A2_UINT;
   case MESA_FORMAT_R10G10B10A2_UINT:
      return PIPE_FORMAT_R10G10B10A2_UINT;

   case MESA_FORMAT_B4G4R4X4_UNORM:
      return PIPE_FORMAT_B4G4R4X4_UNORM;
   case MESA_FORMAT_B5G5R5X1_UNORM:
      return PIPE_FORMAT_B5G5R5X1_UNORM;
   case MESA_FORMAT_X1B5G5R5_UNORM:
      return PIPE_FORMAT_X1B5G5R5_UNORM;
   case MESA_FORMAT_R8G8B8X8_SNORM:
      return PIPE_FORMAT_RGBX8888_SNORM;
   case MESA_FORMAT_X8B8G8R8_SNORM:
      return PIPE_FORMAT_XBGR8888_SNORM;
   case MESA_FORMAT_R8G8B8X8_SRGB:
      return PIPE_FORMAT_RGBX8888_SRGB;
   case MESA_FORMAT_X8B8G8R8_SRGB:
      return PIPE_FORMAT_XBGR8888_SRGB;
   case MESA_FORMAT_RGBX_UINT8:
      return PIPE_FORMAT_R8G8B8X8_UINT;
   case MESA_FORMAT_RGBX_SINT8:
      return PIPE_FORMAT_R8G8B8X8_SINT;
   case MESA_FORMAT_B10G10R10X2_UNORM:
      return PIPE_FORMAT_B10G10R10X2_UNORM;
   case MESA_FORMAT_RGBX_UNORM16:
      return PIPE_FORMAT_R16G16B16X16_UNORM;
   case MESA_FORMAT_RGBX_SNORM16:
      return PIPE_FORMAT_R16G16B16X16_SNORM;
   case MESA_FORMAT_RGBX_FLOAT16:
      return PIPE_FORMAT_R16G16B16X16_FLOAT;
   case MESA_FORMAT_RGBX_UINT16:
      return PIPE_FORMAT_R16G16B16X16_UINT;
   case MESA_FORMAT_RGBX_SINT16:
      return PIPE_FORMAT_R16G16B16X16_SINT;
   case MESA_FORMAT_RGBX_FLOAT32:
      return PIPE_FORMAT_R32G32B32X32_FLOAT;
   case MESA_FORMAT_RGBX_UINT32:
      return PIPE_FORMAT_R32G32B32X32_UINT;
   case MESA_FORMAT_RGBX_SINT32:
      return PIPE_FORMAT_R32G32B32X32_SINT;

   case MESA_FORMAT_B8G8R8X8_SRGB:
      return PIPE_FORMAT_BGRX8888_SRGB;
   case MESA_FORMAT_X8R8G8B8_SRGB:
      return PIPE_FORMAT_XRGB8888_SRGB;

   /* ETC2 formats are emulated as uncompressed ones.
    * The destination formats mustn't be changed, because they are also
    * destination formats of the unpack/decompression function. */
   case MESA_FORMAT_ETC2_RGB8:
      return true /*st->has_etc2*/ ? PIPE_FORMAT_ETC2_RGB8 : PIPE_FORMAT_R8G8B8A8_UNORM; // fincs-edit
   case MESA_FORMAT_ETC2_SRGB8:
      return true /*st->has_etc2*/ ? PIPE_FORMAT_ETC2_SRGB8 : // fincs-edit
	 has_bgra_srgb ? PIPE_FORMAT_B8G8R8A8_SRGB : PIPE_FORMAT_R8G8B8A8_SRGB;
   case MESA_FORMAT_ETC2_RGBA8_EAC:
      return true /*st->has_etc2*/ ? PIPE_FORMAT_ETC2_RGBA8 : PIPE_FORMAT_R8G8B8A8_UNORM; // fincs-edit
   case MESA_FORMAT_ETC2_SRGB8_ALPHA8_EAC:
      return true /*st->has_etc2*/ ? PIPE_FORMAT_ETC2_SRGBA8 : // fincs-edit
	 has_bgra_srgb ? PIPE_FORMAT_B8G8R8A8_SRGB : PIPE_FORMAT_R8G8B8A8_SRGB;
   case MESA_FORMAT_ETC2_R11_EAC:
      return true /*st->has_etc2*/ ? PIPE_FORMAT_ETC2_R11_UNORM : PIPE_FORMAT_R16_UNORM; // fincs-edit
   case MESA_FORMAT_ETC2_RG11_EAC:
      return true /*st->has_etc2*/ ? PIPE_FORMAT_ETC2_RG11_UNORM : PIPE_FORMAT_R16G16_UNORM; // fincs-edit
   case MESA_FORMAT_ETC2_SIGNED_R11_EAC:
      return true /*st->has_etc2*/ ? PIPE_FORMAT_ETC2_R11_SNORM : PIPE_FORMAT_R16_SNORM; // fincs-edit
   case MESA_FORMAT_ETC2_SIGNED_RG11_EAC:
      return true /*st->has_etc2*/ ? PIPE_FORMAT_ETC2_RG11_SNORM : PIPE_FORMAT_R16G16_SNORM; // fincs-edit
   case MESA_FORMAT_ETC2_RGB8_PUNCHTHROUGH_ALPHA1:
      return true /*st->has_etc2*/ ? PIPE_FORMAT_ETC2_RGB8A1 : PIPE_FORMAT_R8G8B8A8_UNORM; // fincs-edit
   case MESA_FORMAT_ETC2_SRGB8_PUNCHTHROUGH_ALPHA1:
      return true /*st->has_etc2*/ ? PIPE_FORMAT_ETC2_SRGB8A1 : // fincs-edit
	 has_bgra_srgb ? PIPE_FORMAT_B8G8R8A8_SRGB : PIPE_FORMAT_R8G8B8A8_SRGB;

   case MESA_FORMAT_RGBA_ASTC_4x4:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_UNORM;
      return PIPE_FORMAT_ASTC_4x4;
   case MESA_FORMAT_RGBA_ASTC_5x4:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_UNORM;
      return PIPE_FORMAT_ASTC_5x4;
   case MESA_FORMAT_RGBA_ASTC_5x5:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_UNORM;
      return PIPE_FORMAT_ASTC_5x5;
   case MESA_FORMAT_RGBA_ASTC_6x5:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_UNORM;
      return PIPE_FORMAT_ASTC_6x5;
   case MESA_FORMAT_RGBA_ASTC_6x6:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_UNORM;
      return PIPE_FORMAT_ASTC_6x6;
   case MESA_FORMAT_RGBA_ASTC_8x5:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_UNORM;
      return PIPE_FORMAT_ASTC_8x5;
   case MESA_FORMAT_RGBA_ASTC_8x6:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_UNORM;
      return PIPE_FORMAT_ASTC_8x6;
   case MESA_FORMAT_RGBA_ASTC_8x8:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_UNORM;
      return PIPE_FORMAT_ASTC_8x8;
   case MESA_FORMAT_RGBA_ASTC_10x5:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_UNORM;
      return PIPE_FORMAT_ASTC_10x5;
   case MESA_FORMAT_RGBA_ASTC_10x6:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_UNORM;
      return PIPE_FORMAT_ASTC_10x6;
   case MESA_FORMAT_RGBA_ASTC_10x8:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_UNORM;
      return PIPE_FORMAT_ASTC_10x8;
   case MESA_FORMAT_RGBA_ASTC_10x10:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_UNORM;
      return PIPE_FORMAT_ASTC_10x10;
   case MESA_FORMAT_RGBA_ASTC_12x10:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_UNORM;
      return PIPE_FORMAT_ASTC_12x10;
   case MESA_FORMAT_RGBA_ASTC_12x12:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_UNORM;
      return PIPE_FORMAT_ASTC_12x12;

   case MESA_FORMAT_SRGB8_ALPHA8_ASTC_4x4:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_SRGB;
      return PIPE_FORMAT_ASTC_4x4_SRGB;
   case MESA_FORMAT_SRGB8_ALPHA8_ASTC_5x4:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_SRGB;
      return PIPE_FORMAT_ASTC_5x4_SRGB;
   case MESA_FORMAT_SRGB8_ALPHA8_ASTC_5x5:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_SRGB;
      return PIPE_FORMAT_ASTC_5x5_SRGB;
   case MESA_FORMAT_SRGB8_ALPHA8_ASTC_6x5:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_SRGB;
      return PIPE_FORMAT_ASTC_6x5_SRGB;
   case MESA_FORMAT_SRGB8_ALPHA8_ASTC_6x6:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_SRGB;
      return PIPE_FORMAT_ASTC_6x6_SRGB;
   case MESA_FORMAT_SRGB8_ALPHA8_ASTC_8x5:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_SRGB;
      return PIPE_FORMAT_ASTC_8x5_SRGB;
   case MESA_FORMAT_SRGB8_ALPHA8_ASTC_8x6:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_SRGB;
      return PIPE_FORMAT_ASTC_8x6_SRGB;
   case MESA_FORMAT_SRGB8_ALPHA8_ASTC_8x8:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_SRGB;
      return PIPE_FORMAT_ASTC_8x8_SRGB;
   case MESA_FORMAT_SRGB8_ALPHA8_ASTC_10x5:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_SRGB;
      return PIPE_FORMAT_ASTC_10x5_SRGB;
   case MESA_FORMAT_SRGB8_ALPHA8_ASTC_10x6:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_SRGB;
      return PIPE_FORMAT_ASTC_10x6_SRGB;
   case MESA_FORMAT_SRGB8_ALPHA8_ASTC_10x8:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_SRGB;
      return PIPE_FORMAT_ASTC_10x8_SRGB;
   case MESA_FORMAT_SRGB8_ALPHA8_ASTC_10x10:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_SRGB;
      return PIPE_FORMAT_ASTC_10x10_SRGB;
   case MESA_FORMAT_SRGB8_ALPHA8_ASTC_12x10:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_SRGB;
      return PIPE_FORMAT_ASTC_12x10_SRGB;
   case MESA_FORMAT_SRGB8_ALPHA8_ASTC_12x12:
      if (false /*!st->has_astc_2d_ldr*/) // fincs-edit
         return PIPE_FORMAT_R8G8B8A8_SRGB;
      return PIPE_FORMAT_ASTC_12x12_SRGB;

   default:
      return PIPE_FORMAT_NONE;
   }
}


/**
 * Translate Gallium format to Mesa format.
 */
mesa_format
st_pipe_format_to_mesa_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_ABGR8888_UNORM:
      return MESA_FORMAT_A8B8G8R8_UNORM;
   case PIPE_FORMAT_RGBA8888_UNORM:
      return MESA_FORMAT_R8G8B8A8_UNORM;
   case PIPE_FORMAT_BGRA8888_UNORM:
      return MESA_FORMAT_B8G8R8A8_UNORM;
   case PIPE_FORMAT_ARGB8888_UNORM:
      return MESA_FORMAT_A8R8G8B8_UNORM;
   case PIPE_FORMAT_XBGR8888_UNORM:
      return MESA_FORMAT_X8B8G8R8_UNORM;
   case PIPE_FORMAT_RGBX8888_UNORM:
      return MESA_FORMAT_R8G8B8X8_UNORM;
   case PIPE_FORMAT_BGRX8888_UNORM:
      return MESA_FORMAT_B8G8R8X8_UNORM;
   case PIPE_FORMAT_XRGB8888_UNORM:
      return MESA_FORMAT_X8R8G8B8_UNORM;
   case PIPE_FORMAT_B5G5R5A1_UNORM:
      return MESA_FORMAT_B5G5R5A1_UNORM;
   case PIPE_FORMAT_A1B5G5R5_UNORM:
      return MESA_FORMAT_A1B5G5R5_UNORM;
   case PIPE_FORMAT_B4G4R4A4_UNORM:
      return MESA_FORMAT_B4G4R4A4_UNORM;
   case PIPE_FORMAT_A4B4G4R4_UNORM:
      return MESA_FORMAT_A4B4G4R4_UNORM;
   case PIPE_FORMAT_B5G6R5_UNORM:
      return MESA_FORMAT_B5G6R5_UNORM;
   case PIPE_FORMAT_B2G3R3_UNORM:
      return MESA_FORMAT_B2G3R3_UNORM;
   case PIPE_FORMAT_B10G10R10A2_UNORM:
      return MESA_FORMAT_B10G10R10A2_UNORM;
   case PIPE_FORMAT_R10G10B10A2_UNORM:
      return MESA_FORMAT_R10G10B10A2_UNORM;
   case PIPE_FORMAT_R10G10B10X2_UNORM:
      return MESA_FORMAT_R10G10B10X2_UNORM;
   case PIPE_FORMAT_L4A4_UNORM:
      return MESA_FORMAT_L4A4_UNORM;
   case PIPE_FORMAT_LA88_UNORM:
      return MESA_FORMAT_L8A8_UNORM;
   case PIPE_FORMAT_AL88_UNORM:
      return MESA_FORMAT_A8L8_UNORM;
   case PIPE_FORMAT_LA1616_UNORM:
      return MESA_FORMAT_L16A16_UNORM;
   case PIPE_FORMAT_AL1616_UNORM:
      return MESA_FORMAT_A16L16_UNORM;
   case PIPE_FORMAT_A8_UNORM:
      return MESA_FORMAT_A_UNORM8;
   case PIPE_FORMAT_A16_UNORM:
      return MESA_FORMAT_A_UNORM16;
   case PIPE_FORMAT_L8_UNORM:
      return MESA_FORMAT_L_UNORM8;
   case PIPE_FORMAT_L16_UNORM:
      return MESA_FORMAT_L_UNORM16;
   case PIPE_FORMAT_I8_UNORM:
      return MESA_FORMAT_I_UNORM8;
   case PIPE_FORMAT_I16_UNORM:
      return MESA_FORMAT_I_UNORM16;
   case PIPE_FORMAT_S8_UINT:
      return MESA_FORMAT_S_UINT8;

   case PIPE_FORMAT_R16G16B16A16_UNORM:
      return MESA_FORMAT_RGBA_UNORM16;

   case PIPE_FORMAT_Z16_UNORM:
      return MESA_FORMAT_Z_UNORM16;
   case PIPE_FORMAT_Z32_UNORM:
      return MESA_FORMAT_Z_UNORM32;
   case PIPE_FORMAT_S8_UINT_Z24_UNORM:
      return MESA_FORMAT_S8_UINT_Z24_UNORM;
   case PIPE_FORMAT_X8Z24_UNORM:
      return MESA_FORMAT_X8_UINT_Z24_UNORM;
   case PIPE_FORMAT_Z24X8_UNORM:
      return MESA_FORMAT_Z24_UNORM_X8_UINT;
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      return MESA_FORMAT_Z24_UNORM_S8_UINT;
   case PIPE_FORMAT_Z32_FLOAT:
      return MESA_FORMAT_Z_FLOAT32;
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      return MESA_FORMAT_Z32_FLOAT_S8X24_UINT;

   case PIPE_FORMAT_UYVY:
      return MESA_FORMAT_YCBCR;
   case PIPE_FORMAT_YUYV:
      return MESA_FORMAT_YCBCR_REV;

   case PIPE_FORMAT_DXT1_RGB:
      return MESA_FORMAT_RGB_DXT1;
   case PIPE_FORMAT_DXT1_RGBA:
      return MESA_FORMAT_RGBA_DXT1;
   case PIPE_FORMAT_DXT3_RGBA:
      return MESA_FORMAT_RGBA_DXT3;
   case PIPE_FORMAT_DXT5_RGBA:
      return MESA_FORMAT_RGBA_DXT5;
   case PIPE_FORMAT_DXT1_SRGB:
      return MESA_FORMAT_SRGB_DXT1;
   case PIPE_FORMAT_DXT1_SRGBA:
      return MESA_FORMAT_SRGBA_DXT1;
   case PIPE_FORMAT_DXT3_SRGBA:
      return MESA_FORMAT_SRGBA_DXT3;
   case PIPE_FORMAT_DXT5_SRGBA:
      return MESA_FORMAT_SRGBA_DXT5;
   case PIPE_FORMAT_LA88_SRGB:
      return MESA_FORMAT_L8A8_SRGB;
   case PIPE_FORMAT_AL88_SRGB:
      return MESA_FORMAT_A8L8_SRGB;
   case PIPE_FORMAT_L8_SRGB:
      return MESA_FORMAT_L_SRGB8;
   case PIPE_FORMAT_R8_SRGB:
      return MESA_FORMAT_R_SRGB8;
   case PIPE_FORMAT_R8G8B8_SRGB:
      return MESA_FORMAT_BGR_SRGB8;
   case PIPE_FORMAT_ABGR8888_SRGB:
      return MESA_FORMAT_A8B8G8R8_SRGB;
   case PIPE_FORMAT_RGBA8888_SRGB:
      return MESA_FORMAT_R8G8B8A8_SRGB;
   case PIPE_FORMAT_BGRA8888_SRGB:
      return MESA_FORMAT_B8G8R8A8_SRGB;
   case PIPE_FORMAT_ARGB8888_SRGB:
      return MESA_FORMAT_A8R8G8B8_SRGB;
   case PIPE_FORMAT_R32G32B32A32_FLOAT:
      return MESA_FORMAT_RGBA_FLOAT32;
   case PIPE_FORMAT_R16G16B16A16_FLOAT:
      return MESA_FORMAT_RGBA_FLOAT16;
   case PIPE_FORMAT_R32G32B32_FLOAT:
      return MESA_FORMAT_RGB_FLOAT32;
   case PIPE_FORMAT_R16G16B16_FLOAT:
      return MESA_FORMAT_RGB_FLOAT16;
   case PIPE_FORMAT_L32A32_FLOAT:
      return MESA_FORMAT_LA_FLOAT32;
   case PIPE_FORMAT_L16A16_FLOAT:
      return MESA_FORMAT_LA_FLOAT16;
   case PIPE_FORMAT_L32_FLOAT:
      return MESA_FORMAT_L_FLOAT32;
   case PIPE_FORMAT_L16_FLOAT:
      return MESA_FORMAT_L_FLOAT16;
   case PIPE_FORMAT_A32_FLOAT:
      return MESA_FORMAT_A_FLOAT32;
   case PIPE_FORMAT_A16_FLOAT:
      return MESA_FORMAT_A_FLOAT16;
   case PIPE_FORMAT_I32_FLOAT:
      return MESA_FORMAT_I_FLOAT32;
   case PIPE_FORMAT_I16_FLOAT:
      return MESA_FORMAT_I_FLOAT16;
   case PIPE_FORMAT_R32_FLOAT:
      return MESA_FORMAT_R_FLOAT32;
   case PIPE_FORMAT_R16_FLOAT:
      return MESA_FORMAT_R_FLOAT16;
   case PIPE_FORMAT_R32G32_FLOAT:
      return MESA_FORMAT_RG_FLOAT32;
   case PIPE_FORMAT_R16G16_FLOAT:
      return MESA_FORMAT_RG_FLOAT16;

   case PIPE_FORMAT_R8_UNORM:
      return MESA_FORMAT_R_UNORM8;
   case PIPE_FORMAT_R16_UNORM:
      return MESA_FORMAT_R_UNORM16;
   case PIPE_FORMAT_RG88_UNORM:
      return MESA_FORMAT_R8G8_UNORM;
   case PIPE_FORMAT_GR88_UNORM:
      return MESA_FORMAT_G8R8_UNORM;
   case PIPE_FORMAT_RG1616_UNORM:
      return MESA_FORMAT_R16G16_UNORM;
   case PIPE_FORMAT_GR1616_UNORM:
      return MESA_FORMAT_G16R16_UNORM;

   case PIPE_FORMAT_A8_UINT:
      return MESA_FORMAT_A_UINT8;
   case PIPE_FORMAT_A16_UINT:
      return MESA_FORMAT_A_UINT16;
   case PIPE_FORMAT_A32_UINT:
      return MESA_FORMAT_A_UINT32;
   case PIPE_FORMAT_A8_SINT:
      return MESA_FORMAT_A_SINT8;
   case PIPE_FORMAT_A16_SINT:
      return MESA_FORMAT_A_SINT16;
   case PIPE_FORMAT_A32_SINT:
      return MESA_FORMAT_A_SINT32;

   case PIPE_FORMAT_I8_UINT:
      return MESA_FORMAT_I_UINT8;
   case PIPE_FORMAT_I16_UINT:
      return MESA_FORMAT_I_UINT16;
   case PIPE_FORMAT_I32_UINT:
      return MESA_FORMAT_I_UINT32;
   case PIPE_FORMAT_I8_SINT:
      return MESA_FORMAT_I_SINT8;
   case PIPE_FORMAT_I16_SINT:
      return MESA_FORMAT_I_SINT16;
   case PIPE_FORMAT_I32_SINT:
      return MESA_FORMAT_I_SINT32;

  case PIPE_FORMAT_L8_UINT:
      return MESA_FORMAT_L_UINT8;
   case PIPE_FORMAT_L16_UINT:
      return MESA_FORMAT_L_UINT16;
   case PIPE_FORMAT_L32_UINT:
      return MESA_FORMAT_L_UINT32;
   case PIPE_FORMAT_L8_SINT:
      return MESA_FORMAT_L_SINT8;
   case PIPE_FORMAT_L16_SINT:
      return MESA_FORMAT_L_SINT16;
   case PIPE_FORMAT_L32_SINT:
      return MESA_FORMAT_L_SINT32;

   case PIPE_FORMAT_L8A8_UINT:
      return MESA_FORMAT_LA_UINT8;
   case PIPE_FORMAT_L16A16_UINT:
      return MESA_FORMAT_LA_UINT16;
   case PIPE_FORMAT_L32A32_UINT:
      return MESA_FORMAT_LA_UINT32;
   case PIPE_FORMAT_L8A8_SINT:
      return MESA_FORMAT_LA_SINT8;
   case PIPE_FORMAT_L16A16_SINT:
      return MESA_FORMAT_LA_SINT16;
   case PIPE_FORMAT_L32A32_SINT:
      return MESA_FORMAT_LA_SINT32;

   case PIPE_FORMAT_R8_SINT:
      return MESA_FORMAT_R_SINT8;
   case PIPE_FORMAT_R8G8_SINT:
      return MESA_FORMAT_RG_SINT8;
   case PIPE_FORMAT_R8G8B8_SINT:
      return MESA_FORMAT_RGB_SINT8;
   case PIPE_FORMAT_R8G8B8A8_SINT:
      return MESA_FORMAT_RGBA_SINT8;

   case PIPE_FORMAT_R16_SINT:
      return MESA_FORMAT_R_SINT16;
   case PIPE_FORMAT_R16G16_SINT:
      return MESA_FORMAT_RG_SINT16;
   case PIPE_FORMAT_R16G16B16_SINT:
      return MESA_FORMAT_RGB_SINT16;
   case PIPE_FORMAT_R16G16B16A16_SINT:
      return MESA_FORMAT_RGBA_SINT16;

   case PIPE_FORMAT_R32_SINT:
      return MESA_FORMAT_R_SINT32;
   case PIPE_FORMAT_R32G32_SINT:
      return MESA_FORMAT_RG_SINT32;
   case PIPE_FORMAT_R32G32B32_SINT:
      return MESA_FORMAT_RGB_SINT32;
   case PIPE_FORMAT_R32G32B32A32_SINT:
      return MESA_FORMAT_RGBA_SINT32;

   /* unsigned int formats */
   case PIPE_FORMAT_R8_UINT:
      return MESA_FORMAT_R_UINT8;
   case PIPE_FORMAT_R8G8_UINT:
      return MESA_FORMAT_RG_UINT8;
   case PIPE_FORMAT_R8G8B8_UINT:
      return MESA_FORMAT_RGB_UINT8;
   case PIPE_FORMAT_R8G8B8A8_UINT:
      return MESA_FORMAT_RGBA_UINT8;

   case PIPE_FORMAT_R16_UINT:
      return MESA_FORMAT_R_UINT16;
   case PIPE_FORMAT_R16G16_UINT:
      return MESA_FORMAT_RG_UINT16;
   case PIPE_FORMAT_R16G16B16_UINT:
      return MESA_FORMAT_RGB_UINT16;
   case PIPE_FORMAT_R16G16B16A16_UINT:
      return MESA_FORMAT_RGBA_UINT16;

   case PIPE_FORMAT_R32_UINT:
      return MESA_FORMAT_R_UINT32;
   case PIPE_FORMAT_R32G32_UINT:
      return MESA_FORMAT_RG_UINT32;
   case PIPE_FORMAT_R32G32B32_UINT:
      return MESA_FORMAT_RGB_UINT32;
   case PIPE_FORMAT_R32G32B32A32_UINT:
      return MESA_FORMAT_RGBA_UINT32;

   case PIPE_FORMAT_RGTC1_UNORM:
      return MESA_FORMAT_R_RGTC1_UNORM;
   case PIPE_FORMAT_RGTC1_SNORM:
      return MESA_FORMAT_R_RGTC1_SNORM;
   case PIPE_FORMAT_RGTC2_UNORM:
      return MESA_FORMAT_RG_RGTC2_UNORM;
   case PIPE_FORMAT_RGTC2_SNORM:
      return MESA_FORMAT_RG_RGTC2_SNORM;

   case PIPE_FORMAT_LATC1_UNORM:
      return MESA_FORMAT_L_LATC1_UNORM;
   case PIPE_FORMAT_LATC1_SNORM:
      return MESA_FORMAT_L_LATC1_SNORM;
   case PIPE_FORMAT_LATC2_UNORM:
      return MESA_FORMAT_LA_LATC2_UNORM;
   case PIPE_FORMAT_LATC2_SNORM:
      return MESA_FORMAT_LA_LATC2_SNORM;

   case PIPE_FORMAT_ETC1_RGB8:
      return MESA_FORMAT_ETC1_RGB8;

   case PIPE_FORMAT_BPTC_RGBA_UNORM:
      return MESA_FORMAT_BPTC_RGBA_UNORM;
   case PIPE_FORMAT_BPTC_SRGBA:
      return MESA_FORMAT_BPTC_SRGB_ALPHA_UNORM;
   case PIPE_FORMAT_BPTC_RGB_FLOAT:
      return MESA_FORMAT_BPTC_RGB_SIGNED_FLOAT;
   case PIPE_FORMAT_BPTC_RGB_UFLOAT:
      return MESA_FORMAT_BPTC_RGB_UNSIGNED_FLOAT;

   /* signed normalized formats */
   case PIPE_FORMAT_R8_SNORM:
      return MESA_FORMAT_R_SNORM8;
   case PIPE_FORMAT_RG88_SNORM:
      return MESA_FORMAT_R8G8_SNORM;
   case PIPE_FORMAT_GR88_SNORM:
      return MESA_FORMAT_G8R8_SNORM;
   case PIPE_FORMAT_RGBA8888_SNORM:
      return MESA_FORMAT_R8G8B8A8_SNORM;
   case PIPE_FORMAT_ABGR8888_SNORM:
      return MESA_FORMAT_A8B8G8R8_SNORM;

   case PIPE_FORMAT_A8_SNORM:
      return MESA_FORMAT_A_SNORM8;
   case PIPE_FORMAT_L8_SNORM:
      return MESA_FORMAT_L_SNORM8;
   case PIPE_FORMAT_LA88_SNORM:
      return MESA_FORMAT_L8A8_SNORM;
   case PIPE_FORMAT_AL88_SNORM:
      return MESA_FORMAT_A8L8_SNORM;
   case PIPE_FORMAT_I8_SNORM:
      return MESA_FORMAT_I_SNORM8;

   case PIPE_FORMAT_R16_SNORM:
      return MESA_FORMAT_R_SNORM16;
   case PIPE_FORMAT_RG1616_SNORM:
      return MESA_FORMAT_R16G16_SNORM;
   case PIPE_FORMAT_GR1616_SNORM:
      return MESA_FORMAT_G16R16_SNORM;
   case PIPE_FORMAT_R16G16B16A16_SNORM:
      return MESA_FORMAT_RGBA_SNORM16;

   case PIPE_FORMAT_A16_SNORM:
      return MESA_FORMAT_A_SNORM16;
   case PIPE_FORMAT_L16_SNORM:
      return MESA_FORMAT_L_SNORM16;
   case PIPE_FORMAT_L16A16_SNORM:
      return MESA_FORMAT_LA_SNORM16;
   case PIPE_FORMAT_I16_SNORM:
      return MESA_FORMAT_I_SNORM16;

   case PIPE_FORMAT_R9G9B9E5_FLOAT:
      return MESA_FORMAT_R9G9B9E5_FLOAT;
   case PIPE_FORMAT_R11G11B10_FLOAT:
      return MESA_FORMAT_R11G11B10_FLOAT;

   case PIPE_FORMAT_B10G10R10A2_UINT:
      return MESA_FORMAT_B10G10R10A2_UINT;
   case PIPE_FORMAT_R10G10B10A2_UINT:
      return MESA_FORMAT_R10G10B10A2_UINT;

   case PIPE_FORMAT_B4G4R4X4_UNORM:
      return MESA_FORMAT_B4G4R4X4_UNORM;
   case PIPE_FORMAT_B5G5R5X1_UNORM:
      return MESA_FORMAT_B5G5R5X1_UNORM;
   case PIPE_FORMAT_X1B5G5R5_UNORM:
      return MESA_FORMAT_X1B5G5R5_UNORM;
   case PIPE_FORMAT_RGBX8888_SNORM:
      return MESA_FORMAT_R8G8B8X8_SNORM;
   case PIPE_FORMAT_XBGR8888_SNORM:
      return MESA_FORMAT_X8B8G8R8_SNORM;
   case PIPE_FORMAT_RGBX8888_SRGB:
      return MESA_FORMAT_R8G8B8X8_SRGB;
   case PIPE_FORMAT_XBGR8888_SRGB:
      return MESA_FORMAT_X8B8G8R8_SRGB;
   case PIPE_FORMAT_R8G8B8X8_UINT:
      return MESA_FORMAT_RGBX_UINT8;
   case PIPE_FORMAT_R8G8B8X8_SINT:
      return MESA_FORMAT_RGBX_SINT8;
   case PIPE_FORMAT_B10G10R10X2_UNORM:
      return MESA_FORMAT_B10G10R10X2_UNORM;
   case PIPE_FORMAT_R16G16B16X16_UNORM:
      return MESA_FORMAT_RGBX_UNORM16;
   case PIPE_FORMAT_R16G16B16X16_SNORM:
      return MESA_FORMAT_RGBX_SNORM16;
   case PIPE_FORMAT_R16G16B16X16_FLOAT:
      return MESA_FORMAT_RGBX_FLOAT16;
   case PIPE_FORMAT_R16G16B16X16_UINT:
      return MESA_FORMAT_RGBX_UINT16;
   case PIPE_FORMAT_R16G16B16X16_SINT:
      return MESA_FORMAT_RGBX_SINT16;
   case PIPE_FORMAT_R32G32B32X32_FLOAT:
      return MESA_FORMAT_RGBX_FLOAT32;
   case PIPE_FORMAT_R32G32B32X32_UINT:
      return MESA_FORMAT_RGBX_UINT32;
   case PIPE_FORMAT_R32G32B32X32_SINT:
      return MESA_FORMAT_RGBX_SINT32;

   case PIPE_FORMAT_BGRX8888_SRGB:
      return MESA_FORMAT_B8G8R8X8_SRGB;
   case PIPE_FORMAT_XRGB8888_SRGB:
      return MESA_FORMAT_X8R8G8B8_SRGB;

   case PIPE_FORMAT_ETC2_RGB8:
      return MESA_FORMAT_ETC2_RGB8;
   case PIPE_FORMAT_ETC2_SRGB8:
      return MESA_FORMAT_ETC2_SRGB8;
   case PIPE_FORMAT_ETC2_RGB8A1:
      return MESA_FORMAT_ETC2_RGB8_PUNCHTHROUGH_ALPHA1;
   case PIPE_FORMAT_ETC2_SRGB8A1:
      return MESA_FORMAT_ETC2_SRGB8_PUNCHTHROUGH_ALPHA1;
   case PIPE_FORMAT_ETC2_RGBA8:
      return MESA_FORMAT_ETC2_RGBA8_EAC;
   case PIPE_FORMAT_ETC2_SRGBA8:
      return MESA_FORMAT_ETC2_SRGB8_ALPHA8_EAC;
   case PIPE_FORMAT_ETC2_R11_UNORM:
      return MESA_FORMAT_ETC2_R11_EAC;
   case PIPE_FORMAT_ETC2_R11_SNORM:
      return MESA_FORMAT_ETC2_SIGNED_R11_EAC;
   case PIPE_FORMAT_ETC2_RG11_UNORM:
      return MESA_FORMAT_ETC2_RG11_EAC;
   case PIPE_FORMAT_ETC2_RG11_SNORM:
      return MESA_FORMAT_ETC2_SIGNED_RG11_EAC;

   case PIPE_FORMAT_ASTC_4x4:
      return MESA_FORMAT_RGBA_ASTC_4x4;
   case PIPE_FORMAT_ASTC_5x4:
      return MESA_FORMAT_RGBA_ASTC_5x4;
   case PIPE_FORMAT_ASTC_5x5:
      return MESA_FORMAT_RGBA_ASTC_5x5;
   case PIPE_FORMAT_ASTC_6x5:
      return MESA_FORMAT_RGBA_ASTC_6x5;
   case PIPE_FORMAT_ASTC_6x6:
      return MESA_FORMAT_RGBA_ASTC_6x6;
   case PIPE_FORMAT_ASTC_8x5:
      return MESA_FORMAT_RGBA_ASTC_8x5;
   case PIPE_FORMAT_ASTC_8x6:
      return MESA_FORMAT_RGBA_ASTC_8x6;
   case PIPE_FORMAT_ASTC_8x8:
      return MESA_FORMAT_RGBA_ASTC_8x8;
   case PIPE_FORMAT_ASTC_10x5:
      return MESA_FORMAT_RGBA_ASTC_10x5;
   case PIPE_FORMAT_ASTC_10x6:
      return MESA_FORMAT_RGBA_ASTC_10x6;
   case PIPE_FORMAT_ASTC_10x8:
      return MESA_FORMAT_RGBA_ASTC_10x8;
   case PIPE_FORMAT_ASTC_10x10:
      return MESA_FORMAT_RGBA_ASTC_10x10;
   case PIPE_FORMAT_ASTC_12x10:
      return MESA_FORMAT_RGBA_ASTC_12x10;
   case PIPE_FORMAT_ASTC_12x12:
      return MESA_FORMAT_RGBA_ASTC_12x12;

   case PIPE_FORMAT_ASTC_4x4_SRGB:
      return MESA_FORMAT_SRGB8_ALPHA8_ASTC_4x4;
   case PIPE_FORMAT_ASTC_5x4_SRGB:
      return MESA_FORMAT_SRGB8_ALPHA8_ASTC_5x4;
   case PIPE_FORMAT_ASTC_5x5_SRGB:
      return MESA_FORMAT_SRGB8_ALPHA8_ASTC_5x5;
   case PIPE_FORMAT_ASTC_6x5_SRGB:
      return MESA_FORMAT_SRGB8_ALPHA8_ASTC_6x5;
   case PIPE_FORMAT_ASTC_6x6_SRGB:
      return MESA_FORMAT_SRGB8_ALPHA8_ASTC_6x6;
   case PIPE_FORMAT_ASTC_8x5_SRGB:
      return MESA_FORMAT_SRGB8_ALPHA8_ASTC_8x5;
   case PIPE_FORMAT_ASTC_8x6_SRGB:
      return MESA_FORMAT_SRGB8_ALPHA8_ASTC_8x6;
   case PIPE_FORMAT_ASTC_8x8_SRGB:
      return MESA_FORMAT_SRGB8_ALPHA8_ASTC_8x8;
   case PIPE_FORMAT_ASTC_10x5_SRGB:
      return MESA_FORMAT_SRGB8_ALPHA8_ASTC_10x5;
   case PIPE_FORMAT_ASTC_10x6_SRGB:
      return MESA_FORMAT_SRGB8_ALPHA8_ASTC_10x6;
   case PIPE_FORMAT_ASTC_10x8_SRGB:
      return MESA_FORMAT_SRGB8_ALPHA8_ASTC_10x8;
   case PIPE_FORMAT_ASTC_10x10_SRGB:
      return MESA_FORMAT_SRGB8_ALPHA8_ASTC_10x10;
   case PIPE_FORMAT_ASTC_12x10_SRGB:
      return MESA_FORMAT_SRGB8_ALPHA8_ASTC_12x10;
   case PIPE_FORMAT_ASTC_12x12_SRGB:
      return MESA_FORMAT_SRGB8_ALPHA8_ASTC_12x12;

   default:
      return MESA_FORMAT_NONE;
   }
}

// fincs-edit: everything from this point on was removed
