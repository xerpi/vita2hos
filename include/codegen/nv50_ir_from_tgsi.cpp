/*
 * Copyright 2011 Christoph Bumiller
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "tgsi/tgsi_build.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_scan.h"
#include "tgsi/tgsi_util.h"

#include <set>

#include "codegen/nv50_ir.h"
#include "codegen/nv50_ir_util.h"
#include "codegen/nv50_ir_build_util.h"

namespace tgsi {

class Source;

static nv50_ir::operation translateOpcode(uint opcode);
static nv50_ir::DataFile translateFile(uint file);
static nv50_ir::TexTarget translateTexture(uint texTarg);
static nv50_ir::SVSemantic translateSysVal(uint sysval);
static nv50_ir::CacheMode translateCacheMode(uint qualifier);
static nv50_ir::ImgFormat translateImgFormat(uint format);

class Instruction
{
public:
   Instruction(const struct tgsi_full_instruction *inst) : insn(inst) { }

   class SrcRegister
   {
   public:
      SrcRegister(const struct tgsi_full_src_register *src)
         : reg(src->Register),
           fsr(src)
      { }

      SrcRegister(const struct tgsi_src_register& src) : reg(src), fsr(NULL) { }

      SrcRegister(const struct tgsi_ind_register& ind)
         : reg(tgsi_util_get_src_from_ind(&ind)),
           fsr(NULL)
      { }

      struct tgsi_src_register offsetToSrc(struct tgsi_texture_offset off)
      {
         struct tgsi_src_register reg;
         memset(&reg, 0, sizeof(reg));
         reg.Index = off.Index;
         reg.File = off.File;
         reg.SwizzleX = off.SwizzleX;
         reg.SwizzleY = off.SwizzleY;
         reg.SwizzleZ = off.SwizzleZ;
         return reg;
      }

      SrcRegister(const struct tgsi_texture_offset& off) :
         reg(offsetToSrc(off)),
         fsr(NULL)
      { }

      uint getFile() const { return reg.File; }

      bool is2D() const { return reg.Dimension; }

      bool isIndirect(int dim) const
      {
         return (dim && fsr) ? fsr->Dimension.Indirect : reg.Indirect;
      }

      int getIndex(int dim) const
      {
         return (dim && fsr) ? fsr->Dimension.Index : reg.Index;
      }

      int getSwizzle(int chan) const
      {
         return tgsi_util_get_src_register_swizzle(&reg, chan);
      }

      int getArrayId() const
      {
         if (isIndirect(0))
            return fsr->Indirect.ArrayID;
         return 0;
      }

      nv50_ir::Modifier getMod(int chan) const;

      SrcRegister getIndirect(int dim) const
      {
         assert(fsr && isIndirect(dim));
         if (dim)
            return SrcRegister(fsr->DimIndirect);
         return SrcRegister(fsr->Indirect);
      }

      uint32_t getValueU32(int c, const struct nv50_ir_prog_info *info) const
      {
         assert(reg.File == TGSI_FILE_IMMEDIATE);
         assert(!reg.Absolute);
         assert(!reg.Negate);
         return info->immd.data[reg.Index * 4 + getSwizzle(c)];
      }

   private:
      const struct tgsi_src_register reg;
      const struct tgsi_full_src_register *fsr;
   };

   class DstRegister
   {
   public:
      DstRegister(const struct tgsi_full_dst_register *dst)
         : reg(dst->Register),
           fdr(dst)
      { }

      DstRegister(const struct tgsi_dst_register& dst) : reg(dst), fdr(NULL) { }

      uint getFile() const { return reg.File; }

      bool is2D() const { return reg.Dimension; }

      bool isIndirect(int dim) const
      {
         return (dim && fdr) ? fdr->Dimension.Indirect : reg.Indirect;
      }

      int getIndex(int dim) const
      {
         return (dim && fdr) ? fdr->Dimension.Dimension : reg.Index;
      }

      unsigned int getMask() const { return reg.WriteMask; }

      bool isMasked(int chan) const { return !(getMask() & (1 << chan)); }

      SrcRegister getIndirect(int dim) const
      {
         assert(fdr && isIndirect(dim));
         if (dim)
            return SrcRegister(fdr->DimIndirect);
         return SrcRegister(fdr->Indirect);
      }

      struct tgsi_full_src_register asSrc()
      {
         assert(fdr);
         return tgsi_full_src_register_from_dst(fdr);
      }

      int getArrayId() const
      {
         if (isIndirect(0))
            return fdr->Indirect.ArrayID;
         return 0;
      }

   private:
      const struct tgsi_dst_register reg;
      const struct tgsi_full_dst_register *fdr;
   };

   inline uint getOpcode() const { return insn->Instruction.Opcode; }

   unsigned int srcCount() const { return insn->Instruction.NumSrcRegs; }
   unsigned int dstCount() const { return insn->Instruction.NumDstRegs; }

   // mask of used components of source s
   unsigned int srcMask(unsigned int s) const;
   unsigned int texOffsetMask() const;

   SrcRegister getSrc(unsigned int s) const
   {
      assert(s < srcCount());
      return SrcRegister(&insn->Src[s]);
   }

   DstRegister getDst(unsigned int d) const
   {
      assert(d < dstCount());
      return DstRegister(&insn->Dst[d]);
   }

   SrcRegister getTexOffset(unsigned int i) const
   {
      assert(i < TGSI_FULL_MAX_TEX_OFFSETS);
      return SrcRegister(insn->TexOffsets[i]);
   }

   unsigned int getNumTexOffsets() const { return insn->Texture.NumOffsets; }

   bool checkDstSrcAliasing() const;

   inline nv50_ir::operation getOP() const {
      return translateOpcode(getOpcode()); }

   nv50_ir::DataType inferSrcType() const;
   nv50_ir::DataType inferDstType() const;

   nv50_ir::CondCode getSetCond() const;

   nv50_ir::TexInstruction::Target getTexture(const Source *, int s) const;

   const nv50_ir::TexInstruction::ImgFormatDesc *getImageFormat() const {
      return &nv50_ir::TexInstruction::formatTable[
            translateImgFormat(insn->Memory.Format)];
   }

   nv50_ir::TexTarget getImageTarget() const {
      return translateTexture(insn->Memory.Texture);
   }

   nv50_ir::CacheMode getCacheMode() const {
      if (!insn->Instruction.Memory)
         return nv50_ir::CACHE_CA;
      return translateCacheMode(insn->Memory.Qualifier);
   }

   inline uint getLabel() { return insn->Label.Label; }

   unsigned getSaturate() const { return insn->Instruction.Saturate; }

   void print() const
   {
      tgsi_dump_instruction(insn, 1);
   }

private:
   const struct tgsi_full_instruction *insn;
};

unsigned int Instruction::texOffsetMask() const
{
   const struct tgsi_instruction_texture *tex = &insn->Texture;
   assert(insn->Instruction.Texture);

   switch (tex->Texture) {
   case TGSI_TEXTURE_BUFFER:
   case TGSI_TEXTURE_1D:
   case TGSI_TEXTURE_SHADOW1D:
   case TGSI_TEXTURE_1D_ARRAY:
   case TGSI_TEXTURE_SHADOW1D_ARRAY:
      return 0x1;
   case TGSI_TEXTURE_2D:
   case TGSI_TEXTURE_SHADOW2D:
   case TGSI_TEXTURE_2D_ARRAY:
   case TGSI_TEXTURE_SHADOW2D_ARRAY:
   case TGSI_TEXTURE_RECT:
   case TGSI_TEXTURE_SHADOWRECT:
   case TGSI_TEXTURE_2D_MSAA:
   case TGSI_TEXTURE_2D_ARRAY_MSAA:
      return 0x3;
   case TGSI_TEXTURE_3D:
      return 0x7;
   default:
      assert(!"Unexpected texture target");
      return 0xf;
   }
}

unsigned int Instruction::srcMask(unsigned int s) const
{
   unsigned int mask = insn->Dst[0].Register.WriteMask;

   switch (insn->Instruction.Opcode) {
   case TGSI_OPCODE_COS:
   case TGSI_OPCODE_SIN:
      return (mask & 0x8) | ((mask & 0x7) ? 0x1 : 0x0);
   case TGSI_OPCODE_DP2:
      return 0x3;
   case TGSI_OPCODE_DP3:
      return 0x7;
   case TGSI_OPCODE_DP4:
   case TGSI_OPCODE_KILL_IF: /* WriteMask ignored */
      return 0xf;
   case TGSI_OPCODE_DST:
      return mask & (s ? 0xa : 0x6);
   case TGSI_OPCODE_EX2:
   case TGSI_OPCODE_EXP:
   case TGSI_OPCODE_LG2:
   case TGSI_OPCODE_LOG:
   case TGSI_OPCODE_POW:
   case TGSI_OPCODE_RCP:
   case TGSI_OPCODE_RSQ:
      return 0x1;
   case TGSI_OPCODE_IF:
   case TGSI_OPCODE_UIF:
      return 0x1;
   case TGSI_OPCODE_LIT:
      return 0xb;
   case TGSI_OPCODE_TEX2:
   case TGSI_OPCODE_TXB2:
   case TGSI_OPCODE_TXL2:
      return (s == 0) ? 0xf : 0x3;
   case TGSI_OPCODE_TEX:
   case TGSI_OPCODE_TXB:
   case TGSI_OPCODE_TXD:
   case TGSI_OPCODE_TXL:
   case TGSI_OPCODE_TXP:
   case TGSI_OPCODE_TXF:
   case TGSI_OPCODE_TG4:
   case TGSI_OPCODE_TEX_LZ:
   case TGSI_OPCODE_TXF_LZ:
   case TGSI_OPCODE_LODQ:
   {
      const struct tgsi_instruction_texture *tex = &insn->Texture;

      assert(insn->Instruction.Texture);

      mask = 0x7;
      if (insn->Instruction.Opcode != TGSI_OPCODE_TEX &&
          insn->Instruction.Opcode != TGSI_OPCODE_TEX_LZ &&
          insn->Instruction.Opcode != TGSI_OPCODE_TXF_LZ &&
          insn->Instruction.Opcode != TGSI_OPCODE_TXD)
         mask |= 0x8; /* bias, lod or proj */

      switch (tex->Texture) {
      case TGSI_TEXTURE_1D:
         mask &= 0x9;
         break;
      case TGSI_TEXTURE_SHADOW1D:
         mask &= 0xd;
         break;
      case TGSI_TEXTURE_1D_ARRAY:
      case TGSI_TEXTURE_2D:
      case TGSI_TEXTURE_RECT:
         mask &= 0xb;
         break;
      case TGSI_TEXTURE_CUBE_ARRAY:
      case TGSI_TEXTURE_SHADOW2D_ARRAY:
      case TGSI_TEXTURE_SHADOWCUBE:
      case TGSI_TEXTURE_SHADOWCUBE_ARRAY:
         mask |= 0x8;
         break;
      default:
         break;
      }
   }
      return mask;
   case TGSI_OPCODE_TXQ:
      return 1;
   case TGSI_OPCODE_D2I:
   case TGSI_OPCODE_D2U:
   case TGSI_OPCODE_D2F:
   case TGSI_OPCODE_DSLT:
   case TGSI_OPCODE_DSGE:
   case TGSI_OPCODE_DSEQ:
   case TGSI_OPCODE_DSNE:
   case TGSI_OPCODE_U64SEQ:
   case TGSI_OPCODE_U64SNE:
   case TGSI_OPCODE_I64SLT:
   case TGSI_OPCODE_U64SLT:
   case TGSI_OPCODE_I64SGE:
   case TGSI_OPCODE_U64SGE:
   case TGSI_OPCODE_I642F:
   case TGSI_OPCODE_U642F:
      switch (util_bitcount(mask)) {
      case 1: return 0x3;
      case 2: return 0xf;
      default:
         assert(!"unexpected mask");
         return 0xf;
      }
   case TGSI_OPCODE_I2D:
   case TGSI_OPCODE_U2D:
   case TGSI_OPCODE_F2D: {
      unsigned int x = 0;
      if ((mask & 0x3) == 0x3)
         x |= 1;
      if ((mask & 0xc) == 0xc)
         x |= 2;
      return x;
   }
   case TGSI_OPCODE_PK2H:
      return 0x3;
   case TGSI_OPCODE_UP2H:
      return 0x1;
   default:
      break;
   }

   return mask;
}

nv50_ir::Modifier Instruction::SrcRegister::getMod(int chan) const
{
   nv50_ir::Modifier m(0);

   if (reg.Absolute)
      m = m | nv50_ir::Modifier(NV50_IR_MOD_ABS);
   if (reg.Negate)
      m = m | nv50_ir::Modifier(NV50_IR_MOD_NEG);
   return m;
}

static nv50_ir::DataFile translateFile(uint file)
{
   switch (file) {
   case TGSI_FILE_CONSTANT:        return nv50_ir::FILE_MEMORY_CONST;
   case TGSI_FILE_INPUT:           return nv50_ir::FILE_SHADER_INPUT;
   case TGSI_FILE_OUTPUT:          return nv50_ir::FILE_SHADER_OUTPUT;
   case TGSI_FILE_TEMPORARY:       return nv50_ir::FILE_GPR;
   case TGSI_FILE_ADDRESS:         return nv50_ir::FILE_ADDRESS;
   case TGSI_FILE_IMMEDIATE:       return nv50_ir::FILE_IMMEDIATE;
   case TGSI_FILE_SYSTEM_VALUE:    return nv50_ir::FILE_SYSTEM_VALUE;
   case TGSI_FILE_BUFFER:          return nv50_ir::FILE_MEMORY_BUFFER;
   case TGSI_FILE_IMAGE:           return nv50_ir::FILE_MEMORY_GLOBAL;
   case TGSI_FILE_MEMORY:          return nv50_ir::FILE_MEMORY_GLOBAL;
   case TGSI_FILE_SAMPLER:
   case TGSI_FILE_NULL:
   default:
      return nv50_ir::FILE_NULL;
   }
}

static nv50_ir::SVSemantic translateSysVal(uint sysval)
{
   switch (sysval) {
   case TGSI_SEMANTIC_FACE:       return nv50_ir::SV_FACE;
   case TGSI_SEMANTIC_PSIZE:      return nv50_ir::SV_POINT_SIZE;
   case TGSI_SEMANTIC_PRIMID:     return nv50_ir::SV_PRIMITIVE_ID;
   case TGSI_SEMANTIC_INSTANCEID: return nv50_ir::SV_INSTANCE_ID;
   case TGSI_SEMANTIC_VERTEXID:   return nv50_ir::SV_VERTEX_ID;
   case TGSI_SEMANTIC_GRID_SIZE:  return nv50_ir::SV_NCTAID;
   case TGSI_SEMANTIC_BLOCK_ID:   return nv50_ir::SV_CTAID;
   case TGSI_SEMANTIC_BLOCK_SIZE: return nv50_ir::SV_NTID;
   case TGSI_SEMANTIC_THREAD_ID:  return nv50_ir::SV_TID;
   case TGSI_SEMANTIC_SAMPLEID:   return nv50_ir::SV_SAMPLE_INDEX;
   case TGSI_SEMANTIC_SAMPLEPOS:  return nv50_ir::SV_SAMPLE_POS;
   case TGSI_SEMANTIC_SAMPLEMASK: return nv50_ir::SV_SAMPLE_MASK;
   case TGSI_SEMANTIC_INVOCATIONID: return nv50_ir::SV_INVOCATION_ID;
   case TGSI_SEMANTIC_TESSCOORD:  return nv50_ir::SV_TESS_COORD;
   case TGSI_SEMANTIC_TESSOUTER:  return nv50_ir::SV_TESS_OUTER;
   case TGSI_SEMANTIC_TESSINNER:  return nv50_ir::SV_TESS_INNER;
   case TGSI_SEMANTIC_VERTICESIN: return nv50_ir::SV_VERTEX_COUNT;
   case TGSI_SEMANTIC_HELPER_INVOCATION: return nv50_ir::SV_THREAD_KILL;
   case TGSI_SEMANTIC_BASEVERTEX: return nv50_ir::SV_BASEVERTEX;
   case TGSI_SEMANTIC_BASEINSTANCE: return nv50_ir::SV_BASEINSTANCE;
   case TGSI_SEMANTIC_DRAWID:     return nv50_ir::SV_DRAWID;
   case TGSI_SEMANTIC_WORK_DIM:   return nv50_ir::SV_WORK_DIM;
   case TGSI_SEMANTIC_SUBGROUP_INVOCATION: return nv50_ir::SV_LANEID;
   case TGSI_SEMANTIC_SUBGROUP_EQ_MASK: return nv50_ir::SV_LANEMASK_EQ;
   case TGSI_SEMANTIC_SUBGROUP_LT_MASK: return nv50_ir::SV_LANEMASK_LT;
   case TGSI_SEMANTIC_SUBGROUP_LE_MASK: return nv50_ir::SV_LANEMASK_LE;
   case TGSI_SEMANTIC_SUBGROUP_GT_MASK: return nv50_ir::SV_LANEMASK_GT;
   case TGSI_SEMANTIC_SUBGROUP_GE_MASK: return nv50_ir::SV_LANEMASK_GE;
   default:
      assert(0);
      return nv50_ir::SV_CLOCK;
   }
}

#define NV50_IR_TEX_TARG_CASE(a, b) \
   case TGSI_TEXTURE_##a: return nv50_ir::TEX_TARGET_##b;

static nv50_ir::TexTarget translateTexture(uint tex)
{
   switch (tex) {
   NV50_IR_TEX_TARG_CASE(1D, 1D);
   NV50_IR_TEX_TARG_CASE(2D, 2D);
   NV50_IR_TEX_TARG_CASE(2D_MSAA, 2D_MS);
   NV50_IR_TEX_TARG_CASE(3D, 3D);
   NV50_IR_TEX_TARG_CASE(CUBE, CUBE);
   NV50_IR_TEX_TARG_CASE(RECT, RECT);
   NV50_IR_TEX_TARG_CASE(1D_ARRAY, 1D_ARRAY);
   NV50_IR_TEX_TARG_CASE(2D_ARRAY, 2D_ARRAY);
   NV50_IR_TEX_TARG_CASE(2D_ARRAY_MSAA, 2D_MS_ARRAY);
   NV50_IR_TEX_TARG_CASE(CUBE_ARRAY, CUBE_ARRAY);
   NV50_IR_TEX_TARG_CASE(SHADOW1D, 1D_SHADOW);
   NV50_IR_TEX_TARG_CASE(SHADOW2D, 2D_SHADOW);
   NV50_IR_TEX_TARG_CASE(SHADOWCUBE, CUBE_SHADOW);
   NV50_IR_TEX_TARG_CASE(SHADOWRECT, RECT_SHADOW);
   NV50_IR_TEX_TARG_CASE(SHADOW1D_ARRAY, 1D_ARRAY_SHADOW);
   NV50_IR_TEX_TARG_CASE(SHADOW2D_ARRAY, 2D_ARRAY_SHADOW);
   NV50_IR_TEX_TARG_CASE(SHADOWCUBE_ARRAY, CUBE_ARRAY_SHADOW);
   NV50_IR_TEX_TARG_CASE(BUFFER, BUFFER);

   case TGSI_TEXTURE_UNKNOWN:
   default:
      assert(!"invalid texture target");
      return nv50_ir::TEX_TARGET_2D;
   }
}

static nv50_ir::CacheMode translateCacheMode(uint qualifier)
{
   if (qualifier & TGSI_MEMORY_VOLATILE)
      return nv50_ir::CACHE_CV;
   if (qualifier & TGSI_MEMORY_COHERENT)
      return nv50_ir::CACHE_CG;
   return nv50_ir::CACHE_CA;
}

static nv50_ir::ImgFormat translateImgFormat(uint format)
{

#define FMT_CASE(a, b) \
  case PIPE_FORMAT_ ## a: return nv50_ir::FMT_ ## b

   switch (format) {
   FMT_CASE(NONE, NONE);

   FMT_CASE(R32G32B32A32_FLOAT, RGBA32F);
   FMT_CASE(R16G16B16A16_FLOAT, RGBA16F);
   FMT_CASE(R32G32_FLOAT, RG32F);
   FMT_CASE(R16G16_FLOAT, RG16F);
   FMT_CASE(R11G11B10_FLOAT, R11G11B10F);
   FMT_CASE(R32_FLOAT, R32F);
   FMT_CASE(R16_FLOAT, R16F);

   FMT_CASE(R32G32B32A32_UINT, RGBA32UI);
   FMT_CASE(R16G16B16A16_UINT, RGBA16UI);
   FMT_CASE(R10G10B10A2_UINT, RGB10A2UI);
   FMT_CASE(R8G8B8A8_UINT, RGBA8UI);
   FMT_CASE(R32G32_UINT, RG32UI);
   FMT_CASE(R16G16_UINT, RG16UI);
   FMT_CASE(R8G8_UINT, RG8UI);
   FMT_CASE(R32_UINT, R32UI);
   FMT_CASE(R16_UINT, R16UI);
   FMT_CASE(R8_UINT, R8UI);

   FMT_CASE(R32G32B32A32_SINT, RGBA32I);
   FMT_CASE(R16G16B16A16_SINT, RGBA16I);
   FMT_CASE(R8G8B8A8_SINT, RGBA8I);
   FMT_CASE(R32G32_SINT, RG32I);
   FMT_CASE(R16G16_SINT, RG16I);
   FMT_CASE(R8G8_SINT, RG8I);
   FMT_CASE(R32_SINT, R32I);
   FMT_CASE(R16_SINT, R16I);
   FMT_CASE(R8_SINT, R8I);

   FMT_CASE(R16G16B16A16_UNORM, RGBA16);
   FMT_CASE(R10G10B10A2_UNORM, RGB10A2);
   FMT_CASE(R8G8B8A8_UNORM, RGBA8);
   FMT_CASE(R16G16_UNORM, RG16);
   FMT_CASE(R8G8_UNORM, RG8);
   FMT_CASE(R16_UNORM, R16);
   FMT_CASE(R8_UNORM, R8);

   FMT_CASE(R16G16B16A16_SNORM, RGBA16_SNORM);
   FMT_CASE(R8G8B8A8_SNORM, RGBA8_SNORM);
   FMT_CASE(R16G16_SNORM, RG16_SNORM);
   FMT_CASE(R8G8_SNORM, RG8_SNORM);
   FMT_CASE(R16_SNORM, R16_SNORM);
   FMT_CASE(R8_SNORM, R8_SNORM);

   FMT_CASE(B8G8R8A8_UNORM, BGRA8);
   }

   assert(!"Unexpected format");
   return nv50_ir::FMT_NONE;
}

