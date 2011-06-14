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

#define SCLP_CMDW_READ_SCP_INFO         0x00020001
#define SCLP_CMDW_READ_SCP_INFO_FORCED  0x00120001

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

int kvm_arch_init(KVMState *s)
{
    return 0;
}

int kvm_arch_init_vcpu(CPUState *env)
{
    int ret = 0;

    if (kvm_vcpu_ioctl(env, KVM_S390_INITIAL_RESET, NULL) < 0) {
        perror("cannot init reset vcpu");
    }

    return ret;
}

void kvm_arch_reset_vcpu(CPUState *env)
{
    /* FIXME: add code to reset vcpu. */
}

int kvm_arch_put_registers(CPUState *env, int level)
{
    struct kvm_regs regs;
    int ret;
    int i;

    ret = kvm_vcpu_ioctl(env, KVM_GET_REGS, &regs);
    if (ret < 0) {
        return ret;
    }

    for (i = 0; i < 16; i++) {
        regs.gprs[i] = env->regs[i];
    }

    ret = kvm_vcpu_ioctl(env, KVM_SET_REGS, &regs);
    if (ret < 0) {
        return ret;
    }

    env->kvm_run->psw_addr = env->psw.addr;
    env->kvm_run->psw_mask = env->psw.mask;

    return ret;
}

int kvm_arch_get_registers(CPUState *env)
{
    int ret;
    struct kvm_regs regs;
    int i;

    ret = kvm_vcpu_ioctl(env, KVM_GET_REGS, &regs);
    if (ret < 0) {
        return ret;
    }

    for (i = 0; i < 16; i++) {
        env->regs[i] = regs.gprs[i];
    }

    env->psw.addr = env->kvm_run->psw_addr;
    env->psw.mask = env->kvm_run->psw_mask;

    return 0;
}

int kvm_arch_insert_sw_breakpoint(CPUState *env, struct kvm_sw_breakpoint *bp)
{
    static const uint8_t diag_501[] = {0x83, 0x24, 0x05, 0x01};

    if (cpu_memory_rw_debug(env, bp->pc, (uint8_t *)&bp->saved_insn, 4, 0) ||
        cpu_memory_rw_debug(env, bp->pc, (uint8_t *)diag_501, 4, 1)) {
        return -EINVAL;
    }
    return 0;
}

int kvm_arch_remove_sw_breakpoint(CPUState *env, struct kvm_sw_breakpoint *bp)
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

void kvm_arch_pre_run(CPUState *env, struct kvm_run *run)
{
}

void kvm_arch_post_run(CPUState *env, struct kvm_run *run)
{
}

int kvm_arch_process_async_events(CPUState *env)
{
    return env->halted;
}

void kvm_s390_interrupt_internal(CPUState *env, int type, uint32_t parm,
                                 uint64_t parm64, int vm)
{
    struct kvm_s390_interrupt kvmint;
    int r;

    if (!env->kvm_state) {
        return;
    }

    env->halted = 0;
    env->exception_index = -1;
    qemu_cpu_kick(env);

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

void kvm_s390_virtio_irq(CPUState *env, int config_change, uint64_t token)
{
    kvm_s390_interrupt_internal(env, KVM_S390_INT_VIRTIO, config_change,
                                token, 1);
}

void kvm_s390_interrupt(CPUState *env, int type, uint32_t code)
{
    kvm_s390_interrupt_internal(env, type, code, 0, 0);
}

static void enter_pgmcheck(CPUState *env, uint16_t code)
{
    kvm_s390_interrupt(env, KVM_S390_PROGRAM_INT, code);
}

static inline void setcc(CPUState *env, uint64_t cc)
{
    env->kvm_run->psw_mask &= ~(3ull << 44);
    env->kvm_run->psw_mask |= (cc & 3) << 44;

    env->psw.mask &= ~(3ul << 44);
    env->psw.mask |= (cc & 3) << 44;
}

static int kvm_sclp_service_call(CPUState *env, struct kvm_run *run,
                                 uint16_t ipbh0)
{
    uint32_t sccb;
    uint64_t code;
    int r = 0;

    cpu_synchronize_state(env);
    sccb = env->regs[ipbh0 & 0xf];
    code = env->regs[(ipbh0 & 0xf0) >> 4];

    r = sclp_service_call(env, sccb, code);
    if (r) {
        setcc(env, 3);
    }

    return 0;
}

static int handle_priv(CPUState *env, struct kvm_run *run, uint8_t ipa1)
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

static int handle_hypercall(CPUState *env, struct kvm_run *run)
{
    cpu_synchronize_state(env);
    env->regs[2] = s390_virtio_hypercall(env, env->regs[2], env->regs[1]);

    return 0;
}

static int handle_diag(CPUState *env, struct kvm_run *run, int ipb_code)
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

static int s390_cpu_restart(CPUState *env)
{
    kvm_s390_interrupt(env, KVM_S390_RESTART, 0);
    env->halted = 0;
    env->exception_index = -1;
    qemu_cpu_kick(env);
    dprintf("DONE: SIGP cpu restart: %p\n", env);
    return 0;
}

static int s390_store_status(CPUState *env, uint32_t parameter)
{
    /* XXX */
    fprintf(stderr, "XXX SIGP store status\n");
    return -1;
}

static int s390_cpu_initial_reset(CPUState *env)
{
    int i;

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

static int handle_sigp(CPUState *env, struct kvm_run *run, uint8_t ipa1)
{
    uint8_t order_code;
    uint32_t parameter;
    uint16_t cpu_addr;
    uint8_t t;
    int r = -1;
    CPUState *target_env;

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

    target_env = s390_cpu_addr2state(cpu_addr);
    if (!target_env) {
        goto out;
    }

    switch (order_code) {
        case SIGP_RESTART:
            r = s390_cpu_restart(target_env);
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

static int handle_instruction(CPUState *env, struct kvm_run *run)
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

static int handle_intercept(CPUState *env)
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
            /* XXX What to do on system shutdown? */
            env->halted = 1;
            env->exception_index = EXCP_HLT;
            break;
        case ICPT_SOFT_INTERCEPT:
            fprintf(stderr, "KVM unimplemented icpt SOFT\n");
            exit(1);
            break;
        case ICPT_CPU_STOP:
            qemu_system_shutdown_request();
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

int kvm_arch_handle_exit(CPUState *env, struct kvm_run *run)
{
    int ret = 0;

    switch (run->exit_reason) {
        case KVM_EXIT_S390_SIEIC:
            ret = handle_intercept(env);
            break;
        case KVM_EXIT_S390_RESET:
            fprintf(stderr, "RESET not implemented\n");
            exit(1);
            break;
        default:
            fprintf(stderr, "Unknown KVM exit: %d\n", run->exit_reason);
            break;
    }

    if (ret == 0) {
        ret = EXCP_INTERRUPT;
    } else if (ret > 0) {
        ret = 0;
    }
    return ret;
}

bool kvm_arch_stop_on_emulation_error(CPUState *env)
{
    return true;
}

int kvm_arch_on_sigbus_vcpu(CPUState *env, int code, void *addr)
{
    return 1;
}

int kvm_arch_on_sigbus(int code, void *addr)
{
    return 1;
}
