/* KNC soft-fp control state.  The integer arithmetic and IEEE formats are
   shared with i386/64; MXCSR control access does not require SSE math.
   Reference: i386/64/sfp-machine.h and the donor's KNC runtime.  */
#include "config/i386/sfp-machine.h"

#ifndef _SOFT_FLOAT
/* KNC supports the legacy MXCSR control instructions, not VEX encodings.  */
#undef FP_INIT_ROUNDMODE
#define FP_INIT_ROUNDMODE \
  do { \
    __asm__ __volatile__ ("stmxcsr\t%0" : "=m" (_fcw)); \
  } while (0)
#endif