nv50_ir::DataType Instruction::inferSrcType() const
{
   switch (getOpcode()) {
   case TGSI_OPCODE_UIF:
   case TGSI_OPCODE_AND:
   case TGSI_OPCODE_OR:
   case TGSI_OPCODE_XOR:
   case TGSI_OPCODE_NOT:
   case TGSI_OPCODE_SHL:
   case TGSI_OPCODE_U2F:
   case TGSI_OPCODE_U2D:
   case TGSI_OPCODE_U2I64:
   case TGSI_OPCODE_UADD:
   case TGSI_OPCODE_UDIV:
   case TGSI_OPCODE_UMOD:
   case TGSI_OPCODE_UMAD:
   case TGSI_OPCODE_UMUL:
   case TGSI_OPCODE_UMUL_HI:
   case TGSI_OPCODE_UMAX:
   case TGSI_OPCODE_UMIN:
   case TGSI_OPCODE_USEQ:
   case TGSI_OPCODE_USGE:
   case TGSI_OPCODE_USLT:
   case TGSI_OPCODE_USNE:
   case TGSI_OPCODE_USHR:
   case TGSI_OPCODE_ATOMUADD:
   case TGSI_OPCODE_ATOMXCHG:
   case TGSI_OPCODE_ATOMCAS:
   case TGSI_OPCODE_ATOMAND:
   case TGSI_OPCODE_ATOMOR:
   case TGSI_OPCODE_ATOMXOR:
   case TGSI_OPCODE_ATOMUMIN:
   case TGSI_OPCODE_ATOMUMAX:
   case TGSI_OPCODE_UBFE:
   case TGSI_OPCODE_UMSB:
   case TGSI_OPCODE_UP2H:
   case TGSI_OPCODE_VOTE_ALL:
   case TGSI_OPCODE_VOTE_ANY:
   case TGSI_OPCODE_VOTE_EQ:
      return nv50_ir::TYPE_U32;
   case TGSI_OPCODE_I2F:
   case TGSI_OPCODE_I2D:
   case TGSI_OPCODE_I2I64:
   case TGSI_OPCODE_IDIV:
   case TGSI_OPCODE_IMUL_HI:
   case TGSI_OPCODE_IMAX:
   case TGSI_OPCODE_IMIN:
   case TGSI_OPCODE_IABS:
   case TGSI_OPCODE_INEG:
   case TGSI_OPCODE_ISGE:
   case TGSI_OPCODE_ISHR:
   case TGSI_OPCODE_ISLT:
   case TGSI_OPCODE_ISSG:
   case TGSI_OPCODE_MOD:
   case TGSI_OPCODE_UARL:
   case TGSI_OPCODE_ATOMIMIN:
   case TGSI_OPCODE_ATOMIMAX:
   case TGSI_OPCODE_IBFE:
   case TGSI_OPCODE_IMSB:
      return nv50_ir::TYPE_S32;
   case TGSI_OPCODE_D2F:
   case TGSI_OPCODE_D2I:
   case TGSI_OPCODE_D2U:
   case TGSI_OPCODE_D2I64:
   case TGSI_OPCODE_D2U64:
   case TGSI_OPCODE_DABS:
   case TGSI_OPCODE_DNEG:
   case TGSI_OPCODE_DADD:
   case TGSI_OPCODE_DMUL:
   case TGSI_OPCODE_DDIV:
   case TGSI_OPCODE_DMAX:
   case TGSI_OPCODE_DMIN:
   case TGSI_OPCODE_DSLT:
   case TGSI_OPCODE_DSGE:
   case TGSI_OPCODE_DSEQ:
   case TGSI_OPCODE_DSNE:
   case TGSI_OPCODE_DRCP:
   case TGSI_OPCODE_DSQRT:
   case TGSI_OPCODE_DMAD:
   case TGSI_OPCODE_DFMA:
   case TGSI_OPCODE_DFRAC:
   case TGSI_OPCODE_DRSQ:
   case TGSI_OPCODE_DTRUNC:
   case TGSI_OPCODE_DCEIL:
   case TGSI_OPCODE_DFLR:
   case TGSI_OPCODE_DROUND:
      return nv50_ir::TYPE_F64;
   case TGSI_OPCODE_U64SEQ:
   case TGSI_OPCODE_U64SNE:
   case TGSI_OPCODE_U64SLT:
   case TGSI_OPCODE_U64SGE:
   case TGSI_OPCODE_U64MIN:
   case TGSI_OPCODE_U64MAX:
   case TGSI_OPCODE_U64ADD:
   case TGSI_OPCODE_U64MUL:
   case TGSI_OPCODE_U64SHL:
   case TGSI_OPCODE_U64SHR:
   case TGSI_OPCODE_U64DIV:
   case TGSI_OPCODE_U64MOD:
   case TGSI_OPCODE_U642F:
   case TGSI_OPCODE_U642D:
      return nv50_ir::TYPE_U64;
   case TGSI_OPCODE_I64ABS:
   case TGSI_OPCODE_I64SSG:
   case TGSI_OPCODE_I64NEG:
   case TGSI_OPCODE_I64SLT:
   case TGSI_OPCODE_I64SGE:
   case TGSI_OPCODE_I64MIN:
   case TGSI_OPCODE_I64MAX:
   case TGSI_OPCODE_I64SHR:
   case TGSI_OPCODE_I64DIV:
   case TGSI_OPCODE_I64MOD:
   case TGSI_OPCODE_I642F:
   case TGSI_OPCODE_I642D:
      return nv50_ir::TYPE_S64;
   default:
      return nv50_ir::TYPE_F32;
   }
}

nv50_ir::DataType Instruction::inferDstType() const
{
   switch (getOpcode()) {
   case TGSI_OPCODE_D2U:
   case TGSI_OPCODE_F2U: return nv50_ir::TYPE_U32;
   case TGSI_OPCODE_D2I:
   case TGSI_OPCODE_F2I: return nv50_ir::TYPE_S32;
   case TGSI_OPCODE_FSEQ:
   case TGSI_OPCODE_FSGE:
   case TGSI_OPCODE_FSLT:
   case TGSI_OPCODE_FSNE:
   case TGSI_OPCODE_DSEQ:
   case TGSI_OPCODE_DSGE:
   case TGSI_OPCODE_DSLT:
   case TGSI_OPCODE_DSNE:
   case TGSI_OPCODE_I64SLT:
   case TGSI_OPCODE_I64SGE:
   case TGSI_OPCODE_U64SEQ:
   case TGSI_OPCODE_U64SNE:
   case TGSI_OPCODE_U64SLT:
   case TGSI_OPCODE_U64SGE:
   case TGSI_OPCODE_PK2H:
      return nv50_ir::TYPE_U32;
   case TGSI_OPCODE_I2F:
   case TGSI_OPCODE_U2F:
   case TGSI_OPCODE_D2F:
   case TGSI_OPCODE_I642F:
   case TGSI_OPCODE_U642F:
   case TGSI_OPCODE_UP2H:
      return nv50_ir::TYPE_F32;
   case TGSI_OPCODE_I2D:
   case TGSI_OPCODE_U2D:
   case TGSI_OPCODE_F2D:
   case TGSI_OPCODE_I642D:
   case TGSI_OPCODE_U642D:
      return nv50_ir::TYPE_F64;
   case TGSI_OPCODE_I2I64:
   case TGSI_OPCODE_U2I64:
   case TGSI_OPCODE_F2I64:
   case TGSI_OPCODE_D2I64:
      return nv50_ir::TYPE_S64;
   case TGSI_OPCODE_F2U64:
   case TGSI_OPCODE_D2U64:
      return nv50_ir::TYPE_U64;
   default:
      return inferSrcType();
   }
}

nv50_ir::CondCode Instruction::getSetCond() const
{
   using namespace nv50_ir;

   switch (getOpcode()) {
   case TGSI_OPCODE_SLT:
   case TGSI_OPCODE_ISLT:
   case TGSI_OPCODE_USLT:
   case TGSI_OPCODE_FSLT:
   case TGSI_OPCODE_DSLT:
   case TGSI_OPCODE_I64SLT:
   case TGSI_OPCODE_U64SLT:
      return CC_LT;
   case TGSI_OPCODE_SLE:
      return CC_LE;
   case TGSI_OPCODE_SGE:
   case TGSI_OPCODE_ISGE:
   case TGSI_OPCODE_USGE:
   case TGSI_OPCODE_FSGE:
   case TGSI_OPCODE_DSGE:
   case TGSI_OPCODE_I64SGE:
   case TGSI_OPCODE_U64SGE:
      return CC_GE;
   case TGSI_OPCODE_SGT:
      return CC_GT;
   case TGSI_OPCODE_SEQ:
   case TGSI_OPCODE_USEQ:
   case TGSI_OPCODE_FSEQ:
   case TGSI_OPCODE_DSEQ:
   case TGSI_OPCODE_U64SEQ:
      return CC_EQ;
   case TGSI_OPCODE_SNE:
   case TGSI_OPCODE_FSNE:
   case TGSI_OPCODE_DSNE:
   case TGSI_OPCODE_U64SNE:
      return CC_NEU;
   case TGSI_OPCODE_USNE:
      return CC_NE;
   default:
      return CC_ALWAYS;
   }
}

#define NV50_IR_OPCODE_CASE(a, b) case TGSI_OPCODE_##a: return nv50_ir::OP_##b

static nv50_ir::operation translateOpcode(uint opcode)
{
   switch (opcode) {
   NV50_IR_OPCODE_CASE(ARL, SHL);
   NV50_IR_OPCODE_CASE(MOV, MOV);

   NV50_IR_OPCODE_CASE(RCP, RCP);
   NV50_IR_OPCODE_CASE(RSQ, RSQ);
   NV50_IR_OPCODE_CASE(SQRT, SQRT);

   NV50_IR_OPCODE_CASE(MUL, MUL);
   NV50_IR_OPCODE_CASE(ADD, ADD);

   NV50_IR_OPCODE_CASE(MIN, MIN);
   NV50_IR_OPCODE_CASE(MAX, MAX);
   NV50_IR_OPCODE_CASE(SLT, SET);
   NV50_IR_OPCODE_CASE(SGE, SET);
   NV50_IR_OPCODE_CASE(MAD, MAD);
   NV50_IR_OPCODE_CASE(FMA, FMA);

   NV50_IR_OPCODE_CASE(FLR, FLOOR);
   NV50_IR_OPCODE_CASE(ROUND, CVT);
   NV50_IR_OPCODE_CASE(EX2, EX2);
   NV50_IR_OPCODE_CASE(LG2, LG2);
   NV50_IR_OPCODE_CASE(POW, POW);

   NV50_IR_OPCODE_CASE(COS, COS);
   NV50_IR_OPCODE_CASE(DDX, DFDX);
   NV50_IR_OPCODE_CASE(DDX_FINE, DFDX);
   NV50_IR_OPCODE_CASE(DDY, DFDY);
   NV50_IR_OPCODE_CASE(DDY_FINE, DFDY);
   NV50_IR_OPCODE_CASE(KILL, DISCARD);

   NV50_IR_OPCODE_CASE(SEQ, SET);
   NV50_IR_OPCODE_CASE(SGT, SET);
   NV50_IR_OPCODE_CASE(SIN, SIN);
   NV50_IR_OPCODE_CASE(SLE, SET);
   NV50_IR_OPCODE_CASE(SNE, SET);
   NV50_IR_OPCODE_CASE(TEX, TEX);
   NV50_IR_OPCODE_CASE(TXD, TXD);
   NV50_IR_OPCODE_CASE(TXP, TEX);

   NV50_IR_OPCODE_CASE(CAL, CALL);
   NV50_IR_OPCODE_CASE(RET, RET);
   NV50_IR_OPCODE_CASE(CMP, SLCT);

   NV50_IR_OPCODE_CASE(TXB, TXB);

   NV50_IR_OPCODE_CASE(DIV, DIV);

   NV50_IR_OPCODE_CASE(TXL, TXL);
   NV50_IR_OPCODE_CASE(TEX_LZ, TXL);

   NV50_IR_OPCODE_CASE(CEIL, CEIL);
   NV50_IR_OPCODE_CASE(I2F, CVT);
   NV50_IR_OPCODE_CASE(NOT, NOT);
   NV50_IR_OPCODE_CASE(TRUNC, TRUNC);
   NV50_IR_OPCODE_CASE(SHL, SHL);

   NV50_IR_OPCODE_CASE(AND, AND);
   NV50_IR_OPCODE_CASE(OR, OR);
   NV50_IR_OPCODE_CASE(MOD, MOD);
   NV50_IR_OPCODE_CASE(XOR, XOR);
   NV50_IR_OPCODE_CASE(TXF, TXF);
   NV50_IR_OPCODE_CASE(TXF_LZ, TXF);
   NV50_IR_OPCODE_CASE(TXQ, TXQ);
   NV50_IR_OPCODE_CASE(TXQS, TXQ);
   NV50_IR_OPCODE_CASE(TG4, TXG);
   NV50_IR_OPCODE_CASE(LODQ, TXLQ);

   NV50_IR_OPCODE_CASE(EMIT, EMIT);
   NV50_IR_OPCODE_CASE(ENDPRIM, RESTART);

   NV50_IR_OPCODE_CASE(KILL_IF, DISCARD);

   NV50_IR_OPCODE_CASE(F2I, CVT);
   NV50_IR_OPCODE_CASE(FSEQ, SET);
   NV50_IR_OPCODE_CASE(FSGE, SET);
   NV50_IR_OPCODE_CASE(FSLT, SET);
   NV50_IR_OPCODE_CASE(FSNE, SET);
   NV50_IR_OPCODE_CASE(IDIV, DIV);
   NV50_IR_OPCODE_CASE(IMAX, MAX);
   NV50_IR_OPCODE_CASE(IMIN, MIN);
   NV50_IR_OPCODE_CASE(IABS, ABS);
   NV50_IR_OPCODE_CASE(INEG, NEG);
   NV50_IR_OPCODE_CASE(ISGE, SET);
   NV50_IR_OPCODE_CASE(ISHR, SHR);
   NV50_IR_OPCODE_CASE(ISLT, SET);
   NV50_IR_OPCODE_CASE(F2U, CVT);
   NV50_IR_OPCODE_CASE(U2F, CVT);
   NV50_IR_OPCODE_CASE(UADD, ADD);
   NV50_IR_OPCODE_CASE(UDIV, DIV);
   NV50_IR_OPCODE_CASE(UMAD, MAD);
   NV50_IR_OPCODE_CASE(UMAX, MAX);
   NV50_IR_OPCODE_CASE(UMIN, MIN);
   NV50_IR_OPCODE_CASE(UMOD, MOD);
   NV50_IR_OPCODE_CASE(UMUL, MUL);
   NV50_IR_OPCODE_CASE(USEQ, SET);
   NV50_IR_OPCODE_CASE(USGE, SET);
   NV50_IR_OPCODE_CASE(USHR, SHR);
   NV50_IR_OPCODE_CASE(USLT, SET);
   NV50_IR_OPCODE_CASE(USNE, SET);

   NV50_IR_OPCODE_CASE(DABS, ABS);
   NV50_IR_OPCODE_CASE(DNEG, NEG);
   NV50_IR_OPCODE_CASE(DADD, ADD);
   NV50_IR_OPCODE_CASE(DMUL, MUL);
   NV50_IR_OPCODE_CASE(DDIV, DIV);
   NV50_IR_OPCODE_CASE(DMAX, MAX);
   NV50_IR_OPCODE_CASE(DMIN, MIN);
   NV50_IR_OPCODE_CASE(DSLT, SET);
   NV50_IR_OPCODE_CASE(DSGE, SET);
   NV50_IR_OPCODE_CASE(DSEQ, SET);
   NV50_IR_OPCODE_CASE(DSNE, SET);
   NV50_IR_OPCODE_CASE(DRCP, RCP);
   NV50_IR_OPCODE_CASE(DSQRT, SQRT);
   NV50_IR_OPCODE_CASE(DMAD, MAD);
   NV50_IR_OPCODE_CASE(DFMA, FMA);
   NV50_IR_OPCODE_CASE(D2I, CVT);
   NV50_IR_OPCODE_CASE(D2U, CVT);
   NV50_IR_OPCODE_CASE(I2D, CVT);
   NV50_IR_OPCODE_CASE(U2D, CVT);
   NV50_IR_OPCODE_CASE(DRSQ, RSQ);
   NV50_IR_OPCODE_CASE(DTRUNC, TRUNC);
   NV50_IR_OPCODE_CASE(DCEIL, CEIL);
   NV50_IR_OPCODE_CASE(DFLR, FLOOR);
   NV50_IR_OPCODE_CASE(DROUND, CVT);

   NV50_IR_OPCODE_CASE(U64SEQ, SET);
   NV50_IR_OPCODE_CASE(U64SNE, SET);
   NV50_IR_OPCODE_CASE(U64SLT, SET);
   NV50_IR_OPCODE_CASE(U64SGE, SET);
   NV50_IR_OPCODE_CASE(I64SLT, SET);
   NV50_IR_OPCODE_CASE(I64SGE, SET);
   NV50_IR_OPCODE_CASE(I2I64, CVT);
   NV50_IR_OPCODE_CASE(U2I64, CVT);
   NV50_IR_OPCODE_CASE(F2I64, CVT);
   NV50_IR_OPCODE_CASE(F2U64, CVT);
   NV50_IR_OPCODE_CASE(D2I64, CVT);
   NV50_IR_OPCODE_CASE(D2U64, CVT);
   NV50_IR_OPCODE_CASE(I642F, CVT);
   NV50_IR_OPCODE_CASE(U642F, CVT);
   NV50_IR_OPCODE_CASE(I642D, CVT);
   NV50_IR_OPCODE_CASE(U642D, CVT);

   NV50_IR_OPCODE_CASE(I64MIN, MIN);
   NV50_IR_OPCODE_CASE(U64MIN, MIN);
   NV50_IR_OPCODE_CASE(I64MAX, MAX);
   NV50_IR_OPCODE_CASE(U64MAX, MAX);
   NV50_IR_OPCODE_CASE(I64ABS, ABS);
   NV50_IR_OPCODE_CASE(I64NEG, NEG);
   NV50_IR_OPCODE_CASE(U64ADD, ADD);
   NV50_IR_OPCODE_CASE(U64MUL, MUL);
   NV50_IR_OPCODE_CASE(U64SHL, SHL);
   NV50_IR_OPCODE_CASE(I64SHR, SHR);
   NV50_IR_OPCODE_CASE(U64SHR, SHR);

   NV50_IR_OPCODE_CASE(IMUL_HI, MUL);
   NV50_IR_OPCODE_CASE(UMUL_HI, MUL);

   NV50_IR_OPCODE_CASE(SAMPLE, TEX);
   NV50_IR_OPCODE_CASE(SAMPLE_B, TXB);
   NV50_IR_OPCODE_CASE(SAMPLE_C, TEX);
   NV50_IR_OPCODE_CASE(SAMPLE_C_LZ, TEX);
   NV50_IR_OPCODE_CASE(SAMPLE_D, TXD);
   NV50_IR_OPCODE_CASE(SAMPLE_L, TXL);
   NV50_IR_OPCODE_CASE(SAMPLE_I, TXF);
   NV50_IR_OPCODE_CASE(SAMPLE_I_MS, TXF);
   NV50_IR_OPCODE_CASE(GATHER4, TXG);
   NV50_IR_OPCODE_CASE(SVIEWINFO, TXQ);

   NV50_IR_OPCODE_CASE(ATOMUADD, ATOM);
   NV50_IR_OPCODE_CASE(ATOMXCHG, ATOM);
   NV50_IR_OPCODE_CASE(ATOMCAS, ATOM);
   NV50_IR_OPCODE_CASE(ATOMAND, ATOM);
   NV50_IR_OPCODE_CASE(ATOMOR, ATOM);
   NV50_IR_OPCODE_CASE(ATOMXOR, ATOM);
   NV50_IR_OPCODE_CASE(ATOMUMIN, ATOM);
   NV50_IR_OPCODE_CASE(ATOMUMAX, ATOM);
   NV50_IR_OPCODE_CASE(ATOMIMIN, ATOM);
   NV50_IR_OPCODE_CASE(ATOMIMAX, ATOM);
   NV50_IR_OPCODE_CASE(ATOMFADD, ATOM);

   NV50_IR_OPCODE_CASE(TEX2, TEX);
   NV50_IR_OPCODE_CASE(TXB2, TXB);
   NV50_IR_OPCODE_CASE(TXL2, TXL);

   NV50_IR_OPCODE_CASE(IBFE, EXTBF);
   NV50_IR_OPCODE_CASE(UBFE, EXTBF);
   NV50_IR_OPCODE_CASE(BFI, INSBF);
   NV50_IR_OPCODE_CASE(BREV, EXTBF);
   NV50_IR_OPCODE_CASE(POPC, POPCNT);
   NV50_IR_OPCODE_CASE(LSB, BFIND);
   NV50_IR_OPCODE_CASE(IMSB, BFIND);
   NV50_IR_OPCODE_CASE(UMSB, BFIND);

   NV50_IR_OPCODE_CASE(VOTE_ALL, VOTE);
   NV50_IR_OPCODE_CASE(VOTE_ANY, VOTE);
   NV50_IR_OPCODE_CASE(VOTE_EQ, VOTE);

   NV50_IR_OPCODE_CASE(BALLOT, VOTE);
   NV50_IR_OPCODE_CASE(READ_INVOC, SHFL);
   NV50_IR_OPCODE_CASE(READ_FIRST, SHFL);

   NV50_IR_OPCODE_CASE(END, EXIT);

   default:
      return nv50_ir::OP_NOP;
   }
}

