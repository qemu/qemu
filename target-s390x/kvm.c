/*
 * QEMU S390x KVM implementation
 *
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 * Copyright IBM Corp. 2012
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * Contributions after 2012-10-29 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 *
 * You should have received a copy of the GNU (Lesser) General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/kvm.h>
#include <asm/ptrace.h>

#include "qemu-common.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "cpu.h"
#include "sysemu/device_tree.h"

/* #define DEBUG_KVM */

#ifdef DEBUG_KVM
#define dprintf(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

#define IPA0_DIAG                       0x8300
#define IPA0_SIGP                       0xae00
#define IPA0_B2                         0xb200
#define IPA0_B9                         0xb900
#define IPA0_EB                         0xeb00

#define PRIV_SCLP_CALL                  0x20
#define PRIV_CSCH                       0x30
#define PRIV_HSCH                       0x31
#define PRIV_MSCH                       0x32
#define PRIV_SSCH                       0x33
#define PRIV_STSCH                      0x34
#define PRIV_TSCH                       0x35
#define PRIV_TPI                        0x36
#define PRIV_SAL                        0x37
#define PRIV_RSCH                       0x38
#define PRIV_STCRW                      0x39
#define PRIV_STCPS                      0x3a
#define PRIV_RCHP                       0x3b
#define PRIV_SCHM                       0x3c
#define PRIV_CHSC                       0x5f
#define PRIV_SIGA                       0x74
#define PRIV_XSCH                       0x76
#define PRIV_SQBS                       0x8a
#define PRIV_EQBS                       0x9c
#define DIAG_KVM_HYPERCALL              0x500
#define DIAG_KVM_BREAKPOINT             0x501

#define ICPT_INSTRUCTION                0x04
#define ICPT_WAITPSW                    0x1c
#define ICPT_SOFT_INTERCEPT             0x24
#define ICPT_CPU_STOP                   0x28
#define ICPT_IO                         0x40

#define SIGP_RESTART                    0x06
#define SIGP_INITIAL_CPU_RESET          0x0b
#define SIGP_STORE_STATUS_ADDR          0x0e
#define SIGP_SET_ARCH                   0x12

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

static int cap_sync_regs;

int kvm_arch_init(KVMState *s)
{
    cap_sync_regs = kvm_check_extension(s, KVM_CAP_SYNC_REGS);
    return 0;
}

unsigned long kvm_arch_vcpu_id(CPUState *cpu)
{
    return cpu->cpu_index;
}

int kvm_arch_init_vcpu(CPUState *cpu)
{
    int ret = 0;

    if (kvm_vcpu_ioctl(cpu, KVM_S390_INITIAL_RESET, NULL) < 0) {
        perror("cannot init reset vcpu");
    }

    return ret;
}

void kvm_arch_reset_vcpu(CPUState *cpu)
{
    /* The initial reset call is needed here to reset in-kernel
     * vcpu data that we can't access directly from QEMU
     * (i.e. with older kernels which don't support sync_regs/ONE_REG).
     * Before this ioctl cpu_synchronize_state() is called in common kvm
     * code (kvm-all) */
    if (kvm_vcpu_ioctl(cpu, KVM_S390_INITIAL_RESET, NULL)) {
        perror("Can't reset vcpu\n");
    }
}

int kvm_arch_put_registers(CPUState *cs, int level)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    struct kvm_sregs sregs;
    struct kvm_regs regs;
    int ret;
    int i;

    /* always save the PSW  and the GPRS*/
    cs->kvm_run->psw_addr = env->psw.addr;
    cs->kvm_run->psw_mask = env->psw.mask;

    if (cap_sync_regs && cs->kvm_run->kvm_valid_regs & KVM_SYNC_GPRS) {
        for (i = 0; i < 16; i++) {
            cs->kvm_run->s.regs.gprs[i] = env->regs[i];
            cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_GPRS;
        }
    } else {
        for (i = 0; i < 16; i++) {
            regs.gprs[i] = env->regs[i];
        }
        ret = kvm_vcpu_ioctl(cs, KVM_SET_REGS, &regs);
        if (ret < 0) {
            return ret;
        }
    }

    /* Do we need to save more than that? */
    if (level == KVM_PUT_RUNTIME_STATE) {
        return 0;
    }

    if (cap_sync_regs &&
        cs->kvm_run->kvm_valid_regs & KVM_SYNC_ACRS &&
        cs->kvm_run->kvm_valid_regs & KVM_SYNC_CRS) {
        for (i = 0; i < 16; i++) {
            cs->kvm_run->s.regs.acrs[i] = env->aregs[i];
            cs->kvm_run->s.regs.crs[i] = env->cregs[i];
        }
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_ACRS;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_CRS;
    } else {
        for (i = 0; i < 16; i++) {
            sregs.acrs[i] = env->aregs[i];
            sregs.crs[i] = env->cregs[i];
        }
        ret = kvm_vcpu_ioctl(cs, KVM_SET_SREGS, &sregs);
        if (ret < 0) {
            return ret;
        }
    }

    /* Finally the prefix */
    if (cap_sync_regs && cs->kvm_run->kvm_valid_regs & KVM_SYNC_PREFIX) {
        cs->kvm_run->s.regs.prefix = env->psa;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_PREFIX;
    } else {
        /* prefix is only supported via sync regs */
    }
    return 0;
}

