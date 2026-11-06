;; KNC scalar ABI transport (ADR-0004).
;; Lane-zero broadcasts and masked stores follow donor knc.md and i386.md.
;; No SSE constraints or implicit scratch registers.
;; Transfers use UNSPEC to retain the memory bridge: forwarding a SET into
;; a plain x87/zmm register copy would require an unavailable instruction.

;; KNC storage classes do not enable SSE or AVX instruction alternatives.
(define_register_constraint "YJ" "TARGET_KNC ? ALL_SSE_REGS : NO_REGS"
 "@internal KNC zmm storage register.")
(define_register_constraint "YK" "TARGET_KNC ? ALL_MASK_REGS : NO_REGS"
 "@internal KNC mask transport register.")
(define_register_constraint "YL" "TARGET_KNC ? MASK_REGS : NO_REGS"
 "@internal KNC nonzero predicate register.")

;; Shared move expanders retain their original SSE modes as well as KNC.
(define_mode_iterator VMOVE_KNC
  [(V64QI "TARGET_AVX512F") (V32QI "TARGET_AVX") V16QI
   (V32HI "TARGET_AVX512F") (V16HI "TARGET_AVX || TARGET_KNC") V8HI
   (V16SI "TARGET_AVX512F || TARGET_KNC") (V8SI "TARGET_AVX") V4SI
   (V8DI "TARGET_AVX512F || TARGET_KNC")  (V4DI "TARGET_AVX") V2DI
   (V4TI "TARGET_AVX512F") (V2TI "TARGET_AVX") V1TI
   (V32HF "TARGET_AVX512F") (V16HF "TARGET_AVX") V8HF
   (V32BF "TARGET_AVX512F") (V16BF "TARGET_AVX") V8BF
   (V16SF "TARGET_AVX512F || TARGET_KNC") (V8SF "TARGET_AVX") V4SF
   (V8DF "TARGET_AVX512F || TARGET_KNC")  (V4DF "TARGET_AVX") V2DF])

;; Expander-only copies of VI_AVX2, VI4_AVX512F and VI for the shared
;; integer add/sub, mul and logic expanders (ADR-0006 point 4, ADR-0007
;; point 2).  The sse.md insns keep the original iterators.  Each expander
;; condition also tests ix86_knc_packed_mode_p, so the KNC modes enabled
;; here are exactly those with a "|| TARGET_KNC" entry.  The add/sub
;; expander sends KNC to @knc_plusminus<insn><mode> on entry (ADR-0006
;; rule 2), and V8DI uses the carry sequence there (ADR-0007 point 6).
(define_mode_iterator VI_AVX2_KNC
  [(V64QI "TARGET_AVX512BW") (V32QI "TARGET_AVX2") V16QI
   (V32HI "TARGET_AVX512BW") (V16HI "TARGET_AVX2") V8HI
   (V16SI "TARGET_AVX512F || TARGET_KNC") (V8SI "TARGET_AVX2") V4SI
   (V8DI "TARGET_AVX512F || TARGET_KNC") (V4DI "TARGET_AVX2") V2DI])

(define_mode_iterator VI4_AVX512F_KNC
  [(V16SI "TARGET_AVX512F || TARGET_KNC") (V8SI "TARGET_AVX2") V4SI])

(define_mode_iterator VI_KNC
  [(V16SI "TARGET_AVX512F || TARGET_KNC") (V8DI "TARGET_AVX512F || TARGET_KNC")
   (V64QI "TARGET_AVX512BW") (V32QI "TARGET_AVX") V16QI
   (V32HI "TARGET_AVX512BW") (V16HI "TARGET_AVX") V8HI
   (V8SI "TARGET_AVX") V4SI
   (V4DI "TARGET_AVX") V2DI])

;; VF1 copy for floatuns and the signed float named insn permitted by
;; ADR-0008 point 5 (S5-08 lead amendment).  Mask/round guards stay off.
(define_mode_iterator VF1_KNC
  [(V16SF "TARGET_AVX512F || TARGET_KNC") (V8SF "TARGET_AVX") V4SF])

;; Same-width conversions, Intel 327364-001 pp.157-179, ADR-0008 4/5.
;; These precede the sse.md named insns and accept all their base operands.
;; Bm permits only aligned full-width memory; nonimmediate_operand also
;; covers vector_operand so recog cannot fall through to the x86 insns.
;; Immediate 0 means no exponent adjustment, with MXCSR rounding.
(define_insn "*knc_floatv16siv16sf2"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (float:V16SF
          (match_operand:V16SI 1 "nonimmediate_operand" "YJBm")))]
  "TARGET_KNC"
  "vcvtfxpntdq2ps\t{$0, %g1, %g0|%g0, %g1, 0}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "XI")])

(define_insn "*knc_floatunsv16siv16sf2"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (unsigned_float:V16SF
          (match_operand:V16SI 1 "nonimmediate_operand" "YJBm")))]
  "TARGET_KNC"
  "vcvtfxpntudq2ps\t{$0, %g1, %g0|%g0, %g1, 0}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "XI")])

;; Immediate 3 truncates regardless of MXCSR.RC, without suppressing IE/PE
;; (convert card probe, docs/research/card-runs-s5.md).  Bits 2/3 stay zero.
(define_insn "*knc_fix<fixunssuffix>_truncv16sfv16si2"
  [(set (match_operand:V16SI 0 "register_operand" "=YJ")
        (any_fix:V16SI
          (match_operand:V16SF 1 "nonimmediate_operand" "YJBm")))]
  "TARGET_KNC"
  "vcvtfxpntps2<fixsuffix>dq\t{$3, %g1, %g0|%g0, %g1, 3}"
  [(set_attr "type" "ssecvt")
   (set_attr "mode" "XI")])

;; Width conversions, ADR-0008 4/5; Intel 327364-001 pp.154-189.
;; Only the expander copies enable KNC; x86 insn iterators stay unchanged.
(define_mode_iterator VF2_512_256_KNC
  [(V8DF "TARGET_AVX512F || TARGET_KNC") V4DF])
(define_mode_iterator VF2_KNC
  [(V8DF "TARGET_AVX512F || TARGET_KNC") (V4DF "TARGET_AVX") V2DF])

(define_insn "*knc_vec_unpacks_lo_v16sf"
  [(set (match_operand:V8DF 0 "register_operand" "=YJ")
        (float_extend:V8DF
          (vec_select:V8SF
            (match_operand:V16SF 1 "nonimmediate_operand" "YJ")
            (parallel [(const_int 0) (const_int 1) (const_int 2) (const_int 3)
                       (const_int 4) (const_int 5) (const_int 6) (const_int 7)]))))]
  "TARGET_KNC"
  "vcvtps2pd\t{%g1, %g0|%g0, %g1}"
  [(set_attr "type" "ssecvt") (set_attr "mode" "XI")])

(define_insn "*knc_vec_unpacks_float_lo_v16si"
  [(set (match_operand:V8DF 0 "register_operand" "=YJ")
        (float:V8DF
          (vec_select:V8SI
            (match_operand:V16SI 1 "nonimmediate_operand" "YJ")
            (parallel [(const_int 0) (const_int 1) (const_int 2) (const_int 3)
                       (const_int 4) (const_int 5) (const_int 6) (const_int 7)]))))]
  "TARGET_KNC"
  "vcvtdq2pd\t{%g1, %g0|%g0, %g1}"
  [(set_attr "type" "ssecvt") (set_attr "mode" "XI")])

(define_insn "*knc_vec_unpacku_float_lo_v16si"
  [(set (match_operand:V8DF 0 "register_operand" "=YJ")
        (unsigned_float:V8DF
          (vec_select:V8SI
            (match_operand:V16SI 1 "nonimmediate_operand" "YJ")
            (parallel [(const_int 0) (const_int 1) (const_int 2) (const_int 3)
                       (const_int 4) (const_int 5) (const_int 6) (const_int 7)]))))]
  "TARGET_KNC"
  "vcvtudq2pd\t{%g1, %g0|%g0, %g1}"
  [(set_attr "type" "ssecvt") (set_attr "mode" "XI")])

(define_insn "knc_cvtpd2ps_lo"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (vec_concat:V16SF
          (float_truncate:V8SF (match_operand:V8DF 1 "register_operand" "YJ"))
          (match_operand:V8SF 2 "const0_operand" "C")))]
  "TARGET_KNC"
  "vcvtpd2ps\t{%g1, %g0|%g0, %g1}"
  [(set_attr "type" "ssecvt") (set_attr "mode" "XI")])

(define_insn "knc_cvtpd2dq_lo"
  [(set (match_operand:V16SI 0 "register_operand" "=YJ")
        (vec_concat:V16SI
          (fix:V8SI (match_operand:V8DF 1 "register_operand" "YJ"))
          (match_operand:V8SI 2 "const0_operand" "C")))]
  "TARGET_KNC"
  "vcvtfxpntpd2dq\t{$3, %g1, %g0|%g0, %g1, 3}"
  [(set_attr "type" "ssecvt") (set_attr "mode" "XI")])

(define_insn "knc_cvtpd2udq_lo"
  [(set (match_operand:V16SI 0 "register_operand" "=YJ")
        (vec_concat:V16SI
          (unsigned_fix:V8SI (match_operand:V8DF 1 "register_operand" "YJ"))
          (match_operand:V8SI 2 "const0_operand" "C")))]
  "TARGET_KNC"
  "vcvtfxpntpd2udq\t{$3, %g1, %g0|%g0, %g1, 3}"
  [(set_attr "type" "ssecvt") (set_attr "mode" "XI")])

;; ADR-0008 point 7, including the S5-16 transport-width amendment.
;; Intel 327364-001 pp.136-139: these broadcasts read exactly 16/32 bytes.
;; V16QI occupies the low 128 bits; V16HI occupies the low 256 bits.
;; Registers copy all of zmm; no byte arithmetic or movmisalign is exposed.
(define_mode_iterator KNC_NARROW [V16QI V16HI])
(define_code_attr knc_extend_kind [(sign_extend "s") (zero_extend "u")])
(define_mode_attr knc_narrow_bits [(V16QI "8") (V16HI "16")])
(define_mode_attr knc_transport_bits [(V16QI "32") (V16HI "64")])

(define_insn "*knc_transport_move<mode>"
  [(set (match_operand:KNC_NARROW 0 "register_operand" "=YJ,YJ")
        (match_operand:KNC_NARROW 1 "nonimmediate_operand" "YJ,Bm"))]
  "TARGET_KNC && !misaligned_operand (operands[1], <MODE>mode)"
  "@
   vmovdqa32\t{%g1, %g0|%g0, %g1}
   vbroadcasti<knc_transport_bits>x4\t{%1, %g0|%g0, %1}"
  [(set_attr "type" "ssemov") (set_attr "mode" "XI")])

;; Intel 327364-001 pp.426-428: pack only the low two/four qwords.
;; Natural alignment guarantees that the entire value is in this line.
;; Expose both scratches to LRA, including for moves inserted by reload.
(define_insn_and_split "knc_transport_store<mode>"
  [(set (match_operand:KNC_NARROW 0 "memory_operand" "=Bm")
        (match_operand:KNC_NARROW 1 "register_operand" "YJ"))
   (clobber (match_scratch:SI 2 "=&r"))
   (clobber (match_scratch:HI 3 "=&YL"))]
  "TARGET_KNC && !misaligned_operand (operands[0], <MODE>mode)"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  emit_move_insn (operands[2], GEN_INT (<MODE>mode == V16QImode ? 3 : 15));
  emit_insn (gen_knc_mask (operands[3], gen_lowpart (HImode, operands[2])));
  emit_insn (gen_knc_transport_maskstore<mode>
             (operands[0], operands[1], operands[3]));
  DONE;
}
  [(set_attr "type" "multi") (set_attr "mode" "XI")])

(define_insn "knc_transport_maskstore<mode>"
  [(set (match_operand:KNC_NARROW 0 "memory_operand" "=Bm")
        (unspec:KNC_NARROW
          [(match_operand:KNC_NARROW 1 "register_operand" "YJ")
           (match_operand:HI 2 "register_operand" "YL")]
          UNSPEC_KNC_TRANSFER))]
  "TARGET_KNC && !misaligned_operand (operands[0], <MODE>mode)"
  "vpackstorelq\t{%g1, %0%{%2%}|%0%{%2%}, %g1}"
  [(set_attr "type" "ssemov") (set_attr "mode" "XI")])

;; Intel 327364-001 pp.32-34, 379-384: converted memory accesses have
;; natural alignment and the exact source/destination mode's footprint.
;; A non-folded register source is reloaded through an explicit stack slot.
(define_insn "*knc_<code><mode>v16si2"
  [(set (match_operand:V16SI 0 "register_operand" "=YJ")
        (any_extend:V16SI
          (match_operand:KNC_NARROW 1 "nonimmediate_operand" "Bm")))]
  "TARGET_KNC && !misaligned_operand (operands[1], <MODE>mode)"
  "vmovdqa32\t{%1%{<knc_extend_kind>int<knc_narrow_bits>%}, %g0|%g0, %1%{<knc_extend_kind>int<knc_narrow_bits>%}}"
  [(set_attr "type" "ssecvt") (set_attr "mode" "XI")])