static uint16_t opcodeToSubOp(uint opcode)
{
   switch (opcode) {
   case TGSI_OPCODE_ATOMUADD: return NV50_IR_SUBOP_ATOM_ADD;
   case TGSI_OPCODE_ATOMXCHG: return NV50_IR_SUBOP_ATOM_EXCH;
   case TGSI_OPCODE_ATOMCAS:  return NV50_IR_SUBOP_ATOM_CAS;
   case TGSI_OPCODE_ATOMAND:  return NV50_IR_SUBOP_ATOM_AND;
   case TGSI_OPCODE_ATOMOR:   return NV50_IR_SUBOP_ATOM_OR;
   case TGSI_OPCODE_ATOMXOR:  return NV50_IR_SUBOP_ATOM_XOR;
   case TGSI_OPCODE_ATOMUMIN: return NV50_IR_SUBOP_ATOM_MIN;
   case TGSI_OPCODE_ATOMIMIN: return NV50_IR_SUBOP_ATOM_MIN;
   case TGSI_OPCODE_ATOMUMAX: return NV50_IR_SUBOP_ATOM_MAX;
   case TGSI_OPCODE_ATOMIMAX: return NV50_IR_SUBOP_ATOM_MAX;
   case TGSI_OPCODE_ATOMFADD: return NV50_IR_SUBOP_ATOM_ADD;
   case TGSI_OPCODE_IMUL_HI:
   case TGSI_OPCODE_UMUL_HI:
      return NV50_IR_SUBOP_MUL_HIGH;
   case TGSI_OPCODE_VOTE_ALL: return NV50_IR_SUBOP_VOTE_ALL;
   case TGSI_OPCODE_VOTE_ANY: return NV50_IR_SUBOP_VOTE_ANY;
   case TGSI_OPCODE_VOTE_EQ: return NV50_IR_SUBOP_VOTE_UNI;
   default:
      return 0;
   }
}

bool Instruction::checkDstSrcAliasing() const
{
   if (insn->Dst[0].Register.Indirect) // no danger if indirect, using memory
      return false;

   for (int s = 0; s < TGSI_FULL_MAX_SRC_REGISTERS; ++s) {
      if (insn->Src[s].Register.File == TGSI_FILE_NULL)
         break;
      if (insn->Src[s].Register.File == insn->Dst[0].Register.File &&
          insn->Src[s].Register.Index == insn->Dst[0].Register.Index)
         return true;
   }
   return false;
}

class Source
{
public:
   Source(struct nv50_ir_prog_info *);
   ~Source();

public:
   bool scanSource();
   unsigned fileSize(unsigned file) const { return scan.file_max[file] + 1; }

public:
   struct tgsi_shader_info scan;
   struct tgsi_full_instruction *insns;
   const struct tgsi_token *tokens;
   struct nv50_ir_prog_info *info;

   nv50_ir::DynArray tempArrays;
   nv50_ir::DynArray immdArrays;

   typedef nv50_ir::BuildUtil::Location Location;
   // these registers are per-subroutine, cannot be used for parameter passing
   std::set<Location> locals;

   std::set<int> indirectTempArrays;
   std::map<int, int> indirectTempOffsets;
   std::map<int, std::pair<int, int> > tempArrayInfo;
   std::vector<int> tempArrayId;

   int clipVertexOutput;

   struct TextureView {
      uint8_t target; // TGSI_TEXTURE_*
   };
   std::vector<TextureView> textureViews;

   /*
   struct Resource {
      uint8_t target; // TGSI_TEXTURE_*
      bool raw;
      uint8_t slot; // $surface index
   };
   std::vector<Resource> resources;
   */

   struct MemoryFile {
      uint8_t mem_type; // TGSI_MEMORY_TYPE_*
   };
   std::vector<MemoryFile> memoryFiles;

   std::vector<bool> bufferAtomics;

private:
   int inferSysValDirection(unsigned sn) const;
   bool scanDeclaration(const struct tgsi_full_declaration *);
   bool scanInstruction(const struct tgsi_full_instruction *);
   void scanInstructionSrc(const Instruction& insn,
                           const Instruction::SrcRegister& src,
                           unsigned mask);
   void scanProperty(const struct tgsi_full_property *);
   void scanImmediate(const struct tgsi_full_immediate *);

   inline bool isEdgeFlagPassthrough(const Instruction&) const;
};

Source::Source(struct nv50_ir_prog_info *prog) : info(prog)
{
   tokens = (const struct tgsi_token *)info->bin.source;

   if (prog->dbgFlags & NV50_IR_DEBUG_BASIC)
      tgsi_dump(tokens, 0);
}

Source::~Source()
{
   if (insns)
      FREE(insns);

   if (info->immd.data)
      FREE(info->immd.data);
   if (info->immd.type)
      FREE(info->immd.type);
}

bool Source::scanSource()
{
   unsigned insnCount = 0;
   struct tgsi_parse_context parse;

   tgsi_scan_shader(tokens, &scan);

   insns = (struct tgsi_full_instruction *)MALLOC(scan.num_instructions *
                                                  sizeof(insns[0]));
   if (!insns)
      return false;

   clipVertexOutput = -1;

   textureViews.resize(scan.file_max[TGSI_FILE_SAMPLER_VIEW] + 1);
   //resources.resize(scan.file_max[TGSI_FILE_RESOURCE] + 1);
   tempArrayId.resize(scan.file_max[TGSI_FILE_TEMPORARY] + 1);
   memoryFiles.resize(scan.file_max[TGSI_FILE_MEMORY] + 1);
   bufferAtomics.resize(scan.file_max[TGSI_FILE_BUFFER] + 1);

   info->immd.bufSize = 0;

   info->numInputs = scan.file_max[TGSI_FILE_INPUT] + 1;
   info->numOutputs = scan.file_max[TGSI_FILE_OUTPUT] + 1;
   info->numSysVals = scan.file_max[TGSI_FILE_SYSTEM_VALUE] + 1;

   if (info->type == PIPE_SHADER_FRAGMENT) {
      info->prop.fp.writesDepth = scan.writes_z;
      info->prop.fp.usesDiscard = scan.uses_kill || info->io.alphaRefBase;
   } else
   if (info->type == PIPE_SHADER_GEOMETRY) {
      info->prop.gp.instanceCount = 1; // default value
   }

   info->io.viewportId = -1;

   info->immd.data = (uint32_t *)MALLOC(scan.immediate_count * 16);
   info->immd.type = (ubyte *)MALLOC(scan.immediate_count * sizeof(ubyte));

   tgsi_parse_init(&parse, tokens);
   while (!tgsi_parse_end_of_tokens(&parse)) {
      tgsi_parse_token(&parse);

      switch (parse.FullToken.Token.Type) {
      case TGSI_TOKEN_TYPE_IMMEDIATE:
         scanImmediate(&parse.FullToken.FullImmediate);
         break;
      case TGSI_TOKEN_TYPE_DECLARATION:
         scanDeclaration(&parse.FullToken.FullDeclaration);
         break;
      case TGSI_TOKEN_TYPE_INSTRUCTION:
         insns[insnCount++] = parse.FullToken.FullInstruction;
         scanInstruction(&parse.FullToken.FullInstruction);
         break;
      case TGSI_TOKEN_TYPE_PROPERTY:
         scanProperty(&parse.FullToken.FullProperty);
         break;
      default:
         INFO("unknown TGSI token type: %d\n", parse.FullToken.Token.Type);
         break;
      }
   }
   tgsi_parse_free(&parse);

   if (indirectTempArrays.size()) {
      int tempBase = 0;
      for (std::set<int>::const_iterator it = indirectTempArrays.begin();
           it != indirectTempArrays.end(); ++it) {
         std::pair<int, int>& info = tempArrayInfo[*it];
         indirectTempOffsets.insert(std::make_pair(*it, tempBase - info.first));
         tempBase += info.second;
      }
      info->bin.tlsSpace += tempBase * 16;
   }

   if (info->io.genUserClip > 0) {
      info->io.clipDistances = info->io.genUserClip;

      const unsigned int nOut = (info->io.genUserClip + 3) / 4;

      for (unsigned int n = 0; n < nOut; ++n) {
         unsigned int i = info->numOutputs++;
         info->out[i].id = i;
         info->out[i].sn = TGSI_SEMANTIC_CLIPDIST;
         info->out[i].si = n;
         info->out[i].mask = ((1 << info->io.clipDistances) - 1) >> (n * 4);
      }
   }

   return info->assignSlots(info) == 0;
}

void Source::scanProperty(const struct tgsi_full_property *prop)
{
   switch (prop->Property.PropertyName) {
   case TGSI_PROPERTY_GS_OUTPUT_PRIM:
      info->prop.gp.outputPrim = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_GS_INPUT_PRIM:
      info->prop.gp.inputPrim = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_GS_MAX_OUTPUT_VERTICES:
      info->prop.gp.maxVertices = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_GS_INVOCATIONS:
      info->prop.gp.instanceCount = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_FS_COLOR0_WRITES_ALL_CBUFS:
      info->prop.fp.separateFragData = true;
      break;
   case TGSI_PROPERTY_FS_COORD_ORIGIN:
   case TGSI_PROPERTY_FS_COORD_PIXEL_CENTER:
      // we don't care
      break;
   case TGSI_PROPERTY_FS_DEPTH_LAYOUT: // fincs-edit (this entire section is new)
      info->prop.fp.hasZcullTestMask = true;
      switch (prop->u[0].Data) {
         default:
         case TGSI_FS_DEPTH_LAYOUT_ANY:
            info->prop.fp.zcullTestMask = 0x11;
            break;
         case TGSI_FS_DEPTH_LAYOUT_GREATER:
            info->prop.fp.zcullTestMask = 0x10;
            break;
         case TGSI_FS_DEPTH_LAYOUT_LESS:
            info->prop.fp.zcullTestMask = 0x01;
            break;
         case TGSI_FS_DEPTH_LAYOUT_UNCHANGED:
            info->prop.fp.zcullTestMask = 0x00;
            break;
      }
      break;
   case TGSI_PROPERTY_VS_PROHIBIT_UCPS:
      info->io.genUserClip = -1;
      break;
   case TGSI_PROPERTY_TCS_VERTICES_OUT:
      info->prop.tp.outputPatchSize = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_TES_PRIM_MODE:
      info->prop.tp.domain = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_TES_SPACING:
      info->prop.tp.partitioning = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_TES_VERTEX_ORDER_CW:
      info->prop.tp.winding = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_TES_POINT_MODE:
      if (prop->u[0].Data)
         info->prop.tp.outputPrim = PIPE_PRIM_POINTS;
      else
         info->prop.tp.outputPrim = PIPE_PRIM_TRIANGLES; /* anything but points */
      break;
   case TGSI_PROPERTY_CS_FIXED_BLOCK_WIDTH:
      info->prop.cp.numThreads[0] = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_CS_FIXED_BLOCK_HEIGHT:
      info->prop.cp.numThreads[1] = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_CS_FIXED_BLOCK_DEPTH:
      info->prop.cp.numThreads[2] = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_NUM_CLIPDIST_ENABLED:
      info->io.clipDistances = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_NUM_CULLDIST_ENABLED:
      info->io.cullDistances = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_NEXT_SHADER:
      /* Do not need to know the next shader stage. */
      break;
   case TGSI_PROPERTY_FS_EARLY_DEPTH_STENCIL:
      info->prop.fp.earlyFragTests = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_FS_POST_DEPTH_COVERAGE:
      info->prop.fp.postDepthCoverage = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_MUL_ZERO_WINS:
      info->io.mul_zero_wins = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_LAYER_VIEWPORT_RELATIVE:
      info->io.layer_viewport_relative = prop->u[0].Data;
      break;
   default:
      INFO("unhandled TGSI property %d\n", prop->Property.PropertyName);
      break;
   }
}

void Source::scanImmediate(const struct tgsi_full_immediate *imm)
{
   const unsigned n = info->immd.count++;

   assert(n < scan.immediate_count);

   for (int c = 0; c < 4; ++c)
      info->immd.data[n * 4 + c] = imm->u[c].Uint;

   info->immd.type[n] = imm->Immediate.DataType;
}

int Source::inferSysValDirection(unsigned sn) const
{
   switch (sn) {
   case TGSI_SEMANTIC_INSTANCEID:
   case TGSI_SEMANTIC_VERTEXID:
      return 1;
   case TGSI_SEMANTIC_LAYER:
#if 0
   case TGSI_SEMANTIC_VIEWPORTINDEX:
      return 0;
#endif
   case TGSI_SEMANTIC_PRIMID:
      return (info->type == PIPE_SHADER_FRAGMENT) ? 1 : 0;
   default:
      return 0;
   }
}

bool Source::scanDeclaration(const struct tgsi_full_declaration *decl)
{
   unsigned i, c;
   unsigned sn = TGSI_SEMANTIC_GENERIC;
   unsigned si = 0;
   const unsigned first = decl->Range.First, last = decl->Range.Last;
   const int arrayId = decl->Array.ArrayID;

   if (decl->Declaration.Semantic) {
      sn = decl->Semantic.Name;
      si = decl->Semantic.Index;
   }

   if (decl->Declaration.Local || decl->Declaration.File == TGSI_FILE_ADDRESS) {
      for (i = first; i <= last; ++i) {
         for (c = 0; c < 4; ++c) {
            locals.insert(
               Location(decl->Declaration.File, decl->Dim.Index2D, i, c));
         }
      }
   }

   switch (decl->Declaration.File) {
   case TGSI_FILE_INPUT:
      if (info->type == PIPE_SHADER_VERTEX) {
         // all vertex attributes are equal
         for (i = first; i <= last; ++i) {
            info->in[i].sn = TGSI_SEMANTIC_GENERIC;
            info->in[i].si = i;
         }
      } else {
         for (i = first; i <= last; ++i, ++si) {
            info->in[i].id = i;
            info->in[i].sn = sn;
            info->in[i].si = si;
            if (info->type == PIPE_SHADER_FRAGMENT) {
               // translate interpolation mode
               switch (decl->Interp.Interpolate) {
               case TGSI_INTERPOLATE_CONSTANT:
                  info->in[i].flat = 1;
                  break;
               case TGSI_INTERPOLATE_COLOR:
                  info->in[i].sc = 1;
                  break;
               case TGSI_INTERPOLATE_LINEAR:
                  info->in[i].linear = 1;
                  break;
               default:
                  break;
               }
               if (decl->Interp.Location)
                  info->in[i].centroid = 1;
            }

            if (sn == TGSI_SEMANTIC_PATCH)
               info->in[i].patch = 1;
            if (sn == TGSI_SEMANTIC_PATCH)
               info->numPatchConstants = MAX2(info->numPatchConstants, si + 1);
         }
      }
      break;
   case TGSI_FILE_OUTPUT:
      for (i = first; i <= last; ++i, ++si) {
         switch (sn) {
         case TGSI_SEMANTIC_POSITION:
            if (info->type == PIPE_SHADER_FRAGMENT)
               info->io.fragDepth = i;
            else
            if (clipVertexOutput < 0)
               clipVertexOutput = i;
            break;
         case TGSI_SEMANTIC_COLOR:
            if (info->type == PIPE_SHADER_FRAGMENT)
               info->prop.fp.numColourResults++;
            break;
         case TGSI_SEMANTIC_EDGEFLAG:
            info->io.edgeFlagOut = i;
            break;
         case TGSI_SEMANTIC_CLIPVERTEX:
            clipVertexOutput = i;
            break;
         case TGSI_SEMANTIC_CLIPDIST:
            info->io.genUserClip = -1;
            break;
         case TGSI_SEMANTIC_SAMPLEMASK:
            info->io.sampleMask = i;
            break;
         case TGSI_SEMANTIC_VIEWPORT_INDEX:
            info->io.viewportId = i;
            break;
         case TGSI_SEMANTIC_PATCH:
            info->numPatchConstants = MAX2(info->numPatchConstants, si + 1);
            /* fallthrough */
         case TGSI_SEMANTIC_TESSOUTER:
         case TGSI_SEMANTIC_TESSINNER:
            info->out[i].patch = 1;
            break;
         default:
            break;
         }
         info->out[i].id = i;
         info->out[i].sn = sn;
         info->out[i].si = si;
      }
      break;
   case TGSI_FILE_SYSTEM_VALUE:
      switch (sn) {
      case TGSI_SEMANTIC_INSTANCEID:
         info->io.instanceId = first;
         break;
      case TGSI_SEMANTIC_VERTEXID:
         info->io.vertexId = first;
         break;
      case TGSI_SEMANTIC_BASEVERTEX:
      case TGSI_SEMANTIC_BASEINSTANCE:
      case TGSI_SEMANTIC_DRAWID:
         info->prop.vp.usesDrawParameters = true;
         break;
      case TGSI_SEMANTIC_SAMPLEID:
      case TGSI_SEMANTIC_SAMPLEPOS:
         info->prop.fp.persampleInvocation = true;
         break;
      case TGSI_SEMANTIC_SAMPLEMASK:
         info->prop.fp.usesSampleMaskIn = true;
         break;
      default:
         break;
      }
      for (i = first; i <= last; ++i, ++si) {
         info->sv[i].sn = sn;
         info->sv[i].si = si;
         info->sv[i].input = inferSysValDirection(sn);

         switch (sn) {
         case TGSI_SEMANTIC_TESSOUTER:
         case TGSI_SEMANTIC_TESSINNER:
            info->sv[i].patch = 1;
            break;
         }
      }
      break;
/*
   case TGSI_FILE_RESOURCE:
      for (i = first; i <= last; ++i) {
         resources[i].target = decl->Resource.Resource;
         resources[i].raw = decl->Resource.Raw;
         resources[i].slot = i;
      }
      break;
*/
   case TGSI_FILE_SAMPLER_VIEW:
      for (i = first; i <= last; ++i)
         textureViews[i].target = decl->SamplerView.Resource;
      break;
   case TGSI_FILE_MEMORY:
      for (i = first; i <= last; ++i)
         memoryFiles[i].mem_type = decl->Declaration.MemType;
      break;
   case TGSI_FILE_NULL:
   case TGSI_FILE_TEMPORARY:
      for (i = first; i <= last; ++i)
         tempArrayId[i] = arrayId;
      if (arrayId)
         tempArrayInfo.insert(std::make_pair(arrayId, std::make_pair(
                                                   first, last - first + 1)));
      break;
   case TGSI_FILE_BUFFER:
      for (i = first; i <= last; ++i)
         bufferAtomics[i] = decl->Declaration.Atomic;
      break;
   case TGSI_FILE_ADDRESS:
   case TGSI_FILE_CONSTANT:
   case TGSI_FILE_IMMEDIATE:
   case TGSI_FILE_SAMPLER:
   case TGSI_FILE_IMAGE:
      break;
   default:
      ERROR("unhandled TGSI_FILE %d\n", decl->Declaration.File);
      return false;
   }
   return true;
}

inline bool Source::isEdgeFlagPassthrough(const Instruction& insn) const
{
   return insn.getOpcode() == TGSI_OPCODE_MOV &&
      insn.getDst(0).getIndex(0) == info->io.edgeFlagOut &&
      insn.getSrc(0).getFile() == TGSI_FILE_INPUT;
}

void Source::scanInstructionSrc(const Instruction& insn,
                                const Instruction::SrcRegister& src,
                                unsigned mask)
{
   if (src.getFile() == TGSI_FILE_TEMPORARY) {
      if (src.isIndirect(0))
         indirectTempArrays.insert(src.getArrayId());
   } else
   if (src.getFile() == TGSI_FILE_OUTPUT) {
      if (src.isIndirect(0)) {
         // We don't know which one is accessed, just mark everything for
         // reading. This is an extremely unlikely occurrence.
         for (unsigned i = 0; i < info->numOutputs; ++i)
            info->out[i].oread = 1;
      } else {
         info->out[src.getIndex(0)].oread = 1;
      }
   }
   if (src.getFile() == TGSI_FILE_SYSTEM_VALUE) {
      if (info->sv[src.getIndex(0)].sn == TGSI_SEMANTIC_SAMPLEPOS)
         info->prop.fp.readsSampleLocations = true;
   }
   if (src.getFile() != TGSI_FILE_INPUT)
      return;

   if (src.isIndirect(0)) {
      for (unsigned i = 0; i < info->numInputs; ++i)
         info->in[i].mask = 0xf;
   } else {
      const int i = src.getIndex(0);
      for (unsigned c = 0; c < 4; ++c) {
         if (!(mask & (1 << c)))
            continue;
         int k = src.getSwizzle(c);
         if (k <= TGSI_SWIZZLE_W)
            info->in[i].mask |= 1 << k;
      }
      switch (info->in[i].sn) {
      case TGSI_SEMANTIC_PSIZE:
      case TGSI_SEMANTIC_PRIMID:
      case TGSI_SEMANTIC_FOG:
         info->in[i].mask &= 0x1;
         break;
      case TGSI_SEMANTIC_PCOORD:
         info->in[i].mask &= 0x3;
         break;
      default:
         break;
      }
   }
}

bool Source::scanInstruction(const struct tgsi_full_instruction *inst)
{
   Instruction insn(inst);

   if (insn.getOpcode() == TGSI_OPCODE_BARRIER)
      info->numBarriers = 1;

   if (insn.getOpcode() == TGSI_OPCODE_FBFETCH)
      info->prop.fp.readsFramebuffer = true;

   if (insn.getOpcode() == TGSI_OPCODE_INTERP_SAMPLE)
      info->prop.fp.readsSampleLocations = true;

   if (insn.dstCount()) {
      Instruction::DstRegister dst = insn.getDst(0);

      if (insn.getOpcode() == TGSI_OPCODE_STORE &&
          dst.getFile() != TGSI_FILE_MEMORY) {
         info->io.globalAccess |= 0x2;

         if (dst.getFile() == TGSI_FILE_INPUT) {
            // TODO: Handle indirect somehow?
            const int i = dst.getIndex(0);
            info->in[i].mask |= 1;
         }
      }

      if (dst.getFile() == TGSI_FILE_OUTPUT) {
         if (dst.isIndirect(0))
            for (unsigned i = 0; i < info->numOutputs; ++i)
               info->out[i].mask = 0xf;
         else
            info->out[dst.getIndex(0)].mask |= dst.getMask();

         if (info->out[dst.getIndex(0)].sn == TGSI_SEMANTIC_PSIZE ||
             info->out[dst.getIndex(0)].sn == TGSI_SEMANTIC_PRIMID ||
             info->out[dst.getIndex(0)].sn == TGSI_SEMANTIC_LAYER ||
             info->out[dst.getIndex(0)].sn == TGSI_SEMANTIC_VIEWPORT_INDEX ||
             info->out[dst.getIndex(0)].sn == TGSI_SEMANTIC_VIEWPORT_MASK ||
             info->out[dst.getIndex(0)].sn == TGSI_SEMANTIC_FOG)
            info->out[dst.getIndex(0)].mask &= 1;

         if (isEdgeFlagPassthrough(insn))
            info->io.edgeFlagIn = insn.getSrc(0).getIndex(0);
      } else
      if (dst.getFile() == TGSI_FILE_TEMPORARY) {
         if (dst.isIndirect(0))
            indirectTempArrays.insert(dst.getArrayId());
      } else
      if (dst.getFile() == TGSI_FILE_BUFFER ||
          dst.getFile() == TGSI_FILE_IMAGE ||
          (dst.getFile() == TGSI_FILE_MEMORY &&
           memoryFiles[dst.getIndex(0)].mem_type == TGSI_MEMORY_TYPE_GLOBAL)) {
         info->io.globalAccess |= 0x2;
      }
   }

   if (insn.srcCount() && (
             insn.getSrc(0).getFile() != TGSI_FILE_MEMORY ||
             memoryFiles[insn.getSrc(0).getIndex(0)].mem_type ==
             TGSI_MEMORY_TYPE_GLOBAL)) {
      switch (insn.getOpcode()) {
      case TGSI_OPCODE_ATOMUADD:
      case TGSI_OPCODE_ATOMXCHG:
      case TGSI_OPCODE_ATOMCAS:
      case TGSI_OPCODE_ATOMAND:
      case TGSI_OPCODE_ATOMOR:
      case TGSI_OPCODE_ATOMXOR:
      case TGSI_OPCODE_ATOMUMIN:
      case TGSI_OPCODE_ATOMIMIN:
      case TGSI_OPCODE_ATOMUMAX:
      case TGSI_OPCODE_ATOMIMAX:
      case TGSI_OPCODE_ATOMFADD:
      case TGSI_OPCODE_LOAD:
         info->io.globalAccess |= (insn.getOpcode() == TGSI_OPCODE_LOAD) ?
            0x1 : 0x2;
         break;
      }
   }


   for (unsigned s = 0; s < insn.srcCount(); ++s)
      scanInstructionSrc(insn, insn.getSrc(s), insn.srcMask(s));

   for (unsigned s = 0; s < insn.getNumTexOffsets(); ++s)
      scanInstructionSrc(insn, insn.getTexOffset(s), insn.texOffsetMask());

   return true;
}

