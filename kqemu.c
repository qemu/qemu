/*
 *  KQEMU support
 * 
 *  Copyright (c) 2005 Fabrice Bellard
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "config.h"
#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#else
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>

#include "cpu.h"
#include "exec-all.h"

#ifdef USE_KQEMU

#define DEBUG

#include <unistd.h>
#include <fcntl.h>
#include "kqemu/kqemu.h"

/* compatibility stuff */
#ifndef KQEMU_RET_SYSCALL
#define KQEMU_RET_SYSCALL   0x0300 /* syscall insn */
#endif

#ifdef _WIN32
#define KQEMU_DEVICE "\\\\.\\kqemu"
#else
#define KQEMU_DEVICE "/dev/kqemu"
#endif

#ifdef _WIN32
#define KQEMU_INVALID_FD INVALID_HANDLE_VALUE
HANDLE kqemu_fd = KQEMU_INVALID_FD;
#define kqemu_closefd(x) CloseHandle(x)
#else
#define KQEMU_INVALID_FD -1
int kqemu_fd = KQEMU_INVALID_FD;
#define kqemu_closefd(x) close(x)
#endif

int kqemu_allowed = 1;
unsigned long *pages_to_flush;
unsigned int nb_pages_to_flush;
extern uint32_t **l1_phys_map;

#define cpuid(index, eax, ebx, ecx, edx) \
  asm volatile ("cpuid" \
                : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) \
                : "0" (index))

#ifdef __x86_64__
static int is_cpuid_supported(void)
{
    return 1;
}
#else
static int is_cpuid_supported(void)
{
    int v0, v1;
    asm volatile ("pushf\n"
                  "popl %0\n"
                  "movl %0, %1\n"
                  "xorl $0x00200000, %0\n"
                  "pushl %0\n"
                  "popf\n"
                  "pushf\n"
                  "popl %0\n"
                  : "=a" (v0), "=d" (v1)
                  :
                  : "cc");
    return (v0 != v1);
}
#endif

static void kqemu_update_cpuid(CPUState *env)
{
    int critical_features_mask, features;
    uint32_t eax, ebx, ecx, edx;

    /* the following features are kept identical on the host and
       target cpus because they are important for user code. Strictly
       speaking, only SSE really matters because the OS must support
       it if the user code uses it. */
    critical_features_mask = 
        CPUID_CMOV | CPUID_CX8 | 
        CPUID_FXSR | CPUID_MMX | CPUID_SSE | 
        CPUID_SSE2;
    if (!is_cpuid_supported()) {
        features = 0;
    } else {
        cpuid(1, eax, ebx, ecx, edx);
        features = edx;
    }
    env->cpuid_features = (env->cpuid_features & ~critical_features_mask) |
        (features & critical_features_mask);
    /* XXX: we could update more of the target CPUID state so that the
       non accelerated code sees exactly the same CPU features as the
       accelerated code */
}

int kqemu_init(CPUState *env)
{
    struct kqemu_init init;
    int ret, version;
#ifdef _WIN32
    DWORD temp;
#endif

    if (!kqemu_allowed)
        return -1;

#ifdef _WIN32
    kqemu_fd = CreateFile(KQEMU_DEVICE, GENERIC_WRITE | GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                          NULL);
#else
    kqemu_fd = open(KQEMU_DEVICE, O_RDWR);
#endif
    if (kqemu_fd == KQEMU_INVALID_FD) {
        fprintf(stderr, "Could not open '%s' - QEMU acceleration layer not activated\n", KQEMU_DEVICE);
        return -1;
    }
    version = 0;
#ifdef _WIN32
    DeviceIoControl(kqemu_fd, KQEMU_GET_VERSION, NULL, 0,
                    &version, sizeof(version), &temp, NULL);
#else
    ioctl(kqemu_fd, KQEMU_GET_VERSION, &version);
#endif
    if (version != KQEMU_VERSION) {
        fprintf(stderr, "Version mismatch between kqemu module and qemu (%08x %08x) - disabling kqemu use\n",
                version, KQEMU_VERSION);
        goto fail;
    }

    pages_to_flush = qemu_vmalloc(KQEMU_MAX_PAGES_TO_FLUSH * 
                                  sizeof(unsigned long));
    if (!pages_to_flush)
        goto fail;

    init.ram_base = phys_ram_base;
    init.ram_size = phys_ram_size;
    init.ram_dirty = phys_ram_dirty;
    init.phys_to_ram_map = l1_phys_map;
    init.pages_to_flush = pages_to_flush;
#ifdef _WIN32
    ret = DeviceIoControl(kqemu_fd, KQEMU_INIT, &init, sizeof(init),
                          NULL, 0, &temp, NULL) == TRUE ? 0 : -1;
#else
    ret = ioctl(kqemu_fd, KQEMU_INIT, &init);
#endif
    if (ret < 0) {
        fprintf(stderr, "Error %d while initializing QEMU acceleration layer - disabling it for now\n", ret);
    fail:
        kqemu_closefd(kqemu_fd);
        kqemu_fd = KQEMU_INVALID_FD;
        return -1;
    }
    kqemu_update_cpuid(env);
    env->kqemu_enabled = 1;
    nb_pages_to_flush = 0;
    return 0;
}

