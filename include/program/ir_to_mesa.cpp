/*
 * Copyright (C) 2005-2007  Brian Paul   All Rights Reserved.
 * Copyright (C) 2008  VMware, Inc.   All Rights Reserved.
 * Copyright © 2010 Intel Corporation
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
 */

/**
 * \file ir_to_mesa.cpp
 *
 * Translate GLSL IR to Mesa's gl_program representation.
 */

// fincs-edit: This file was stripped down to the only functions that we actually need:
// _mesa_generate_parameters_list_for_uniforms
// _mesa_associate_uniform_storage

#include <stdio.h>
#include "main/macros.h"
#include "main/mtypes.h"
#include "glsl/standalone_scaffolding.h"
#include "main/uniforms.h"
#include "glsl/ast.h"
#include "glsl/ir.h"
#include "glsl/ir_expression_flattening.h"
#include "glsl/ir_visitor.h"
#include "glsl/ir_optimization.h"
#include "glsl/ir_uniform.h"
#include "glsl/glsl_parser_extras.h"
#include "compiler/glsl_types.h"
#include "glsl/linker.h"
#include "glsl/program.h"
#include "glsl/string_to_uint_map.h"
#include "program/prog_instruction.h"
#include "program/program.h"
#include "program/prog_parameter.h"

#include "ir_to_mesa.h" // fincs-edit

namespace {

class add_uniform_to_shader : public program_resource_visitor {
public:
   add_uniform_to_shader(struct gl_context *ctx,
                         struct gl_shader_program *shader_program,
			 struct gl_program_parameter_list *params)
      : ctx(ctx), params(params), idx(-1)
   {
      /* empty */
   }

   void process(ir_variable *var)
   {
      this->idx = -1;
      this->var = var;
      this->program_resource_visitor::process(var,
                                         ctx->Const.UseSTD430AsDefaultPacking);
      var->data.param_index = this->idx;
   }

private:
   virtual void visit_field(const glsl_type *type, const char *name,
                            bool row_major, const glsl_type *record_type,
                            const enum glsl_interface_packing packing,
                            bool last_field);

   struct gl_context *ctx;
   struct gl_program_parameter_list *params;
   int idx;
   ir_variable *var;
};

} /* anonymous namespace */

void
add_uniform_to_shader::visit_field(const glsl_type *type, const char *name,
                                   bool /* row_major */,
                                   const glsl_type * /* record_type */,
                                   const enum glsl_interface_packing,
                                   bool /* last_field */)
{
   /* opaque types don't use storage in the param list unless they are
    * bindless samplers or images.
    */
   if (type->contains_opaque() && !var->data.bindless)
      return;

   /* Add the uniform to the param list */
   assert(_mesa_lookup_parameter_index(params, name) < 0);
   int index = _mesa_lookup_parameter_index(params, name);

   unsigned num_params = type->arrays_of_arrays_size();
   num_params = MAX2(num_params, 1);
   num_params *= type->without_array()->matrix_columns;

   bool is_dual_slot = type->without_array()->is_dual_slot();
   if (is_dual_slot)
      num_params *= 2;

   _mesa_reserve_parameter_storage(params, num_params);
   index = params->NumParameters;

   if (ctx->Const.PackedDriverUniformStorage) {
      for (unsigned i = 0; i < num_params; i++) {
         unsigned dmul = type->without_array()->is_64bit() ? 2 : 1;
         unsigned comps = type->without_array()->vector_elements * dmul;
         if (is_dual_slot) {
            if (i & 0x1)
               comps -= 4;
            else
               comps = 4;
         }

         _mesa_add_parameter(params, PROGRAM_UNIFORM, name, comps,
                             type->gl_type, NULL, NULL, false);
      }
   } else {
      for (unsigned i = 0; i < num_params; i++) {
         _mesa_add_parameter(params, PROGRAM_UNIFORM, name, 4,
                             type->gl_type, NULL, NULL, true);
      }
   }

   /* The first part of the uniform that's processed determines the base
    * location of the whole uniform (for structures).
    */
   if (this->idx < 0)
      this->idx = index;
}

/**
 * Generate the program parameters list for the user uniforms in a shader
 *
 * \param shader_program Linked shader program.  This is only used to
 *                       emit possible link errors to the info log.
 * \param sh             Shader whose uniforms are to be processed.
 * \param params         Parameter list to be filled in.
 */
void
_mesa_generate_parameters_list_for_uniforms(struct gl_context *ctx,
                                            struct gl_shader_program
					    *shader_program,
					    struct gl_linked_shader *sh,
					    struct gl_program_parameter_list
					    *params)
{
   add_uniform_to_shader add(ctx, shader_program, params);

   foreach_in_list(ir_instruction, node, sh->ir) {
      ir_variable *var = node->as_variable();

      if ((var == NULL) || (var->data.mode != ir_var_uniform)
	  || var->is_in_buffer_block() || (strncmp(var->name, "gl_", 3) == 0))
	 continue;

      add.process(var);
   }
}

void
_mesa_associate_uniform_storage(struct gl_context *ctx,
                                struct gl_shader_program *shader_program,
                                struct gl_program *prog)
{
   struct gl_program_parameter_list *params = prog->Parameters;
   gl_shader_stage shader_type = prog->info.stage;

