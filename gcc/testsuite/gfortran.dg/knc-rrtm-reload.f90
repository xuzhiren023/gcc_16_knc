! { dg-do compile { target { x86_64-*-* k1om-*-* } } }
! { dg-options "-O2 -march=knc" }
!
! S11-23: reduced from WRF 4.8.0 phys/module_ra_rrtm.F, RTRN.
! See knc-rrtm-reload.LICENSE for the retained WRF notice.
! Compile-only reduction: values and bounds need not be initialized.
! LRA leaves an SF register copy recognized as *movsf_internal, whose
! SSE constraints are unavailable on KNC; postreload then ICEs.
module module_ra_rrtm
integer, parameter :: ngpt=140
integer, save :: nlayers
real, save :: bpade
contains
subroutine rtrn(kts, ktep1, cldfrac, totdflux, icldlyr)
REAL, DIMENSION( kts:ktep1 ), INTENT(IN   ) ::       CLDFRAC
REAL, DIMENSION(   0:ktep1 ),INTENT(INOUT)::         TOTDFLUX
INTEGER, DIMENSION( kts:ktep1 ), INTENT(IN   ) ::    ICLDLYR
DIMENSION BBUTOT(NGPT*(ktep1-kts))
DIMENSION ABSS(NGPT*(ktep1-kts+1))
DIMENSION ATOT(NGPT*(ktep1-kts))
DIMENSION ODCLR(NGPT,kts:ktep1-1)
DIMENSION ODCLD(kts:ktep1-1)
DIMENSION EFCLFRAC(kts:ktep1-1)
DIMENSION RADLD(NGPT)
do LEV = NLAYERS, 1, -1
IF (ICLDLYR(LEV).EQ.1) THEN
do IPR = 1, NGPT
INDEX = IENT + IPR
ODSM = ODCLR(IPR,LEV) + ODCLD(LEV)
FACTOT = ODSM / (BPADE + ODSM)
BBUTOT(INDEX) = BGLAY + FACTOT * DELBGUP
RADLD(IPR) = RADLD(IPR) - RADLD(IPR) * (ABSS(INDEX) + &
  EFCLFRAC(LEV) * (1.-ABSS(INDEX))) + GASSRC + &
  CLDFRAC(LEV) * (BBDLEVD * ATOT(INDEX) - GASSRC)
end do
ELSE
do IPR = 1, NGPT
DRAD = DRAD + RADLD(IPR)
end do
ENDIF
TOTDFLUX(LEV-1) = DRAD * WTNUM
end do
END  SUBROUTINE RTRN
end module
