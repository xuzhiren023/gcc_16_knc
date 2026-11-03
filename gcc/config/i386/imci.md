;; KNC scalar ABI transport (ADR-0004).
;; Lane-zero broadcasts and masked stores follow donor knc.md and i386.md.
;; No SSE constraints, implicit scratch registers or arithmetic optabs.
;; Transfers use UNSPEC to retain the memory bridge: forwarding a SET into
;; a plain x87/zmm register copy would require an unavailable instruction.

(define_mode_iterator KNC_SCALAR [SF DF SI DI TI TF])
(define_mode_iterator KNC_BROADCAST [SF DF SI DI])
(define_mode_iterator KNC_PAIR [TI TF])
(define_mode_attr knc_load [(SF "ss") (DF "sd") (SI "ss") (DI "sd")])
(define_mode_attr knc_store
  [(SF "ps") (DF "pd") (SI "ps") (DI "pd") (TI "q") (TF "pd")])

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