nv50_ir::TexInstruction::Target
Instruction::getTexture(const tgsi::Source *code, int s) const
{
   // XXX: indirect access
   unsigned int r;

   switch (getSrc(s).getFile()) {
/*
   case TGSI_FILE_RESOURCE:
      r = getSrc(s).getIndex(0);
      return translateTexture(code->resources.at(r).target);
*/
   case TGSI_FILE_SAMPLER_VIEW:
      r = getSrc(s).getIndex(0);
      return translateTexture(code->textureViews.at(r).target);
   default:
      return translateTexture(insn->Texture.Texture);
   }
}

} // namespace tgsi

namespace {

using namespace nv50_ir;

class Converter : public BuildUtil
{
public:
   Converter(Program *, const tgsi::Source *);
   ~Converter();

   bool run();

private:
   struct Subroutine
   {
      Subroutine(Function *f) : f(f) { }
      Function *f;
      ValueMap values;
   };

   Value *shiftAddress(Value *);
   Value *getVertexBase(int s);
   Value *getOutputBase(int s);
   DataArray *getArrayForFile(unsigned file, int idx);
   Value *fetchSrc(int s, int c);
   Value *fetchDst(int d, int c);
   Value *acquireDst(int d, int c);
   void storeDst(int d, int c, Value *);

   Value *fetchSrc(const tgsi::Instruction::SrcRegister src, int c, Value *ptr);
   void storeDst(const tgsi::Instruction::DstRegister dst, int c,
                 Value *val, Value *ptr);

   void adjustTempIndex(int arrayId, int &idx, int &idx2d) const;
   Value *applySrcMod(Value *, int s, int c);

   Symbol *makeSym(uint file, int fileIndex, int idx, int c, uint32_t addr);
   Symbol *srcToSym(tgsi::Instruction::SrcRegister, int c);
   Symbol *dstToSym(tgsi::Instruction::DstRegister, int c);

   bool isSubGroupMask(uint8_t semantic);

   bool handleInstruction(const struct tgsi_full_instruction *);
   void exportOutputs();
   inline Subroutine *getSubroutine(unsigned ip);
   inline Subroutine *getSubroutine(Function *);
   inline bool isEndOfSubroutine(uint ip);

   void loadProjTexCoords(Value *dst[4], Value *src[4], unsigned int mask);

   // R,S,L,C,Dx,Dy encode TGSI sources for respective values (0xSf for auto)
   void setTexRS(TexInstruction *, unsigned int& s, int R, int S);
   void handleTEX(Value *dst0[4], int R, int S, int L, int C, int Dx, int Dy);
   void handleTXF(Value *dst0[4], int R, int L_M);
   void handleTXQ(Value *dst0[4], enum TexQuery, int R);
   void handleFBFETCH(Value *dst0[4]);
   void handleLIT(Value *dst0[4]);
   void handleUserClipPlanes();

   // Symbol *getResourceBase(int r);
   void getImageCoords(std::vector<Value *>&, int s);

   void handleLOAD(Value *dst0[4]);
   void handleSTORE();
   void handleATOM(Value *dst0[4], DataType, uint16_t subOp);

   void handleINTERP(Value *dst0[4]);

   uint8_t translateInterpMode(const struct nv50_ir_varying *var,
                               operation& op);
   Value *interpolate(tgsi::Instruction::SrcRegister, int c, Value *ptr);

   void insertConvergenceOps(BasicBlock *conv, BasicBlock *fork);

   Value *buildDot(int dim);

   class BindArgumentsPass : public Pass {
   public:
      BindArgumentsPass(Converter &conv) : conv(conv) { }

   private:
      Converter &conv;
      Subroutine *sub;

      inline const Location *getValueLocation(Subroutine *, Value *);

      template<typename T> inline void
      updateCallArgs(Instruction *i, void (Instruction::*setArg)(int, Value *),
                     T (Function::*proto));

      template<typename T> inline void
      updatePrototype(BitSet *set, void (Function::*updateSet)(),
                      T (Function::*proto));

   protected:
      bool visit(Function *);
      bool visit(BasicBlock *bb) { return false; }
   };

private:
   const tgsi::Source *code;
   const struct nv50_ir_prog_info *info;

   struct {
      std::map<unsigned, Subroutine> map;
      Subroutine *cur;
   } sub;

   uint ip; // instruction pointer

   tgsi::Instruction tgsi;

   DataType dstTy;
   DataType srcTy;

   DataArray tData; // TGSI_FILE_TEMPORARY
   DataArray lData; // TGSI_FILE_TEMPORARY, for indirect arrays
   DataArray aData; // TGSI_FILE_ADDRESS
   DataArray oData; // TGSI_FILE_OUTPUT (if outputs in registers)

   Value *zero;
   Value *fragCoord[4];
   Value *clipVtx[4];

   Value *vtxBase[5]; // base address of vertex in primitive (for TP/GP)
   uint8_t vtxBaseValid;

   Value *outBase; // base address of vertex out patch (for TCP)

   Stack condBBs;  // fork BB, then else clause BB
   Stack joinBBs;  // fork BB, for inserting join ops on ENDIF
   Stack loopBBs;  // loop headers
   Stack breakBBs; // end of / after loop

