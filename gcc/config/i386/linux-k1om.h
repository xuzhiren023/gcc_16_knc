/* Definitions for Intel K1OM running GNU/Linux.
   Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

/* Select KNC before the common option handler derives the default tune.
   Reference: donor config/i386/linuxk1om.h; ISA separation: ADR-0002.  */
#undef SUBSUBTARGET_OVERRIDE_OPTIONS
#define SUBSUBTARGET_OVERRIDE_OPTIONS \
  do { \
    if (!opts->x_ix86_arch_string) \
      opts->x_ix86_arch_string = "knc"; \
  } while (0)

#undef TARGET_SUBTARGET64_ISA_DEFAULT
#define TARGET_SUBTARGET64_ISA_DEFAULT 0
