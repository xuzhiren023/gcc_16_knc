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
#include "card.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/* The operand of the DELAY in cpu_relax (futex.h, mutex.c).  */
extern unsigned int gomp_knc_delay;

/* Read GOMP_KNC_DELAY, the cycle count of one spin iteration's DELAY: a
   decimal from 1 to 4294967295.  Anything else is reported and the default
   kept.  env.c calls this before it sets the spin counts.  */
static inline void
gomp_knc_init_delay (void)
{
  const char *env = getenv ("GOMP_KNC_DELAY");
  char *end;
  unsigned long long value;

  if (env == NULL)
    return;
  while (isspace ((unsigned char) *env))
    ++env;
  if (!isdigit ((unsigned char) *env))
    goto invalid;
  errno = 0;
  value = strtoull (env, &end, 10);
  if (errno || value == 0 || value > 0xffffffffULL)
    goto invalid;
  while (isspace ((unsigned char) *end))
    ++end;
  if (*end != '\0')
    goto invalid;
  gomp_knc_delay = value;
  return;

 invalid:
  gomp_error ("Invalid value for environment variable GOMP_KNC_DELAY");
}

/* The verbose omp_display_env line for GOMP_KNC_DELAY.  */
static inline void
gomp_knc_display_delay (FILE *f)
{
  fprintf (f, "  [host] GOMP_KNC_DELAY = '%u'\n", gomp_knc_delay);
}

/* One spin iteration on KNC is one DELAY of gomp_knc_delay cycles (futex.h),
   not one PAUSE, so the generic count of 300000 does not mean 3 msec here.
   The default is 3 msec at the card's core clock in DELAY periods (card.h):
   103125 at 1100 MHz and the default delay of 32, 116062 on a 7120's
   1238 MHz.  There is no CPUID probe: KNC is not a hybrid part.  */
static inline void do_adjust_default_spincount (void)
{
  gomp_spin_count_var
    = gomp_knc_default_spincount (gomp_knc_read_mhz (""), gomp_knc_delay);
}

#endif /* GOMP_SPINCOUNT_H */