(define_insn "*knc_float_<code><mode>v16sf2"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (float:V16SF
          (any_extend:V16SI
            (match_operand:KNC_NARROW 1 "memory_operand" "Bm"))))]
  "TARGET_KNC && !misaligned_operand (operands[1], <MODE>mode)"
  "vmovaps\t{%1%{<knc_extend_kind>int<knc_narrow_bits>%}, %g0|%g0, %1%{<knc_extend_kind>int<knc_narrow_bits>%}}"
  [(set_attr "type" "ssecvt") (set_attr "mode" "XI")])

(define_insn_and_split "*knc_truncv16si<mode>2_store"
  [(set (match_operand:KNC_NARROW 0 "nonimmediate_operand" "=Bm,YJ")
        (truncate:KNC_NARROW
          (match_operand:V16SI 1 "register_operand" "YJ,YJ")))]
  "TARGET_KNC && !misaligned_operand (operands[0], <MODE>mode)"
  "#"
  "&& can_create_pseudo_p ()"
  [(const_int 0)]
{
  ix86_knc_expand_narrow_store (operands[0], operands[1]);
  DONE;
}
  [(set_attr "type" "multi") (set_attr "mode" "XI")])

(define_insn "*knc_ustruncv16si<mode>2_store"
  [(set (match_operand:KNC_NARROW 0 "memory_operand" "=Bm")
        (us_truncate:KNC_NARROW
          (match_operand:V16SI 1 "register_operand" "YJ")))]
  "TARGET_KNC && !misaligned_operand (operands[0], <MODE>mode)"
  "vmovdqa32\t{%g1%{uint<knc_narrow_bits>%}, %0|%0, %g1%{uint<knc_narrow_bits>%}}"
  [(set_attr "type" "ssecvt") (set_attr "mode" "XI")])

(define_mode_iterator KNC_SCALAR [SF DF SI DI TI TF])
(define_mode_iterator KNC_BROADCAST [SF DF SI DI])
(define_mode_iterator KNC_PAIR [TI TF])
(define_mode_attr knc_load [(SF "ss") (DF "sd") (SI "ss") (DI "sd")])
(define_mode_attr knc_store
  [(SF "ps") (DF "pd") (SI "ps") (DI "pd") (TI "q") (TF "pd")])

;; LRA supplies operand 2, leaving one GPR sufficient even when the other
;; fourteen are live.  The HI mask is a separate allocated pseudo, never k2.
;; Unlike the donor's memory-output secondary reload, both operands here
;; are registers: LRA retains any address setup on the ordinary moves.
(define_expand "knc_reload_out<mode>"
  [(parallel [(match_operand:KNC_SCALAR 0 "register_operand" "")
              (match_operand:KNC_SCALAR 1 "register_operand" "")
              (match_operand:SI 2 "register_operand" "=&r")])]
  "TARGET_KNC"
{
  rtx mem = assign_stack_temp (<MODE>mode, GET_MODE_SIZE (<MODE>mode));
  rtx mask = gen_reg_rtx (HImode);
  emit_move_insn (operands[2], GEN_INT (GET_MODE_SIZE (<MODE>mode) > 8 ? 3 : 1));
  emit_insn (gen_knc_mask (mask, gen_lowpart (HImode, operands[2])));
  emit_insn (gen_knc_store<mode> (mem, operands[1], mask));
  emit_move_insn (operands[0], mem);
  DONE;
})

(define_expand "knc_reload_in<mode>"
  [(parallel [(match_operand:KNC_SCALAR 0 "register_operand" "")
              (match_operand:KNC_SCALAR 1 "register_operand" "")
              (match_operand:SI 2 "register_operand" "=&r")])]
  "TARGET_KNC"
{
  rtx mem = assign_stack_temp (<MODE>mode, GET_MODE_SIZE (<MODE>mode));
  emit_move_insn (mem, operands[1]);
  if (<MODE>mode == TImode || <MODE>mode == TFmode)
    {
      rtx mask = gen_reg_rtx (HImode);
      emit_move_insn (operands[2], GEN_INT (3));
      emit_insn (gen_knc_mask (mask, gen_lowpart (HImode, operands[2])));
      if (<MODE>mode == TImode)
        emit_insn (gen_knc_load_pairti (operands[0], mem, mask));
      else
        emit_insn (gen_knc_load_pairtf (operands[0], mem, mask));
    }
  else if (<MODE>mode == SFmode)
    emit_insn (gen_knc_loadsf (operands[0], mem));
  else if (<MODE>mode == DFmode)
    emit_insn (gen_knc_loaddf (operands[0], mem));
  else if (<MODE>mode == SImode)
    emit_insn (gen_knc_loadsi (operands[0], mem));
  else
    emit_insn (gen_knc_loaddi (operands[0], mem));
  DONE;
})

;; Both the GPR materialization and mask definition have ordinary dataflow.
(define_insn "knc_mask"
  [(set (match_operand:HI 0 "register_operand" "=YK")
        (unspec:HI [(match_operand:HI 1 "register_operand" "r")]
                   UNSPEC_KNC_MASK))]
  "TARGET_KNC"
  "kmov\t%k1, %0"
  [(set_attr "type" "mskmov")])

;; A complete HI move must recognize LRA's pseudos before allocation.
;; Prefer GPRs for ordinary shorts; register forms follow donor *movhi_internal.
;; K1OM GAS rejects kmov memory operands, so spill masks through a GPR.
(define_insn "*knc_movhi"
  [(set (match_operand:HI 0 "nonimmediate_operand"
          "=r,r,r,m,m,?*YK,?r,?*YK")
        (match_operand:HI 1 "general_operand"
          "r,m,n,r,n,r,*YK,*YK"))]
  "TARGET_KNC && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "@
   mov{w}\t{%1, %0|%0, %1}
   mov{w}\t{%1, %0|%0, %1}
   mov{w}\t{%1, %0|%0, %1}
   mov{w}\t{%1, %0|%0, %1}
   mov{w}\t{%1, %0|%0, %1}
   kmov\t{%k1, %0|%0, %k1}
   kmov\t{%1, %k0|%k0, %1}
   kmov\t{%1, %0|%0, %1}"
  [(set_attr "type" "imov,imov,imov,imov,imov,mskmov,mskmov,mskmov")
   (set_attr "mode" "HI")])

;; ADR-0007 point 1: 8-lane masks are QI.  Like *knc_movhi, a complete QI
;; move ahead of *movqi_internal, whose k alternatives are AVX-512 kmovw or
;; kmovb.  GPR destinations are written whole (movl, movzbl), so byte moves
;; keep the code *movqi_internal gives under KNC's partial-register tuning.
;; q, not r, keeps -m32 stores on byte registers.  A k register reaches
;; memory through a GPR (ix86_secondary_reload supplies Q_REGS).  kmov r32,k
;; also copies bits 15:8 of the mask; a QI value never reads them.
(define_insn "*knc_movqi"
  [(set (match_operand:QI 0 "nonimmediate_operand"
          "=q,q,q,m,m,?*YK,?r,?*YK")
        (match_operand:QI 1 "general_operand"
          "q,m,n,q,n,r,*YK,*YK"))]
  "TARGET_KNC && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "@
   mov{l}\t{%k1, %k0|%k0, %k1}
   movz{bl|x}\t{%1, %k0|%k0, %1}
   mov{l}\t{%k1, %k0|%k0, %k1}
   mov{b}\t{%1, %0|%0, %1}
   mov{b}\t{%1, %0|%0, %1}
   kmov\t{%k1, %0|%0, %k1}
   kmov\t{%1, %k0|%k0, %1}
   kmov\t{%1, %0|%0, %1}"
  [(set_attr "type" "imov,imovx,imov,imov,imov,mskmov,mskmov,mskmov")
   (set_attr "mode" "SI,SI,SI,QI,QI,QI,QI,QI")])

(define_insn "knc_store<mode>"
  [(set (match_operand:KNC_SCALAR 0 "memory_operand" "=m")
        (unspec:KNC_SCALAR
          [(match_operand:KNC_SCALAR 1 "register_operand" "YJ")
           (match_operand:HI 2 "register_operand" "YL")]
          UNSPEC_KNC_TRANSFER))]
  "TARGET_KNC"
  "vpackstorel<knc_store>\t%g1, %0%{%2%}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "<MODE>")])

;; SysV varargs slots are 16-byte aligned, so mask 3 saves both quadwords
;; directly, as in donor setup_incoming_varargs_64 / knc.md (ADR-0004).
(define_expand "knc_save_varargs"
  [(set (match_operand:TI 0 "memory_operand")
        (match_operand:TI 1 "register_operand"))]
  "TARGET_KNC"
{
  rtx gpr = gen_reg_rtx (SImode);
  rtx mask = gen_reg_rtx (HImode);
  emit_move_insn (gpr, GEN_INT (3));
  emit_insn (gen_knc_mask (mask, gen_lowpart (HImode, gpr)));
  emit_insn (gen_knc_storeti (operands[0], operands[1], mask));
  DONE;
})

(define_insn "knc_load<mode>"
  [(set (match_operand:KNC_BROADCAST 0 "register_operand" "=YJ")
        (unspec:KNC_BROADCAST
          [(match_operand:KNC_BROADCAST 1 "memory_operand" "m")]
          UNSPEC_KNC_TRANSFER))]
  "TARGET_KNC"
  "vbroadcast<knc_load>\t%1, %g0"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "<MODE>")])

(define_insn "knc_load_pair<mode>"
  [(set (match_operand:KNC_PAIR 0 "register_operand" "=YJ")
        (unspec:KNC_PAIR
          [(match_operand:KNC_PAIR 1 "memory_operand" "m")
           (match_operand:HI 2 "register_operand" "YL")]
          UNSPEC_KNC_TRANSFER))]
  "TARGET_KNC"
  "vloadunpacklpd\t%1, %g0%{%2%}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "<MODE>")])

(define_insn "*knc_copy<mode>"
  [(set (match_operand:KNC_SCALAR 0 "register_operand" "=YJ")
        (match_operand:KNC_SCALAR 1 "register_operand" "YJ"))]
  "TARGET_KNC && SSE_REG_P (operands[0]) && SSE_REG_P (operands[1])"
  "vmovaps\t%g1, %g0"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "<MODE>")])

(define_expand "knc_move<mode>"
  [(set (match_operand:KNC_SCALAR 0 "nonimmediate_operand")
        (match_operand:KNC_SCALAR 1 "general_operand"))]
  "TARGET_KNC"
{
  rtx dst = operands[0], src = operands[1];
  if (SSE_REG_P (dst) && SSE_REG_P (src))
    emit_insn (gen_rtx_SET (dst, src));
  else
    {
      /* The bridge has the mode's natural alignment, so a one/two-lane
         pack or unpack cannot straddle a 64-byte boundary.  */
      rtx mem = assign_stack_temp (<MODE>mode, GET_MODE_SIZE (<MODE>mode));
      rtx gpr = gen_reg_rtx (SImode);
      rtx mask = gen_reg_rtx (HImode);
      emit_move_insn (gpr, GEN_INT (GET_MODE_SIZE (<MODE>mode) > 8 ? 3 : 1));
      emit_insn (gen_knc_mask (mask, gen_lowpart (HImode, gpr)));
      if (SSE_REG_P (src))
        {
          emit_insn (gen_knc_store<mode> (mem, src, mask));
          emit_move_insn (dst, mem);
        }
      else
        {
          emit_move_insn (mem, src);
          if (<MODE>mode == TImode)
            emit_insn (gen_knc_load_pairti (dst, mem, mask));
          else if (<MODE>mode == TFmode)
            emit_insn (gen_knc_load_pairtf (dst, mem, mask));
          else if (<MODE>mode == SFmode)
            emit_insn (gen_knc_loadsf (dst, mem));
          else if (<MODE>mode == DFmode)
            emit_insn (gen_knc_loaddf (dst, mem));
          else if (<MODE>mode == SImode)
            emit_insn (gen_knc_loadsi (dst, mem));
          else
            emit_insn (gen_knc_loaddi (dst, mem));
        }
    }
  DONE;
})

