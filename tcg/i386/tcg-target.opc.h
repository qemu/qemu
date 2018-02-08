/* Target-specific opcodes for host vector expansion.  These will be
   emitted by tcg_expand_vec_op.  For those familiar with GCC internals,
   consider these to be UNSPEC with names.  */

DEF(x86_shufps_vec, 1, 2, 1, IMPLVEC)
DEF(x86_vpblendvb_vec, 1, 3, 0, IMPLVEC)
DEF(x86_blend_vec, 1, 2, 1, IMPLVEC)
DEF(x86_packss_vec, 1, 2, 0, IMPLVEC)
DEF(x86_packus_vec, 1, 2, 0, IMPLVEC)
DEF(x86_psrldq_vec, 1, 1, 1, IMPLVEC)
DEF(x86_vperm2i128_vec, 1, 2, 1, IMPLVEC)
DEF(x86_punpckl_vec, 1, 2, 0, IMPLVEC)
DEF(x86_punpckh_vec, 1, 2, 0, IMPLVEC)
