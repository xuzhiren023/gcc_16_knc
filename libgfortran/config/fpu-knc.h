/* FPU-related code for Intel Knights Corner.
   Copyright (C) 2005-2026 Free Software Foundation, Inc.
   Contributed by Francois-Xavier Coudert <coudert@clipper.ens.fr>

This file is part of the GNU Fortran 95 runtime library (libgfortran).

Libgfortran is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

Libgfortran is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

/* This structure corresponds to the layout of the block
   written by FSTENV.  */
struct fenv
{
  unsigned short int __control_word;
  unsigned short int __unused1;
  unsigned short int __status_word;
  unsigned short int __unused2;
  unsigned short int __tags;
  unsigned short int __unused3;
  unsigned int __eip;
  unsigned short int __cs_selector;
  unsigned int __opcode:11;
  unsigned int __unused4:5;
  unsigned int __data_offset;
  unsigned short int __data_selector;
  unsigned short int __unused5;
  unsigned int __mxcsr;
} __attribute__ ((gcc_struct));

/* Check we can actually store the FPU state in the allocated size.  */
_Static_assert (sizeof(struct fenv) <= (size_t) GFC_FPE_STATE_BUFFER_SIZE,
		"GFC_FPE_STATE_BUFFER_SIZE is too small");

/* KNC has MXCSR but no SSE.  The S7-07 card probe (2026-11-07)
   established sticky flags 0-5 and rounding control 13-14.  R27 Table 2.15
   p.39 (verified early KNC specification): bits 7-12 and 20 are reserved;
   bit 21 is DUE (Disable Unmasked Exceptions) and must survive every write.
   IMCI does not halt on exceptions, even with x87 exceptions unmasked.
   Consequently halting and mixed x87/IMCI underflow control are unsupported.
   See fpu-387.h for the x87 environment layout and rounding encodings.  */
#define KNC_FLAGS 0x3fu
#define KNC_ROUND 0x6000u
#define KNC_RESERVED 0x101f80u
#define _FPU_RC_NEAREST 0
#define _FPU_RC_DOWN 1
#define _FPU_RC_UP 2
#define _FPU_RC_ZERO 3
#define _FPU_RC_MASK 3

/* R12 CD28 p.25 (verified production x100 erratum) specifies saving the
   full x87 state, editing its status, and restoring it to clear an unmasked
   exception.  Foreign code can unmask x87 even though our mixed-domain
   halting API is unsupported.  Both flag clearing and environment restore
   can therefore need this recovery.  Retain all eight live register values.
   The first 28 bytes have the same layout as fenv before __mxcsr.  */
struct knc_x87_state
{
  unsigned short words[54];
};
_Static_assert (sizeof (struct knc_x87_state) == 108
                && __builtin_offsetof (struct fenv, __mxcsr) == 28,
                "unexpected x87 save layout");

static void
knc_restore_x87 (struct knc_x87_state *state)
{
  unsigned short cw = state->words[0];
  unsigned short sw = state->words[2];
  /* Recompute ES and B from the remaining unmasked exception flags, so
     clearing the last pending exception cannot leave a stale summary.  */
  sw &= ~0x8080u;
  if (sw & ~cw & KNC_FLAGS)
    sw |= 0x8080u;
  state->words[2] = sw;
  __asm__ __volatile__ ("frstor\t%0" : : "m" (*state) : "memory");
}

static unsigned int
knc_mxcsr (void)
{
  /* R12 CD19 p.23 (verified production x100 erratum): a vector operand
     fault can change MXCSR.  This reads the live word; it cannot recover
     the pre-fault flags for a signal handler.  */
  unsigned int value;
  __asm__ __volatile__ ("stmxcsr\t%0" : "=m" (value));
  return value;
}

static void
knc_update_mxcsr (unsigned int mask, unsigned int value)
{
  unsigned int live = knc_mxcsr ();
  mask &= KNC_FLAGS | KNC_ROUND;
  value = ((live & ~mask) | (value & mask)) & ~KNC_RESERVED;
  __asm__ __volatile__ ("ldmxcsr\t%0" : : "m" (value));
}

static unsigned int
knc_exception_bits (int flags)
{
  unsigned int bits = 0;
  if (flags & GFC_FPE_INVALID) bits |= 1;
  if (flags & GFC_FPE_DENORMAL) bits |= 2;
  if (flags & GFC_FPE_ZERO) bits |= 4;
  if (flags & GFC_FPE_OVERFLOW) bits |= 8;
  if (flags & GFC_FPE_UNDERFLOW) bits |= 16;
  if (flags & GFC_FPE_INEXACT) bits |= 32;
  return bits;
}

void
set_fpu_trap_exceptions (int trap, int notrap)
{
  unsigned short cw;
  /* Do not silently enable x87-only halting for a mixed-domain request.
     Disabling exceptions remains safe, including masks set by foreign code.  */
  if (trap)
    estr_write ("Fortran runtime warning: KNC IEEE halting is unsupported.\n");
  __asm__ __volatile__ ("fnstcw\t%0" : "=m" (cw));
  cw |= knc_exception_bits (notrap);
  __asm__ __volatile__ ("fldcw\t%0" : : "m" (cw));
}

void
set_fpu (void)
{
  set_fpu_trap_exceptions (options.fpe, 0);
}

int
get_fpu_trap_exceptions (void)
{
  /* No exception has halting support across both execution domains.  */
  return 0;
}

int
support_fpu_trap (int flag __attribute__((unused)))
{
  return 0;
}