;; XI is storage for a complete register; it advertises no packed arithmetic.
(define_insn "knc_movxi"
  [(set (match_operand:XI 0 "nonimmediate_operand" "=YJ,YJ,m")
        (match_operand:XI 1 "nonimmediate_operand" "YJ,m,YJ"))]
  "TARGET_KNC && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "vmovaps\t%g1, %g0"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "XI")])
(define_insn "knc_pause"
  [(set (match_operand:BLK 0)
        (unspec:BLK [(match_dup 0)
                     (match_operand:SI 1 "register_operand" "r")]
                    UNSPEC_PAUSE))]
  "TARGET_KNC"
  "delay\t%1"
  [(set_attr "type" "other")
   (set_attr "prefix" "vex")
   (set_attr "memory" "unknown")])

;; Intel 327364-001 Appendix A, DELAY (p. 628): register cycle-count
;; operand, no flags modified.  The common pause expander supplies 32.
;; The donor i386.md still emits rep;nop, so use the ISA definition.

;; Legacy __sync lock operations need these libcalls too.  Model tags
;; have GCC-internal SYNC bits which are not part of the libatomic ABI.
(define_expand "atomic_exchangeti"
  [(match_operand:TI 0 "register_operand")
   (match_operand:TI 1 "memory_operand")
   (match_operand:TI 2 "register_operand")
   (match_operand:SI 3 "const_int_operand")]
  "TARGET_KNC"
{
  ix86_knc_expand_atomic_exchange (operands);
  DONE;
})

(define_expand "atomic_storeti"
  [(match_operand:TI 0 "memory_operand")
   (match_operand:TI 1 "nonmemory_operand")
   (match_operand:SI 2 "const_int_operand")]
  "TARGET_KNC"
{
  ix86_knc_expand_atomic_store (operands);
  DONE;
})

;; Packed storage (ADR-0005 points 1, 3, 4 and 7).
(define_mode_iterator KNC_PACKED [V16SF V8DF V16SI V8DI])
(define_mode_iterator KNC_FP [V16SF V8DF])
(define_mode_attr knc_fp_suffix [(V16SF "ps") (V8DF "pd")])
(define_mode_attr knc_fp_broadcast [(V16SF "ss") (V8DF "sd")])
(define_mode_attr knc_fp_scalar [(V16SF "SF") (V8DF "DF")])
(define_mode_attr knc_packed_move
  [(V16SF "vmovaps") (V8DF "vmovapd")
   (V16SI "vmovdqa32") (V8DI "vmovdqa64")])

;; Bm, not m: the insn condition is not rechecked when LRA substitutes a
;; memory equivalence, so the constraint itself must refuse an address that
;; is not known 64-byte aligned (S4-09).  LRA then reloads through YJ.
(define_insn "*knc_packed_move<mode>"
  [(set (match_operand:KNC_PACKED 0 "nonimmediate_operand" "=YJ,YJ,Bm,YJ")
        (match_operand:KNC_PACKED 1 "nonimmediate_or_sse_const_operand"
                                  "YJ,Bm,YJ,C"))]
  "TARGET_KNC && (register_operand (operands[0], <MODE>mode)
                  || register_operand (operands[1], <MODE>mode))
   && !misaligned_operand (operands[0], <MODE>mode)
   && !misaligned_operand (operands[1], <MODE>mode)"
{
  if (which_alternative == 3)
    return "vpxord\t%g0, %g0, %g0";
  if (MEM_P (operands[1])
      && SYMBOL_REF_P (XEXP (operands[1], 0))
      && CONSTANT_POOL_ADDRESS_P (XEXP (operands[1], 0)))
    return "vmovaps\t%g1, %g0";
  return "<knc_packed_move>\t%g1, %g0";
}
  [(set_attr "type" "ssemov")
   (set_attr "mode" "XI")])

;; Intel 327364-001, VLOADUNPACK / VPACKSTORE, and S3-03's card probe.
;; Both instructions share one logical 64-byte access.  The high half at
;; A+64 is a no-op when A is aligned; no mask or extra vector is needed.
;; Derive the high address from the same MEM after allocation.
;; Mark the pair as multi and bound both instruction lengths, rather than
;; pretending this is a single aligned move.  Spills retain the insn above.
(define_mode_attr knc_packed_pair
  [(V16SF "ps") (V8DF "pd") (V16SI "d") (V8DI "q")])

(define_insn "*knc_packed_unaligned_load<mode>"
  [(set (match_operand:KNC_PACKED 0 "register_operand" "=YJ")
        (match_operand:KNC_PACKED 1 "memory_operand" "m"))]
  "TARGET_KNC && misaligned_operand (operands[1], <MODE>mode)
   && ix86_knc_element_aligned_mem_p (operands[1], <MODE>mode)"
{
  operands[2] = adjust_address (operands[1], <MODE>mode, 64);
  return "vloadunpackl<knc_packed_pair>\t%1, %g0\n\tvloadunpackh<knc_packed_pair>\t%2, %g0";
}
  [(set_attr "type" "multi")
   (set_attr "memory" "load")
   (set_attr "length" "24")
   (set_attr "mode" "XI")])

(define_insn "*knc_packed_unaligned_store<mode>"
  [(set (match_operand:KNC_PACKED 0 "memory_operand" "=m")
        (match_operand:KNC_PACKED 1 "register_operand" "YJ"))]
  "TARGET_KNC && misaligned_operand (operands[0], <MODE>mode)
   && ix86_knc_element_aligned_mem_p (operands[0], <MODE>mode)"
{
  operands[2] = adjust_address (operands[0], <MODE>mode, 64);
  return "vpackstorel<knc_packed_pair>\t%g1, %0\n\tvpackstoreh<knc_packed_pair>\t%g1, %2";
}
  [(set_attr "type" "multi")
   (set_attr "memory" "store")
   (set_attr "length" "24")
   (set_attr "mode" "XI")])

;; Intel 327364-001, VADD/VSUB/VMUL: only an aligned full-width memory
;; operand may be folded.  vector_operand enforces that without AVX flags
;; at recog; Bm (vector_memory_operand) enforces it when LRA substitutes a
;; memory equivalence, which plain m would accept misaligned (S4-09).
(define_code_iterator KNC_FP_BINARY [plus minus mult])
(define_code_attr knc_fp_binary [(plus "add") (minus "sub") (mult "mul")])
(define_code_attr knc_fp_type [(plus "sseadd") (minus "sseadd") (mult "ssemul")])
(define_insn "*knc_<knc_fp_binary><mode>3"
  [(set (match_operand:KNC_FP 0 "register_operand" "=YJ")
        (KNC_FP_BINARY:KNC_FP
          (match_operand:KNC_FP 1 "register_operand" "YJ")
          (match_operand:KNC_FP 2 "vector_operand" "YJBm")))]
  "TARGET_KNC"
  "v<knc_fp_binary><knc_fp_suffix>\t%g2, %g1, %g0"
  [(set_attr "type" "<knc_fp_type>")
   (set_attr "mode" "<MODE>")])

;; Intel 327364-001 VPADDD, VPSUBD, VPMULLD and VPAND/VPOR/VPXOR{D,Q}.
;; k1om has no vpaddq, vpsubq or vpmullq, so V8DI is logic only (ADR-0005
;; point 3).  The memory source is aligned and full-width, as above (S4-09).
(define_code_iterator KNC_INT_BINARY [plus minus mult])
(define_code_attr knc_int_binary [(plus "add") (minus "sub") (mult "mull")])
(define_code_attr knc_int_type
  [(plus "sseiadd") (minus "sseiadd") (mult "sseimul")])
(define_code_attr knc_int_comm [(plus "%") (minus "") (mult "%")])
(define_insn "*knc_<code>v16si3"
  [(set (match_operand:V16SI 0 "register_operand" "=YJ")
        (KNC_INT_BINARY:V16SI
          (match_operand:V16SI 1 "register_operand" "<knc_int_comm>YJ")
          (match_operand:V16SI 2 "vector_operand" "YJBm")))]
  "TARGET_KNC"
  "vp<knc_int_binary>d\t%g2, %g1, %g0"
  [(set_attr "type" "<knc_int_type>")
   (set_attr "mode" "XI")])

(define_mode_iterator KNC_INT [V16SI V8DI])
(define_mode_attr knc_int_suffix [(V16SI "d") (V8DI "q")])
(define_insn "*knc_<code><mode>3"
  [(set (match_operand:KNC_INT 0 "register_operand" "=YJ")
        (any_logic:KNC_INT
          (match_operand:KNC_INT 1 "register_operand" "%YJ")
          (match_operand:KNC_INT 2 "vector_operand" "YJBm")))]
  "TARGET_KNC"
  "vp<logic><knc_int_suffix>\t%g2, %g1, %g0"
  [(set_attr "type" "sselog")
   (set_attr "mode" "XI")])

;; All four FMAMODEM expanders become available together.  These register
;; forms implement each sign combination without any separate negation.
;; The tied addend models the destructive 231 encoding for the allocator.
(define_insn "*knc_fma<mode>4"
  [(set (match_operand:KNC_FP 0 "register_operand" "=YJ")
        (fma:KNC_FP (match_operand:KNC_FP 1 "nonimmediate_operand" "YJ")
                    (match_operand:KNC_FP 2 "nonimmediate_operand" "YJ")
                    (match_operand:KNC_FP 3 "nonimmediate_operand" "0")))]
  "TARGET_KNC"
  "vfmadd231<knc_fp_suffix>\t%g2, %g1, %g0"
  [(set_attr "type" "ssemuladd") (set_attr "mode" "<MODE>")])

(define_insn "*knc_fms<mode>4"
  [(set (match_operand:KNC_FP 0 "register_operand" "=YJ")
        (fma:KNC_FP (match_operand:KNC_FP 1 "nonimmediate_operand" "YJ")
                    (match_operand:KNC_FP 2 "nonimmediate_operand" "YJ")
                    (neg:KNC_FP (match_operand:KNC_FP 3 "nonimmediate_operand" "0"))))]
  "TARGET_KNC"
  "vfmsub231<knc_fp_suffix>\t%g2, %g1, %g0"
  [(set_attr "type" "ssemuladd") (set_attr "mode" "<MODE>")])

(define_insn "*knc_fnma<mode>4"
  [(set (match_operand:KNC_FP 0 "register_operand" "=YJ")
        (fma:KNC_FP (neg:KNC_FP (match_operand:KNC_FP 1 "nonimmediate_operand" "YJ"))
                    (match_operand:KNC_FP 2 "nonimmediate_operand" "YJ")
                    (match_operand:KNC_FP 3 "nonimmediate_operand" "0")))]
  "TARGET_KNC"
  "vfnmadd231<knc_fp_suffix>\t%g2, %g1, %g0"
  [(set_attr "type" "ssemuladd") (set_attr "mode" "<MODE>")])

(define_insn "*knc_fnms<mode>4"
  [(set (match_operand:KNC_FP 0 "register_operand" "=YJ")
        (fma:KNC_FP (neg:KNC_FP (match_operand:KNC_FP 1 "nonimmediate_operand" "YJ"))
                    (match_operand:KNC_FP 2 "nonimmediate_operand" "YJ")
                    (neg:KNC_FP (match_operand:KNC_FP 3 "nonimmediate_operand" "0"))))]
  "TARGET_KNC"
  "vfnmsub231<knc_fp_suffix>\t%g2, %g1, %g0"
  [(set_attr "type" "ssemuladd") (set_attr "mode" "<MODE>")])

(define_expand "vec_duplicate<mode>"
  [(set (match_operand:KNC_FP 0 "register_operand")
        (vec_duplicate:KNC_FP (match_operand:<knc_fp_scalar> 1 "nonimmediate_operand")))]
  "TARGET_KNC"
{
  ix86_knc_expand_duplicate (operands[0], operands[1]);
  DONE;
})

;; Integer broadcasts reach this through vec_init (ix86_knc_expand_duplicate);
;; sse.md's vec_duplicate for V16SI and V8DI stays off.  The scalar source
;; must be element-aligned, like every IMCI memory operand.
(define_mode_attr knc_packed_scalar
  [(V16SF "SF") (V8DF "DF") (V16SI "SI") (V8DI "DI")])
(define_mode_attr knc_packed_broadcast
  [(V16SF "vbroadcastss") (V8DF "vbroadcastsd")
   (V16SI "vpbroadcastd") (V8DI "vpbroadcastq")])
(define_mode_attr knc_packed_insn_mode
  [(V16SF "V16SF") (V8DF "V8DF") (V16SI "XI") (V8DI "XI")])
(define_insn "*knc_broadcast_memory<mode>"
  [(set (match_operand:KNC_PACKED 0 "register_operand" "=YJ")
        (vec_duplicate:KNC_PACKED
          (match_operand:<knc_packed_scalar> 1 "memory_operand" "m")))]
  "TARGET_KNC && ix86_knc_element_aligned_mem_p (operands[1], <MODE>mode)"
  "<knc_packed_broadcast>\t%1, %g0"
  [(set_attr "type" "ssemov") (set_attr "mode" "<knc_packed_insn_mode>")])

;; The block result has four copies of the scalar's low 128-bit block.
;; Upper scalar lanes are unspecified; the following swizzle selects only
;; copies of lane zero.  Keep both instructions independently allocated.
(define_insn "knc_broadcast_block<mode>"
  [(set (match_operand:KNC_FP 0 "register_operand" "=YJ")
        (unspec:KNC_FP [(match_operand:<knc_fp_scalar> 1 "register_operand" "YJ")]
                       UNSPEC_KNC_BROADCAST_BLOCK))]
  "TARGET_KNC"
  "vpermf32x4\t$0, %g1, %g0"
  [(set_attr "type" "sselog1") (set_attr "mode" "<MODE>")])

;; Explicit selects describe the real swizzle groups, not a fictitious
;; full-vector register broadcast.  Intel 327364-001 tables 2.2 and 2.3.
(define_insn "knc_broadcast_swizzlev16sf"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (vec_select:V16SF (match_operand:V16SF 1 "register_operand" "YJ")
          (parallel [(const_int 0) (const_int 0) (const_int 0) (const_int 0)
                     (const_int 4) (const_int 4) (const_int 4) (const_int 4)
                     (const_int 8) (const_int 8) (const_int 8) (const_int 8)
                     (const_int 12) (const_int 12) (const_int 12) (const_int 12)])))]
  "TARGET_KNC"
  "vmovaps\t%g1%{aaaa%}, %g0"
  [(set_attr "type" "sselog1") (set_attr "mode" "V16SF")])

