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
#include "calls.h"
#include "varasm.h"
#include "target.h"
#include "regs.h"
#include "diagnostic-core.h"
#include "optabs.h"
#include "cpplib.h"
#include "opts.h"
#include "targhooks.h"
#include "fold-const.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "dumpfile.h"
#include "case-cfn-macros.h"
#include "cfgloop.h"
#include "rtl-iter.h"
#include "predict.h"
#include "tree-vectorizer.h"
#include "i386-builtins.h"
#include "i386-options.h"
#include "builtins.h"
#include "stringpool.h"
#include "tree-pass.h"
#include "input.h"
#include "ira.h"
#include "lra-int.h"

/* Before inlining, recognize both the C entry and the standard C++ overload.
   Replacing the latter here keeps -O0 safe and diagnoses each user's call,
   rather than the shared implementation in a system header.  */
static bool
knc_cbrtl_decl_p (tree decl)
{
  if (!decl || !DECL_NAME (decl))
    return false;
  tree type = TREE_TYPE (decl);
  tree args = TYPE_ARG_TYPES (type);
  if (TREE_TYPE (type) != long_double_type_node || !args
      || TREE_VALUE (args) != long_double_type_node
      || !TREE_CHAIN (args) || TREE_VALUE (TREE_CHAIN (args)) != void_type_node)
    return false;
  if (fndecl_built_in_p (decl, BUILT_IN_CBRTL))
    return true;
  const char *name = IDENTIFIER_POINTER (DECL_NAME (decl));
  if (!strcmp (name, "cbrtl") && DECL_EXTERNAL (decl)
      && !strcmp (IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl)), "cbrtl"))
    return true;
  tree context = DECL_CONTEXT (decl);
  return !strcmp (name, "cbrt") && DECL_IN_SYSTEM_HEADER (decl)
    && context && TREE_CODE (context) == NAMESPACE_DECL
    && DECL_NAME (context)
    && !strcmp (IDENTIFIER_POINTER (DECL_NAME (context)), "std");
}

namespace {
const pass_data pass_data_knc_cbrtl =
{
  GIMPLE_PASS, "knccbrtl", OPTGROUP_NONE, TV_NONE, PROP_cfg, 0, 0, 0, 0
};

class pass_knc_cbrtl : public gimple_opt_pass
{
public:
  pass_knc_cbrtl (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_knc_cbrtl, ctxt) {}
  bool gate (function *) final override { return TARGET_KNC; }
  unsigned int execute (function *fun) final override
  {
    tree replacement = NULL_TREE;
    basic_block bb;
    FOR_EACH_BB_FN (bb, fun)
      for (gimple_stmt_iterator it = gsi_start_bb (bb); !gsi_end_p (it);
	   gsi_next (&it))
	{
	  gimple *stmt = gsi_stmt (it);
	  if (!is_gimple_call (stmt)
	      || !knc_cbrtl_decl_p (gimple_call_fndecl (stmt)))
	    continue;
	  location_t loc = linemap_resolve_location
	    (line_table, gimple_location (stmt), LRK_MACRO_EXPANSION_POINT, NULL);
	  warning_at (loc, OPT_Wknc_unsafe_cbrtl,
		      "MPSS %<cbrtl%> is unsafe on KNC; using replacement %<__knc_cbrtl%>");
	  if (!replacement)
	    replacement = build_fn_decl
	      ("__knc_cbrtl", build_function_type_list
	       (long_double_type_node, long_double_type_node, NULL_TREE));
	  gimple_call_set_fndecl (as_a<gcall *> (stmt), replacement);
	}
    return 0;
  }
};
}

gimple_opt_pass *
make_pass_knc_cbrtl (gcc::context *ctxt)
{
  return new pass_knc_cbrtl (ctxt);
}

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
    {COSTS_N_INSNS (10), COSTS_N_INSNS (10), COSTS_N_INSNS (10),
     COSTS_N_INSNS (10), COSTS_N_INSNS (20)}, /* S12-21: R02 p.56 models a
        ten-cycle scalar multiply; measured SI/DI register and memory forms
        take about nine TSC ticks/op, dependent or independent.  Retain the
        estimated two-multiply cost for TI.  See docs/research/s12-21-phase1.md.  */
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
    32, 256, 64,            /* ADR-0009 point 6: L1 KB, L2 KB and line
                               bytes; both caches 8-way (no GCC cost field).
                               S8-14 (amendment 1 point 3): the L2 the loop
                               passes block and prefetch for is the smaller
                               of the 512 KB cache and the DTLB reach, 64
                               entries of 4 KiB = 256 KB (S8-12 C4: a load
                               beyond it costs about 29 more ticks).  */
    1024,                   /* S8-03: software prefetch slots the loop pass
                               may keep in flight, not a hardware queue; C5
                               ran eight streams at 16 + 32 lines (384)
                               with profit.  */
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
    COSTS_N_INSNS (38), COSTS_N_INSNS (38), /* E: vector div fallbacks;
			      ix86_knc_division_cost specializes SF and DF.
			      Instruction costs, not expansion latencies.  */
    COSTS_N_INSNS (38), COSTS_N_INSNS (38), /* E: vector sqrt fallbacks;
			      SF/DF specialized costs in the vectorizer and
			      RTL hooks below.  R27 pp.55-56 describes FP
			      semantics, not a numerical proof of lowering.  */
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
    "16", "16:11:8", "0:0:8", "64", /* M: functions on the 64-byte line
                                       (S8-08 A/A pairs); loops, jumps and
                                       labels generic.  */
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

/* S8-08 (E5-12, ADR-0009 amendment 1 point 6): -mtune=knc starts from the
   generic flags and changes only those with a documented or measured
   reason; docs/research/knc-costs.md lists every flag.  The 0x66 and 0x67
   prefixes cost 2 decode cycles (R01, SSDG 328207-001EN p.19;
   R02 p.56), so promote HImode register
   operations to SImode.  S8-19: KNC has no decoded icache, so drop
   pass_align_tight_loops; its unbounded .p2align before a loop entered by
   fallthrough runs nops on every outer trip (m2_mat8 1.105x slower, lead
   card 2026-11-10).  Runs before -mtune-ctrl, which still wins.  */
void
ix86_knc_tune_features (const gcc_options *opts)
{
  if (!opts->x_ix86_tune_string
      || strcmp (opts->x_ix86_tune_string, "knc"))
    return;
  ix86_tune_features[X86_TUNE_FAST_PREFIX] = 0;
  ix86_tune_features[X86_TUNE_ALIGN_TIGHT_LOOPS] = 0;
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
  KNC_BUILTIN_SCATTER_DI,
  KNC_BUILTIN_STORENT_FENCE
};

/* Keep the internal decls alive without registering front-end builtins.
   Their distinct function types identify the gather-only ABI exemption.  */
static GTY(()) tree knc_indexed_decls[4];

/* S8-13 (E5-08): the fence the loop prefetch pass calls after a loop of
   no-read stores (FENCE_FOLLOWING_MOVNT; i386-builtins.cc points x86_mfence
   here on KNC, which has no mfence).  Not pure and not const, so no store
   crosses it in GIMPLE; the insn is a scheduling barrier.  */
static GTY(()) tree knc_storent_fence;

tree
ix86_knc_storent_fence_decl (void)
{
  if (!knc_storent_fence)
    {
      tree type = build_function_type_list (void_type_node, NULL_TREE);
      tree decl = build_fn_decl ("__builtin_knc_storent_fence", type);
      set_decl_built_in_function (decl, BUILT_IN_MD,
				  KNC_BUILTIN_STORENT_FENCE);
      TREE_NOTHROW (decl) = 1;
      knc_storent_fence = decl;
    }
  return knc_storent_fence;
}

/* The storent<mode> expander on KNC: a vmovnrngoap* of a whole line, or
   false (the expander FAILs and the store stays a normal one) when the
   address is not known to be 64-byte aligned.  OPERANDS are the memory and
   the register.  */