int kvm_arch_get_registers(CPUState *cs)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    struct kvm_sregs sregs;
    struct kvm_regs regs;
    int ret;
    int i;

    /* get the PSW */
    env->psw.addr = cs->kvm_run->psw_addr;
    env->psw.mask = cs->kvm_run->psw_mask;

    /* the GPRS */
    if (cap_sync_regs && cs->kvm_run->kvm_valid_regs & KVM_SYNC_GPRS) {
        for (i = 0; i < 16; i++) {
            env->regs[i] = cs->kvm_run->s.regs.gprs[i];
        }
    } else {
        ret = kvm_vcpu_ioctl(cs, KVM_GET_REGS, &regs);
        if (ret < 0) {
            return ret;
        }
         for (i = 0; i < 16; i++) {
            env->regs[i] = regs.gprs[i];
        }
    }

    /* The ACRS and CRS */
    if (cap_sync_regs &&
        cs->kvm_run->kvm_valid_regs & KVM_SYNC_ACRS &&
        cs->kvm_run->kvm_valid_regs & KVM_SYNC_CRS) {
        for (i = 0; i < 16; i++) {
            env->aregs[i] = cs->kvm_run->s.regs.acrs[i];
            env->cregs[i] = cs->kvm_run->s.regs.crs[i];
        }
    } else {
        ret = kvm_vcpu_ioctl(cs, KVM_GET_SREGS, &sregs);
        if (ret < 0) {
            return ret;
        }
         for (i = 0; i < 16; i++) {
            env->aregs[i] = sregs.acrs[i];
            env->cregs[i] = sregs.crs[i];
        }
    }

    /* Finally the prefix */
    if (cap_sync_regs && cs->kvm_run->kvm_valid_regs & KVM_SYNC_PREFIX) {
        env->psa = cs->kvm_run->s.regs.prefix;
    } else {
        /* no prefix without sync regs */
    }

    return 0;
}

/*
 * Legacy layout for s390:
 * Older S390 KVM requires the topmost vma of the RAM to be
 * smaller than an system defined value, which is at least 256GB.
 * Larger systems have larger values. We put the guest between
 * the end of data segment (system break) and this value. We
 * use 32GB as a base to have enough room for the system break
 * to grow. We also have to use MAP parameters that avoid
 * read-only mapping of guest pages.
 */
static void *legacy_s390_alloc(ram_addr_t size)
{
    void *mem;

    mem = mmap((void *) 0x800000000ULL, size,
               PROT_EXEC|PROT_READ|PROT_WRITE,
               MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (mem == MAP_FAILED) {
        fprintf(stderr, "Allocating RAM failed\n");
        abort();
    }
    return mem;
}

void *kvm_arch_vmalloc(ram_addr_t size)
{
    /* Can we use the standard allocation ? */
    if (kvm_check_extension(kvm_state, KVM_CAP_S390_GMAP) &&
        kvm_check_extension(kvm_state, KVM_CAP_S390_COW)) {
        return NULL;
    } else {
        return legacy_s390_alloc(size);
    }
}

int kvm_arch_insert_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    static const uint8_t diag_501[] = {0x83, 0x24, 0x05, 0x01};

    if (cpu_memory_rw_debug(env, bp->pc, (uint8_t *)&bp->saved_insn, 4, 0) ||
        cpu_memory_rw_debug(env, bp->pc, (uint8_t *)diag_501, 4, 1)) {
        return -EINVAL;
    }
    return 0;
}