   Value *viewport;
};

Symbol *
Converter::srcToSym(tgsi::Instruction::SrcRegister src, int c)
{
   const int swz = src.getSwizzle(c);

   /* TODO: Use Array ID when it's available for the index */
   return makeSym(src.getFile(),
                  src.is2D() ? src.getIndex(1) : 0,
                  src.getIndex(0), swz,
                  src.getIndex(0) * 16 + swz * 4);
}

Symbol *
Converter::dstToSym(tgsi::Instruction::DstRegister dst, int c)
{
   /* TODO: Use Array ID when it's available for the index */
   return makeSym(dst.getFile(),
                  dst.is2D() ? dst.getIndex(1) : 0,
                  dst.getIndex(0), c,
                  dst.getIndex(0) * 16 + c * 4);
}

Symbol *
Converter::makeSym(uint tgsiFile, int fileIdx, int idx, int c, uint32_t address)
{
   Symbol *sym = new_Symbol(prog, tgsi::translateFile(tgsiFile));

   sym->reg.fileIndex = fileIdx;

   if (tgsiFile == TGSI_FILE_MEMORY) {
      switch (code->memoryFiles[fileIdx].mem_type) {
      case TGSI_MEMORY_TYPE_GLOBAL:
         /* No-op this is the default for TGSI_FILE_MEMORY */
         sym->setFile(FILE_MEMORY_GLOBAL);
         break;
      case TGSI_MEMORY_TYPE_SHARED:
         sym->setFile(FILE_MEMORY_SHARED);
         break;
      case TGSI_MEMORY_TYPE_INPUT:
         assert(prog->getType() == Program::TYPE_COMPUTE);
         assert(idx == -1);
         sym->setFile(FILE_SHADER_INPUT);
         address += info->prop.cp.inputOffset;
         break;
      default:
         assert(0); /* TODO: Add support for global and private memory */
      }
   }

   if (idx >= 0) {
      if (sym->reg.file == FILE_SHADER_INPUT)
         sym->setOffset(info->in[idx].slot[c] * 4);
      else
      if (sym->reg.file == FILE_SHADER_OUTPUT)
         sym->setOffset(info->out[idx].slot[c] * 4);
      else
      if (sym->reg.file == FILE_SYSTEM_VALUE)
         sym->setSV(tgsi::translateSysVal(info->sv[idx].sn), c);
      else
         sym->setOffset(address);
   } else {
      sym->setOffset(address);
   }
   return sym;
}

uint8_t
Converter::translateInterpMode(const struct nv50_ir_varying *var, operation& op)
{
   uint8_t mode = NV50_IR_INTERP_PERSPECTIVE;

   if (var->flat)
      mode = NV50_IR_INTERP_FLAT;
   else
   if (var->linear)
      mode = NV50_IR_INTERP_LINEAR;
   else
   if (var->sc)
      mode = NV50_IR_INTERP_SC;

   op = (mode == NV50_IR_INTERP_PERSPECTIVE || mode == NV50_IR_INTERP_SC)
      ? OP_PINTERP : OP_LINTERP;

   if (var->centroid)
      mode |= NV50_IR_INTERP_CENTROID;

   return mode;
}

Value *
Converter::interpolate(tgsi::Instruction::SrcRegister src, int c, Value *ptr)
{
   operation op;

   // XXX: no way to know interpolation mode if we don't know what's accessed
   const uint8_t mode = translateInterpMode(&info->in[ptr ? 0 :
                                                      src.getIndex(0)], op);

   Instruction *insn = new_Instruction(func, op, TYPE_F32);

   insn->setDef(0, getScratch());
   insn->setSrc(0, srcToSym(src, c));
   if (op == OP_PINTERP)
      insn->setSrc(1, fragCoord[3]);
   if (ptr)
      insn->setIndirect(0, 0, ptr);

   insn->setInterpolate(mode);

   bb->insertTail(insn);
   return insn->getDef(0);
}

Value *
Converter::applySrcMod(Value *val, int s, int c)
{
   Modifier m = tgsi.getSrc(s).getMod(c);
   DataType ty = tgsi.inferSrcType();

   if (m & Modifier(NV50_IR_MOD_ABS))
      val = mkOp1v(OP_ABS, ty, getScratch(), val);

   if (m & Modifier(NV50_IR_MOD_NEG))
      val = mkOp1v(OP_NEG, ty, getScratch(), val);

   return val;
}

Value *
Converter::getVertexBase(int s)
{
   assert(s < 5);
   if (!(vtxBaseValid & (1 << s))) {
      const int index = tgsi.getSrc(s).getIndex(1);
      Value *rel = NULL;
      if (tgsi.getSrc(s).isIndirect(1))
         rel = fetchSrc(tgsi.getSrc(s).getIndirect(1), 0, NULL);
      vtxBaseValid |= 1 << s;
      vtxBase[s] = mkOp2v(OP_PFETCH, TYPE_U32, getSSA(4, FILE_ADDRESS),
                          mkImm(index), rel);
   }
   return vtxBase[s];
}

Value *
Converter::getOutputBase(int s)
{
   assert(s < 5);
   if (!(vtxBaseValid & (1 << s))) {
      Value *offset = loadImm(NULL, tgsi.getSrc(s).getIndex(1));
      if (tgsi.getSrc(s).isIndirect(1))
         offset = mkOp2v(OP_ADD, TYPE_U32, getSSA(),
                         fetchSrc(tgsi.getSrc(s).getIndirect(1), 0, NULL),
                         offset);
      vtxBaseValid |= 1 << s;
      vtxBase[s] = mkOp2v(OP_ADD, TYPE_U32, getSSA(), outBase, offset);
   }
   return vtxBase[s];
}

Value *
Converter::fetchSrc(int s, int c)
{
   Value *res;
   Value *ptr = NULL, *dimRel = NULL;

   tgsi::Instruction::SrcRegister src = tgsi.getSrc(s);

   if (src.isIndirect(0))
      ptr = fetchSrc(src.getIndirect(0), 0, NULL);

   if (src.is2D()) {
      switch (src.getFile()) {
      case TGSI_FILE_OUTPUT:
         dimRel = getOutputBase(s);
         break;
      case TGSI_FILE_INPUT:
         dimRel = getVertexBase(s);
         break;
      case TGSI_FILE_CONSTANT:
         // on NVC0, this is valid and c{I+J}[k] == cI[(J << 16) + k]
         if (src.isIndirect(1))
            dimRel = fetchSrc(src.getIndirect(1), 0, 0);
         break;
      default:
         break;
      }
   }

   res = fetchSrc(src, c, ptr);

   if (dimRel)
      res->getInsn()->setIndirect(0, 1, dimRel);

   return applySrcMod(res, s, c);
}

Value *
Converter::fetchDst(int d, int c)
{
   Value *res;
   Value *ptr = NULL, *dimRel = NULL;

   tgsi::Instruction::DstRegister dst = tgsi.getDst(d);

   if (dst.isIndirect(0))
      ptr = fetchSrc(dst.getIndirect(0), 0, NULL);

   if (dst.is2D()) {
      switch (dst.getFile()) {
      case TGSI_FILE_OUTPUT:
         assert(0); // TODO
         dimRel = NULL;
         break;
      case TGSI_FILE_INPUT:
         assert(0); // TODO
         dimRel = NULL;
         break;
      case TGSI_FILE_CONSTANT:
         // on NVC0, this is valid and c{I+J}[k] == cI[(J << 16) + k]
         if (dst.isIndirect(1))
            dimRel = fetchSrc(dst.getIndirect(1), 0, 0);
         break;
      default:
         break;
      }
   }

   struct tgsi_full_src_register fsr = dst.asSrc();
   tgsi::Instruction::SrcRegister src(&fsr);
   res = fetchSrc(src, c, ptr);

   if (dimRel)
      res->getInsn()->setIndirect(0, 1, dimRel);

   return res;
}

Converter::DataArray *
Converter::getArrayForFile(unsigned file, int idx)
{
   switch (file) {
   case TGSI_FILE_TEMPORARY:
      return idx == 0 ? &tData : &lData;
   case TGSI_FILE_ADDRESS:
      return &aData;
   case TGSI_FILE_OUTPUT:
      assert(prog->getType() == Program::TYPE_FRAGMENT);
      return &oData;
   default:
      assert(!"invalid/unhandled TGSI source file");
      return NULL;
   }
}

Value *
Converter::shiftAddress(Value *index)
{
   if (!index)
      return NULL;
   return mkOp2v(OP_SHL, TYPE_U32, getSSA(4, FILE_ADDRESS), index, mkImm(4));
}

void
Converter::adjustTempIndex(int arrayId, int &idx, int &idx2d) const
{
   std::map<int, int>::const_iterator it =
      code->indirectTempOffsets.find(arrayId);
   if (it == code->indirectTempOffsets.end())
      return;

   idx2d = 1;
   idx += it->second;
}

bool
Converter::isSubGroupMask(uint8_t semantic)
{
   switch (semantic) {
      case TGSI_SEMANTIC_SUBGROUP_EQ_MASK:
      case TGSI_SEMANTIC_SUBGROUP_LT_MASK:
      case TGSI_SEMANTIC_SUBGROUP_LE_MASK:
      case TGSI_SEMANTIC_SUBGROUP_GT_MASK:
      case TGSI_SEMANTIC_SUBGROUP_GE_MASK:
         return true;
      default:
         return false;
   }
}

Value *
Converter::fetchSrc(tgsi::Instruction::SrcRegister src, int c, Value *ptr)
{
   int idx2d = src.is2D() ? src.getIndex(1) : 0;
   int idx = src.getIndex(0);
   const int swz = src.getSwizzle(c);
   Instruction *ld;

   switch (src.getFile()) {
   case TGSI_FILE_IMMEDIATE:
      assert(!ptr);
      return loadImm(NULL, info->immd.data[idx * 4 + swz]);
   case TGSI_FILE_CONSTANT:
      return mkLoadv(TYPE_U32, srcToSym(src, c), shiftAddress(ptr));
   case TGSI_FILE_INPUT:
      if (prog->getType() == Program::TYPE_FRAGMENT) {
         // don't load masked inputs, won't be assigned a slot
         if (!ptr && !(info->in[idx].mask & (1 << swz)))
            return loadImm(NULL, swz == TGSI_SWIZZLE_W ? 1.0f : 0.0f);
         return interpolate(src, c, shiftAddress(ptr));
      } else
      if (prog->getType() == Program::TYPE_GEOMETRY) {
         if (!ptr && info->in[idx].sn == TGSI_SEMANTIC_PRIMID)
            return mkOp1v(OP_RDSV, TYPE_U32, getSSA(), mkSysVal(SV_PRIMITIVE_ID, 0));
         // XXX: This is going to be a problem with scalar arrays, i.e. when
         // we cannot assume that the address is given in units of vec4.
         //
         // nv50 and nvc0 need different things here, so let the lowering
         // passes decide what to do with the address
         if (ptr)
            return mkLoadv(TYPE_U32, srcToSym(src, c), ptr);
      }
      ld = mkLoad(TYPE_U32, getSSA(), srcToSym(src, c), shiftAddress(ptr));
      ld->perPatch = info->in[idx].patch;
      return ld->getDef(0);
   case TGSI_FILE_OUTPUT:
      assert(prog->getType() == Program::TYPE_TESSELLATION_CONTROL);
      ld = mkLoad(TYPE_U32, getSSA(), srcToSym(src, c), shiftAddress(ptr));
      ld->perPatch = info->out[idx].patch;
      return ld->getDef(0);
   case TGSI_FILE_SYSTEM_VALUE:
      assert(!ptr);
      if (info->sv[idx].sn == TGSI_SEMANTIC_THREAD_ID &&
          info->prop.cp.numThreads[swz] == 1)
         return loadImm(NULL, 0u);
      if (isSubGroupMask(info->sv[idx].sn) && swz > 0)
         return loadImm(NULL, 0u);
      if (info->sv[idx].sn == TGSI_SEMANTIC_SUBGROUP_SIZE)
         return loadImm(NULL, 32u);
      ld = mkOp1(OP_RDSV, TYPE_U32, getSSA(), srcToSym(src, c));
      ld->perPatch = info->sv[idx].patch;
      return ld->getDef(0);
   case TGSI_FILE_TEMPORARY: {
      int arrayid = src.getArrayId();
      if (!arrayid)
         arrayid = code->tempArrayId[idx];
      adjustTempIndex(arrayid, idx, idx2d);
   }
      /* fallthrough */
   default:
      return getArrayForFile(src.getFile(), idx2d)->load(
         sub.cur->values, idx, swz, shiftAddress(ptr));
   }
}

Value *
Converter::acquireDst(int d, int c)
{
   const tgsi::Instruction::DstRegister dst = tgsi.getDst(d);
   const unsigned f = dst.getFile();
   int idx = dst.getIndex(0);
   int idx2d = dst.is2D() ? dst.getIndex(1) : 0;

   if (dst.isMasked(c) || f == TGSI_FILE_BUFFER || f == TGSI_FILE_MEMORY ||
       f == TGSI_FILE_IMAGE)
      return NULL;

   if (dst.isIndirect(0) ||
       f == TGSI_FILE_SYSTEM_VALUE ||
       (f == TGSI_FILE_OUTPUT && prog->getType() != Program::TYPE_FRAGMENT))
      return getScratch();

   if (f == TGSI_FILE_TEMPORARY) {
      int arrayid = dst.getArrayId();
      if (!arrayid)
         arrayid = code->tempArrayId[idx];
      adjustTempIndex(arrayid, idx, idx2d);
   }

   return getArrayForFile(f, idx2d)-> acquire(sub.cur->values, idx, c);
}

void
Converter::storeDst(int d, int c, Value *val)
{
   const tgsi::Instruction::DstRegister dst = tgsi.getDst(d);

   if (tgsi.getSaturate()) {
      mkOp1(OP_SAT, dstTy, val, val);
   }

   Value *ptr = NULL;
   if (dst.isIndirect(0))
      ptr = shiftAddress(fetchSrc(dst.getIndirect(0), 0, NULL));

   if (info->io.genUserClip > 0 &&
       dst.getFile() == TGSI_FILE_OUTPUT &&
       !dst.isIndirect(0) && dst.getIndex(0) == code->clipVertexOutput) {
      mkMov(clipVtx[c], val);
      val = clipVtx[c];
   }

   storeDst(dst, c, val, ptr);
}

void
Converter::storeDst(const tgsi::Instruction::DstRegister dst, int c,
                    Value *val, Value *ptr)
{
   const unsigned f = dst.getFile();
   int idx = dst.getIndex(0);
   int idx2d = dst.is2D() ? dst.getIndex(1) : 0;

   if (f == TGSI_FILE_SYSTEM_VALUE) {
      assert(!ptr);
      mkOp2(OP_WRSV, TYPE_U32, NULL, dstToSym(dst, c), val);
   } else
   if (f == TGSI_FILE_OUTPUT && prog->getType() != Program::TYPE_FRAGMENT) {

      if (ptr || (info->out[idx].mask & (1 << c))) {
         /* Save the viewport index into a scratch register so that it can be
            exported at EMIT time */
         if (info->out[idx].sn == TGSI_SEMANTIC_VIEWPORT_INDEX &&
             prog->getType() == Program::TYPE_GEOMETRY &&
             viewport != NULL)
            mkOp1(OP_MOV, TYPE_U32, viewport, val);
         else
            mkStore(OP_EXPORT, TYPE_U32, dstToSym(dst, c), ptr, val)->perPatch =
               info->out[idx].patch;
      }
   } else
   if (f == TGSI_FILE_TEMPORARY ||
       f == TGSI_FILE_ADDRESS ||
       f == TGSI_FILE_OUTPUT) {
      if (f == TGSI_FILE_TEMPORARY) {
         int arrayid = dst.getArrayId();
         if (!arrayid)
            arrayid = code->tempArrayId[idx];
         adjustTempIndex(arrayid, idx, idx2d);
      }

      getArrayForFile(f, idx2d)->store(sub.cur->values, idx, c, ptr, val);
   } else {
      assert(!"invalid dst file");
   }
}

#define FOR_EACH_DST_ENABLED_CHANNEL(d, chan, inst) \
   for (chan = 0; chan < 4; ++chan)                 \
      if (!inst.getDst(d).isMasked(chan))

Value *
Converter::buildDot(int dim)
{
   assert(dim > 0);

   Value *src0 = fetchSrc(0, 0), *src1 = fetchSrc(1, 0);
   Value *dotp = getScratch();

   mkOp2(OP_MUL, TYPE_F32, dotp, src0, src1)
      ->dnz = info->io.mul_zero_wins;

   for (int c = 1; c < dim; ++c) {
      src0 = fetchSrc(0, c);
      src1 = fetchSrc(1, c);
      mkOp3(OP_MAD, TYPE_F32, dotp, src0, src1, dotp)
         ->dnz = info->io.mul_zero_wins;
   }
   return dotp;
}

void
Converter::insertConvergenceOps(BasicBlock *conv, BasicBlock *fork)
{
   FlowInstruction *join = new_FlowInstruction(func, OP_JOIN, NULL);
   join->fixed = 1;
   conv->insertHead(join);

   assert(!fork->joinAt);
   fork->joinAt = new_FlowInstruction(func, OP_JOINAT, conv);
   fork->insertBefore(fork->getExit(), fork->joinAt);
}

void
Converter::setTexRS(TexInstruction *tex, unsigned int& s, int R, int S)
{
   unsigned rIdx = 0, sIdx = 0;

   if (R >= 0 && tgsi.getSrc(R).getFile() != TGSI_FILE_SAMPLER) {
      // This is the bindless case. We have to get the actual value and pass
      // it in. This will be the complete handle.
      tex->tex.rIndirectSrc = s;
      tex->setSrc(s++, fetchSrc(R, 0));
      tex->setTexture(tgsi.getTexture(code, R), 0xff, 0x1f);
      tex->tex.bindless = true;
      return;
   }

   if (R >= 0)
      rIdx = tgsi.getSrc(R).getIndex(0);
   if (S >= 0)
      sIdx = tgsi.getSrc(S).getIndex(0);

   tex->setTexture(tgsi.getTexture(code, R), rIdx, sIdx);

   if (tgsi.getSrc(R).isIndirect(0)) {
      tex->tex.rIndirectSrc = s;
      tex->setSrc(s++, fetchSrc(tgsi.getSrc(R).getIndirect(0), 0, NULL));
   }
   if (S >= 0 && tgsi.getSrc(S).isIndirect(0)) {
      tex->tex.sIndirectSrc = s;
      tex->setSrc(s++, fetchSrc(tgsi.getSrc(S).getIndirect(0), 0, NULL));
   }
}

void
Converter::handleTXQ(Value *dst0[4], enum TexQuery query, int R)
{
   TexInstruction *tex = new_TexInstruction(func, OP_TXQ);
   tex->tex.query = query;
   unsigned int c, d;

   for (d = 0, c = 0; c < 4; ++c) {
      if (!dst0[c])
         continue;
      tex->tex.mask |= 1 << c;
      tex->setDef(d++, dst0[c]);
   }
   if (query == TXQ_DIMS)
      tex->setSrc((c = 0), fetchSrc(0, 0)); // mip level
   else
      tex->setSrc((c = 0), zero);

   setTexRS(tex, ++c, R, -1);

   bb->insertTail(tex);
}

void
Converter::loadProjTexCoords(Value *dst[4], Value *src[4], unsigned int mask)
{
   Value *proj = fetchSrc(0, 3);
   Instruction *insn = proj->getUniqueInsn();
   int c;

   if (insn->op == OP_PINTERP) {
      bb->insertTail(insn = cloneForward(func, insn));
      insn->op = OP_LINTERP;
      insn->setInterpolate(NV50_IR_INTERP_LINEAR | insn->getSampleMode());
      insn->setSrc(1, NULL);
      proj = insn->getDef(0);
   }
   proj = mkOp1v(OP_RCP, TYPE_F32, getSSA(), proj);

   for (c = 0; c < 4; ++c) {
      if (!(mask & (1 << c)))
         continue;
      if ((insn = src[c]->getUniqueInsn())->op != OP_PINTERP)
         continue;
      mask &= ~(1 << c);

      bb->insertTail(insn = cloneForward(func, insn));
      insn->setInterpolate(NV50_IR_INTERP_PERSPECTIVE | insn->getSampleMode());
      insn->setSrc(1, proj);
      dst[c] = insn->getDef(0);
   }
   if (!mask)
      return;

   proj = mkOp1v(OP_RCP, TYPE_F32, getSSA(), fetchSrc(0, 3));

   for (c = 0; c < 4; ++c)
      if (mask & (1 << c))
         dst[c] = mkOp2v(OP_MUL, TYPE_F32, getSSA(), src[c], proj);
}

// order of nv50 ir sources: x y z layer lod/bias shadow
// order of TGSI TEX sources: x y z layer shadow lod/bias
//  lowering will finally set the hw specific order (like array first on nvc0)
void
Converter::handleTEX(Value *dst[4], int R, int S, int L, int C, int Dx, int Dy)
{
   Value *arg[4], *src[8];
   Value *lod = NULL, *shd = NULL;
   unsigned int s, c, d;
   TexInstruction *texi = new_TexInstruction(func, tgsi.getOP());

   TexInstruction::Target tgt = tgsi.getTexture(code, R);

   for (s = 0; s < tgt.getArgCount(); ++s)
      arg[s] = src[s] = fetchSrc(0, s);

   if (tgsi.getOpcode() == TGSI_OPCODE_TEX_LZ)
      lod = loadImm(NULL, 0);
   else if (texi->op == OP_TXL || texi->op == OP_TXB)
      lod = fetchSrc(L >> 4, L & 3);

   if (C == 0x0f)
      C = 0x00 | MAX2(tgt.getArgCount(), 2); // guess DC src

   if (tgsi.getOpcode() == TGSI_OPCODE_TG4 &&
       tgt == TEX_TARGET_CUBE_ARRAY_SHADOW)
      shd = fetchSrc(1, 0);
   else if (tgt.isShadow())
      shd = fetchSrc(C >> 4, C & 3);

   if (texi->op == OP_TXD) {
      for (c = 0; c < tgt.getDim() + tgt.isCube(); ++c) {
         texi->dPdx[c].set(fetchSrc(Dx >> 4, (Dx & 3) + c));
         texi->dPdy[c].set(fetchSrc(Dy >> 4, (Dy & 3) + c));
      }
   }

   // cube textures don't care about projection value, it's divided out
   if (tgsi.getOpcode() == TGSI_OPCODE_TXP && !tgt.isCube() && !tgt.isArray()) {
      unsigned int n = tgt.getDim();
      if (shd) {
         arg[n] = shd;
         ++n;
         assert(tgt.getDim() == tgt.getArgCount());
      }
      loadProjTexCoords(src, arg, (1 << n) - 1);
      if (shd)
         shd = src[n - 1];
   }

   for (c = 0, d = 0; c < 4; ++c) {
      if (dst[c]) {
         texi->setDef(d++, dst[c]);
         texi->tex.mask |= 1 << c;
      } else {
         // NOTE: maybe hook up def too, for CSE
      }
   }
   for (s = 0; s < tgt.getArgCount(); ++s)
      texi->setSrc(s, src[s]);
   if (lod)
      texi->setSrc(s++, lod);
   if (shd)
      texi->setSrc(s++, shd);

   setTexRS(texi, s, R, S);

   if (tgsi.getOpcode() == TGSI_OPCODE_SAMPLE_C_LZ)
      texi->tex.levelZero = true;
   if (prog->getType() != Program::TYPE_FRAGMENT &&
       (tgsi.getOpcode() == TGSI_OPCODE_TEX ||
        tgsi.getOpcode() == TGSI_OPCODE_TEX2 ||
        tgsi.getOpcode() == TGSI_OPCODE_TXP))
      texi->tex.levelZero = true;
   if (tgsi.getOpcode() == TGSI_OPCODE_TG4 && !tgt.isShadow())
      texi->tex.gatherComp = tgsi.getSrc(1).getValueU32(0, info);

   texi->tex.useOffsets = tgsi.getNumTexOffsets();
   for (s = 0; s < tgsi.getNumTexOffsets(); ++s) {
      for (c = 0; c < 3; ++c) {
         texi->offset[s][c].set(fetchSrc(tgsi.getTexOffset(s), c, NULL));
         texi->offset[s][c].setInsn(texi);
      }
   }

   bb->insertTail(texi);
}

// 1st source: xyz = coordinates, w = lod/sample
// 2nd source: offset
void
Converter::handleTXF(Value *dst[4], int R, int L_M)
{
   TexInstruction *texi = new_TexInstruction(func, tgsi.getOP());
   int ms;
   unsigned int c, d, s;

   texi->tex.target = tgsi.getTexture(code, R);

   ms = texi->tex.target.isMS() ? 1 : 0;
   texi->tex.levelZero = ms; /* MS textures don't have mip-maps */

   for (c = 0, d = 0; c < 4; ++c) {
      if (dst[c]) {
         texi->setDef(d++, dst[c]);
         texi->tex.mask |= 1 << c;
      }
   }
   for (c = 0; c < (texi->tex.target.getArgCount() - ms); ++c)
      texi->setSrc(c, fetchSrc(0, c));
   if (!ms && tgsi.getOpcode() == TGSI_OPCODE_TXF_LZ)
      texi->setSrc(c++, loadImm(NULL, 0));
   else
      texi->setSrc(c++, fetchSrc(L_M >> 4, L_M & 3)); // lod or ms

   setTexRS(texi, c, R, -1);

   texi->tex.useOffsets = tgsi.getNumTexOffsets();
   for (s = 0; s < tgsi.getNumTexOffsets(); ++s) {
      for (c = 0; c < 3; ++c) {
         texi->offset[s][c].set(fetchSrc(tgsi.getTexOffset(s), c, NULL));
         texi->offset[s][c].setInsn(texi);
      }
   }

   bb->insertTail(texi);
}

void
Converter::handleFBFETCH(Value *dst[4])
{
   TexInstruction *texi = new_TexInstruction(func, OP_TXF);
   unsigned int c, d;

   texi->tex.target = TEX_TARGET_2D_MS_ARRAY;
   texi->tex.levelZero = 1;
   texi->tex.useOffsets = 0;

   for (c = 0, d = 0; c < 4; ++c) {
      if (dst[c]) {
         texi->setDef(d++, dst[c]);
         texi->tex.mask |= 1 << c;
      }
   }

   Value *x = mkOp1v(OP_RDSV, TYPE_F32, getScratch(), mkSysVal(SV_POSITION, 0));
   Value *y = mkOp1v(OP_RDSV, TYPE_F32, getScratch(), mkSysVal(SV_POSITION, 1));
   Value *z = mkOp1v(OP_RDSV, TYPE_U32, getScratch(), mkSysVal(SV_LAYER, 0));
   Value *ms = mkOp1v(OP_RDSV, TYPE_U32, getScratch(), mkSysVal(SV_SAMPLE_INDEX, 0));

   mkCvt(OP_CVT, TYPE_U32, x, TYPE_F32, x)->rnd = ROUND_Z;
   mkCvt(OP_CVT, TYPE_U32, y, TYPE_F32, y)->rnd = ROUND_Z;
   texi->setSrc(0, x);
   texi->setSrc(1, y);
   texi->setSrc(2, z);
   texi->setSrc(3, ms);

   texi->tex.r = texi->tex.s = -1;

   bb->insertTail(texi);
}

void
Converter::handleLIT(Value *dst0[4])
{
   Value *val0 = NULL;
   unsigned int mask = tgsi.getDst(0).getMask();

   if (mask & (1 << 0))
      loadImm(dst0[0], 1.0f);

   if (mask & (1 << 3))
      loadImm(dst0[3], 1.0f);

   if (mask & (3 << 1)) {
      val0 = getScratch();
      mkOp2(OP_MAX, TYPE_F32, val0, fetchSrc(0, 0), zero);
      if (mask & (1 << 1))
         mkMov(dst0[1], val0);
   }

   if (mask & (1 << 2)) {
      Value *src1 = fetchSrc(0, 1), *src3 = fetchSrc(0, 3);
      Value *val1 = getScratch(), *val3 = getScratch();

      Value *pos128 = loadImm(NULL, +127.999999f);
      Value *neg128 = loadImm(NULL, -127.999999f);

      mkOp2(OP_MAX, TYPE_F32, val1, src1, zero);
      mkOp2(OP_MAX, TYPE_F32, val3, src3, neg128);
      mkOp2(OP_MIN, TYPE_F32, val3, val3, pos128);
      mkOp2(OP_POW, TYPE_F32, val3, val1, val3);

      mkCmp(OP_SLCT, CC_GT, TYPE_F32, dst0[2], TYPE_F32, val3, zero, val0);
   }
}

/* Keep this around for now as reference when adding img support
static inline bool
isResourceSpecial(const int r)
{
   return (r == TGSI_RESOURCE_GLOBAL ||
           r == TGSI_RESOURCE_LOCAL ||
           r == TGSI_RESOURCE_PRIVATE ||
           r == TGSI_RESOURCE_INPUT);
}

static inline bool
isResourceRaw(const tgsi::Source *code, const int r)
{
   return isResourceSpecial(r) || code->resources[r].raw;
}

static inline nv50_ir::TexTarget
getResourceTarget(const tgsi::Source *code, int r)
{
   if (isResourceSpecial(r))
      return nv50_ir::TEX_TARGET_BUFFER;
   return tgsi::translateTexture(code->resources.at(r).target);
}

Symbol *
Converter::getResourceBase(const int r)
{
   Symbol *sym = NULL;

   switch (r) {
   case TGSI_RESOURCE_GLOBAL:
      sym = new_Symbol(prog, nv50_ir::FILE_MEMORY_GLOBAL,
                       info->io.auxCBSlot);
      break;
   case TGSI_RESOURCE_LOCAL:
      assert(prog->getType() == Program::TYPE_COMPUTE);
      sym = mkSymbol(nv50_ir::FILE_MEMORY_SHARED, 0, TYPE_U32,
                     info->prop.cp.sharedOffset);
      break;
   case TGSI_RESOURCE_PRIVATE:
      sym = mkSymbol(nv50_ir::FILE_MEMORY_LOCAL, 0, TYPE_U32,
                     info->bin.tlsSpace);
      break;
   case TGSI_RESOURCE_INPUT:
      assert(prog->getType() == Program::TYPE_COMPUTE);
      sym = mkSymbol(nv50_ir::FILE_SHADER_INPUT, 0, TYPE_U32,
                     info->prop.cp.inputOffset);
      break;
   default:
      sym = new_Symbol(prog,
                       nv50_ir::FILE_MEMORY_GLOBAL, code->resources.at(r).slot);
      break;
   }
   return sym;
}

void
Converter::getResourceCoords(std::vector<Value *> &coords, int r, int s)
{
   const int arg =
      TexInstruction::Target(getResourceTarget(code, r)).getArgCount();

   for (int c = 0; c < arg; ++c)
      coords.push_back(fetchSrc(s, c));

   // NOTE: TGSI_RESOURCE_GLOBAL needs FILE_GPR; this is an nv50 quirk
   if (r == TGSI_RESOURCE_LOCAL ||
       r == TGSI_RESOURCE_PRIVATE ||
       r == TGSI_RESOURCE_INPUT)
      coords[0] = mkOp1v(OP_MOV, TYPE_U32, getScratch(4, FILE_ADDRESS),
                         coords[0]);
}

static inline int
partitionLoadStore(uint8_t comp[2], uint8_t size[2], uint8_t mask)
{
   int n = 0;

   while (mask) {
      if (mask & 1) {
         size[n]++;
      } else {
         if (size[n])
            comp[n = 1] = size[0] + 1;
         else
            comp[n]++;
      }
      mask >>= 1;
   }
   if (size[0] == 3) {
      n = 1;
      size[0] = (comp[0] == 1) ? 1 : 2;
      size[1] = 3 - size[0];
      comp[1] = comp[0] + size[0];
   }
   return n + 1;
}
*/
void
Converter::getImageCoords(std::vector<Value *> &coords, int s)
{
   TexInstruction::Target t =
      TexInstruction::Target(tgsi.getImageTarget());
   const int arg = t.getDim() + (t.isArray() || t.isCube());

   for (int c = 0; c < arg; ++c)
      coords.push_back(fetchSrc(s, c));

   if (t.isMS())
      coords.push_back(fetchSrc(s, 3));
}

// For raw loads, granularity is 4 byte.
// Usage of the texture read mask on OP_SULDP is not allowed.
void
Converter::handleLOAD(Value *dst0[4])
{
   const int r = tgsi.getSrc(0).getIndex(0);
   int c;
   std::vector<Value *> off, src, ldv, def;
   Value *ind = NULL;

   if (tgsi.getSrc(0).isIndirect(0))
      ind = fetchSrc(tgsi.getSrc(0).getIndirect(0), 0, 0);

   switch (tgsi.getSrc(0).getFile()) {
   case TGSI_FILE_BUFFER:
   case TGSI_FILE_MEMORY:
      for (c = 0; c < 4; ++c) {
         if (!dst0[c])
            continue;

         Value *off;
         Symbol *sym;
         uint32_t src0_component_offset = tgsi.getSrc(0).getSwizzle(c) * 4;

         if (tgsi.getSrc(1).getFile() == TGSI_FILE_IMMEDIATE) {
            off = NULL;
            sym = makeSym(tgsi.getSrc(0).getFile(), r, -1, c,
                          tgsi.getSrc(1).getValueU32(0, info) +
                          src0_component_offset);
         } else {
            // yzw are ignored for buffers
            off = fetchSrc(1, 0);
            sym = makeSym(tgsi.getSrc(0).getFile(), r, -1, c,
                          src0_component_offset);
         }

         Instruction *ld = mkLoad(TYPE_U32, dst0[c], sym, off);
         if (tgsi.getSrc(0).getFile() == TGSI_FILE_BUFFER &&
             code->bufferAtomics[r])
            ld->cache = nv50_ir::CACHE_CG;
         else
            ld->cache = tgsi.getCacheMode();
         if (ind)
            ld->setIndirect(0, 1, ind);
      }
      break;
   default: {
      getImageCoords(off, 1);
      def.resize(4);

      for (c = 0; c < 4; ++c) {
         if (!dst0[c] || tgsi.getSrc(0).getSwizzle(c) != (TGSI_SWIZZLE_X + c))
            def[c] = getScratch();
         else
            def[c] = dst0[c];
      }

      bool bindless = tgsi.getSrc(0).getFile() != TGSI_FILE_IMAGE;
      if (bindless)
         ind = fetchSrc(0, 0);

      TexInstruction *ld =
         mkTex(OP_SULDP, tgsi.getImageTarget(), 0, 0, def, off);
      ld->tex.mask = tgsi.getDst(0).getMask();
      ld->tex.format = tgsi.getImageFormat();
      ld->cache = tgsi.getCacheMode();
      ld->tex.bindless = bindless;
      if (!bindless)
         ld->tex.r = r;
      if (ind)
         ld->setIndirectR(ind);

      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
         if (dst0[c] != def[c])
            mkMov(dst0[c], def[tgsi.getSrc(0).getSwizzle(c)]);
      break;
   }
   }


/* Keep this around for now as reference when adding img support
   getResourceCoords(off, r, 1);

   if (isResourceRaw(code, r)) {
      uint8_t mask = 0;
      uint8_t comp[2] = { 0, 0 };
      uint8_t size[2] = { 0, 0 };

      Symbol *base = getResourceBase(r);

      // determine the base and size of the at most 2 load ops
      for (c = 0; c < 4; ++c)
         if (!tgsi.getDst(0).isMasked(c))
            mask |= 1 << (tgsi.getSrc(0).getSwizzle(c) - TGSI_SWIZZLE_X);

      int n = partitionLoadStore(comp, size, mask);

      src = off;

      def.resize(4); // index by component, the ones we need will be non-NULL
      for (c = 0; c < 4; ++c) {
         if (dst0[c] && tgsi.getSrc(0).getSwizzle(c) == (TGSI_SWIZZLE_X + c))
            def[c] = dst0[c];
         else
         if (mask & (1 << c))
            def[c] = getScratch();
      }

      const bool useLd = isResourceSpecial(r) ||
         (info->io.nv50styleSurfaces &&
          code->resources[r].target == TGSI_TEXTURE_BUFFER);

      for (int i = 0; i < n; ++i) {
         ldv.assign(def.begin() + comp[i], def.begin() + comp[i] + size[i]);

         if (comp[i]) // adjust x component of source address if necessary
            src[0] = mkOp2v(OP_ADD, TYPE_U32, getSSA(4, off[0]->reg.file),
                            off[0], mkImm(comp[i] * 4));
         else
            src[0] = off[0];

         if (useLd) {
            Instruction *ld =
               mkLoad(typeOfSize(size[i] * 4), ldv[0], base, src[0]);
            for (size_t c = 1; c < ldv.size(); ++c)
               ld->setDef(c, ldv[c]);
         } else {
            mkTex(OP_SULDB, getResourceTarget(code, r), code->resources[r].slot,
                  0, ldv, src)->dType = typeOfSize(size[i] * 4);
         }
      }
   } else {
      def.resize(4);
      for (c = 0; c < 4; ++c) {
         if (!dst0[c] || tgsi.getSrc(0).getSwizzle(c) != (TGSI_SWIZZLE_X + c))
            def[c] = getScratch();
         else
            def[c] = dst0[c];
      }

      mkTex(OP_SULDP, getResourceTarget(code, r), code->resources[r].slot, 0,
            def, off);
   }
   FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
      if (dst0[c] != def[c])
         mkMov(dst0[c], def[tgsi.getSrc(0).getSwizzle(c)]);
*/
}

// For formatted stores, the write mask on OP_SUSTP can be used.
// Raw stores have to be split.
void
Converter::handleSTORE()
{
   const int r = tgsi.getDst(0).getIndex(0);
   int c;
   std::vector<Value *> off, src, dummy;
   Value *ind = NULL;

   if (tgsi.getDst(0).isIndirect(0))
      ind = fetchSrc(tgsi.getDst(0).getIndirect(0), 0, 0);

   switch (tgsi.getDst(0).getFile()) {
   case TGSI_FILE_BUFFER:
   case TGSI_FILE_MEMORY:
      for (c = 0; c < 4; ++c) {
         if (!(tgsi.getDst(0).getMask() & (1 << c)))
            continue;

         Symbol *sym;
         Value *off;
         if (tgsi.getSrc(0).getFile() == TGSI_FILE_IMMEDIATE) {
            off = NULL;
            sym = makeSym(tgsi.getDst(0).getFile(), r, -1, c,
                          tgsi.getSrc(0).getValueU32(0, info) + 4 * c);
         } else {
            // yzw are ignored for buffers
            off = fetchSrc(0, 0);
            sym = makeSym(tgsi.getDst(0).getFile(), r, -1, c, 4 * c);
         }

         Instruction *st = mkStore(OP_STORE, TYPE_U32, sym, off, fetchSrc(1, c));
         st->cache = tgsi.getCacheMode();
         if (ind)
            st->setIndirect(0, 1, ind);
      }
      break;
   default: {
      getImageCoords(off, 0);
      src = off;

      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
         src.push_back(fetchSrc(1, c));

      bool bindless = tgsi.getDst(0).getFile() != TGSI_FILE_IMAGE;
      if (bindless)
         ind = fetchDst(0, 0);

      TexInstruction *st =
         mkTex(OP_SUSTP, tgsi.getImageTarget(), 0, 0, dummy, src);
      st->tex.mask = tgsi.getDst(0).getMask();
      st->tex.format = tgsi.getImageFormat();
      st->cache = tgsi.getCacheMode();
      st->tex.bindless = bindless;
      if (!bindless)
         st->tex.r = r;
      if (ind)
         st->setIndirectR(ind);

      break;
   }
   }

/* Keep this around for now as reference when adding img support
   getResourceCoords(off, r, 0);
   src = off;
   const int s = src.size();

   if (isResourceRaw(code, r)) {
      uint8_t comp[2] = { 0, 0 };
      uint8_t size[2] = { 0, 0 };

      int n = partitionLoadStore(comp, size, tgsi.getDst(0).getMask());

      Symbol *base = getResourceBase(r);

      const bool useSt = isResourceSpecial(r) ||
         (info->io.nv50styleSurfaces &&
          code->resources[r].target == TGSI_TEXTURE_BUFFER);

      for (int i = 0; i < n; ++i) {
         if (comp[i]) // adjust x component of source address if necessary
            src[0] = mkOp2v(OP_ADD, TYPE_U32, getSSA(4, off[0]->reg.file),
                            off[0], mkImm(comp[i] * 4));
         else
            src[0] = off[0];

         const DataType stTy = typeOfSize(size[i] * 4);

         if (useSt) {
            Instruction *st =
               mkStore(OP_STORE, stTy, base, NULL, fetchSrc(1, comp[i]));
            for (c = 1; c < size[i]; ++c)
               st->setSrc(1 + c, fetchSrc(1, comp[i] + c));
            st->setIndirect(0, 0, src[0]);
         } else {
            // attach values to be stored
            src.resize(s + size[i]);
            for (c = 0; c < size[i]; ++c)
               src[s + c] = fetchSrc(1, comp[i] + c);
            mkTex(OP_SUSTB, getResourceTarget(code, r), code->resources[r].slot,
                  0, dummy, src)->setType(stTy);
         }
      }
   } else {
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
         src.push_back(fetchSrc(1, c));

      mkTex(OP_SUSTP, getResourceTarget(code, r), code->resources[r].slot, 0,
            dummy, src)->tex.mask = tgsi.getDst(0).getMask();
   }
*/
}

// XXX: These only work on resources with the single-component u32/s32 formats.
// Therefore the result is replicated. This might not be intended by TGSI, but
// operating on more than 1 component would produce undefined results because
// they do not exist.
void
Converter::handleATOM(Value *dst0[4], DataType ty, uint16_t subOp)
{
   const int r = tgsi.getSrc(0).getIndex(0);
   std::vector<Value *> srcv;
   std::vector<Value *> defv;
   LValue *dst = getScratch();
   Value *ind = NULL;

   if (tgsi.getSrc(0).isIndirect(0))
      ind = fetchSrc(tgsi.getSrc(0).getIndirect(0), 0, 0);

   switch (tgsi.getSrc(0).getFile()) {
   case TGSI_FILE_BUFFER:
   case TGSI_FILE_MEMORY:
      for (int c = 0; c < 4; ++c) {
         if (!dst0[c])
            continue;

         Instruction *insn;
         Value *off = fetchSrc(1, c);
         Value *sym;
         if (tgsi.getSrc(1).getFile() == TGSI_FILE_IMMEDIATE)
            sym = makeSym(tgsi.getSrc(0).getFile(), r, -1, c,
                          tgsi.getSrc(1).getValueU32(c, info));
         else
            sym = makeSym(tgsi.getSrc(0).getFile(), r, -1, c, 0);
         if (subOp == NV50_IR_SUBOP_ATOM_CAS)
            insn = mkOp3(OP_ATOM, ty, dst, sym, fetchSrc(2, c), fetchSrc(3, c));
         else
            insn = mkOp2(OP_ATOM, ty, dst, sym, fetchSrc(2, c));
         if (tgsi.getSrc(1).getFile() != TGSI_FILE_IMMEDIATE)
            insn->setIndirect(0, 0, off);
         if (ind)
            insn->setIndirect(0, 1, ind);
         insn->subOp = subOp;
      }
      for (int c = 0; c < 4; ++c)
         if (dst0[c])
            dst0[c] = dst; // not equal to rDst so handleInstruction will do mkMov
      break;
   default: {
      getImageCoords(srcv, 1);
      defv.push_back(dst);
      srcv.push_back(fetchSrc(2, 0));

      if (subOp == NV50_IR_SUBOP_ATOM_CAS)
         srcv.push_back(fetchSrc(3, 0));

      bool bindless = tgsi.getSrc(0).getFile() != TGSI_FILE_IMAGE;
      if (bindless)
         ind = fetchSrc(0, 0);

      TexInstruction *tex = mkTex(OP_SUREDP, tgsi.getImageTarget(),
                                  0, 0, defv, srcv);
      tex->subOp = subOp;
      tex->tex.mask = 1;
      tex->tex.format = tgsi.getImageFormat();
      tex->setType(ty);
      tex->tex.bindless = bindless;
      if (!bindless)
         tex->tex.r = r;
      if (ind)
         tex->setIndirectR(ind);

      for (int c = 0; c < 4; ++c)
         if (dst0[c])
            dst0[c] = dst; // not equal to rDst so handleInstruction will do mkMov
      break;
   }
   }

/* Keep this around for now as reference when adding img support
   getResourceCoords(srcv, r, 1);

   if (isResourceSpecial(r)) {
      assert(r != TGSI_RESOURCE_INPUT);
      Instruction *insn;
      insn = mkOp2(OP_ATOM, ty, dst, getResourceBase(r), fetchSrc(2, 0));
      insn->subOp = subOp;
      if (subOp == NV50_IR_SUBOP_ATOM_CAS)
         insn->setSrc(2, fetchSrc(3, 0));
      insn->setIndirect(0, 0, srcv.at(0));
   } else {
      operation op = isResourceRaw(code, r) ? OP_SUREDB : OP_SUREDP;
      TexTarget targ = getResourceTarget(code, r);
      int idx = code->resources[r].slot;
      defv.push_back(dst);
      srcv.push_back(fetchSrc(2, 0));
      if (subOp == NV50_IR_SUBOP_ATOM_CAS)
         srcv.push_back(fetchSrc(3, 0));
      TexInstruction *tex = mkTex(op, targ, idx, 0, defv, srcv);
      tex->subOp = subOp;
      tex->tex.mask = 1;
      tex->setType(ty);
   }

   for (int c = 0; c < 4; ++c)
      if (dst0[c])
         dst0[c] = dst; // not equal to rDst so handleInstruction will do mkMov
*/
}

void
Converter::handleINTERP(Value *dst[4])
{
   // Check whether the input is linear. All other attributes ignored.
   Instruction *insn;
   Value *offset = NULL, *ptr = NULL, *w = NULL;
   Symbol *sym[4] = { NULL };
   bool linear;
   operation op = OP_NOP;
   int c, mode = 0;

   tgsi::Instruction::SrcRegister src = tgsi.getSrc(0);

   // In some odd cases, in large part due to varying packing, the source
   // might not actually be an input. This is illegal TGSI, but it's easier to
   // account for it here than it is to fix it where the TGSI is being
   // generated. In that case, it's going to be a straight up mov (or sequence
   // of mov's) from the input in question. We follow the mov chain to see
   // which input we need to use.
   if (src.getFile() != TGSI_FILE_INPUT) {
      if (src.isIndirect(0)) {
         ERROR("Ignoring indirect input interpolation\n");
         return;
      }
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         Value *val = fetchSrc(0, c);
         assert(val->defs.size() == 1);
         insn = val->getInsn();
         while (insn->op == OP_MOV) {
            assert(insn->getSrc(0)->defs.size() == 1);
            insn = insn->getSrc(0)->getInsn();
            if (!insn) {
               ERROR("Miscompiling shader due to unhandled INTERP\n");
               return;
            }
         }
         if (insn->op != OP_LINTERP && insn->op != OP_PINTERP) {
            ERROR("Trying to interpolate non-input, this is not allowed.\n");
            return;
         }
         sym[c] = insn->getSrc(0)->asSym();
         assert(sym[c]);
         op = insn->op;
         mode = insn->ipa;
         ptr = insn->getIndirect(0, 0);
      }
   } else {
      if (src.isIndirect(0))
         ptr = shiftAddress(fetchSrc(src.getIndirect(0), 0, NULL));

      // We can assume that the fixed index will point to an input of the same
      // interpolation type in case of an indirect.
      // TODO: Make use of ArrayID.
      linear = info->in[src.getIndex(0)].linear;
      if (linear) {
         op = OP_LINTERP;
         mode = NV50_IR_INTERP_LINEAR;
      } else {
         op = OP_PINTERP;
         mode = NV50_IR_INTERP_PERSPECTIVE;
      }
   }

