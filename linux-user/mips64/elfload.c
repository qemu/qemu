#include "../mips/elfload.c"

/*
 * mips/elfload.c defines elf_core_copy_regs guarded by #ifndef TARGET_MIPS64.
 *
 * We must provide the mips64 version here.  We cannot use r->pt.regs[] because
 * when mips/elfload.c is #include'd above its "#include "target_elf.h"" resolves
 * to mips/target_elf.h (compiler searches the including file's directory first),
 * which pulls in mips/target_ptrace.h.  That struct has pad0[6] before regs[],
 * so r->pt.regs[i] writes to reserved[6+i] — offset by 6 from what the kernel
 * and glibc expect for the N64 ABI (EPC at reserved[34], not reserved[40]).
 *
 * Write directly to reserved[] using the mips64 N64 index layout:
 *   R0-R31 at reserved[0..31], LO at [32], HI at [33], EPC at [34].
 */
void elf_core_copy_regs(target_elf_gregset_t *r, const CPUMIPSState *env)
{
    /* R0 is always 0; r->reserved is zero-initialised by the caller */
    for (int i = 1; i < 32; i++) {
        r->reserved[i] = tswap64(env->active_tc.gpr[i]);
    }
    r->reserved[26] = 0;   /* k0 */
    r->reserved[27] = 0;   /* k1 */
    r->reserved[32] = tswap64(env->active_tc.LO[0]);
    r->reserved[33] = tswap64(env->active_tc.HI[0]);
    r->reserved[34] = tswap64(env->active_tc.PC);
    r->reserved[35] = tswap64(env->CP0_BadVAddr);
    r->reserved[36] = tswap64(env->CP0_Status);
    r->reserved[37] = tswap64(env->CP0_Cause);
}
