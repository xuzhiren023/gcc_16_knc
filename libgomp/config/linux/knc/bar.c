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

/* The KNC team barrier (bar.h) and the dock's barrier (simple-bar.h).  The
   functions follow config/linux/bar.c one for one: where that file waits on
   or wakes the generation word, this one waits on the caller's group or
   notifies the groups.  */

#ifdef GOMP_KNC_FLAT_BARRIER
#include "../bar.c"
#else

#include <limits.h>
#include "wait.h"

/* The calling thread's group.  */

static inline struct gomp_knc_bar_group *
gomp_knc_bar_group (gomp_barrier_t *bar)
{
  struct gomp_thread *thr = gomp_thread ();
  unsigned id = 0;

  /* gomp_team_end waits a last time on the barrier of a nested team it has
     already left, as the thread 0 it was there.  */
  if (thr->ts.team != NULL && &thr->ts.team->barrier == bar)
    id = thr->ts.team_id;
  return &bar->group[id / bar->gsize];
}

/* Arrive at the group's counter GROUP and, as the last thread of the group,
   at the root's counter ROOT.  */

static inline gomp_barrier_state_t
gomp_knc_bar_arrive (gomp_barrier_t *bar, unsigned *group, unsigned size,
		     unsigned *root)
{
  unsigned int ret = __atomic_load_n (&bar->generation, MEMMODEL_ACQUIRE);
  ret &= -BAR_INCR | BAR_CANCELLED;
  /* MEMMODEL_ACQ_REL is the implicit flush of a barrier region; see
     gomp_barrier_wait_start in config/linux/bar.h.  */
  if (__atomic_add_fetch (group, -1, MEMMODEL_ACQ_REL) == 0)
    {
      /* Next time the group awaits all its threads again.  None of them
	 arrives again before the root releases, and the root does not
	 release before the decrement below.  */
      __atomic_store_n (group, size, MEMMODEL_RELEASE);
      if (__atomic_add_fetch (root, -1, MEMMODEL_ACQ_REL) == 0)
	ret |= BAR_WAS_LAST;
    }
  return ret;
}

gomp_barrier_state_t
gomp_barrier_wait_start (gomp_barrier_t *bar)
{
  struct gomp_knc_bar_group *grp = gomp_knc_bar_group (bar);

  return gomp_knc_bar_arrive (bar, &grp->awaited, grp->size, &bar->awaited);
}

/* This is like gomp_barrier_wait_start, except it decrements the
   awaited_final counters and should be used for the gomp_team_end barrier
   only.  */

gomp_barrier_state_t
gomp_barrier_wait_final_start (gomp_barrier_t *bar)
{
  struct gomp_knc_bar_group *grp = gomp_knc_bar_group (bar);

  return gomp_knc_bar_arrive (bar, &grp->awaited_final, grp->size,
			      &bar->awaited_final);
}

/* The last thread of a barrier: next time we'll be awaiting all the groups
   again.  The groups have rearmed themselves, except after `cancel parallel':
   that leaves the groups of a cancellable barrier part arrived, and every
   thread then comes to the team's final barrier, whose last thread is
   here.  */

static inline void
gomp_knc_bar_rearm (gomp_barrier_t *bar)
{
  unsigned i;

  bar->awaited = bar->ngroups;
  if (__builtin_expect (__atomic_load_n (&bar->generation, MEMMODEL_ACQUIRE)
			& BAR_CANCELLED, 0))
    for (i = 0; i < bar->ngroups; i++)
      bar->group[i].awaited = bar->group[i].size;
}

/* Wait in group GRP until it is notified, unless the root generation is no
   longer EXPECTED, and return the root generation.  The thread announces
   itself in WAITERS before it reads the root, and gomp_knc_bar_notify reads
   WAITERS after the root has changed, so one of the two sees the other.  A
   bump of SEQ between the read of SEQ here and futex_wait makes futex_wait
   return at once.  */

static inline unsigned
gomp_knc_bar_wait (gomp_barrier_t *bar, struct gomp_knc_bar_group *grp,
		   unsigned expected)
{
  unsigned gen = __atomic_load_n (&bar->generation, MEMMODEL_ACQUIRE), seq;

  if (gen != expected)
    return gen;
  __atomic_add_fetch (&grp->waiters, 1, MEMMODEL_SEQ_CST);
  seq = __atomic_load_n (&grp->seq, MEMMODEL_ACQUIRE);
  if (__atomic_load_n (&bar->generation, MEMMODEL_ACQUIRE) == expected
      && do_spin ((int *) &grp->seq, seq))
    {
      __atomic_add_fetch (&grp->waiters, GOMP_KNC_BAR_PARKED,
			  MEMMODEL_SEQ_CST);
      futex_wait ((int *) &grp->seq, seq);
      __atomic_add_fetch (&grp->waiters, -GOMP_KNC_BAR_PARKED,
			  MEMMODEL_SEQ_CST);
    }
  __atomic_add_fetch (&grp->waiters, -1, MEMMODEL_SEQ_CST);
  return __atomic_load_n (&bar->generation, MEMMODEL_ACQUIRE);
}

