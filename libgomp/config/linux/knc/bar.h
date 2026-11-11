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

/* The KNC team barrier.  The Linux barrier (config/linux/bar.h) has every
   thread decrement one counter and poll one generation word, and on a KNC
   card each of those is a cache line crossing the ring between up to 61
   cores.  Here the threads of a team form groups, at the default binding
   (affinity.c) the hardware threads of one core, and a thread touches only
   its group's line and reads the root:

   - A thread arrives at its group's counter; the one that completes the
     group arrives at the root's.
   - The root generation word is the only holder of the BAR_* flags and the
     only word a decision is taken from, exactly as in the Linux barrier, so
     task.c works on it unchanged.
   - A waiter spins and sleeps on its group's SEQ word.  A release, a
     gomp_team_barrier_wake and a cancel change the root and then bump SEQ in
     the groups that have waiters.  SEQ is a count of these bumps and not a
     copy of the root: two concurrent writers of a copy could leave the older
     value behind, and a thread that sleeps on it then is never woken.

   The barrier type is used for team barriers only, whose thread count never
   changes (gomp_new_team); the thread pool's dock keeps the one-level
   barrier (simple-bar.h).  -DGOMP_KNC_FLAT_BARRIER builds the Linux barrier
   instead, as the baseline to measure against.  */

/* The cores the default place list holds (affinity.c); env.c calls
   gomp_knc_init_ncores with either barrier.  */
extern unsigned int gomp_knc_ncores;
extern void gomp_knc_init_ncores (void);

#ifdef GOMP_KNC_FLAT_BARRIER
#include "../bar.h"
#else
#ifndef GOMP_BARRIER_H
#define GOMP_BARRIER_H 1

#include "mutex.h"

/* A card has at most 61 cores.  */
#define GOMP_KNC_BAR_GROUPS	64

struct gomp_knc_bar_group
{
  /* Bumped to make this group's waiters read the root again.  */
  unsigned seq __attribute__((aligned (64)));
  /* The threads waiting on SEQ, plus GOMP_KNC_BAR_PARKED for each of them
     that is in futex_wait.  */
  unsigned waiters;
  /* The threads of this group still to arrive.  */
  unsigned awaited;
  unsigned awaited_final;
  unsigned size;
};

#define GOMP_KNC_BAR_PARKED	0x10000

typedef struct
{
  /* As in the Linux barrier, total/generation is in a mostly read cacheline
     and awaited in a separate one.  AWAITED counts groups.  */
  unsigned total __attribute__((aligned (64)));
  unsigned generation;
  unsigned ngroups;
  /* Thread TEAM_ID is in group TEAM_ID / GSIZE.  */
  unsigned gsize;
  unsigned awaited __attribute__((aligned (64)));
  unsigned awaited_final;
  struct gomp_knc_bar_group group[GOMP_KNC_BAR_GROUPS];
} gomp_barrier_t;

/* Each group is one cache line of its own.  */
extern char gomp_knc_bar_group_check
  [sizeof (struct gomp_knc_bar_group) == 64
   && __builtin_offsetof (gomp_barrier_t, group) % 64 == 0 ? 1 : -1];

typedef unsigned int gomp_barrier_state_t;

/* The generation field contains a counter in the high bits, with a few
   low bits dedicated to flags.  Note that TASK_PENDING and WAS_LAST can
   share space because WAS_LAST is never stored back to generation.  */
#define BAR_TASK_PENDING	1
#define BAR_WAS_LAST		1
#define BAR_WAITING_FOR_TASK	2
#define BAR_CANCELLED		4
#define BAR_INCR		8

/* The master fixes the groups before it releases the team: consecutive
   team ids share a group, which is how team.c fills a place that takes
   several threads.  */

