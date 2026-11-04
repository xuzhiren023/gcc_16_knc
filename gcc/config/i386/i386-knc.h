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

extern bool ix86_knc_packed_mode_p (machine_mode);
extern machine_mode ix86_knc_preferred_simd_mode (scalar_mode);
extern bool ix86_knc_support_vector_misalignment (machine_mode, int, bool, bool);

extern bool ix86_knc_zero_constant_p (rtx, machine_mode);
extern bool ix86_knc_unsupported_abi_type_p (const_tree, machine_mode);
extern void ix86_knc_expand_packed_move (machine_mode, rtx *);
extern void ix86_knc_fixup_binary_operands (machine_mode, rtx *);
extern void ix86_knc_expand_vector_init (rtx, rtx);
extern void ix86_knc_expand_duplicate (rtx, rtx);

extern void ix86_expand_knc_cas (rtx *);
extern bool ix86_knc_expand_move (rtx, rtx);
extern enum processor_type ix86_knc_target_macros
  (struct cpp_reader *, void (*) (struct cpp_reader *, const char *));
extern bool ix86_knc_option_override (struct gcc_options *);
extern bool ix86_knc_check_abi_type (const_tree, machine_mode);
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

#endif /* GCC_I386_KNC_H */
