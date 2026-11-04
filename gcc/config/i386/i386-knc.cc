/* Copyright (C) 1988-2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* KNC-only helpers; shared i386 entry points dispatch here (ADR-0005).  */
#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "memmodel.h"
#include "tm_p.h"
#include "emit-rtl.h"
#include "insn-config.h"
#include "insn-flags.h"
#include "recog.h"
#include "explow.h"
#include "expr.h"
#include "varasm.h"
#include "target.h"
#include "regs.h"
#include "diagnostic-core.h"
#include "optabs.h"
#include "cpplib.h"

/* Exactly the packed storage modes in ADR-0005 point 1.  This predicate
   does not advertise arithmetic, an ABI, or automatic vectorization.  */
bool
ix86_knc_packed_mode_p (machine_mode mode)
{
  return TARGET_KNC && TARGET_64BIT
	 && (mode == V16SFmode || mode == V8DFmode
	     || mode == V16SImode || mode == V8DImode);
}

/* ADR-0005 point 5: try one full ZMM vector size.  Unsupported scalar
   types retain word_mode; tails and peeling use the scalar expanders.  */
machine_mode
ix86_knc_preferred_simd_mode (scalar_mode mode)
{
  switch (mode)
    {
    case E_SFmode: return V16SFmode;
    case E_DFmode: return V8DFmode;
    case E_SImode: return V16SImode;
    case E_DImode: return V8DImode;
    default: return word_mode;
    }
}

/* Only a packed all-bits-zero constant has an immediate IMCI form.  */
bool
ix86_knc_zero_constant_p (rtx x, machine_mode mode)
{
  if (mode == VOIDmode)
    mode = GET_MODE (x);
  return ix86_knc_packed_mode_p (mode) && x == CONST0_RTX (mode);
}

/* Walk by-value fields before the SysV classifier's large-object exits.
   Pointer targets are deliberately not traversed (ADR-0005 point 6).  */
bool
ix86_knc_unsupported_abi_type_p (const_tree type, machine_mode mode)
{
  if (!type)
    return false;
  switch (TREE_CODE (type))
    {
    case VECTOR_TYPE:
      return mode == XImode || ix86_knc_packed_mode_p (mode);
    case ARRAY_TYPE:
      type = TREE_TYPE (type);
      return ix86_knc_unsupported_abi_type_p (type, TYPE_MODE (type));
    case RECORD_TYPE:
    case UNION_TYPE:
    case QUAL_UNION_TYPE:
      for (tree field = TYPE_FIELDS (type); field; field = DECL_CHAIN (field))
	if (TREE_CODE (field) == FIELD_DECL
	    && ix86_knc_unsupported_abi_type_p (TREE_TYPE (field),
					       TYPE_MODE (TREE_TYPE (field))))
	  return true;
      return false;
    default:
      return false;
    }
}

/* Keep constants and memory transfers visible to allocation and costing.
   Pool entries have the packed mode's natural 512-bit alignment.  */
void
ix86_knc_expand_packed_move (machine_mode mode, rtx *operands)
{
  gcc_assert (ix86_knc_packed_mode_p (mode));
  if (CONSTANT_P (operands[1])
      && !ix86_knc_zero_constant_p (operands[1], mode))
    operands[1] = validize_mem (force_const_mem (mode, operands[1]));
  if (MEM_P (operands[0]) && !REG_P (operands[1]))
    operands[1] = force_reg (mode, operands[1]);
  rtx mem = MEM_P (operands[0]) ? operands[0] : operands[1];
  if (MEM_P (mem) && MEM_ALIGN (mem) < 512)
    {
      /* S3-03 / Intel 327364-001: a full unmasked pair is safe for a
	 mapped 64-byte payload.  Keep one logical SET and derive A+64
	 from its MEM at output, so combine cannot separate the halves.
	 Legalize displacement overflow now, without an output scratch.  */
      rtx addr = XEXP (mem, 0);
      if (!memory_address_addr_space_p (mode, plus_constant (Pmode, addr, 64),
					MEM_ADDR_SPACE (mem)))
	operands[MEM_P (operands[0]) ? 0 : 1]
	  = replace_equiv_address (mem, force_reg (Pmode, addr));
    }
  emit_insn (gen_rtx_SET (operands[0], operands[1]));
}

/* IMCI binary arithmetic has three operands, with an aligned memory
   source only in the last position (Intel 327364-001, VADD/VSUB/VMUL).
   In particular, never let an unaligned pair collapse into arithmetic.  */
void
ix86_knc_fixup_binary_operands (machine_mode mode, rtx *operands)
{
  operands[1] = force_reg (mode, operands[1]);
  if (!vector_operand (operands[2], mode))
    operands[2] = force_reg (mode, operands[2]);
}