int kvm_arch_remove_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    uint8_t t[4];
    static const uint8_t diag_501[] = {0x83, 0x24, 0x05, 0x01};

    if (cpu_memory_rw_debug(env, bp->pc, t, 4, 0)) {
        return -EINVAL;
    } else if (memcmp(t, diag_501, 4)) {
        return -EINVAL;
    } else if (cpu_memory_rw_debug(env, bp->pc, (uint8_t *)&bp->saved_insn, 1, 1)) {
        return -EINVAL;
    }

    return 0;
}

void kvm_arch_pre_run(CPUState *cpu, struct kvm_run *run)
{
}

void kvm_arch_post_run(CPUState *cpu, struct kvm_run *run)
{
}

int kvm_arch_process_async_events(CPUState *cs)
{
    S390CPU *cpu = S390_CPU(cs);
    return cpu->env.halted;
}

void kvm_s390_interrupt_internal(S390CPU *cpu, int type, uint32_t parm,
                                 uint64_t parm64, int vm)
{
    CPUState *cs = CPU(cpu);
    struct kvm_s390_interrupt kvmint;
    int r;

    if (!cs->kvm_state) {
        return;
    }

    kvmint.type = type;
    kvmint.parm = parm;
    kvmint.parm64 = parm64;

    if (vm) {
        r = kvm_vm_ioctl(cs->kvm_state, KVM_S390_INTERRUPT, &kvmint);
    } else {
        r = kvm_vcpu_ioctl(cs, KVM_S390_INTERRUPT, &kvmint);
    }

    if (r < 0) {
        fprintf(stderr, "KVM failed to inject interrupt\n");
        exit(1);
    }
}

void kvm_s390_virtio_irq(S390CPU *cpu, int config_change, uint64_t token)
{
    kvm_s390_interrupt_internal(cpu, KVM_S390_INT_VIRTIO, config_change,
                                token, 1);
}

void kvm_s390_interrupt(S390CPU *cpu, int type, uint32_t code)
{
    kvm_s390_interrupt_internal(cpu, type, code, 0, 0);
}

static void enter_pgmcheck(S390CPU *cpu, uint16_t code)
{
    kvm_s390_interrupt(cpu, KVM_S390_PROGRAM_INT, code);
}

static inline void setcc(S390CPU *cpu, uint64_t cc)
{
    CPUS390XState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    cs->kvm_run->psw_mask &= ~(3ull << 44);
    cs->kvm_run->psw_mask |= (cc & 3) << 44;

    env->psw.mask &= ~(3ul << 44);
    env->psw.mask |= (cc & 3) << 44;
}

static int kvm_sclp_service_call(S390CPU *cpu, struct kvm_run *run,
                                 uint16_t ipbh0)
{
    CPUS390XState *env = &cpu->env;
    uint32_t sccb;
    uint64_t code;
    int r = 0;

    cpu_synchronize_state(env);
    sccb = env->regs[ipbh0 & 0xf];
    code = env->regs[(ipbh0 & 0xf0) >> 4];

    r = sclp_service_call(sccb, code);
    if (r < 0) {
        enter_pgmcheck(cpu, -r);
    }
    setcc(cpu, r);

    return 0;
}

