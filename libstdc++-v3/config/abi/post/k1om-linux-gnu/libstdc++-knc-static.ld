/* Implicit linker script for KNC (k1om), named by g++ before a static -lstdc++
   (-static-libstdc++ or -static; LIBSTDCXX_RUNTIME_GUARD_STATIC in
   gcc/config/i386/linux-k1om.h).

   The MPSS toolchain carries the GCC 5 libstdc++.a, found first through a -L
   directory as libstdc++-knc.ld describes.  An archive member is only in the
   link when something pulls it, so EXTERN pulls one.  Both GLIBCXX_3.4.36
   symbols live in large members (tzdb.o, format-inst.o: about 370 KB of text);
   the std.compat module initializer (GLIBCXX_3.4.35, GCC 15) is a 1-byte
   function alone in std.compat.o with no references, and is in no GCC 5
   archive.  */

EXTERN(_ZGIW3stdW6compat)
ASSERT(DEFINED(_ZGIW3stdW6compat), "GCC-KNC: the libstdc++ found first is older than this compiler's libstdc++.so.6 (its libstdc++.a lacks GCC 15's symbols; for example the GCC 5 libstdc++.a); remove the -L path that supplies it")
