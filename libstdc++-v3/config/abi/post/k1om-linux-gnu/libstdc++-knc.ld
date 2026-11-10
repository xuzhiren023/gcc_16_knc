/* Implicit linker script for KNC (k1om), named by g++ before a shared -lstdc++
   (LIBSTDCXX_RUNTIME_GUARD in gcc/config/i386/linux-k1om.h).

   The card and the MPSS toolchain carry the GCC 5 libstdc++.so.6.0.21.  A -L
   directory holding it is searched before this compiler's own, and a program
   whose library calls all exist in both would then link against the old
   library and run with GCC 16 headers against a GCC 5 runtime.  This symbol
   is in the GLIBCXX_3.4.36 node of every libstdc++.so.6 this compiler builds
   and in no older one.  A shared library's symbols are defined in the link
   without a reference, so no EXTERN: the program gains no version need.  */

ASSERT(DEFINED(_ZNSt6chrono8__detail25__recent_leap_second_infoERNS_16leap_second_infoEj), "GCC-KNC: the libstdc++ found first is older than this compiler's libstdc++.so.6 (it lacks GLIBCXX_3.4.36; for example the GCC 5 libstdc++.so.6.0.21); remove the -L path that supplies it")