int
get_fpu_except_flags (void)
{
  unsigned short sw;
  unsigned int bits;
  int flags = 0;
  __asm__ __volatile__ ("fnstsw\t%0" : "=am" (sw));
  bits = (sw | knc_mxcsr ()) & KNC_FLAGS;
  if (bits & 1) flags |= GFC_FPE_INVALID;
  if (bits & 2) flags |= GFC_FPE_DENORMAL;
  if (bits & 4) flags |= GFC_FPE_ZERO;
  if (bits & 8) flags |= GFC_FPE_OVERFLOW;
  if (bits & 16) flags |= GFC_FPE_UNDERFLOW;
  if (bits & 32) flags |= GFC_FPE_INEXACT;
  return flags;
}

void
set_fpu_except_flags (int set, int clear)
{
  struct knc_x87_state state;
  unsigned int raised = knc_exception_bits (set);
  unsigned int cleared = knc_exception_bits (clear);
  /* Set sticky state directly: flag setting is not an attempt to emulate
     unsupported IMCI halting.  Setting wins when both arguments name a bit.  */
  __asm__ __volatile__ ("fnsave\t%0" : "=m" (state) : : "memory");
  state.words[2] = (state.words[2] & ~cleared) | raised;
  if (cleared & 1)
    state.words[2] &= ~0x40u; /* SF accompanies the invalid exception.  */
  knc_restore_x87 (&state);
  knc_update_mxcsr (cleared | raised, raised);
}

int
support_fpu_flag (int flag)
{
  const int supported = GFC_FPE_INVALID | GFC_FPE_DENORMAL | GFC_FPE_ZERO
                        | GFC_FPE_OVERFLOW | GFC_FPE_UNDERFLOW | GFC_FPE_INEXACT;
  return flag != 0 && (flag & ~supported) == 0;
}

void
set_fpu_rounding_mode (int round)
{
  int round_mode;
  unsigned short cw;

  switch (round)
    {
    case GFC_FPE_TONEAREST:
      round_mode = _FPU_RC_NEAREST;
      break;
    case GFC_FPE_UPWARD:
      round_mode = _FPU_RC_UP;
      break;
    case GFC_FPE_DOWNWARD:
      round_mode = _FPU_RC_DOWN;
      break;
    case GFC_FPE_TOWARDZERO:
      round_mode = _FPU_RC_ZERO;
      break;
    default:
      return; /* Should be unreachable.  */
    }

  __asm__ __volatile__ ("fnstcw\t%0" : "=m" (cw));

  /* The x87 round control bits are shifted by 10 bits.  */
  cw &= ~(_FPU_RC_MASK << 10);
  cw |= round_mode << 10;

  __asm__ __volatile__ ("fldcw\t%0" : : "m" (cw));

  knc_update_mxcsr (0x6000, (unsigned int) round_mode << 13);
}

int
get_fpu_rounding_mode (void)
{
  int round_mode;

  unsigned short cw;
  __asm__ __volatile__ ("fnstcw\t%0" : "=m" (cw));
  round_mode = cw >> 10;

  round_mode &= _FPU_RC_MASK;

  switch (round_mode)
    {
    case _FPU_RC_NEAREST:
      return GFC_FPE_TONEAREST;
    case _FPU_RC_UP:
      return GFC_FPE_UPWARD;
    case _FPU_RC_DOWN:
      return GFC_FPE_DOWNWARD;
    case _FPU_RC_ZERO:
      return GFC_FPE_TOWARDZERO;
    default:
      return 0; /* Should be unreachable.  */
    }
}

int
support_fpu_rounding_mode (int mode)
{
  return mode == GFC_FPE_TONEAREST || mode == GFC_FPE_DOWNWARD
         || mode == GFC_FPE_UPWARD || mode == GFC_FPE_TOWARDZERO;
}

void
get_fpu_state (void *state)
{
  struct fenv *env = state;
  __asm__ __volatile__ ("fnstenv\t%0" : "=m" (*env));
  /* FNSTENV masks exceptions as a side effect.  */
  __asm__ __volatile__ ("fldcw\t%0" : : "m" (env->__control_word));
  env->__mxcsr = knc_mxcsr ();
}

void
set_fpu_state (void *state)
{
  struct fenv *env = state;
  struct knc_x87_state live;
  __asm__ __volatile__ ("fnsave\t%0" : "=m" (live) : : "memory");
  __builtin_memcpy (live.words, env, 28);
  knc_restore_x87 (&live);
  /* Restore every supported MXCSR field, retaining live opaque controls.
     In particular, never load a saved ordinary-x86 exception mask.  */
  knc_update_mxcsr (KNC_FLAGS | KNC_ROUND, env->__mxcsr);
}

int
support_fpu_underflow_control (int kind __attribute__((unused)))
{
  return 0;
}

int
get_fpu_underflow_mode (void)
{
  /* The original probe's 2^-126 * 2^-126 rounds to binary32 zero even
     with gradual underflow; it did not establish flushing.  R27 section
     4.1 pp.55-56 (verified early KNC specification) distinguishes DAZ
     inputs from FZ results.  Separate x87/IMCI tiny-result probes are
     needed for production-card evidence.  Until mixed-domain control is
     qualified, keep the unsupported API response, not a hardware claim.  */
  return 0;
}

void
set_fpu_underflow_mode (int gradual __attribute__((unused)))
{
  /* Neither request changes FTZ/DAZ or pretends to control x87 underflow.  */
  estr_write ("Fortran runtime warning: KNC IEEE underflow control is unsupported.\n");
}
