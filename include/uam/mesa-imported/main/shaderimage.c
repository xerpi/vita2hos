/*
 * Copyright 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Francisco Jerez <currojerez@riseup.net>
 */

#include <assert.h>

#include "shaderimage.h"
#include "mtypes.h"
#include "formats.h"
#include "errors.h"
#include "hash.h"
#include "context.h"
//#include "texobj.h" // fincs-edit
//#include "teximage.h" // fincs-edit
#include "enums.h"

/*
 * Define endian-invariant aliases for some mesa formats that are
 * defined in terms of their channel layout from LSB to MSB in a
 * 32-bit word.  The actual byte offsets matter here because the user
 * is allowed to bit-cast one format into another and get predictable
 * results.
 */
#ifdef MESA_BIG_ENDIAN
# define MESA_FORMAT_RGBA_8 MESA_FORMAT_A8B8G8R8_UNORM
# define MESA_FORMAT_RG_16 MESA_FORMAT_G16R16_UNORM
# define MESA_FORMAT_RG_8 MESA_FORMAT_G8R8_UNORM
# define MESA_FORMAT_SIGNED_RGBA_8 MESA_FORMAT_A8B8G8R8_SNORM
# define MESA_FORMAT_SIGNED_RG_16 MESA_FORMAT_G16R16_SNORM
# define MESA_FORMAT_SIGNED_RG_8 MESA_FORMAT_G8R8_SNORM
#else
# define MESA_FORMAT_RGBA_8 MESA_FORMAT_R8G8B8A8_UNORM
# define MESA_FORMAT_RG_16 MESA_FORMAT_R16G16_UNORM
# define MESA_FORMAT_RG_8 MESA_FORMAT_R8G8_UNORM
# define MESA_FORMAT_SIGNED_RGBA_8 MESA_FORMAT_R8G8B8A8_SNORM
# define MESA_FORMAT_SIGNED_RG_16 MESA_FORMAT_R16G16_SNORM
# define MESA_FORMAT_SIGNED_RG_8 MESA_FORMAT_R8G8_SNORM
#endif

mesa_format
_mesa_get_shader_image_format(GLenum format)
{
   switch (format) {
   case GL_RGBA32F:
      return MESA_FORMAT_RGBA_FLOAT32;

   case GL_RGBA16F:
      return MESA_FORMAT_RGBA_FLOAT16;

   case GL_RG32F:
      return MESA_FORMAT_RG_FLOAT32;

   case GL_RG16F:
      return MESA_FORMAT_RG_FLOAT16;

   case GL_R11F_G11F_B10F:
      return MESA_FORMAT_R11G11B10_FLOAT;

   case GL_R32F:
      return MESA_FORMAT_R_FLOAT32;

   case GL_R16F:
      return MESA_FORMAT_R_FLOAT16;

   case GL_RGBA32UI:
      return MESA_FORMAT_RGBA_UINT32;

   case GL_RGBA16UI:
      return MESA_FORMAT_RGBA_UINT16;

   case GL_RGB10_A2UI:
      return MESA_FORMAT_R10G10B10A2_UINT;

   case GL_RGBA8UI:
      return MESA_FORMAT_RGBA_UINT8;

   case GL_RG32UI:
      return MESA_FORMAT_RG_UINT32;

   case GL_RG16UI:
      return MESA_FORMAT_RG_UINT16;

   case GL_RG8UI:
      return MESA_FORMAT_RG_UINT8;

   case GL_R32UI:
      return MESA_FORMAT_R_UINT32;

   case GL_R16UI:
      return MESA_FORMAT_R_UINT16;

   case GL_R8UI:
      return MESA_FORMAT_R_UINT8;

   case GL_RGBA32I:
      return MESA_FORMAT_RGBA_SINT32;

   case GL_RGBA16I:
      return MESA_FORMAT_RGBA_SINT16;

   case GL_RGBA8I:
      return MESA_FORMAT_RGBA_SINT8;

   case GL_RG32I:
      return MESA_FORMAT_RG_SINT32;

   case GL_RG16I:
      return MESA_FORMAT_RG_SINT16;

   case GL_RG8I:
      return MESA_FORMAT_RG_SINT8;

   case GL_R32I:
      return MESA_FORMAT_R_SINT32;

   case GL_R16I:
      return MESA_FORMAT_R_SINT16;

   case GL_R8I:
      return MESA_FORMAT_R_SINT8;

   case GL_RGBA16:
      return MESA_FORMAT_RGBA_UNORM16;

   case GL_RGB10_A2:
      return MESA_FORMAT_R10G10B10A2_UNORM;

   case GL_RGBA8:
      return MESA_FORMAT_RGBA_8;

   case GL_RG16:
      return MESA_FORMAT_RG_16;

   case GL_RG8:
      return MESA_FORMAT_RG_8;

   case GL_R16:
      return MESA_FORMAT_R_UNORM16;

   case GL_R8:
      return MESA_FORMAT_R_UNORM8;

   case GL_RGBA16_SNORM:
      return MESA_FORMAT_RGBA_SNORM16;

   case GL_RGBA8_SNORM:
      return MESA_FORMAT_SIGNED_RGBA_8;

   case GL_RG16_SNORM:
      return MESA_FORMAT_SIGNED_RG_16;

   case GL_RG8_SNORM:
      return MESA_FORMAT_SIGNED_RG_8;

   case GL_R16_SNORM:
      return MESA_FORMAT_R_SNORM16;

   case GL_R8_SNORM:
      return MESA_FORMAT_R_SNORM8;

   default:
      return MESA_FORMAT_NONE;
   }
}

