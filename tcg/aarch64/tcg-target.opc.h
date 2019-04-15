/* Target-specific opcodes for host vector expansion.  These will be
   emitted by tcg_expand_vec_op.  For those familiar with GCC internals,
   consider these to be UNSPEC with names.  */

DEF(aa64_sshl_vec, 1, 2, 0, IMPLVEC)
