/* KNC compatibility CPU ABI.
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

#include "cpuid.h"
#include "auto-target.h"
#include "common/config/i386/i386-cpuinfo.h"

/* The donor ABI is four 32-bit words, as in i386/cpuinfo.c.  There is no
   KNC type/subtype or IMCI feature bit in this ABI.  In particular, IMCI
   is not AVX512F.  Do not import the x86 detector's XGETBV or report
   unsupported CMOV, MMX, SSE or AVX features from its generic paths.  */
struct
{
  unsigned int __cpu_vendor;
  unsigned int __cpu_type;
  unsigned int __cpu_subtype;
  unsigned int __cpu_features[1];
} __cpu_model;

#ifdef HAVE_INIT_PRIORITY
#define KNC_CPU_INIT_PRIORITY (101)
#else
#define KNC_CPU_INIT_PRIORITY
#endif

int __cpu_indicator_init (void)
  __attribute__ ((constructor KNC_CPU_INIT_PRIORITY));

int
__cpu_indicator_init (void)
{
  unsigned int eax, ebx, ecx, edx;

  if (__cpu_model.__cpu_vendor)
    return 0;

  __cpuid (0, eax, ebx, ecx, edx);
  if (ebx != signature_INTEL_ebx || ecx != signature_INTEL_ecx
      || edx != signature_INTEL_edx || eax < 1)
    {
      __cpu_model.__cpu_vendor = VENDOR_OTHER;
      return -1;
    }

  __cpuid (1, eax, ebx, ecx, edx);
  if (ecx & bit_POPCNT)
    __cpu_model.__cpu_features[0] = 1U << FEATURE_POPCNT;
  __cpu_model.__cpu_vendor = VENDOR_INTEL;
  return 0;
}
