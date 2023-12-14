/*
 * Memory helpers that will be used by TCG generated code.
 *
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef TCG_LDST_H
#define TCG_LDST_H

/* Value zero-extended to tcg register size.  */
tcg_target_ulong helper_ldub_mmu(CPUArchState *env, uint64_t addr,
                                 MemOpIdx oi, uintptr_t retaddr);
tcg_target_ulong helper_lduw_mmu(CPUArchState *env, uint64_t addr,
                                 MemOpIdx oi, uintptr_t retaddr);
tcg_target_ulong helper_ldul_mmu(CPUArchState *env, uint64_t addr,
                                 MemOpIdx oi, uintptr_t retaddr);
uint64_t helper_ldq_mmu(CPUArchState *env, uint64_t addr,
                        MemOpIdx oi, uintptr_t retaddr);
Int128 helper_ld16_mmu(CPUArchState *env, uint64_t addr,
                       MemOpIdx oi, uintptr_t retaddr);

/* Value sign-extended to tcg register size.  */
tcg_target_ulong helper_ldsb_mmu(CPUArchState *env, uint64_t addr,
                                 MemOpIdx oi, uintptr_t retaddr);
tcg_target_ulong helper_ldsw_mmu(CPUArchState *env, uint64_t addr,
                                 MemOpIdx oi, uintptr_t retaddr);
tcg_target_ulong helper_ldsl_mmu(CPUArchState *env, uint64_t addr,
                                 MemOpIdx oi, uintptr_t retaddr);

/*
 * Value extended to at least uint32_t, so that some ABIs do not require
 * zero-extension from uint8_t or uint16_t.
 */
void helper_stb_mmu(CPUArchState *env, uint64_t addr, uint32_t val,
                    MemOpIdx oi, uintptr_t retaddr);
void helper_stw_mmu(CPUArchState *env, uint64_t addr, uint32_t val,
                    MemOpIdx oi, uintptr_t retaddr);
void helper_stl_mmu(CPUArchState *env, uint64_t addr, uint32_t val,
                    MemOpIdx oi, uintptr_t retaddr);
void helper_stq_mmu(CPUArchState *env, uint64_t addr, uint64_t val,
                    MemOpIdx oi, uintptr_t retaddr);
void helper_st16_mmu(CPUArchState *env, uint64_t addr, Int128 val,
                     MemOpIdx oi, uintptr_t retaddr);

#endif /* TCG_LDST_H */