(define_insn "knc_broadcast_swizzlev8df"
  [(set (match_operand:V8DF 0 "register_operand" "=YJ")
        (vec_select:V8DF (match_operand:V8DF 1 "register_operand" "YJ")
          (parallel [(const_int 0) (const_int 0) (const_int 0) (const_int 0)
                     (const_int 4) (const_int 4) (const_int 4) (const_int 4)])))]
  "TARGET_KNC"
  "vmovapd\t%g1%{aaaa%}, %g0"
  [(set_attr "type" "sselog1") (set_attr "mode" "V8DF")])

;; Compares into k masks (ADR-0007 points 1, 2, 3 and 5).
;; The shared vec_cmp expander passes its comparison through this predicate.
;; Under KNC it refuses the quiet UNLT, UNLE, UNGT, UNGE, UNEQ and LTGT:
;; they need a guarded compare that the card has not cleared yet (point 5),
;; so can_vec_cmp_compare_p reports no support and those loops stay scalar.
;; With -fno-trapping-math no flag is observable, so UNLT..UNGE are the
;; signaling nle/nlt.  match.pd folds ~(a < b) into a u>= b there without
;; asking the target (S4-03: cond_add exposed it as an ICE in expand).
;; Any other target keeps the expander's former "" (any comparison).
(define_special_predicate "knc_vec_cmp_operator"
  (ior (not (match_test "TARGET_KNC"))
       (match_code "eq,ne,lt,le,gt,ge,ltu,leu,gtu,geu,unordered,ordered")
       (and (match_code "unlt,unle,ungt,unge")
	    (match_test "!flag_trapping_math"))))

;; k1om has no vpcmpq, so V8DI compares stay off (point 6).
(define_mode_iterator KNC_CMP [V16SF V8DF V16SI])
;; sse.md's avx512fmaskmode, which is defined after this file is read.
(define_mode_attr knc_maskmode [(V16SF "HI") (V16SI "HI") (V8DF "QI") (V8DI "QI")])
(define_mode_attr knc_cmp [(V16SF "vcmpps") (V8DF "vcmppd") (V16SI "vpcmpd")])

;; Intel 327364-001 VCMPPS/VCMPPD and VPCMPD/VPCMPUD.  Operand 3 is KNC's
;; 3-bit predicate (ix86_knc_expand_mask_vec_cmp), not AVX-512's 5-bit one.
;; The 8-lane forms write zero to bits 15:8 of the mask.
;; Unsigned compares use UNSPEC_KNC_UCMP: sse.md's UNSPEC_UNSIGNED_PCMP is
;; declared after this file is read.
(define_c_enum "unspec" [UNSPEC_KNC_UCMP])
(define_insn "*knc_cmp<mode>"
  [(set (match_operand:<knc_maskmode> 0 "register_operand" "=YK")
	(unspec:<knc_maskmode>
	  [(match_operand:KNC_CMP 1 "register_operand" "YJ")
	   (match_operand:KNC_CMP 2 "nonimmediate_operand" "YJBm")
	   (match_operand:SI 3 "const_0_to_7_operand")]
	  UNSPEC_PCMP))]
  "TARGET_KNC"
  "<knc_cmp>\t{%3, %g2, %g1, %0|%0, %g1, %g2, %3}"
  [(set_attr "type" "ssecmp")
   (set_attr "length_immediate" "1")
   (set_attr "mode" "XI")])

(define_insn "*knc_ucmpv16si"
  [(set (match_operand:HI 0 "register_operand" "=YK")
	(unspec:HI
	  [(match_operand:V16SI 1 "register_operand" "YJ")
	   (match_operand:V16SI 2 "nonimmediate_operand" "YJBm")
	   (match_operand:SI 3 "const_0_to_7_operand")]
	  UNSPEC_KNC_UCMP))]
  "TARGET_KNC"
  "vpcmpud\t{%3, %g2, %g1, %0|%0, %g1, %g2, %3}"
  [(set_attr "type" "ssecmp")
   (set_attr "length_immediate" "1")
   (set_attr "mode" "XI")])

;; Intel 327364-001 VBLENDMPS/VBLENDMPD/VPBLENDMD/VPBLENDMQ: the destination
;; takes operand 1 where the mask is set and operand 2 elsewhere, which is
;; vcond_mask's (vec_merge op1 op2 mask).  The mask is k1-k7 (k0 means no
;; mask).  The shared vcond_mask expander puts a zero else-value in a
;; register first, so its vpxord is a visible, hoistable insn (point 3).
(define_mode_attr knc_blend
  [(V16SF "vblendmps") (V8DF "vblendmpd")
   (V16SI "vpblendmd") (V8DI "vpblendmq")])
(define_insn "*knc_blend<mode>"
  [(set (match_operand:KNC_PACKED 0 "register_operand" "=YJ")
	(vec_merge:KNC_PACKED
	  (match_operand:KNC_PACKED 1 "nonimmediate_operand" "YJBm")
	  (match_operand:KNC_PACKED 2 "register_operand" "YJ")
	  (match_operand:<knc_maskmode> 3 "register_operand" "YL")))]
  "TARGET_KNC"
  "<knc_blend>\t{%g1, %g2, %g0%{%3%}|%g0%{%3%}, %g2, %g1}"
  [(set_attr "type" "ssemov")
   (set_attr "mode" "XI")])

;; Masked moves (ADR-0007 point 7, Intel 327364-001 VMOVAPS/VMOVAPD/
;; VMOVDQA32/VMOVDQA64 pp.376-387).  A masked aligned move names exactly
;; one 64-byte line, so it obeys the line rule when that line holds an
;; active element: the access is 64-byte aligned and the mask is not empty.
;; Masks never suppress a fault (S3-03), so every use sits behind a guard.
;; sse.md's UNSPEC_MASKMOV and UNSPEC_MASKLOAD are declared after this file.
(define_c_enum "unspec" [UNSPEC_KNC_MASKMOV UNSPEC_KNC_MASKLOAD UNSPEC_KNC_KTEST])

(define_insn "knc_maskstore_aligned<mode>"
  [(set (match_operand:KNC_PACKED 0 "vector_memory_operand" "=Bm")
	(unspec:KNC_PACKED
	  [(match_operand:KNC_PACKED 1 "register_operand" "YJ")
	   (match_dup 0)
	   (match_operand:<knc_maskmode> 2 "register_operand" "YL")]
	  UNSPEC_KNC_MASKMOV))]
  "TARGET_KNC"
  "<knc_packed_move>\t{%g1, %0%{%2%}|%0%{%2%}, %g1}"
  [(set_attr "type" "ssemov")
   (set_attr "memory" "store")
   (set_attr "mode" "XI")])

;; Merge-only masking (no {z}): operand 2 is the else value, tied.
(define_insn "knc_maskload_aligned<mode>"
  [(set (match_operand:KNC_PACKED 0 "register_operand" "=YJ")
	(vec_merge:KNC_PACKED
	  (unspec:KNC_PACKED
	    [(match_operand:KNC_PACKED 1 "vector_memory_operand" "Bm")]
	    UNSPEC_KNC_MASKLOAD)
	  (match_operand:KNC_PACKED 2 "register_operand" "0")
	  (match_operand:<knc_maskmode> 3 "register_operand" "YL")))]
  "TARGET_KNC"
  "<knc_packed_move>\t{%1, %g0%{%3%}|%g0%{%3%}, %1}"
  [(set_attr "type" "ssemov")
   (set_attr "memory" "load")
   (set_attr "mode" "XI")])

;; Intel 327364-001 JKZD/JKNZD pp.73-78: branch on all 16 mask bits.  The
;; unspec keeps an ordinary HImode test of a short from matching (it would
;; force the short into a k register).  Inversion swaps eq and ne.
(define_code_iterator KNC_JK [eq ne])
(define_code_attr knc_jk [(eq "jkzd") (ne "jknzd")])
(define_insn "knc_jk<code>"
  [(set (pc)
	(if_then_else
	  (KNC_JK (unspec:HI [(match_operand:HI 0 "register_operand" "YK")]
			     UNSPEC_KNC_KTEST)
		  (const_int 0))
	  (label_ref (match_operand 1))
	  (pc)))]
  "TARGET_KNC"
  "<knc_jk>\t{%l1, %0|%0, %l1}"
  [(set_attr "type" "ibr")
   (set_attr "memory" "none")
   (set_attr "length" "7")])

;; ADR-0008 point 6; Intel 327364-001 pp.300-302, 484-486, 530-532,
;; 598-600.  Each execution can complete only a subset of the lanes.
;; Keep the retry loop in one insn, with a consumed non-k0 mask and a
;; destination distinct from the index.  BLK memory with an opaque VSIB
;; address describes the whole indexed footprint, not one scalar access.
;; The card probe confirmed signed address-width scaling, masked fault
;; suppression, restartability and highest-active-lane-wins scatter order.
(define_mode_iterator KNC_GATHER16 [V16SF V16SI])
(define_mode_iterator KNC_GATHER [V16SF V16SI V8DF V8DI])
(define_mode_attr knc_gather_op
  [(V16SF "vgatherdps") (V16SI "vpgatherdd")
   (V8DF "vgatherdpd") (V8DI "vpgatherdq")])
(define_mode_attr knc_scatter_op
  [(V16SF "vscatterdps") (V16SI "vpscatterdd")
   (V8DF "vscatterdpd") (V8DI "vpscatterdq")])
(define_c_enum "unspec"
  [UNSPEC_KNC_GATHER UNSPEC_KNC_SCATTER UNSPEC_KNC_VSIBADDR])

(define_insn "knc_gather<mode>"
  [(set (match_operand:KNC_GATHER 0 "register_operand" "=&YJ")
        (unspec:KNC_GATHER
          [(match_operand:KNC_GATHER 1 "register_operand" "0")
           (match_operand:HI 7 "register_operand" "5")
           (match_operator:BLK 6 "vsib_mem_operator"
             [(unspec:DI
                [(match_operand:DI 2 "register_operand" "r")
                 (match_operand:V16SI 3 "register_operand" "YJ")
                 (match_operand:SI 4 "const1248_operand")]
                UNSPEC_KNC_VSIBADDR)])]
          UNSPEC_KNC_GATHER))
   (clobber (match_scratch:HI 5 "=&YL"))]
  "TARGET_KNC"
  ".Lknc_gather%=:\n\t<knc_gather_op>\t{(%2,%g3,%c4), %g0%{%5%}|%g0%{%5%}, [%2+%g3*%c4]}\n\tjknzd\t{.Lknc_gather%=, %5|%5, .Lknc_gather%=}"
  [(set_attr "type" "multi")
   (set_attr "memory" "load")
   (set_attr "length" "20")
   (set_attr "mode" "XI")])

(define_insn "knc_scatter<mode>"
  [(set (match_operator:BLK 5 "vsib_mem_operator"
          [(unspec:DI
             [(match_operand:DI 0 "register_operand" "r")
              (match_operand:V16SI 1 "register_operand" "YJ")
              (match_operand:SI 2 "const1248_operand")
              (match_operand:HI 6 "register_operand" "4")]
             UNSPEC_KNC_VSIBADDR)])
        (unspec:BLK
          [(match_operand:KNC_GATHER 3 "register_operand" "YJ")
           (match_dup 6)]
          UNSPEC_KNC_SCATTER))
   (clobber (match_scratch:HI 4 "=&YL"))]
  "TARGET_KNC"
  ".Lknc_scatter%=:\n\t<knc_scatter_op>\t{%g3, (%0,%g1,%c2)%{%4%}|[%0+%g1*%c2]%{%4%}, %g3}\n\tjknzd\t{.Lknc_scatter%=, %4|%4, .Lknc_scatter%=}"
  [(set_attr "type" "multi")
   (set_attr "memory" "store")
   (set_attr "length" "20")
   (set_attr "mode" "XI")])

;; The optab sign and scale predicates are queried before expansion.
;; In particular, unsigned dwords cannot be represented by IMCI VSIB.
(define_expand "mask_gather_load<mode>v16si"
  [(match_operand:KNC_GATHER16 0 "register_operand")
   (match_operand:DI 1 "register_operand")
   (match_operand:V16SI 2 "register_operand")
   (match_operand:SI 3 "const0_operand")
   (match_operand:SI 4 "const1248_operand")
   (match_operand:HI 5 "register_operand")
   (match_operand:KNC_GATHER16 6 "nonmemory_operand")]
  "TARGET_KNC"
{
  rtx addr = gen_rtx_UNSPEC (DImode,
                            gen_rtvec (3, operands[1], operands[2], operands[4]),
                            UNSPEC_KNC_VSIBADDR);
  rtx mem = gen_rtx_MEM (BLKmode, addr);
  rtx otherwise = force_reg (<MODE>mode, operands[6]);
  emit_insn (gen_knc_gather<mode> (operands[0], otherwise, operands[1],
                                  operands[2], operands[4], NULL_RTX, mem,
                                  operands[5]));
  DONE;
})

