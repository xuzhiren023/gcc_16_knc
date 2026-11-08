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
#include "gimple-iterator.h"
#include "dumpfile.h"
#include "case-cfn-macros.h"
#include "cfgloop.h"
#include "tree-vectorizer.h"
#include "i386-builtins.h"

/* S6-03 docs/research/knc-characterization.md, measured 2026-11-06.
   Cost units below are per-core cycles (COSTS_N_INSNS), or twice cycles
   for register/memory moves.  E denotes an estimate, with its reason.
   CH denotes a row of that table.  No SSE/AVX feature is enabled here.  */
static stringop_algs knc_stringops[2] = {
  {libcall, {{-1, libcall, false}}},
  {libcall, {{-1, libcall, false}}}
}; /* E: no measured stringop crossover; keep size-independent libc calls.  */

static processor_costs
knc_processor_costs (unsigned chains)
{
  return {
    {
      2, {2, 2, 2}, {2, 2, 2}, /* CH l1.load_rt/store_normal, rounded 1.  */
      2,                       /* E: x87 register move, one U-pipe op.  */
      {2, 2, 4}, {2, 2, 4},    /* E: L1 moves; XF uses two word transfers.  */
      2, {2, 2}, {2, 2},       /* E: MMX unavailable; one-op placeholders.  */
      2, 2, 2,                /* CH vector issue; narrow modes share ZMM.  */
      {2, 2, 2, 2, 2}, {2, 2, 2, 2, 2}, /* CH masked_load/store.rt.  */
      24, 24,                 /* E: dependent memory bridge, CH latency 12.  */
      1, 1,                   /* CH kmov.rt: half cycle per core.  */
      {2, 2, 2}, {2, 2, 2},    /* E: mask memory access like scalar L1.  */
      1                       /* CH kmov.rt.  */
    },
    COSTS_N_INSNS (1),         /* CH scalar_add.lat: one cycle per core at
                                 density 4.  Dependent IV updates cannot use
                                 the half-cycle independent-add throughput.  */
    COSTS_N_INSNS (1),         /* E: LEA, one unpaired address operation.  */
    COSTS_N_INSNS (1), COSTS_N_INSNS (1), /* E: shifts, one scalar op.  */
    {COSTS_N_INSNS (3), COSTS_N_INSNS (3), COSTS_N_INSNS (3),
     COSTS_N_INSNS (3), COSTS_N_INSNS (6)}, /* E: integer mul, 3; TI two.  */
    0,                        /* E: multiply not dependent on set bits.  */
    {COSTS_N_INSNS (16), COSTS_N_INSNS (22), COSTS_N_INSNS (30),
     COSTS_N_INSNS (74), COSTS_N_INSNS (74)}, /* E: unmeasured integer div;
                                             retain generic estimates.  */
    COSTS_N_INSNS (1), COSTS_N_INSNS (1), /* E: extensions, one scalar op.  */
    8, 17, 10,                /* E: generic size/copy/clear thresholds;
                                 no code-size crossover measured.  */
    {2, 2, 2}, {2, 2, 2},    /* CH l1.load_rt/store_normal.  */
    {2, 2, 2, 2, 2}, {2, 2, 2, 2, 2}, /* CH masked_load/store.rt.  */
    {4, 4, 4, 4, 4},         /* CH unpack.rt: two ops.  */
    {4, 4, 4, 4, 4},         /* E: pack-store pair analogous to unpack.  */
    2, 2, 2,                /* CH vector issue.  */
    24, 24,                 /* E: memory bridge, CH dependent latency 12.  */
    0, 16,                  /* E: 8 cycles/lane from gather_scattered.rt;
                               the stmt hook refines by lines touched.  */
    0, 16,                  /* E: scatter uses gather's line-service cost.  */
    32, 512, 64,            /* ADR-0009 point 6: L1/L2 KB and line bytes;
                               both caches 8-way (no GCC cost field).  */
    4,                      /* E: one outstanding prefetch per HW thread;
                               no measured queue capacity yet.  */
    3,                      /* E: retain generic branch heuristic.  */
    COSTS_N_INSNS (1),       /* CH x87_add.rt.  */
    COSTS_N_INSNS (2),       /* CH x87_mul.rt.  */
    COSTS_N_INSNS (38),      /* CH x87_div.rt, not pipelined.  */
    COSTS_N_INSNS (1), COSTS_N_INSNS (1), /* E: fabs/fchs, one U op.  */
    COSTS_N_INSNS (38),      /* E: sqrt unmeasured, use serial divide.  */
    COSTS_N_INSNS (1),       /* CH vpaddd/vpxord.rt.  */
    COSTS_N_INSNS (1),       /* CH vaddps/pd.rt.  */
    COSTS_N_INSNS (1), COSTS_N_INSNS (1), /* CH vmulps/pd.rt.  */
    COSTS_N_INSNS (1), COSTS_N_INSNS (1), /* CH vfmadd213ps/pd.rt.  */
    COSTS_N_INSNS (38), COSTS_N_INSNS (38), /* E: no native vector div;
                                           scalar fallback; SF hook overrides.  */
    COSTS_N_INSNS (38), COSTS_N_INSNS (38), /* E: sqrt as above.  */
    COSTS_N_INSNS (1), COSTS_N_INSNS (1), COSTS_N_INSNS (1),
                            /* CH convert_up/down.rt, one memory op.  */
    COSTS_N_INSNS (1), COSTS_N_INSNS (1),
    COSTS_N_INSNS (1), COSTS_N_INSNS (1), /* CH vcvtfxpnt*.rt.  */
    1, 1, int (chains), int (chains),   /* E: scalar reassociation unchanged at one;
                              vectors: CH latency 4 / per-thread issue.  */
    {chains, chains, chains}, /* CH add/mul/FMA latency 4; E: dot/SAD use
                                 same bound until separately measured.  */
    2,                     /* CH latency 4 / minimum issue interval 2.  */
    knc_stringops, knc_stringops,
    COSTS_N_INSNS (4), COSTS_N_INSNS (2), /* E: generic branch estimates,
                                          branch latency unmeasured.  */
    "16", "16:11:8", "0:0:8", "16", /* E: generic code alignment pending
                                       front-end measurements.  */
    4, 2, COSTS_N_INSNS (2) /* E: generic small unroll/mispredict heuristics;
                              only reduction unrolling measured here.  */
  };
}

