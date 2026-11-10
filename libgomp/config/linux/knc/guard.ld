/* Implicit linker script for KNC (k1om), named by libgomp.spec.

   The card and the MPSS toolchain can carry an older libgomp.so.1 (GCC 5)
   and its libgomp.a under the same names.  A -L directory holding them is
   searched before this compiler's own, and a program whose runtime calls all
   exist in both would then link against the old runtime.
   omp_get_device_from_uid (GOMP_6.0) is in every libgomp this compiler builds
   and in no older one; EXTERN pulls it from the archive in a static link, and
   ASSERT fails the link when nothing defined it.  */

EXTERN(omp_get_device_from_uid)
ASSERT(DEFINED(omp_get_device_from_uid), "GCC-KNC: the libgomp found first is older than this compiler's libgomp.so.1 (for example a GCC 5 libgomp.so.1); remove the -L path that supplies it")
