/* { dg-do compile } */
/* { dg-options "-O2 -march=knc" } */

extern long double cbrtl (long double);

long double
silenced (long double x)
{
  _Pragma ("GCC diagnostic push")
  _Pragma ("GCC diagnostic ignored \"-Wknc-unsafe-cbrtl\"")
  x = cbrtl (x);
  _Pragma ("GCC diagnostic pop")
  return x;
}

long double
warned (long double x)
{
  return cbrtl (x); /* { dg-warning "is unsafe on KNC" } */
}

