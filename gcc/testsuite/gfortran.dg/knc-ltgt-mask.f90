! { dg-do compile { target { i?86-*-* x86_64-*-* k1om-*-* } } }
! { dg-options "-O2 -march=knc" }
! { dg-final { scan-assembler {vcmpps[\t ]+\$1,} } }
! { dg-final { scan-assembler-not {vcmpps[\t ]+\$4,} } }
! { dg-final { scan-assembler-not {vcmpps[\t ]+\$7,} } }
!
! Equal expressions on ordered branches can become an LTGT vector mask.
! NaNs and either signed zero must leave the destination unchanged.
! The ordered comparisons must still raise invalid on QNaNs and SNaNs.
subroutine ordered_nonzero(dst, src, scale, lhs, rhs, n)
  integer :: n, i
  real :: dst(n), src(n), scale(n), lhs(n), rhs(n)
  do i = 1, n
    if (src(i) > 0.0) then
      dst(i) = scale(i) * (lhs(i) - rhs(i))
    else if (src(i) < 0.0) then
      dst(i) = scale(i) * (lhs(i) - rhs(i))
    end if
  end do
end subroutine
