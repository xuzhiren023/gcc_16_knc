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

/* Exactly the packed storage modes in ADR-0005 point 1.  This predicate
   does not advertise arithmetic, an ABI, or automatic vectorization.  */
bool
ix86_knc_packed_mode_p (machine_mode mode)
{
  return TARGET_KNC && TARGET_64BIT
	 && (mode == V16SFmode || mode == V8DFmode
	     || mode == V16SImode || mode == V8DImode);
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
