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

/* The Linux CPU affinity setting, and the default place list on KNC.  */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "libgomp.h"

static void gomp_linux_init_affinity (void);
#define gomp_init_affinity gomp_linux_init_affinity
#include "../affinity.c"
#undef gomp_init_affinity

#ifdef HAVE_PTHREAD_AFFINITY_NP

/* A KNC card numbers its logical CPUs so that the four hardware threads of
   core C are CPUs 4C+1 to 4C+4, and CPU 0 shares the last core with the three
   highest CPUs.  The card's OS keeps its own work on that core.  One place per
   logical CPU in number order, the Linux default, therefore fills one core
   four deep before it uses the next.  The KNC default is one place per core
   of the affinity mask, without the core of CPU 0 unless the mask holds no
   other; env.c spreads the threads over these places.

   The two readers below store, for CPU 0 and each CPU of the affinity mask
   below MAX, a number in CORE that two CPUs share when they are on one core.
   They return false when it is missing for one of these CPUs.  */

static bool
gomp_knc_cores_from_sysfs (unsigned long *core, unsigned long max)
{
  char name[sizeof ("/sys/devices/system/cpu/cpu/topology/"
		    "thread_siblings_list") + 3 * sizeof (unsigned long)];
  unsigned long i;

  for (i = 0; i < max; i++)
    if (i == 0 || CPU_ISSET_S (i, gomp_cpuset_size, gomp_cpusetp))
      {
	FILE *f;
	int found;

	/* The first sibling: the kernel lists them in increasing order.  */
	sprintf (name, "/sys/devices/system/cpu/cpu%lu/topology/"
		 "thread_siblings_list", i);
	f = fopen (name, "r");
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

static bool
gomp_knc_cores_from_cpuinfo (unsigned long *core, unsigned long max)
{
  FILE *f = fopen ("/proc/cpuinfo", "r");
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
    if ((i == 0 || CPU_ISSET_S (i, gomp_cpuset_size, gomp_cpusetp))
	&& core[i] == ULONG_MAX)
      return false;
  return true;
}

/* Build the KNC default place list.  omp_get_num_procs and the default
   number of threads then count the CPUs of these places.  Leave
   gomp_places_list NULL if the card's topology cannot be read.  */

static void
gomp_knc_init_core_places (void)
{
  unsigned long i, j, cpus, max = 8 * gomp_cpuset_size;
  unsigned long *core;
  cpu_set_t *copy;
  void **places;
  struct gomp_icv_list *none, *all;

  if (gomp_cpusetp == NULL)
    return;
  core = malloc (max * sizeof (unsigned long));
  if (core == NULL)
    return;
  if (!gomp_knc_cores_from_sysfs (core, max)
      && !gomp_knc_cores_from_cpuinfo (core, max))
    {
      free (core);
      return;
    }

  copy = gomp_alloca (gomp_cpuset_size);
  memcpy (copy, gomp_cpusetp, gomp_cpuset_size);
  for (i = 0; i < max; i++)
    if (CPU_ISSET_S (i, gomp_cpuset_size, copy) && core[i] == core[0])
      CPU_CLR_S (i, gomp_cpuset_size, copy);
  cpus = gomp_cpuset_popcount (gomp_cpuset_size, copy);
  if (cpus == 0)
    {
      memcpy (copy, gomp_cpusetp, gomp_cpuset_size);
      cpus = gomp_cpuset_popcount (gomp_cpuset_size, copy);
    }

  places = gomp_affinity_alloc (cpus, true);
  if (places == NULL)
    {
      free (core);
      return;
    }
  gomp_places_list = places;
  gomp_places_list_len = 0;
  for (i = 0; i < max; i++)
    if (CPU_ISSET_S (i, gomp_cpuset_size, copy))
      {
	cpu_set_t *place = (cpu_set_t *) places[gomp_places_list_len++];

	CPU_ZERO_S (gomp_cpuset_size, place);
	for (j = i; j < max; j++)
	  if (CPU_ISSET_S (j, gomp_cpuset_size, copy) && core[j] == core[i])
	    {
	      CPU_SET_S (j, gomp_cpuset_size, place);
	      CPU_CLR_S (j, gomp_cpuset_size, copy);
	    }
      }
  free (core);

  gomp_available_cpus = cpus;
  none = gomp_get_initial_icv_item (GOMP_DEVICE_NUM_FOR_NO_SUFFIX);
  all = gomp_get_initial_icv_item (GOMP_DEVICE_NUM_FOR_ALL);
  if (!(none != NULL && gomp_get_icv_flag (none->flags, GOMP_ICV_NTHREADS))
      && !(all != NULL && gomp_get_icv_flag (all->flags, GOMP_ICV_NTHREADS)))
    gomp_global_icv.nthreads_var = cpus;
}

#endif

void
gomp_init_affinity (void)
{
#ifdef HAVE_PTHREAD_AFFINITY_NP
  if (gomp_places_list == NULL)
    gomp_knc_init_core_places ();
#endif
  gomp_linux_init_affinity ();
}
