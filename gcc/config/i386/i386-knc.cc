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
         mapped 64-byte payload.  Legalize A+64 before allocation, so a
         displacement overflow cannot require a hidden output scratch.
         Keep the logical memory access as one SET: the two halves have
         alignment-dependent footprints, not two independent vectors.  */
      rtx high = adjust_address (mem, mode, 64);
      emit_insn (gen_rtx_PARALLEL (VOIDmode, gen_rtvec (2,
                   gen_rtx_SET (operands[0], operands[1]),
                   gen_rtx_USE (VOIDmode, XEXP (high, 0)))));
      return;
    }
  emit_insn (gen_rtx_SET (operands[0], operands[1]));
}
