// { dg-do compile }
// { dg-options "-O2 -march=knc" }

extern "C" long double cbrtl (long double);

long double
silenced (long double x)
{
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wknc-unsafe-cbrtl"
  x = cbrtl (x);
  #pragma GCC diagnostic pop
  return x;
}

long double
warned (long double x)
{
  return cbrtl (x); // { dg-warning "is unsafe on KNC" }
}
