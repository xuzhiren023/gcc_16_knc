/* { dg-do compile } */
/* { dg-options "-O2 -march=knc -fopt-info-vec" } */
/* { dg-prune-output "vectorized using .* byte vectors" } */
/* { dg-final { scan-assembler-not "%[xy]mm" } } */

/* S11-23: generic integer-mode SLP fallback is legal on KNC when it lowers
   to scalar GPR moves.  The 16-byte V2DI and 2-byte V2QI reports for these
   stores must not introduce SSE or AVX instructions.  tools/check.py's
   --rrtm selection also assembles and checks this output with knc_lint.  */
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