static int kvm_handle_css_inst(S390CPU *cpu, struct kvm_run *run,
                               uint8_t ipa0, uint8_t ipa1, uint8_t ipb)
{
    int r = 0;
    int no_cc = 0;
    CPUS390XState *env = &cpu->env;

    if (ipa0 != 0xb2) {
        /* Not handled for now. */
        return -1;
    }
    cpu_synchronize_state(env);
    switch (ipa1) {
    case PRIV_XSCH:
        r = ioinst_handle_xsch(env, env->regs[1]);
        break;
    case PRIV_CSCH:
        r = ioinst_handle_csch(env, env->regs[1]);
        break;
    case PRIV_HSCH:
        r = ioinst_handle_hsch(env, env->regs[1]);
        break;
    case PRIV_MSCH:
        r = ioinst_handle_msch(env, env->regs[1], run->s390_sieic.ipb);
        break;
    case PRIV_SSCH:
        r = ioinst_handle_ssch(env, env->regs[1], run->s390_sieic.ipb);
        break;
    case PRIV_STCRW:
        r = ioinst_handle_stcrw(env, run->s390_sieic.ipb);
        break;
    case PRIV_STSCH:
        r = ioinst_handle_stsch(env, env->regs[1], run->s390_sieic.ipb);
        break;
    case PRIV_TSCH:
        /* We should only get tsch via KVM_EXIT_S390_TSCH. */
        fprintf(stderr, "Spurious tsch intercept\n");
        break;
    case PRIV_CHSC:
        r = ioinst_handle_chsc(env, run->s390_sieic.ipb);
        break;
    case PRIV_TPI:
        /* This should have been handled by kvm already. */
        fprintf(stderr, "Spurious tpi intercept\n");
        break;
    case PRIV_SCHM:
        no_cc = 1;
        r = ioinst_handle_schm(env, env->regs[1], env->regs[2],
                               run->s390_sieic.ipb);
        break;
    case PRIV_RSCH:
        r = ioinst_handle_rsch(env, env->regs[1]);
        break;
    case PRIV_RCHP:
        r = ioinst_handle_rchp(env, env->regs[1]);
        break;
    case PRIV_STCPS:
        /* We do not provide this instruction, it is suppressed. */
        no_cc = 1;
        r = 0;
        break;
    case PRIV_SAL:
        no_cc = 1;
        r = ioinst_handle_sal(env, env->regs[1]);
        break;
    default:
        r = -1;
        break;
    }

    if (r >= 0) {
        if (!no_cc) {
            setcc(cpu, r);
        }
        r = 0;
    } else if (r < -1) {
        r = 0;
    }
    return r;
}

static int is_ioinst(uint8_t ipa0, uint8_t ipa1, uint8_t ipb)
{
    int ret = 0;
    uint16_t ipa = (ipa0 << 8) | ipa1;

    switch (ipa) {
    case IPA0_B2 | PRIV_CSCH:
    case IPA0_B2 | PRIV_HSCH:
    case IPA0_B2 | PRIV_MSCH:
    case IPA0_B2 | PRIV_SSCH:
    case IPA0_B2 | PRIV_STSCH:
    case IPA0_B2 | PRIV_TPI:
    case IPA0_B2 | PRIV_SAL:
    case IPA0_B2 | PRIV_RSCH:
    case IPA0_B2 | PRIV_STCRW:
    case IPA0_B2 | PRIV_STCPS:
    case IPA0_B2 | PRIV_RCHP:
    case IPA0_B2 | PRIV_SCHM:
    case IPA0_B2 | PRIV_CHSC:
    case IPA0_B2 | PRIV_SIGA:
    case IPA0_B2 | PRIV_XSCH:
    case IPA0_B9 | PRIV_EQBS:
    case IPA0_EB | PRIV_SQBS:
        ret = 1;
        break;
    }

    return ret;
}

static int handle_priv(S390CPU *cpu, struct kvm_run *run,
                       uint8_t ipa0, uint8_t ipa1)
{
    int r = 0;
    uint16_t ipbh0 = (run->s390_sieic.ipb & 0xffff0000) >> 16;
    uint8_t ipb = run->s390_sieic.ipb & 0xff;

    dprintf("KVM: PRIV: %d\n", ipa1);
    switch (ipa1) {
        case PRIV_SCLP_CALL:
            r = kvm_sclp_service_call(cpu, run, ipbh0);
            break;
        default:
            if (is_ioinst(ipa0, ipa1, ipb)) {
                r = kvm_handle_css_inst(cpu, run, ipa0, ipa1, ipb);
                if (r == -1) {
                    setcc(cpu, 3);
                    r = 0;
                }
            } else {
                dprintf("KVM: unknown PRIV: 0x%x\n", ipa1);
                r = -1;
            }
            break;
    }

    return r;
}

static int handle_hypercall(CPUS390XState *env, struct kvm_run *run)
{
    cpu_synchronize_state(env);
    env->regs[2] = s390_virtio_hypercall(env);

    return 0;
}

static int handle_diag(CPUS390XState *env, struct kvm_run *run, int ipb_code)
{
    int r = 0;

    switch (ipb_code) {
        case DIAG_KVM_HYPERCALL:
            r = handle_hypercall(env, run);
            break;
        case DIAG_KVM_BREAKPOINT:
            sleep(10);
            break;
        default:
            dprintf("KVM: unknown DIAG: 0x%x\n", ipb_code);
            r = -1;
            break;
    }

    return r;
}