/* The root generation has changed: make COUNT waiters, or all of them for
   INT_MAX, read it again.  Group lines without a waiter are only read.  */

static void
gomp_knc_bar_notify (gomp_barrier_t *bar, int count)
{
  unsigned i;

  /* The caller's store to the root comes before the loads of WAITERS.  */
  __atomic_thread_fence (MEMMODEL_SEQ_CST);
  for (i = 0; i < bar->ngroups && count > 0; i++)
    {
      struct gomp_knc_bar_group *grp = &bar->group[i];
      unsigned waiters = __atomic_load_n (&grp->waiters, MEMMODEL_ACQUIRE);

      if (waiters == 0)
	continue;
      __atomic_add_fetch (&grp->seq, 1, MEMMODEL_SEQ_CST);
      /* A waiter that is not counted as parked here has not read SEQ in
	 futex_wait yet, and will read the new one.  */
      if (__atomic_load_n (&grp->waiters, MEMMODEL_SEQ_CST)
	  >= GOMP_KNC_BAR_PARKED)
	futex_wake ((int *) &grp->seq, count);
      if (count != INT_MAX)
	count -= waiters & (GOMP_KNC_BAR_PARKED - 1);
    }
}

void
gomp_barrier_wait_end (gomp_barrier_t *bar, gomp_barrier_state_t state)
{
  if (__builtin_expect (state & BAR_WAS_LAST, 0))
    {
      /* Next time we'll be awaiting all the groups again.  */
      bar->awaited = bar->ngroups;
      __atomic_store_n (&bar->generation, bar->generation + BAR_INCR,
			MEMMODEL_RELEASE);
      gomp_knc_bar_notify (bar, INT_MAX);
    }
  else
    {
      struct gomp_knc_bar_group *grp = gomp_knc_bar_group (bar);

      while (gomp_knc_bar_wait (bar, grp, state) == state)
	continue;
    }
}

void
gomp_barrier_wait (gomp_barrier_t *bar)
{
  gomp_barrier_wait_end (bar, gomp_barrier_wait_start (bar));
}

/* Like gomp_barrier_wait, except that if the encountering thread
   is not the last one to hit the barrier, it returns immediately.
   The intended usage is that a thread which intends to gomp_barrier_destroy
   this barrier calls gomp_barrier_wait, while all other threads
   call gomp_barrier_wait_last.  When gomp_barrier_wait returns,
   the barrier can be safely destroyed.  */

void
gomp_barrier_wait_last (gomp_barrier_t *bar)
{
  gomp_barrier_state_t state = gomp_barrier_wait_start (bar);
  if (state & BAR_WAS_LAST)
    gomp_barrier_wait_end (bar, state);
}

void
gomp_team_barrier_wake (gomp_barrier_t *bar, int count)
{
  gomp_knc_bar_notify (bar, count == 0 ? INT_MAX : count);
}

void
gomp_team_barrier_wait_end (gomp_barrier_t *bar, gomp_barrier_state_t state)
{
  unsigned int generation, gen;
  struct gomp_knc_bar_group *grp;

  if (__builtin_expect (state & BAR_WAS_LAST, 0))
    {
      struct gomp_thread *thr = gomp_thread ();
      struct gomp_team *team = thr->ts.team;

      gomp_knc_bar_rearm (bar);
      team->work_share_cancelled = 0;
      unsigned task_count
	= __atomic_load_n (&team->task_count, MEMMODEL_ACQUIRE);
      if (__builtin_expect (task_count, 0))
	{
	  gomp_barrier_handle_tasks (state);
	  state &= ~BAR_WAS_LAST;
	}
      else
	{
	  state &= ~BAR_CANCELLED;
	  state += BAR_INCR - BAR_WAS_LAST;
	  __atomic_store_n (&bar->generation, state, MEMMODEL_RELEASE);
	  gomp_knc_bar_notify (bar, INT_MAX);
	  return;
	}
    }

  grp = gomp_knc_bar_group (bar);
  generation = state;
  state &= ~BAR_CANCELLED;
  do
    {
      gen = gomp_knc_bar_wait (bar, grp, generation);
      if (__builtin_expect (gen & BAR_TASK_PENDING, 0))
	{
	  gomp_barrier_handle_tasks (state);
	  gen = __atomic_load_n (&bar->generation, MEMMODEL_ACQUIRE);
	}
      generation |= gen & BAR_WAITING_FOR_TASK;
    }
  while (!gomp_barrier_state_is_incremented (gen, state));
}

