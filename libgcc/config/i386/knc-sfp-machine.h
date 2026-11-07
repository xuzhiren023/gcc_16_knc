/* KNC soft-fp control state.  The integer arithmetic and IEEE formats are
   shared with i386/64; MXCSR control access does not require SSE math.
   Reference: i386/64/sfp-machine.h and the donor's KNC runtime.  */
#include "config/i386/sfp-machine.h"

/* A single-precision significand (including work bits) has 27 bits, so
   its full product fits in the inherited 64-bit word.  Reuse soft-fp's
   integer division loop, including its sticky remainder bit.  */
#define _FP_MUL_MEAT_S(R, X, Y) \
  _FP_MUL_MEAT_1_imm (_FP_WFRACBITS_S, R, X, Y)
#define _FP_DIV_MEAT_S(R, X, Y) _FP_DIV_MEAT_1_loop (S, R, X, Y)

#ifndef _SOFT_FLOAT
/* KNC supports the legacy MXCSR control instructions, not VEX encodings.  */
#undef FP_INIT_ROUNDMODE
#define FP_INIT_ROUNDMODE \
  do { \
    __asm__ __volatile__ ("stmxcsr\t%0" : "=m" (_fcw)); \
  } while (0)
#endif