(define_expand "gather_load<mode>v16si"
  [(match_operand:KNC_GATHER16 0 "register_operand")
   (match_operand:DI 1 "register_operand")
   (match_operand:V16SI 2 "register_operand")
   (match_operand:SI 3 "const0_operand")
   (match_operand:SI 4 "const1248_operand")]
  "TARGET_KNC"
{
  emit_insn (gen_mask_gather_load<mode>v16si
    (operands[0], operands[1], operands[2], operands[3], operands[4],
     force_reg (HImode, constm1_rtx), CONST0_RTX (<MODE>mode)));
  DONE;
})

(define_expand "mask_scatter_store<mode>v16si"
  [(match_operand:DI 0 "register_operand")
   (match_operand:V16SI 1 "register_operand")
   (match_operand:SI 2 "const0_operand")
   (match_operand:SI 3 "const1248_operand")
   (match_operand:KNC_GATHER16 4 "register_operand")
   (match_operand:HI 5 "register_operand")]
  "TARGET_KNC"
{
  rtx addr = gen_rtx_UNSPEC (DImode,
                            gen_rtvec (4, operands[0], operands[1],
                                       operands[3], operands[5]),
                            UNSPEC_KNC_VSIBADDR);
  rtx mem = gen_rtx_MEM (BLKmode, addr);
  emit_insn (gen_knc_scatter<mode> (operands[0], operands[1], operands[3],
                                   operands[4], NULL_RTX, mem, operands[5]));
  DONE;
})

(define_expand "scatter_store<mode>v16si"
  [(match_operand:DI 0 "register_operand")
   (match_operand:V16SI 1 "register_operand")
   (match_operand:SI 2 "const0_operand")
   (match_operand:SI 3 "const1248_operand")
   (match_operand:KNC_GATHER16 4 "register_operand")]
  "TARGET_KNC"
{
  emit_insn (gen_mask_scatter_store<mode>v16si
    (operands[0], operands[1], operands[2], operands[3], operands[4],
     force_reg (HImode, constm1_rtx)));
  DONE;
})

;; Intel 327364-001 VPERMD p.480: lane N takes dword (operand 2)[N] bits 3:0
;; of operand 1.  Only those four index bits are read, so a rotation index
;; needs no AND.  The 8-lane modes rotate by dword pairs.
(define_c_enum "unspec" [UNSPEC_KNC_PERMVAR])
(define_insn "@knc_permvar<mode>"
  [(set (match_operand:KNC_PACKED 0 "register_operand" "=YJ")
	(unspec:KNC_PACKED
	  [(match_operand:KNC_PACKED 1 "register_operand" "YJ")
	   (match_operand:V16SI 2 "register_operand" "YJ")]
	  UNSPEC_KNC_PERMVAR))]
  "TARGET_KNC"
  "vpermd\t{%g1, %g2, %g0|%g0, %g2, %g1}"
  [(set_attr "type" "sselog1")
   (set_attr "mode" "XI")])

;; Constant permutes (S5-06, ADR-0008 point 1), emitted only by
;; ix86_knc_vectorize_vec_perm_const.  Each immediate is computed on the
;; dword view of the selector, so one insn serves all four modes.
;; Lanes with a clear bit in operand 4 keep operand 3 ("retain their
;; previous values", VPERMD p.480); the 8-lane modes use this on their
;; V16SI lowpart, with a dword-pair mask.
(define_insn "knc_permvarv16si_mask"
  [(set (match_operand:V16SI 0 "register_operand" "=YJ")
	(vec_merge:V16SI
	  (unspec:V16SI
	    [(match_operand:V16SI 1 "register_operand" "YJ")
	     (match_operand:V16SI 2 "register_operand" "YJ")]
	    UNSPEC_KNC_PERMVAR)
	  (match_operand:V16SI 3 "register_operand" "0")
	  (match_operand:HI 4 "register_operand" "YL")))]
  "TARGET_KNC"
  "vpermd\t{%g1, %g2, %g0%{%4%}|%g0%{%4%}, %g2, %g1}"
  [(set_attr "type" "sselog1")
   (set_attr "mode" "XI")])

(define_c_enum "unspec"
  [UNSPEC_KNC_SWIZZLE UNSPEC_KNC_PERMF32X4 UNSPEC_KNC_PSHUFD
   UNSPEC_KNC_VALIGND])

;; A register swizzle on a plain move (Intel 327364-001 tables 2.2 and 2.3,
;; p.29-30).  Operand 2 is the swizzle field (1-7) of a 32-bit swizzle,
;; which repeats in every 128-bit block, plus 8 for a 64-bit swizzle,
;; which repeats in every 256-bit block.  The integer moves do no FP
;; arithmetic, and they carry no rounding override: EH=1 would reuse the
;; swizzle bits (p.28).
(define_insn "@knc_swizzle<mode>"
  [(set (match_operand:KNC_PACKED 0 "register_operand" "=YJ")
	(unspec:KNC_PACKED
	  [(match_operand:KNC_PACKED 1 "register_operand" "YJ")
	   (match_operand:SI 2 "const_0_to_15_operand")]
	  UNSPEC_KNC_SWIZZLE))]
  "TARGET_KNC && (INTVAL (operands[2]) & 7) != 0"
{
  static const char *const names[8]
    = { "dcba", "cdab", "badc", "dacb", "aaaa", "bbbb", "cccc", "dddd" };
  static char buf[80];
  const char *name = names[INTVAL (operands[2]) & 7];
  snprintf (buf, sizeof buf,
	    "vmovdqa%d\t{%%g1%%{%s%%}, %%g0|%%g0, %%g1%%{%s%%}}",
	    INTVAL (operands[2]) & 8 ? 64 : 32, name, name);
  return buf;
}
  [(set_attr "type" "sselog1")
   (set_attr "mode" "XI")])

;; VPERMF32X4 p.482: 128-bit block N of the result is block
;; (operand 2 >> 2N) & 3 of operand 1.
(define_insn "@knc_permf32x4<mode>"
  [(set (match_operand:KNC_PACKED 0 "register_operand" "=YJ")
	(unspec:KNC_PACKED
	  [(match_operand:KNC_PACKED 1 "register_operand" "YJ")
	   (match_operand:SI 2 "const_0_to_255_operand")]
	  UNSPEC_KNC_PERMF32X4))]
  "TARGET_KNC"
  "vpermf32x4\t{%2, %g1, %g0|%g0, %g1, %2}"
  [(set_attr "type" "sselog1")
   (set_attr "length_immediate" "1")
   (set_attr "mode" "XI")])

;; The narrowing instructions zero their entire high half, even masked.
;; Merge the second low half into the first using the S5-06 block permutation.
(define_mode_iterator KNC_CVT_PACK [V16SF V16SI])
(define_insn "@knc_cvt_merge<mode>"
  [(set (match_operand:KNC_CVT_PACK 0 "register_operand" "=YJ")
        (vec_merge:KNC_CVT_PACK
          (unspec:KNC_CVT_PACK
            [(match_operand:KNC_CVT_PACK 1 "register_operand" "YJ")
             (match_operand:SI 2 "const_0_to_255_operand")]
            UNSPEC_KNC_PERMF32X4)
          (match_operand:KNC_CVT_PACK 3 "register_operand" "0")
          (match_operand:HI 4 "register_operand" "YL")))]
  "TARGET_KNC"
  "vpermf32x4\t{%2, %g1, %g0%{%4%}|%g0%{%4%}, %g1, %2}"
  [(set_attr "type" "sselog1") (set_attr "mode" "XI")])

;; VPSHUFD p.536: dword J of every 128-bit block is dword
;; (operand 2 >> 2J) & 3 of the same block of operand 1.
(define_insn "@knc_pshufd<mode>"
  [(set (match_operand:KNC_PACKED 0 "register_operand" "=YJ")
	(unspec:KNC_PACKED
	  [(match_operand:KNC_PACKED 1 "register_operand" "YJ")
	   (match_operand:SI 2 "const_0_to_255_operand")]
	  UNSPEC_KNC_PSHUFD))]
  "TARGET_KNC"
  "vpshufd\t{%2, %g1, %g0|%g0, %g1, %2}"
  [(set_attr "type" "sselog1")
   (set_attr "length_immediate" "1")
   (set_attr "mode" "XI")])

;; VALIGND p.124: the result is dwords operand 3 to operand 3 + 15 of the
;; 32-dword concatenation with operand 2 low and operand 1 high.
(define_insn "@knc_valignd<mode>"
  [(set (match_operand:KNC_PACKED 0 "register_operand" "=YJ")
	(unspec:KNC_PACKED
	  [(match_operand:KNC_PACKED 1 "register_operand" "YJ")
	   (match_operand:KNC_PACKED 2 "register_operand" "YJ")
	   (match_operand:SI 3 "const_0_to_15_operand")]
	  UNSPEC_KNC_VALIGND))]
  "TARGET_KNC"
  "valignd\t{%3, %g2, %g1, %g0|%g0, %g1, %g2, %3}"
  [(set_attr "type" "sselog1")
   (set_attr "length_immediate" "1")
   (set_attr "mode" "XI")])

;; maskload (operand 1 is memory, else value zero) and maskstore (operand 0
;; is memory) for the shared sse.md expanders, which may not FAIL.  A
;; 64-byte aligned access is one masked move behind an empty-mask guard:
;; jkzd for HI, a byte test for QI, whose bits 15:8 are undefined (point 1).
;; An element-aligned address A is split over its lines L0 = A & -64 and
;; L0 + 64 (S4-14): with s = (A & 63) / element size and L lanes, lane J
;; lives in L0 lane J + s when J + s < L, else in the next line's lane
;; J + s - L.  The masks (k << s) & (2^L - 1) and k >> (L - s) are formed
;; in a GPR from the zero-extended mask (so a QI mask's bits 15:8 are
;; dropped), and each line is one aligned masked move behind a GPR test of
;; its mask.  A line is thus touched only when it holds an active element,
;; which the scalar loop accesses too, and no move runs with an empty mask
;; (S3-03's fault rules).  Loads merge both lines into a zeroed register
;; (disjoint lanes) and rotate it down by s with vpermd; stores rotate the
;; data up by s first.  The dword index is iota + d for loads and iota - d
;; for stores, with d = (A & 63) / 4 for both element sizes.  The cost
;; term in ix86_vector_costs::add_stmt_cost still keeps loops with such
;; accesses scalar unless costs are ignored (S4-14 kept it).  An address
;; below element alignment keeps the fallback that moves each active
;; element through a GPR, touching nothing the scalar loop does not.
(define_expand "@knc_maskmove<mode>"
  [(match_operand:KNC_PACKED 0 "nonimmediate_operand")
   (match_operand:KNC_PACKED 1 "nonimmediate_operand")
   (match_operand:<knc_maskmode> 2 "register_operand")]
  "TARGET_KNC"
{
  bool load = MEM_P (operands[1]);
  rtx mem = load ? operands[1] : operands[0];
  rtx mask = operands[2];
  rtx data = load ? gen_reg_rtx (<MODE>mode) : operands[1];
  if (load)
    emit_move_insn (data, CONST0_RTX (<MODE>mode));
  if (MEM_ALIGN (mem) >= 512)
    {
      rtx skip = gen_label_rtx ();
      if (<knc_maskmode>mode == HImode)
	emit_jump_insn (gen_knc_jkeq (mask, skip));
      else
	emit_cmp_and_jump_insns (mask, const0_rtx, EQ, NULL_RTX, QImode, 1,
				 skip);
      if (load)
	emit_insn (gen_knc_maskload_aligned<mode> (data, mem, data, mask));
      else
	emit_insn (gen_knc_maskstore_aligned<mode> (mem, data, mask));
      emit_label (skip);
    }
  else if (ix86_knc_element_aligned_mem_p (mem, <MODE>mode))
    {
      unsigned int lanes = GET_MODE_NUNITS (<MODE>mode);
      int log2_size = exact_log2 (GET_MODE_UNIT_SIZE (<MODE>mode));
      rtx addr = force_reg (Pmode, XEXP (mem, 0));
      rtx line = expand_simple_binop (Pmode, AND, addr, GEN_INT (-64),
				      NULL_RTX, 1, OPTAB_DIRECT);
      rtx lines[2];
      lines[0] = change_address (mem, <MODE>mode, line);
      set_mem_align (lines[0], 512);
      lines[1] = adjust_address (lines[0], <MODE>mode, 64);
      set_mem_align (lines[1], 512);
      rtx off = expand_simple_binop (SImode, AND, gen_lowpart (SImode, addr),
				     GEN_INT (63), NULL_RTX, 1, OPTAB_DIRECT);
      rtx s = expand_simple_binop (SImode, LSHIFTRT, off, GEN_INT (log2_size),
				   NULL_RTX, 1, OPTAB_DIRECT);
      rtx d = expand_simple_binop (SImode, LSHIFTRT, off, GEN_INT (2),
				   NULL_RTX, 1, OPTAB_DIRECT);
      rtx kk = force_reg (SImode, convert_to_mode (SImode, mask, 1));
      rtx masks[2];
      masks[0] = expand_simple_binop (SImode, ASHIFT, kk, s, NULL_RTX, 1,
				      OPTAB_DIRECT);
      masks[0] = expand_simple_binop (SImode, AND, masks[0],
				      GEN_INT ((1 << lanes) - 1), NULL_RTX, 1,
				      OPTAB_DIRECT);
      rtx rest = expand_simple_binop (SImode, MINUS, GEN_INT (lanes), s,
				      NULL_RTX, 1, OPTAB_DIRECT);
      masks[1] = expand_simple_binop (SImode, LSHIFTRT, kk, rest, NULL_RTX, 1,
				      OPTAB_DIRECT);
      rtx shift = gen_reg_rtx (V16SImode);
      ix86_knc_expand_duplicate (shift, d);
      rtx iota = force_reg (V16SImode, gen_const_vec_series (V16SImode,
							     const0_rtx,
							     const1_rtx));
      rtx idx = gen_reg_rtx (V16SImode);
      emit_insn (gen_rtx_SET (idx, load
				   ? gen_rtx_PLUS (V16SImode, iota, shift)
				   : gen_rtx_MINUS (V16SImode, iota, shift)));
      rtx lanes_data = data;
      if (!load)
	{
	  lanes_data = gen_reg_rtx (<MODE>mode);
	  emit_insn (gen_knc_permvar<mode> (lanes_data, force_reg (<MODE>mode,
								   data),
					    idx));
	}
      for (int i = 0; i < 2; i++)
	{
	  rtx skip = gen_label_rtx ();
	  emit_cmp_and_jump_insns (masks[i], const0_rtx, EQ, NULL_RTX, SImode,
				   1, skip);
	  rtx k = gen_reg_rtx (<knc_maskmode>mode);
	  emit_move_insn (k, gen_lowpart (<knc_maskmode>mode, masks[i]));
	  if (load)
	    emit_insn (gen_knc_maskload_aligned<mode> (lanes_data, lines[i],
						       lanes_data, k));
	  else
	    emit_insn (gen_knc_maskstore_aligned<mode> (lines[i], lanes_data,
							k));
	  emit_label (skip);
	}
      if (load)
	{
	  data = gen_reg_rtx (<MODE>mode);
	  emit_insn (gen_knc_permvar<mode> (data, lanes_data, idx));
	}
    }
  else
    {
      unsigned int size = GET_MODE_UNIT_SIZE (<MODE>mode);
      machine_mode emode = size == 4 ? SImode : DImode;
      rtx slot = assign_stack_temp (<MODE>mode, GET_MODE_SIZE (<MODE>mode));
      emit_move_insn (slot, data);
      rtx bits = force_reg (SImode, convert_to_mode (SImode, mask, 1));
      for (unsigned int i = 0; i < GET_MODE_NUNITS (<MODE>mode); i++)
	{
	  rtx next = gen_label_rtx ();
	  rtx bit = expand_simple_binop (SImode, AND, bits, GEN_INT (1u << i),
					 NULL_RTX, 1, OPTAB_DIRECT);
	  emit_cmp_and_jump_insns (bit, const0_rtx, EQ, NULL_RTX, SImode, 1,
				   next);
	  rtx from = adjust_address (load ? mem : slot, emode, i * size);
	  rtx to = adjust_address (load ? slot : mem, emode, i * size);
	  emit_move_insn (to, force_reg (emode, from));
	  emit_label (next);
	}
      if (load)
	emit_move_insn (data, slot);
    }
  if (load)
    emit_move_insn (operands[0], data);
  DONE;
})

;; Intel 327364-001 VPMINSD/VPMAXSD/VPMINUD/VPMAXUD pp.497-506.  k1om has no
;; 64-bit forms, so V8DI min/max stay off.
(define_insn "*knc_<code>v16si3"
  [(set (match_operand:V16SI 0 "register_operand" "=YJ")
	(maxmin:V16SI
	  (match_operand:V16SI 1 "register_operand" "%YJ")
	  (match_operand:V16SI 2 "vector_operand" "YJBm")))]
  "TARGET_KNC"
  "vp<maxmin_int>d\t%g2, %g1, %g0"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "XI")])

