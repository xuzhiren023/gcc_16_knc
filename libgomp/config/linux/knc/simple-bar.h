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

/* The barrier of the thread pool's dock on KNC with -DGOMP_KNC_GROUP_BARRIER.
   team.c grows the dock while threads wait on it (gomp_simple_barrier_reinit),
   which the grouped team barrier of bar.h cannot do, so the dock is the
   one-level Linux barrier without the team flags: config/posix/simple-bar.h
   over config/linux/bar.h, with its own type.  A default build has
   config/posix/simple-bar.h itself.  */

#ifndef GOMP_KNC_GROUP_BARRIER
#include "../../posix/simple-bar.h"
#else
#ifndef GOMP_SIMPLE_BARRIER_H
#define GOMP_SIMPLE_BARRIER_H 1

#include "bar.h"

typedef struct
{
  unsigned total __attribute__((aligned (64)));
  unsigned generation;
  unsigned awaited __attribute__((aligned (64)));
} gomp_simple_barrier_t;

static inline void
gomp_simple_barrier_init (gomp_simple_barrier_t *bar, unsigned count)
{
  bar->total = count;
  bar->awaited = count;
  bar->generation = 0;
}

static inline void
gomp_simple_barrier_reinit (gomp_simple_barrier_t *bar, unsigned count)
{
  __atomic_add_fetch (&bar->awaited, count - bar->total, MEMMODEL_ACQ_REL);
  bar->total = count;
}

static inline void
gomp_simple_barrier_destroy (gomp_simple_barrier_t *bar)
{
}

/* In bar.c.  */
extern void gomp_simple_barrier_wait (gomp_simple_barrier_t *);
extern void gomp_simple_barrier_wait_last (gomp_simple_barrier_t *);

#endif /* GOMP_SIMPLE_BARRIER_H */
#endif /* GOMP_KNC_GROUP_BARRIER */
