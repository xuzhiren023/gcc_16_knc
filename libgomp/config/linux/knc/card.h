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

/* What libgomp reads from the KNC card it runs on, instead of one model's
   constants: the core count, the core clock and the memory that sizes
   glibc's malloc arena cap (ADR-0012 in GCC-KNC).  The card's model number
   is not read: /proc/cpuinfo gives only family 0b model 01, and memory is
   what decides.  The readers take a ROOT prefixed to /proc and /sys, ""
   on the card, so that they can be checked on fixture trees.  */

#ifndef GOMP_KNC_CARD_H
#define GOMP_KNC_CARD_H 1

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <limits.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fallback MHz estimate when /proc/cpuinfo has no "cpu MHz".
   This is not a model clock: R05 p.5 and R21 p.3 give about 1.05 GHz
   for their 5110P configurations.  */
#define GOMP_KNC_MHZ_DEFAULT 1100

/* The default operand of the DELAY in a spin iteration (mutex.c,
   GOMP_KNC_DELAY), in cycles.  On the card, three siblings spinning with 32
   slowed the worker on their core by 6.8%, with 128 by 1.1%, and empty
   fork-join regions were 2.4-3.3% faster (S9-08 in GCC-KNC).  */
#define GOMP_KNC_DELAY_DEFAULT 128

/* How long a waiting thread spins by default before it sleeps, in msec
   (spincount.h); GOMP_SPINCOUNT and OMP_WAIT_POLICY override it.  On the
   card, entering a region after 10 msec of serial work took 3.75 msec with
   3 msec of spin (the threads slept and had to be woken) and 0.30 msec with
   200 (S9-15 in GCC-KNC).  */
#define GOMP_KNC_SPIN_MSEC 200

/* The cores the barrier groups count when the card's topology cannot be
   read: a 7120's 61 without the OS core.  */
#define GOMP_KNC_NCORES_DEFAULT 60

/* Each hardware thread of a core is one logical CPU.  */
#define GOMP_KNC_THREADS_PER_CORE 4

/* The address space glibc 2.21 reserves for each arena but the main one:
   HEAP_MAX_SIZE, 2 * DEFAULT_MMAP_THRESHOLD_MAX on LP64.  */
#define GOMP_KNC_ARENA_BYTES (64ULL << 20)

/* The smallest cap libgomp derives; fewer arenas than this make the threads
   queue on the arena locks.  The lead sets it from measurement 7.  */
#define GOMP_KNC_ARENA_MIN 8

/* Address space the budget keeps for everything but the arenas and the
   thread stacks: the program, its libraries, the main heap, and the second
   64 MiB glibc maps for a moment to align a new heap.  */
#define GOMP_KNC_ARENA_RESERVE (512ULL << 20)

/* The stack glibc 2.21 gives a thread on x86-64 and k1om when RLIMIT_STACK
   is unlimited: ARCH_STACK_DEFAULT_SIZE.  */
#define GOMP_KNC_STACK_DEFAULT (2ULL << 20)

/* No limit, as RLIM_INFINITY.  */
#define GOMP_KNC_NO_LIMIT (~0ULL)

/* The inputs of the arena cap.  */
struct gomp_knc_mem
{
  unsigned long long mem_total;		/* MemTotal in bytes; 0 if unread.  */
  unsigned long long commit_limit;	/* CommitLimit in bytes; 0 if unread.  */
  int overcommit;			/* overcommit_memory; -1 if unread.  */
  unsigned long long rlimit_as;		/* RLIMIT_AS soft limit, or NO_LIMIT.  */
  unsigned long cpus;			/* CPUs of the affinity mask.  */
  unsigned long long stack;		/* The stack each thread gets.  */
};

enum gomp_knc_arena_source
{
  GOMP_KNC_ARENA_USER,		/* MALLOC_ARENA_MAX was set.  */
  GOMP_KNC_ARENA_DERIVED	/* libgomp set the cap.  */
};

enum gomp_knc_budget
{
  GOMP_KNC_BUDGET_NONE,		/* Nothing bounds the reservations.  */
  GOMP_KNC_BUDGET_RLIMIT_AS,
  GOMP_KNC_BUDGET_COMMIT_LIMIT
};