bool
ix86_knc_expand_storent (rtx *operands)
{
  rtx mem = operands[0];
  machine_mode mode = GET_MODE (mem);
  if (!ix86_knc_packed_mode_p (mode)
      || !MEM_P (mem)
      || MEM_ALIGN (mem) < GET_MODE_ALIGNMENT (mode))
    return false;
  emit_insn (gen_knc_storent (mode, mem, force_reg (mode, operands[1])));
  return true;
}

tree
ix86_knc_builtin_decl (unsigned code)
{
  if (code == KNC_BUILTIN_STORENT_FENCE)
    return ix86_knc_storent_fence_decl ();
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
  if (code == KNC_BUILTIN_STORENT_FENCE)
    {
      rtx mem = gen_rtx_MEM (BLKmode, gen_rtx_SCRATCH (Pmode));
      MEM_VOLATILE_P (mem) = 1;
      emit_insn (gen_knc_storent_fence (mem));
      return const0_rtx;
    }
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

/* For ix86_vector_costs::finish_cost, on a loop over partial vectors with
   a known trip count: whether the iterations it would mask (the trip
   count of a main loop, the remainder the main loop leaves an epilogue)
   are within C6's x87 limit, so that the masked loop must lose and the
   x87 loop stays.  With partial-vector usage 1 every epilogue is masked
   unless rejected here.  KNC has no narrower vector mode, so this also
   replaces the generic rejection in favour of a narrower mode.  */
bool
ix86_knc_short_masked_loop_p (loop_vec_info loop_vinfo)
{
  bool aligned;
  knc_masked_loop_rejection (loop_vinfo, &aligned);
  HOST_WIDE_INT niters = LOOP_VINFO_INT_NITERS (loop_vinfo);
  if (LOOP_VINFO_EPILOGUE_P (loop_vinfo))
    {
      niters = knc_known_remainder (LOOP_VINFO_MAIN_LOOP_INFO (loop_vinfo));
      if (niters < 0)
	return false;
    }
  return (unsigned HOST_WIDE_INT) niters <= knc_x87_tail_limit (aligned);
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
			       "KNC candidate body call %T "
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

/* Share the bridge alignment check with predicates and constraints without
   exposing MEM_ALIGN to gencondmd.  */
bool
ix86_knc_aligned_mem_p (rtx mem, machine_mode mode)
{
  return MEM_P (mem) && MEM_ALIGN (mem) >= GET_MODE_ALIGNMENT (mode);
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
  /* ADR-0014 point 1: a stack vector argument, as for V16HI above.  */
  if (push_operand (operands[0], VOIDmode))
    operands[0] = emit_move_resolve_push (mode, operands[0]);
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
  /* A register value stays a vec_duplicate until reload
     (*knc_duplicate_reg, S8-06), so combine can fold its load.  */
  if (!MEM_P (value))
    value = force_reg (inner, value);
  emit_insn (gen_rtx_SET (target, gen_rtx_VEC_DUPLICATE (mode, value)));
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
  /* Keep the positive near distance strictly below the far distance,
     including explicit overrides.  L2's option range starts at two.  */
  if (opts->x_knc_prefetch_l1_distance >= opts->x_knc_prefetch_l2_distance)
    opts->x_knc_prefetch_l1_distance = opts->x_knc_prefetch_l2_distance - 1;

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

/* S8-02, ADR-0009 amendment 1 point 9, after the generic x86 parameter
   defaults.  C6 (knc-characterization.md): a trip count below the vector
   length is a whole-loop mask, not an x87 loop, so partial vectors for
   the epilogue and for loops the vectorizer knows to be shorter than one
   vector (usage 1); the masked epilogue itself is still requested from
   finish_cost.  Alignment peeling was left at the generic default here
   (cost model #2 refuses a peel only for a known trip count below the
   vector length plus the peel); S8-22 below turns it off.  A user setting
   wins, as with the generic default.  */
void
ix86_knc_vectorizer_params (struct gcc_options *opts,
			    struct gcc_options *opts_set)
{
  SET_OPTION_IF_UNSET (opts, opts_set, param_vect_partial_vector_usage, 1);

  /* S8-14 (E5-13, ADR-0009 point 5): static data of a 64-byte line or
     more starts a line (prefetch_block), so vector loops over it load
     whole lines and take aligned masked tails.  -malign-data=abi or
     compat still wins.  Definitions only: references keep the ABI
     alignment.  */
  SET_OPTION_IF_UNSET (opts, opts_set, ix86_align_data_type,
		       ix86_align_data_type_cacheline);

  /* S8-03 (E5-07, ADR-0009 point 5): software prefetch carries streaming
     code on KNC, whose L2 hardware prefetcher follows at most 16 streams
     and stops at a 4 KiB page.  A load miss suspends the requesting
     context; other contexts can issue and requests can remain outstanding
     (R01, SSDG 328207-001EN p.19).
     -march=knc turns -fprefetch-loop-arrays on at -O2 and above
     (not -Os; a user setting wins).  The pass's instruction ratios drop to
     one: an eight-stream row is two instructions per reference, and C2
     measured no incremental time for vprefetch0 in its paired sequence
     and about two core cycles for vprefetch1.  This is not a zero resource
     cost; pairing depends on instruction class and encoding (R02 pp.34,56).
     The distances come from ix86_knc_prefetch_distances.  */
  if (opts->x_flag_prefetch_loop_arrays < 0
      && opts->x_optimize >= 2
      && !opts->x_optimize_size)
    opts->x_flag_prefetch_loop_arrays = 1;
  /* S12-30: follow the KNC software-prefetch optimization levels, while
     preserving an explicit -mno-knc-indirect-prefetch.  */
  SET_OPTION_IF_UNSET (opts, opts_set, ix86_knc_indirect_prefetch,
		       opts->x_optimize >= 2 && !opts->x_optimize_size);
  SET_OPTION_IF_UNSET (opts, opts_set, param_min_insn_to_prefetch_ratio, 1);
  SET_OPTION_IF_UNSET (opts, opts_set, param_prefetch_min_insn_to_mem_ratio,
		       1);

  /* S8-09 (E5-18): the generic -O2 cost model (very-cheap) keeps every
     loop that needs a runtime alias check or an epilogue scalar, which on
     KNC is x87 code.  -march=knc -O2 uses the cheap model, as -O2
     -ftree-vectorize already does: alias versioning and masked epilogues,
     but no alignment peeling or versioning (those are dynamic, -O3).  Over
     the S0-07 C corpus at -O2 this vectorizes 64 loops instead of 17; the
     39 more that dynamic takes are all 8- to 32-bit integer loops in zlib,
     bzip2 and SQLite, reached only through alignment versioning.  A runtime
     stride (Fortran assumed-shape arrays, a[i * s]) gets a stride-1
     version, as at -O3.  Predictive commoning stays at -O3: it adds x87
     stack traffic to scalar recurrences.  Not at -Os; a user setting wins.  */
  if (opts->x_optimize >= 2 && !opts->x_optimize_size)
    {
      if (!opts_set->x_flag_vect_cost_model
	  && opts->x_flag_vect_cost_model == VECT_COST_MODEL_VERY_CHEAP)
	opts->x_flag_vect_cost_model = VECT_COST_MODEL_CHEAP;
      SET_OPTION_IF_UNSET (opts, opts_set, flag_version_loops_for_strides, 1);
    }

  /* S8-22: two -O3 transforms that -O2 lacks put x87 code on KNC hot paths
     (-mphinix, M2 on the card: mat16 12.9x and short 1.27x slower than
     -O2).  Complete unrolling before the vectorizer (cunrolli) may grow
     code at -O3; it flattened a 16-step inner loop, which lost the -O2
     outer-loop vector form, and the 16 invariants of the flattened row
     loop go x87, stack, broadcast.  The late cunroll still unrolls after
     vectorization.  Alignment peeling under the dynamic model compares
     only in-loop costs, while its prologue is up to VF - 1 x87 iterations
     (15 for float); the threshold then sends a short call to the x87
     loop.  Misaligned accesses keep the unpack and pack pairs, and the
     known-aligned ones (S8-14) stay aligned.  A user setting wins.  */
  SET_OPTION_IF_UNSET (opts, opts_set, param_cunrolli_grow_size, 0);
  SET_OPTION_IF_UNSET (opts, opts_set, param_vect_max_peeling_for_alignment,
		       0);

  /* S8-08 (E5-12): -mtune=knc starts functions on a 64-byte line.
     In A/A pairs on the card (one assembly linked twice, the second copy
     shifted half a line), m2_mat8 ran 1.34x apart at the generic 16 and
     0.76x at 32, and every kernel stayed within 1.8% at 64.
     ix86_default_align reads the per-processor table, not the KNC one, so
     replace its choice here; an explicit -falign-functions= still wins, and
     -Os or -fno-align-functions stays off.  Loops keep the default: 64 has
     no max-skip, so an inner loop entered by fallthrough ran up to 63 bytes
     of nops per outer trip (mat8 1.106x, mat16 1.173x slower on the card).  */
  const processor_costs *knc = ix86_knc_tune_cost (opts, NULL);
  if (knc && opts->x_flag_align_functions
      && !opts_set->x_str_align_functions)
    opts->x_str_align_functions = knc->align_func;

  /* S10-03 (ADR-0014 point 2): loops call libknc_vmath unless the user
     chose another vector library.  */
  if (!opts_set->x_ix86_veclibabi_type)
    ix86_veclib_handler = &ix86_knc_veclibabi;
}

/* S10-03, ADR-0014 point 2: the -mveclibabi=knc handler, the default under
   -march=knc.  Return a declaration of libknc_vmath's by-value entry point
   __knc_vmath_<name>_<lanes> for FN on 16 floats or 8 doubles (the v0 set:
   exp, log and pow; S10-07's v1a set: sin, cos, tan, log10, atan2, sinh,
   cosh and tanh, and atan on floats only, as the library has no 8-lane
   atan; S10-08's v1b set: acos, asin, cbrt, erf, erfc, exp2, exp10, expm1,
   log1p, log2 and hypot), or NULL_TREE.  sincos needs no entry: it reaches
   the vectorizer as cexpi, whose parts become the sin and cos calls.  The
   entry point's 64-byte vector arguments and result travel in zmm (ADR-0014
   point 1).  The library rounds to nearest within about 1 ulp (erfc on
   doubles: 1.75) and keeps the IEEE special values, so unlike SVML it needs no
   fast-math flag; it never sets errno, and the vectorizer only asks once
   -fno-math-errno has made the scalar call const.  Not under
   -frounding-math: other rounding modes are not qualified.  */
tree
ix86_knc_veclibabi (combined_fn fn, tree type_out, tree type_in)
{
  if (!TARGET_KNC || flag_rounding_math)
    return NULL_TREE;

  machine_mode el_mode = TYPE_MODE (TREE_TYPE (type_out));
  int n = TYPE_VECTOR_SUBPARTS (type_out);
  int in_n = TYPE_VECTOR_SUBPARTS (type_in);
  if (el_mode != TYPE_MODE (TREE_TYPE (type_in))
      || n != in_n
      || !((el_mode == SFmode && n == 16) || (el_mode == DFmode && n == 8)))
    return NULL_TREE;

  bool binary = false;
  switch (fn)
    {
    CASE_CFN_POW:
    CASE_CFN_ATAN2:
    CASE_CFN_HYPOT:
      binary = true;
      break;

    CASE_CFN_ATAN:
      if (el_mode != SFmode)
	return NULL_TREE;
      break;

    CASE_CFN_EXP:
    CASE_CFN_LOG:
    CASE_CFN_SIN:
    CASE_CFN_COS:
    CASE_CFN_TAN:
    CASE_CFN_LOG10:
    CASE_CFN_SINH:
    CASE_CFN_COSH:
    CASE_CFN_TANH:
    CASE_CFN_ACOS:
    CASE_CFN_ASIN:
    CASE_CFN_CBRT:
    CASE_CFN_ERF:
    CASE_CFN_ERFC:
    CASE_CFN_EXP2:
    CASE_CFN_EXP10:
    CASE_CFN_EXPM1:
    CASE_CFN_LOG1P:
    CASE_CFN_LOG2:
      break;

    default:
      return NULL_TREE;
    }

  tree fndecl = mathfn_built_in (el_mode == DFmode
				 ? double_type_node : float_type_node, fn);
  if (!fndecl)
    return NULL_TREE;

  /* The name after "__builtin_": expf, log, ...  */
  char name[32];
  snprintf (name, sizeof name, "__knc_vmath_%s_%d",
	    IDENTIFIER_POINTER (DECL_NAME (fndecl)) + 10, n);

  tree fntype = (binary
		 ? build_function_type_list (type_out, type_in, type_in, NULL)
		 : build_function_type_list (type_out, type_in, NULL));
  tree new_fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
				get_identifier (name), fntype);
  TREE_PUBLIC (new_fndecl) = 1;
  DECL_EXTERNAL (new_fndecl) = 1;
  DECL_IS_NOVOPS (new_fndecl) = 1;
  TREE_READONLY (new_fndecl) = 1;

  return new_fndecl;
}

/* S8-10 (E5-16): a remote line crosses the ring, so data that other cores
   write must not share a 64-byte line with data this core writes.  */
#define KNC_LINE_BITS 512

/* LOCAL_ALIGNMENT for DECL.  The .omp_data_o block of an OpenMP region is
   read by every thread of the team, and reductions are combined into it,
   while the encountering thread's own frames (GOMP_parallel, the outlined
   body) grow just below it.  Starting it on a line keeps those writes off
   its line.  Not at -Os, where the frame realignment is not worth it.  */
unsigned int
ix86_knc_local_alignment (tree decl, unsigned int align)
{
  if (align < KNC_LINE_BITS
      && VAR_P (decl)
      && DECL_ARTIFICIAL (decl)
      && DECL_NAME (decl)
      && startswith (IDENTIFIER_POINTER (DECL_NAME (decl)), ".omp_data_o")
      && cfun
      && !optimize_function_for_size_p (cfun))
    return KNC_LINE_BITS;
  /* S8-14 (E5-13): a local array of a line or more starts a line, as
     -malign-data=cacheline does for static data.  */
  if (align < KNC_LINE_BITS
      && VAR_P (decl)
      && !DECL_USER_ALIGN (decl)
      && TREE_CODE (TREE_TYPE (decl)) == ARRAY_TYPE
      && DECL_SIZE (decl)
      && TREE_CODE (DECL_SIZE (decl)) == INTEGER_CST
      && !wi::ltu_p (wi::to_wide (DECL_SIZE (decl)), KNC_LINE_BITS)
      && ix86_align_data_type == ix86_align_data_type_cacheline
      && cfun
      && !optimize_function_for_size_p (cfun))
    return KNC_LINE_BITS;
  return align;
}

/* TARGET_ENCODE_SECTION_INFO entry.  varasm never raises a thread-local
   variable above a word, so a per-thread array of a line or more would
   straddle two lines.  Start each such array this unit defines (__thread
   or OpenMP threadprivate) on a line, as -malign-data=cacheline does for
   static data.  The raise reaches the definition before varasm emits it;
   only definitions that bind here are raised, so no reference elsewhere
   assumes it.  -malign-data=abi keeps the ABI alignment.  */
void
ix86_knc_align_thread_local (tree decl)
{
  if (!VAR_P (decl)
      || !DECL_THREAD_LOCAL_P (decl)
      || DECL_EXTERNAL (decl)
      || DECL_USER_ALIGN (decl)
      || DECL_ALIGN (decl) >= KNC_LINE_BITS
      || ix86_align_data_type == ix86_align_data_type_abi
      || !AGGREGATE_TYPE_P (TREE_TYPE (decl))
      || !DECL_SIZE (decl)
      || TREE_CODE (DECL_SIZE (decl)) != INTEGER_CST
      || wi::ltu_p (wi::to_wide (DECL_SIZE (decl)), KNC_LINE_BITS)
      || !decl_binds_to_current_def_p (decl))
    return;
  SET_DECL_ALIGN (decl, KNC_LINE_BITS);
}

/* Provisional S12-30 policy cutoffs, one per reduction form: rows of one
   index array, rows with their own index pointer, lone loop.  Replace
   with the compiler-gate's measured cutoffs before the default-on
   application A/B; these are not hardware constants.
   --param=knc-indirect-prefetch-min-trips overrides all three.  */
static const unsigned knc_indirect_prefetch_default_min_trips[]
  = { 16384, 16384, 16384 };

/* TARGET_INDIRECT_PREFETCH_POLICY.  */
bool
ix86_knc_indirect_prefetch_policy (unsigned form, unsigned *distance,
				   unsigned *rows_ahead, unsigned *min_rows)
{
  if (!TARGET_KNC || !ix86_knc_indirect_prefetch || optimize < 2
      || optimize_size
      || form >= ARRAY_SIZE (knc_indirect_prefetch_default_min_trips))
    return false;

  /* S12-30 legal-gate lookahead, not architectural latency constants.  */
  *distance = 32;
  *rows_ahead = 3;
  *min_rows = (knc_indirect_prefetch_min_trips
	       ? knc_indirect_prefetch_min_trips
	       : knc_indirect_prefetch_default_min_trips[form]);
  return true;
}

/* TARGET_PREFETCH_DISTANCES.  Distances in cache lines for the two prefetch
   levels (knc-characterization.md C2, C5 and the S8-12 rerun, 2026-11-08):
   vprefetch1 (L2) at knc-prefetch-l2-distance, 32 lines at the best point of
   a stream, a stencil and an eight-stream row at 2 and 4 threads per core;
   vprefetch0 (L1) at knc-prefetch-l1-distance, 16 lines, halved when the
   L1 footprint (distance x prefetched streams x threads per core) exceeds a
   quarter of L1 (128 lines), which gives the row its measured best 8 at 2
   and 4 threads and keeps 16 at one thread.  L1 prefetch alone measured
   1.7-2.3x slower than none, so the pass never gets a near distance without
   a far one, and the near distance stays below the far one however the
   params are set.  NSTREAMS is zero when the pass only asks whether to
   defer its trip count check to the far distance.  */
bool
ix86_knc_prefetch_distances (unsigned nstreams, unsigned *near, unsigned *far)
{
  if (!TARGET_KNC)
    return false;

  unsigned l1 = knc_prefetch_l1_distance, l2 = knc_prefetch_l2_distance;
  unsigned l1_lines = (param_l1_cache_line_size
		       ? param_l1_cache_size * 1024 / param_l1_cache_line_size
		       : 512);
  if (nstreams && l1 > 1 && l1 * nstreams * knc_threads_per_core > l1_lines / 4)
    l1 /= 2;
  if (l1 >= l2)
    l1 = MAX (1, l2 / 2);
  *near = l1;
  *far = l2;
  return true;
}

/* S8-15 (E5-09, ADR-0009 point 6): the -O2 small-loop unroll
   (-munroll-only-small-loops) unrolls a vector loop of at most
   KNC_SMALL_VECTOR_LOOP_INSNS insns up to 4 times.  S8-01 C9: a dependent
   vector add costs about 2 ticks per vector op plus 2 per loop branch, so
   unroll 4 beat 2 and 1 at 1 and 2 threads per core (25% then 17% over
   none).  16 admits two loads, an op, a store and their prefetches
   (S8-03) and keeps loops of more vector work at one copy.  Scalar loops
   keep the generic rule.  Return false to leave NUNROLL to the caller.

   S8-20: a stream, at most one vector op per vector store, follows
   knc-threads-per-core.  The S8-15 card run (2026-11-10) found M2
   affine_f64 3-7% and short 2% slower unrolled 4 times.  affine_f64's
   loop is a load, an FMA, a store and four prefetches: unroll 4 saves 5
   of 44 insns per 4 lines, at 4 threads per core the other threads fill
   the loop branch's issue slots and the L2 stream bounds the loop, and
   the 3-copy remainder peel enters the loop by fall-through, so the loop
   head loses its alignment.  short runs one vector iteration per call:
   unroll 4's remainder dispatch (shift, add, and, two compares and a
   taken branch) is pure cost, while unroll 2's single test costs nothing
   over no unroll.  So a stream unrolls 2 times at 1-2 threads per core
   and not at 3-4; two ops per store (stencil1) and no store (mat16)
   gained at both densities and keep 4.  */
#define KNC_SMALL_VECTOR_LOOP_INSNS 16

/* True if the vector value SRC is computed rather than moved: an
   arithmetic, conversion or FMA code, under a merge mask.  */
static bool
ix86_knc_vector_op_p (rtx src)
{
  if (GET_CODE (src) == VEC_MERGE)
    src = XEXP (src, 0);
  switch (GET_RTX_CLASS (GET_CODE (src)))
    {
    case RTX_BIN_ARITH:
    case RTX_COMM_ARITH:
      return true;
    case RTX_UNARY:
      return GET_CODE (src) != VEC_DUPLICATE;
    default:
      return GET_CODE (src) == FMA;
    }
}

bool
ix86_knc_small_loop_unroll (unsigned *nunroll, class loop *loop)
{
  if (!TARGET_KNC || loop->ninsns > KNC_SMALL_VECTOR_LOOP_INSNS)
    return false;
  bool vector = false;
  unsigned ops = 0, stores = 0;
  basic_block *bbs = get_loop_body (loop);
  subrtx_iterator::array_type array;
  for (unsigned i = 0; i < loop->num_nodes; i++)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bbs[i], insn)
	if (NONDEBUG_INSN_P (insn))
	  {
	    rtx set = single_set (insn);
	    if (set && ix86_knc_packed_mode_p (GET_MODE (SET_DEST (set))))
	      {
		if (MEM_P (SET_DEST (set)))
		  stores++;
		else if (ix86_knc_vector_op_p (SET_SRC (set)))
		  ops++;
	      }
	    if (!vector)
	      FOR_EACH_SUBRTX (iter, array, PATTERN (insn), NONCONST)
		if (ix86_knc_packed_mode_p (GET_MODE (*iter)))
		  {
		    vector = true;
		    break;
		  }
	  }
    }
  free (bbs);
  if (!vector)
    return false;
  if (stores && ops <= stores)
    *nunroll = MIN (*nunroll, knc_threads_per_core >= 3 ? 1 : 2);
  else
    *nunroll = MIN (*nunroll, 4);
  return true;
}

