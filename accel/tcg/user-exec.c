/*
 *  User emulator execution
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "hw/core/tcg-cpu-ops.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"
#include "qemu/bitops.h"
#include "exec/cpu_ldst.h"
#include "exec/translate-all.h"
#include "exec/helper-proto.h"
#include "qemu/atomic128.h"
#include "trace/trace-root.h"
#include "internal.h"

__thread uintptr_t helper_retaddr;

//#define DEBUG_SIGNAL

/*
 * Adjust the pc to pass to cpu_restore_state; return the memop type.
 */
MMUAccessType adjust_signal_pc(uintptr_t *pc, bool is_write)
{
    switch (helper_retaddr) {
    default:
        /*
         * Fault during host memory operation within a helper function.
         * The helper's host return address, saved here, gives us a
         * pointer into the generated code that will unwind to the
         * correct guest pc.
         */
        *pc = helper_retaddr;
        break;

    case 0:
        /*
         * Fault during host memory operation within generated code.
         * (Or, a unrelated bug within qemu, but we can't tell from here).
         *
         * We take the host pc from the signal frame.  However, we cannot
         * use that value directly.  Within cpu_restore_state_from_tb, we
         * assume PC comes from GETPC(), as used by the helper functions,
         * so we adjust the address by -GETPC_ADJ to form an address that
         * is within the call insn, so that the address does not accidentally
         * match the beginning of the next guest insn.  However, when the
         * pc comes from the signal frame it points to the actual faulting
         * host memory insn and not the return from a call insn.
         *
         * Therefore, adjust to compensate for what will be done later
         * by cpu_restore_state_from_tb.
         */
        *pc += GETPC_ADJ;
        break;

    case 1:
        /*
         * Fault during host read for translation, or loosely, "execution".
         *
         * The guest pc is already pointing to the start of the TB for which
         * code is being generated.  If the guest translator manages the
         * page crossings correctly, this is exactly the correct address
         * (and if the translator doesn't handle page boundaries correctly
         * there's little we can do about that here).  Therefore, do not
         * trigger the unwinder.
         *
         * Like tb_gen_code, release the memory lock before cpu_loop_exit.
         */
        mmap_unlock();
        *pc = 0;
        return MMU_INST_FETCH;
    }

    return is_write ? MMU_DATA_STORE : MMU_DATA_LOAD;
}

/**
 * handle_sigsegv_accerr_write:
 * @cpu: the cpu context
 * @old_set: the sigset_t from the signal ucontext_t
 * @host_pc: the host pc, adjusted for the signal
 * @guest_addr: the guest address of the fault
 *
 * Return true if the write fault has been handled, and should be re-tried.
 *
 * Note that it is important that we don't call page_unprotect() unless
 * this is really a "write to nonwriteable page" fault, because
 * page_unprotect() assumes that if it is called for an access to
 * a page that's writeable this means we had two threads racing and
 * another thread got there first and already made the page writeable;
 * so we will retry the access. If we were to call page_unprotect()
 * for some other kind of fault that should really be passed to the
 * guest, we'd end up in an infinite loop of retrying the faulting access.
 */
bool handle_sigsegv_accerr_write(CPUState *cpu, sigset_t *old_set,
                                 uintptr_t host_pc, abi_ptr guest_addr)
{
    switch (page_unprotect(guest_addr, host_pc)) {
    case 0:
        /*
         * Fault not caused by a page marked unwritable to protect
         * cached translations, must be the guest binary's problem.
         */
        return false;
    case 1:
        /*
         * Fault caused by protection of cached translation; TBs
         * invalidated, so resume execution.
         */
        return true;
    case 2:
        /*
         * Fault caused by protection of cached translation, and the
         * currently executing TB was modified and must be exited immediately.
         */
        sigprocmask(SIG_SETMASK, old_set, NULL);
        cpu_loop_exit_noexc(cpu);
        /* NORETURN */
    default:
        g_assert_not_reached();
    }
}

/*
 * 'pc' is the host PC at which the exception was raised.
 * 'address' is the effective address of the memory exception.
 * 'is_write' is 1 if a write caused the exception and otherwise 0.
 * 'old_set' is the signal set which should be restored.
 */