/* ADR-0005 point 3 / Intel 327364-001 section 2.2.1: replicate the low
   128-bit block, then swizzle within 128-bit SF / 256-bit DF groups.
   Each instruction has its own allocated result.  A scalar arriving in
   x87 reaches the YJ source through S2's visible reload bridge.  */
void
ix86_knc_expand_duplicate (rtx target, rtx value)
{
  machine_mode mode = GET_MODE (target);
  gcc_assert (mode == V16SFmode || mode == V8DFmode);
  machine_mode inner = GET_MODE_INNER (mode);
  if (CONSTANT_P (value))
    {
      rtx mem = assign_stack_temp (inner, GET_MODE_SIZE (inner));
      emit_move_insn (mem, value);
      value = mem;
    }
  if (MEM_P (value))
    emit_insn (gen_rtx_SET (target, gen_rtx_VEC_DUPLICATE (mode, value)));
  else
    {
      rtx block = gen_reg_rtx (mode);
      value = force_reg (inner, value);
      if (mode == V16SFmode)
	{
	  emit_insn (gen_knc_broadcast_blockv16sf (block, value));
	  emit_insn (gen_knc_broadcast_swizzlev16sf (target, block));
	}
      else
	{
	  emit_insn (gen_knc_broadcast_blockv8df (block, value));
	  emit_insn (gen_knc_broadcast_swizzlev8df (target, block));
	}
    }
}

/* General construction uses scalar stores followed by one aligned load.
   No half-vector modes or hidden memory transfers are introduced.  */
void
ix86_knc_expand_vector_init (rtx target, rtx vals)
{
  machine_mode mode = GET_MODE (target);
  machine_mode inner = GET_MODE_INNER (mode);
  unsigned int count = GET_MODE_NUNITS (mode);
  gcc_assert (count == (unsigned int) XVECLEN (vals, 0));
  bool same = true;
  for (unsigned int i = 1; i < count; ++i)
    same &= rtx_equal_p (XVECEXP (vals, 0, 0), XVECEXP (vals, 0, i));
  if (same)
    {
      ix86_knc_expand_duplicate (target, XVECEXP (vals, 0, 0));
      return;
    }
  rtx mem = assign_stack_temp (mode, GET_MODE_SIZE (mode));
  for (unsigned int i = 0; i < count; ++i)
    emit_move_insn (adjust_address (mem, inner, i * GET_MODE_SIZE (inner)),
		    XVECEXP (vals, 0, i));
  emit_move_insn (target, mem);
}

/* KNC has no CMPXCHG16B (Intel 327364-001, Appendix B).  Keep the
   libatomic call and its expected-value temporary visible in RTL.
   Do not add atomic_loadti: TI loads are not lock-free, and __atomic
   operations must continue to use the matching libatomic implementation.  */
void
ix86_expand_knc_cas (rtx *operands)
{
  rtx expected = assign_stack_temp (TImode, 16);
  emit_move_insn (expected, operands[3]);
  rtx_mode_t args[] = {
    rtx_mode_t (XEXP (operands[2], 0), Pmode),
    rtx_mode_t (XEXP (expected, 0), Pmode),
    rtx_mode_t (operands[4], TImode),
    rtx_mode_t (GEN_INT (INTVAL (operands[6]) & MEMMODEL_BASE_MASK), SImode),
    rtx_mode_t (GEN_INT (INTVAL (operands[7]) & MEMMODEL_BASE_MASK), SImode)
  };
  rtx result = emit_library_call_value_1 (
    1, init_one_libfunc ("__atomic_compare_exchange_16"), NULL_RTX,
    LCT_NORMAL, QImode, 5, args);
  emit_move_insn (operands[0], result);
  emit_move_insn (operands[1], expected);
}

/* KNC ABI registers are storage, not an SSE arithmetic bank.  The
   expander exposes the memory bridge and allocated mask to the RTL.  */
bool
ix86_knc_expand_move (rtx op0, rtx op1)
{
  if (SSE_REG_P (op0) || SSE_REG_P (op1))
    {
      switch (GET_MODE (op0))
	{
	case SFmode: emit_insn (gen_knc_movesf (op0, op1)); return true;
	case DFmode: emit_insn (gen_knc_movedf (op0, op1)); return true;
	case SImode: emit_insn (gen_knc_movesi (op0, op1)); return true;
	case DImode: emit_insn (gen_knc_movedi (op0, op1)); return true;
	case TImode: emit_insn (gen_knc_moveti (op0, op1)); return true;
	case TFmode: emit_insn (gen_knc_movetf (op0, op1)); return true;
	default: break;
	}
    }
  return false;
}