void
gomp_team_barrier_wait (gomp_barrier_t *bar)
{
  gomp_team_barrier_wait_end (bar, gomp_barrier_wait_start (bar));
}

void
gomp_team_barrier_wait_final (gomp_barrier_t *bar)
{
  gomp_barrier_state_t state = gomp_barrier_wait_final_start (bar);
  if (__builtin_expect (state & BAR_WAS_LAST, 0))
    bar->awaited_final = bar->ngroups;
  gomp_team_barrier_wait_end (bar, state);
}

bool
gomp_team_barrier_wait_cancel_end (gomp_barrier_t *bar,
				   gomp_barrier_state_t state)
{
  unsigned int generation, gen;
  struct gomp_knc_bar_group *grp;

  if (__builtin_expect (state & BAR_WAS_LAST, 0))
    {
      /* BAR_CANCELLED should never be set in state here, because
	 cancellation means that at least one of the threads has been
	 cancelled, thus on a cancellable barrier we should never see
	 all threads to arrive.  */
      struct gomp_thread *thr = gomp_thread ();
      struct gomp_team *team = thr->ts.team;

      gomp_knc_bar_rearm (bar);
      team->work_share_cancelled = 0;
      unsigned task_count
	= __atomic_load_n (&team->task_count, MEMMODEL_ACQUIRE);
      if (__builtin_expect (task_count, 0))
	{
	  gomp_barrier_handle_tasks (state);
	  state &= ~BAR_WAS_LAST;
	}
      else
	{
	  state += BAR_INCR - BAR_WAS_LAST;
	  __atomic_store_n (&bar->generation, state, MEMMODEL_RELEASE);
	  gomp_knc_bar_notify (bar, INT_MAX);
	  return false;
	}
    }

  if (__builtin_expect (state & BAR_CANCELLED, 0))
    return true;

  grp = gomp_knc_bar_group (bar);
  generation = state;
  do
    {
      gen = gomp_knc_bar_wait (bar, grp, generation);
      if (__builtin_expect (gen & BAR_CANCELLED, 0))
	return true;
      if (__builtin_expect (gen & BAR_TASK_PENDING, 0))
	{
	  gomp_barrier_handle_tasks (state);
	  gen = __atomic_load_n (&bar->generation, MEMMODEL_ACQUIRE);
	}
      generation |= gen & BAR_WAITING_FOR_TASK;
    }
  while (!gomp_barrier_state_is_incremented (gen, state));

  return false;
}

bool
gomp_team_barrier_wait_cancel (gomp_barrier_t *bar)
{
  return gomp_team_barrier_wait_cancel_end (bar, gomp_barrier_wait_start (bar));
}

void
gomp_team_barrier_cancel (struct gomp_team *team)
{
  gomp_mutex_lock (&team->task_lock);
  if (team->barrier.generation & BAR_CANCELLED)
    {
      gomp_mutex_unlock (&team->task_lock);
      return;
    }
  team->barrier.generation |= BAR_CANCELLED;
  gomp_mutex_unlock (&team->task_lock);
  gomp_knc_bar_notify (&team->barrier, INT_MAX);
}

/* The dock's barrier: gomp_barrier_wait and gomp_barrier_wait_last of
   config/linux/bar.c, for a barrier that never carries a flag.  */

static void
gomp_knc_dock_wait (gomp_simple_barrier_t *bar, bool last_only)
{
  unsigned state = __atomic_load_n (&bar->generation, MEMMODEL_ACQUIRE);

  if (__atomic_add_fetch (&bar->awaited, -1, MEMMODEL_ACQ_REL) == 0)
    {
      /* Next time we'll be awaiting TOTAL threads again.  */
      bar->awaited = bar->total;
      __atomic_store_n (&bar->generation, bar->generation + BAR_INCR,
			MEMMODEL_RELEASE);
      futex_wake ((int *) &bar->generation, INT_MAX);
    }
  else if (!last_only)
    do
      do_wait ((int *) &bar->generation, state);
    while (__atomic_load_n (&bar->generation, MEMMODEL_ACQUIRE) == state);
}

void
gomp_simple_barrier_wait (gomp_simple_barrier_t *bar)
{
  gomp_knc_dock_wait (bar, false);
}

void
gomp_simple_barrier_wait_last (gomp_simple_barrier_t *bar)
{
  gomp_knc_dock_wait (bar, true);
}

#endif /* !GOMP_KNC_FLAT_BARRIER */
