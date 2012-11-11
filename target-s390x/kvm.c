/*
 * QEMU S390x KVM implementation
 *
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/kvm.h>
#include <asm/ptrace.h>

#include "qemu-common.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "kvm.h"
#include "cpu.h"
#include "device_tree.h"

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
#define IPA0_PRIV                       0xb200

#define PRIV_SCLP_CALL                  0x20
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

int kvm_arch_init_vcpu(CPUS390XState *env)
{
    int ret = 0;

    if (kvm_vcpu_ioctl(env, KVM_S390_INITIAL_RESET, NULL) < 0) {
        perror("cannot init reset vcpu");
    }

    return ret;
}

void kvm_arch_reset_vcpu(CPUS390XState *env)
{
    /* FIXME: add code to reset vcpu. */
}

int kvm_arch_put_registers(CPUS390XState *env, int level)
{
    struct kvm_sregs sregs;
    struct kvm_regs regs;
    int ret;
    int i;

    /* always save the PSW  and the GPRS*/
    env->kvm_run->psw_addr = env->psw.addr;
    env->kvm_run->psw_mask = env->psw.mask;

    if (cap_sync_regs && env->kvm_run->kvm_valid_regs & KVM_SYNC_GPRS) {
        for (i = 0; i < 16; i++) {
            env->kvm_run->s.regs.gprs[i] = env->regs[i];
            env->kvm_run->kvm_dirty_regs |= KVM_SYNC_GPRS;
        }
    } else {
        for (i = 0; i < 16; i++) {
            regs.gprs[i] = env->regs[i];
        }
        ret = kvm_vcpu_ioctl(env, KVM_SET_REGS, &regs);
        if (ret < 0) {
            return ret;
        }
    }

    /* Do we need to save more than that? */
    if (level == KVM_PUT_RUNTIME_STATE) {
        return 0;
    }

    if (cap_sync_regs &&
        env->kvm_run->kvm_valid_regs & KVM_SYNC_ACRS &&
        env->kvm_run->kvm_valid_regs & KVM_SYNC_CRS) {
        for (i = 0; i < 16; i++) {
            env->kvm_run->s.regs.acrs[i] = env->aregs[i];
            env->kvm_run->s.regs.crs[i] = env->cregs[i];
        }
        env->kvm_run->kvm_dirty_regs |= KVM_SYNC_ACRS;
        env->kvm_run->kvm_dirty_regs |= KVM_SYNC_CRS;
    } else {
        for (i = 0; i < 16; i++) {
            sregs.acrs[i] = env->aregs[i];
            sregs.crs[i] = env->cregs[i];
        }
        ret = kvm_vcpu_ioctl(env, KVM_SET_SREGS, &sregs);
        if (ret < 0) {
            return ret;
        }
    }

    /* Finally the prefix */
    if (cap_sync_regs && env->kvm_run->kvm_valid_regs & KVM_SYNC_PREFIX) {
        env->kvm_run->s.regs.prefix = env->psa;
        env->kvm_run->kvm_dirty_regs |= KVM_SYNC_PREFIX;
    } else {
        /* prefix is only supported via sync regs */
    }
    return 0;
}