const processor_costs *
ix86_knc_tune_cost (const gcc_options *opts, const processor_costs *fallback)
{
  if (!opts->x_ix86_tune_string
      || strcmp (opts->x_ix86_tune_string, "knc"))
    return fallback;
  /* CH point 1: one thread issues every other cycle.  Two chains suffice
     at 1 or 2 threads, ceil(4/3) at 3, and one chain at 4.  */
  static const processor_costs four_threads = knc_processor_costs (1);
  static const processor_costs fewer_threads = knc_processor_costs (2);
  return opts->x_knc_threads_per_core == 4 ? &four_threads : &fewer_threads;
}

/* Costs that need KNC lowering knowledge rather than x86 mode splitting.  */
int
ix86_knc_stmt_cost (int kind, machine_mode mode, stmt_vec_info stmt)
{
  switch (kind)
    {
    case vector_gather_load:
    case vector_scatter_store:
      {
        unsigned lanes = GET_MODE_NUNITS (mode);
        unsigned lines = lanes; /* E: unknown indices, one line per lane.  */
        if (stmt && STMT_VINFO_DR_INFO (stmt))
          {
            tree step = DR_STEP (STMT_VINFO_DR_INFO (stmt)->dr);
            /* A zero step represents unknown gather indices, not a stride.  */
            if (step && !integer_zerop (step) && tree_fits_shwi_p (step))
              {
                HOST_WIDE_INT stride = tree_to_shwi (step);
                /* Avoid overflow; strides of at least a line touch LANES.  */
                if (stride > -64 && stride < 64)
                  lines = MAX (1U, (unsigned) CEIL (lanes * (stride < 0 ? -stride : stride), 64));
              }
          }
        /* CH gather_all.rt = 7, gather_scattered.rt = 127.  E: interpolate
           line service plus retry overhead; stride 2 FP32 = 14 cycles.
           Scatter is estimated from the same line service, not measured.  */
        return COSTS_N_INSNS (7 * lines + lines * (lines - 1) / 16);
      }
    case vec_perm:
      return COSTS_N_INSNS (1); /* CH vpermd/vpermf32x4/swizzle.rt.  */
    case vec_promote_demote:
      return COSTS_N_INSNS (1); /* CH convert_up/down.rt.  */
    case vector_load:
    case vector_store:
      return COSTS_N_INSNS (1); /* CH masked_load/store.rt incl sparse.  */
    case unaligned_load:
    case unaligned_store:
      /* A masked access of unknown alignment is a gather or scatter over
	 one or two lines (knc_maskmove, S8-02).  C6: the gather tail of
	 y = x + 1 costs about three scalar iterations against two for the
	 aligned tail, so each weighs twice an unpack pair.  */
      if (stmt && gimple_call_internal_p (stmt->stmt))
	return COSTS_N_INSNS (4);
      return COSTS_N_INSNS (2); /* CH unpack.rt; E: store pair analogous.  */
    case scalar_to_vec:
    case vec_to_scalar:
      /* CH spill_reload.lat = 12; E: exposed dependency cost of the memory
         bridge, per-thread issue interval max(2,density), rounded up.
         This deliberately differs from an independent spill's throughput.  */
      return COSTS_N_INSNS (CEIL (12, MAX (2, knc_threads_per_core)));
    default:
      return -1;
    }
}

bool
ix86_knc_reduction_p (stmt_vec_info stmt)
{
  if (!stmt || !vect_is_reduction (stmt) || !is_gimple_assign (stmt->stmt))
    return false;
  tree type = TREE_TYPE (gimple_assign_lhs (stmt->stmt));
  if (FLOAT_TYPE_P (type) && !flag_associative_math)
    return false;
  tree_code code = gimple_assign_rhs_code (stmt->stmt);
  return code == PLUS_EXPR || code == MINUS_EXPR || code == MULT_EXPR;
}

/* These middle-end-only builtins use the low eight signed dwords of a
   V16SI index.  Keep their codes outside the shared x86 builtin tables.
   ADR-0008 point 6; Intel 327364-001 pp.297-299, 487-489, 533-535,
   601-603.  The eight-lane forms clear k[15:8], so the same jknzd retry
   loop used by the sixteen-lane patterns tests completion correctly.  */
enum knc_builtin_code
{
  KNC_BUILTIN_GATHER_DF = IX86_BUILTIN_MAX + 1,
  KNC_BUILTIN_GATHER_DI,
  KNC_BUILTIN_SCATTER_DF,
  KNC_BUILTIN_SCATTER_DI
};

/* Keep the internal decls alive without registering front-end builtins.
   Their distinct function types identify the gather-only ABI exemption.  */
static GTY(()) tree knc_indexed_decls[4];

tree
ix86_knc_builtin_decl (unsigned code)
{
  if (code < KNC_BUILTIN_GATHER_DF || code > KNC_BUILTIN_SCATTER_DI)
    return error_mark_node;

  unsigned slot = code - KNC_BUILTIN_GATHER_DF;
  if (knc_indexed_decls[slot])
    return knc_indexed_decls[slot];

  bool scatter = code >= KNC_BUILTIN_SCATTER_DF;
  bool fp = code == KNC_BUILTIN_GATHER_DF || code == KNC_BUILTIN_SCATTER_DF;
  tree data = build_vector_type (fp ? double_type_node : intDI_type_node, 8);
  tree index = build_vector_type (intSI_type_node, 16);
  tree mask = short_unsigned_type_node;
  tree type = scatter
    ? build_function_type_list (void_type_node, ptr_type_node, mask, index,
				data, integer_type_node, NULL_TREE)
    : build_function_type_list (data, data, const_ptr_type_node, index,
				mask, integer_type_node, NULL_TREE);
  const char *name = scatter
    ? (fp ? "__builtin_knc_scatter_v8df" : "__builtin_knc_scatter_v8di")
    : (fp ? "__builtin_knc_gather_v8df" : "__builtin_knc_gather_v8di");
  tree decl = build_fn_decl (name, build_distinct_type_copy (type));
  set_decl_built_in_function (decl, BUILT_IN_MD, code);
  DECL_PURE_P (decl) = !scatter;
  TREE_NOTHROW (decl) = !flag_non_call_exceptions;
  return knc_indexed_decls[slot] = decl;
}