static inline int handle_cpu_signal(uintptr_t pc, siginfo_t *info,
                                    int is_write, sigset_t *old_set)
{
    CPUState *cpu = current_cpu;
    CPUClass *cc;
    unsigned long host_addr = (unsigned long)info->si_addr;
    MMUAccessType access_type = adjust_signal_pc(&pc, is_write);
    abi_ptr guest_addr;

    /* For synchronous signals we expect to be coming from the vCPU
     * thread (so current_cpu should be valid) and either from running
     * code or during translation which can fault as we cross pages.
     *
     * If neither is true then something has gone wrong and we should
     * abort rather than try and restart the vCPU execution.
     */
    if (!cpu || !cpu->running) {
        printf("qemu:%s received signal outside vCPU context @ pc=0x%"
               PRIxPTR "\n",  __func__, pc);
        abort();
    }

#if defined(DEBUG_SIGNAL)
    printf("qemu: SIGSEGV pc=0x%08lx address=%08lx w=%d oldset=0x%08lx\n",
           pc, host_addr, is_write, *(unsigned long *)old_set);
#endif

    /* Convert forcefully to guest address space, invalid addresses
       are still valid segv ones */
    guest_addr = h2g_nocheck(host_addr);

    /* XXX: locking issue */
    if (is_write &&
        info->si_signo == SIGSEGV &&
        info->si_code == SEGV_ACCERR &&
        h2g_valid(host_addr) &&
        handle_sigsegv_accerr_write(cpu, old_set, pc, guest_addr)) {
        return 1;
    }

    /*
     * There is no way the target can handle this other than raising
     * an exception.  Undo signal and retaddr state prior to longjmp.
     */
    sigprocmask(SIG_SETMASK, old_set, NULL);

    cc = CPU_GET_CLASS(cpu);
    cc->tcg_ops->tlb_fill(cpu, guest_addr, 0, access_type,
                          MMU_USER_IDX, false, pc);
    g_assert_not_reached();
}

static int probe_access_internal(CPUArchState *env, target_ulong addr,
                                 int fault_size, MMUAccessType access_type,
                                 bool nonfault, uintptr_t ra)
{
    int flags;

    switch (access_type) {
    case MMU_DATA_STORE:
        flags = PAGE_WRITE;
        break;
    case MMU_DATA_LOAD:
        flags = PAGE_READ;
        break;
    case MMU_INST_FETCH:
        flags = PAGE_EXEC;
        break;
    default:
        g_assert_not_reached();
    }

    if (!guest_addr_valid_untagged(addr) ||
        page_check_range(addr, 1, flags) < 0) {
        if (nonfault) {
            return TLB_INVALID_MASK;
        } else {
            CPUState *cpu = env_cpu(env);
            CPUClass *cc = CPU_GET_CLASS(cpu);
            cc->tcg_ops->tlb_fill(cpu, addr, fault_size, access_type,
                                  MMU_USER_IDX, false, ra);
            g_assert_not_reached();
        }
    }
    return 0;
}

int probe_access_flags(CPUArchState *env, target_ulong addr,
                       MMUAccessType access_type, int mmu_idx,
                       bool nonfault, void **phost, uintptr_t ra)
{
    int flags;

    flags = probe_access_internal(env, addr, 0, access_type, nonfault, ra);
    *phost = flags ? NULL : g2h(env_cpu(env), addr);
    return flags;
}

void *probe_access(CPUArchState *env, target_ulong addr, int size,
                   MMUAccessType access_type, int mmu_idx, uintptr_t ra)
{
    int flags;

    g_assert(-(addr | TARGET_PAGE_MASK) >= size);
    flags = probe_access_internal(env, addr, size, access_type, false, ra);
    g_assert(flags == 0);

    return size ? g2h(env_cpu(env), addr) : NULL;
}