enum image_format_class
{
   /** Not a valid image format. */
   IMAGE_FORMAT_CLASS_NONE = 0,

   /** Classes of image formats you can cast into each other. */
   /** \{ */
   IMAGE_FORMAT_CLASS_1X8,
   IMAGE_FORMAT_CLASS_1X16,
   IMAGE_FORMAT_CLASS_1X32,
   IMAGE_FORMAT_CLASS_2X8,
   IMAGE_FORMAT_CLASS_2X16,
   IMAGE_FORMAT_CLASS_2X32,
   IMAGE_FORMAT_CLASS_10_11_11,
   IMAGE_FORMAT_CLASS_4X8,
   IMAGE_FORMAT_CLASS_4X16,
   IMAGE_FORMAT_CLASS_4X32,
   IMAGE_FORMAT_CLASS_2_10_10_10
   /** \} */
};

static enum image_format_class
get_image_format_class(mesa_format format)
{
   switch (format) {
   case MESA_FORMAT_RGBA_FLOAT32:
      return IMAGE_FORMAT_CLASS_4X32;

   case MESA_FORMAT_RGBA_FLOAT16:
      return IMAGE_FORMAT_CLASS_4X16;

   case MESA_FORMAT_RG_FLOAT32:
      return IMAGE_FORMAT_CLASS_2X32;

   case MESA_FORMAT_RG_FLOAT16:
      return IMAGE_FORMAT_CLASS_2X16;

   case MESA_FORMAT_R11G11B10_FLOAT:
      return IMAGE_FORMAT_CLASS_10_11_11;

   case MESA_FORMAT_R_FLOAT32:
      return IMAGE_FORMAT_CLASS_1X32;

   case MESA_FORMAT_R_FLOAT16:
      return IMAGE_FORMAT_CLASS_1X16;

   case MESA_FORMAT_RGBA_UINT32:
      return IMAGE_FORMAT_CLASS_4X32;

   case MESA_FORMAT_RGBA_UINT16:
      return IMAGE_FORMAT_CLASS_4X16;

   case MESA_FORMAT_R10G10B10A2_UINT:
      return IMAGE_FORMAT_CLASS_2_10_10_10;

   case MESA_FORMAT_RGBA_UINT8:
      return IMAGE_FORMAT_CLASS_4X8;

   case MESA_FORMAT_RG_UINT32:
      return IMAGE_FORMAT_CLASS_2X32;

   case MESA_FORMAT_RG_UINT16:
      return IMAGE_FORMAT_CLASS_2X16;

   case MESA_FORMAT_RG_UINT8:
      return IMAGE_FORMAT_CLASS_2X8;

   case MESA_FORMAT_R_UINT32:
      return IMAGE_FORMAT_CLASS_1X32;

   case MESA_FORMAT_R_UINT16:
      return IMAGE_FORMAT_CLASS_1X16;

   case MESA_FORMAT_R_UINT8:
      return IMAGE_FORMAT_CLASS_1X8;

   case MESA_FORMAT_RGBA_SINT32:
      return IMAGE_FORMAT_CLASS_4X32;

   case MESA_FORMAT_RGBA_SINT16:
      return IMAGE_FORMAT_CLASS_4X16;

   case MESA_FORMAT_RGBA_SINT8:
      return IMAGE_FORMAT_CLASS_4X8;

   case MESA_FORMAT_RG_SINT32:
      return IMAGE_FORMAT_CLASS_2X32;

   case MESA_FORMAT_RG_SINT16:
      return IMAGE_FORMAT_CLASS_2X16;

   case MESA_FORMAT_RG_SINT8:
      return IMAGE_FORMAT_CLASS_2X8;

   case MESA_FORMAT_R_SINT32:
      return IMAGE_FORMAT_CLASS_1X32;

   case MESA_FORMAT_R_SINT16:
      return IMAGE_FORMAT_CLASS_1X16;

   case MESA_FORMAT_R_SINT8:
      return IMAGE_FORMAT_CLASS_1X8;

   case MESA_FORMAT_RGBA_UNORM16:
      return IMAGE_FORMAT_CLASS_4X16;

   case MESA_FORMAT_R10G10B10A2_UNORM:
      return IMAGE_FORMAT_CLASS_2_10_10_10;

   case MESA_FORMAT_RGBA_8:
      return IMAGE_FORMAT_CLASS_4X8;

   case MESA_FORMAT_RG_16:
      return IMAGE_FORMAT_CLASS_2X16;

   case MESA_FORMAT_RG_8:
      return IMAGE_FORMAT_CLASS_2X8;

   case MESA_FORMAT_R_UNORM16:
      return IMAGE_FORMAT_CLASS_1X16;

   case MESA_FORMAT_R_UNORM8:
      return IMAGE_FORMAT_CLASS_1X8;

   case MESA_FORMAT_RGBA_SNORM16:
      return IMAGE_FORMAT_CLASS_4X16;

   case MESA_FORMAT_SIGNED_RGBA_8:
      return IMAGE_FORMAT_CLASS_4X8;

   case MESA_FORMAT_SIGNED_RG_16:
      return IMAGE_FORMAT_CLASS_2X16;

   case MESA_FORMAT_SIGNED_RG_8:
      return IMAGE_FORMAT_CLASS_2X8;

   case MESA_FORMAT_R_SNORM16:
      return IMAGE_FORMAT_CLASS_1X16;

   case MESA_FORMAT_R_SNORM8:
      return IMAGE_FORMAT_CLASS_1X8;

   default:
      return IMAGE_FORMAT_CLASS_NONE;
   }
}