/* Only these internal calls expand directly to a pseudo-register result.
   A user function with the same signature still has the ordinary KNC ABI
   and must not inherit this exemption through a canonical function type.  */
bool
ix86_knc_gather_type_p (const_tree fntype_or_decl)
{
  const_tree fntype = fntype_or_decl && DECL_P (fntype_or_decl)
    ? TREE_TYPE (fntype_or_decl) : fntype_or_decl;
  return fntype
    && ((knc_indexed_decls[0]
         && fntype == TREE_TYPE (knc_indexed_decls[0]))
        || (knc_indexed_decls[1]
            && fntype == TREE_TYPE (knc_indexed_decls[1])));
}

static tree
knc_indexed_builtin (const_tree vectype, const_tree index_type, int scale,
		     bool scatter)
{
  if (!TARGET_64BIT || TREE_CODE (index_type) != INTEGER_TYPE
      || TYPE_MODE (index_type) != SImode || TYPE_UNSIGNED (index_type)
      || scale <= 0 || scale > 8 || (scale & (scale - 1)) != 0)
    return NULL_TREE;

  machine_mode mode = TYPE_MODE (vectype);
  if (mode != V8DFmode && mode != V8DImode)
    return NULL_TREE;
  unsigned code = scatter ? KNC_BUILTIN_SCATTER_DF : KNC_BUILTIN_GATHER_DF;
  return ix86_knc_builtin_decl (code + (mode == V8DImode));
}

tree
ix86_knc_vectorize_builtin_gather (const_tree vectype, const_tree index_type,
				  int scale)
{
  return knc_indexed_builtin (vectype, index_type, scale, false);
}

tree
ix86_knc_vectorize_builtin_scatter (const_tree vectype, const_tree index_type,
				   int scale)
{
  return knc_indexed_builtin (vectype, index_type, scale, true);
}

rtx
ix86_knc_expand_builtin (tree exp, rtx target)
{
  unsigned code = DECL_MD_FUNCTION_CODE (TREE_OPERAND (CALL_EXPR_FN (exp), 0));
  gcc_assert (code >= KNC_BUILTIN_GATHER_DF && code <= KNC_BUILTIN_SCATTER_DI);
  bool scatter = code >= KNC_BUILTIN_SCATTER_DF;
  bool fp = code == KNC_BUILTIN_GATHER_DF || code == KNC_BUILTIN_SCATTER_DF;
  machine_mode mode = fp ? V8DFmode : V8DImode;
  rtx ops[5];
  for (unsigned i = 0; i < 5; ++i)
    ops[i] = expand_normal (CALL_EXPR_ARG (exp, i));
  rtx base = force_reg (Pmode, ops[scatter ? 0 : 1]);
  rtx index = force_reg (V16SImode, ops[2]);
  rtx mask = force_reg (HImode, ops[scatter ? 1 : 3]);
  rtx data = force_reg (mode, ops[scatter ? 3 : 0]);
  rtx scale = ops[4];
  gcc_assert (const1248_operand (scale, SImode));
  rtvec address = scatter ? gen_rtvec (4, base, index, scale, mask)
			 : gen_rtvec (3, base, index, scale);
  rtx mem = gen_rtx_MEM (BLKmode,
		       gen_rtx_UNSPEC (DImode, address, UNSPEC_KNC_VSIBADDR));
  if (scatter)
    {
      emit_insn (fp
	? gen_knc_scatterv8df (base, index, scale, data, NULL_RTX, mem, mask)
	: gen_knc_scatterv8di (base, index, scale, data, NULL_RTX, mem, mask));
      return const0_rtx;
    }
  if (!target || !register_operand (target, mode))
    target = gen_reg_rtx (mode);
  emit_insn (fp
    ? gen_knc_gatherv8df (target, data, base, index, scale, NULL_RTX, mem, mask)
    : gen_knc_gatherv8di (target, data, base, index, scale, NULL_RTX, mem, mask));
  return target;
}

/* Exactly the packed storage modes in ADR-0005 point 1.  This predicate
   does not advertise arithmetic, an ABI, or automatic vectorization.  */
bool
ix86_knc_packed_mode_p (machine_mode mode)
{
  return TARGET_KNC && TARGET_64BIT
	 && (mode == V16SFmode || mode == V8DFmode
	     || mode == V16SImode || mode == V8DImode);
}

/* ADR-0008 point 7: these modes transport sixteen narrow elements only.
   Do not include them in packed_mode_p, which also enables arithmetic.  */
bool
ix86_knc_transport_mode_p (machine_mode mode)
{
  return TARGET_KNC && TARGET_64BIT
	 && (mode == V16QImode || mode == V16HImode);
}

/* V4SF is only the shared SysV varargs save loop's 16-byte transport.
   It does not enable SSE arithmetic or change the packed storage modes.  */
bool
ix86_knc_vector_move_mode_p (machine_mode mode)
{
  return ix86_knc_packed_mode_p (mode)
	 || ix86_knc_transport_mode_p (mode)
	 || (TARGET_KNC && TARGET_64BIT && mode == V4SFmode);
}

/* ADR-0005 point 5: try one full ZMM vector size.  Unsupported scalar
   types retain word_mode; tails and peeling use the scalar expanders.  */