#if defined(__s390__)

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    ucontext_t *uc = puc;
    unsigned long pc;
    uint16_t *pinsn;
    int is_write = 0;

    pc = uc->uc_mcontext.psw.addr;

    /*
     * ??? On linux, the non-rt signal handler has 4 (!) arguments instead
     * of the normal 2 arguments.  The 4th argument contains the "Translation-
     * Exception Identification for DAT Exceptions" from the hardware (aka
     * "int_parm_long"), which does in fact contain the is_write value.
     * The rt signal handler, as far as I can tell, does not give this value
     * at all.  Not that we could get to it from here even if it were.
     * So fall back to parsing instructions.  Treat read-modify-write ones as
     * writes, which is not fully correct, but for tracking self-modifying code
     * this is better than treating them as reads.  Checking si_addr page flags
     * might be a viable improvement, albeit a racy one.
     */
    /* ??? This is not even close to complete.  */
    pinsn = (uint16_t *)pc;
    switch (pinsn[0] >> 8) {
    case 0x50: /* ST */
    case 0x42: /* STC */
    case 0x40: /* STH */
    case 0xba: /* CS */
    case 0xbb: /* CDS */
        is_write = 1;
        break;
    case 0xc4: /* RIL format insns */
        switch (pinsn[0] & 0xf) {
        case 0xf: /* STRL */
        case 0xb: /* STGRL */
        case 0x7: /* STHRL */
            is_write = 1;
        }
        break;
    case 0xc8: /* SSF format insns */
        switch (pinsn[0] & 0xf) {
        case 0x2: /* CSST */
            is_write = 1;
        }
        break;
    case 0xe3: /* RXY format insns */
        switch (pinsn[2] & 0xff) {
        case 0x50: /* STY */
        case 0x24: /* STG */
        case 0x72: /* STCY */
        case 0x70: /* STHY */
        case 0x8e: /* STPQ */
        case 0x3f: /* STRVH */
        case 0x3e: /* STRV */
        case 0x2f: /* STRVG */
            is_write = 1;
        }
        break;
    case 0xeb: /* RSY format insns */
        switch (pinsn[2] & 0xff) {
        case 0x14: /* CSY */
        case 0x30: /* CSG */
        case 0x31: /* CDSY */
        case 0x3e: /* CDSG */
        case 0xe4: /* LANG */
        case 0xe6: /* LAOG */
        case 0xe7: /* LAXG */
        case 0xe8: /* LAAG */
        case 0xea: /* LAALG */
        case 0xf4: /* LAN */
        case 0xf6: /* LAO */
        case 0xf7: /* LAX */
        case 0xfa: /* LAAL */
        case 0xf8: /* LAA */
            is_write = 1;
        }
        break;
    }

    return handle_cpu_signal(pc, info, is_write, &uc->uc_sigmask);
}

#elif defined(__mips__)

#if defined(__misp16) || defined(__mips_micromips)
#error "Unsupported encoding"
#endif

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    ucontext_t *uc = puc;
    uintptr_t pc = uc->uc_mcontext.pc;
    uint32_t insn = *(uint32_t *)pc;
    int is_write = 0;

    /* Detect all store instructions at program counter. */
    switch((insn >> 26) & 077) {
    case 050: /* SB */
    case 051: /* SH */
    case 052: /* SWL */
    case 053: /* SW */
    case 054: /* SDL */
    case 055: /* SDR */
    case 056: /* SWR */
    case 070: /* SC */
    case 071: /* SWC1 */
    case 074: /* SCD */
    case 075: /* SDC1 */
    case 077: /* SD */
#if !defined(__mips_isa_rev) || __mips_isa_rev < 6
    case 072: /* SWC2 */
    case 076: /* SDC2 */
#endif
        is_write = 1;
        break;
    case 023: /* COP1X */
        /* Required in all versions of MIPS64 since
           MIPS64r1 and subsequent versions of MIPS32r2. */
        switch (insn & 077) {
        case 010: /* SWXC1 */
        case 011: /* SDXC1 */
        case 015: /* SUXC1 */
            is_write = 1;
        }
        break;
    }

    return handle_cpu_signal(pc, info, is_write, &uc->uc_sigmask);
}