   switch (tgsi.getOpcode()) {
   case TGSI_OPCODE_INTERP_CENTROID:
      mode |= NV50_IR_INTERP_CENTROID;
      break;
   case TGSI_OPCODE_INTERP_SAMPLE:
      insn = mkOp1(OP_PIXLD, TYPE_U32, (offset = getScratch()), fetchSrc(1, 0));
      insn->subOp = NV50_IR_SUBOP_PIXLD_OFFSET;
      mode |= NV50_IR_INTERP_OFFSET;
      break;
   case TGSI_OPCODE_INTERP_OFFSET: {
      // The input in src1.xy is float, but we need a single 32-bit value
      // where the upper and lower 16 bits are encoded in S0.12 format. We need
      // to clamp the input coordinates to (-0.5, 0.4375), multiply by 4096,
      // and then convert to s32.
      Value *offs[2];
      for (c = 0; c < 2; c++) {
         offs[c] = getScratch();
         mkOp2(OP_MIN, TYPE_F32, offs[c], fetchSrc(1, c), loadImm(NULL, 0.4375f));
         mkOp2(OP_MAX, TYPE_F32, offs[c], offs[c], loadImm(NULL, -0.5f));
         mkOp2(OP_MUL, TYPE_F32, offs[c], offs[c], loadImm(NULL, 4096.0f));
         mkCvt(OP_CVT, TYPE_S32, offs[c], TYPE_F32, offs[c]);
      }
      offset = mkOp3v(OP_INSBF, TYPE_U32, getScratch(),
                      offs[1], mkImm(0x1010), offs[0]);
      mode |= NV50_IR_INTERP_OFFSET;
      break;
   }
   }

   if (op == OP_PINTERP) {
      if (offset) {
         w = mkOp2v(OP_RDSV, TYPE_F32, getSSA(), mkSysVal(SV_POSITION, 3), offset);
         mkOp1(OP_RCP, TYPE_F32, w, w);
      } else {
         w = fragCoord[3];
      }
   }


   FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
      insn = mkOp1(op, TYPE_F32, dst[c], sym[c] ? sym[c] : srcToSym(src, c));
      if (op == OP_PINTERP)
         insn->setSrc(1, w);
      if (offset)
         insn->setSrc(op == OP_PINTERP ? 2 : 1, offset);
      if (ptr)
         insn->setIndirect(0, 0, ptr);

      insn->setInterpolate(mode);
   }
}

Converter::Subroutine *
Converter::getSubroutine(unsigned ip)
{
   std::map<unsigned, Subroutine>::iterator it = sub.map.find(ip);

   if (it == sub.map.end())
      it = sub.map.insert(std::make_pair(
              ip, Subroutine(new Function(prog, "SUB", ip)))).first;

   return &it->second;
}

Converter::Subroutine *
Converter::getSubroutine(Function *f)
{
   unsigned ip = f->getLabel();
   std::map<unsigned, Subroutine>::iterator it = sub.map.find(ip);

   if (it == sub.map.end())
      it = sub.map.insert(std::make_pair(ip, Subroutine(f))).first;

   return &it->second;
}

bool
Converter::isEndOfSubroutine(uint ip)
{
   assert(ip < code->scan.num_instructions);
   tgsi::Instruction insn(&code->insns[ip]);
   return (insn.getOpcode() == TGSI_OPCODE_END ||
           insn.getOpcode() == TGSI_OPCODE_ENDSUB ||
           // does END occur at end of main or the very end ?
           insn.getOpcode() == TGSI_OPCODE_BGNSUB);
}