/* ADR-0014 point 1: a by-value 64-byte vector of a packed mode travels
   whole, in zmm0-zmm7 or a 64-byte aligned stack slot, by the shared
   classifier's 512-bit vector rule.  Not as a field of an aggregate.  */
static bool
knc_vector_abi_type_p (const_tree type, machine_mode mode)
{
  return type && VECTOR_TYPE_P (type) && ix86_knc_packed_mode_p (mode);
}

/* KNC scalar transport admits XImode, which has no register ABI.  Reject
   the vector types without one before recursive ABI classification.  */
bool
ix86_knc_check_abi_type (const_tree type, machine_mode mode)
{
  if (knc_vector_abi_type_p (type, mode)
      || !ix86_knc_unsupported_abi_type_p (type, mode))
    return true;
  error ("unsupported KNC register ABI mode");
  return false;
}

/* TARGET_PASS_BY_REFERENCE entry.  A vector argument has its register ABI
   only in a SysV function that is not variadic: ADR-0014 point 1 keeps
   every vector argument of a variadic function an error, named or not.
   CUM is null for queries outside a call, which va_arg checks itself.  */
bool
ix86_knc_check_abi_arg (const CUMULATIVE_ARGS *cum,
			const function_arg_info &arg)
{
  if (knc_vector_abi_type_p (arg.type, arg.mode)
      && cum
      && (cum->stdarg || !arg.named || cum->call_abi != SYSV_ABI))
    {
      error ("unsupported KNC register ABI mode");
      return false;
    }
  return ix86_knc_check_abi_type (arg.type, arg.mode);
}

