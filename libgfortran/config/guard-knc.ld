/* Implicit linker script for KNC (k1om), named by libgfortran.spec.

   The card and the MPSS toolchain carry the GCC 5 libgfortran.so.3 and its
   libgfortran.a.  A -L directory holding them is searched before this
   compiler's own, and a program whose runtime calls all exist in both would
   then link against the old runtime and run across the GCC 8 descriptor ABI
   change.  _gfortran_os_error_at (GFORTRAN_10) is in every libgfortran this
   compiler builds and in no GCC 5 one; EXTERN pulls it from the archive in a
   static link, and ASSERT fails the link when nothing defined it.  */

EXTERN(_gfortran_os_error_at)
ASSERT(DEFINED(_gfortran_os_error_at), "GCC-KNC: the libgfortran found first is older than this compiler's libgfortran.so.5 (for example the GCC 5 libgfortran.so.3); remove the -L path that supplies it")