machine_mode
ix86_knc_preferred_simd_mode (scalar_mode mode)
{
  switch (mode)
    {
    case E_QImode: return V16QImode;
    case E_HImode: return V16HImode;
    case E_SFmode: return V16SFmode;
    case E_DFmode: return V8DFmode;
    case E_SImode: return V16SImode;
    case E_DImode: return V8DImode;
    default: return word_mode;
    }
}

/* Pair the narrow transport modes with sixteen dwords, preserving an
   explicit lane count requested by the vectorizer.  Leave the existing
   eight-lane width conversions and every non-KNC target unchanged.  */
opt_machine_mode
ix86_knc_vectorize_related_mode (machine_mode vector_mode,
				scalar_mode element_mode, poly_uint64 nunits)
{
  if (TARGET_KNC && TARGET_64BIT
      && (ix86_knc_transport_mode_p (vector_mode)
	  || vector_mode == V16SImode || vector_mode == V16SFmode)
      && (known_eq (nunits, 0U) || known_eq (nunits, 16U)))
    {
      switch (element_mode)
	{
	case E_QImode: return V16QImode;
	case E_HImode: return V16HImode;
	case E_SImode: return V16SImode;
	case E_SFmode: return V16SFmode;
	default: break;
	}
    }
  return default_vectorize_related_mode (vector_mode, element_mode, nunits);
}

/* Intel 327364-001 VLOADUNPACKLD p.361: pairs accept element,
   not arbitrary byte, alignment.  Both SLP and loop vectorization call
   vect_supportable_dr_alignment, whose is_packed is not_size_aligned:
   get_object_alignment of the scalar reference must cover its size.
   Unknown vector misalignment is therefore safe only when !is_packed.
   Gather/scatter lanes have the same element alignment requirement
   (Intel 327364-001 p.26); their vector offsets need no line alignment.  */
bool
ix86_knc_support_vector_misalignment (machine_mode mode, int misalignment,
                                      bool is_packed, bool is_gather_scatter)
{
  if (!TARGET_KNC)
    return default_builtin_support_vector_misalignment
      (mode, misalignment, is_packed, is_gather_scatter);
  if (!ix86_knc_packed_mode_p (mode) || is_packed)
    return false;
  unsigned int bytes = GET_MODE_SIZE (GET_MODE_INNER (mode));
  return misalignment == -1
         || (misalignment >= 0 && misalignment % bytes == 0);
}

/* S4-05 / S8-02 (ADR-0007 point 7, ADR-0009 amendment 1 point 9): a masked
   epilogue or a whole-loop mask is one aligned masked move per access
   known 64-byte aligned and one gather or scatter per access of unknown
   alignment (knc_maskmove), so every contiguous forward access
   qualifies.  *ALIGNED reports whether every access is known 64-byte
   aligned, which selects the C6 crossover against x87.  After an
   alignment peel the epilogue's references have unknown misalignment
   (PR 92351): they take the gather form, which is still correct.  */
static const char *
knc_masked_loop_rejection (loop_vec_info loop_vinfo, bool *aligned)
{
  *aligned = true;
  if (!ix86_knc_packed_mode_p (loop_vinfo->vector_mode))
    return "unsupported mode";
  if (LOOP_REQUIRES_VERSIONING_FOR_ALIGNMENT (loop_vinfo))
    return "versioned for alignment";
  for (data_reference *dr : LOOP_VINFO_DATAREFS (loop_vinfo))
    {
      dr_vec_info *dr_info = loop_vinfo->lookup_dr (dr);
      stmt_vec_info stmt_info = dr_info->stmt;

      /* Gather/scatter references have no DR_STEP.  */
      if (STMT_VINFO_GATHER_SCATTER_P (stmt_info))
	return "gather/scatter needs a different masked epilogue";
      if (!STMT_VINFO_RELEVANT_P (stmt_info) || integer_zerop (DR_STEP (dr)))
	continue;
      tree vectype = STMT_VINFO_VECTYPE (stmt_info);
      if (STMT_VINFO_STRIDED_P (stmt_info)
	  || STMT_VINFO_GROUPED_ACCESS (stmt_info)
	  || TREE_CODE (DR_STEP (dr)) != INTEGER_CST
	  || tree_int_cst_sgn (DR_STEP (dr)) < 0)
	return "non-contiguous or backward masked access";
      if (!vectype)
	return "access without a vector type";
      if (dr_misalignment (dr_info, vectype) != 0
	  || !known_eq (DR_TARGET_ALIGNMENT (dr_info), 64U))
	*aligned = false;
    }
  return nullptr;
}

/* C6 in docs/research/knc-characterization.md, lengths 1-33 of y = x + 1:
   the aligned masked tail beats the x87 loop from n = 2 at 1 and 4
   threads per core; the gather tail beats it from n = 3 at 1 thread and
   from n = 4 at 4 threads.  Scalar stays up to this many iterations.  2
   and 3 threads were not measured and take the 4-thread value.  */
static unsigned HOST_WIDE_INT
knc_x87_tail_limit (bool aligned)
{
  if (aligned)
    return 1;
  return knc_threads_per_core == 1 ? 2 : 3;
}

/* The scalar iterations the full-vector loop leaves over when the trip
   count is a compile-time constant, or -1 when it is not known here.  */
static HOST_WIDE_INT
knc_known_remainder (loop_vec_info loop_vinfo)
{
  if (!LOOP_VINFO_NITERS_KNOWN_P (loop_vinfo)
      || LOOP_VINFO_PEELING_FOR_ALIGNMENT (loop_vinfo) < 0)
    return -1;
  HOST_WIDE_INT niters = (LOOP_VINFO_INT_NITERS (loop_vinfo)
			  - LOOP_VINFO_PEELING_FOR_ALIGNMENT (loop_vinfo)
			  - (LOOP_VINFO_PEELING_FOR_GAPS (loop_vinfo) ? 1 : 0));
  return niters % LOOP_VINFO_VECT_FACTOR (loop_vinfo).to_constant ();
}

/* For ix86_vector_costs::finish_cost: ask for a masked epilogue unless a
   known remainder is within C6's x87 limit (or there is none).  */
