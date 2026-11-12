/* Scalar cube root for the Intel Xeon Phi x100 coprocessor.
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

/* Original implementation: normalize x = a * 2^(3*q), 1 <= a < 8.
   Seven Newton steps from 2 give a full-precision x87 approximation.
   One binary128 residual correction removes accumulated x87 rounding
   error before the single rounding to long double.  Binary128 arithmetic
   uses libgcc's existing soft-fp helpers; there is no libm call or table.
   The final exponent adjustment is exact and always normal, even for
   the smallest subnormal input.  This code requires the KNC 80-bit ABI.  */
typedef union
{
  long double value;
  struct { unsigned long long significand; unsigned short exponent; } bits;
} knc_extended;

long double
__knc_cbrtl (long double x)
{
  knc_extended a = { .value = x };
  unsigned sign = a.bits.exponent & 0x8000;
  int e = a.bits.exponent & 0x7fff;
  if (e == 0x7fff)
    return x + x;
  if (e == 0 && a.bits.significand == 0)
    return x;
  if (e == 0)
    {
      e = 1;
      while (!(a.bits.significand >> 63))
	{
	  a.bits.significand <<= 1;
	  --e;
	}
    }
  e -= 16383;
  int q = e / 3, r = e % 3;
  if (r < 0)
    { r += 3; --q; }
  a.bits.exponent = 16383 + r;
  long double y = 2.0L;
  for (int i = 0; i < 7; ++i)
    y = (2.0L * y + a.value / (y * y)) / 3.0L;
  __float128 t = y;
  t += ((__float128) a.value - t * t * t) / (3 * t * t);
  /* Apply the sign before rounding, including directed rounding modes.  */
  a.value = sign ? -t : t;
  a.bits.exponent = (a.bits.exponent & 0x7fff) + q + sign;
  return a.value;
}
