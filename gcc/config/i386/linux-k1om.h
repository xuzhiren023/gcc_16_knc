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

/* Donor config/i386/linuxk1om.h uses x87 precision with 16-byte storage,
   not IEEE binary128 long double.  */
#undef TARGET_SUBTARGET64_DEFAULT
#define TARGET_SUBTARGET64_DEFAULT MASK_128BIT_LONG_DOUBLE

/* Keep GCC 16's GNU/Linux link policy, selecting the donor's K1OM ABI
   through the emulation and interpreter hooks in linux64.h/gnu-user64.h.  */
#undef GNU_USER_LINK_EMULATION64
#define GNU_USER_LINK_EMULATION64 "elf_k1om"

#undef GLIBC_DYNAMIC_LINKER64
#define GLIBC_DYNAMIC_LINKER64 "/lib64/ld-linux-k1om.so.2"

#undef ASM_SPEC
#define ASM_SPEC "%{" SPEC_32 ":--32} %{" SPEC_64 ":--march=k1om}"

/* Match the donor's debug format for the card's debugger.  */
#undef DWARF_VERSION_DEFAULT
#define DWARF_VERSION_DEFAULT 4

/* Use gcc.cc:init_gcc_specs for shared/static libgcc selection, including
   Linux's as-needed default and static-libgcc override.  Keep libatomic
   explicit until that separate runtime is provided by this port.  */
#undef TARGET_PROVIDES_LIBATOMIC

/* g++ names one of these implicit linker scripts after -lstdc++ (cp/g++spec.cc).
   libstdc++ installs them on k1om (config/abi/post/k1om-linux-gnu); they fail
   a link whose -lstdc++ found an older library, such as the card's GCC 5 one.  */
#define LIBSTDCXX_RUNTIME_GUARD ":libstdc++-knc.ld"
#define LIBSTDCXX_RUNTIME_GUARD_STATIC ":libstdc++-knc-static.ld"

/* The driver otherwise injects its configured x86-64/generic defaults
   before SUBSUBTARGET_OVERRIDE_OPTIONS can select KNC.  */
#undef OPTION_DEFAULT_SPECS
#define OPTION_DEFAULT_SPECS \
  {"cpu", "%{!mtune=*:%{!mcpu=*:%{!march=*:-mtune=knc}}}"}, \
  {"arch", "%{!march=*:-march=knc}"}, \
  {"tls", "%{!mtls-dialect=*:-mtls-dialect=%(VALUE)}"},