;; Expander-only copy of sse.md's VFH for cond_<insn><mode> (ADR-0007
;; point 2); the insns keep VFH.  ix86_knc_packed_mode_p limits KNC to the
;; 64-bit packed modes.
(define_mode_iterator VFH_KNC
  [(V32HF "TARGET_AVX512FP16")
   (V16HF "TARGET_AVX512FP16 && TARGET_AVX512VL")
   (V8HF "TARGET_AVX512FP16 && TARGET_AVX512VL")
   (V16SF "TARGET_AVX512F || ix86_knc_packed_mode_p (V16SFmode)")
   (V8SF "TARGET_AVX") V4SF
   (V8DF "TARGET_AVX512F || ix86_knc_packed_mode_p (V8DFmode)")
   (V4DF "TARGET_AVX")
   (V2DF "TARGET_SSE2")])

;; Masked arithmetic (ADR-0007 point 3): lanes with a clear mask bit keep
;; operand 3 and raise no exception (Intel 327364-001 2.1.2, p.22).  The
;; shared cond_<insn><mode> expander emits this through <insn><mode>3_mask.
(define_insn "*knc_<knc_fp_binary><mode>3_mask"
  [(set (match_operand:KNC_FP 0 "register_operand" "=YJ")
	(vec_merge:KNC_FP
	  (KNC_FP_BINARY:KNC_FP
	    (match_operand:KNC_FP 1 "register_operand" "YJ")
	    (match_operand:KNC_FP 2 "vector_operand" "YJBm"))
	  (match_operand:KNC_FP 3 "nonimm_or_0_operand" "0")
	  (match_operand:<knc_maskmode> 4 "register_operand" "YL")))]
  "TARGET_KNC"
  "v<knc_fp_binary><knc_fp_suffix>\t{%g2, %g1, %g0%{%4%}|%g0%{%4%}, %g1, %g2}"
  [(set_attr "type" "<knc_fp_type>")
   (set_attr "mode" "<MODE>")])

;; Masked FMA (S4-05, Intel 327364-001 VFMADD231PS/PD): active lanes get
;; operand 1 * operand 2 + operand 3 with one rounding, like the unmasked
;; body; the others keep operand 3 and raise no exception (2.1.2, p.22).
(define_insn "knc_fma<mode>_mask"
  [(set (match_operand:KNC_FP 0 "register_operand" "=YJ")
	(vec_merge:KNC_FP
	  (fma:KNC_FP
	    (match_operand:KNC_FP 1 "register_operand" "YJ")
	    (match_operand:KNC_FP 2 "register_operand" "YJ")
	    (match_operand:KNC_FP 3 "register_operand" "0"))
	  (match_dup 3)
	  (match_operand:<knc_maskmode> 4 "register_operand" "YL")))]
  "TARGET_KNC"
  "vfmadd231<knc_fp_suffix>\t{%g2, %g1, %g0%{%4%}|%g0%{%4%}, %g1, %g2}"
  [(set_attr "type" "ssemuladd")
   (set_attr "mode" "<MODE>")])

;; cond_fma: sse.md's cond_fma<mode> calls this on entry under KNC.  There
;; is no {z} form, so an else value other than the addend is blended into
;; the addend first (vblendm, the mask selecting the addend); the masked
;; vfmadd231 then leaves the else value in the inactive lanes.  Unlike an
;; unmasked FMA followed by a blend, no inactive lane is computed.
(define_expand "@knc_cond_fma<mode>"
  [(match_operand:KNC_FP 0 "register_operand")
   (match_operand:<knc_maskmode> 1 "register_operand")
   (match_operand:KNC_FP 2 "vector_operand")
   (match_operand:KNC_FP 3 "vector_operand")
   (match_operand:KNC_FP 4 "vector_operand")
   (match_operand:KNC_FP 5 "nonimm_or_0_operand")]
  "TARGET_KNC"
{
  rtx mask = operands[1];
  rtx acc = gen_reg_rtx (<MODE>mode);
  if (rtx_equal_p (operands[4], operands[5]))
    emit_move_insn (acc, operands[4]);
  else
    emit_insn (gen_rtx_SET (acc,
			    gen_rtx_VEC_MERGE (<MODE>mode, operands[4],
					       force_reg (<MODE>mode,
							  operands[5]),
					       mask)));
  emit_insn (gen_knc_fma<mode>_mask (acc, force_reg (<MODE>mode, operands[2]),
				     force_reg (<MODE>mode, operands[3]),
				     acc, mask));
  emit_move_insn (operands[0], acc);
  DONE;
})

;; while_ult (S4-05, ADR-0007 point 7): lane I of operand 0 is set when
;; operand 1 + I < operand 2 (unsigned), for I below operand 3.  The lane
;; count min (operand 2 - operand 1, operand 3), or 0, is formed in a GPR
;; without branches (no cmov under KNC), and (1 << count) - 1 moves to the
;; mask.  An 8-lane mask is at most 0xff, so bits 15:8 stay clear.
(define_mode_iterator KNC_WCMP [SI (DI "TARGET_64BIT")])
(define_mode_iterator KNC_WMASK [HI QI])
(define_expand "while_ult<KNC_WCMP:mode><KNC_WMASK:mode>"
  [(match_operand:KNC_WMASK 0 "register_operand")
   (match_operand:KNC_WCMP 1 "register_operand")
   (match_operand:KNC_WCMP 2 "register_operand")
   (match_operand 3 "const_int_operand")]
  "TARGET_KNC"
{
  machine_mode cmode = <KNC_WCMP:MODE>mode;
  rtx lanes = GEN_INT (INTVAL (operands[3]));
  /* All ones when operand 1 < operand 2, else zero.  */
  rtx live = emit_store_flag_force (gen_reg_rtx (cmode), LTU, operands[1],
				    operands[2], cmode, 1, -1);
  rtx left = expand_simple_binop (cmode, MINUS, operands[2], operands[1],
				  NULL_RTX, 1, OPTAB_DIRECT);
  left = expand_simple_binop (cmode, AND, left, live, NULL_RTX, 1,
			      OPTAB_DIRECT);
  /* min (left, lanes) = lanes + ((left - lanes) & (left < lanes ? -1 : 0)).  */
  rtx below = emit_store_flag_force (gen_reg_rtx (cmode), LTU, left, lanes,
				     cmode, 1, -1);
  rtx count = expand_simple_binop (cmode, MINUS, left, lanes, NULL_RTX, 1,
				   OPTAB_DIRECT);
  count = expand_simple_binop (cmode, AND, count, below, NULL_RTX, 1,
			       OPTAB_DIRECT);
  count = expand_simple_binop (cmode, PLUS, count, lanes, NULL_RTX, 1,
			       OPTAB_DIRECT);
  rtx bits = expand_simple_binop (SImode, ASHIFT, const1_rtx,
				  gen_lowpart (SImode, count), NULL_RTX, 1,
				  OPTAB_DIRECT);
  bits = expand_simple_binop (SImode, PLUS, bits, constm1_rtx, NULL_RTX, 1,
			      OPTAB_DIRECT);
  emit_move_insn (operands[0], gen_lowpart (<KNC_WMASK:MODE>mode, bits));
  DONE;
})

;; Integer add and sub (ADR-0007 point 6).  sse.md's <insn><mode>3 calls
;; this on entry under KNC, so upstream changes to that body (constant
;; operands, the +1 to -1 rewrite) never reach KNC.  Constants go to
;; registers (ix86_knc_fixup_binary_operands).  V16SI is vpaddd/vpsubd.
(define_mode_iterator KNC_ADDSUB [V16SI V8DI])
(define_expand "@knc_plusminus<insn><mode>"
  [(set (match_operand:KNC_ADDSUB 0 "register_operand")
	(plusminus:KNC_ADDSUB
	  (match_operand:KNC_ADDSUB 1 "register_operand")
	  (match_operand:KNC_ADDSUB 2 "vector_operand")))]
  "TARGET_KNC"
{
  ix86_knc_fixup_binary_operands (<MODE>mode, operands);
  if (<MODE>mode == V8DImode)
    {
      /* The mask and zero are ordinary moves, so loop invariant motion
	 hoists them; the carry sequence splits before reload.  */
      rtx kodd = force_reg (HImode, gen_int_mode (0xaaaa, HImode));
      rtx zero = force_reg (V16SImode, CONST0_RTX (V16SImode));
      operands[2] = force_reg (V8DImode, operands[2]);
      emit_insn (gen_knc_<insn>v8di3_carry (operands[0], operands[1],
					    operands[2], kodd, zero));
      DONE;
    }
})

;; k1om has no vpaddq or vpsubq.  Qword j is dword 2j (low) and 2j+1
;; (high).  Register swizzle {cdab} swaps the dwords of each qword (Intel
;; 327364-001 table 2.2, p.29).  For add (sub uses the borrow forms):
;;   vmovdqa32  t, a{cdab}         t[2j+1] = a.lo (p.382)
;;   vpaddsetcd t, kc, b{cdab}     kc[2j+1] = carry (a.lo + b.lo) (p.435)
;;   vpaddd     r, a, b            both halves, no carry (p.432)
;;   vpadcd     r{kodd}, kc, zero  r.hi += kc[2j+1] (p.429)
;; vpaddsetcd has no write mask, so kc is fully defined.  A carry stays in
;; its own lane's bit.  VPSUBSETBD p.565, VPSUBD p.556, VPSBBD p.524.
(define_c_enum "unspec" [UNSPEC_KNC_CARRY UNSPEC_KNC_CARRY_IN])
(define_code_attr knc_carry_out [(plus "vpaddsetcd") (minus "vpsubsetbd")])
(define_code_attr knc_carry_in [(plus "vpadcd") (minus "vpsbbd")])