void kqemu_flush_page(CPUState *env, target_ulong addr)
{
#ifdef DEBUG
    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "kqemu_flush_page: addr=" TARGET_FMT_lx "\n", addr);
    }
#endif
    if (nb_pages_to_flush >= KQEMU_MAX_PAGES_TO_FLUSH)
        nb_pages_to_flush = KQEMU_FLUSH_ALL;
    else
        pages_to_flush[nb_pages_to_flush++] = addr;
}

void kqemu_flush(CPUState *env, int global)
{
#ifdef DEBUG
    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "kqemu_flush:\n");
    }
#endif
    nb_pages_to_flush = KQEMU_FLUSH_ALL;
}

struct fpstate {
    uint16_t fpuc;
    uint16_t dummy1;
    uint16_t fpus;
    uint16_t dummy2;
    uint16_t fptag;
    uint16_t dummy3;

    uint32_t fpip;
    uint32_t fpcs;
    uint32_t fpoo;
    uint32_t fpos;
    uint8_t fpregs1[8 * 10];
};

struct fpxstate {
    uint16_t fpuc;
    uint16_t fpus;
    uint16_t fptag;
    uint16_t fop;
    uint32_t fpuip;
    uint16_t cs_sel;
    uint16_t dummy0;
    uint32_t fpudp;
    uint16_t ds_sel;
    uint16_t dummy1;
    uint32_t mxcsr;
    uint32_t mxcsr_mask;
    uint8_t fpregs1[8 * 16];
    uint8_t xmm_regs[16 * 16];
    uint8_t dummy2[96];
};

static struct fpxstate fpx1 __attribute__((aligned(16)));

static void restore_native_fp_frstor(CPUState *env)
{
    int fptag, i, j;
    struct fpstate fp1, *fp = &fp1;
    
    fp->fpuc = env->fpuc;
    fp->fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    fptag = 0;
    for (i=7; i>=0; i--) {
	fptag <<= 2;
	if (env->fptags[i]) {
            fptag |= 3;
        } else {
            /* the FPU automatically computes it */
        }
    }
    fp->fptag = fptag;
    j = env->fpstt;
    for(i = 0;i < 8; i++) {
        memcpy(&fp->fpregs1[i * 10], &env->fpregs[j].d, 10);
        j = (j + 1) & 7;
    }
    asm volatile ("frstor %0" : "=m" (*fp));
}
 
static void save_native_fp_fsave(CPUState *env)
{
    int fptag, i, j;
    uint16_t fpuc;
    struct fpstate fp1, *fp = &fp1;

    asm volatile ("fsave %0" : : "m" (*fp));
    env->fpuc = fp->fpuc;
    env->fpstt = (fp->fpus >> 11) & 7;
    env->fpus = fp->fpus & ~0x3800;
    fptag = fp->fptag;
    for(i = 0;i < 8; i++) {
        env->fptags[i] = ((fptag & 3) == 3);
        fptag >>= 2;
    }
    j = env->fpstt;
    for(i = 0;i < 8; i++) {
        memcpy(&env->fpregs[j].d, &fp->fpregs1[i * 10], 10);
        j = (j + 1) & 7;
    }
    /* we must restore the default rounding state */
    fpuc = 0x037f | (env->fpuc & (3 << 10));
    asm volatile("fldcw %0" : : "m" (fpuc));
}

