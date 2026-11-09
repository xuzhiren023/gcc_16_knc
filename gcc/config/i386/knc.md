;; KNC scheduling, measured by S6-03 (knc-characterization.md, 2026-11-06).
;; Copyright (C) 2026 Free Software Foundation, Inc.
;; This file is part of GCC, distributed under the GNU General Public
;; License, version 3 or (at your option) any later version.
;;
;; A scheduler cycle is one issue opportunity for this thread: at least two
;; core cycles, or threads-per-core cycles at production density (default 4).
;; Reservation latencies below are CORE cycles; ix86_adjust_cost converts
;; dependence costs to issue opportunities, including the measured 1-cycle
;; AGI (not the old, unmeasured 3-cycle estimate).  Four-cycle vector chains
;; therefore need one accumulator at four threads, two at one/two threads.
;; Each opportunity can issue U then V.  Vector and x87 require U; ordinary
;; scalar and mask instructions can use either pipe.  Two vectors never pair.

(define_automaton "knc")
(define_cpu_unit "knc-u,knc-v,knc-fp" "knc")
(final_presence_set "knc-v" "knc-u")

;; Explicit overrides cover instructions whose shared x86 type is too broad.
(define_attr "knc_special" "none,permute,convert_df,reciprocal,pair,gather,prefetch"
  (const_string "none"))

;; A software prefetch takes a scalar slot: C2 (S8-01) measured vprefetch0
;; pairing with a vector op at no cost.
(define_attr "knc_pipe" "scalar,vector,fp,serial"
  (cond [(eq_attr "knc_special" "prefetch") (const_string "scalar")
         (eq_attr "type" "mskmov,msklog") (const_string "scalar")
         (eq_attr "unit" "sse") (const_string "vector")
         (eq_attr "unit" "i387") (const_string "fp")
         (eq_attr "type" "alu,alu1,negnot,imov,imovx,lea,incdec,ishift,ishift1,rotate,rotate1,icmp,test,ibr,setcc,icmov")
           (const_string "scalar")]
        (const_string "serial")))

;; The two-instruction unpack/pack stays visible as two U issues.  There is
;; no exposed scalar slot between the instructions inside this RTL insn.
(define_insn_reservation "knc_pair" 24
  (and (eq_attr "cpu" "knc") (eq_attr "knc_special" "pair"))
  "(knc-u+knc-v)*2")

;; One-line gather is the optimistic measured case.  Reserve both pipes
;; for the opaque retry loop and at least seven core-throughput slots;
;; the vector cost model separately charges for scattered cache lines.
(define_insn_reservation "knc_gather" 15
  (and (eq_attr "cpu" "knc") (eq_attr "knc_special" "gather"))
  "(knc-u+knc-v)*7")

(define_insn_reservation "knc_vector_mem" 12
  (and (eq_attr "cpu" "knc")
       (and (eq_attr "knc_special" "none,permute,convert_df,reciprocal")
            (and (eq_attr "knc_pipe" "vector") (eq_attr "memory" "!none"))))
  "knc-u")

(define_insn_reservation "knc_vector_long" 6
  (and (eq_attr "cpu" "knc")
       (and (eq_attr "knc_special" "permute,reciprocal")
            (eq_attr "memory" "none")))
  "knc-u")

(define_insn_reservation "knc_convert_df" 7
  (and (eq_attr "cpu" "knc")
       (and (eq_attr "knc_special" "convert_df") (eq_attr "memory" "none")))
  "knc-u")

(define_insn_reservation "knc_vector" 4
  (and (eq_attr "cpu" "knc")
       (and (eq_attr "knc_special" "none")
            (and (eq_attr "knc_pipe" "vector") (eq_attr "memory" "none"))))
  "knc-u")

;; x87 multiply sustains one per two core cycles; divide is unpipelined.
;; With all threads sharing the FPU these throughput limits remain two
;; and 38 opportunities per thread.  Other U instructions may overlap.
(define_insn_reservation "knc_fmul" 3
  (and (eq_attr "cpu" "knc") (eq_attr "type" "fmul"))
  "(knc-u+knc-fp),knc-fp")
(define_insn_reservation "knc_fdiv" 38
  (and (eq_attr "cpu" "knc") (eq_attr "type" "fdiv"))
  "(knc-u+knc-fp),knc-fp*37")
(define_insn_reservation "knc_fp" 3
  (and (eq_attr "cpu" "knc")
       (and (eq_attr "knc_pipe" "fp") (eq_attr "type" "!fmul,fdiv")))
  "knc-u+knc-fp")

(define_insn_reservation "knc_scalar_load" 3
  (and (eq_attr "cpu" "knc")
       (and (eq_attr "knc_pipe" "scalar") (eq_attr "memory" "load,both")))
  "knc-u|knc-v")
(define_insn_reservation "knc_scalar" 2
  (and (eq_attr "cpu" "knc")
       (and (eq_attr "knc_pipe" "scalar") (eq_attr "memory" "none,store")))
  "knc-u|knc-v")

;; Uncharacterized complex instructions cannot claim a free pairing slot.
(define_insn_reservation "knc_serial" 4
  (and (eq_attr "cpu" "knc")
       (and (eq_attr "knc_pipe" "serial") (eq_attr "knc_special" "none")))
  "knc-u+knc-v")