(define_insn "knc_swap_pairsv16si"
  [(set (match_operand:V16SI 0 "register_operand" "=YJ")
	(vec_select:V16SI
	  (match_operand:V16SI 1 "register_operand" "YJ")
	  (parallel [(const_int 1) (const_int 0) (const_int 3) (const_int 2)
		     (const_int 5) (const_int 4) (const_int 7) (const_int 6)
		     (const_int 9) (const_int 8) (const_int 11) (const_int 10)
		     (const_int 13) (const_int 12) (const_int 15) (const_int 14)])))]
  "TARGET_KNC"
  "vmovdqa32\t{%g1%{cdab%}, %g0|%g0, %g1%{cdab%}}"
  [(set_attr "type" "sselog1")
   (set_attr "mode" "XI")])

;; Operand 1 is the carry (borrow) out of each lane of the sum.
(define_insn "knc_<insn>_carry_outv16si"
  [(set (match_operand:V16SI 0 "register_operand" "=YJ")
	(plusminus:V16SI
	  (match_operand:V16SI 2 "register_operand" "0")
	  (vec_select:V16SI
	    (match_operand:V16SI 3 "register_operand" "YJ")
	    (parallel [(const_int 1) (const_int 0) (const_int 3) (const_int 2)
		       (const_int 5) (const_int 4) (const_int 7) (const_int 6)
		       (const_int 9) (const_int 8) (const_int 11) (const_int 10)
		       (const_int 13) (const_int 12) (const_int 15) (const_int 14)]))))
   (set (match_operand:HI 1 "register_operand" "=YK")
	(unspec:HI
	  [(plusminus:V16SI
	     (match_dup 2)
	     (vec_select:V16SI
	       (match_dup 3)
	       (parallel [(const_int 1) (const_int 0) (const_int 3) (const_int 2)
			 (const_int 5) (const_int 4) (const_int 7) (const_int 6)
			 (const_int 9) (const_int 8) (const_int 11) (const_int 10)
			 (const_int 13) (const_int 12) (const_int 15) (const_int 14)])))]
	  UNSPEC_KNC_CARRY))]
  "TARGET_KNC"
  "<knc_carry_out>\t{%g3%{cdab%}, %1, %g0|%g0, %1, %g3%{cdab%}}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "XI")])

;; Lanes set in operand 5 take operand 2 +/- operand 4 +/- operand 3's bit
;; and write their carry (borrow) back into that bit; the others keep both.
(define_insn "knc_<insn>_carry_inv16si"
  [(set (match_operand:V16SI 0 "register_operand" "=YJ")
	(vec_merge:V16SI
	  (unspec:V16SI
	    [(plusminus:V16SI
	       (match_operand:V16SI 2 "register_operand" "0")
	       (match_operand:V16SI 4 "register_operand" "YJ"))
	     (match_operand:HI 3 "register_operand" "1")]
	    UNSPEC_KNC_CARRY_IN)
	  (match_dup 2)
	  (match_operand:HI 5 "register_operand" "YL")))
   (set (match_operand:HI 1 "register_operand" "=YK")
	(unspec:HI [(match_dup 2) (match_dup 4) (match_dup 3) (match_dup 5)]
		   UNSPEC_KNC_CARRY_IN))]
  "TARGET_KNC"
  "<knc_carry_in>\t{%g4, %1, %g0%{%5%}|%g0%{%5%}, %1, %g4}"
  [(set_attr "type" "sseiadd")
   (set_attr "mode" "XI")])

;; Operand 3 is kodd (0xaaaa) and operand 4 a zero vector, both set up by
;; the expander outside the loop.  The temporaries are new pseudos.
(define_insn_and_split "knc_<insn>v8di3_carry"
  [(set (match_operand:V8DI 0 "register_operand")
	(plusminus:V8DI
	  (match_operand:V8DI 1 "register_operand")
	  (match_operand:V8DI 2 "register_operand")))
   (use (match_operand:HI 3 "register_operand"))
   (use (match_operand:V16SI 4 "register_operand"))]
  "TARGET_KNC && ix86_pre_reload_split ()"
  "#"
  "&& 1"
  [(const_int 0)]
{
  rtx a = gen_lowpart (V16SImode, operands[1]);
  rtx b = gen_lowpart (V16SImode, operands[2]);
  rtx t = gen_reg_rtx (V16SImode);
  rtx low = gen_reg_rtx (V16SImode);
  rtx carry = gen_reg_rtx (HImode);
  rtx r = gen_reg_rtx (V16SImode);
  rtx result = gen_reg_rtx (V16SImode);
  rtx spent = gen_reg_rtx (HImode);
  emit_insn (gen_knc_swap_pairsv16si (t, a));
  emit_insn (gen_knc_<insn>_carry_outv16si (low, carry, t, b));
  emit_insn (gen_rtx_SET (r, gen_rtx_<CODE> (V16SImode, a, b)));
  emit_insn (gen_knc_<insn>_carry_inv16si (result, spent, r, carry,
					   operands[4], operands[3]));
  emit_move_insn (operands[0], gen_lowpart (V8DImode, result));
  DONE;
})

;; S5-15 / ADR-0008 point 3.  Intel 327364-001 pp.334-349: one quiet
;; NaN returns the number; two quiet NaNs return a quiet NaN.  Signaling
;; NaNs are not quietened, so the C fmin/fmax optabs exclude that mode.
;; RTL smin/smax permit either operand for NaNs and signed zeros.
(define_code_attr knc_maxmin [(smax "max") (smin "min")])
(define_insn "*knc_<knc_maxmin><mode>3"
  [(set (match_operand:KNC_FP 0 "register_operand" "=YJ")
        (smaxmin:KNC_FP
          (match_operand:KNC_FP 1 "register_operand" "%YJ")
          (match_operand:KNC_FP 2 "register_operand" "YJ")))]
  "TARGET_KNC"
  "vg<knc_maxmin><knc_fp_suffix>\t{%g2, %g1, %g0|%g0, %g1, %g2}"
  [(set_attr "type" "sseadd") (set_attr "mode" "<MODE>")])

(define_c_enum "unspec" [UNSPEC_KNC_FMIN UNSPEC_KNC_FMAX])
(define_int_iterator KNC_FMINMAX [UNSPEC_KNC_FMIN UNSPEC_KNC_FMAX])
(define_int_attr knc_fminmax
  [(UNSPEC_KNC_FMIN "min") (UNSPEC_KNC_FMAX "max")])
(define_int_attr knc_fminmax_code
  [(UNSPEC_KNC_FMIN "SMIN") (UNSPEC_KNC_FMAX "SMAX")])

(define_insn "f<knc_fminmax><mode>3"
  [(set (match_operand:KNC_FP 0 "register_operand" "=YJ")
        (unspec:KNC_FP
          [(match_operand:KNC_FP 1 "register_operand" "YJ")
           (match_operand:KNC_FP 2 "register_operand" "YJ")]
          KNC_FMINMAX))]
  "TARGET_KNC && !flag_signaling_nans"
  "vg<knc_fminmax><knc_fp_suffix>\t{%g2, %g1, %g0|%g0, %g1, %g2}"
  [(set_attr "type" "sseadd") (set_attr "mode" "<MODE>")])

(define_expand "reduc_f<knc_fminmax>_scal_<mode>"
  [(match_operand:<knc_fp_scalar> 0 "register_operand")
   (unspec:KNC_FP [(match_operand:KNC_FP 1 "register_operand")]
                  KNC_FMINMAX)]
  "TARGET_KNC && !flag_signaling_nans"
{
  ix86_knc_expand_reduc (<knc_fminmax_code>, operands[0], operands[1], true);
  DONE;
})

(define_insn "*knc_reduc_<knc_maxmin><mode>_swizzle"
  [(set (match_operand:KNC_FP 0 "register_operand" "=YJ")
        (smaxmin:KNC_FP
          (match_operand:KNC_FP 1 "register_operand" "YJ")
          (vec_select:KNC_FP
            (match_operand:KNC_FP 2 "register_operand" "YJ")
            (match_parallel 3 ""
              [(match_operand 4 "const_int_operand")]))))]
  "TARGET_KNC && ix86_knc_reduc_swizzle_p (operands[3], <MODE>mode)"
{
  if (INTVAL (operands[4]) == 1)
    return "vg<knc_maxmin><knc_fp_suffix>\t{%g2%{cdab%}, %g1, %g0|%g0, %g1, %g2%{cdab%}}";
  return "vg<knc_maxmin><knc_fp_suffix>\t{%g2%{badc%}, %g1, %g0|%g0, %g1, %g2%{badc%}}";
}
  [(set_attr "type" "sseadd") (set_attr "mode" "<MODE>")])

(define_insn "*knc_reduc_f<knc_fminmax><mode>_swizzle"
  [(set (match_operand:KNC_FP 0 "register_operand" "=YJ")
        (unspec:KNC_FP
          [(match_operand:KNC_FP 1 "register_operand" "YJ")
           (vec_select:KNC_FP
             (match_operand:KNC_FP 2 "register_operand" "YJ")
             (match_parallel 3 ""
               [(match_operand 4 "const_int_operand")]))]
          KNC_FMINMAX))]
  "TARGET_KNC && !flag_signaling_nans
   && ix86_knc_reduc_swizzle_p (operands[3], <MODE>mode)"
{
  if (INTVAL (operands[4]) == 1)
    return "vg<knc_fminmax><knc_fp_suffix>\t{%g2%{cdab%}, %g1, %g0|%g0, %g1, %g2%{cdab%}}";
  return "vg<knc_fminmax><knc_fp_suffix>\t{%g2%{badc%}, %g1, %g0|%g0, %g1, %g2%{badc%}}";
}
  [(set_attr "type" "sseadd") (set_attr "mode" "<MODE>")])

;; S5-07: reductions use the register-only SwizzUpConv operand (Intel
;; 327364-001 pp.28-30).  The selector is an exact VEC_SELECT: every lane
;; is XORed with 1 ({cdab}) or 2 ({badc}), in the instruction's element
;; size.  No rounding override may share these swizzle bits.
(define_code_iterator KNC_REDUC_INT [plus smin smax umin umax])
(define_code_attr knc_reduc_int
  [(plus "addd") (smin "minsd") (smax "maxsd")
   (umin "minud") (umax "maxud")])

(define_insn "*knc_reduc_plus<mode>_swizzle"
  [(set (match_operand:KNC_FP 0 "register_operand" "=YJ")
        (plus:KNC_FP
          (match_operand:KNC_FP 1 "register_operand" "YJ")
          (vec_select:KNC_FP
            (match_operand:KNC_FP 2 "register_operand" "YJ")
            (match_parallel 3 ""
              [(match_operand 4 "const_int_operand")]))))]
  "TARGET_KNC && ix86_knc_reduc_swizzle_p (operands[3], <MODE>mode)"
{
  if (INTVAL (operands[4]) == 1)
    return "vadd<knc_fp_suffix>\t{%g2%{cdab%}, %g1, %g0|%g0, %g1, %g2%{cdab%}}";
  return "vadd<knc_fp_suffix>\t{%g2%{badc%}, %g1, %g0|%g0, %g1, %g2%{badc%}}";
}
  [(set_attr "type" "sseadd") (set_attr "mode" "<MODE>")])

(define_insn "*knc_reduc_<code>v16si_swizzle"
  [(set (match_operand:V16SI 0 "register_operand" "=YJ")
        (KNC_REDUC_INT:V16SI
          (match_operand:V16SI 1 "register_operand" "YJ")
          (vec_select:V16SI
            (match_operand:V16SI 2 "register_operand" "YJ")
            (match_parallel 3 ""
              [(match_operand 4 "const_int_operand")]))))]
  "TARGET_KNC && ix86_knc_reduc_swizzle_p (operands[3], V16SImode)"
{
  if (INTVAL (operands[4]) == 1)
    return "vp<knc_reduc_int>\t{%g2%{cdab%}, %g1, %g0|%g0, %g1, %g2%{cdab%}}";
  return "vp<knc_reduc_int>\t{%g2%{badc%}, %g1, %g0|%g0, %g1, %g2%{badc%}}";
}
  [(set_attr "type" "sseiadd") (set_attr "mode" "XI")])

(define_insn "*knc_reduc_<code><mode>_swizzle"
  [(set (match_operand:KNC_INT 0 "register_operand" "=YJ")
        (any_logic:KNC_INT
          (match_operand:KNC_INT 1 "register_operand" "YJ")
          (vec_select:KNC_INT
            (match_operand:KNC_INT 2 "register_operand" "YJ")
            (match_parallel 3 ""
              [(match_operand 4 "const_int_operand")]))))]
  "TARGET_KNC && ix86_knc_reduc_swizzle_p (operands[3], <MODE>mode)"
{
  if (INTVAL (operands[4]) == 1)
    return "vp<logic><knc_int_suffix>\t{%g2%{cdab%}, %g1, %g0|%g0, %g1, %g2%{cdab%}}";
  return "vp<logic><knc_int_suffix>\t{%g2%{badc%}, %g1, %g0|%g0, %g1, %g2%{badc%}}";
}
  [(set_attr "type" "sselog") (set_attr "mode" "XI")])

;; S5-10, Intel 327364-001 pp.157-179, 197-198, 221-222, 316, 324,
;; 588-592.  Each UNSPEC is one register instruction: its rounding/SAE
;; semantics differ from ordinary arithmetic.  Keep all operands visible.
;; KNC gas 2.22 requires SAE before the immediate in AT&T syntax.
(define_c_enum "unspec"
  [UNSPEC_KNC_SQRT_MANT UNSPEC_KNC_SQRT_EXP UNSPEC_KNC_SQRT_CVT
   UNSPEC_KNC_SQRT_RSQRT UNSPEC_KNC_SQRT_MUL UNSPEC_KNC_SQRT_FMA
   UNSPEC_KNC_SQRT_FNMA UNSPEC_KNC_SQRT_SCALE UNSPEC_KNC_SQRT_FIXUP])
(define_c_enum "unspecv" [UNSPECV_KNC_SQRT_INVALID])

(define_insn "knc_sqrt_mant"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (unspec:V16SF [(match_operand:V16SF 1 "register_operand" "YJ")]
                       UNSPEC_KNC_SQRT_MANT))]
  "TARGET_KNC"
  "vgetmantps\t{%{sae%}, $5, %g1, %g0|%g0, %g1, 5, %{sae%}}"
  [(set_attr "type" "sse") (set_attr "mode" "V16SF")])