bool
Converter::handleInstruction(const struct tgsi_full_instruction *insn)
{
   Instruction *geni;

   Value *dst0[4], *rDst0[4];
   Value *src0, *src1, *src2, *src3;
   Value *val0, *val1;
   int c;

   tgsi = tgsi::Instruction(insn);

   bool useScratchDst = tgsi.checkDstSrcAliasing();

   operation op = tgsi.getOP();
   dstTy = tgsi.inferDstType();
   srcTy = tgsi.inferSrcType();

   unsigned int mask = tgsi.dstCount() ? tgsi.getDst(0).getMask() : 0;

   if (tgsi.dstCount() && tgsi.getOpcode() != TGSI_OPCODE_STORE) {
      for (c = 0; c < 4; ++c) {
         rDst0[c] = acquireDst(0, c);
         dst0[c] = (useScratchDst && rDst0[c]) ? getScratch() : rDst0[c];
      }
   }

   switch (tgsi.getOpcode()) {
   case TGSI_OPCODE_ADD:
   case TGSI_OPCODE_UADD:
   case TGSI_OPCODE_AND:
   case TGSI_OPCODE_DIV:
   case TGSI_OPCODE_IDIV:
   case TGSI_OPCODE_UDIV:
   case TGSI_OPCODE_MAX:
   case TGSI_OPCODE_MIN:
   case TGSI_OPCODE_IMAX:
   case TGSI_OPCODE_IMIN:
   case TGSI_OPCODE_UMAX:
   case TGSI_OPCODE_UMIN:
   case TGSI_OPCODE_MOD:
   case TGSI_OPCODE_UMOD:
   case TGSI_OPCODE_MUL:
   case TGSI_OPCODE_UMUL:
   case TGSI_OPCODE_IMUL_HI:
   case TGSI_OPCODE_UMUL_HI:
   case TGSI_OPCODE_OR:
   case TGSI_OPCODE_SHL:
   case TGSI_OPCODE_ISHR:
   case TGSI_OPCODE_USHR:
   case TGSI_OPCODE_XOR:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = fetchSrc(0, c);
         src1 = fetchSrc(1, c);
         geni = mkOp2(op, dstTy, dst0[c], src0, src1);
         geni->subOp = tgsi::opcodeToSubOp(tgsi.getOpcode());
         if (op == OP_MUL && dstTy == TYPE_F32)
            geni->dnz = info->io.mul_zero_wins;
         geni->precise = insn->Instruction.Precise;
      }
      break;
   case TGSI_OPCODE_MAD:
   case TGSI_OPCODE_UMAD:
   case TGSI_OPCODE_FMA:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = fetchSrc(0, c);
         src1 = fetchSrc(1, c);
         src2 = fetchSrc(2, c);
         geni = mkOp3(op, dstTy, dst0[c], src0, src1, src2);
         if (dstTy == TYPE_F32)
            geni->dnz = info->io.mul_zero_wins;
         geni->precise = insn->Instruction.Precise;
      }
      break;
   case TGSI_OPCODE_MOV:
   case TGSI_OPCODE_CEIL:
   case TGSI_OPCODE_FLR:
   case TGSI_OPCODE_TRUNC:
   case TGSI_OPCODE_RCP:
   case TGSI_OPCODE_SQRT:
   case TGSI_OPCODE_IABS:
   case TGSI_OPCODE_INEG:
   case TGSI_OPCODE_NOT:
   case TGSI_OPCODE_DDX:
   case TGSI_OPCODE_DDY:
   case TGSI_OPCODE_DDX_FINE:
   case TGSI_OPCODE_DDY_FINE:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
         mkOp1(op, dstTy, dst0[c], fetchSrc(0, c));
      break;
   case TGSI_OPCODE_RSQ:
      src0 = fetchSrc(0, 0);
      val0 = getScratch();
      mkOp1(OP_ABS, TYPE_F32, val0, src0);
      mkOp1(OP_RSQ, TYPE_F32, val0, val0);
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
         mkMov(dst0[c], val0);
      break;
   case TGSI_OPCODE_ARL:
   case TGSI_OPCODE_ARR:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         const RoundMode rnd =
            tgsi.getOpcode() == TGSI_OPCODE_ARR ? ROUND_N : ROUND_M;
         src0 = fetchSrc(0, c);
         mkCvt(OP_CVT, TYPE_S32, dst0[c], TYPE_F32, src0)->rnd = rnd;
      }
      break;
   case TGSI_OPCODE_UARL:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
         mkOp1(OP_MOV, TYPE_U32, dst0[c], fetchSrc(0, c));
      break;
   case TGSI_OPCODE_POW:
      val0 = mkOp2v(op, TYPE_F32, getScratch(), fetchSrc(0, 0), fetchSrc(1, 0));
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
         mkOp1(OP_MOV, TYPE_F32, dst0[c], val0);
      break;
   case TGSI_OPCODE_EX2:
   case TGSI_OPCODE_LG2:
      val0 = mkOp1(op, TYPE_F32, getScratch(), fetchSrc(0, 0))->getDef(0);
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
         mkOp1(OP_MOV, TYPE_F32, dst0[c], val0);
      break;
   case TGSI_OPCODE_COS:
   case TGSI_OPCODE_SIN:
      val0 = getScratch();
      if (mask & 7) {
         mkOp1(OP_PRESIN, TYPE_F32, val0, fetchSrc(0, 0));
         mkOp1(op, TYPE_F32, val0, val0);
         for (c = 0; c < 3; ++c)
            if (dst0[c])
               mkMov(dst0[c], val0);
      }
      if (dst0[3]) {
         mkOp1(OP_PRESIN, TYPE_F32, val0, fetchSrc(0, 3));
         mkOp1(op, TYPE_F32, dst0[3], val0);
      }
      break;
   case TGSI_OPCODE_EXP:
      src0 = fetchSrc(0, 0);
      val0 = mkOp1v(OP_FLOOR, TYPE_F32, getSSA(), src0);
      if (dst0[1])
         mkOp2(OP_SUB, TYPE_F32, dst0[1], src0, val0);
      if (dst0[0])
         mkOp1(OP_EX2, TYPE_F32, dst0[0], val0);
      if (dst0[2])
         mkOp1(OP_EX2, TYPE_F32, dst0[2], src0);
      if (dst0[3])
         loadImm(dst0[3], 1.0f);
      break;
   case TGSI_OPCODE_LOG:
      src0 = mkOp1v(OP_ABS, TYPE_F32, getSSA(), fetchSrc(0, 0));
      val0 = mkOp1v(OP_LG2, TYPE_F32, dst0[2] ? dst0[2] : getSSA(), src0);
      if (dst0[0] || dst0[1])
         val1 = mkOp1v(OP_FLOOR, TYPE_F32, dst0[0] ? dst0[0] : getSSA(), val0);
      if (dst0[1]) {
         mkOp1(OP_EX2, TYPE_F32, dst0[1], val1);
         mkOp1(OP_RCP, TYPE_F32, dst0[1], dst0[1]);
         mkOp2(OP_MUL, TYPE_F32, dst0[1], dst0[1], src0)
            ->dnz = info->io.mul_zero_wins;
      }
      if (dst0[3])
         loadImm(dst0[3], 1.0f);
      break;
   case TGSI_OPCODE_DP2:
      val0 = buildDot(2);
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
         mkMov(dst0[c], val0);
      break;
   case TGSI_OPCODE_DP3:
      val0 = buildDot(3);
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
         mkMov(dst0[c], val0);
      break;
   case TGSI_OPCODE_DP4:
      val0 = buildDot(4);
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
         mkMov(dst0[c], val0);
      break;
   case TGSI_OPCODE_DST:
      if (dst0[0])
         loadImm(dst0[0], 1.0f);
      if (dst0[1]) {
         src0 = fetchSrc(0, 1);
         src1 = fetchSrc(1, 1);
         mkOp2(OP_MUL, TYPE_F32, dst0[1], src0, src1)
            ->dnz = info->io.mul_zero_wins;
      }
      if (dst0[2])
         mkMov(dst0[2], fetchSrc(0, 2));
      if (dst0[3])
         mkMov(dst0[3], fetchSrc(1, 3));
      break;
   case TGSI_OPCODE_LRP:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = fetchSrc(0, c);
         src1 = fetchSrc(1, c);
         src2 = fetchSrc(2, c);
         mkOp3(OP_MAD, TYPE_F32, dst0[c],
               mkOp2v(OP_SUB, TYPE_F32, getSSA(), src1, src2), src0, src2)
            ->dnz = info->io.mul_zero_wins;
      }
      break;
   case TGSI_OPCODE_LIT:
      handleLIT(dst0);
      break;
   case TGSI_OPCODE_ISSG:
   case TGSI_OPCODE_SSG:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = fetchSrc(0, c);
         val0 = getScratch();
         val1 = getScratch();
         mkCmp(OP_SET, CC_GT, srcTy, val0, srcTy, src0, zero);
         mkCmp(OP_SET, CC_LT, srcTy, val1, srcTy, src0, zero);
         if (srcTy == TYPE_F32)
            mkOp2(OP_SUB, TYPE_F32, dst0[c], val0, val1);
         else
            mkOp2(OP_SUB, TYPE_S32, dst0[c], val1, val0);
      }
      break;
   case TGSI_OPCODE_UCMP:
      srcTy = TYPE_U32;
      /* fallthrough */
   case TGSI_OPCODE_CMP:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = fetchSrc(0, c);
         src1 = fetchSrc(1, c);
         src2 = fetchSrc(2, c);
         if (src1 == src2)
            mkMov(dst0[c], src1);
         else
            mkCmp(OP_SLCT, (srcTy == TYPE_F32) ? CC_LT : CC_NE,
                  srcTy, dst0[c], srcTy, src1, src2, src0);
      }
      break;
   case TGSI_OPCODE_FRC:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = fetchSrc(0, c);
         val0 = getScratch();
         mkOp1(OP_FLOOR, TYPE_F32, val0, src0);
         mkOp2(OP_SUB, TYPE_F32, dst0[c], src0, val0);
      }
      break;
   case TGSI_OPCODE_ROUND:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
         mkCvt(OP_CVT, TYPE_F32, dst0[c], TYPE_F32, fetchSrc(0, c))
         ->rnd = ROUND_NI;
      break;
   case TGSI_OPCODE_SLT:
   case TGSI_OPCODE_SGE:
   case TGSI_OPCODE_SEQ:
   case TGSI_OPCODE_SGT:
   case TGSI_OPCODE_SLE:
   case TGSI_OPCODE_SNE:
   case TGSI_OPCODE_FSEQ:
   case TGSI_OPCODE_FSGE:
   case TGSI_OPCODE_FSLT:
   case TGSI_OPCODE_FSNE:
   case TGSI_OPCODE_ISGE:
   case TGSI_OPCODE_ISLT:
   case TGSI_OPCODE_USEQ:
   case TGSI_OPCODE_USGE:
   case TGSI_OPCODE_USLT:
   case TGSI_OPCODE_USNE:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = fetchSrc(0, c);
         src1 = fetchSrc(1, c);
         mkCmp(op, tgsi.getSetCond(), dstTy, dst0[c], srcTy, src0, src1);
      }
      break;
   case TGSI_OPCODE_VOTE_ALL:
   case TGSI_OPCODE_VOTE_ANY:
   case TGSI_OPCODE_VOTE_EQ:
      val0 = new_LValue(func, FILE_PREDICATE);
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         mkCmp(OP_SET, CC_NE, TYPE_U32, val0, TYPE_U32, fetchSrc(0, c), zero);
         mkOp1(op, dstTy, val0, val0)
            ->subOp = tgsi::opcodeToSubOp(tgsi.getOpcode());
         mkCvt(OP_CVT, TYPE_U32, dst0[c], TYPE_U8, val0);
      }
      break;
   case TGSI_OPCODE_BALLOT:
      if (!tgsi.getDst(0).isMasked(0)) {
         // fincs-edit start: Help codegen by detecting if the source operand is an immediate,
         // and convert that directly to PT or ~PT in OP_VOTE instead of going through OP_SET.
         if (tgsi.getSrc(0).getFile() == TGSI_FILE_IMMEDIATE) {
            val0 = mkImm(info->immd.data[tgsi.getSrc(0).getIndex(0)*4 + tgsi.getSrc(0).getSwizzle(0)] != 0);
         } else {
            val0 = new_LValue(func, FILE_PREDICATE);
            mkCmp(OP_SET, CC_NE, TYPE_U32, val0, TYPE_U32, fetchSrc(0,0), zero);
         }
         // fincs-edit end
         mkOp1(op, TYPE_U32, dst0[0], val0)->subOp = NV50_IR_SUBOP_VOTE_ANY;
      }
      if (!tgsi.getDst(0).isMasked(1))
         mkMov(dst0[1], zero, TYPE_U32);
      break;
   case TGSI_OPCODE_READ_FIRST:
      // ReadFirstInvocationARB(src) is implemented as
      // ReadInvocationARB(src, findLSB(ballot(true)))
      val0 = getScratch();
      mkOp1(OP_VOTE, TYPE_U32, val0, mkImm(1))->subOp = NV50_IR_SUBOP_VOTE_ANY;
      mkOp2(OP_EXTBF, TYPE_U32, val0, val0, mkImm(0x2000))
         ->subOp = NV50_IR_SUBOP_EXTBF_REV;
      mkOp1(OP_BFIND, TYPE_U32, val0, val0)->subOp = NV50_IR_SUBOP_BFIND_SAMT;
      src1 = val0;
      /* fallthrough */
   case TGSI_OPCODE_READ_INVOC:
      if (tgsi.getOpcode() == TGSI_OPCODE_READ_INVOC)
         src1 = fetchSrc(1, 0);
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         geni = mkOp3(op, dstTy, dst0[c], fetchSrc(0, c), src1, mkImm(0x1f));
         geni->subOp = NV50_IR_SUBOP_SHFL_IDX;
      }
      break;
   case TGSI_OPCODE_CLOCK:
      // Stick the 32-bit clock into the high dword of the logical result.
      if (!tgsi.getDst(0).isMasked(0))
         mkOp1(OP_MOV, TYPE_U32, dst0[0], zero);
      if (!tgsi.getDst(0).isMasked(1))
         mkOp1(OP_RDSV, TYPE_U32, dst0[1], mkSysVal(SV_CLOCK, 0))->fixed = 1;
      break;
   case TGSI_OPCODE_KILL_IF:
      val0 = new_LValue(func, FILE_PREDICATE);
      mask = 0;
      for (c = 0; c < 4; ++c) {
         const int s = tgsi.getSrc(0).getSwizzle(c);
         if (mask & (1 << s))
            continue;
         mask |= 1 << s;
         mkCmp(OP_SET, CC_LT, TYPE_F32, val0, TYPE_F32, fetchSrc(0, c), zero);
         mkOp(OP_DISCARD, TYPE_NONE, NULL)->setPredicate(CC_P, val0);
      }
      break;
   case TGSI_OPCODE_KILL:
      mkOp(OP_DISCARD, TYPE_NONE, NULL);
      break;
   case TGSI_OPCODE_TEX:
   case TGSI_OPCODE_TEX_LZ:
   case TGSI_OPCODE_TXB:
   case TGSI_OPCODE_TXL:
   case TGSI_OPCODE_TXP:
   case TGSI_OPCODE_LODQ:
      //              R  S     L     C    Dx    Dy
      handleTEX(dst0, 1, 1, 0x03, 0x0f, 0x00, 0x00);
      break;
   case TGSI_OPCODE_TXD:
      handleTEX(dst0, 3, 3, 0x03, 0x0f, 0x10, 0x20);
      break;
   case TGSI_OPCODE_TG4:
      handleTEX(dst0, 2, 2, 0x03, 0x0f, 0x00, 0x00);
      break;
   case TGSI_OPCODE_TEX2:
      handleTEX(dst0, 2, 2, 0x03, 0x10, 0x00, 0x00);
      break;
   case TGSI_OPCODE_TXB2:
   case TGSI_OPCODE_TXL2:
      handleTEX(dst0, 2, 2, 0x10, 0x0f, 0x00, 0x00);
      break;
   case TGSI_OPCODE_SAMPLE:
   case TGSI_OPCODE_SAMPLE_B:
   case TGSI_OPCODE_SAMPLE_D:
   case TGSI_OPCODE_SAMPLE_L:
   case TGSI_OPCODE_SAMPLE_C:
   case TGSI_OPCODE_SAMPLE_C_LZ:
      handleTEX(dst0, 1, 2, 0x30, 0x30, 0x30, 0x40);
      break;
   case TGSI_OPCODE_TXF_LZ:
   case TGSI_OPCODE_TXF:
      handleTXF(dst0, 1, 0x03);
      break;
   case TGSI_OPCODE_SAMPLE_I:
      handleTXF(dst0, 1, 0x03);
      break;
   case TGSI_OPCODE_SAMPLE_I_MS:
      handleTXF(dst0, 1, 0x20);
      break;
   case TGSI_OPCODE_TXQ:
   case TGSI_OPCODE_SVIEWINFO:
      handleTXQ(dst0, TXQ_DIMS, 1);
      break;
   case TGSI_OPCODE_TXQS:
      // The TXQ_TYPE query returns samples in its 3rd arg, but we need it to
      // be in .x
      dst0[1] = dst0[2] = dst0[3] = NULL;
      std::swap(dst0[0], dst0[2]);
      handleTXQ(dst0, TXQ_TYPE, 0);
      std::swap(dst0[0], dst0[2]);
      break;
   case TGSI_OPCODE_FBFETCH:
      handleFBFETCH(dst0);
      break;
   case TGSI_OPCODE_F2I:
   case TGSI_OPCODE_F2U:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
         mkCvt(OP_CVT, dstTy, dst0[c], srcTy, fetchSrc(0, c))->rnd = ROUND_Z;
      break;
   case TGSI_OPCODE_I2F:
   case TGSI_OPCODE_U2F:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
         mkCvt(OP_CVT, dstTy, dst0[c], srcTy, fetchSrc(0, c));
      break;
   case TGSI_OPCODE_PK2H:
      val0 = getScratch();
      val1 = getScratch();
      mkCvt(OP_CVT, TYPE_F16, val0, TYPE_F32, fetchSrc(0, 0));
      mkCvt(OP_CVT, TYPE_F16, val1, TYPE_F32, fetchSrc(0, 1));
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi)
         mkOp3(OP_INSBF, TYPE_U32, dst0[c], val1, mkImm(0x1010), val0);
      break;
   case TGSI_OPCODE_UP2H:
      src0 = fetchSrc(0, 0);
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         geni = mkCvt(OP_CVT, TYPE_F32, dst0[c], TYPE_F16, src0);
         geni->subOp = c & 1;
      }
      break;
   case TGSI_OPCODE_EMIT:
      /* export the saved viewport index */
      if (viewport != NULL) {
         Symbol *vpSym = mkSymbol(FILE_SHADER_OUTPUT, 0, TYPE_U32,
                                  info->out[info->io.viewportId].slot[0] * 4);
         mkStore(OP_EXPORT, TYPE_U32, vpSym, NULL, viewport);
      }
      /* handle user clip planes for each emitted vertex */
      if (info->io.genUserClip > 0)
         handleUserClipPlanes();
      /* fallthrough */
   case TGSI_OPCODE_ENDPRIM:
   {
      // get vertex stream (must be immediate)
      unsigned int stream = tgsi.getSrc(0).getValueU32(0, info);
      if (stream && op == OP_RESTART)
         break;
      if (info->prop.gp.maxVertices == 0)
         break;
      src0 = mkImm(stream);
      mkOp1(op, TYPE_U32, NULL, src0)->fixed = 1;
      break;
   }
   case TGSI_OPCODE_IF:
   case TGSI_OPCODE_UIF:
   {
      BasicBlock *ifBB = new BasicBlock(func);

      bb->cfg.attach(&ifBB->cfg, Graph::Edge::TREE);
      condBBs.push(bb);
      joinBBs.push(bb);

      mkFlow(OP_BRA, NULL, CC_NOT_P, fetchSrc(0, 0))->setType(srcTy);

      setPosition(ifBB, true);
   }
      break;
   case TGSI_OPCODE_ELSE:
   {
      BasicBlock *elseBB = new BasicBlock(func);
      BasicBlock *forkBB = reinterpret_cast<BasicBlock *>(condBBs.pop().u.p);

      forkBB->cfg.attach(&elseBB->cfg, Graph::Edge::TREE);
      condBBs.push(bb);

      forkBB->getExit()->asFlow()->target.bb = elseBB;
      if (!bb->isTerminated())
         mkFlow(OP_BRA, NULL, CC_ALWAYS, NULL);

      setPosition(elseBB, true);
   }
      break;
   case TGSI_OPCODE_ENDIF:
   {
      BasicBlock *convBB = new BasicBlock(func);
      BasicBlock *prevBB = reinterpret_cast<BasicBlock *>(condBBs.pop().u.p);
      BasicBlock *forkBB = reinterpret_cast<BasicBlock *>(joinBBs.pop().u.p);

      if (!bb->isTerminated()) {
         // we only want join if none of the clauses ended with CONT/BREAK/RET
         if (prevBB->getExit()->op == OP_BRA && joinBBs.getSize() < 6)
            insertConvergenceOps(convBB, forkBB);
         mkFlow(OP_BRA, convBB, CC_ALWAYS, NULL);
         bb->cfg.attach(&convBB->cfg, Graph::Edge::FORWARD);
      }

      if (prevBB->getExit()->op == OP_BRA) {
         prevBB->cfg.attach(&convBB->cfg, Graph::Edge::FORWARD);
         prevBB->getExit()->asFlow()->target.bb = convBB;
      }
      setPosition(convBB, true);
   }
      break;
   case TGSI_OPCODE_BGNLOOP:
   {
      BasicBlock *lbgnBB = new BasicBlock(func);
      BasicBlock *lbrkBB = new BasicBlock(func);

      loopBBs.push(lbgnBB);
      breakBBs.push(lbrkBB);
      if (loopBBs.getSize() > func->loopNestingBound)
         func->loopNestingBound++;

      mkFlow(OP_PREBREAK, lbrkBB, CC_ALWAYS, NULL);

      bb->cfg.attach(&lbgnBB->cfg, Graph::Edge::TREE);
      setPosition(lbgnBB, true);
      mkFlow(OP_PRECONT, lbgnBB, CC_ALWAYS, NULL);
   }
      break;
   case TGSI_OPCODE_ENDLOOP:
   {
      BasicBlock *loopBB = reinterpret_cast<BasicBlock *>(loopBBs.pop().u.p);

      if (!bb->isTerminated()) {
         mkFlow(OP_CONT, loopBB, CC_ALWAYS, NULL);
         bb->cfg.attach(&loopBB->cfg, Graph::Edge::BACK);
      }
      setPosition(reinterpret_cast<BasicBlock *>(breakBBs.pop().u.p), true);

      // If the loop never breaks (e.g. only has RET's inside), then there
      // will be no way to get to the break bb. However BGNLOOP will have
      // already made a PREBREAK to it, so it must be in the CFG.
      if (getBB()->cfg.incidentCount() == 0)
         loopBB->cfg.attach(&getBB()->cfg, Graph::Edge::TREE);
   }
      break;
   case TGSI_OPCODE_BRK:
   {
      if (bb->isTerminated())
         break;
      BasicBlock *brkBB = reinterpret_cast<BasicBlock *>(breakBBs.peek().u.p);
      mkFlow(OP_BREAK, brkBB, CC_ALWAYS, NULL);
      bb->cfg.attach(&brkBB->cfg, Graph::Edge::CROSS);
   }
      break;
   case TGSI_OPCODE_CONT:
   {
      if (bb->isTerminated())
         break;
      BasicBlock *contBB = reinterpret_cast<BasicBlock *>(loopBBs.peek().u.p);
      mkFlow(OP_CONT, contBB, CC_ALWAYS, NULL);
      contBB->explicitCont = true;
      bb->cfg.attach(&contBB->cfg, Graph::Edge::BACK);
   }
      break;
   case TGSI_OPCODE_BGNSUB:
   {
      Subroutine *s = getSubroutine(ip);
      BasicBlock *entry = new BasicBlock(s->f);
      BasicBlock *leave = new BasicBlock(s->f);

      // multiple entrypoints possible, keep the graph connected
      if (prog->getType() == Program::TYPE_COMPUTE)
         prog->main->call.attach(&s->f->call, Graph::Edge::TREE);

      sub.cur = s;
      s->f->setEntry(entry);
      s->f->setExit(leave);
      setPosition(entry, true);
      return true;
   }
   case TGSI_OPCODE_ENDSUB:
   {
      sub.cur = getSubroutine(prog->main);
      setPosition(BasicBlock::get(sub.cur->f->cfg.getRoot()), true);
      return true;
   }
   case TGSI_OPCODE_CAL:
   {
      Subroutine *s = getSubroutine(tgsi.getLabel());
      mkFlow(OP_CALL, s->f, CC_ALWAYS, NULL);
      func->call.attach(&s->f->call, Graph::Edge::TREE);
      return true;
   }
   case TGSI_OPCODE_RET:
   {
      if (bb->isTerminated())
         return true;
      BasicBlock *leave = BasicBlock::get(func->cfgExit);

      if (!isEndOfSubroutine(ip + 1)) {
         // insert a PRERET at the entry if this is an early return
         // (only needed for sharing code in the epilogue)
         BasicBlock *root = BasicBlock::get(func->cfg.getRoot());
         if (root->getEntry() == NULL || root->getEntry()->op != OP_PRERET) {
            BasicBlock *pos = getBB();
            setPosition(root, false);
            mkFlow(OP_PRERET, leave, CC_ALWAYS, NULL)->fixed = 1;
            setPosition(pos, true);
         }
      }
      mkFlow(OP_RET, NULL, CC_ALWAYS, NULL)->fixed = 1;
      bb->cfg.attach(&leave->cfg, Graph::Edge::CROSS);
   }
      break;
   case TGSI_OPCODE_END:
   {
      // attach and generate epilogue code
      BasicBlock *epilogue = BasicBlock::get(func->cfgExit);
      bb->cfg.attach(&epilogue->cfg, Graph::Edge::TREE);
      setPosition(epilogue, true);
      if (prog->getType() == Program::TYPE_FRAGMENT)
         exportOutputs();
      if ((prog->getType() == Program::TYPE_VERTEX ||
           prog->getType() == Program::TYPE_TESSELLATION_EVAL
          ) && info->io.genUserClip > 0)
         handleUserClipPlanes();
      mkOp(OP_EXIT, TYPE_NONE, NULL)->terminator = 1;
   }
      break;
   case TGSI_OPCODE_SWITCH:
   case TGSI_OPCODE_CASE:
      ERROR("switch/case opcode encountered, should have been lowered\n");
      abort();
      break;
   case TGSI_OPCODE_LOAD:
      handleLOAD(dst0);
      break;
   case TGSI_OPCODE_STORE:
      handleSTORE();
      break;
   case TGSI_OPCODE_BARRIER:
      geni = mkOp2(OP_BAR, TYPE_U32, NULL, mkImm(0), mkImm(0));
      geni->fixed = 1;
      geni->subOp = NV50_IR_SUBOP_BAR_SYNC;
      break;
   case TGSI_OPCODE_MEMBAR:
   {
      uint32_t level = tgsi.getSrc(0).getValueU32(0, info);
      geni = mkOp(OP_MEMBAR, TYPE_NONE, NULL);
      geni->fixed = 1;
      if (!(level & ~(TGSI_MEMBAR_THREAD_GROUP | TGSI_MEMBAR_SHARED)))
         geni->subOp = NV50_IR_SUBOP_MEMBAR(M, CTA);
      else
         geni->subOp = NV50_IR_SUBOP_MEMBAR(M, GL);
   }
      break;
   case TGSI_OPCODE_ATOMUADD:
   case TGSI_OPCODE_ATOMXCHG:
   case TGSI_OPCODE_ATOMCAS:
   case TGSI_OPCODE_ATOMAND:
   case TGSI_OPCODE_ATOMOR:
   case TGSI_OPCODE_ATOMXOR:
   case TGSI_OPCODE_ATOMUMIN:
   case TGSI_OPCODE_ATOMIMIN:
   case TGSI_OPCODE_ATOMUMAX:
   case TGSI_OPCODE_ATOMIMAX:
   case TGSI_OPCODE_ATOMFADD:
      handleATOM(dst0, dstTy, tgsi::opcodeToSubOp(tgsi.getOpcode()));
      break;
   case TGSI_OPCODE_RESQ:
      if (tgsi.getSrc(0).getFile() == TGSI_FILE_BUFFER) {
         Value *ind = NULL;
         if (tgsi.getSrc(0).isIndirect(0))
            ind = fetchSrc(tgsi.getSrc(0).getIndirect(0), 0, 0);
         geni = mkOp1(OP_BUFQ, TYPE_U32, dst0[0],
                      makeSym(tgsi.getSrc(0).getFile(),
                              tgsi.getSrc(0).getIndex(0), -1, 0, 0));
         if (ind)
            geni->setIndirect(0, 1, ind);
      } else {
         TexInstruction *texi = new_TexInstruction(func, OP_SUQ);
         for (int c = 0, d = 0; c < 4; ++c) {
            if (dst0[c]) {
               texi->setDef(d++, dst0[c]);
               texi->tex.mask |= 1 << c;
            }
         }
         if (tgsi.getSrc(0).getFile() == TGSI_FILE_IMAGE) {
            texi->tex.r = tgsi.getSrc(0).getIndex(0);
            if (tgsi.getSrc(0).isIndirect(0))
               texi->setIndirectR(fetchSrc(tgsi.getSrc(0).getIndirect(0), 0, NULL));
         } else {
            texi->tex.bindless = true;
            texi->setIndirectR(fetchSrc(0, 0));
         }
         texi->tex.target = tgsi.getImageTarget();

         bb->insertTail(texi);
      }
      break;
   case TGSI_OPCODE_IBFE:
   case TGSI_OPCODE_UBFE:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = fetchSrc(0, c);
         val0 = getScratch();
         if (tgsi.getSrc(1).getFile() == TGSI_FILE_IMMEDIATE &&
             tgsi.getSrc(2).getFile() == TGSI_FILE_IMMEDIATE) {
            loadImm(val0, (tgsi.getSrc(2).getValueU32(c, info) << 8) |
                    tgsi.getSrc(1).getValueU32(c, info));
         } else {
            src1 = fetchSrc(1, c);
            src2 = fetchSrc(2, c);
            mkOp3(OP_INSBF, TYPE_U32, val0, src2, mkImm(0x808), src1);
         }
         mkOp2(OP_EXTBF, dstTy, dst0[c], src0, val0);
      }
      break;
   case TGSI_OPCODE_BFI:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = fetchSrc(0, c);
         src1 = fetchSrc(1, c);
         src2 = fetchSrc(2, c);
         src3 = fetchSrc(3, c);
         val0 = getScratch();
         mkOp3(OP_INSBF, TYPE_U32, val0, src3, mkImm(0x808), src2);
         mkOp3(OP_INSBF, TYPE_U32, dst0[c], src1, val0, src0);
      }
      break;
   case TGSI_OPCODE_LSB:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = fetchSrc(0, c);
         val0 = getScratch();
         geni = mkOp2(OP_EXTBF, TYPE_U32, val0, src0, mkImm(0x2000));
         geni->subOp = NV50_IR_SUBOP_EXTBF_REV;
         geni = mkOp1(OP_BFIND, TYPE_U32, dst0[c], val0);
         geni->subOp = NV50_IR_SUBOP_BFIND_SAMT;
      }
      break;
   case TGSI_OPCODE_IMSB:
   case TGSI_OPCODE_UMSB:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = fetchSrc(0, c);
         mkOp1(OP_BFIND, srcTy, dst0[c], src0);
      }
      break;
   case TGSI_OPCODE_BREV:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = fetchSrc(0, c);
         geni = mkOp2(OP_EXTBF, TYPE_U32, dst0[c], src0, mkImm(0x2000));
         geni->subOp = NV50_IR_SUBOP_EXTBF_REV;
      }
      break;
   case TGSI_OPCODE_POPC:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = fetchSrc(0, c);
         mkOp2(OP_POPCNT, TYPE_U32, dst0[c], src0, src0);
      }
      break;
   case TGSI_OPCODE_INTERP_CENTROID:
   case TGSI_OPCODE_INTERP_SAMPLE:
   case TGSI_OPCODE_INTERP_OFFSET:
      handleINTERP(dst0);
      break;
   case TGSI_OPCODE_I642F:
   case TGSI_OPCODE_U642F:
   case TGSI_OPCODE_D2I:
   case TGSI_OPCODE_D2U:
   case TGSI_OPCODE_D2F: {
      int pos = 0;
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         Value *dreg = getSSA(8);
         src0 = fetchSrc(0, pos);
         src1 = fetchSrc(0, pos + 1);
         mkOp2(OP_MERGE, TYPE_U64, dreg, src0, src1);
         Instruction *cvt = mkCvt(OP_CVT, dstTy, dst0[c], srcTy, dreg);
         if (!isFloatType(dstTy))
            cvt->rnd = ROUND_Z;
         pos += 2;
      }
      break;
   }
   case TGSI_OPCODE_I2I64:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         dst0[c] = fetchSrc(0, c / 2);
         mkOp2(OP_SHR, TYPE_S32, dst0[c + 1], dst0[c], loadImm(NULL, 31));
         c++;
      }
      break;
   case TGSI_OPCODE_U2I64:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         dst0[c] = fetchSrc(0, c / 2);
         dst0[c + 1] = zero;
         c++;
      }
      break;
   case TGSI_OPCODE_F2I64:
   case TGSI_OPCODE_F2U64:
   case TGSI_OPCODE_I2D:
   case TGSI_OPCODE_U2D:
   case TGSI_OPCODE_F2D:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         Value *dreg = getSSA(8);
         Instruction *cvt = mkCvt(OP_CVT, dstTy, dreg, srcTy, fetchSrc(0, c / 2));
         if (!isFloatType(dstTy))
            cvt->rnd = ROUND_Z;
         mkSplit(&dst0[c], 4, dreg);
         c++;
      }
      break;
   case TGSI_OPCODE_D2I64:
   case TGSI_OPCODE_D2U64:
   case TGSI_OPCODE_I642D:
   case TGSI_OPCODE_U642D:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = getSSA(8);
         Value *dst = getSSA(8), *tmp[2];
         tmp[0] = fetchSrc(0, c);
         tmp[1] = fetchSrc(0, c + 1);
         mkOp2(OP_MERGE, TYPE_U64, src0, tmp[0], tmp[1]);
         Instruction *cvt = mkCvt(OP_CVT, dstTy, dst, srcTy, src0);
         if (!isFloatType(dstTy))
            cvt->rnd = ROUND_Z;
         mkSplit(&dst0[c], 4, dst);
         c++;
      }
      break;
   case TGSI_OPCODE_I64NEG:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = getSSA(8);
         Value *dst = getSSA(8), *tmp[2];
         tmp[0] = fetchSrc(0, c);
         tmp[1] = fetchSrc(0, c + 1);
         mkOp2(OP_MERGE, TYPE_U64, src0, tmp[0], tmp[1]);
         mkOp2(OP_SUB, dstTy, dst, zero, src0);
         mkSplit(&dst0[c], 4, dst);
         c++;
      }
      break;
   case TGSI_OPCODE_I64ABS:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = getSSA(8);
         Value *neg = getSSA(8), *srcComp[2], *negComp[2];
         srcComp[0] = fetchSrc(0, c);
         srcComp[1] = fetchSrc(0, c + 1);
         mkOp2(OP_MERGE, TYPE_U64, src0, srcComp[0], srcComp[1]);
         mkOp2(OP_SUB, dstTy, neg, zero, src0);
         mkSplit(negComp, 4, neg);
         mkCmp(OP_SLCT, CC_LT, TYPE_S32, dst0[c], TYPE_S32,
               negComp[0], srcComp[0], srcComp[1]);
         mkCmp(OP_SLCT, CC_LT, TYPE_S32, dst0[c + 1], TYPE_S32,
               negComp[1], srcComp[1], srcComp[1]);
         c++;
      }
      break;
   case TGSI_OPCODE_DABS:
   case TGSI_OPCODE_DNEG:
   case TGSI_OPCODE_DRCP:
   case TGSI_OPCODE_DSQRT:
   case TGSI_OPCODE_DRSQ:
   case TGSI_OPCODE_DTRUNC:
   case TGSI_OPCODE_DCEIL:
   case TGSI_OPCODE_DFLR:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = getSSA(8);
         Value *dst = getSSA(8), *tmp[2];
         tmp[0] = fetchSrc(0, c);
         tmp[1] = fetchSrc(0, c + 1);
         mkOp2(OP_MERGE, TYPE_U64, src0, tmp[0], tmp[1]);
         mkOp1(op, dstTy, dst, src0);
         mkSplit(&dst0[c], 4, dst);
         c++;
      }
      break;
   case TGSI_OPCODE_DFRAC:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = getSSA(8);
         Value *dst = getSSA(8), *tmp[2];
         tmp[0] = fetchSrc(0, c);
         tmp[1] = fetchSrc(0, c + 1);
         mkOp2(OP_MERGE, TYPE_U64, src0, tmp[0], tmp[1]);
         mkOp1(OP_FLOOR, TYPE_F64, dst, src0);
         mkOp2(OP_SUB, TYPE_F64, dst, src0, dst);
         mkSplit(&dst0[c], 4, dst);
         c++;
      }
      break;
   case TGSI_OPCODE_U64SEQ:
   case TGSI_OPCODE_U64SNE:
   case TGSI_OPCODE_U64SLT:
   case TGSI_OPCODE_U64SGE:
   case TGSI_OPCODE_I64SLT:
   case TGSI_OPCODE_I64SGE:
   case TGSI_OPCODE_DSLT:
   case TGSI_OPCODE_DSGE:
   case TGSI_OPCODE_DSEQ:
   case TGSI_OPCODE_DSNE: {
      int pos = 0;
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         Value *tmp[2];

         src0 = getSSA(8);
         src1 = getSSA(8);
         tmp[0] = fetchSrc(0, pos);
         tmp[1] = fetchSrc(0, pos + 1);
         mkOp2(OP_MERGE, TYPE_U64, src0, tmp[0], tmp[1]);
         tmp[0] = fetchSrc(1, pos);
         tmp[1] = fetchSrc(1, pos + 1);
         mkOp2(OP_MERGE, TYPE_U64, src1, tmp[0], tmp[1]);
         mkCmp(op, tgsi.getSetCond(), dstTy, dst0[c], srcTy, src0, src1);
         pos += 2;
      }
      break;
   }
   case TGSI_OPCODE_U64MIN:
   case TGSI_OPCODE_U64MAX:
   case TGSI_OPCODE_I64MIN:
   case TGSI_OPCODE_I64MAX: {
      dstTy = isSignedIntType(dstTy) ? TYPE_S32 : TYPE_U32;
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         Value *flag = getSSA(1, FILE_FLAGS);
         src0 = fetchSrc(0, c + 1);
         src1 = fetchSrc(1, c + 1);
         geni = mkOp2(op, dstTy, dst0[c + 1], src0, src1);
         geni->subOp = NV50_IR_SUBOP_MINMAX_HIGH;
         geni->setFlagsDef(1, flag);

         src0 = fetchSrc(0, c);
         src1 = fetchSrc(1, c);
         geni = mkOp2(op, TYPE_U32, dst0[c], src0, src1);
         geni->subOp = NV50_IR_SUBOP_MINMAX_LOW;
         geni->setFlagsSrc(2, flag);

         c++;
      }
      break;
   }
   case TGSI_OPCODE_U64SHL:
   case TGSI_OPCODE_I64SHR:
   case TGSI_OPCODE_U64SHR:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = getSSA(8);
         Value *dst = getSSA(8), *tmp[2];
         tmp[0] = fetchSrc(0, c);
         tmp[1] = fetchSrc(0, c + 1);
         mkOp2(OP_MERGE, TYPE_U64, src0, tmp[0], tmp[1]);
         // Theoretically src1 is a 64-bit value but in practice only the low
         // bits matter. The IR expects this to be a 32-bit value.
         src1 = fetchSrc(1, c);
         mkOp2(op, dstTy, dst, src0, src1);
         mkSplit(&dst0[c], 4, dst);
         c++;
      }
      break;
   case TGSI_OPCODE_U64ADD:
   case TGSI_OPCODE_U64MUL:
   case TGSI_OPCODE_DADD:
   case TGSI_OPCODE_DMUL:
   case TGSI_OPCODE_DDIV:
   case TGSI_OPCODE_DMAX:
   case TGSI_OPCODE_DMIN:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = getSSA(8);
         src1 = getSSA(8);
         Value *dst = getSSA(8), *tmp[2];
         tmp[0] = fetchSrc(0, c);
         tmp[1] = fetchSrc(0, c + 1);
         mkOp2(OP_MERGE, TYPE_U64, src0, tmp[0], tmp[1]);
         tmp[0] = fetchSrc(1, c);
         tmp[1] = fetchSrc(1, c + 1);
         mkOp2(OP_MERGE, TYPE_U64, src1, tmp[0], tmp[1]);
         mkOp2(op, dstTy, dst, src0, src1);
         mkSplit(&dst0[c], 4, dst);
         c++;
      }
      break;
   case TGSI_OPCODE_DMAD:
   case TGSI_OPCODE_DFMA:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = getSSA(8);
         src1 = getSSA(8);
         src2 = getSSA(8);
         Value *dst = getSSA(8), *tmp[2];
         tmp[0] = fetchSrc(0, c);
         tmp[1] = fetchSrc(0, c + 1);
         mkOp2(OP_MERGE, TYPE_U64, src0, tmp[0], tmp[1]);
         tmp[0] = fetchSrc(1, c);
         tmp[1] = fetchSrc(1, c + 1);
         mkOp2(OP_MERGE, TYPE_U64, src1, tmp[0], tmp[1]);
         tmp[0] = fetchSrc(2, c);
         tmp[1] = fetchSrc(2, c + 1);
         mkOp2(OP_MERGE, TYPE_U64, src2, tmp[0], tmp[1]);
         mkOp3(op, dstTy, dst, src0, src1, src2);
         mkSplit(&dst0[c], 4, dst);
         c++;
      }
      break;
   case TGSI_OPCODE_DROUND:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = getSSA(8);
         Value *dst = getSSA(8), *tmp[2];
         tmp[0] = fetchSrc(0, c);
         tmp[1] = fetchSrc(0, c + 1);
         mkOp2(OP_MERGE, TYPE_U64, src0, tmp[0], tmp[1]);
         mkCvt(OP_CVT, TYPE_F64, dst, TYPE_F64, src0)
         ->rnd = ROUND_NI;
         mkSplit(&dst0[c], 4, dst);
         c++;
      }
      break;
   case TGSI_OPCODE_DSSG:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = getSSA(8);
         Value *dst = getSSA(8), *dstF32 = getSSA(), *tmp[2];
         tmp[0] = fetchSrc(0, c);
         tmp[1] = fetchSrc(0, c + 1);
         mkOp2(OP_MERGE, TYPE_U64, src0, tmp[0], tmp[1]);

         val0 = getScratch();
         val1 = getScratch();
         // The zero is wrong here since it's only 32-bit, but it works out in
         // the end since it gets replaced with $r63.
         mkCmp(OP_SET, CC_GT, TYPE_F32, val0, TYPE_F64, src0, zero);
         mkCmp(OP_SET, CC_LT, TYPE_F32, val1, TYPE_F64, src0, zero);
         mkOp2(OP_SUB, TYPE_F32, dstF32, val0, val1);
         mkCvt(OP_CVT, TYPE_F64, dst, TYPE_F32, dstF32);
         mkSplit(&dst0[c], 4, dst);
         c++;
      }
      break;
   case TGSI_OPCODE_I64SSG:
      FOR_EACH_DST_ENABLED_CHANNEL(0, c, tgsi) {
         src0 = getSSA(8);
         Value *tmp[2];
         tmp[0] = fetchSrc(0, c);
         tmp[1] = fetchSrc(0, c + 1);
         mkOp2(OP_MERGE, TYPE_U64, src0, tmp[0], tmp[1]);

         val0 = getScratch();
         val1 = getScratch();
         mkCmp(OP_SET, CC_GT, TYPE_U32, val0, TYPE_S64, src0, zero);
         mkCmp(OP_SET, CC_LT, TYPE_U32, val1, TYPE_S64, src0, zero);
         mkOp2(OP_SUB, TYPE_S32, dst0[c], val1, val0);
         mkOp2(OP_SHR, TYPE_S32, dst0[c + 1], dst0[c], loadImm(0, 31));
         c++;
      }
      break;
   default:
      ERROR("unhandled TGSI opcode: %u\n", tgsi.getOpcode());
      assert(0);
      break;
   }

   if (tgsi.dstCount() && tgsi.getOpcode() != TGSI_OPCODE_STORE) {
      for (c = 0; c < 4; ++c) {
         if (!dst0[c])
            continue;
         if (dst0[c] != rDst0[c])
            mkMov(rDst0[c], dst0[c]);
         storeDst(0, c, rDst0[c]);
      }
   }
   vtxBaseValid = 0;

   return true;
}