/* What libgomp decided, for omp_display_env.  */
struct gomp_knc_arena
{
  enum gomp_knc_arena_source source;
  const char *user;		/* MALLOC_ARENA_MAX, when SOURCE is USER.  */
  unsigned long cap;		/* The derived cap.  */
  enum gomp_knc_budget budget_kind;
  unsigned long long budget;	/* Bytes, unless BUDGET_KIND is NONE.  */
  struct gomp_knc_mem in;
};

/* One arena per allowed CPU unless reservations and thread stacks do not
   fit in the budget; then the largest count that fits, but no fewer than
   GOMP_KNC_ARENA_MIN (or the CPUs, if fewer).  The budget is RLIMIT_AS when
   finite and CommitLimit under strict overcommit, the smaller when both
   apply.  With neither, this implementation uses one arena per CPU and
   ignores MemTotal.  This diverges from ADR-0012 point 3's measured share
   of MemTotal; the lead must reconcile that policy (S12-14 N09/B18).
   R24a, new_heap/arena_get2, distinguishes virtual reservations from
   physical consumption; it does not establish a fastest cap.  Historical
   measurement 7 in GCC-KNC observed about 169 arenas taking 64-67 s versus
   140-149 s at a cap of 60-64 for that malloc-heavy job.  */

static inline unsigned long
gomp_knc_arena_cap (const struct gomp_knc_mem *in, struct gomp_knc_arena *out)
{
  unsigned long long budget = GOMP_KNC_NO_LIMIT, need, fit;
  enum gomp_knc_budget kind = GOMP_KNC_BUDGET_NONE;
  unsigned long cpus = in->cpus ? in->cpus : 1, cap, floor;

  if (in->rlimit_as != GOMP_KNC_NO_LIMIT)
    {
      budget = in->rlimit_as;
      kind = GOMP_KNC_BUDGET_RLIMIT_AS;
    }
  if (in->overcommit == 2 && in->commit_limit != 0
      && in->commit_limit < budget)
    {
      budget = in->commit_limit;
      kind = GOMP_KNC_BUDGET_COMMIT_LIMIT;
    }

  if (kind == GOMP_KNC_BUDGET_NONE)
    cap = cpus;
  else
    {
      need = cpus * in->stack + GOMP_KNC_ARENA_RESERVE;
      fit = budget > need ? (budget - need) / GOMP_KNC_ARENA_BYTES : 0;
      cap = fit < cpus ? fit : cpus;
      floor = GOMP_KNC_ARENA_MIN < cpus ? GOMP_KNC_ARENA_MIN : cpus;
      if (cap < floor)
	cap = floor;
    }

  out->source = GOMP_KNC_ARENA_DERIVED;
  out->user = NULL;
  out->cap = cap;
  out->budget_kind = kind;
  out->budget = budget;
  out->in = *in;
  return cap;
}

/* Decide the arena cap: none when USER, the MALLOC_ARENA_MAX of the
   environment, is set (glibc reads it itself and it wins), else the derived
   one.  Returns the cap for mallopt (M_ARENA_MAX), or 0 for none.  */

static inline unsigned long
gomp_knc_arena_decide (const char *user, const struct gomp_knc_mem *in,
		       struct gomp_knc_arena *out)
{
  if (user != NULL && *user != '\0')
    {
      memset (out, 0, sizeof *out);
      out->source = GOMP_KNC_ARENA_USER;
      out->user = user;
      out->in = *in;
      return 0;
    }
  return gomp_knc_arena_cap (in, out);
}

/* The verbose omp_display_env line of the arena cap.  */

static inline void
gomp_knc_display_arena (FILE *f, const struct gomp_knc_arena *a)
{
  static const char *const kinds[]
    = { "none", "RLIMIT_AS", "CommitLimit" };

  if (a->source == GOMP_KNC_ARENA_USER)
    {
      fprintf (f, "  [host] KNC malloc arena cap = '%s' (user: MALLOC_ARENA_MAX)\n",
	       a->user);
      return;
    }
  fprintf (f, "  [host] KNC malloc arena cap = '%lu' (derived: budget ", a->cap);
  if (a->budget_kind == GOMP_KNC_BUDGET_NONE)
    fputs ("none", f);
  else
    fprintf (f, "%llu MiB from %s", a->budget >> 20, kinds[a->budget_kind]);
  fprintf (f, ", %lu CPUs, %llu KiB stacks)\n", a->in.cpus, a->in.stack >> 10);
}

