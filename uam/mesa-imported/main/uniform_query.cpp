/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2004-2008  Brian Paul   All Rights Reserved.
 * Copyright (C) 2009-2010  VMware, Inc.  All Rights Reserved.
 * Copyright © 2010, 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <inttypes.h>  /* for PRIx64 macro */
#include <math.h>

#include "main/context.h"
//#include "main/shaderapi.h" // fincs-edit
#include "main/shaderobj.h"
#include "main/uniforms.h"
#include "glsl/ir.h" // fincs-edit
#include "glsl/ir_uniform.h" // fincs-edit
#include "glsl/glsl_parser_extras.h" // fincs-edit
#include "glsl/program.h" // fincs-edit
#include "util/bitscan.h"

// fincs-edit: File stripped down except for _mesa_propagate_uniforms_to_driver_storage

/**
 * Propagate some values from uniform backing storage to driver storage
 *
 * Values propagated from uniform backing storage to driver storage
 * have all format / type conversions previously requested by the
 * driver applied.  This function is most often called by the
 * implementations of \c glUniform1f, etc. and \c glUniformMatrix2f,
 * etc.
 *
 * \param uni          Uniform whose data is to be propagated to driver storage
 * \param array_index  If \c uni is an array, this is the element of
 *                     the array to be propagated.
 * \param count        Number of array elements to propagate.
 */
extern "C" void
_mesa_propagate_uniforms_to_driver_storage(struct gl_uniform_storage *uni,
					   unsigned array_index,
					   unsigned count)
{
   unsigned i;

   const unsigned components = uni->type->vector_elements;
   const unsigned vectors = uni->type->matrix_columns;
   const int dmul = uni->type->is_64bit() ? 2 : 1;

   /* Store the data in the driver's requested type in the driver's storage
    * areas.
    */
   unsigned src_vector_byte_stride = components * 4 * dmul;

   for (i = 0; i < uni->num_driver_storage; i++) {
      struct gl_uniform_driver_storage *const store = &uni->driver_storage[i];
      uint8_t *dst = (uint8_t *) store->data;
      const unsigned extra_stride =
	 store->element_stride - (vectors * store->vector_stride);
      const uint8_t *src =
	 (uint8_t *) (&uni->storage[array_index * (dmul * components * vectors)].i);

#if 0
      printf("%s: %p[%d] components=%u vectors=%u count=%u vector_stride=%u "
	     "extra_stride=%u\n",
	     __func__, dst, array_index, components,
	     vectors, count, store->vector_stride, extra_stride);
#endif

      dst += array_index * store->element_stride;

      switch (store->format) {
      case uniform_native: {
	 unsigned j;
	 unsigned v;

	 if (src_vector_byte_stride == store->vector_stride) {
	    if (extra_stride) {
	       for (j = 0; j < count; j++) {
	          memcpy(dst, src, src_vector_byte_stride * vectors);
	          src += src_vector_byte_stride * vectors;
	          dst += store->vector_stride * vectors;

	          dst += extra_stride;
	       }
	    } else {
	       /* Unigine Heaven benchmark gets here */
	       memcpy(dst, src, src_vector_byte_stride * vectors * count);
	       src += src_vector_byte_stride * vectors * count;
	       dst += store->vector_stride * vectors * count;
	    }
	 } else {
	    for (j = 0; j < count; j++) {
	       for (v = 0; v < vectors; v++) {
	          memcpy(dst, src, src_vector_byte_stride);
	          src += src_vector_byte_stride;
	          dst += store->vector_stride;
	       }

	       dst += extra_stride;
	    }
	 }
	 break;
      }

      case uniform_int_float: {
	 const int *isrc = (const int *) src;
	 unsigned j;
	 unsigned v;
	 unsigned c;

	 for (j = 0; j < count; j++) {
	    for (v = 0; v < vectors; v++) {
	       for (c = 0; c < components; c++) {
		  ((float *) dst)[c] = (float) *isrc;
		  isrc++;
	       }

	       dst += store->vector_stride;
	    }

	    dst += extra_stride;
	 }
	 break;
      }

      default:
	 assert(!"Should not get here.");
	 break;
      }
   }
}