#elif defined(__riscv)

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    ucontext_t *uc = puc;
    greg_t pc = uc->uc_mcontext.__gregs[REG_PC];
    uint32_t insn = *(uint32_t *)pc;
    int is_write = 0;

    /* Detect store by reading the instruction at the program
       counter. Note: we currently only generate 32-bit
       instructions so we thus only detect 32-bit stores */
    switch (((insn >> 0) & 0b11)) {
    case 3:
        switch (((insn >> 2) & 0b11111)) {
        case 8:
            switch (((insn >> 12) & 0b111)) {
            case 0: /* sb */
            case 1: /* sh */
            case 2: /* sw */
            case 3: /* sd */
            case 4: /* sq */
                is_write = 1;
                break;
            default:
                break;
            }
            break;
        case 9:
            switch (((insn >> 12) & 0b111)) {
            case 2: /* fsw */
            case 3: /* fsd */
            case 4: /* fsq */
                is_write = 1;
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

    /* Check for compressed instructions */
    switch (((insn >> 13) & 0b111)) {
    case 7:
        switch (insn & 0b11) {
        case 0: /*c.sd */
        case 2: /* c.sdsp */
            is_write = 1;
            break;
        default:
            break;
        }
        break;
    case 6:
        switch (insn & 0b11) {
        case 0: /* c.sw */
        case 3: /* c.swsp */
            is_write = 1;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    return handle_cpu_signal(pc, info, is_write, &uc->uc_sigmask);
}
#endif

/* The softmmu versions of these helpers are in cputlb.c.  */

/*
 * Verify that we have passed the correct MemOp to the correct function.
 *
 * We could present one function to target code, and dispatch based on
 * the MemOp, but so far we have worked hard to avoid an indirect function
 * call along the memory path.
 */
static void validate_memop(MemOpIdx oi, MemOp expected)
{
#ifdef CONFIG_DEBUG_TCG
    MemOp have = get_memop(oi) & (MO_SIZE | MO_BSWAP);
    assert(have == expected);
#endif
}

static void *cpu_mmu_lookup(CPUArchState *env, target_ulong addr,
                            MemOpIdx oi, uintptr_t ra, MMUAccessType type)
{
    void *ret;

    /* TODO: Enforce guest required alignment.  */

    ret = g2h(env_cpu(env), addr);
    set_helper_retaddr(ra);
    return ret;
}

uint8_t cpu_ldb_mmu(CPUArchState *env, abi_ptr addr,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    uint8_t ret;

    validate_memop(oi, MO_UB);
    trace_guest_ld_before_exec(env_cpu(env), addr, oi);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_LOAD);
    ret = ldub_p(haddr);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
    return ret;
}

uint16_t cpu_ldw_be_mmu(CPUArchState *env, abi_ptr addr,
                        MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    uint16_t ret;

    validate_memop(oi, MO_BEUW);
    trace_guest_ld_before_exec(env_cpu(env), addr, oi);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_LOAD);
    ret = lduw_be_p(haddr);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
    return ret;
}

uint32_t cpu_ldl_be_mmu(CPUArchState *env, abi_ptr addr,
                        MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    uint32_t ret;

    validate_memop(oi, MO_BEUL);
    trace_guest_ld_before_exec(env_cpu(env), addr, oi);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_LOAD);
    ret = ldl_be_p(haddr);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
    return ret;
}

uint64_t cpu_ldq_be_mmu(CPUArchState *env, abi_ptr addr,
                        MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    uint64_t ret;

    validate_memop(oi, MO_BEQ);
    trace_guest_ld_before_exec(env_cpu(env), addr, oi);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_LOAD);
    ret = ldq_be_p(haddr);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
    return ret;
}

uint16_t cpu_ldw_le_mmu(CPUArchState *env, abi_ptr addr,
                        MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    uint16_t ret;

    validate_memop(oi, MO_LEUW);
    trace_guest_ld_before_exec(env_cpu(env), addr, oi);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_LOAD);
    ret = lduw_le_p(haddr);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
    return ret;
}

uint32_t cpu_ldl_le_mmu(CPUArchState *env, abi_ptr addr,
                        MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    uint32_t ret;

    validate_memop(oi, MO_LEUL);
    trace_guest_ld_before_exec(env_cpu(env), addr, oi);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_LOAD);
    ret = ldl_le_p(haddr);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
    return ret;
}

uint64_t cpu_ldq_le_mmu(CPUArchState *env, abi_ptr addr,
                        MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    uint64_t ret;

    validate_memop(oi, MO_LEQ);
    trace_guest_ld_before_exec(env_cpu(env), addr, oi);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_LOAD);
    ret = ldq_le_p(haddr);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
    return ret;
}

