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
   (V32HI "TARGET_AVX512F") (V16HI "TARGET_AVX") V8HI
   (V16SI "TARGET_AVX512F || TARGET_KNC") (V8SI "TARGET_AVX") V4SI
   (V8DI "TARGET_AVX512F || TARGET_KNC")  (V4DI "TARGET_AVX") V2DI
   (V4TI "TARGET_AVX512F") (V2TI "TARGET_AVX") V1TI
   (V32HF "TARGET_AVX512F") (V16HF "TARGET_AVX") V8HF
   (V32BF "TARGET_AVX512F") (V16BF "TARGET_AVX") V8BF
   (V16SF "TARGET_AVX512F || TARGET_KNC") (V8SF "TARGET_AVX") V4SF
   (V8DF "TARGET_AVX512F || TARGET_KNC")  (V4DF "TARGET_AVX") V2DF])

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

(define_insn "*knc_broadcast_memory<mode>"
  [(set (match_operand:KNC_FP 0 "register_operand" "=YJ")
        (vec_duplicate:KNC_FP (match_operand:<knc_fp_scalar> 1 "memory_operand" "m")))]
  "TARGET_KNC"
  "vbroadcast<knc_fp_broadcast>\t%1, %g0"
  [(set_attr "type" "ssemov") (set_attr "mode" "<MODE>")])

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