static void restore_native_fp_fxrstor(CPUState *env)
{
    struct fpxstate *fp = &fpx1;
    int i, j, fptag;

    fp->fpuc = env->fpuc;
    fp->fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    fptag = 0;
    for(i = 0; i < 8; i++)
        fptag |= (env->fptags[i] << i);
    fp->fptag = fptag ^ 0xff;

    j = env->fpstt;
    for(i = 0;i < 8; i++) {
        memcpy(&fp->fpregs1[i * 16], &env->fpregs[j].d, 10);
        j = (j + 1) & 7;
    }
    if (env->cpuid_features & CPUID_SSE) {
        fp->mxcsr = env->mxcsr;
        /* XXX: check if DAZ is not available */
        fp->mxcsr_mask = 0xffff;
        memcpy(fp->xmm_regs, env->xmm_regs, CPU_NB_REGS * 16);
    }
    asm volatile ("fxrstor %0" : "=m" (*fp));
}

static void save_native_fp_fxsave(CPUState *env)
{
    struct fpxstate *fp = &fpx1;
    int fptag, i, j;
    uint16_t fpuc;

    asm volatile ("fxsave %0" : : "m" (*fp));
    env->fpuc = fp->fpuc;
    env->fpstt = (fp->fpus >> 11) & 7;
    env->fpus = fp->fpus & ~0x3800;
    fptag = fp->fptag ^ 0xff;
    for(i = 0;i < 8; i++) {
        env->fptags[i] = (fptag >> i) & 1;
    }
    j = env->fpstt;
    for(i = 0;i < 8; i++) {
        memcpy(&env->fpregs[j].d, &fp->fpregs1[i * 16], 10);
        j = (j + 1) & 7;
    }
    if (env->cpuid_features & CPUID_SSE) {
        env->mxcsr = fp->mxcsr;
        memcpy(env->xmm_regs, fp->xmm_regs, CPU_NB_REGS * 16);
    }

    /* we must restore the default rounding state */
    asm volatile ("fninit");
    fpuc = 0x037f | (env->fpuc & (3 << 10));
    asm volatile("fldcw %0" : : "m" (fpuc));
}

static int do_syscall(CPUState *env,
                      struct kqemu_cpu_state *kenv)
{
    int selector;
    
    selector = (env->star >> 32) & 0xffff;
#ifdef __x86_64__
    if (env->hflags & HF_LMA_MASK) {
        env->regs[R_ECX] = kenv->next_eip;
        env->regs[11] = env->eflags;

        cpu_x86_set_cpl(env, 0);
        cpu_x86_load_seg_cache(env, R_CS, selector & 0xfffc, 
                               0, 0xffffffff, 
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK | DESC_L_MASK);
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) & 0xfffc, 
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_W_MASK | DESC_A_MASK);
        env->eflags &= ~env->fmask;
        if (env->hflags & HF_CS64_MASK)
            env->eip = env->lstar;
        else
            env->eip = env->cstar;
    } else 
#endif
    {
        env->regs[R_ECX] = (uint32_t)kenv->next_eip;
        
        cpu_x86_set_cpl(env, 0);
        cpu_x86_load_seg_cache(env, R_CS, selector & 0xfffc, 
                           0, 0xffffffff, 
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) & 0xfffc, 
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_W_MASK | DESC_A_MASK);
        env->eflags &= ~(IF_MASK | RF_MASK | VM_MASK);
        env->eip = (uint32_t)env->star;
    }
    return 2;
}

int kqemu_cpu_exec(CPUState *env)
{
    struct kqemu_cpu_state kcpu_state, *kenv = &kcpu_state;
    int ret;
#ifdef _WIN32
    DWORD temp;
#endif

#ifdef DEBUG
    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "kqemu: cpu_exec: enter\n");
        cpu_dump_state(env, logfile, fprintf, 0);
    }
#endif
    memcpy(kenv->regs, env->regs, sizeof(kenv->regs));
    kenv->eip = env->eip;
    kenv->eflags = env->eflags;
    memcpy(&kenv->segs, &env->segs, sizeof(env->segs));
    memcpy(&kenv->ldt, &env->ldt, sizeof(env->ldt));
    memcpy(&kenv->tr, &env->tr, sizeof(env->tr));
    memcpy(&kenv->gdt, &env->gdt, sizeof(env->gdt));
    memcpy(&kenv->idt, &env->idt, sizeof(env->idt));
    kenv->cr0 = env->cr[0];
    kenv->cr2 = env->cr[2];
    kenv->cr3 = env->cr[3];
    kenv->cr4 = env->cr[4];
    kenv->a20_mask = env->a20_mask;
