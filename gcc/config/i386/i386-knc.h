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

#ifndef GCC_I386_KNC_H
#define GCC_I386_KNC_H

struct HARD_REG_SET;
class _loop_vec_info;
class _stmt_vec_info;
struct _slp_tree;
class vec_perm_indices;

extern const struct processor_costs *ix86_knc_tune_cost
  (const struct gcc_options *, const struct processor_costs *);
extern int ix86_knc_stmt_cost (int, machine_mode, class _stmt_vec_info *);
extern bool ix86_knc_reduction_p (class _stmt_vec_info *);
extern bool ix86_knc_packed_mode_p (machine_mode);
extern bool ix86_knc_transport_mode_p (machine_mode);
extern opt_machine_mode ix86_knc_vectorize_related_mode (machine_mode,
							scalar_mode, poly_uint64);
extern void ix86_knc_expand_narrow_store (rtx, rtx);
extern bool ix86_knc_vector_move_mode_p (machine_mode);
extern machine_mode ix86_knc_preferred_simd_mode (scalar_mode);
extern bool ix86_knc_support_vector_misalignment (machine_mode, int, bool, bool);
extern tree ix86_knc_vectorize_builtin_gather (const_tree, const_tree, int);
extern tree ix86_knc_vectorize_builtin_scatter (const_tree, const_tree, int);
extern tree ix86_knc_builtin_decl (unsigned);
extern rtx ix86_knc_expand_builtin (tree, rtx);
extern bool ix86_knc_gather_type_p (const_tree);

extern bool ix86_knc_element_aligned_mem_p (rtx, machine_mode);
extern bool ix86_knc_zero_constant_p (rtx, machine_mode);
extern bool ix86_knc_unsupported_abi_type_p (const_tree, machine_mode);
extern void ix86_knc_expand_vector_move (machine_mode, rtx *);
extern void ix86_knc_fixup_binary_operands (machine_mode, rtx *);
extern void ix86_knc_expand_vector_init (rtx, rtx);
extern void ix86_knc_expand_duplicate (rtx, rtx);
extern bool ix86_knc_expand_mask_vec_cmp (rtx, enum rtx_code, rtx, rtx);
extern void ix86_knc_report_loop (class vec_info *, bool);
extern bool ix86_knc_masked_epilogue_p (class _loop_vec_info *);
extern bool ix86_knc_short_masked_loop_p (class _loop_vec_info *);
extern unsigned HOST_WIDE_INT ix86_knc_x87_tail_limit (bool);
extern bool ix86_knc_vectorize_vec_perm_const (machine_mode, machine_mode, rtx,
					       rtx, rtx,
					       const vec_perm_indices &);
extern void ix86_knc_expand_sqrt (rtx *);
extern bool ix86_knc_sqrt_rtx_cost (rtx, machine_mode, int *);
extern bool ix86_knc_sqrt_stmt_p (int, class _stmt_vec_info *, tree);
extern unsigned ix86_knc_sqrt_stmt_cost (tree);
extern void ix86_knc_expand_reduc (enum rtx_code, rtx, rtx, bool = false);
extern bool ix86_knc_reduc_swizzle_p (rtx, machine_mode);
extern bool ix86_knc_two_input_perm_p (machine_mode, class _stmt_vec_info *,
				       struct _slp_tree *);

extern void ix86_expand_knc_cas (rtx *);
extern void ix86_knc_expand_atomic_exchange (rtx *);
extern void ix86_knc_expand_atomic_store (rtx *);
extern bool ix86_knc_expand_move (rtx, rtx);
extern HARD_REG_SET ix86_knc_zero_call_used_regs (HARD_REG_SET);
extern enum processor_type ix86_knc_target_macros
  (struct cpp_reader *, void (*) (struct cpp_reader *, const char *));
extern bool ix86_knc_option_override (struct gcc_options *);
extern void ix86_knc_vectorizer_params (struct gcc_options *,
					struct gcc_options *);
extern bool ix86_knc_check_abi_type (const_tree, machine_mode);
extern tree ix86_knc_va_arg_error (tree);
extern bool ix86_knc_check_abi_mode (machine_mode, bool);
extern unsigned int ix86_knc_libcall_result_regno (machine_mode, unsigned int);
extern bool ix86_knc_output_reload_p (machine_mode, reg_class_t);
extern bool ix86_knc_reload_memory_bridge_p (machine_mode, reg_class_t, reg_class_t);
extern bool ix86_knc_hard_regno_mode_ok (unsigned int, machine_mode);
extern bool ix86_knc_secondary_reload (bool, rtx, reg_class_t, machine_mode,
				       struct secondary_reload_info *);

/* Keep the ABI classifier's private enum in i386.cc.  */
template <typename RegClass>
bool
ix86_knc_check_abi_container (machine_mode mode, int n, const RegClass *classes,
			     RegClass half_class)
{
  bool half = false;
  for (int i = 0; i < n; ++i)
    half |= classes[i] == half_class;
  return ix86_knc_check_abi_mode (mode, half);
}

extern void ix86_knc_expand_unpack (enum rtx_code, rtx *, bool);
extern void ix86_knc_expand_pack (enum rtx_code, rtx *);

extern void ix86_knc_expand_div (rtx *, bool);
extern int ix86_knc_division_cost (const struct processor_costs *);

#endif /* GCC_I386_KNC_H */