static int s390_cpu_restart(S390CPU *cpu)
{
    kvm_s390_interrupt(cpu, KVM_S390_RESTART, 0);
    s390_add_running_cpu(cpu);
    qemu_cpu_kick(CPU(cpu));
    dprintf("DONE: SIGP cpu restart: %p\n", &cpu->env);
    return 0;
}

static int s390_store_status(CPUS390XState *env, uint32_t parameter)
{
    /* XXX */
    fprintf(stderr, "XXX SIGP store status\n");
    return -1;
}

static int s390_cpu_initial_reset(S390CPU *cpu)
{
    CPUS390XState *env = &cpu->env;
    int i;

    s390_del_running_cpu(cpu);
    if (kvm_vcpu_ioctl(CPU(cpu), KVM_S390_INITIAL_RESET, NULL) < 0) {
        perror("cannot init reset vcpu");
    }

    /* Manually zero out all registers */
    cpu_synchronize_state(env);
    for (i = 0; i < 16; i++) {
        env->regs[i] = 0;
    }

    dprintf("DONE: SIGP initial reset: %p\n", env);
    return 0;
}

static int handle_sigp(S390CPU *cpu, struct kvm_run *run, uint8_t ipa1)
{
    CPUS390XState *env = &cpu->env;
    uint8_t order_code;
    uint32_t parameter;
    uint16_t cpu_addr;
    uint8_t t;
    int r = -1;
    S390CPU *target_cpu;
    CPUS390XState *target_env;

    cpu_synchronize_state(env);

    /* get order code */
    order_code = run->s390_sieic.ipb >> 28;
    if (order_code > 0) {
        order_code = env->regs[order_code];
    }
    order_code += (run->s390_sieic.ipb & 0x0fff0000) >> 16;

    /* get parameters */
    t = (ipa1 & 0xf0) >> 4;
    if (!(t % 2)) {
        t++;
    }

    parameter = env->regs[t] & 0x7ffffe00;
    cpu_addr = env->regs[ipa1 & 0x0f];

    target_cpu = s390_cpu_addr2state(cpu_addr);
    if (target_cpu == NULL) {
        goto out;
    }
    target_env = &target_cpu->env;

    switch (order_code) {
        case SIGP_RESTART:
            r = s390_cpu_restart(target_cpu);
            break;
        case SIGP_STORE_STATUS_ADDR:
            r = s390_store_status(target_env, parameter);
            break;
        case SIGP_SET_ARCH:
            /* make the caller panic */
            return -1;
        case SIGP_INITIAL_CPU_RESET:
            r = s390_cpu_initial_reset(target_cpu);
            break;
        default:
            fprintf(stderr, "KVM: unknown SIGP: 0x%x\n", order_code);
            break;
    }

out:
    setcc(cpu, r ? 3 : 0);
    return 0;
}

static int handle_instruction(S390CPU *cpu, struct kvm_run *run)
{
    CPUS390XState *env = &cpu->env;
    unsigned int ipa0 = (run->s390_sieic.ipa & 0xff00);
    uint8_t ipa1 = run->s390_sieic.ipa & 0x00ff;
    int ipb_code = (run->s390_sieic.ipb & 0x0fff0000) >> 16;
    int r = -1;

    dprintf("handle_instruction 0x%x 0x%x\n", run->s390_sieic.ipa, run->s390_sieic.ipb);
    switch (ipa0) {
    case IPA0_B2:
    case IPA0_B9:
    case IPA0_EB:
        r = handle_priv(cpu, run, ipa0 >> 8, ipa1);
        break;
    case IPA0_DIAG:
        r = handle_diag(env, run, ipb_code);
        break;
    case IPA0_SIGP:
        r = handle_sigp(cpu, run, ipa1);
        break;
    }

    if (r < 0) {
        enter_pgmcheck(cpu, 0x0001);
    }
    return 0;
}

static bool is_special_wait_psw(CPUState *cs)
{
    /* signal quiesce */
    return cs->kvm_run->psw_addr == 0xfffUL;
}