void
Converter::handleUserClipPlanes()
{
   Value *res[8];
   int n, i, c;

   for (c = 0; c < 4; ++c) {
      for (i = 0; i < info->io.genUserClip; ++i) {
         Symbol *sym = mkSymbol(FILE_MEMORY_CONST, info->io.auxCBSlot,
                                TYPE_F32, info->io.ucpBase + i * 16 + c * 4);
         Value *ucp = mkLoadv(TYPE_F32, sym, NULL);
         if (c == 0)
            res[i] = mkOp2v(OP_MUL, TYPE_F32, getScratch(), clipVtx[c], ucp);
         else
            mkOp3(OP_MAD, TYPE_F32, res[i], clipVtx[c], ucp, res[i]);
      }
   }

   const int first = info->numOutputs - (info->io.genUserClip + 3) / 4;

   for (i = 0; i < info->io.genUserClip; ++i) {
      n = i / 4 + first;
      c = i % 4;
      Symbol *sym =
         mkSymbol(FILE_SHADER_OUTPUT, 0, TYPE_F32, info->out[n].slot[c] * 4);
      mkStore(OP_EXPORT, TYPE_F32, sym, NULL, res[i]);
   }
}

void
Converter::exportOutputs()
{
   if (info->io.alphaRefBase) {
      for (unsigned int i = 0; i < info->numOutputs; ++i) {
         if (info->out[i].sn != TGSI_SEMANTIC_COLOR ||
             info->out[i].si != 0)
            continue;
         const unsigned int c = 3;
         if (!oData.exists(sub.cur->values, i, c))
            continue;
         Value *val = oData.load(sub.cur->values, i, c, NULL);
         if (!val)
            continue;

         Symbol *ref = mkSymbol(FILE_MEMORY_CONST, info->io.auxCBSlot,
                                TYPE_U32, info->io.alphaRefBase);
         Value *pred = new_LValue(func, FILE_PREDICATE);
         mkCmp(OP_SET, CC_TR, TYPE_U32, pred, TYPE_F32, val,
               mkLoadv(TYPE_U32, ref, NULL))
            ->subOp = 1;
         mkOp(OP_DISCARD, TYPE_NONE, NULL)->setPredicate(CC_NOT_P, pred);
      }
   }

   for (unsigned int i = 0; i < info->numOutputs; ++i) {
      for (unsigned int c = 0; c < 4; ++c) {
         if (!oData.exists(sub.cur->values, i, c))
            continue;
         Symbol *sym = mkSymbol(FILE_SHADER_OUTPUT, 0, TYPE_F32,
                                info->out[i].slot[c] * 4);
         Value *val = oData.load(sub.cur->values, i, c, NULL);
         if (val) {
            if (info->out[i].sn == TGSI_SEMANTIC_POSITION)
               mkOp1(OP_SAT, TYPE_F32, val, val);
            mkStore(OP_EXPORT, TYPE_F32, sym, NULL, val);
         }
      }
   }
}

Converter::Converter(Program *ir, const tgsi::Source *code) : BuildUtil(ir),
     code(code),
     tgsi(NULL),
     tData(this), lData(this), aData(this), oData(this)
{
   info = code->info;

   const unsigned tSize = code->fileSize(TGSI_FILE_TEMPORARY);
   const unsigned aSize = code->fileSize(TGSI_FILE_ADDRESS);
   const unsigned oSize = code->fileSize(TGSI_FILE_OUTPUT);

   tData.setup(TGSI_FILE_TEMPORARY, 0, 0, tSize, 4, 4, FILE_GPR, 0);
   lData.setup(TGSI_FILE_TEMPORARY, 1, 0, tSize, 4, 4, FILE_MEMORY_LOCAL, 0);
   aData.setup(TGSI_FILE_ADDRESS, 0, 0, aSize, 4, 4, FILE_GPR, 0);
   oData.setup(TGSI_FILE_OUTPUT, 0, 0, oSize, 4, 4, FILE_GPR, 0);

   zero = mkImm((uint32_t)0);

   vtxBaseValid = 0;
}

Converter::~Converter()
{
}

inline const Converter::Location *
Converter::BindArgumentsPass::getValueLocation(Subroutine *s, Value *v)
{
   ValueMap::l_iterator it = s->values.l.find(v);
   return it == s->values.l.end() ? NULL : &it->second;
}

template<typename T> inline void
Converter::BindArgumentsPass::updateCallArgs(
   Instruction *i, void (Instruction::*setArg)(int, Value *),
   T (Function::*proto))
{
   Function *g = i->asFlow()->target.fn;
   Subroutine *subg = conv.getSubroutine(g);

   for (unsigned a = 0; a < (g->*proto).size(); ++a) {
      Value *v = (g->*proto)[a].get();
      const Converter::Location &l = *getValueLocation(subg, v);
      Converter::DataArray *array = conv.getArrayForFile(l.array, l.arrayIdx);

      (i->*setArg)(a, array->acquire(sub->values, l.i, l.c));
   }
}

template<typename T> inline void
Converter::BindArgumentsPass::updatePrototype(
   BitSet *set, void (Function::*updateSet)(), T (Function::*proto))
{
   (func->*updateSet)();

   for (unsigned i = 0; i < set->getSize(); ++i) {
      Value *v = func->getLValue(i);
      const Converter::Location *l = getValueLocation(sub, v);

      // only include values with a matching TGSI register
      if (set->test(i) && l && !conv.code->locals.count(*l))
         (func->*proto).push_back(v);
   }
}

bool
Converter::BindArgumentsPass::visit(Function *f)
{
   sub = conv.getSubroutine(f);

   for (ArrayList::Iterator bi = f->allBBlocks.iterator();
        !bi.end(); bi.next()) {
      for (Instruction *i = BasicBlock::get(bi)->getFirst();
           i; i = i->next) {
         if (i->op == OP_CALL && !i->asFlow()->builtin) {
            updateCallArgs(i, &Instruction::setSrc, &Function::ins);
            updateCallArgs(i, &Instruction::setDef, &Function::outs);
         }
      }
   }

   if (func == prog->main /* && prog->getType() != Program::TYPE_COMPUTE */)
      return true;
   updatePrototype(&BasicBlock::get(f->cfg.getRoot())->liveSet,
                   &Function::buildLiveSets, &Function::ins);
   updatePrototype(&BasicBlock::get(f->cfgExit)->defSet,
                   &Function::buildDefSets, &Function::outs);

   return true;
}

bool
Converter::run()
{
   BasicBlock *entry = new BasicBlock(prog->main);
   BasicBlock *leave = new BasicBlock(prog->main);

   prog->main->setEntry(entry);
   prog->main->setExit(leave);

   setPosition(entry, true);
   sub.cur = getSubroutine(prog->main);

   if (info->io.genUserClip > 0) {
      for (int c = 0; c < 4; ++c)
         clipVtx[c] = getScratch();
   }

   switch (prog->getType()) {
   case Program::TYPE_TESSELLATION_CONTROL:
      outBase = mkOp2v(
         OP_SUB, TYPE_U32, getSSA(),
         mkOp1v(OP_RDSV, TYPE_U32, getSSA(), mkSysVal(SV_LANEID, 0)),
         mkOp1v(OP_RDSV, TYPE_U32, getSSA(), mkSysVal(SV_INVOCATION_ID, 0)));
      break;
   case Program::TYPE_FRAGMENT: {
      Symbol *sv = mkSysVal(SV_POSITION, 3);
      fragCoord[3] = mkOp1v(OP_RDSV, TYPE_F32, getSSA(), sv);
      mkOp1(OP_RCP, TYPE_F32, fragCoord[3], fragCoord[3]);
      break;
   }
   default:
      break;
   }

   if (info->io.viewportId >= 0)
      viewport = getScratch();
   else
      viewport = NULL;

   for (ip = 0; ip < code->scan.num_instructions; ++ip) {
      if (!handleInstruction(&code->insns[ip]))
         return false;
   }

   if (!BindArgumentsPass(*this).run(prog))
      return false;

   return true;
}

} // unnamed namespace

namespace nv50_ir {

bool
Program::makeFromTGSI(struct nv50_ir_prog_info *info)
{
   tgsi::Source src(info);
   if (!src.scanSource())
      return false;
   tlsSize = info->bin.tlsSpace;

   Converter builder(this, &src);
   return builder.run();
}

} // namespace nv50_ir