#if KQEMU_VERSION >= 0x010100
    kenv->efer = env->efer;
#endif
    if (env->dr[7] & 0xff) {
        kenv->dr7 = env->dr[7];
        kenv->dr0 = env->dr[0];
        kenv->dr1 = env->dr[1];
        kenv->dr2 = env->dr[2];
        kenv->dr3 = env->dr[3];
    } else {
        kenv->dr7 = 0;
    }
    kenv->dr6 = env->dr[6];
    kenv->cpl = 3;
    kenv->nb_pages_to_flush = nb_pages_to_flush;
    nb_pages_to_flush = 0;
    
    if (!(kenv->cr0 & CR0_TS_MASK)) {
        if (env->cpuid_features & CPUID_FXSR)
            restore_native_fp_fxrstor(env);
        else
            restore_native_fp_frstor(env);
    }

#ifdef _WIN32
    DeviceIoControl(kqemu_fd, KQEMU_EXEC,
		    kenv, sizeof(struct kqemu_cpu_state),
		    kenv, sizeof(struct kqemu_cpu_state),
		    &temp, NULL);
    ret = kenv->retval;
#else
#if KQEMU_VERSION >= 0x010100
    ioctl(kqemu_fd, KQEMU_EXEC, kenv);
    ret = kenv->retval;
#else
    ret = ioctl(kqemu_fd, KQEMU_EXEC, kenv);
#endif
#endif
    if (!(kenv->cr0 & CR0_TS_MASK)) {
        if (env->cpuid_features & CPUID_FXSR)
            save_native_fp_fxsave(env);
        else
            save_native_fp_fsave(env);
    }

    memcpy(env->regs, kenv->regs, sizeof(env->regs));
    env->eip = kenv->eip;
    env->eflags = kenv->eflags;
    memcpy(env->segs, kenv->segs, sizeof(env->segs));
#if 0
    /* no need to restore that */
    memcpy(env->ldt, kenv->ldt, sizeof(env->ldt));
    memcpy(env->tr, kenv->tr, sizeof(env->tr));
    memcpy(env->gdt, kenv->gdt, sizeof(env->gdt));
    memcpy(env->idt, kenv->idt, sizeof(env->idt));
    env->cr[0] = kenv->cr0;
    env->cr[3] = kenv->cr3;
    env->cr[4] = kenv->cr4;
    env->a20_mask = kenv->a20_mask;
#endif
    env->cr[2] = kenv->cr2;
    env->dr[6] = kenv->dr6;

#ifdef DEBUG
    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "kqemu: kqemu_cpu_exec: ret=0x%x\n", ret);
    }
#endif
    if (ret == KQEMU_RET_SYSCALL) {
        /* syscall instruction */
        return do_syscall(env, kenv);
    } else 
    if ((ret & 0xff00) == KQEMU_RET_INT) {
        env->exception_index = ret & 0xff;
        env->error_code = 0;
        env->exception_is_int = 1;
        env->exception_next_eip = kenv->next_eip;
#ifdef DEBUG
        if (loglevel & CPU_LOG_INT) {
            fprintf(logfile, "kqemu: interrupt v=%02x:\n", 
                    env->exception_index);
            cpu_dump_state(env, logfile, fprintf, 0);
        }
#endif
        return 1;
    } else if ((ret & 0xff00) == KQEMU_RET_EXCEPTION) {
        env->exception_index = ret & 0xff;
        env->error_code = kenv->error_code;
        env->exception_is_int = 0;
        env->exception_next_eip = 0;
#ifdef DEBUG
        if (loglevel & CPU_LOG_INT) {
            fprintf(logfile, "kqemu: exception v=%02x e=%04x:\n",
                    env->exception_index, env->error_code);
            cpu_dump_state(env, logfile, fprintf, 0);
        }
#endif
        return 1;
    } else if (ret == KQEMU_RET_INTR) {
#ifdef DEBUG
        if (loglevel & CPU_LOG_INT) {
            cpu_dump_state(env, logfile, fprintf, 0);
        }
#endif
        return 0;
    } else if (ret == KQEMU_RET_SOFTMMU) { 
        return 2;
    } else {
        cpu_dump_state(env, stderr, fprintf, 0);
        fprintf(stderr, "Unsupported return value: 0x%x\n", ret);
        exit(1);
    }
    return 0;
}

#endif
