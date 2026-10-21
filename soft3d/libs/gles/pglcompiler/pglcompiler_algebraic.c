/* Focused nir_opt_algebraic() for the on-device V3D 7.1 compiler.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * Upstream mesa generates nir_opt_algebraic.c from nir_opt_algebraic.py: a
 * ~49 MB / 3.76 M-line translation unit that is impractical to vendor or to
 * compile on the Pi5. v3dcompiler_stubs.c therefore leaves the five *other*
 * algebraic entry points (nir_opt_algebraic_late, _before_ffma, ...) as no-ops,
 * which is fine because those are pure optimisations.
 *
 * nir_opt_algebraic() itself is NOT safe to no-op for the broadcom backend,
 * though. The v3d gallium driver has no native QPU instruction for fdiv, fsqrt,
 * fpow, fmod/frem, ffract, flrp or ffma - nir_to_vir.c's ntq_emit_alu() hits
 * `mesa_loge("Unknown NIR ALU inst ..."); abort();` on every one of them. On a
 * normal mesa build those opcodes are rewritten away by the algebraic rules
 * (gated by the lower_fdiv/lower_fsqrt/lower_fpow/lower_fmod/lower_ffract
 * options) before the backend ever sees them. With the pass stubbed out they
 * survive straight into the backend and abort the compile.
 *
 * So this file provides a *minimal, correctness-preserving* nir_opt_algebraic()
 * that lowers exactly the opcodes the v3d backend cannot emit, into primitives
 * it can:
 *
 *   ffma(a,b,c)      -> fadd(fmul(a,b), c)          [V3D has no fused mul-add]
 *   ffma_weak(a,b,c) -> fadd(fmul(a,b), c)
 *   fdiv(a,b)        -> fmul(a, frcp(b))            [frcp  -> vir_RECIP]
 *   fsqrt(a)         -> fmul(a, frsq(a))            [frsq  -> vir_RSQRT]
 *   fpow(a,b)        -> fexp2(fmul(flog2(fabs(a)), b))   [fexp2/flog2 native]
 *   fmod(a,b)        -> fsub(a, fmul(b, ffloor(fdiv(a,b))))
 *   frem(a,b)        -> fsub(a, fmul(b, ftrunc(fdiv(a,b))))
 *   ffract(a)        -> fsub(a, ffloor(a))
 *   flrp(a,b,c)      -> fadd(a, fmul(c, fsub(b,a)))
 *   fsat(a)          -> fmin(fmax(a,0),1)          [ONLY when lower_fsat]
 *   fsat_signed(a)   -> fmin(fmax(a,-1),1)         [ONLY when lower_fsat]
 *   unpack_32_2x16_split_x(a) -> u2u16(a)           [truncate low 16]
 *   unpack_32_2x16_split_y(a) -> u2u16(ushr(a,16))  [extract high 16]
 *
 * fsat lowering (V3D 4.2 / Pi4): the GLSL frontend's opt_algebraic folds a
 * clamp(x,0,1) into ir_unop_saturate, which glsl_to_nir turns into nir_op_fsat.
 * ntq_emit_alu() implements fsat with the saturating float unpack
 * (V3D71_QPU_UNPACK_SAT) and ASSERTS v3d_device_has_unpack_sat(), which is
 * false before V3D 4.5 - so on ver 42 an fsat that survives to the backend
 * aborts the compile.  Upstream mesa expands fsat back to fmin/fmax in the
 * generated generic nir_opt_algebraic under options->lower_fsat; this focused
 * pass is that generated pass's stand-in, so it performs the same expansion,
 * gated on the SAME lower_fsat option the frontend sets from the live device
 * (!v3d_be_has_unpack_sat()).  When lower_fsat is unset (V3D 7.1 / Pi5) fsat is
 * left intact so the backend keeps the single-instruction unpack_sat and
 * raspi5's emitted QPU is byte-for-byte unchanged.
 *
 * Every target opcode (fmul, fadd, fsub, frcp, frsq, fexp2, flog2, ffloor,
 * ftrunc, fabs) IS handled by ntq_emit_alu(), so a fixpoint of this pass leaves
 * only backend-supported ALU. fmod/frem introduce a fdiv that the next fixpoint
 * iteration rewrites to fmul+frcp, so the pass converges.
 *
 * WHERE IT RUNS
 * -------------
 * v3d_optimize_nir() (nir_to_vir.c) calls nir_opt_algebraic() inside a
 * do/while progress loop, immediately AFTER nir_lower_alu_to_scalar(). By that
 * point every vector ALU has been split to per-component scalars, so the
 * instructions seen here are scalar; the nir_builder helpers below are
 * vector-agnostic regardless. v3d_optimize_nir() runs twice per compile (once in
 * the *_early hook, once at the tail of v3d_attempt_compile after texture/idiv/
 * subgroup lowering), so opcodes re-introduced by those later passes are caught
 * too. This is the canonical position mesa itself uses, so no vendored file is
 * modified.
 *
 * This is platform-INDEPENDENT compiler code. Upstream pin
 * f45825a0c724d06f231a1210484fe9af71a3ffab. See VENDORED.txt.
 */
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"