/* TARGET_RETURN_IN_MEMORY entry: a vector result is in zmm0 for a SysV
   function only.  */
bool
ix86_knc_check_abi_result (const_tree type, const_tree fntype)
{
  machine_mode mode = TYPE_MODE (type);
  if (knc_vector_abi_type_p (type, mode)
      && ix86_function_type_abi (fntype) != SYSV_ABI)
    {
      error ("unsupported KNC register ABI mode");
      return false;
    }
  return ix86_knc_check_abi_type (type, mode);
}

/* va_arg of a vector type keeps its error (ADR-0014 point 1).  */
bool
ix86_knc_check_va_arg_type (const_tree type)
{
  machine_mode mode = TYPE_MODE (type);
  if (knc_vector_abi_type_p (type, mode))
    {
      error ("unsupported KNC register ABI mode");
      return false;
    }
  return ix86_knc_check_abi_type (type, mode);
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
   hard register is constructed.  The classifier's private enum stays shared.
   A packed mode is a whole ZMM register (ADR-0014 point 1).  */
bool
ix86_knc_check_abi_mode (machine_mode mode, bool half_class)
{
  if (ix86_knc_packed_mode_p (mode)
      || (!VECTOR_MODE_P (mode)
	  && !DECIMAL_FLOAT_MODE_P (GET_MODE_INNER (mode))
	  && GET_MODE_INNER (mode) != HFmode
	  && GET_MODE_INNER (mode) != BFmode && !half_class))
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

/* LRA's get_equiv can replace a bridge pseudo without rechecking its
   predicate.  Preserve the copy when its equivalent memory does not meet
   the transport mode's alignment.  Other targets retain the default hook
   result (ADR-0006).  */
bool
ix86_knc_cannot_substitute_mem_equiv_p (rtx subst)
{
  return TARGET_KNC && MEM_P (subst)
         && knc_scalar_transport_mode_p (GET_MODE (subst))
         && MEM_ALIGN (subst) < GET_MODE_ALIGNMENT (GET_MODE (subst));
}

/* A bridge created during LRA must start in memory.  In particular, a TI/TF
   pseudo with an unrestricted class can acquire a vector register from the
   ordinary move before the memory-only transfer is processed, recursively
   requiring another secondary reload.  Let LRA own the spill slot so that
   lra-spills.cc:remove_pseudos also applies the frame elimination offset.  */
rtx
ix86_knc_reload_bridge (machine_mode mode)
{
  gcc_assert (lra_in_progress && knc_scalar_transport_mode_p (mode));
  return lra_create_new_reg_with_unique_value (mode, NULL_RTX, NO_REGS, NULL,
                                              "KNC bridge");
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
   -fno-trapping-math (knc_vec_cmp_operator), as nle/nlt; UNEQ never does.
   LTGT combines signaling lt masks in both operand orders.  Integer
   immediates follow ix86_cmp_code_to_pcmp_immediate.  */
bool
ix86_knc_expand_mask_vec_cmp (rtx dest, enum rtx_code code, rtx op0, rtx op1)
{
  machine_mode mode = GET_MODE (op0);
  bool fp = FLOAT_MODE_P (mode);
  if (code == LTGT)
    {
      gcc_assert (fp);
      /* A late GIMPLE fold can introduce LTGT after vectorization has
	 checked the original comparisons.  Rejecting it in the predicate
	 then leaves do_store_flag without a vector expansion.
	 LTGT signals on NaNs, like LT (rtlanal.cc:may_trap_p_1).  Combine
	 LT in both operand orders: NaNs and equal signed zeros yield false,
	 and both QNaNs and SNaNs raise invalid.  Keep both compares and the
	 mask OR visible to the optimizer.  */
      op0 = force_reg (mode, op0);
      op1 = force_reg (mode, op1);
      machine_mode mask_mode = GET_MODE (dest);
      rtx lt = gen_reg_rtx (mask_mode);
      rtx gt = gen_reg_rtx (mask_mode);
      ix86_knc_expand_mask_vec_cmp (lt, LT, op0, op1);
      ix86_knc_expand_mask_vec_cmp (gt, LT, op1, op0);
      if (mask_mode == HImode)
	emit_insn (gen_iorhi3 (dest, lt, gt));
      else
	{
	  gcc_assert (mask_mode == QImode);
	  emit_insn (gen_iorqi3 (dest, lt, gt));
	}
      return true;
    }
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

static void knc_expand_div_df (rtx *, bool);
static void knc_expand_sqrt_df (rtx *, bool);

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
ix86_knc_expand_sqrt (rtx *operands, bool conditional)
{
  if (GET_MODE (operands[0]) == V8DFmode)
    {
      knc_expand_sqrt_df (operands, conditional);
      return;
    }
  gcc_assert (!conditional);
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
   19 instructions for sixteen elements (research section 2.5).  S10-09:
   the V8DF counts are the vector instructions of the fast paths as
   emitted at -O2 for eight doubles, register copies included (31 and 29
   without them; section 3 estimated 31 and 34).  */
#define KNC_SQRT_DF_INSNS 35
#define KNC_DIV_DF_INSNS 34
bool
ix86_knc_sqrt_rtx_cost (rtx x, machine_mode mode, int *total)
{
  /* S8-06: an FMA may only fold an aligned full-line operand (imci.md
     *knc_fma<mode>4).  Its predicates also take a misaligned memory
     operand, which LRA must reload with the unpack pair (CH unpack.rt,
     two ops), so a fold of one costs that pair and combine keeps the
     load separate, leaving the memory slot for a {1to16} broadcast.  */
  if (GET_CODE (x) == FMA && (mode == V16SFmode || mode == V8DFmode))
    {
      int pairs = 0;
      for (int i = 0; i < 3; ++i)
	{
	  rtx op = XEXP (x, i);
	  if (GET_CODE (op) == NEG)
	    op = XEXP (op, 0);
	  pairs += MEM_P (op) && misaligned_operand (op, mode);
	}
      if (!pairs)
	return false;
      *total = (mode == V16SFmode ? ix86_cost->fmass : ix86_cost->fmasd)
	       + pairs * COSTS_N_INSNS (2);
      return true;
    }
  if (GET_CODE (x) != SQRT || (mode != V16SFmode && mode != V8DFmode))
    return false;
  *total = COSTS_N_INSNS (mode == V8DFmode ? KNC_SQRT_DF_INSNS : 19);
  return true;
}

bool
ix86_knc_sqrt_stmt_p (int kind, stmt_vec_info stmt, tree vectype)
{
  if (!stmt)
    return false;
  if (kind == scalar_stmt && !vectype)
    {
      /* S10-09: the scalar baseline carries no vector type.  A double
	 root there is an x87 fsqrt; costed as a plain statement it hid
	 38 cycles per element and kept such loops scalar.  */
      tree lhs = gimple_get_lhs (stmt->stmt);
      if (!lhs || TYPE_MODE (TREE_TYPE (lhs)) != DFmode)
	return false;
    }
  else if ((kind != vector_stmt && kind != scalar_stmt) || !vectype
	   || (TYPE_MODE (vectype) != V16SFmode
	       && TYPE_MODE (vectype) != V8DFmode))
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
ix86_knc_sqrt_stmt_cost (int kind, tree vectype)
{
  if (kind == scalar_stmt)
    return ix86_cost->fsqrt;
  return ((TYPE_MODE (vectype) == V8DFmode ? KNC_SQRT_DF_INSNS : 19)
	  * ix86_cost->sse_op);
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
  if (GET_MODE (operands[0]) == V8DFmode)
    {
      knc_expand_div_df (operands, conditional);
      return;
    }
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

/* S10-09: V8DF division and square root, docs/research/knc-div-sqrt.md
   section 3; host model tests/regress/knc-v8df-model.c (each instruction
   one MPFR operation, every estimate within one ulp).  The estimate is the
   FP32 one on the significand narrowed in RN: |m*y0 - 1| < 2^-22.5.  k1om
   has no vscalepd and no 64-bit integer add, so a normal result takes its
   exponent by a dword add to the high halves.

   Division.  With e0 = 1 - mb*y0 and p = e0 + e0^2 + e0^3, y1 = y0 + y0*p
   has relative error below e0^4 + 2^-74 < 2^-73, so it is faithful and
   e1 = 1 - mb*y1 is exact (Lemma A on 1/mb).  y2 = y1 + e1*y1 is then
   RN(1/mb) by Markstein's reciprocal theorem, except for the all-ones
   significand, where RN(1/mb) = 1/2 + 2^-53 is blended in; so
   |mb*y2 - 1| <= B*2^-106 < 1/(B+1).  From q0 on, Lemmas A, M and B (above)
   hold at any precision: q1 is faithful, r1 exact, and fma(r1,y2,q1) is
   the correctly rounded significand quotient in the dynamic mode, inexact
   iff a/b is.  The model checks each claim on 200,064 divisors and the
   whole sequence on every operand pair at 8 to 11 bits, on the 128-bit
   modular near-midpoint pairs, the Markstein class, exponents and
   specials, in four modes; without the blend the all-ones divisor fails.

   The quotient exponent k is in [-2097,2097].  For -1021 <= k <= 1023 the
   result is normal and the exponent add is exact.  Any other active lane
   takes the rare path.  Special lanes: a regular operand becomes signed
   one, the divisor its reciprocal class (0 <-> inf by one XOR, NaN kept),
   and one vmulpd gives the IEEE result and flags; a masked vrcp23ps of
   zero adds #Z for finite/0.  Range lanes: qm * 2^h is exact and
   (qm * 2^h) * 2^(k-h) rounds once in the dynamic mode, with vmulpd's
   overflow and underflow.  As for FP32 (section 2.4), a tiny exact
   scaling of an inexact quotient raises underflow by a masked product,
   and under RN a subnormal tie is resolved by the sign of the exact
   residual ma - mb*qm.  The tie is found without integer shifts:
   RZ (2*res - 2*t1*s2) is nonzero only when the scaled quotient was
   exactly half a subnormal step from res.

   Square root.  s = y0*y0 is exact (48 bits), eps = 1 - m*s,
   p = eps*(1/2 + eps*(3/8 + eps*5/16)) truncates (1-eps)^-1/2 - 1 below
   2^-89.  g0 + eg = m*y0 exactly, so g1 = RN(g0 + g0*p + eg) is within
   u/2 + sqrt(m)*2^-73 of r = sqrt(m): faithful.  (The code keeps -eg and
   the negated sum, which RN mirrors exactly.)  h1 = hy + hy*p is 1/(2r)
   to 2^-53 relative.  Section 3.2's open obligation, the final step
   g2 = g1 + d*h1 with d = RN(m - g1^2) inexact: write t = r - g1,
   |t| <= u/2 + r*2^-73.  The exact m - g1^2 is t*(2r - t), so the exact
   step gives r - t^2/(2r), and the roundings of d and h1 add at most
   |t|*2^-52.  In units of the least distance of r from a midpoint
   (2^-107/r for m in [1,2), 2^-109/r for m in [1/2,1)) that is below
   1 + 8 = 9 units, for any estimate.  r is n units from a midpoint
   (4n from a double) exactly when m*2^BITS = G^2 + n has an integer root
   G, n = 7 mod 8, so the model enumerates by Hensel lifting every reduced
   argument within 2^16 units and runs it with every estimate in four
   modes.  One fails, under RN: m = 1 - 2^-53, one unit below the midpoint
   1 - 2^-54.  There RN(r) = m, and with g1 = m blended in, d = m*2^-53 is
   exact and the step is correct in every mode.  Every other argument is
   more than 2^16 units from a boundary, so g2 is the correctly rounded
   root with the right inexact flag, and the sequence of 3.2 is kept.

   sqrt(x) of a positive double is normal: the exponent add is exact and
   there is no rare path.  Negative lanes raise invalid by a masked
   inf - inf; the masked fixup gives signed zero, +inf and quiet NaNs.
   Every unmasked instruction suppresses flags; masked ones see only the
   lanes they must.  The mask compares are {sae}, so an inactive or
   special signalling NaN raises nothing there.  */

/* A V8DI register with BITS in every lane, as mode MODE.  */
static rtx
knc_df_bits (machine_mode mode, unsigned HOST_WIDE_INT bits)
{
  rtx c = gen_const_vec_duplicate (V8DImode, gen_int_mode (bits, DImode));
  return gen_lowpart (mode, force_reg (V8DImode, c));
}

static rtx
knc_df_const (unsigned HOST_WIDE_INT bits)
{
  return knc_df_bits (V8DFmode, bits);
}

/* The integer N in every lane.  */
static rtx
knc_df_int (HOST_WIDE_INT n)
{
  REAL_VALUE_TYPE r;
  real_from_integer (&r, DFmode, n, SIGNED);
  rtx c = const_double_from_real_value (r, DFmode);
  return force_reg (V8DFmode, gen_const_vec_duplicate (V8DFmode, c));
}

/* The lanes of MASK, or of all eight without one, where A PRED B, PRED
   being IMCI's 3-bit predicate (0 eq, 1 lt, 2 le, 3 unord, 4 neq); no
   flag is raised.  */
static rtx
knc_df_cmp (int pred, rtx a, rtx b, rtx mask)
{
  rtx k = gen_reg_rtx (QImode);
  emit_insn (mask ? gen_knc_div_cmp_df (k, a, b, GEN_INT (pred), mask)
		  : gen_knc_div_cmp_all_df (k, a, b, GEN_INT (pred)));
  return k;
}

/* The FP32 estimate of 1/M, or of 1/sqrt (M), widened exactly.  Lanes
   8-15 of the narrowed M are zero; {sae} keeps their infinity quiet.  */
static rtx
knc_df_estimate (rtx m, bool rsqrt)
{
  rtx ms = gen_reg_rtx (V16SFmode), ys = gen_reg_rtx (V16SFmode);
  rtx ops[2] = { gen_reg_rtx (V8DFmode), ys };
  emit_insn (gen_knc_div_narrow_df (ms, m));
  emit_insn (rsqrt ? gen_knc_sqrt_rsqrt (ys, ms) : gen_knc_div_rcp (ys, ms));
  ix86_knc_expand_unpack (FLOAT_EXTEND, ops, false);
  return ops[0];
}

/* The int32 lanes 0-7 of KI moved to the high dword of each qword, over
   a zero low dword: lanes 8-15 of KI are zero.  */
static rtx
knc_df_high_dwords (rtx ki)
{
  unsigned char d[16];
  for (unsigned int i = 0; i < 16; ++i)
    d[i] = i & 1 ? i >> 1 : 8;
  rtx kq = gen_reg_rtx (V16SImode);
  emit_insn (gen_knc_permvar (V16SImode, kq, ki, knc_perm_index (d)));
  return kq;
}

/* KQ << 20: exponent fields from knc_df_high_dwords.  */
static rtx
knc_df_exponent (rtx kq)
{
  rtx ke = gen_reg_rtx (V16SImode);
  emit_insn (gen_knc_div_ashl_imm (ke, kq, GEN_INT (20)));
  return ke;
}

static void
knc_expand_div_df (rtx *operands, bool conditional)
{
  const machine_mode mode = V8DFmode;
  rtx a = force_reg (mode, operands[conditional ? 2 : 1]);
  rtx b = force_reg (mode, operands[conditional ? 3 : 2]);
  /* Null when every lane is active.  */
  rtx active = conditional ? operands[1] : NULL_RTX;
  rtx zero = force_reg (mode, CONST0_RTX (mode));
  rtx one = knc_df_const (HOST_WIDE_INT_UC (0x3ff0000000000000));
  rtx ma = gen_reg_rtx (mode), mb = gen_reg_rtx (mode);
  rtx ea = gen_reg_rtx (mode), eb = gen_reg_rtx (mode);
  rtx kf = gen_reg_rtx (mode);
  emit_insn (gen_knc_div_mant_df (ma, a, GEN_INT (0)));
  emit_insn (gen_knc_div_mant_df (mb, b, GEN_INT (4)));
  emit_insn (gen_knc_sqrt_exp_df (ea, a));
  emit_insn (gen_knc_sqrt_exp_df (eb, b));
  /* Exact; infinite or NaN when an operand is zero, infinite or NaN.  */
  emit_insn (gen_knc_div_sub_df (kf, ea, eb));
  rtx fast = knc_df_cmp (2, knc_df_int (-1021), kf, active);
  fast = knc_df_cmp (2, kf, knc_df_int (1023), fast);
  rtx ki = gen_reg_rtx (V16SImode);
  emit_insn (gen_knc_sqrt_cvt_df (ki, kf));
  rtx kq = knc_df_high_dwords (ki);
  rtx ke = knc_df_exponent (kq);
  rtx sign = knc_df_bits (V16SImode, HOST_WIDE_INT_UC (0x8000000000000000));
  rtx bi = gen_lowpart (V16SImode, b);
  rtx sb = knc_div_bin (AND, bi, sign);
  ma = gen_lowpart (mode, knc_div_bin (XOR, gen_lowpart (V16SImode, ma), sb));

  rtx y0 = knc_df_estimate (mb, false);
  rtx e0 = gen_reg_rtx (mode), tt = gen_reg_rtx (mode);
  rtx pp = gen_reg_rtx (mode), y1 = gen_reg_rtx (mode);
  rtx e1 = gen_reg_rtx (mode), y2 = gen_reg_rtx (mode);
  rtx q0 = gen_reg_rtx (mode), r0 = gen_reg_rtx (mode);
  rtx q1 = gen_reg_rtx (mode), r1 = gen_reg_rtx (mode);
  rtx qm = gen_reg_rtx (mode);
  emit_insn (gen_knc_sqrt_fnma_df (e0, mb, y0, one));
  emit_insn (gen_knc_sqrt_fma_df (tt, e0, e0, e0));
  emit_insn (gen_knc_sqrt_fma_df (pp, tt, e0, e0));
  emit_insn (gen_knc_sqrt_fma_df (y1, y0, pp, y0));
  emit_insn (gen_knc_sqrt_fnma_df (e1, mb, y1, one));
  emit_insn (gen_knc_sqrt_fma_df (y2, e1, y1, y1));
  rtx all1 = knc_df_cmp (0, mb,
			 knc_df_const (HOST_WIDE_INT_UC (0x3fffffffffffffff)),
			 active);
  y2 = knc_div_blend (all1,
		      knc_df_const (HOST_WIDE_INT_UC (0x3fe0000000000001)), y2);
  emit_insn (gen_knc_sqrt_mul_df (q0, ma, y2));
  emit_insn (gen_knc_sqrt_fnma_df (r0, mb, q0, ma));
  emit_insn (gen_knc_sqrt_fma_df (q1, r0, y2, q0));
  emit_insn (gen_knc_sqrt_fnma_df (r1, mb, q1, ma));
  /* The only flag of the fast path: inexact, in the lanes that finish
     here.  The other lanes keep q1.  */
  emit_insn (gen_knc_fmav8df_mask (qm, r1, y2, q1, fast));
  rtx result = gen_reg_rtx (mode);
  emit_insn (gen_addv16si3 (gen_lowpart (V16SImode, result),
			    gen_lowpart (V16SImode, qm), ke));
  rtx_code_label *done = gen_label_rtx ();
  /* FAST is a subset of ACTIVE.  A QI mask is tested in a GPR: bits 15:8
     of its register are undefined.  */
  emit_cmp_and_jump_insns (fast, conditional ? active : constm1_rtx, EQ,
			   NULL_RTX, QImode, 1, done,
			   profile_probability::very_likely ());

  rtx rest = knc_div_bin (XOR, fast, conditional ? active : constm1_rtx);
  rtx t = gen_reg_rtx (mode);
  emit_insn (gen_knc_div_sub_df (t, kf, kf));
  rtx special = knc_df_cmp (4, t, zero, rest);
  rtx range = knc_df_cmp (0, t, zero, rest);

  /* Special lanes.  ea - ea is zero for a finite nonzero a.  */
  rtx ta = gen_reg_rtx (mode), tb = gen_reg_rtx (mode);
  emit_insn (gen_knc_div_sub_df (ta, ea, ea));
  emit_insn (gen_knc_div_sub_df (tb, eb, eb));
  rtx afin = knc_df_cmp (0, ta, zero, special);
  rtx bfin = knc_df_cmp (0, tb, zero, special);
  rtx bnan = knc_df_cmp (3, b, b, special);
  rtx onei = gen_lowpart (V16SImode, one);
  rtx ai = gen_lowpart (V16SImode, a);
  rtx aone = knc_div_bin (IOR, knc_div_bin (AND, ai, sign), onei);
  rtx bone = knc_div_bin (IOR, sb, onei);
  rtx inf = knc_df_bits (V16SImode, HOST_WIDE_INT_UC (0x7ff0000000000000));
  rtx ap = knc_div_blend (afin, gen_lowpart (mode, aone), a);
  rtx rp = gen_lowpart (mode, knc_div_bin (XOR, bi, inf));
  rp = knc_div_blend (bfin, gen_lowpart (mode, bone), rp);
  rp = knc_div_blend (bnan, b, rp);
  emit_insn (gen_rtx_SET (result,
			  gen_rtx_VEC_MERGE (mode, gen_rtx_MULT (mode, ap, rp),
					     result, special)));
  /* #Z for a finite nonzero numerator over zero.  The estimate has
     sixteen lanes, so the mask is extended with zeros.  */
  rtx bz = convert_to_mode (HImode, knc_df_cmp (0, b, zero, afin), 1);
  rtx zero16 = force_reg (V16SFmode, CONST0_RTX (V16SFmode));
  emit_insn (gen_knc_div_rcp_flags (gen_reg_rtx (V16SFmode), zero16, zero16,
				    bz));

  /* Range lanes: the significand quotient, then two scalings by powers
     of two, of which only the second can round.  */
  emit_insn (gen_knc_fmav8df_mask (qm, r1, y2, qm, range));
  rtx kc = gen_reg_rtx (V16SImode), h = gen_reg_rtx (V16SImode);
  emit_insn (gen_knc_div_clamp_smax
	     (kc, kq, knc_df_bits (V16SImode,
				   HOST_WIDE_INT_UC (0xfffffbb400000000))));
  emit_insn (gen_knc_div_clamp_smin
	     (kc, kc, knc_df_bits (V16SImode, HOST_WIDE_INT_UC (1100) << 32)));
  emit_insn (gen_knc_sqrt_shift (h, kc));
  rtx bias = knc_df_bits (V16SImode, HOST_WIDE_INT_UC (1023) << 32);
  rtx s1 = knc_df_exponent (knc_div_bin (PLUS, h, bias));
  rtx s2 = knc_df_exponent (knc_div_bin (PLUS, knc_div_bin (MINUS, kc, h),
					 bias));
  s1 = gen_lowpart (mode, s1);
  s2 = gen_lowpart (mode, s2);
  rtx t1 = gen_reg_rtx (mode), rr = gen_reg_rtx (mode);
  emit_insn (gen_knc_sqrt_mul_df (t1, qm, s1));
  emit_insn (gen_rtx_SET (result,
			  gen_rtx_VEC_MERGE (mode, gen_rtx_MULT (mode, t1, s2),
					     result, range)));
  emit_insn (gen_knc_sqrt_fnma_df (rr, mb, qm, ma));
  rtx nonexact = knc_df_cmp (4, rr, zero, range);
  rtx absres
    = knc_div_bin (AND, gen_lowpart (V16SImode, result),
		   knc_df_bits (V16SImode,
				HOST_WIDE_INT_UC (0x7fffffffffffffff)));
  rtx dblmin = knc_df_const (HOST_WIDE_INT_UC (0x0010000000000000));
  rtx underflow = knc_df_cmp (1, gen_lowpart (mode, absres), dblmin, nonexact);
  emit_insn (gen_knc_div_underflow_df
	     (gen_reg_rtx (mode), dblmin,
	      knc_df_const (HOST_WIDE_INT_UC (0x3fefffffffffffff)), underflow));
  rtx csr_mem = assign_stack_temp (SImode, 4);
  emit_insn (gen_knc_div_mxcsr (csr_mem));
  rtx csr = force_reg (SImode, csr_mem);
  rtx rc = knc_div_bin (AND, csr, GEN_INT (0x6000));
  emit_cmp_and_jump_insns (rc, const0_rtx, NE, NULL_RTX, SImode, true, done);
  /* k < 0 keeps an overflowed lane out: its difference is not zero.  */
  rtx low = knc_df_cmp (1, kf, zero, range);
  rtx two = knc_df_const (HOST_WIDE_INT_UC (0x4000000000000000));
  rtx t2 = gen_reg_rtx (mode), res2 = gen_reg_rtx (mode);
  rtx w = gen_reg_rtx (mode);
  emit_insn (gen_knc_sqrt_mul_df (t2, t1, two));
  emit_insn (gen_knc_sqrt_mul_df (res2, result, two));
  emit_insn (gen_knc_div_fnma_rz_df (w, t2, s2, res2));
  rtx tie = knc_df_cmp (4, w, zero, low);
  rtx up = knc_df_cmp (1, zero, rr, tie);
  rtx down = knc_df_cmp (1, rr, zero, tie);
  emit_insn (gen_knc_div_mul_ru_df (result, t1, s2, result, up));
  emit_insn (gen_knc_div_mul_rd_df (result, t1, s2, result, down));
  emit_label (done);
  if (conditional)
    result = knc_div_blend (active, result, force_reg (mode, operands[4]));
  emit_move_insn (operands[0], result);
}

static void
knc_expand_sqrt_df (rtx *operands, bool conditional)
{
  const machine_mode mode = V8DFmode;
  rtx x = force_reg (mode, operands[conditional ? 2 : 1]);
  rtx active = conditional ? operands[1] : NULL_RTX;
  rtx zero = force_reg (mode, CONST0_RTX (mode));
  rtx inf = knc_df_const (HOST_WIDE_INT_UC (0x7ff0000000000000));
  rtx m = gen_reg_rtx (mode), e = gen_reg_rtx (mode);
  rtx ei = gen_reg_rtx (V16SImode), j = gen_reg_rtx (V16SImode);
  /* x = m * 2^(2j), 1/2 <= m < 2, j = ceil (e/2).  */
  emit_insn (gen_knc_div_mant_df (m, x, GEN_INT (5)));
  emit_insn (gen_knc_sqrt_exp_df (e, x));
  emit_insn (gen_knc_sqrt_cvt_df (ei, e));
  emit_insn (gen_addv16si3 (ei, ei,
			    force_reg (V16SImode, CONST1_RTX (V16SImode))));
  emit_insn (gen_knc_sqrt_shift (j, ei));
  rtx je = knc_df_exponent (knc_df_high_dwords (j));
  /* 0 < x < inf, and x < 0 (-inf included, -0 and NaN not).  */
  rtx kreg = knc_df_cmp (1, zero, x, active);
  kreg = knc_df_cmp (1, x, inf, kreg);
  rtx kneg = knc_df_cmp (1, x, zero, active);

  rtx one = knc_df_const (HOST_WIDE_INT_UC (0x3ff0000000000000));
  rtx half = knc_df_const (HOST_WIDE_INT_UC (0x3fe0000000000000));
  rtx c38 = knc_df_const (HOST_WIDE_INT_UC (0x3fd8000000000000));
  rtx c516 = knc_df_const (HOST_WIDE_INT_UC (0x3fd4000000000000));
  rtx y0 = knc_df_estimate (m, true);
  rtx ss = gen_reg_rtx (mode), eps = gen_reg_rtx (mode);
  rtx p1 = gen_reg_rtx (mode), p2 = gen_reg_rtx (mode);
  rtx pp = gen_reg_rtx (mode), g0 = gen_reg_rtx (mode);
  rtx eg = gen_reg_rtx (mode), tt = gen_reg_rtx (mode);
  rtx g1 = gen_reg_rtx (mode), hy = gen_reg_rtx (mode);
  rtx h1 = gen_reg_rtx (mode), d = gen_reg_rtx (mode);
  rtx g2 = gen_reg_rtx (mode);
  emit_insn (gen_knc_sqrt_mul_df (ss, y0, y0));
  emit_insn (gen_knc_sqrt_fnma_df (eps, m, ss, one));
  emit_insn (gen_knc_sqrt_fma_df (p1, eps, c516, c38));
  emit_insn (gen_knc_sqrt_fma_df (p2, eps, p1, half));
  emit_insn (gen_knc_sqrt_mul_df (pp, eps, p2));
  emit_insn (gen_knc_sqrt_mul_df (g0, m, y0));
  /* eg = g0 - m*y0 and tt = eg - g0*pp are the negated product error and
     correction, so g1 = g0 - tt.  */
  emit_insn (gen_knc_sqrt_fnma_df (eg, m, y0, g0));
  emit_insn (gen_knc_sqrt_fnma_df (tt, g0, pp, eg));
  emit_insn (gen_knc_div_sub_df (g1, g0, tt));
  rtx all1 = knc_df_cmp (0, m,
			 knc_df_const (HOST_WIDE_INT_UC (0x3fefffffffffffff)),
			 active);
  g1 = knc_div_blend (all1, m, g1);
  emit_insn (gen_knc_sqrt_mul_df (hy, y0, half));
  emit_insn (gen_knc_sqrt_fma_df (h1, hy, pp, hy));
  emit_insn (gen_knc_sqrt_fnma_df (d, g1, g1, m));
  emit_insn (gen_knc_fmav8df_mask (g2, d, h1, g1, kreg));
  rtx result = gen_reg_rtx (mode);
  emit_insn (gen_addv16si3 (gen_lowpart (V16SImode, result),
			    gen_lowpart (V16SImode, g2), je));
  emit_insn (gen_knc_sqrt_invalid_df (result, result, inf, kneg));
  rtx table = force_reg (V8DImode,
			 gen_const_vec_duplicate (V8DImode, GEN_INT (0x206ad)));
  emit_insn (conditional
	     ? gen_knc_sqrt_fixup_df (result, result, x, table, active)
	     : gen_knc_sqrt_fixup_all_df (result, result, x, table));
  if (conditional)
    result = knc_div_blend (active, result, force_reg (mode, operands[3]));
  emit_move_insn (operands[0], result);
}

int
ix86_knc_division_cost (const struct processor_costs *cost, machine_mode mode)
{
  /* Fast path count; constants and transfers have their own RTL costs. */
  return (mode == V8DFmode ? KNC_DIV_DF_INSNS : 27) * cost->sse_op;
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