static int handle_intercept(S390CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    struct kvm_run *run = cs->kvm_run;
    int icpt_code = run->s390_sieic.icptcode;
    int r = 0;

    dprintf("intercept: 0x%x (at 0x%lx)\n", icpt_code,
            (long)cs->kvm_run->psw_addr);
    switch (icpt_code) {
        case ICPT_INSTRUCTION:
            r = handle_instruction(cpu, run);
            break;
        case ICPT_WAITPSW:
            if (s390_del_running_cpu(cpu) == 0 &&
                is_special_wait_psw(cs)) {
                qemu_system_shutdown_request();
            }
            r = EXCP_HALTED;
            break;
        case ICPT_CPU_STOP:
            if (s390_del_running_cpu(cpu) == 0) {
                qemu_system_shutdown_request();
            }
            r = EXCP_HALTED;
            break;
        case ICPT_SOFT_INTERCEPT:
            fprintf(stderr, "KVM unimplemented icpt SOFT\n");
            exit(1);
            break;
        case ICPT_IO:
            fprintf(stderr, "KVM unimplemented icpt IO\n");
            exit(1);
            break;
        default:
            fprintf(stderr, "Unknown intercept code: %d\n", icpt_code);
            exit(1);
            break;
    }

    return r;
}

static int handle_tsch(S390CPU *cpu)
{
    CPUS390XState *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    struct kvm_run *run = cs->kvm_run;
    int ret;

    cpu_synchronize_state(env);
    ret = ioinst_handle_tsch(env, env->regs[1], run->s390_tsch.ipb);
    if (ret >= 0) {
        /* Success; set condition code. */
        setcc(cpu, ret);
        ret = 0;
    } else if (ret < -1) {
        /*
         * Failure.
         * If an I/O interrupt had been dequeued, we have to reinject it.
         */
        if (run->s390_tsch.dequeued) {
            uint16_t subchannel_id = run->s390_tsch.subchannel_id;
            uint16_t subchannel_nr = run->s390_tsch.subchannel_nr;
            uint32_t io_int_parm = run->s390_tsch.io_int_parm;
            uint32_t io_int_word = run->s390_tsch.io_int_word;
            uint32_t type = ((subchannel_id & 0xff00) << 24) |
                ((subchannel_id & 0x00060) << 22) | (subchannel_nr << 16);

            kvm_s390_interrupt_internal(cpu, type,
                                        ((uint32_t)subchannel_id << 16)
                                        | subchannel_nr,
                                        ((uint64_t)io_int_parm << 32)
                                        | io_int_word, 1);
        }
        ret = 0;
    }
    return ret;
}

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    S390CPU *cpu = S390_CPU(cs);
    int ret = 0;

    switch (run->exit_reason) {
        case KVM_EXIT_S390_SIEIC:
            ret = handle_intercept(cpu);
            break;
        case KVM_EXIT_S390_RESET:
            qemu_system_reset_request();
            break;
        case KVM_EXIT_S390_TSCH:
            ret = handle_tsch(cpu);
            break;
        default:
            fprintf(stderr, "Unknown KVM exit: %d\n", run->exit_reason);
            break;
    }

    if (ret == 0) {
        ret = EXCP_INTERRUPT;
    }
    return ret;
}

bool kvm_arch_stop_on_emulation_error(CPUState *cpu)
{
    return true;
}

int kvm_arch_on_sigbus_vcpu(CPUState *cpu, int code, void *addr)
{
    return 1;
}

int kvm_arch_on_sigbus(int code, void *addr)
{
    return 1;
}

void kvm_s390_io_interrupt(S390CPU *cpu, uint16_t subchannel_id,
                           uint16_t subchannel_nr, uint32_t io_int_parm,
                           uint32_t io_int_word)
{
    uint32_t type;

    type = ((subchannel_id & 0xff00) << 24) |
        ((subchannel_id & 0x00060) << 22) | (subchannel_nr << 16);
    kvm_s390_interrupt_internal(cpu, type,
                                ((uint32_t)subchannel_id << 16) | subchannel_nr,
                                ((uint64_t)io_int_parm << 32) | io_int_word, 1);
}

void kvm_s390_crw_mchk(S390CPU *cpu)
{
    kvm_s390_interrupt_internal(cpu, KVM_S390_MCHK, 1 << 28,
                                0x00400f1d40330000, 1);
}

void kvm_s390_enable_css_support(S390CPU *cpu)
{
    struct kvm_enable_cap cap = {};
    int r;

    /* Activate host kernel channel subsystem support. */
    cap.cap = KVM_CAP_S390_CSS_SUPPORT;
    r = kvm_vcpu_ioctl(CPU(cpu), KVM_ENABLE_CAP, &cap);
    assert(r == 0);
}