void cpu_stb_mmu(CPUArchState *env, abi_ptr addr, uint8_t val,
                 MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_UB);
    trace_guest_st_before_exec(env_cpu(env), addr, oi);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stb_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

void cpu_stw_be_mmu(CPUArchState *env, abi_ptr addr, uint16_t val,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_BEUW);
    trace_guest_st_before_exec(env_cpu(env), addr, oi);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stw_be_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

void cpu_stl_be_mmu(CPUArchState *env, abi_ptr addr, uint32_t val,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_BEUL);
    trace_guest_st_before_exec(env_cpu(env), addr, oi);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stl_be_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

void cpu_stq_be_mmu(CPUArchState *env, abi_ptr addr, uint64_t val,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_BEQ);
    trace_guest_st_before_exec(env_cpu(env), addr, oi);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stq_be_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

void cpu_stw_le_mmu(CPUArchState *env, abi_ptr addr, uint16_t val,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_LEUW);
    trace_guest_st_before_exec(env_cpu(env), addr, oi);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stw_le_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

void cpu_stl_le_mmu(CPUArchState *env, abi_ptr addr, uint32_t val,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_LEUL);
    trace_guest_st_before_exec(env_cpu(env), addr, oi);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stl_le_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

void cpu_stq_le_mmu(CPUArchState *env, abi_ptr addr, uint64_t val,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_LEQ);
    trace_guest_st_before_exec(env_cpu(env), addr, oi);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stq_le_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

uint32_t cpu_ldub_code(CPUArchState *env, abi_ptr ptr)
{
    uint32_t ret;

    set_helper_retaddr(1);
    ret = ldub_p(g2h_untagged(ptr));
    clear_helper_retaddr();
    return ret;
}

uint32_t cpu_lduw_code(CPUArchState *env, abi_ptr ptr)
{
    uint32_t ret;

    set_helper_retaddr(1);
    ret = lduw_p(g2h_untagged(ptr));
    clear_helper_retaddr();
    return ret;
}

uint32_t cpu_ldl_code(CPUArchState *env, abi_ptr ptr)
{
    uint32_t ret;

    set_helper_retaddr(1);
    ret = ldl_p(g2h_untagged(ptr));
    clear_helper_retaddr();
    return ret;
}

uint64_t cpu_ldq_code(CPUArchState *env, abi_ptr ptr)
{
    uint64_t ret;

    set_helper_retaddr(1);
    ret = ldq_p(g2h_untagged(ptr));
    clear_helper_retaddr();
    return ret;
}

#include "ldst_common.c.inc"

/*
 * Do not allow unaligned operations to proceed.  Return the host address.
 *
 * @prot may be PAGE_READ, PAGE_WRITE, or PAGE_READ|PAGE_WRITE.
 */
static void *atomic_mmu_lookup(CPUArchState *env, target_ulong addr,
                               MemOpIdx oi, int size, int prot,
                               uintptr_t retaddr)
{
    /* Enforce qemu required alignment.  */
    if (unlikely(addr & (size - 1))) {
        cpu_loop_exit_atomic(env_cpu(env), retaddr);
    }
    void *ret = g2h(env_cpu(env), addr);
    set_helper_retaddr(retaddr);
    return ret;
}

#include "atomic_common.c.inc"

/*
 * First set of functions passes in OI and RETADDR.
 * This makes them callable from other helpers.
 */

#define ATOMIC_NAME(X) \
    glue(glue(glue(cpu_atomic_ ## X, SUFFIX), END), _mmu)
#define ATOMIC_MMU_CLEANUP do { clear_helper_retaddr(); } while (0)
#define ATOMIC_MMU_IDX MMU_USER_IDX

#define DATA_SIZE 1
#include "atomic_template.h"

#define DATA_SIZE 2
#include "atomic_template.h"

#define DATA_SIZE 4
#include "atomic_template.h"

#ifdef CONFIG_ATOMIC64
#define DATA_SIZE 8
#include "atomic_template.h"
#endif

#if HAVE_ATOMIC128 || HAVE_CMPXCHG128
#define DATA_SIZE 16
#include "atomic_template.h"
#endif