/* KNC shares generic costs, but has no generic x86 arch macro.  */
enum processor_type
ix86_knc_target_macros (cpp_reader *reader,
			void (*def_or_undef) (cpp_reader *, const char *))
{
  def_or_undef (reader, "__KNC__");
  def_or_undef (reader, "__k1om__");
  def_or_undef (reader, "__k1om");
  return PROCESSOR_max;
}

/* ADR-0002: check after common handling expands ISA dependencies.  */
bool
ix86_knc_option_override (struct gcc_options *opts)
{
  opts->x_ix86_isa_flags |= OPTION_MASK_ISA_KNC;
  if (opts->x_ix86_isa_flags
      & (OPTION_MASK_ISA_SSE | OPTION_MASK_ISA_MMX
         | OPTION_MASK_ISA_AVX | OPTION_MASK_ISA_AVX512F))
    {
      error ("SSE, AVX and MMX options are not supported with "
             "%<-march=knc%>");
      return false;
    }
  return true;
}

/* KNC scalar transport admits XImode, but its packed vector ABI belongs
   to E3.  Reject these vector types before recursive ABI classification.  */
bool
ix86_knc_check_abi_type (const_tree type, machine_mode mode)
{
  if (!ix86_knc_unsupported_abi_type_p (type, mode))
    return true;
  error ("unsupported KNC register ABI mode");
  return false;
}

/* Diagnose unsupported SSE vector/half/decimal ABI containers before a
   hard register is constructed.  The classifier's private enum stays shared.  */
bool
ix86_knc_check_abi_mode (machine_mode mode, bool half_class)
{
  if (!VECTOR_MODE_P (mode)
      && !DECIMAL_FLOAT_MODE_P (GET_MODE_INNER (mode))
      && GET_MODE_INNER (mode) != HFmode
      && GET_MODE_INNER (mode) != BFmode && !half_class)
    return true;
  error ("unsupported KNC register ABI mode");
  return false;
}

unsigned int
ix86_knc_libcall_result_regno (machine_mode mode, unsigned int regno)
{
  if (regno == FIRST_SSE_REG
      && mode != SFmode && mode != DFmode && mode != TFmode)
    {
      error ("unsupported KNC libcall result mode");
      return AX_REG;
    }
  return regno;
}

/* Scalar modes transported through KNC's visible memory bridge.  */
static bool
knc_scalar_transport_mode_p (machine_mode mode)
{
  return mode == SFmode || mode == DFmode || mode == SImode
         || mode == DImode || mode == TImode || mode == TFmode;
}

bool
ix86_knc_output_reload_p (machine_mode mode, reg_class_t regclass)
{
  return SSE_CLASS_P (regclass)
         && (knc_scalar_transport_mode_p (mode) || mode == XImode);
}

/* Use the secondary reload expander, retaining its optimizer-visible memory
   cost in inline_secondary_memory_needed for ix86_register_move_cost.  */
bool
ix86_knc_reload_memory_bridge_p (machine_mode mode, reg_class_t class1,
				reg_class_t class2)
{
  return SSE_CLASS_P (class1) != SSE_CLASS_P (class2)
         && knc_scalar_transport_mode_p (mode);
}

bool
ix86_knc_hard_regno_mode_ok (unsigned int regno, machine_mode mode)
{
  if (MASK_REGNO_P (regno))
    return mode == HImode;
  return knc_scalar_transport_mode_p (mode)
         || ix86_knc_packed_mode_p (mode) || mode == XImode;
}

/* GCC 16 passes unstripped register or SUBREG operands here.  Keep the complete
   memory bridge in the expander, with an allocated mask materialization
   scratch (ADR-0004), rather than the donor's deleted memory store.  */
bool
ix86_knc_secondary_reload (bool in_p, rtx x, reg_class_t rclass,
			   machine_mode mode, secondary_reload_info *sri)
{
  int knc_regno = true_regnum (x);
  if (knc_regno >= 0 && knc_regno < FIRST_PSEUDO_REGISTER
      && SSE_CLASS_P (rclass) != SSE_REGNO_P (knc_regno))
    {
      bool to_vector = in_p ? SSE_CLASS_P (rclass)
			   : SSE_REGNO_P (knc_regno);
      switch (mode)
	{
#define KNC_RELOAD(M, m) \
	case M##mode: sri->icode = (to_vector ? CODE_FOR_knc_reload_in##m \
					: CODE_FOR_knc_reload_out##m); break
	KNC_RELOAD (SF, sf);
	KNC_RELOAD (DF, df);
	KNC_RELOAD (SI, si);
	KNC_RELOAD (DI, di);
	KNC_RELOAD (TI, ti);
	KNC_RELOAD (TF, tf);
#undef KNC_RELOAD
	default: return true;
	}
      sri->extra_cost = to_vector ? 2 : 4;
      return true;
    }
  return false;
}

