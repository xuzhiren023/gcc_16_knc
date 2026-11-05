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
#include "gimple-expr.h"
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
#include "targhooks.h"
#include "fold-const.h"
#include "gimple.h"
#include "case-cfn-macros.h"
#include "cfgloop.h"
#include "tree-vectorizer.h"

/* Exactly the packed storage modes in ADR-0005 point 1.  This predicate
   does not advertise arithmetic, an ABI, or automatic vectorization.  */
bool
ix86_knc_packed_mode_p (machine_mode mode)
{
  return TARGET_KNC && TARGET_64BIT
	 && (mode == V16SFmode || mode == V8DFmode
	     || mode == V16SImode || mode == V8DImode);
}

/* V4SF is only the shared SysV varargs save loop's 16-byte transport.
   It does not enable SSE arithmetic or change the packed storage modes.  */
bool
ix86_knc_vector_move_mode_p (machine_mode mode)
{
  return ix86_knc_packed_mode_p (mode)
	 || (TARGET_KNC && TARGET_64BIT && mode == V4SFmode);
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

/* Intel 327364-001 VLOADUNPACKLD p.361: pairs accept element,
   not arbitrary byte, alignment.  Both SLP and loop vectorization call
   vect_supportable_dr_alignment, whose is_packed is not_size_aligned:
   get_object_alignment of the scalar reference must cover its size.
   Unknown vector misalignment is therefore safe only when !is_packed.  */
bool
ix86_knc_support_vector_misalignment (machine_mode mode, int misalignment,
                                      bool is_packed, bool is_gather_scatter)
{
  if (!TARGET_KNC)
    return default_builtin_support_vector_misalignment
      (mode, misalignment, is_packed, is_gather_scatter);
  if (!ix86_knc_packed_mode_p (mode) || is_packed || is_gather_scatter)
    return false;
  unsigned int bytes = GET_MODE_SIZE (GET_MODE_INNER (mode));
  return misalignment == -1
         || (misalignment >= 0 && misalignment % bytes == 0);
}

/* S4-05, ADR-0007 point 7: ask for a masked epilogue only when each of
   its masked moves will be one aligned line.  Every relevant access must
   be contiguous, forward, and known 64-byte aligned without a peel: after
   an alignment peel the epilogue's references have unknown misalignment
   (PR 92351), and a misaligned masked access has no IMCI form yet.  */
bool
ix86_knc_masked_epilogue_p (loop_vec_info loop_vinfo)
{
  if (!ix86_knc_packed_mode_p (loop_vinfo->vector_mode)
      || LOOP_VINFO_PEELING_FOR_ALIGNMENT (loop_vinfo) != 0
      || LOOP_REQUIRES_VERSIONING_FOR_ALIGNMENT (loop_vinfo))
    return false;
  for (data_reference *dr : LOOP_VINFO_DATAREFS (loop_vinfo))
    {
      dr_vec_info *dr_info = loop_vinfo->lookup_dr (dr);
      stmt_vec_info stmt_info = dr_info->stmt;
      if (!STMT_VINFO_RELEVANT_P (stmt_info) || integer_zerop (DR_STEP (dr)))
	continue;
      tree vectype = STMT_VINFO_VECTYPE (stmt_info);
      if (STMT_VINFO_GATHER_SCATTER_P (stmt_info)
	  || STMT_VINFO_STRIDED_P (stmt_info)
	  || STMT_VINFO_GROUPED_ACCESS (stmt_info)
	  || TREE_CODE (DR_STEP (dr)) != INTEGER_CST
	  || tree_int_cst_sgn (DR_STEP (dr)) < 0
	  || !vectype
	  || dr_misalignment (dr_info, vectype) != 0
	  || !known_eq (DR_TARGET_ALIGNMENT (dr_info), 64U))
	return false;
    }
  return true;
}

/* Also guard recognition after combine and reload.  Keep MEM_ALIGN in
   target code, since gencondmd does not expose that RTL accessor.  */
bool
ix86_knc_element_aligned_mem_p (rtx mem, machine_mode mode)
{
  return MEM_ALIGN (mem) >= GET_MODE_BITSIZE (GET_MODE_INNER (mode));
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
ix86_knc_expand_vector_move (machine_mode mode, rtx *operands)
{
  if (mode == V4SFmode)
    {
      /* Keep the donor's 16-byte save slots and TI transport (ADR-0004).
         Expansion stays after the shared loop's AL test, so the scratch
         definitions and the resulting save sequence are unchanged.  */
      gcc_assert (MEM_P (operands[0]) && SSE_REG_P (operands[1])
		  && MEM_ALIGN (operands[0]) >= 128);
      emit_insn (gen_knc_save_varargs
		 (adjust_address (operands[0], TImode, 0),
		  gen_rtx_REG (TImode, REGNO (operands[1]))));
      return;
    }
  gcc_assert (ix86_knc_packed_mode_p (mode));
  if (CONSTANT_P (operands[1])
      && !ix86_knc_zero_constant_p (operands[1], mode))
    operands[1] = validize_mem (force_const_mem (mode, operands[1]));
  if (MEM_P (operands[0]) && !REG_P (operands[1]))
    operands[1] = force_reg (mode, operands[1]);
  rtx mem = MEM_P (operands[0]) ? operands[0] : operands[1];
  if (MEM_P (mem)
      && MEM_ALIGN (mem) < GET_MODE_BITSIZE (GET_MODE_INNER (mode)))
    {
      /* Integer moves permit byte alignment and touch exactly the payload.
         Expose all eight transfers and the aligned vector move to RTL;
         do not use a block copy that could select movmisalign again.  */
      rtx slot = assign_stack_temp (mode, GET_MODE_SIZE (mode));
      bool store = MEM_P (operands[0]);
      if (store)
        emit_move_insn (slot, operands[1]);
      for (unsigned int offset = 0; offset < 64; offset += 8)
        {
          rtx from = adjust_address (store ? slot : mem, DImode, offset);
          rtx to = adjust_address (store ? mem : slot, DImode, offset);
          emit_move_insn (to, force_reg (DImode, from));
        }
      if (!store)
        emit_move_insn (operands[0], slot);
      return;
    }
  if (MEM_P (mem) && MEM_ALIGN (mem) < 512)
    {
      /* S3-03 / Intel 327364-001: a full unmasked pair is safe for an
	 element-aligned mapped 64-byte payload.  Keep one logical SET and derive A+64
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
   x87 reaches the YJ source through S2's visible reload bridge.
   An integer scalar lives in a GPR, so it is broadcast from a stack slot
   with vpbroadcastd/q.  A memory scalar that is not element-aligned is
   copied to an aligned slot first: IMCI memory operands must be aligned.  */
void
ix86_knc_expand_duplicate (rtx target, rtx value)
{
  machine_mode mode = GET_MODE (target);
  gcc_assert (ix86_knc_packed_mode_p (mode));
  machine_mode inner = GET_MODE_INNER (mode);
  if (CONSTANT_P (value)
      || (MEM_P (value) && !ix86_knc_element_aligned_mem_p (value, mode))
      || (!MEM_P (value) && INTEGRAL_MODE_P (mode)))
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

/* Legacy __sync model tags contain internal bits outside the libatomic ABI.
   Keep these calls visible in RTL, as in the original imci.md expanders.  */
void
ix86_knc_expand_atomic_exchange (rtx *operands)
{
  rtx result = emit_library_call_value (
    init_one_libfunc ("__atomic_exchange_16"), NULL_RTX, LCT_NORMAL,
    TImode, XEXP (operands[1], 0), Pmode, operands[2], TImode,
    GEN_INT (INTVAL (operands[3]) & MEMMODEL_BASE_MASK), SImode);
  emit_move_insn (operands[0], result);
}

void
ix86_knc_expand_atomic_store (rtx *operands)
{
  emit_library_call (
    init_one_libfunc ("__atomic_store_16"), LCT_NORMAL, VOIDmode,
    XEXP (operands[0], 0), Pmode, operands[1], TImode,
    GEN_INT (INTVAL (operands[2]) & MEMMODEL_BASE_MASK), SImode);
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
  if (GET_MODE (op0) == XImode)
    {
      /* Like ix86_expand_move, expose the intermediate register for a
         memory copy.  knc_movxi has no memory-to-memory alternative.  */
      if (MEM_P (op0) && MEM_P (op1))
        op1 = force_reg (XImode, op1);
      emit_insn (gen_knc_movxi (op0, op1));
      return true;
    }

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

/* KNC cannot implement register clearing using the shared SSE sequences.  */
HARD_REG_SET
ix86_knc_zero_call_used_regs (HARD_REG_SET /* need_zeroed_hardregs */)
{
  HARD_REG_SET zeroed_hardregs;
  CLEAR_HARD_REG_SET (zeroed_hardregs);
  error ("zero-call-used-regs is unsupported for KNC");
  return zeroed_hardregs;
}

/* KNC shares generic costs, but has no generic x86 arch macro.  */
enum processor_type
ix86_knc_target_macros (cpp_reader *reader,
			void (*def_or_undef) (cpp_reader *, const char *))
{
  /* c-family/c-cppbuiltin.cc defines the atomic macros before calling the
     target builtins hook.  Our TI CAS expander uses lock-based libatomic,
     so its presence does not mean native 16-byte CAS is available.  */
  cpp_undef (reader, "__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16");
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

/* Keep va_arg expansion well formed after the ABI diagnostic, like the
   typed result retained by aarch64_gimplify_va_arg_expr after
   aarch64_err_no_fpadvsimd.  Generic gimplify_va_arg_expr likewise keeps
   a result of the requested type on its invalid promoted-type path.  */
tree
ix86_knc_va_arg_error (tree type)
{
  if (AGGREGATE_TYPE_P (type))
    return create_tmp_var (type, "va_arg_tmp");
  return build_zero_cst (type);
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
  /* ADR-0007 point 1: 16-lane masks are HI, 8-lane masks QI.  */
  if (MASK_REGNO_P (regno))
    return mode == HImode || mode == QImode;
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

/* ADR-0007 point 5 and Intel 327364-001 VCMPPS/VCMPPD (3-bit predicate:
   eq 0, lt 1, le 2, unord 3, neq 4, nlt 5, nle 6, ord 7) and VPCMPD/VPCMPUD
   (3 and 7 reserved).  FP > and >= swap the operands of lt and le, so they
   signal on a QNaN as C requires.  UNLT..UNGE get here only with
   -fno-trapping-math (knc_vec_cmp_operator), as nle/nlt; UNEQ and LTGT
   never do.  The immediates of the integer forms are those of
   ix86_cmp_code_to_pcmp_immediate.  */
bool
ix86_knc_expand_mask_vec_cmp (rtx dest, enum rtx_code code, rtx op0, rtx op1)
{
  machine_mode mode = GET_MODE (op0);
  bool fp = FLOAT_MODE_P (mode);
  bool swap = false;
  int imm;
  switch (code)
    {
    case EQ: imm = 0; break;
    case NE: imm = 4; break;
    case LT: case LTU: imm = 1; break;
    case LE: case LEU: imm = 2; break;
    case GT: case GTU: imm = fp ? 1 : 6; swap = fp; break;
    case GE: case GEU: imm = fp ? 2 : 5; swap = fp; break;
    case UNORDERED: gcc_assert (fp); imm = 3; break;
    case ORDERED: gcc_assert (fp); imm = 7; break;
    case UNGE: case UNLE: imm = 5; swap = code == UNLE; break;
    case UNGT: case UNLT: imm = 6; swap = code == UNLT; break;
    default: gcc_unreachable ();
    }
  if (swap)
    std::swap (op0, op1);
  op0 = force_reg (mode, op0);
  if (!nonimmediate_operand (op1, mode))
    op1 = force_reg (mode, op1);
  bool unsigned_p = code == LTU || code == LEU || code == GTU || code == GEU;
  rtx cmp = gen_rtx_UNSPEC (GET_MODE (dest),
			    gen_rtvec (3, op0, op1, GEN_INT (imm)),
			    unsigned_p ? UNSPEC_KNC_UCMP : UNSPEC_PCMP);
  emit_insn (gen_rtx_SET (dest, cmp));
  return true;
}

/* S5-06, ADR-0008 point 1: the register swizzles of Intel 327364-001
   tables 2.2 and 2.3 (p.29-30) as the source lane of lanes 0-3 of each
   group, in the order of the swizzle field: {dcba} {cdab} {badc} {dacb}
   {aaaa} {bbbb} {cccc} {dddd}.  */
static const unsigned char knc_swizzle_lanes[8][4]
  = { { 0, 1, 2, 3 }, { 1, 0, 3, 2 }, { 2, 3, 0, 1 }, { 1, 2, 0, 3 },
      { 0, 0, 0, 0 }, { 1, 1, 1, 1 }, { 2, 2, 2, 2 }, { 3, 3, 3, 3 } };

/* Operand 2 of knc_swizzle<mode> for the one-input dword selector D, or
   -1.  A 32-bit swizzle repeats in every 128-bit block and a 64-bit one
   (operand + 8) in every 256-bit block.  QWORDS_FIRST tries 64-bit first,
   so that 8-lane data is swizzled in its own element size.  */
static int
knc_swizzle_code (const unsigned char *d, bool qwords_first)
{
  for (int pass = 0; pass < 2; ++pass)
    {
      bool qwords = (pass == 0) == qwords_first;
      for (int s = 1; s < 8; ++s)
	{
	  bool ok = true;
	  for (unsigned int i = 0; i < 16 && ok; ++i)
	    {
	      unsigned int want;
	      if (qwords)
		{
		  unsigned int q = i / 2;
		  want = 2 * ((q & ~3U) + knc_swizzle_lanes[s][q & 3]) + (i & 1);
		}
	      else
		want = (i & ~3U) + knc_swizzle_lanes[s][i & 3];
	      ok = d[i] == want;
	    }
	  if (ok)
	    return qwords ? s + 8 : s;
	}
    }
  return -1;
}

/* The vpermf32x4 immediate (p.482) for D, or -1: whole 128-bit blocks.  */
static int
knc_permf32x4_imm (const unsigned char *d)
{
  int imm = 0;
  for (unsigned int b = 0; b < 4; ++b)
    {
      if (d[4 * b] % 4 != 0)
	return -1;
      for (unsigned int j = 1; j < 4; ++j)
	if (d[4 * b + j] != d[4 * b] + j)
	  return -1;
      imm |= (d[4 * b] / 4) << (2 * b);
    }
  return imm;
}

/* The vpshufd immediate (p.536) for D, or -1: one permutation within
   every 128-bit block.  */
static int
knc_pshufd_imm (const unsigned char *d)
{
  int imm = 0;
  for (unsigned int j = 0; j < 4; ++j)
    {
      if (d[j] >= 4)
	return -1;
      for (unsigned int b = 1; b < 4; ++b)
	if (d[4 * b + j] != 4 * b + d[j])
	  return -1;
      imm |= d[j] << (2 * j);
    }
  return imm;
}

/* The valignd shift K (p.124) when D[I] is (K + I) & MASK for every I,
   else 0.  MASK is 15 for a one-input rotation and 31 for two inputs,
   where K + I never exceeds 30.  */
static int
knc_valignd_shift (const unsigned char *d, unsigned int mask)
{
  unsigned int k = d[0];
  if (k == 0 || k >= 16)
    return 0;
  for (unsigned int i = 1; i < 16; ++i)
    if (d[i] != ((k + i) & mask))
      return 0;
  return k;
}

/* A register holding the vpermd index for D.  VPERMD reads index bits
   3:0 only (p.480), so the 8-lane modes use dword pairs, which D already
   is.  The constant is an ordinary move, so it is hoisted out of loops.  */
static rtx
knc_perm_index (const unsigned char *d)
{
  rtvec v = rtvec_alloc (16);
  for (unsigned int i = 0; i < 16; ++i)
    RTVEC_ELT (v, i) = GEN_INT (d[i] & 15);
  return force_reg (V16SImode, gen_rtx_CONST_VECTOR (V16SImode, v));
}

/* TARGET_VECTORIZE_VEC_PERM_CONST for KNC (ADR-0008 point 1), reached
   from the entry of ix86_vectorize_vec_perm_const, so no AVX-512 helper
   runs.  The selector is viewed as 16 dwords and the cheapest form is
   taken: a move, a register swizzle, vpermf32x4, vpshufd, valignd, a
   blend, one vpermd, or vpermd plus a write-masked vpermd.  The last
   takes any two inputs, so every selector is supported and testing
   always succeeds, as the expansion does.  */
bool
ix86_knc_vectorize_vec_perm_const (machine_mode vmode, machine_mode op_mode,
				   rtx target, rtx op0, rtx op1,
				   const vec_perm_indices &sel)
{
  if (vmode != op_mode || !ix86_knc_packed_mode_p (vmode))
    return false;
  if (!target)
    return true;

  unsigned int nelt = GET_MODE_NUNITS (vmode);
  unsigned int per = 16 / nelt;
  gcc_assert (sel.length () == nelt);
  unsigned char e[16], d[16];
  bool same = rtx_equal_p (op0, op1);
  bool from0 = false, from1 = false;
  for (unsigned int i = 0; i < nelt; ++i)
    {
      e[i] = sel[i].to_constant () % (same ? nelt : 2 * nelt);
      from0 |= e[i] < nelt;
      from1 |= e[i] >= nelt;
    }
  if (!from0)
    {
      std::swap (op0, op1);
      for (unsigned int i = 0; i < nelt; ++i)
	e[i] -= nelt;
      from1 = false;
    }
  for (unsigned int i = 0; i < 16; ++i)
    d[i] = e[i / per] * per + i % per;

  rtx dest = register_operand (target, vmode) ? target : gen_reg_rtx (vmode);
  op0 = force_reg (vmode, op0);
  int imm;
  if (!from1)
    {
      bool identity = true;
      for (unsigned int i = 0; i < 16; ++i)
	identity &= d[i] == i;
      if (identity)
	emit_move_insn (dest, op0);
      else if ((imm = knc_swizzle_code (d, nelt == 8)) >= 0)
	emit_insn (gen_knc_swizzle (vmode, dest, op0, GEN_INT (imm)));
      else if ((imm = knc_permf32x4_imm (d)) >= 0)
	emit_insn (gen_knc_permf32x4 (vmode, dest, op0, GEN_INT (imm)));
      else if ((imm = knc_pshufd_imm (d)) >= 0)
	emit_insn (gen_knc_pshufd (vmode, dest, op0, GEN_INT (imm)));
      else if ((imm = knc_valignd_shift (d, 15)) > 0)
	emit_insn (gen_knc_valignd (vmode, dest, op0, op0, GEN_INT (imm)));
      else
	emit_insn (gen_knc_permvar (vmode, dest, op0, knc_perm_index (d)));
    }
  else
    {
      op1 = force_reg (vmode, op1);
      unsigned char swapped[16];
      bool blend = true;
      unsigned int lanes = 0, dwords = 0;
      for (unsigned int i = 0; i < 16; ++i)
	{
	  swapped[i] = d[i] ^ 16;
	  dwords |= (unsigned int) (d[i] >= 16) << i;
	}
      for (unsigned int i = 0; i < nelt; ++i)
	{
	  blend &= e[i] % nelt == i;
	  lanes |= (unsigned int) (e[i] >= nelt) << i;
	}
      if ((imm = knc_valignd_shift (d, 31)) > 0)
	emit_insn (gen_knc_valignd (vmode, dest, op1, op0, GEN_INT (imm)));
      else if ((imm = knc_valignd_shift (swapped, 31)) > 0)
	emit_insn (gen_knc_valignd (vmode, dest, op0, op1, GEN_INT (imm)));
      else if (blend)
	{
	  /* vblendm (imci.md *knc_blend): lanes set in the mask take op1.  */
	  machine_mode mmode = nelt == 16 ? HImode : QImode;
	  rtx mask = force_reg (mmode, gen_int_mode (lanes, mmode));
	  emit_insn (gen_rtx_SET (dest, gen_rtx_VEC_MERGE (vmode, op1, op0,
							   mask)));
	}
      else
	{
	  /* Index D mod 16 on both inputs; the second vpermd writes only
	     the dwords that come from op1.  */
	  rtx index = knc_perm_index (d);
	  rtx mask = force_reg (HImode, gen_int_mode (dwords, HImode));
	  rtx low = gen_reg_rtx (V16SImode);
	  rtx result = gen_reg_rtx (V16SImode);
	  emit_insn (gen_knc_permvarv16si (low, gen_lowpart (V16SImode, op0),
					   index));
	  emit_insn (gen_knc_permvarv16si_mask (result,
						gen_lowpart (V16SImode, op1),
						index, low, mask));
	  emit_move_insn (dest, gen_lowpart (vmode, result));
	}
    }
  if (dest != target)
    emit_move_insn (target, dest);
  return true;
}

/* ADR-0008 point 1, for ix86_vector_costs::add_stmt_cost: whether a
   vec_perm on MODE for STMT_INFO and NODE needs two inputs per result
   vector, which costs vpermd plus a masked vpermd.  That holds for an SLP
   lane permutation that merges children (unless every lane stays in
   place, which is one blend) or keeps fewer lanes than its one child has,
   and for a grouped access whose node uses only part of the group, or
   whose group does not divide the vector.  A reversal, or a swap within
   each group, stays one-input.  */
bool
ix86_knc_two_input_perm_p (machine_mode mode, stmt_vec_info stmt_info,
			   slp_tree node)
{
  if (!ix86_knc_packed_mode_p (mode) || !node)
    return false;
  if (SLP_TREE_PERMUTE_P (node))
    {
      if (SLP_TREE_CHILDREN (node).length () == 1)
	return (SLP_TREE_LANES (node)
		< SLP_TREE_LANES (SLP_TREE_CHILDREN (node)[0]));
      for (unsigned int i = 0;
	   i < SLP_TREE_LANE_PERMUTATION (node).length (); ++i)
	if (SLP_TREE_LANE_PERMUTATION (node)[i].second != i)
	  return true;
      return false;
    }
  if (!stmt_info || !STMT_VINFO_GROUPED_ACCESS (stmt_info))
    return false;
  unsigned int group = DR_GROUP_SIZE (DR_GROUP_FIRST_ELEMENT (stmt_info));
  return (SLP_TREE_LANES (node) < group
	  || !multiple_p (GET_MODE_NUNITS (mode), group));
}

/* True for exactly the {cdab} and {badc} selectors at the element size
   of MODE.  Reject partial selectors and anything crossing a group.  */
bool
ix86_knc_reduc_swizzle_p (rtx selector, machine_mode mode)
{
  unsigned int nelt = GET_MODE_NUNITS (mode);
  if (GET_CODE (selector) != PARALLEL
      || XVECLEN (selector, 0) != (int) nelt
      || !CONST_INT_P (XVECEXP (selector, 0, 0)))
    return false;
  HOST_WIDE_INT distance = INTVAL (XVECEXP (selector, 0, 0));
  if (distance != 1 && distance != 2)
    return false;
  for (unsigned int i = 0; i < nelt; ++i)
    if (!CONST_INT_P (XVECEXP (selector, 0, i))
        || INTVAL (XVECEXP (selector, 0, i)) != (i ^ distance))
      return false;
  return true;
}

/* ADR-0008 point 2, Intel 327364-001 pp.28-30 and p.482: reduce halves,
   then 128-bit blocks, then pairs.  Swizzles fold into the second source
   of the arithmetic.  GCC only requests FP PLUS when reassociation is
   allowed; no fold_left_plus is supplied here.

   V8DI PLUS uses S4-04's visible carry sequence.  Its operations are on
   dwords, so shuffle whole qwords first rather than swizzling the add.  */
void
ix86_knc_expand_reduc (enum rtx_code code, rtx dest, rtx src)
{
  machine_mode mode = GET_MODE (src);
  unsigned int nelt = GET_MODE_NUNITS (mode);
  bool carry = mode == V8DImode && code == PLUS;
  gcc_assert (ix86_knc_packed_mode_p (mode));
  gcc_assert (code == PLUS
              || (mode == V16SImode
                  && (code == SMIN || code == SMAX
                      || code == UMIN || code == UMAX))
              || ((mode == V16SImode || mode == V8DImode)
                  && (code == AND || code == IOR || code == XOR)));

  for (unsigned int distance = nelt / 2; distance; distance /= 2)
    {
      rtx other;
      if (distance >= 4 || carry)
        {
          other = gen_reg_rtx (mode);
          if (carry && distance == 1)
            emit_insn (gen_knc_pshufd (mode, other, src, GEN_INT (0x4e)));
          else
            emit_insn (gen_knc_permf32x4 (mode, other, src,
                                        GEN_INT (distance == nelt / 2
                                                 ? 0x4e : 0xb1)));
        }
      else
        {
          rtvec lanes = rtvec_alloc (nelt);
          for (unsigned int i = 0; i < nelt; ++i)
            RTVEC_ELT (lanes, i) = GEN_INT (i ^ distance);
          other = gen_rtx_VEC_SELECT (mode, src,
                                     gen_rtx_PARALLEL (VOIDmode, lanes));
        }
      rtx result = gen_reg_rtx (mode);
      if (carry)
        emit_insn (gen_addv8di3 (result, src, other));
      else
        emit_insn (gen_rtx_SET (result,
                               gen_rtx_fmt_ee (code, mode, src, other)));
      src = result;
    }

  /* Lane zero uses the existing scalar zmm transport (ADR-0004).  Any
     bridge to a GPR or x87 remains visible to register allocation.  */
  emit_move_insn (dest, gen_lowpart (GET_MODE (dest), src));
}

/* Correct rounding of V16SF sqrt: docs/research/knc-div-sqrt.md sections
   2.3, 2.5 and 5 (E3), Intel 327364-001 pp.588-590 (estimate bound).

   Lemma A: a faithful quotient has an exact FMA residual a-b*q.
   Lemma M: a/b is not a midpoint and its distance from a midpoint is at
   least u/(2B), and from another float at least u/B, B the significand
   of b.  Lemma B: if |b*y-1| < 1/(B+1), the exact residual correction
   rounds correctly, including directed modes and the inexact flag.
   These division lemmas motivate the correcting step, but are NOT a
   proof for sqrt: d=m-g1*g1 can be inexact.  E3 proves the sqrt claims
   exhaustively for every reduced m and every estimate within 1 ulp
   (stronger than the manual's 0.775 ulp), in all four rounding modes:
   g1 is faithful; fma(d,h1,g1) rounds correctly and is inexact iff sqrt(m)
   is not representable.  h1, rather than h0, is essential.

   Reduction gives x=m*2^(2j), 1/2 <= m < 2, j=ceil(e/2), including
   subnormals with DAZ clear.  sqrt(x) is normal and scaling is exact.
   All intermediates suppress flags; only the final masked FMA can raise
   inexact.  The masked estimate of negative inputs raises invalid, while
   fixup preserves signed zero, +inf and quieted NaN payloads.  There is
   no branch, helper call, hidden spill or implicit precision change.  */
void
ix86_knc_expand_sqrt (rtx *operands)
{
  rtx x = force_reg (V16SFmode, operands[1]);
  rtx m = gen_reg_rtx (V16SFmode);
  rtx e = gen_reg_rtx (V16SFmode);
  rtx ei = gen_reg_rtx (V16SImode);
  rtx j = gen_reg_rtx (V16SImode);
  emit_insn (gen_knc_sqrt_mant (m, x));
  emit_insn (gen_knc_sqrt_exp (e, x));
  emit_insn (gen_knc_sqrt_cvt (ei, e));
  rtx one = force_reg (V16SImode, CONST1_RTX (V16SImode));
  emit_insn (gen_addv16si3 (ei, ei, one));
  emit_insn (gen_knc_sqrt_shift (j, ei));

  rtx xm = gen_reg_rtx (V16SImode);
  emit_insn (gen_subv16si3 (xm, gen_lowpart (V16SImode, x), one));
  rtx kreg = gen_reg_rtx (HImode);
  rtx kneg = gen_reg_rtx (HImode);
  rtx max = force_reg (V16SImode,
                      gen_const_vec_duplicate (V16SImode, GEN_INT (0x7f7fffff)));
  ix86_knc_expand_mask_vec_cmp (kreg, LTU, xm, max);
  ix86_knc_expand_mask_vec_cmp (kneg, LT, xm,
                              force_reg (V16SImode, CONSTM1_RTX (V16SImode)));

  rtx half = force_reg (V16SFmode,
                       gen_const_vec_duplicate (V16SFmode,
                                                const_double_from_real_value
                                                  (dconsthalf, SFmode)));
  rtx y0 = gen_reg_rtx (V16SFmode);
  rtx g0 = gen_reg_rtx (V16SFmode);
  rtx h0 = gen_reg_rtx (V16SFmode);
  rtx r0 = gen_reg_rtx (V16SFmode);
  rtx g1 = gen_reg_rtx (V16SFmode);
  rtx h1 = gen_reg_rtx (V16SFmode);
  rtx d = gen_reg_rtx (V16SFmode);
  rtx result = gen_reg_rtx (V16SFmode);
  emit_insn (gen_knc_sqrt_rsqrt (y0, m));
  emit_insn (gen_knc_sqrt_mul (g0, m, y0));
  emit_insn (gen_knc_sqrt_mul (h0, y0, half));
  emit_insn (gen_knc_sqrt_fnma (r0, g0, h0, half));
  emit_insn (gen_knc_sqrt_fma (g1, g0, r0, g0));
  emit_insn (gen_knc_sqrt_fma (h1, h0, r0, h0));
  emit_insn (gen_knc_sqrt_fnma (d, g1, g1, m));
  emit_insn (gen_knc_fmav16sf_mask (result, d, h1, g1, kreg));
  emit_insn (gen_knc_sqrt_scale (result, result, j, kreg));
  rtx unused = gen_reg_rtx (V16SFmode);
  emit_insn (gen_knc_sqrt_invalid (unused, x, kneg));
  rtx table = force_reg (V16SImode,
                        gen_const_vec_duplicate (V16SImode, GEN_INT (0x206ad)));
  emit_insn (gen_knc_sqrt_fixup (operands[0], result, x, table));
}

/* Instruction counts, not measured latency.  Constants and transfers are
   costed separately.  Scalar SF sqrt uses x87; the vector sequence has
   19 instructions for sixteen elements (research section 2.5).  */
bool
ix86_knc_sqrt_rtx_cost (rtx x, machine_mode mode, int *total)
{
  if (GET_CODE (x) != SQRT || mode != V16SFmode)
    return false;
  *total = COSTS_N_INSNS (19);
  return true;
}

bool
ix86_knc_sqrt_stmt_p (int kind, stmt_vec_info stmt, tree vectype)
{
  if ((kind != vector_stmt && kind != scalar_stmt) || !stmt || !vectype
      || TYPE_MODE (vectype) != V16SFmode)
    return false;
  switch (gimple_call_combined_fn (stmt->stmt))
    {
    CASE_CFN_SQRT:
    CASE_CFN_SQRT_FN:
      return true;
    default:
      return false;
    }
}

unsigned
ix86_knc_sqrt_stmt_cost (tree vectype)
{
  return VECTOR_TYPE_P (vectype) ? 19 * ix86_cost->sse_op : ix86_cost->fsqrt;
}