static bool
lower_v3d_alu(nir_builder *b, nir_alu_instr *instr, UNUSED void *cb_data)
{
   nir_def *lowered = NULL;

   switch (instr->op) {
   case nir_op_ffma:
   case nir_op_ffma_weak:
   case nir_op_fdiv:
   case nir_op_fsqrt:
   case nir_op_fpow:
   case nir_op_fmod:
   case nir_op_frem:
   case nir_op_ffract:
   case nir_op_flrp:
   case nir_op_unpack_32_2x16_split_x:
   case nir_op_unpack_32_2x16_split_y:
      break;
   case nir_op_fsat:
   case nir_op_fsat_signed:
      /* Expand ONLY when the target cannot emit the saturating unpack (V3D 4.2);
       * otherwise leave fsat for the backend's unpack_sat so V3D 7.1 output is
       * unchanged.  options->lower_fsat is set by the frontend from
       * !v3d_be_has_unpack_sat() for the live-selected device version. */
      if (!b->shader->options || !b->shader->options->lower_fsat)
         return false;
      break;
   default:
      return false;
   }

   b->cursor = nir_before_instr(&instr->instr);
   /* Carry the source's float-controls (rounding/exactness) onto the lowered
    * ops so a precise-arith shader keeps its decoration where it still means
    * something. V3D has no exact divide/root anyway, so fdiv/fsqrt are lowered
    * unconditionally - matching what the v3d gallium driver does. */
   b->fp_math_ctrl = instr->fp_math_ctrl;

   nir_def *a = nir_ssa_for_alu_src(b, instr, 0);

   switch (instr->op) {
   case nir_op_ffma:
   case nir_op_ffma_weak: {
      nir_def *bm = nir_ssa_for_alu_src(b, instr, 1);
      nir_def *c  = nir_ssa_for_alu_src(b, instr, 2);
      lowered = nir_fadd(b, nir_fmul(b, a, bm), c);
      break;
   }
   case nir_op_fdiv: {
      nir_def *d = nir_ssa_for_alu_src(b, instr, 1);
      lowered = nir_fmul(b, a, nir_frcp(b, d));
      break;
   }
   case nir_op_fsqrt:
      lowered = nir_fmul(b, a, nir_frsq(b, a));
      break;
   case nir_op_fpow: {
      nir_def *e = nir_ssa_for_alu_src(b, instr, 1);
      lowered = nir_fexp2(b, nir_fmul(b, nir_flog2(b, nir_fabs(b, a)), e));
      break;
   }
   case nir_op_fmod:
   case nir_op_frem: {
      nir_def *d = nir_ssa_for_alu_src(b, instr, 1);
      nir_def *q = nir_fdiv(b, a, d); /* rewritten to fmul+frcp next iteration */
      nir_def *trunc = (instr->op == nir_op_fmod) ? nir_ffloor(b, q)
                                                   : nir_ftrunc(b, q);
      lowered = nir_fsub(b, a, nir_fmul(b, d, trunc));
      break;
   }
   case nir_op_ffract:
      lowered = nir_fsub(b, a, nir_ffloor(b, a));
      break;
   case nir_op_flrp: {
      nir_def *bm = nir_ssa_for_alu_src(b, instr, 1);
      nir_def *t  = nir_ssa_for_alu_src(b, instr, 2);
      lowered = nir_fadd(b, a, nir_fmul(b, t, nir_fsub(b, bm, a)));
      break;
   }
   case nir_op_fsat: {
      /* clamp(a, 0, 1) with bit-size-matched immediates (fsat may be f16/f32). */
      unsigned bs = instr->def.bit_size;
      lowered = nir_fmin(b,
                         nir_fmax(b, a, nir_imm_floatN_t(b, 0.0, bs)),
                         nir_imm_floatN_t(b, 1.0, bs));
      break;
   }
   case nir_op_fsat_signed: {
      /* clamp(a, -1, 1). */
      unsigned bs = instr->def.bit_size;
      lowered = nir_fmin(b,
                         nir_fmax(b, a, nir_imm_floatN_t(b, -1.0, bs)),
                         nir_imm_floatN_t(b, 1.0, bs));
      break;
   }
   case nir_op_unpack_32_2x16_split_x:
      /* Extract low 16 bits: equivalent to u2u16(a).  The backend handles
       * u2u16 from a 32-bit source as vir_MOV (truncation is implicit in the
       * QPU register file).  Generated by nir_lower_pack() splitting the vec2
       * unpack_32_2x16 that nir_lower_tex(packing_16) produces. */
      lowered = nir_u2u16(b, a);
      break;
   case nir_op_unpack_32_2x16_split_y:
      /* Extract high 16 bits: u2u16(ushr(a, 16)). */
      lowered = nir_u2u16(b, nir_ushr_imm(b, a, 16));
      break;
   default:
      return false;
   }

   nir_def_replace(&instr->def, lowered);
   return true;
}

bool
nir_opt_algebraic(nir_shader *shader)
{
   /* nir_metadata_none: the rewrites invalidate dominators/loops metadata, so
    * declare nothing preserved and let the pass manager recompute as needed. */
   return nir_shader_alu_pass(shader, lower_v3d_alu, nir_metadata_none, NULL);
}
