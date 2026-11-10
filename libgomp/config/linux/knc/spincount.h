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

#ifndef GOMP_SPINCOUNT_H
#define GOMP_SPINCOUNT_H 1

#include "libgomp.h"

/* One spin iteration on KNC is one DELAY of gomp_knc_delay cycles (futex.h),
   not one PAUSE, so the generic count of 300000 does not mean 3 msec here.
   At the default delay of 32 cycles and the 1.1 GHz core clock, 3 msec is
   about 100000 iterations.  There is no CPUID probe: KNC is not a hybrid
   part.  */
static inline void do_adjust_default_spincount (void)
{
  gomp_spin_count_var = 100000LL;
}

#endif /* GOMP_SPINCOUNT_H */
