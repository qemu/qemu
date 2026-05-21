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
    /*
     * linux-user/elfload.c allocates target_elf_prstatus using the
     * definition from mips64/target_elf.h, where target_elf_gregset_t
     * has target_ulong reserved[45] (8 bytes each = 360 bytes total).
     *
     * But in this compilation unit, "#include target_elf.h" resolved to
     * mips/target_elf.h (wrong directory), so our local target_elf_gregset_t
     * has abi_ulong reserved[45] which is only 4 bytes each for mipsn32.
     * Using r->reserved[i] would write to the wrong offsets for mipsn32.
     *
     * Cast to target_ulong * to always write 8-byte entries at the correct
     * positions, matching the layout that elfload.c allocated.
     */
    target_ulong *regs = (target_ulong *)r;

    /* R0 is always 0; buffer is zero-initialised by the caller */
    for (int i = 1; i < 32; i++) {
        regs[i] = tswap64(env->active_tc.gpr[i]);
    }
    regs[26] = 0;   /* k0 */
    regs[27] = 0;   /* k1 */
    regs[32] = tswap64(env->active_tc.LO[0]);
    regs[33] = tswap64(env->active_tc.HI[0]);
    regs[34] = tswap64(env->active_tc.PC);
    regs[35] = tswap64(env->CP0_BadVAddr);
    regs[36] = tswap64(env->CP0_Status);
    regs[37] = tswap64(env->CP0_Cause);
}