bool
ix86_knc_masked_epilogue_p (loop_vec_info loop_vinfo)
{
  bool aligned;
  if (knc_masked_loop_rejection (loop_vinfo, &aligned))
    return false;
  HOST_WIDE_INT rest = knc_known_remainder (loop_vinfo);
  return rest < 0
	 || (unsigned HOST_WIDE_INT) rest > knc_x87_tail_limit (aligned);
}

/* For ix86_vector_costs::finish_cost, on a main loop over partial vectors
   with a known trip count: whether that count is within C6's x87 limit,
   so that the whole-loop mask must lose.  KNC has no narrower vector mode,
   so this replaces the generic rejection in favour of a narrower mode.  */
bool
ix86_knc_short_masked_loop_p (loop_vec_info loop_vinfo)
{
  bool aligned;
  knc_masked_loop_rejection (loop_vinfo, &aligned);
  return ((unsigned HOST_WIDE_INT) LOOP_VINFO_INT_NITERS (loop_vinfo)
	  <= knc_x87_tail_limit (aligned));
}

/* Report costed candidates, not the vectorizer's eventual selection.  The
   existing loop-vectorized diagnostic identifies that selection.  Keep this
   observational: in particular do not request masking or change any cost.
   ADR-0006 entry dispatch; ADR-0007 point 7 alignment policy.  */
