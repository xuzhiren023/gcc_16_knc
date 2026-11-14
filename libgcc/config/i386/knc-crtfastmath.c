/* KNC fast-math startup.
   Copyright (C) 2026 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 3, or (at your option) any later
   version.

   GCC is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */

/* Reference: donor libgcc/config/i386/crtfastmath.c, __x86_64__ branch.
   KNC supports these MXCSR instructions without enabling the SSE ISA.
   R27 Table 2.15 p.39 (verified early KNC specification): FZ is bit 15,
   DAZ is bit 6, bits 7-12 and 20 are reserved, and bit 21 is DUE
   (Disable Unmasked Exceptions).  Set only FZ and DAZ; retain DUE and
   other controls, and never introduce reserved bits.  */
static void __attribute__ ((constructor))
set_fast_math (void)
{
  unsigned int mxcsr;
  __asm__ __volatile__ ("stmxcsr %0\n\t"
		      "orl $0x8040, %0\n\t"
		      "ldmxcsr %0"
		      : "=m" (mxcsr));
}
