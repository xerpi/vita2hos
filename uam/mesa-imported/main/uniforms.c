/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2004-2008  Brian Paul   All Rights Reserved.
 * Copyright (C) 2009-2010  VMware, Inc.  All Rights Reserved.
 * Copyright © 2010 Intel Corporation
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

/**
 * \file uniforms.c
 * Functions related to GLSL uniform variables.
 * \author Brian Paul
 */

/**
 * XXX things to do:
 * 1. Check that the right error code is generated for all _mesa_error() calls.
 * 2. Insert FLUSH_VERTICES calls in various places
 */

#include "main/glheader.h"
#include "main/context.h"
//#include "main/shaderapi.h" // fincs-edit
#include "main/shaderobj.h"
#include "main/uniforms.h"
#include "main/enums.h"
#include "glsl/ir_uniform.h" // fincs-edit
#include "compiler/glsl_types.h"
#include "program/program.h"
#include "util/bitscan.h"

/**
 * Update the vertex/fragment program's TexturesUsed array.
 *
 * This needs to be called after glUniform(set sampler var) is called.
 * A call to glUniform(samplerVar, value) causes a sampler to point to a
 * particular texture unit.  We know the sampler's texture target
 * (1D/2D/3D/etc) from compile time but the sampler's texture unit is
 * set by glUniform() calls.
 *
 * So, scan the program->SamplerUnits[] and program->SamplerTargets[]
 * information to update the prog->TexturesUsed[] values.
 * Each value of TexturesUsed[unit] is one of zero, TEXTURE_1D_INDEX,
 * TEXTURE_2D_INDEX, TEXTURE_3D_INDEX, etc.
 * We'll use that info for state validation before rendering.
 */
static inline void
update_single_shader_texture_used(struct gl_shader_program *shProg,
                                  struct gl_program *prog,
                                  GLuint unit, GLuint target)
{
   gl_shader_stage prog_stage =
      _mesa_program_enum_to_shader_stage(prog->Target);

   assert(unit < ARRAY_SIZE(prog->TexturesUsed));
   assert(target < NUM_TEXTURE_TARGETS);

   /* From section 7.10 (Samplers) of the OpenGL 4.5 spec:
    *
    * "It is not allowed to have variables of different sampler types pointing
    *  to the same texture image unit within a program object."
    */
   unsigned stages_mask = shProg->data->linked_stages;
   while (stages_mask) {
      const int stage = u_bit_scan(&stages_mask);

      /* Skip validation if we are yet to update textures used in this
       * stage.
       */
      if (prog_stage < stage)
         break;

      struct gl_program *glprog = shProg->_LinkedShaders[stage]->Program;
      if (glprog->TexturesUsed[unit] & ~(1 << target))
         shProg->SamplersValidated = GL_FALSE;
   }

   prog->TexturesUsed[unit] |= (1 << target);
}

void
_mesa_update_shader_textures_used(struct gl_shader_program *shProg,
                                  struct gl_program *prog)
{
   GLbitfield mask = prog->SamplersUsed;
   gl_shader_stage prog_stage =
      _mesa_program_enum_to_shader_stage(prog->Target);
   MAYBE_UNUSED struct gl_linked_shader *shader =
      shProg->_LinkedShaders[prog_stage];
   GLuint s;

   assert(shader);

   memset(prog->TexturesUsed, 0, sizeof(prog->TexturesUsed));

   while (mask) {
      s = u_bit_scan(&mask);

      update_single_shader_texture_used(shProg, prog,
                                        prog->SamplerUnits[s],
                                        prog->sh.SamplerTargets[s]);
   }

   if (unlikely(prog->sh.HasBoundBindlessSampler)) {
      /* Loop over bindless samplers bound to texture units.
       */
      for (s = 0; s < prog->sh.NumBindlessSamplers; s++) {
         struct gl_bindless_sampler *sampler = &prog->sh.BindlessSamplers[s];

         if (!sampler->bound)
            continue;

         update_single_shader_texture_used(shProg, prog, sampler->unit,
                                           sampler->target);
      }
   }
}

/**
 * Connect a piece of driver storage with a part of a uniform
 *
 * \param uni            The uniform with which the storage will be associated
 * \param element_stride Byte-stride between array elements.
 *                       \sa gl_uniform_driver_storage::element_stride.
 * \param vector_stride  Byte-stride between vectors (in a matrix).
 *                       \sa gl_uniform_driver_storage::vector_stride.
 * \param format         Conversion from native format to driver format
 *                       required by the driver.
 * \param data           Location to dump the data.
 */
void
_mesa_uniform_attach_driver_storage(struct gl_uniform_storage *uni,
				    unsigned element_stride,
				    unsigned vector_stride,
				    enum gl_uniform_driver_format format,
				    void *data)
{
   uni->driver_storage =
      realloc(uni->driver_storage,
	      sizeof(struct gl_uniform_driver_storage)
	      * (uni->num_driver_storage + 1));

   uni->driver_storage[uni->num_driver_storage].element_stride = element_stride;
   uni->driver_storage[uni->num_driver_storage].vector_stride = vector_stride;
   uni->driver_storage[uni->num_driver_storage].format = format;
   uni->driver_storage[uni->num_driver_storage].data = data;

   uni->num_driver_storage++;
}

/**
 * Sever all connections with all pieces of driver storage for all uniforms
 *
 * \warning
 * This function does \b not release any of the \c data pointers
 * previously passed in to \c _mesa_uniform_attach_driver_stoarge.
 */
void
_mesa_uniform_detach_all_driver_storage(struct gl_uniform_storage *uni)
{
   free(uni->driver_storage);
   uni->driver_storage = NULL;
   uni->num_driver_storage = 0;
}

// fincs-edit: Everything below this point was removed.