void
ix86_knc_report_loop (vec_info *vinfo, bool scalar_p)
{
  loop_vec_info loop_vinfo = dyn_cast<loop_vec_info> (vinfo);
  if (!loop_vinfo || scalar_p || !dump_enabled_p ())
    return;

  const dump_flags_t info = MSG_OPTIMIZED_LOCATIONS | MSG_PRIORITY_USER_FACING;
  const dump_flags_t missed = MSG_MISSED_OPTIMIZATION | MSG_PRIORITY_USER_FACING;
  bool aligned;
  const char *reason = knc_masked_loop_rejection (loop_vinfo, &aligned);
  HOST_WIDE_INT rest = knc_known_remainder (loop_vinfo);
  unsigned HOST_WIDE_INT limit = knc_x87_tail_limit (aligned);
  unsigned HOST_WIDE_INT vf = LOOP_VINFO_VECT_FACTOR (loop_vinfo).to_constant ();
  dump_printf_loc (info, vect_location,
		   "KNC candidate mode %s, factor = %wu (%s loop)\n",
		   GET_MODE_NAME (loop_vinfo->vector_mode), vf,
		   LOOP_VINFO_EPILOGUE_P (loop_vinfo) ? "epilogue" : "main");
  int peel = LOOP_VINFO_PEELING_FOR_ALIGNMENT (loop_vinfo);
  dump_printf_loc (info, vect_location,
		   "KNC candidate scalar peel: %s (alignment iterations %d, "
		   "gap peel %d)\n",
		   peel || LOOP_VINFO_PEELING_FOR_GAPS (loop_vinfo)
		   ? "required" : "not required", peel,
		   (int) LOOP_VINFO_PEELING_FOR_GAPS (loop_vinfo));
  if (reason)
    {
      dump_printf_loc (info, vect_location,
		       "KNC candidate scalar epilogue: %s\n", reason);
      dump_printf_loc (missed, vect_location,
		       "KNC rejected masked epilogue candidate: %s\n", reason);
    }
  else if (rest >= 0 && (unsigned HOST_WIDE_INT) rest <= limit)
    dump_printf_loc (info, vect_location,
		     "KNC candidate scalar epilogue: known remainder %wd "
		     "within the x87 limit %wu (C6)\n", rest, limit);
  else
    dump_printf_loc (info, vect_location,
		     "KNC candidate masked epilogue eligible: %s; generic "
		     "profitability and operation support still apply\n",
		     aligned ? "every access 64-byte aligned"
		     : "alignment unknown, a gather or scatter per access");

  unsigned gathers = 0, scatters = 0, converting = 0, masks = 0;
  for (data_reference *dr : LOOP_VINFO_DATAREFS (loop_vinfo))
    {
      stmt_vec_info stmt = loop_vinfo->lookup_dr (dr)->stmt;
      if (!STMT_VINFO_RELEVANT_P (stmt))
	continue;
      if (STMT_VINFO_GATHER_SCATTER_P (stmt))
	(DR_IS_READ (dr) ? gathers : scatters)++;
      tree type = STMT_VINFO_VECTYPE (stmt);
      if (type)
	{
	  /* Mixed-type loops can have several modes but one loop VF.  */
	  dump_printf_loc (info, vect_location,
			   "KNC candidate access mode %s, factor = %wu\n",
			   GET_MODE_NAME (TYPE_MODE (type)), vf);
	  if (ix86_knc_transport_mode_p (TYPE_MODE (type))
	      || TYPE_PRECISION (TREE_TYPE (DR_REF (dr)))
		 != TYPE_PRECISION (TREE_TYPE (type)))
	    converting++;
	}
      gimple *call = stmt->stmt;
      if (gimple_call_internal_p (call, IFN_MASK_LOAD)
	  || gimple_call_internal_p (call, IFN_MASK_STORE))
	masks++;
    }
  dump_printf_loc (info, vect_location,
		   "KNC candidate accesses: gather %u, scatter %u, "
		   "converting %u, masked %u; reductions %u\n",
		   gathers, scatters, converting, masks,
		   LOOP_VINFO_REDUCTIONS (loop_vinfo).length ());
  for (unsigned i = 0; i < loop_vinfo->nbbs; ++i)
    for (gimple_stmt_iterator gsi = gsi_start_bb (loop_vinfo->bbs[i]);
	 !gsi_end_p (gsi); gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (is_gimple_call (stmt) && !gimple_call_internal_p (stmt))
	  {
	    tree decl = gimple_call_fndecl (stmt);
	    if (decl)
	      dump_printf_loc (info, stmt,
			       "KNC candidate body call %qD "
			       "(before target lowering)\n", decl);
	    else
	      dump_printf_loc (info, stmt,
			       "KNC candidate indirect body call\n");
	  }
      }
  dump_printf_loc (info, vect_location,
		   "KNC candidate: helper calls introduced by target lowering "
		   "and register-allocation spills are not known at vector "
		   "costing\n");
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
      /* GCC 5 passed 32-byte short vectors in memory as BLKmode.  The
         V16HI transport mode must not change that existing ABI.  */
      if (mode == V16HImode)
        return false;
      return mode == XImode || ix86_knc_packed_mode_p (mode)
	     || ix86_knc_transport_mode_p (mode);
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
  if (ix86_knc_transport_mode_p (mode))
    {
      if (CONSTANT_P (operands[1]))
	operands[1] = validize_mem (force_const_mem (mode, operands[1]));
      if (MEM_P (operands[0]) && !REG_P (operands[1]))
	operands[1] = force_reg (mode, operands[1]);
      /* Like ix86_expand_vector_move, expose the stack adjustment before
         a vector argument store.  Transport patterns cannot use PRE_DEC.  */
      if (mode == V16HImode && push_operand (operands[0], VOIDmode))
	operands[0] = emit_move_resolve_push (mode, operands[0]);
      /* A language-level unaligned move still needs an exact-size copy.
	 This does not provide movmisalign to the vectorizer.  */
      rtx mem = MEM_P (operands[0]) ? operands[0] : operands[1];
      if (MEM_P (mem) && misaligned_operand (mem, mode))
	{
	  gcc_assert (can_create_pseudo_p ());
	  rtx slot = assign_stack_temp (mode, GET_MODE_SIZE (mode));
	  bool store = MEM_P (operands[0]);
	  if (store)
	    emit_move_insn (slot, operands[1]);
	  for (unsigned off = 0; off < GET_MODE_SIZE (mode); off += 8)
	    emit_move_insn (adjust_address (store ? mem : slot, DImode, off),
			   force_reg (DImode, adjust_address
				      (store ? slot : mem, DImode, off)));
	  if (!store)
	    emit_move_insn (operands[0], slot);
	}
      else
	emit_insn (mode == V16QImode
		   ? gen_knc_transport_movev16qi (operands[0], operands[1])
		   : gen_knc_transport_movev16hi (operands[0], operands[1]));
      return;
    }
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

/* The card confirms saturation (Intel 327364-001 pp.382-384).  An AND
   before unsigned saturation gives C's low-bit truncation for both signed
   and unsigned destinations.  Split after combine so the ordinary store
   can absorb the truncate; a surviving register result uses a visible
   exact-size stack transfer instead.  */
void
ix86_knc_expand_narrow_store (rtx dst, rtx src)
{
  machine_mode mode = GET_MODE (dst);
  gcc_assert (ix86_knc_transport_mode_p (mode));
  rtx mem = MEM_P (dst) ? dst
	    : assign_stack_temp (mode, GET_MODE_SIZE (mode));
  rtx mask = gen_const_vec_duplicate (V16SImode,
				     GEN_INT (mode == V16QImode ? 255 : 65535));
  rtx low = expand_simple_binop (V16SImode, AND, src, mask, NULL_RTX,
				1, OPTAB_DIRECT);
  emit_insn (gen_rtx_SET (mem, gen_rtx_US_TRUNCATE (mode, low)));
  if (!MEM_P (dst))
    emit_move_insn (dst, mem);
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
  if (same && ix86_knc_packed_mode_p (mode))
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

/* KNC has no generic x86 arch macro.  */
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
	 || ix86_knc_packed_mode_p (mode)
	 || ix86_knc_transport_mode_p (mode) || mode == XImode;
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
ix86_knc_expand_reduc (enum rtx_code code, rtx dest, rtx src, bool number_p)
{
  machine_mode mode = GET_MODE (src);
  unsigned int nelt = GET_MODE_NUNITS (mode);
  bool carry = mode == V8DImode && code == PLUS;
  gcc_assert (ix86_knc_packed_mode_p (mode));
  gcc_assert (code == PLUS
              || ((mode == V16SFmode || mode == V8DFmode)
                  && (code == SMIN || code == SMAX))
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
      else if (number_p)
        emit_insn (gen_rtx_SET (result,
                   gen_rtx_UNSPEC (mode, gen_rtvec (2, src, other),
                                   code == SMIN ? UNSPEC_KNC_FMIN
                                                : UNSPEC_KNC_FMAX)));
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

/* Small RTL constructors shared by the two division paths.  Each operation
   remains visible to allocation and scheduling.  */
static rtx
knc_div_int (HOST_WIDE_INT n)
{
  return force_reg (V16SImode,
                    gen_const_vec_duplicate (V16SImode, gen_int_mode (n, SImode)));
}

static rtx
knc_div_float (HOST_WIDE_INT bits)
{
  return gen_lowpart (V16SFmode, knc_div_int (bits));
}

static rtx
knc_div_bin (rtx_code code, rtx a, rtx b)
{
  return expand_simple_binop (GET_MODE (a), code, a, b, NULL_RTX,
                             true, OPTAB_DIRECT);
}

static rtx
knc_div_cmp (rtx_code code, rtx a, rtx b)
{
  rtx k = gen_reg_rtx (HImode);
  ix86_knc_expand_mask_vec_cmp (k, code, a, b);
  return k;
}

static rtx
knc_div_blend (rtx k, rtx a, rtx b)
{
  rtx d = gen_reg_rtx (GET_MODE (a));
  emit_insn (gen_rtx_SET (d, gen_rtx_VEC_MERGE (GET_MODE (a), a, b, k)));
  return d;
}

/* Correct rounding: docs/research/knc-div-sqrt.md 2.2--2.4, Lemma B.
   With integer divisor significand B, a/b is separated from midpoints
   by at least u/(2B), and from other floats by u/B (Lemma M).  A faithful
   q has an exactly representable residual a-b*q (Lemma A).  If
   |b*y-1| < 1/(B+1), fma(a-b*q,y,q) stays on the correct side of every
   rounding boundary, in every mode, and has the correct inexact flag.
   The first correction makes q1 faithful; E1/E2/E4 check these claims.
   Step 15 is essential: B=0xffffff with y0=1/2 otherwise leaves the
   reciprocal error equal to, not smaller than, 1/(B+1).

   Reduced operands avoid estimate DAZ/FZ.  Intermediates use RN-SAE;
   only active regular lanes reach the dynamic-RC final FMA and scale.
   A lane test guards the special/tiny path.  Special operands are
   retained, regular operands become signed one.  Reciprocal of zero
   raises #Z only for a finite nonzero numerator.  Tiny RN midpoints
   are corrected using the sign of the exact residual; directed modes
   need no double-rounding correction.  All memory traffic is explicit. */
void
ix86_knc_expand_div (rtx *operands, bool conditional)
{
  rtx a = force_reg (V16SFmode, operands[conditional ? 2 : 1]);
  rtx b = force_reg (V16SFmode, operands[conditional ? 3 : 2]);
  rtx active = conditional ? operands[1] : force_reg (HImode, constm1_rtx);
  rtx otherwise = force_reg (V16SFmode, conditional ? operands[4]
                                                  : CONST0_RTX (V16SFmode));
  rtx zero = force_reg (V16SFmode, CONST0_RTX (V16SFmode));
  rtx ma = gen_reg_rtx (V16SFmode), mb = gen_reg_rtx (V16SFmode);
  rtx ea = gen_reg_rtx (V16SFmode), eb = gen_reg_rtx (V16SFmode);
  rtx kf = gen_reg_rtx (V16SFmode), t = gen_reg_rtx (V16SFmode);
  rtx k = gen_reg_rtx (V16SImode);
  emit_insn (gen_knc_div_mant (ma, a, GEN_INT (0)));
  emit_insn (gen_knc_div_mant (mb, b, GEN_INT (4)));
  emit_insn (gen_knc_sqrt_exp (ea, a));
  emit_insn (gen_knc_sqrt_exp (eb, b));
  emit_insn (gen_knc_div_sub (kf, ea, eb));
  emit_insn (gen_knc_div_sub (t, kf, kf));
  rtx regular = knc_div_bin (AND, active, knc_div_cmp (EQ, t, zero));
  emit_insn (gen_knc_sqrt_cvt (k, kf));
  rtx sign = knc_div_int (0x80000000);
  rtx sb = knc_div_bin (AND, gen_lowpart (V16SImode, b), sign);
  ma = gen_lowpart (V16SFmode,
                   knc_div_bin (XOR, gen_lowpart (V16SImode, ma), sb));
  rtx y0 = gen_reg_rtx (V16SFmode), e0 = gen_reg_rtx (V16SFmode);
  rtx y1 = gen_reg_rtx (V16SFmode), q0 = gen_reg_rtx (V16SFmode);
  rtx r0 = gen_reg_rtx (V16SFmode), q1 = gen_reg_rtx (V16SFmode);
  rtx r1 = gen_reg_rtx (V16SFmode), qm = gen_reg_rtx (V16SFmode);
  emit_insn (gen_knc_div_rcp (y0, mb));
  emit_insn (gen_knc_sqrt_fnma (e0, mb, y0, knc_div_float (0x3f800000)));
  emit_insn (gen_knc_sqrt_fma (y1, e0, y0, y0));
  /* Integer equality is quiet even for an inactive signalling NaN.  */
  rtx all1 = knc_div_cmp (EQ, gen_lowpart (V16SImode, mb),
                         knc_div_int (0x3fffffff));
  y1 = knc_div_blend (all1, knc_div_float (0x3f000001), y1);
  emit_insn (gen_knc_sqrt_mul (q0, ma, y1));
  emit_insn (gen_knc_sqrt_fnma (r0, mb, q0, ma));
  emit_insn (gen_knc_sqrt_fma (q1, r0, y1, q0));
  emit_insn (gen_knc_sqrt_fnma (r1, mb, q1, ma));
  emit_insn (gen_knc_fmav16sf_mask (qm, r1, y1, q1, regular));
  rtx result = gen_reg_rtx (V16SFmode);
  emit_insn (gen_knc_sqrt_scale (result, qm, k, regular));
  rtx abs_mask = knc_div_int (0x7fffffff);
  rtx ab = knc_div_bin (AND, gen_lowpart (V16SImode, result), abs_mask);
  rtx tiny = knc_div_bin (AND, regular,
                          knc_div_cmp (LEU, ab, knc_div_int (0x00800000)));
  rtx special = knc_div_bin (XOR, active, regular);
  rtx rare = knc_div_bin (IOR, special, tiny);
  rtx_code_label *done = gen_label_rtx ();
  emit_jump_insn (gen_knc_jkeq (rare, done));

  rtx ai = gen_lowpart (V16SImode, a), bi = gen_lowpart (V16SImode, b);
  rtx aa = knc_div_bin (AND, ai, abs_mask);
  rtx bb = knc_div_bin (AND, bi, abs_mask);
  rtx izero = knc_div_int (0), ione = knc_div_int (1);
  rtx finite_limit = knc_div_int (0x7f7fffff);
  rtx afin = knc_div_cmp (LTU, knc_div_bin (MINUS, aa, ione), finite_limit);
  rtx bfin = knc_div_cmp (LTU, knc_div_bin (MINUS, bb, ione), finite_limit);
  rtx aone = knc_div_bin (IOR, knc_div_bin (AND, ai, sign),
                          knc_div_int (0x3f800000));
  rtx bone = knc_div_bin (IOR, sb, knc_div_int (0x3f800000));
  rtx ap = gen_lowpart (V16SFmode, knc_div_blend (afin, aone, ai));
  rtx bp = gen_lowpart (V16SFmode, knc_div_blend (bfin, bone, bi));
  rtx bz = knc_div_cmp (EQ, bb, izero);
  rtx bzspecial = knc_div_bin (AND, special, bz);
  rtx reciprocal = gen_lowpart (V16SFmode,
                                knc_div_bin (IOR, sb, knc_div_int (0x7f800000)));
  rtx nonzero = knc_div_bin (XOR, special, bzspecial);
  rtx recip = gen_reg_rtx (V16SFmode), unused = gen_reg_rtx (V16SFmode);
  emit_insn (gen_knc_div_rcp_flags (recip, bp, reciprocal, nonzero));
  emit_insn (gen_knc_div_rcp_flags (unused, bp, zero,
                                    knc_div_bin (AND, bzspecial, afin)));
  emit_insn (gen_rtx_SET (result,
                          gen_rtx_VEC_MERGE (V16SFmode,
                            gen_rtx_MULT (V16SFmode, ap, recip), result, special)));

  /* d is the number of significand bits lost on the subnormal grid.
     Shifts >=32 produce zero, so the midpoint test also excludes d<=0
     and d>24 without special scalar control flow.  */
  rtx qabs = knc_div_bin (AND, gen_lowpart (V16SImode, qm), abs_mask);
  rtx qe = gen_reg_rtx (V16SImode);
  emit_insn (gen_knc_div_shift_right (qe, qabs, knc_div_int (23)));
  rtx d = knc_div_bin (MINUS, ione, knc_div_bin (PLUS, qe, k));
  rtx rr = gen_reg_rtx (V16SFmode);
  emit_insn (gen_knc_sqrt_fnma (rr, mb, qm, ma));
  rtx nonexact = knc_div_cmp (NE,
                             knc_div_bin (AND, gen_lowpart (V16SImode, rr), abs_mask),
                             izero);
  rtx underflow = knc_div_bin (AND, tiny,
                               knc_div_bin (AND, nonexact,
                                 knc_div_cmp (GT, d, izero)));
  emit_insn (gen_knc_div_underflow (unused, knc_div_float (0x00800000),
                                    knc_div_float (0x3f7fffff), underflow));
  rtx csr_mem = assign_stack_temp (SImode, 4);
  emit_insn (gen_knc_div_mxcsr (csr_mem));
  rtx csr = force_reg (SImode, csr_mem);
  rtx rc = knc_div_bin (AND, csr, GEN_INT (0x6000));
  emit_cmp_and_jump_insns (rc, const0_rtx, NE, NULL_RTX, SImode,
                           true, done);
  rtx sig = knc_div_bin (IOR,
                         knc_div_bin (AND, qabs, knc_div_int (0x007fffff)),
                         knc_div_int (0x00800000));
  rtx shifted = gen_reg_rtx (V16SImode);
  emit_insn (gen_knc_div_shift_left (shifted, sig,
                                     knc_div_bin (MINUS, knc_div_int (32), d)));
  rtx mid = knc_div_bin (AND, tiny, knc_div_cmp (EQ, shifted, sign));
  /* Integer sign tests avoid signalling compares on special-lane NaNs. */
  rtx ri = gen_lowpart (V16SImode, rr);
  rtx up = knc_div_bin (AND, mid,
                         knc_div_cmp (GT, ri, izero));
  rtx down = knc_div_bin (AND, mid,
                           knc_div_bin (AND, nonexact,
                             knc_div_cmp (LT, ri, izero)));
  emit_insn (gen_knc_div_scale_ru (result, qm, k, result, up));
  emit_insn (gen_knc_div_scale_rd (result, qm, k, result, down));
  emit_label (done);
  emit_move_insn (operands[0], knc_div_blend (active, result, otherwise));
}

int
ix86_knc_division_cost (const struct processor_costs *cost)
{
  /* Fast path count; constants and transfers have their own RTL costs. */
  return 27 * cost->sse_op;
}

/* IMCI widens only lanes 0-7 (Intel 327364-001 pp.154, 184, 187).
   Keep all register temporaries full width; V8SF/V8SI occur only in selects.  */
void
ix86_knc_expand_unpack (enum rtx_code code, rtx *ops, bool high)
{
  machine_mode mode = GET_MODE (ops[1]);
  rtx src = force_reg (mode, ops[1]);
  if (high)
    {
      rtx tmp = gen_reg_rtx (mode);
      emit_insn (gen_knc_permf32x4 (mode, tmp, src, GEN_INT (0xee)));
      src = tmp;
    }
  rtvec lanes = rtvec_alloc (8);
  for (int i = 0; i < 8; ++i)
    RTVEC_ELT (lanes, i) = GEN_INT (i);
  rtx low = gen_rtx_VEC_SELECT (mode == V16SFmode ? V8SFmode : V8SImode,
                                src, gen_rtx_PARALLEL (VOIDmode, lanes));
  emit_insn (gen_rtx_SET (ops[0], gen_rtx_fmt_e (code, V8DFmode, low)));
}

/* Each narrowing form zeros lanes 8-15 (pp.162, 166, 181).  Both the
   conversions and the mask materialization remain visible to optimization.  */
void
ix86_knc_expand_pack (enum rtx_code code, rtx *ops)
{
  machine_mode mode = GET_MODE (ops[0]);
  rtx lo = gen_reg_rtx (mode), hi = gen_reg_rtx (mode);
  rtx (*convert) (rtx, rtx, rtx) = code == FLOAT_TRUNCATE
    ? gen_knc_cvtpd2ps_lo : code == FIX
    ? gen_knc_cvtpd2dq_lo : gen_knc_cvtpd2udq_lo;
  rtx zero = CONST0_RTX (mode == V16SFmode ? V8SFmode : V8SImode);
  emit_insn (convert (lo, force_reg (V8DFmode, ops[1]), zero));
  emit_insn (convert (hi, force_reg (V8DFmode, ops[2]), zero));
  rtx mask = gen_reg_rtx (HImode);
  emit_insn (gen_knc_mask (mask, force_reg (HImode, GEN_INT (-256))));
  emit_insn (gen_knc_cvt_merge (mode, ops[0], hi, GEN_INT (0x44), lo, mask));
}

#include "gt-i386-knc.h"