   /* After adding each uniform to the parameter list, connect the storage for
    * the parameter with the tracking structure used by the API for the
    * uniform.
    */
   unsigned last_location = unsigned(~0);
   for (unsigned i = 0; i < params->NumParameters; i++) {
      if (params->Parameters[i].Type != PROGRAM_UNIFORM)
         continue;

      unsigned location;
      const bool found =
         shader_program->UniformHash->get(location, params->Parameters[i].Name);
      assert(found);

      if (!found)
         continue;

      struct gl_uniform_storage *storage =
         &shader_program->data->UniformStorage[location];

      /* Do not associate any uniform storage to built-in uniforms */
      if (storage->builtin)
         continue;

      if (location != last_location) {
         enum gl_uniform_driver_format format = uniform_native;
         unsigned columns = 0;

         int dmul;
         if (ctx->Const.PackedDriverUniformStorage && !prog->is_arb_asm) {
            dmul = storage->type->vector_elements * sizeof(float);
         } else {
            dmul = 4 * sizeof(float);
         }

         switch (storage->type->base_type) {
         case GLSL_TYPE_UINT64:
            if (storage->type->vector_elements > 2)
               dmul *= 2;
            /* fallthrough */
         case GLSL_TYPE_UINT:
         case GLSL_TYPE_UINT16:
         case GLSL_TYPE_UINT8:
            assert(ctx->Const.NativeIntegers);
            format = uniform_native;
            columns = 1;
            break;
         case GLSL_TYPE_INT64:
            if (storage->type->vector_elements > 2)
               dmul *= 2;
            /* fallthrough */
         case GLSL_TYPE_INT:
         case GLSL_TYPE_INT16:
         case GLSL_TYPE_INT8:
            format =
               (ctx->Const.NativeIntegers) ? uniform_native : uniform_int_float;
            columns = 1;
            break;
         case GLSL_TYPE_DOUBLE:
            if (storage->type->vector_elements > 2)
               dmul *= 2;
            /* fallthrough */
         case GLSL_TYPE_FLOAT:
         case GLSL_TYPE_FLOAT16:
            format = uniform_native;
            columns = storage->type->matrix_columns;
            break;
         case GLSL_TYPE_BOOL:
            format = uniform_native;
            columns = 1;
            break;
         case GLSL_TYPE_SAMPLER:
         case GLSL_TYPE_IMAGE:
         case GLSL_TYPE_SUBROUTINE:
            format = uniform_native;
            columns = 1;
            break;
         case GLSL_TYPE_ATOMIC_UINT:
         case GLSL_TYPE_ARRAY:
         case GLSL_TYPE_VOID:
         case GLSL_TYPE_STRUCT:
         case GLSL_TYPE_ERROR:
         case GLSL_TYPE_INTERFACE:
         case GLSL_TYPE_FUNCTION:
            assert(!"Should not get here.");
            break;
         }

         unsigned pvo = params->ParameterValueOffset[i];
         _mesa_uniform_attach_driver_storage(storage, dmul * columns, dmul,
                                             format,
                                             &params->ParameterValues[pvo]);

         /* When a bindless sampler/image is bound to a texture/image unit, we
          * have to overwrite the constant value by the resident handle
          * directly in the constant buffer before the next draw. One solution
          * is to keep track a pointer to the base of the data.
          */
         if (storage->is_bindless && (prog->sh.NumBindlessSamplers ||
                                      prog->sh.NumBindlessImages)) {
            unsigned array_elements = MAX2(1, storage->array_elements);

            for (unsigned j = 0; j < array_elements; ++j) {
               unsigned unit = storage->opaque[shader_type].index + j;

               if (storage->type->without_array()->is_sampler()) {
                  assert(unit >= 0 && unit < prog->sh.NumBindlessSamplers);
                  prog->sh.BindlessSamplers[unit].data =
                     &params->ParameterValues[pvo] + 4 * j;
               } else if (storage->type->without_array()->is_image()) {
                  assert(unit >= 0 && unit < prog->sh.NumBindlessImages);
                  prog->sh.BindlessImages[unit].data =
                     &params->ParameterValues[pvo] + 4 * j;
               }
            }
         }

         /* After attaching the driver's storage to the uniform, propagate any
          * data from the linker's backing store.  This will cause values from
          * initializers in the source code to be copied over.
          */
         unsigned array_elements = MAX2(1, storage->array_elements);
         if (ctx->Const.PackedDriverUniformStorage && !prog->is_arb_asm &&
             (storage->is_bindless || !storage->type->contains_opaque())) {
            const int dmul = storage->type->is_64bit() ? 2 : 1;
            const unsigned components =
               storage->type->vector_elements *
               storage->type->matrix_columns;

            for (unsigned s = 0; s < storage->num_driver_storage; s++) {
               gl_constant_value *uni_storage = (gl_constant_value *)
                  storage->driver_storage[s].data;
               memcpy(uni_storage, storage->storage,
                      sizeof(storage->storage[0]) * components *
                      array_elements * dmul);
            }
         } else {
            _mesa_propagate_uniforms_to_driver_storage(storage, 0,
                                                       array_elements);
         }

	      last_location = location;
      }
   }
}