int kvm_arch_get_registers(CPUS390XState *env)
{
    struct kvm_sregs sregs;
    struct kvm_regs regs;
    int ret;
    int i;

    /* get the PSW */
    env->psw.addr = env->kvm_run->psw_addr;
    env->psw.mask = env->kvm_run->psw_mask;

    /* the GPRS */
    if (cap_sync_regs && env->kvm_run->kvm_valid_regs & KVM_SYNC_GPRS) {
        for (i = 0; i < 16; i++) {
            env->regs[i] = env->kvm_run->s.regs.gprs[i];
        }
    } else {
        ret = kvm_vcpu_ioctl(env, KVM_GET_REGS, &regs);
        if (ret < 0) {
            return ret;
        }
         for (i = 0; i < 16; i++) {
            env->regs[i] = regs.gprs[i];
        }
    }

    /* The ACRS and CRS */
    if (cap_sync_regs &&
        env->kvm_run->kvm_valid_regs & KVM_SYNC_ACRS &&
        env->kvm_run->kvm_valid_regs & KVM_SYNC_CRS) {
        for (i = 0; i < 16; i++) {
            env->aregs[i] = env->kvm_run->s.regs.acrs[i];
            env->cregs[i] = env->kvm_run->s.regs.crs[i];
        }
    } else {
        ret = kvm_vcpu_ioctl(env, KVM_GET_SREGS, &sregs);
        if (ret < 0) {
            return ret;
        }
         for (i = 0; i < 16; i++) {
            env->aregs[i] = sregs.acrs[i];
            env->cregs[i] = sregs.crs[i];
        }
    }

    /* Finally the prefix */
    if (cap_sync_regs && env->kvm_run->kvm_valid_regs & KVM_SYNC_PREFIX) {
        env->psa = env->kvm_run->s.regs.prefix;
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

int kvm_arch_insert_sw_breakpoint(CPUS390XState *env, struct kvm_sw_breakpoint *bp)
{
    static const uint8_t diag_501[] = {0x83, 0x24, 0x05, 0x01};

    if (cpu_memory_rw_debug(env, bp->pc, (uint8_t *)&bp->saved_insn, 4, 0) ||
        cpu_memory_rw_debug(env, bp->pc, (uint8_t *)diag_501, 4, 1)) {
        return -EINVAL;
    }
    return 0;
}

int kvm_arch_remove_sw_breakpoint(CPUS390XState *env, struct kvm_sw_breakpoint *bp)
{
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

void kvm_arch_pre_run(CPUS390XState *env, struct kvm_run *run)
{
}

void kvm_arch_post_run(CPUS390XState *env, struct kvm_run *run)
{
}

int kvm_arch_process_async_events(CPUS390XState *env)
{
    return env->halted;
}

void kvm_s390_interrupt_internal(CPUS390XState *env, int type, uint32_t parm,
                                 uint64_t parm64, int vm)
{
    struct kvm_s390_interrupt kvmint;
    int r;

    if (!env->kvm_state) {
        return;
    }

    kvmint.type = type;
    kvmint.parm = parm;
    kvmint.parm64 = parm64;

    if (vm) {
        r = kvm_vm_ioctl(env->kvm_state, KVM_S390_INTERRUPT, &kvmint);
    } else {
        r = kvm_vcpu_ioctl(env, KVM_S390_INTERRUPT, &kvmint);
    }

    if (r < 0) {
        fprintf(stderr, "KVM failed to inject interrupt\n");
        exit(1);
    }
}

void kvm_s390_virtio_irq(CPUS390XState *env, int config_change, uint64_t token)
{
    kvm_s390_interrupt_internal(env, KVM_S390_INT_VIRTIO, config_change,
                                token, 1);
}

void kvm_s390_interrupt(CPUS390XState *env, int type, uint32_t code)
{
    kvm_s390_interrupt_internal(env, type, code, 0, 0);
}

static void enter_pgmcheck(CPUS390XState *env, uint16_t code)
{
    kvm_s390_interrupt(env, KVM_S390_PROGRAM_INT, code);
}

static inline void setcc(CPUS390XState *env, uint64_t cc)
{
    env->kvm_run->psw_mask &= ~(3ull << 44);
    env->kvm_run->psw_mask |= (cc & 3) << 44;

    env->psw.mask &= ~(3ul << 44);
    env->psw.mask |= (cc & 3) << 44;
}

static int kvm_sclp_service_call(CPUS390XState *env, struct kvm_run *run,
                                 uint16_t ipbh0)
{
    uint32_t sccb;
    uint64_t code;
    int r = 0;

    cpu_synchronize_state(env);
    sccb = env->regs[ipbh0 & 0xf];
    code = env->regs[(ipbh0 & 0xf0) >> 4];

    r = sclp_service_call(sccb, code);
    if (r < 0) {
        enter_pgmcheck(env, -r);
    }
    setcc(env, r);

    return 0;
}

static int handle_priv(CPUS390XState *env, struct kvm_run *run, uint8_t ipa1)
{
    int r = 0;
    uint16_t ipbh0 = (run->s390_sieic.ipb & 0xffff0000) >> 16;

    dprintf("KVM: PRIV: %d\n", ipa1);
    switch (ipa1) {
        case PRIV_SCLP_CALL:
            r = kvm_sclp_service_call(env, run, ipbh0);
            break;
        default:
            dprintf("KVM: unknown PRIV: 0x%x\n", ipa1);
            r = -1;
            break;
    }

    return r;
}

static int handle_hypercall(CPUS390XState *env, struct kvm_run *run)
{
    cpu_synchronize_state(env);
    env->regs[2] = s390_virtio_hypercall(env, env->regs[2], env->regs[1]);

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
    CPUS390XState *env = &cpu->env;

    kvm_s390_interrupt(env, KVM_S390_RESTART, 0);
    s390_add_running_cpu(env);
    qemu_cpu_kick(CPU(cpu));
    dprintf("DONE: SIGP cpu restart: %p\n", env);
    return 0;
}

static int s390_store_status(CPUS390XState *env, uint32_t parameter)
{
    /* XXX */
    fprintf(stderr, "XXX SIGP store status\n");
    return -1;
}

static int s390_cpu_initial_reset(CPUS390XState *env)
{
    int i;

    s390_del_running_cpu(env);
    if (kvm_vcpu_ioctl(env, KVM_S390_INITIAL_RESET, NULL) < 0) {
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

static int handle_sigp(CPUS390XState *env, struct kvm_run *run, uint8_t ipa1)
{
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
            r = s390_cpu_initial_reset(target_env);
            break;
        default:
            fprintf(stderr, "KVM: unknown SIGP: 0x%x\n", order_code);
            break;
    }

out:
    setcc(env, r ? 3 : 0);
    return 0;
}

static int handle_instruction(CPUS390XState *env, struct kvm_run *run)
{
    unsigned int ipa0 = (run->s390_sieic.ipa & 0xff00);
    uint8_t ipa1 = run->s390_sieic.ipa & 0x00ff;
    int ipb_code = (run->s390_sieic.ipb & 0x0fff0000) >> 16;
    int r = -1;

    dprintf("handle_instruction 0x%x 0x%x\n", run->s390_sieic.ipa, run->s390_sieic.ipb);
    switch (ipa0) {
        case IPA0_PRIV:
            r = handle_priv(env, run, ipa1);
            break;
        case IPA0_DIAG:
            r = handle_diag(env, run, ipb_code);
            break;
        case IPA0_SIGP:
            r = handle_sigp(env, run, ipa1);
            break;
    }

    if (r < 0) {
        enter_pgmcheck(env, 0x0001);
    }
    return 0;
}

static bool is_special_wait_psw(CPUS390XState *env)
{
    /* signal quiesce */
    return env->kvm_run->psw_addr == 0xfffUL;
}

static int handle_intercept(CPUS390XState *env)
{
    struct kvm_run *run = env->kvm_run;
    int icpt_code = run->s390_sieic.icptcode;
    int r = 0;

    dprintf("intercept: 0x%x (at 0x%lx)\n", icpt_code,
            (long)env->kvm_run->psw_addr);
    switch (icpt_code) {
        case ICPT_INSTRUCTION:
            r = handle_instruction(env, run);
            break;
        case ICPT_WAITPSW:
            if (s390_del_running_cpu(env) == 0 &&
                is_special_wait_psw(env)) {
                qemu_system_shutdown_request();
            }
            r = EXCP_HALTED;
            break;
        case ICPT_CPU_STOP:
            if (s390_del_running_cpu(env) == 0) {
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

int kvm_arch_handle_exit(CPUS390XState *env, struct kvm_run *run)
{
    int ret = 0;

    switch (run->exit_reason) {
        case KVM_EXIT_S390_SIEIC:
            ret = handle_intercept(env);
            break;
        case KVM_EXIT_S390_RESET:
            qemu_system_reset_request();
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

bool kvm_arch_stop_on_emulation_error(CPUS390XState *env)
{
    return true;
}

int kvm_arch_on_sigbus_vcpu(CPUS390XState *env, int code, void *addr)
{
    return 1;
}

int kvm_arch_on_sigbus(int code, void *addr)
{
    return 1;
}