/* Open ROOT followed by PATH.  */

static inline FILE *
gomp_knc_open (const char *root, const char *path)
{
  char name[PATH_MAX];

  if (snprintf (name, sizeof name, "%s%s", root, path) >= (int) sizeof name)
    return NULL;
  return fopen (name, "r");
}

/* MemTotal and CommitLimit of /proc/meminfo, in bytes, 0 when missing.  */

static inline void
gomp_knc_read_meminfo (const char *root, unsigned long long *mem_total,
		       unsigned long long *commit_limit)
{
  FILE *f = gomp_knc_open (root, "/proc/meminfo");
  char line[256];
  unsigned long long kb;

  *mem_total = *commit_limit = 0;
  if (f == NULL)
    return;
  while (fgets (line, sizeof line, f))
    if (sscanf (line, "MemTotal: %llu kB", &kb) == 1)
      *mem_total = kb << 10;
    else if (sscanf (line, "CommitLimit: %llu kB", &kb) == 1)
      *commit_limit = kb << 10;
  fclose (f);
}

/* /proc/sys/vm/overcommit_memory, or -1.  */

static inline int
gomp_knc_read_overcommit (const char *root)
{
  FILE *f = gomp_knc_open (root, "/proc/sys/vm/overcommit_memory");
  int mode;

  if (f == NULL)
    return -1;
  if (fscanf (f, "%d", &mode) != 1)
    mode = -1;
  fclose (f);
  return mode;
}

/* The core clock in MHz: the first "cpu MHz" of /proc/cpuinfo, rounded
   (1238 on a 7120), or GOMP_KNC_MHZ_DEFAULT.  */

static inline unsigned int
gomp_knc_read_mhz (const char *root)
{
  FILE *f = gomp_knc_open (root, "/proc/cpuinfo");
  char line[256];
  double mhz = 0;

  if (f == NULL)
    return GOMP_KNC_MHZ_DEFAULT;
  while (fgets (line, sizeof line, f))
    if (sscanf (line, "cpu MHz : %lf", &mhz) == 1)
      break;
  fclose (f);
  if (!(mhz >= 1 && mhz < 100000))
    return GOMP_KNC_MHZ_DEFAULT;
  return (unsigned int) (mhz + 0.5);
}

/* The spin count that spins MSEC msec at MHZ in DELAY periods of DELAY
   cycles, at least 1.  */

static inline unsigned long long
gomp_knc_spincount (unsigned int msec, unsigned int mhz, unsigned int delay)
{
  unsigned long long count = 1000ULL * msec * mhz / delay;

  return count ? count : 1;
}

/* The default spin count: GOMP_KNC_SPIN_MSEC at MHZ in DELAY periods
   (spincount.h).  */

static inline unsigned long long
gomp_knc_default_spincount (unsigned int mhz, unsigned int delay)
{
  return gomp_knc_spincount (GOMP_KNC_SPIN_MSEC, mhz, delay);
}

/* A KNC card numbers its logical CPUs so that the four hardware threads of
   core C are CPUs 4C+1 to 4C+4, and CPU 0 shares the last core with the three
   highest CPUs.  The card's OS keeps its own work on that core.

   The two readers below store, for CPU 0 and each CPU of WANT below MAX, a
   number in CORE that two CPUs share when they are on one core.  They
   return false when it is missing for one of these CPUs.  */

