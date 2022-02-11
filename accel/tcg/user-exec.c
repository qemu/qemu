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
#include "tcg/tcg-ldst.h"
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

static int probe_access_internal(CPUArchState *env, target_ulong addr,
                                 int fault_size, MMUAccessType access_type,
                                 bool nonfault, uintptr_t ra)
{
    int acc_flag;
    bool maperr;

    switch (access_type) {
    case MMU_DATA_STORE:
        acc_flag = PAGE_WRITE_ORG;
        break;
    case MMU_DATA_LOAD:
        acc_flag = PAGE_READ;
        break;
    case MMU_INST_FETCH:
        acc_flag = PAGE_EXEC;
        break;
    default:
        g_assert_not_reached();
    }

    if (guest_addr_valid_untagged(addr)) {
        int page_flags = page_get_flags(addr);
        if (page_flags & acc_flag) {
            return 0; /* success */
        }
        maperr = !(page_flags & PAGE_VALID);
    } else {
        maperr = true;
    }

    if (nonfault) {
        return TLB_INVALID_MASK;
    }

    cpu_loop_exit_sigsegv(env_cpu(env), addr, access_type, maperr, ra);
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

void helper_unaligned_ld(CPUArchState *env, target_ulong addr)
{
    cpu_loop_exit_sigbus(env_cpu(env), addr, MMU_DATA_LOAD, GETPC());
}

void helper_unaligned_st(CPUArchState *env, target_ulong addr)
{
    cpu_loop_exit_sigbus(env_cpu(env), addr, MMU_DATA_STORE, GETPC());
}

static void *cpu_mmu_lookup(CPUArchState *env, target_ulong addr,
                            MemOpIdx oi, uintptr_t ra, MMUAccessType type)
{
    MemOp mop = get_memop(oi);
    int a_bits = get_alignment_bits(mop);
    void *ret;

    /* Enforce guest required alignment.  */
    if (unlikely(addr & ((1 << a_bits) - 1))) {
        cpu_loop_exit_sigbus(env_cpu(env), addr, type, ra);
    }

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

    validate_memop(oi, MO_BEUQ);
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

    validate_memop(oi, MO_LEUQ);
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
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stl_be_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

void cpu_stq_be_mmu(CPUArchState *env, abi_ptr addr, uint64_t val,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_BEUQ);
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
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stl_le_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

void cpu_stq_le_mmu(CPUArchState *env, abi_ptr addr, uint64_t val,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_LEUQ);
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
    MemOp mop = get_memop(oi);
    int a_bits = get_alignment_bits(mop);
    void *ret;

    /* Enforce guest required alignment.  */
    if (unlikely(addr & ((1 << a_bits) - 1))) {
        MMUAccessType t = prot == PAGE_READ ? MMU_DATA_LOAD : MMU_DATA_STORE;
        cpu_loop_exit_sigbus(env_cpu(env), addr, t, retaddr);
    }

    /* Enforce qemu required alignment.  */
    if (unlikely(addr & (size - 1))) {
        cpu_loop_exit_atomic(env_cpu(env), retaddr);
    }

    ret = g2h(env_cpu(env), addr);
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
