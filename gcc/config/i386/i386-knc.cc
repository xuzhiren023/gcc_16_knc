/* Copyright (C) 1988-2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* KNC-only helpers; shared i386 entry points dispatch here (ADR-0005).  */
#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "i386-knc.h"

/* Exactly the packed storage modes in ADR-0005 point 1.  This predicate
   does not advertise arithmetic, an ABI, or automatic vectorization.  */
bool
ix86_knc_packed_mode_p (machine_mode mode)
{
  return TARGET_KNC && TARGET_64BIT
         && (mode == V16SFmode || mode == V8DFmode
             || mode == V16SImode || mode == V8DImode);
}