static inline bool
gomp_knc_cores_from_sysfs (const char *root, unsigned long *core,
			   unsigned long max, const cpu_set_t *want,
			   size_t setsize)
{
  char name[sizeof ("/sys/devices/system/cpu/cpu/topology/"
		    "thread_siblings_list") + 3 * sizeof (unsigned long)];
  unsigned long i;

  for (i = 0; i < max; i++)
    if (i == 0 || CPU_ISSET_S (i, setsize, want))
      {
	FILE *f;
	int found;

	/* The first sibling: the kernel lists them in increasing order.  */
	sprintf (name, "/sys/devices/system/cpu/cpu%lu/topology/"
		 "thread_siblings_list", i);
	f = gomp_knc_open (root, name);
	if (f == NULL)
	  return false;
	found = fscanf (f, "%lu", &core[i]);
	fclose (f);
	if (found != 1)
	  return false;
      }
  return true;
}

/* A card is one package, so the "core id" of /proc/cpuinfo names a core.  */

static inline bool
gomp_knc_cores_from_cpuinfo (const char *root, unsigned long *core,
			     unsigned long max, const cpu_set_t *want,
			     size_t setsize)
{
  FILE *f = gomp_knc_open (root, "/proc/cpuinfo");
  char *line = NULL;
  size_t linelen = 0;
  unsigned long i, val, cpu = max;

  if (f == NULL)
    return false;
  for (i = 0; i < max; i++)
    core[i] = ULONG_MAX;
  while (getline (&line, &linelen, f) > 0)
    if (sscanf (line, "processor : %lu", &val) == 1)
      cpu = val;
    else if (sscanf (line, "core id : %lu", &val) == 1 && cpu < max)
      core[cpu] = val;
  free (line);
  fclose (f);

  for (i = 0; i < max; i++)
    if ((i == 0 || CPU_ISSET_S (i, setsize, want)) && core[i] == ULONG_MAX)
      return false;
  return true;
}

/* The online CPUs, /sys/devices/system/cpu/online ("0-243"), into SET.  */

static inline bool
gomp_knc_read_online (const char *root, cpu_set_t *set, size_t setsize)
{
  FILE *f = gomp_knc_open (root, "/sys/devices/system/cpu/online");
  unsigned long first, last, i;
  bool any = false;
  int c;

  CPU_ZERO_S (setsize, set);
  if (f == NULL)
    return false;
  while (fscanf (f, "%lu", &first) == 1)
    {
      last = first;
      c = fgetc (f);
      if (c == '-')
	{
	  if (fscanf (f, "%lu", &last) != 1)
	    break;
	  c = fgetc (f);
	}
      for (i = first; i <= last && i < 8 * setsize; i++)
	CPU_SET_S (i, setsize, set);
      any = true;
      if (c != ',')
	break;
    }
  fclose (f);
  return any;
}

/* The cores the default place list has when it cannot be built: the cores
   of the online CPUs without the OS core (the core of CPU 0), from the
   topology readers; the online CPUs over four less the OS core when the
   topology is missing; GOMP_KNC_NCORES_DEFAULT when neither can be read.
   56 on a 3120, 59 on a 5110P and 60 on a 7120.  */

static inline unsigned int
gomp_knc_count_cores (const char *root)
{
  cpu_set_t online;
  unsigned long max = 8 * sizeof online, i, j, n = 0, cpus;
  unsigned long *core;

  if (!gomp_knc_read_online (root, &online, sizeof online))
    return GOMP_KNC_NCORES_DEFAULT;
  cpus = CPU_COUNT_S (sizeof online, &online);
  core = malloc (max * sizeof *core);
  if (core != NULL
      && (gomp_knc_cores_from_sysfs (root, core, max, &online, sizeof online)
	  || gomp_knc_cores_from_cpuinfo (root, core, max, &online,
					  sizeof online)))
    {
      for (i = 0; i < max; i++)
	if (CPU_ISSET_S (i, sizeof online, &online) && core[i] != core[0])
	  {
	    for (j = 0; j < i; j++)
	      if (CPU_ISSET_S (j, sizeof online, &online)
		  && core[j] == core[i])
		break;
	    n += j == i;
	  }
    }
  else
    {
      n = (cpus + GOMP_KNC_THREADS_PER_CORE - 1) / GOMP_KNC_THREADS_PER_CORE;
      if (n > 1)
	n--;
    }
  free (core);
  return n ? n : 1;
}

#endif /* GOMP_KNC_CARD_H */
