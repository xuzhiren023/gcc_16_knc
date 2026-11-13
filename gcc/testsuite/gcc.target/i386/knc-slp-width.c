/* { dg-do compile } */
/* { dg-options "-O2 -march=knc -fopt-info-vec" } */
/* { dg-bogus "vectorized using ([0-9]|[0-5][0-9]|6[0-35-9]|[7-9][0-9]|[0-9][0-9][0-9]+) byte vectors" "" { target *-*-* } 0 } */
/* { dg-prune-output "vectorized using 64 byte vectors" } */

/* S11-23: SLP must not choose a partial vector for adjacent constants.
   These stores previously reported 16-byte V2DI and 2-byte V2QI groups.  */
long words[2] __attribute__((aligned(64)));
unsigned char bytes[2] __attribute__((aligned(64)));

void
store_words (void)
{
  words[0] = 1;
  words[1] = 2;
}

void
store_bytes (void)
{
  bytes[0] = 1;
  bytes[1] = 2;
}