static GLenum
_image_format_class_to_glenum(enum image_format_class class)
{
   switch (class) {
   case IMAGE_FORMAT_CLASS_NONE:
      return GL_NONE;
   case IMAGE_FORMAT_CLASS_1X8:
      return GL_IMAGE_CLASS_1_X_8;
   case IMAGE_FORMAT_CLASS_1X16:
      return GL_IMAGE_CLASS_1_X_16;
   case IMAGE_FORMAT_CLASS_1X32:
      return GL_IMAGE_CLASS_1_X_32;
   case IMAGE_FORMAT_CLASS_2X8:
      return GL_IMAGE_CLASS_2_X_8;
   case IMAGE_FORMAT_CLASS_2X16:
      return GL_IMAGE_CLASS_2_X_16;
   case IMAGE_FORMAT_CLASS_2X32:
      return GL_IMAGE_CLASS_2_X_32;
   case IMAGE_FORMAT_CLASS_10_11_11:
      return GL_IMAGE_CLASS_11_11_10;
   case IMAGE_FORMAT_CLASS_4X8:
      return GL_IMAGE_CLASS_4_X_8;
   case IMAGE_FORMAT_CLASS_4X16:
      return GL_IMAGE_CLASS_4_X_16;
   case IMAGE_FORMAT_CLASS_4X32:
      return GL_IMAGE_CLASS_4_X_32;
   case IMAGE_FORMAT_CLASS_2_10_10_10:
      return GL_IMAGE_CLASS_10_10_10_2;
   default:
      assert(!"Invalid image_format_class");
      return GL_NONE;
   }
}

GLenum
_mesa_get_image_format_class(GLenum format)
{
   mesa_format tex_format = _mesa_get_shader_image_format(format);
   if (tex_format == MESA_FORMAT_NONE)
      return GL_NONE;

   enum image_format_class class = get_image_format_class(tex_format);
   return _image_format_class_to_glenum(class);
}

bool
_mesa_is_shader_image_format_supported(const struct gl_context *ctx,
                                       GLenum format)
{
   switch (format) {
   /* Formats supported on both desktop and ES GL, c.f. table 8.27 of the
    * OpenGL ES 3.1 specification.
    */
   case GL_RGBA32F:
   case GL_RGBA16F:
   case GL_R32F:
   case GL_RGBA32UI:
   case GL_RGBA16UI:
   case GL_RGBA8UI:
   case GL_R32UI:
   case GL_RGBA32I:
   case GL_RGBA16I:
   case GL_RGBA8I:
   case GL_R32I:
   case GL_RGBA8:
   case GL_RGBA8_SNORM:
      return true;

   /* Formats supported on unextended desktop GL and the original
    * ARB_shader_image_load_store extension, c.f. table 3.21 of the OpenGL 4.2
    * specification or by GLES 3.1 with GL_NV_image_formats extension.
    */
   case GL_RG32F:
   case GL_RG16F:
   case GL_R11F_G11F_B10F:
   case GL_R16F:
   case GL_RGB10_A2UI:
   case GL_RG32UI:
   case GL_RG16UI:
   case GL_RG8UI:
   case GL_R16UI:
   case GL_R8UI:
   case GL_RG32I:
   case GL_RG16I:
   case GL_RG8I:
   case GL_R16I:
   case GL_R8I:
   case GL_RGB10_A2:
   case GL_RG8:
   case GL_R8:
   case GL_RG8_SNORM:
   case GL_R8_SNORM:
      return true;

   /* Formats supported on unextended desktop GL and the original
    * ARB_shader_image_load_store extension, c.f. table 3.21 of the OpenGL 4.2
    * specification.
    *
    * Following formats are supported by GLES 3.1 with GL_NV_image_formats &
    * GL_EXT_texture_norm16 extensions.
    */
   case GL_RGBA16:
   case GL_RGBA16_SNORM:
   case GL_RG16:
   case GL_RG16_SNORM:
   case GL_R16:
   case GL_R16_SNORM:
      return true; //_mesa_is_desktop_gl(ctx) || _mesa_has_EXT_texture_norm16(ctx); // fincs-edit

   default:
      return false;
   }
}

// fincs-edit: Everything below this point was deleted.