static inline void gomp_barrier_init (gomp_barrier_t *bar, unsigned count)
{
  unsigned ngroups = gomp_knc_ncores, i;

  if (ngroups > GOMP_KNC_BAR_GROUPS)
    ngroups = GOMP_KNC_BAR_GROUPS;
  if (ngroups > count)
    ngroups = count;
  if (ngroups == 0)
    ngroups = 1;
  bar->gsize = (count + ngroups - 1) / ngroups;
  if (bar->gsize == 0)
    bar->gsize = 1;
  ngroups = (count + bar->gsize - 1) / bar->gsize;
  for (i = 0; i < ngroups; i++)
    {
      struct gomp_knc_bar_group *grp = &bar->group[i];

      grp->size = i + 1 < ngroups ? bar->gsize : count - i * bar->gsize;
      grp->awaited = grp->size;
      grp->awaited_final = grp->size;
      grp->seq = 0;
      grp->waiters = 0;
    }
  bar->total = count;
  bar->ngroups = ngroups;
  bar->awaited = ngroups;
  bar->awaited_final = ngroups;
  bar->generation = 0;
}

static inline void gomp_barrier_destroy (gomp_barrier_t *bar)
{
}

extern void gomp_barrier_wait (gomp_barrier_t *);
extern void gomp_barrier_wait_last (gomp_barrier_t *);
extern void gomp_barrier_wait_end (gomp_barrier_t *, gomp_barrier_state_t);
extern void gomp_team_barrier_wait (gomp_barrier_t *);
extern void gomp_team_barrier_wait_final (gomp_barrier_t *);
extern void gomp_team_barrier_wait_end (gomp_barrier_t *,
					gomp_barrier_state_t);
extern bool gomp_team_barrier_wait_cancel (gomp_barrier_t *);
extern bool gomp_team_barrier_wait_cancel_end (gomp_barrier_t *,
					       gomp_barrier_state_t);
extern void gomp_team_barrier_wake (gomp_barrier_t *, int);
struct gomp_team;
extern void gomp_team_barrier_cancel (struct gomp_team *);

/* Out of line: the group comes from the calling thread's team id.  */
extern gomp_barrier_state_t gomp_barrier_wait_start (gomp_barrier_t *);
extern gomp_barrier_state_t gomp_barrier_wait_final_start (gomp_barrier_t *);

static inline gomp_barrier_state_t
gomp_barrier_wait_cancel_start (gomp_barrier_t *bar)
{
  return gomp_barrier_wait_start (bar);
}

static inline bool
gomp_barrier_last_thread (gomp_barrier_state_t state)
{
  return state & BAR_WAS_LAST;
}

/* All the inlines below must be called with team->task_lock
   held.  They are those of config/linux/bar.h.  */

static inline void
gomp_team_barrier_set_task_pending (gomp_barrier_t *bar)
{
  bar->generation |= BAR_TASK_PENDING;
}

static inline void
gomp_team_barrier_clear_task_pending (gomp_barrier_t *bar)
{
  bar->generation &= ~BAR_TASK_PENDING;
}

static inline void
gomp_team_barrier_set_waiting_for_tasks (gomp_barrier_t *bar)
{
  bar->generation |= BAR_WAITING_FOR_TASK;
}

static inline bool
gomp_team_barrier_waiting_for_tasks (gomp_barrier_t *bar)
{
  return (bar->generation & BAR_WAITING_FOR_TASK) != 0;
}

static inline bool
gomp_team_barrier_cancelled (gomp_barrier_t *bar)
{
  return __builtin_expect ((bar->generation & BAR_CANCELLED) != 0, 0);
}

static inline void
gomp_team_barrier_done (gomp_barrier_t *bar, gomp_barrier_state_t state)
{
  /* Need the atomic store for acquire-release synchronisation with the
     load in `gomp_team_barrier_wait_{cancel_,}end`.  See PR112356  */
  __atomic_store_n (&bar->generation, (state & -BAR_INCR) + BAR_INCR,
		    MEMMODEL_RELEASE);
}

static inline bool
gomp_barrier_state_is_incremented (gomp_barrier_state_t gen,
				   gomp_barrier_state_t state)
{
  unsigned next_state = (state & -BAR_INCR) + BAR_INCR;
  return next_state > state ? gen >= next_state : gen < state;
}

static inline bool
gomp_barrier_has_completed (gomp_barrier_state_t state, gomp_barrier_t *bar)
{
  /* Handling overflow in the generation.  The "next" state could be less than
     or greater than the current one.  */
  return gomp_barrier_state_is_incremented (bar->generation, state);
}

#endif /* GOMP_BARRIER_H */
#endif /* !GOMP_KNC_FLAT_BARRIER */
