/* Copyright (C) 2026 Free Software Foundation, Inc.

   This file is part of the GNU Offloading and Multi Processing Library
   (libgomp).

   Libgomp is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   Libgomp is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */

/* Provide target-specific access to the futex system call.  */

/* KNC (k1om) makes the x86-64 system call; only the spin hint differs.  The
   x86 cpu_relax is PAUSE, which KNC does not have, and for which the compiler
   substitutes DELAY with a fixed count.  */
#define cpu_relax gomp_x86_cpu_relax
#include "../x86/futex.h"
#undef cpu_relax

/* The cycle count of one spin iteration's DELAY (config/linux/knc/mutex.c).  */
extern unsigned int gomp_knc_delay;

/* Intel 327364-001 Appendix A, DELAY r32: the core does not fetch or issue
   instructions for this hardware thread for that many cycles, so a spinning
   thread leaves the issue slots of its in-order core to its siblings.  No
   flags are modified.  */
static inline void
cpu_relax (void)
{
  __asm volatile ("delay %0" : : "r" (gomp_knc_delay) : "memory");
}