(define_insn "knc_sqrt_exp"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (unspec:V16SF [(match_operand:V16SF 1 "register_operand" "YJ")]
                       UNSPEC_KNC_SQRT_EXP))]
  "TARGET_KNC"
  "vgetexpps\t{%{sae%}, %g1, %g0|%g0, %g1, %{sae%}}"
  [(set_attr "type" "sse") (set_attr "mode" "V16SF")])

(define_insn "knc_sqrt_cvt"
  [(set (match_operand:V16SI 0 "register_operand" "=YJ")
        (unspec:V16SI [(match_operand:V16SF 1 "register_operand" "YJ")]
                       UNSPEC_KNC_SQRT_CVT))]
  "TARGET_KNC"
  "vcvtfxpntps2dq\t{%{sae%}, $0, %g1, %g0|%g0, %g1, 0, %{sae%}}"
  [(set_attr "type" "ssecvt") (set_attr "mode" "V16SF")])

(define_insn "knc_sqrt_rsqrt"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (unspec:V16SF [(match_operand:V16SF 1 "register_operand" "YJ")]
                       UNSPEC_KNC_SQRT_RSQRT))]
  "TARGET_KNC"
  "vrsqrt23ps\t{%{sae%}, %g1, %g0|%g0, %g1, %{sae%}}"
  [(set_attr "type" "sse") (set_attr "mode" "V16SF")])

(define_insn "knc_sqrt_mul"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (unspec:V16SF [(match_operand:V16SF 1 "register_operand" "YJ")
                       (match_operand:V16SF 2 "register_operand" "YJ")]
                      UNSPEC_KNC_SQRT_MUL))]
  "TARGET_KNC"
  "vmulps\t{%{rn-sae%}, %g2, %g1, %g0|%g0, %g1, %g2, %{rn-sae%}}"
  [(set_attr "type" "ssemul") (set_attr "mode" "V16SF")])

(define_insn "knc_sqrt_fma"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (unspec:V16SF [(match_operand:V16SF 1 "register_operand" "YJ")
                       (match_operand:V16SF 2 "register_operand" "YJ")
                       (match_operand:V16SF 3 "register_operand" "0")]
                      UNSPEC_KNC_SQRT_FMA))]
  "TARGET_KNC"
  "vfmadd231ps\t{%{rn-sae%}, %g2, %g1, %g0|%g0, %g1, %g2, %{rn-sae%}}"
  [(set_attr "type" "ssemuladd") (set_attr "mode" "V16SF")])

(define_insn "knc_sqrt_fnma"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (unspec:V16SF [(match_operand:V16SF 1 "register_operand" "YJ")
                       (match_operand:V16SF 2 "register_operand" "YJ")
                       (match_operand:V16SF 3 "register_operand" "0")]
                      UNSPEC_KNC_SQRT_FNMA))]
  "TARGET_KNC"
  "vfnmadd231ps\t{%{rn-sae%}, %g2, %g1, %g0|%g0, %g1, %g2, %{rn-sae%}}"
  [(set_attr "type" "ssemuladd") (set_attr "mode" "V16SF")])

(define_insn "knc_sqrt_scale"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (vec_merge:V16SF
          (unspec:V16SF [(match_operand:V16SF 1 "register_operand" "0")
                         (match_operand:V16SI 2 "register_operand" "YJ")]
                        UNSPEC_KNC_SQRT_SCALE)
          (match_dup 1)
          (match_operand:HI 3 "register_operand" "YL")))]
  "TARGET_KNC"
  "vscaleps\t{%g2, %g1, %g0%{%3%}|%g0%{%3%}, %g1, %g2}"
  [(set_attr "type" "ssemul") (set_attr "mode" "V16SF")])

;; Volatile: the otherwise unused estimate must still raise invalid.
(define_insn "knc_sqrt_invalid"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (unspec_volatile:V16SF
          [(match_operand:V16SF 1 "register_operand" "0")
           (match_operand:HI 2 "register_operand" "YL")]
          UNSPECV_KNC_SQRT_INVALID))]
  "TARGET_KNC"
  "vrsqrt23ps\t{%g1, %g0%{%2%}|%g0%{%2%}, %g1}"
  [(set_attr "type" "sse") (set_attr "mode" "V16SF")])

(define_insn "knc_sqrt_fixup"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (unspec:V16SF [(match_operand:V16SF 1 "register_operand" "0")
                       (match_operand:V16SF 2 "register_operand" "YJ")
                       (match_operand:V16SI 3 "register_operand" "YJ")]
                      UNSPEC_KNC_SQRT_FIXUP))]
  "TARGET_KNC"
  "vfixupnanps\t{%g3, %g2, %g0|%g0, %g2, %g3}"
  [(set_attr "type" "sse") (set_attr "mode" "V16SF")])

;; Private immediate shift for the square-root exponent path.  Do not
;; advertise a general KNC shift optab in this ticket.
(define_insn "knc_sqrt_shift"
  [(set (match_operand:V16SI 0 "register_operand" "=YJ")
        (ashiftrt:V16SI (match_operand:V16SI 1 "register_operand" "YJ")
                       (const_int 1)))]
  "TARGET_KNC"
  "vpsrad\t{$1, %g1, %g0|%g0, %g1, 1}"
  [(set_attr "type" "sseishft") (set_attr "mode" "XI")])

;; S5-11: FP32 division, research/knc-div-sqrt.md section 2.
;; Expander-only VFH copy; FP64 division remains unavailable on KNC.
(define_mode_iterator VFH_KNC_DIV
  [(V32HF "TARGET_AVX512FP16")
   (V16HF "TARGET_AVX512FP16 && TARGET_AVX512VL")
   (V8HF "TARGET_AVX512FP16 && TARGET_AVX512VL")
   (V16SF "TARGET_AVX512F || TARGET_KNC") (V8SF "TARGET_AVX") V4SF
   (V8DF "TARGET_AVX512F") (V4DF "TARGET_AVX") (V2DF "TARGET_SSE2")])
(define_c_enum "unspec"
  [UNSPEC_KNC_DIV_MANT UNSPEC_KNC_DIV_RCP UNSPEC_KNC_DIV_SUB
   UNSPEC_KNC_DIV_SCALE_RU UNSPEC_KNC_DIV_SCALE_RD])
(define_c_enum "unspecv" [UNSPECV_KNC_DIV_FLAGS UNSPECV_KNC_DIV_UNDERFLOW UNSPECV_KNC_DIV_MXCSR])

(define_insn "knc_div_mant"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (unspec:V16SF [(match_operand:V16SF 1 "register_operand" "YJ")
                       (match_operand:SI 2 "const_int_operand")]
                      UNSPEC_KNC_DIV_MANT))]
  "TARGET_KNC"
  "vgetmantps\t{%{sae%}, %2, %g1, %g0|%g0, %g1, %2, %{sae%}}"
  [(set_attr "type" "sse") (set_attr "mode" "V16SF")])

(define_insn "knc_div_rcp"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (unspec:V16SF [(match_operand:V16SF 1 "register_operand" "YJ")]
                      UNSPEC_KNC_DIV_RCP))]
  "TARGET_KNC"
  "vrcp23ps\t{%{sae%}, %g1, %g0|%g0, %g1, %{sae%}}"
  [(set_attr "type" "sse") (set_attr "mode" "V16SF")])

(define_insn "knc_div_sub"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (unspec:V16SF [(match_operand:V16SF 1 "register_operand" "YJ")
                       (match_operand:V16SF 2 "register_operand" "YJ")]
                      UNSPEC_KNC_DIV_SUB))]
  "TARGET_KNC"
  "vsubps\t{%{rn-sae%}, %g2, %g1, %g0|%g0, %g1, %g2, %{rn-sae%}}"
  [(set_attr "type" "sseadd") (set_attr "mode" "V16SF")])

;; The masked reciprocal also raises #Z with an unused result.  Keeping
;; this one instruction volatile prevents deletion of that flag update.
(define_insn "knc_div_rcp_flags"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (unspec_volatile:V16SF
          [(match_operand:V16SF 1 "register_operand" "YJ")
           (match_operand:V16SF 2 "register_operand" "0")
           (match_operand:HI 3 "register_operand" "YL")]
          UNSPECV_KNC_DIV_FLAGS))]
  "TARGET_KNC"
  "vrcp23ps\t{%g1, %g0%{%3%}|%g0%{%3%}, %g1}"
  [(set_attr "type" "sse") (set_attr "mode" "V16SF")])

(define_insn "knc_div_underflow"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (unspec_volatile:V16SF
          [(match_operand:V16SF 1 "register_operand" "0")
           (match_operand:V16SF 2 "register_operand" "YJ")
           (match_operand:HI 3 "register_operand" "YL")]
          UNSPECV_KNC_DIV_UNDERFLOW))]
  "TARGET_KNC"
  "vmulps\t{%g2, %g1, %g0%{%3%}|%g0%{%3%}, %g1, %g2}"
  [(set_attr "type" "ssemul") (set_attr "mode" "V16SF")])

(define_insn "knc_div_mxcsr"
  [(set (match_operand:SI 0 "memory_operand" "=m")
        (unspec_volatile:SI [(const_int 0)] UNSPECV_KNC_DIV_MXCSR))]
  "TARGET_KNC"
  "stmxcsr\t%0"
  [(set_attr "type" "sse") (set_attr "memory" "store")])

(define_insn "knc_div_scale_ru"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (vec_merge:V16SF
          (unspec:V16SF [(match_operand:V16SF 1 "register_operand" "YJ")
                         (match_operand:V16SI 2 "register_operand" "YJ")]
                        UNSPEC_KNC_DIV_SCALE_RU)
          (match_operand:V16SF 3 "register_operand" "0")
          (match_operand:HI 4 "register_operand" "YL")))]
  "TARGET_KNC"
  "vscaleps\t{%{ru%}, %g2, %g1, %g0%{%4%}|%g0%{%4%}, %g1, %g2, %{ru%}}"
  [(set_attr "type" "ssemul") (set_attr "mode" "V16SF")])

(define_insn "knc_div_scale_rd"
  [(set (match_operand:V16SF 0 "register_operand" "=YJ")
        (vec_merge:V16SF
          (unspec:V16SF [(match_operand:V16SF 1 "register_operand" "YJ")
                         (match_operand:V16SI 2 "register_operand" "YJ")]
                        UNSPEC_KNC_DIV_SCALE_RD)
          (match_operand:V16SF 3 "register_operand" "0")
          (match_operand:HI 4 "register_operand" "YL")))]
  "TARGET_KNC"
  "vscaleps\t{%{rd%}, %g2, %g1, %g0%{%4%}|%g0%{%4%}, %g1, %g2, %{rd%}}"
  [(set_attr "type" "ssemul") (set_attr "mode" "V16SF")])

(define_insn "knc_div_shift_left"
  [(set (match_operand:V16SI 0 "register_operand" "=YJ")
        (ashift:V16SI (match_operand:V16SI 1 "register_operand" "YJ")
                       (match_operand:V16SI 2 "register_operand" "YJ")))]
  "TARGET_KNC"
  "vpsllvd\t{%g2, %g1, %g0|%g0, %g1, %g2}"
  [(set_attr "type" "sseishft") (set_attr "mode" "XI")])

(define_insn "knc_div_shift_right"
  [(set (match_operand:V16SI 0 "register_operand" "=YJ")
        (lshiftrt:V16SI (match_operand:V16SI 1 "register_operand" "YJ")
                       (match_operand:V16SI 2 "register_operand" "YJ")))]
  "TARGET_KNC"
  "vpsrlvd\t{%g2, %g1, %g0|%g0, %g1, %g2}"
  [(set_attr "type" "sseishft") (set_attr "mode" "XI")])
