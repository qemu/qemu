/*
 *  KQEMU support
 *
 *  Copyright (c) 2005-2008 Fabrice Bellard
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
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
#ifdef HOST_SOLARIS
#include <sys/ioccom.h>
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
#include "qemu-common.h"

#ifdef USE_KQEMU

#define DEBUG
//#define PROFILE


#ifdef DEBUG
#  define LOG_INT(...) qemu_log_mask(CPU_LOG_INT, ## __VA_ARGS__)
#  define LOG_INT_STATE(env) log_cpu_state_mask(CPU_LOG_INT, (env), 0)
#else
#  define LOG_INT(...) do { } while (0)
#  define LOG_INT_STATE(env) do { } while (0)
#endif

#include <unistd.h>
#include <fcntl.h>
#include "kqemu.h"

#ifdef _WIN32
#define KQEMU_DEVICE "\\\\.\\kqemu"
#else
#define KQEMU_DEVICE "/dev/kqemu"
#endif

static void qpi_init(void);

#ifdef _WIN32
#define KQEMU_INVALID_FD INVALID_HANDLE_VALUE
HANDLE kqemu_fd = KQEMU_INVALID_FD;
#define kqemu_closefd(x) CloseHandle(x)
#else
#define KQEMU_INVALID_FD -1
int kqemu_fd = KQEMU_INVALID_FD;
#define kqemu_closefd(x) close(x)
#endif

/* 0 = not allowed
   1 = user kqemu
   2 = kernel kqemu
*/
int kqemu_allowed = 1;
uint64_t *pages_to_flush;
unsigned int nb_pages_to_flush;
uint64_t *ram_pages_to_update;
unsigned int nb_ram_pages_to_update;
uint64_t *modified_ram_pages;
unsigned int nb_modified_ram_pages;
uint8_t *modified_ram_pages_table;
int qpi_io_memory;
uint32_t kqemu_comm_base; /* physical address of the QPI communication page */
ram_addr_t kqemu_phys_ram_size;
uint8_t *kqemu_phys_ram_base;

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
    int critical_features_mask, features, ext_features, ext_features_mask;
    uint32_t eax, ebx, ecx, edx;

    /* the following features are kept identical on the host and
       target cpus because they are important for user code. Strictly
       speaking, only SSE really matters because the OS must support
       it if the user code uses it. */
    critical_features_mask =
        CPUID_CMOV | CPUID_CX8 |
        CPUID_FXSR | CPUID_MMX | CPUID_SSE |
        CPUID_SSE2 | CPUID_SEP;
    ext_features_mask = CPUID_EXT_SSE3 | CPUID_EXT_MONITOR;
    if (!is_cpuid_supported()) {
        features = 0;
        ext_features = 0;
    } else {
        cpuid(1, eax, ebx, ecx, edx);
        features = edx;
        ext_features = ecx;
    }
#ifdef __x86_64__
    /* NOTE: on x86_64 CPUs, SYSENTER is not supported in
       compatibility mode, so in order to have the best performances
       it is better not to use it */
    features &= ~CPUID_SEP;
#endif
    env->cpuid_features = (env->cpuid_features & ~critical_features_mask) |
        (features & critical_features_mask);
    env->cpuid_ext_features = (env->cpuid_ext_features & ~ext_features_mask) |
        (ext_features & ext_features_mask);
    /* XXX: we could update more of the target CPUID state so that the
       non accelerated code sees exactly the same CPU features as the
       accelerated code */
}

int kqemu_init(CPUState *env)
{
    struct kqemu_init kinit;
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
    if (kqemu_fd == KQEMU_INVALID_FD) {
        fprintf(stderr, "Could not open '%s' - QEMU acceleration layer not activated: %lu\n",
                KQEMU_DEVICE, GetLastError());
        return -1;
    }
#else
    kqemu_fd = open(KQEMU_DEVICE, O_RDWR);
    if (kqemu_fd == KQEMU_INVALID_FD) {
        fprintf(stderr, "Could not open '%s' - QEMU acceleration layer not activated: %s\n",
                KQEMU_DEVICE, strerror(errno));
        return -1;
    }
#endif
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
                                  sizeof(uint64_t));
    if (!pages_to_flush)
        goto fail;

    ram_pages_to_update = qemu_vmalloc(KQEMU_MAX_RAM_PAGES_TO_UPDATE *
                                       sizeof(uint64_t));
    if (!ram_pages_to_update)
        goto fail;

    modified_ram_pages = qemu_vmalloc(KQEMU_MAX_MODIFIED_RAM_PAGES *
                                      sizeof(uint64_t));
    if (!modified_ram_pages)
        goto fail;
    modified_ram_pages_table =
        qemu_mallocz(kqemu_phys_ram_size >> TARGET_PAGE_BITS);
    if (!modified_ram_pages_table)
        goto fail;

    memset(&kinit, 0, sizeof(kinit)); /* set the paddings to zero */
    kinit.ram_base = kqemu_phys_ram_base;
    kinit.ram_size = kqemu_phys_ram_size;
    kinit.ram_dirty = phys_ram_dirty;
    kinit.pages_to_flush = pages_to_flush;
    kinit.ram_pages_to_update = ram_pages_to_update;
    kinit.modified_ram_pages = modified_ram_pages;
#ifdef _WIN32
    ret = DeviceIoControl(kqemu_fd, KQEMU_INIT, &kinit, sizeof(kinit),
                          NULL, 0, &temp, NULL) == TRUE ? 0 : -1;
#else
    ret = ioctl(kqemu_fd, KQEMU_INIT, &kinit);
#endif
    if (ret < 0) {
        fprintf(stderr, "Error %d while initializing QEMU acceleration layer - disabling it for now\n", ret);
    fail:
        kqemu_closefd(kqemu_fd);
        kqemu_fd = KQEMU_INVALID_FD;
        return -1;
    }
    kqemu_update_cpuid(env);
    env->kqemu_enabled = kqemu_allowed;
    nb_pages_to_flush = 0;
    nb_ram_pages_to_update = 0;

    qpi_init();
    return 0;
}

void kqemu_flush_page(CPUState *env, target_ulong addr)
{
    LOG_INT("kqemu_flush_page: addr=" TARGET_FMT_lx "\n", addr);
    if (nb_pages_to_flush >= KQEMU_MAX_PAGES_TO_FLUSH)
        nb_pages_to_flush = KQEMU_FLUSH_ALL;
    else
        pages_to_flush[nb_pages_to_flush++] = addr;
}

void kqemu_flush(CPUState *env, int global)
{
    LOG_INT("kqemu_flush:\n");
    nb_pages_to_flush = KQEMU_FLUSH_ALL;
}

void kqemu_set_notdirty(CPUState *env, ram_addr_t ram_addr)
{
    LOG_INT("kqemu_set_notdirty: addr=%08lx\n", 
                (unsigned long)ram_addr);
    /* we only track transitions to dirty state */
    if (phys_ram_dirty[ram_addr >> TARGET_PAGE_BITS] != 0xff)
        return;
    if (nb_ram_pages_to_update >= KQEMU_MAX_RAM_PAGES_TO_UPDATE)
        nb_ram_pages_to_update = KQEMU_RAM_PAGES_UPDATE_ALL;
    else
        ram_pages_to_update[nb_ram_pages_to_update++] = ram_addr;
}

static void kqemu_reset_modified_ram_pages(void)
{
    int i;
    unsigned long page_index;

    for(i = 0; i < nb_modified_ram_pages; i++) {
        page_index = modified_ram_pages[i] >> TARGET_PAGE_BITS;
        modified_ram_pages_table[page_index] = 0;
    }
    nb_modified_ram_pages = 0;
}

void kqemu_modify_page(CPUState *env, ram_addr_t ram_addr)
{
    unsigned long page_index;
    int ret;
#ifdef _WIN32
    DWORD temp;
#endif

    page_index = ram_addr >> TARGET_PAGE_BITS;
    if (!modified_ram_pages_table[page_index]) {
#if 0
        printf("%d: modify_page=%08lx\n", nb_modified_ram_pages, ram_addr);
#endif
        modified_ram_pages_table[page_index] = 1;
        modified_ram_pages[nb_modified_ram_pages++] = ram_addr;
        if (nb_modified_ram_pages >= KQEMU_MAX_MODIFIED_RAM_PAGES) {
            /* flush */
#ifdef _WIN32
            ret = DeviceIoControl(kqemu_fd, KQEMU_MODIFY_RAM_PAGES,
                                  &nb_modified_ram_pages,
                                  sizeof(nb_modified_ram_pages),
                                  NULL, 0, &temp, NULL);
#else
            ret = ioctl(kqemu_fd, KQEMU_MODIFY_RAM_PAGES,
                        &nb_modified_ram_pages);
#endif
            kqemu_reset_modified_ram_pages();
        }
    }
}

void kqemu_set_phys_mem(uint64_t start_addr, ram_addr_t size, 
                        ram_addr_t phys_offset)
{
    struct kqemu_phys_mem kphys_mem1, *kphys_mem = &kphys_mem1;
    uint64_t end;
    int ret, io_index;

    end = (start_addr + size + TARGET_PAGE_SIZE - 1) & TARGET_PAGE_MASK;
    start_addr &= TARGET_PAGE_MASK;
    kphys_mem->phys_addr = start_addr;
    kphys_mem->size = end - start_addr;
    kphys_mem->ram_addr = phys_offset & TARGET_PAGE_MASK;
    io_index = phys_offset & ~TARGET_PAGE_MASK;
    switch(io_index) {
    case IO_MEM_RAM:
        kphys_mem->io_index = KQEMU_IO_MEM_RAM;
        break;
    case IO_MEM_ROM:
        kphys_mem->io_index = KQEMU_IO_MEM_ROM;
        break;
    default:
        if (qpi_io_memory == io_index) {
            kphys_mem->io_index = KQEMU_IO_MEM_COMM;
        } else {
            kphys_mem->io_index = KQEMU_IO_MEM_UNASSIGNED;
        }
        break;
    }
#ifdef _WIN32
    {
        DWORD temp;
        ret = DeviceIoControl(kqemu_fd, KQEMU_SET_PHYS_MEM, 
                              kphys_mem, sizeof(*kphys_mem),
                              NULL, 0, &temp, NULL) == TRUE ? 0 : -1;
    }
#else
    ret = ioctl(kqemu_fd, KQEMU_SET_PHYS_MEM, kphys_mem);
#endif
    if (ret < 0) {
        fprintf(stderr, "kqemu: KQEMU_SET_PHYS_PAGE error=%d: start_addr=0x%016" PRIx64 " size=0x%08lx phys_offset=0x%08lx\n",
                ret, start_addr, 
                (unsigned long)size, (unsigned long)phys_offset);
    }
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
#ifdef TARGET_X86_64
    if (env->hflags & HF_LMA_MASK) {
        int code64;

        env->regs[R_ECX] = kenv->next_eip;
        env->regs[11] = env->eflags;

        code64 = env->hflags & HF_CS64_MASK;

        cpu_x86_set_cpl(env, 0);
        cpu_x86_load_seg_cache(env, R_CS, selector & 0xfffc,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK | DESC_L_MASK);
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) & 0xfffc,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_W_MASK | DESC_A_MASK);
        env->eflags &= ~env->fmask;
        if (code64)
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

#ifdef CONFIG_PROFILER

#define PC_REC_SIZE 1
#define PC_REC_HASH_BITS 16
#define PC_REC_HASH_SIZE (1 << PC_REC_HASH_BITS)

typedef struct PCRecord {
    unsigned long pc;
    int64_t count;
    struct PCRecord *next;
} PCRecord;

static PCRecord *pc_rec_hash[PC_REC_HASH_SIZE];
static int nb_pc_records;

static void kqemu_record_pc(unsigned long pc)
{
    unsigned long h;
    PCRecord **pr, *r;

    h = pc / PC_REC_SIZE;
    h = h ^ (h >> PC_REC_HASH_BITS);
    h &= (PC_REC_HASH_SIZE - 1);
    pr = &pc_rec_hash[h];
    for(;;) {
        r = *pr;
        if (r == NULL)
            break;
        if (r->pc == pc) {
            r->count++;
            return;
        }
        pr = &r->next;
    }
    r = malloc(sizeof(PCRecord));
    r->count = 1;
    r->pc = pc;
    r->next = NULL;
    *pr = r;
    nb_pc_records++;
}

static int pc_rec_cmp(const void *p1, const void *p2)
{
    PCRecord *r1 = *(PCRecord **)p1;
    PCRecord *r2 = *(PCRecord **)p2;
    if (r1->count < r2->count)
        return 1;
    else if (r1->count == r2->count)
        return 0;
    else
        return -1;
}

static void kqemu_record_flush(void)
{
    PCRecord *r, *r_next;
    int h;

    for(h = 0; h < PC_REC_HASH_SIZE; h++) {
        for(r = pc_rec_hash[h]; r != NULL; r = r_next) {
            r_next = r->next;
            free(r);
        }
        pc_rec_hash[h] = NULL;
    }
    nb_pc_records = 0;
}

void kqemu_record_dump(void)
{
    PCRecord **pr, *r;
    int i, h;
    FILE *f;
    int64_t total, sum;

    pr = malloc(sizeof(PCRecord *) * nb_pc_records);
    i = 0;
    total = 0;
    for(h = 0; h < PC_REC_HASH_SIZE; h++) {
        for(r = pc_rec_hash[h]; r != NULL; r = r->next) {
            pr[i++] = r;
            total += r->count;
        }
    }
    qsort(pr, nb_pc_records, sizeof(PCRecord *), pc_rec_cmp);

    f = fopen("/tmp/kqemu.stats", "w");
    if (!f) {
        perror("/tmp/kqemu.stats");
        exit(1);
    }
    fprintf(f, "total: %" PRId64 "\n", total);
    sum = 0;
    for(i = 0; i < nb_pc_records; i++) {
        r = pr[i];
        sum += r->count;
        fprintf(f, "%08lx: %" PRId64 " %0.2f%% %0.2f%%\n",
                r->pc,
                r->count,
                (double)r->count / (double)total * 100.0,
                (double)sum / (double)total * 100.0);
    }
    fclose(f);
    free(pr);

    kqemu_record_flush();
}
#endif

static inline void kqemu_load_seg(struct kqemu_segment_cache *ksc,
                                  const SegmentCache *sc)
{
    ksc->selector = sc->selector;
    ksc->flags = sc->flags;
    ksc->limit = sc->limit;
    ksc->base = sc->base;
}

static inline void kqemu_save_seg(SegmentCache *sc,
                                  const struct kqemu_segment_cache *ksc)
{
    sc->selector = ksc->selector;
    sc->flags = ksc->flags;
    sc->limit = ksc->limit;
    sc->base = ksc->base;
}

int kqemu_cpu_exec(CPUState *env)
{
    struct kqemu_cpu_state kcpu_state, *kenv = &kcpu_state;
    int ret, cpl, i;
#ifdef CONFIG_PROFILER
    int64_t ti;
#endif
#ifdef _WIN32
    DWORD temp;
#endif

#ifdef CONFIG_PROFILER
    ti = profile_getclock();
#endif
    LOG_INT("kqemu: cpu_exec: enter\n");
    LOG_INT_STATE(env);
    for(i = 0; i < CPU_NB_REGS; i++)
        kenv->regs[i] = env->regs[i];
    kenv->eip = env->eip;
    kenv->eflags = env->eflags;
    for(i = 0; i < 6; i++)
        kqemu_load_seg(&kenv->segs[i], &env->segs[i]);
    kqemu_load_seg(&kenv->ldt, &env->ldt);
    kqemu_load_seg(&kenv->tr, &env->tr);
    kqemu_load_seg(&kenv->gdt, &env->gdt);
    kqemu_load_seg(&kenv->idt, &env->idt);
    kenv->cr0 = env->cr[0];
    kenv->cr2 = env->cr[2];
    kenv->cr3 = env->cr[3];
    kenv->cr4 = env->cr[4];
    kenv->a20_mask = env->a20_mask;
    kenv->efer = env->efer;
    kenv->tsc_offset = 0;
    kenv->star = env->star;
    kenv->sysenter_cs = env->sysenter_cs;
    kenv->sysenter_esp = env->sysenter_esp;
    kenv->sysenter_eip = env->sysenter_eip;
#ifdef TARGET_X86_64
    kenv->lstar = env->lstar;
    kenv->cstar = env->cstar;
    kenv->fmask = env->fmask;
    kenv->kernelgsbase = env->kernelgsbase;
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
    cpl = (env->hflags & HF_CPL_MASK);
    kenv->cpl = cpl;
    kenv->nb_pages_to_flush = nb_pages_to_flush;
    kenv->user_only = (env->kqemu_enabled == 1);
    kenv->nb_ram_pages_to_update = nb_ram_pages_to_update;
    nb_ram_pages_to_update = 0;
    kenv->nb_modified_ram_pages = nb_modified_ram_pages;

    kqemu_reset_modified_ram_pages();

    if (env->cpuid_features & CPUID_FXSR)
        restore_native_fp_fxrstor(env);
    else
        restore_native_fp_frstor(env);

#ifdef _WIN32
    if (DeviceIoControl(kqemu_fd, KQEMU_EXEC,
                        kenv, sizeof(struct kqemu_cpu_state),
                        kenv, sizeof(struct kqemu_cpu_state),
                        &temp, NULL)) {
        ret = kenv->retval;
    } else {
        ret = -1;
    }
#else
    ioctl(kqemu_fd, KQEMU_EXEC, kenv);
    ret = kenv->retval;
#endif
    if (env->cpuid_features & CPUID_FXSR)
        save_native_fp_fxsave(env);
    else
        save_native_fp_fsave(env);

    for(i = 0; i < CPU_NB_REGS; i++)
        env->regs[i] = kenv->regs[i];
    env->eip = kenv->eip;
    env->eflags = kenv->eflags;
    for(i = 0; i < 6; i++)
        kqemu_save_seg(&env->segs[i], &kenv->segs[i]);
    cpu_x86_set_cpl(env, kenv->cpl);
    kqemu_save_seg(&env->ldt, &kenv->ldt);
    env->cr[0] = kenv->cr0;
    env->cr[4] = kenv->cr4;
    env->cr[3] = kenv->cr3;
    env->cr[2] = kenv->cr2;
    env->dr[6] = kenv->dr6;
#ifdef TARGET_X86_64
    env->kernelgsbase = kenv->kernelgsbase;
#endif

    /* flush pages as indicated by kqemu */
    if (kenv->nb_pages_to_flush >= KQEMU_FLUSH_ALL) {
        tlb_flush(env, 1);
    } else {
        for(i = 0; i < kenv->nb_pages_to_flush; i++) {
            tlb_flush_page(env, pages_to_flush[i]);
        }
    }
    nb_pages_to_flush = 0;

#ifdef CONFIG_PROFILER
    kqemu_time += profile_getclock() - ti;
    kqemu_exec_count++;
#endif

    if (kenv->nb_ram_pages_to_update > 0) {
        cpu_tlb_update_dirty(env);
    }

    if (kenv->nb_modified_ram_pages > 0) {
        for(i = 0; i < kenv->nb_modified_ram_pages; i++) {
            unsigned long addr;
            addr = modified_ram_pages[i];
            tb_invalidate_phys_page_range(addr, addr + TARGET_PAGE_SIZE, 0);
        }
    }

    /* restore the hidden flags */
    {
        unsigned int new_hflags;
#ifdef TARGET_X86_64
        if ((env->hflags & HF_LMA_MASK) &&
            (env->segs[R_CS].flags & DESC_L_MASK)) {
            /* long mode */
            new_hflags = HF_CS32_MASK | HF_SS32_MASK | HF_CS64_MASK;
        } else
#endif
        {
            /* legacy / compatibility case */
            new_hflags = (env->segs[R_CS].flags & DESC_B_MASK)
                >> (DESC_B_SHIFT - HF_CS32_SHIFT);
            new_hflags |= (env->segs[R_SS].flags & DESC_B_MASK)
                >> (DESC_B_SHIFT - HF_SS32_SHIFT);
            if (!(env->cr[0] & CR0_PE_MASK) ||
                   (env->eflags & VM_MASK) ||
                   !(env->hflags & HF_CS32_MASK)) {
                /* XXX: try to avoid this test. The problem comes from the
                   fact that is real mode or vm86 mode we only modify the
                   'base' and 'selector' fields of the segment cache to go
                   faster. A solution may be to force addseg to one in
                   translate-i386.c. */
                new_hflags |= HF_ADDSEG_MASK;
            } else {
                new_hflags |= ((env->segs[R_DS].base |
                                env->segs[R_ES].base |
                                env->segs[R_SS].base) != 0) <<
                    HF_ADDSEG_SHIFT;
            }
        }
        env->hflags = (env->hflags &
           ~(HF_CS32_MASK | HF_SS32_MASK | HF_CS64_MASK | HF_ADDSEG_MASK)) |
            new_hflags;
    }
    /* update FPU flags */
    env->hflags = (env->hflags & ~(HF_MP_MASK | HF_EM_MASK | HF_TS_MASK)) |
        ((env->cr[0] << (HF_MP_SHIFT - 1)) & (HF_MP_MASK | HF_EM_MASK | HF_TS_MASK));
    if (env->cr[4] & CR4_OSFXSR_MASK)
        env->hflags |= HF_OSFXSR_MASK;
    else
        env->hflags &= ~HF_OSFXSR_MASK;

    LOG_INT("kqemu: kqemu_cpu_exec: ret=0x%x\n", ret);
    if (ret == KQEMU_RET_SYSCALL) {
        /* syscall instruction */
        return do_syscall(env, kenv);
    } else
    if ((ret & 0xff00) == KQEMU_RET_INT) {
        env->exception_index = ret & 0xff;
        env->error_code = 0;
        env->exception_is_int = 1;
        env->exception_next_eip = kenv->next_eip;
#ifdef CONFIG_PROFILER
        kqemu_ret_int_count++;
#endif
        LOG_INT("kqemu: interrupt v=%02x:\n", env->exception_index);
        LOG_INT_STATE(env);
        return 1;
    } else if ((ret & 0xff00) == KQEMU_RET_EXCEPTION) {
        env->exception_index = ret & 0xff;
        env->error_code = kenv->error_code;
        env->exception_is_int = 0;
        env->exception_next_eip = 0;
#ifdef CONFIG_PROFILER
        kqemu_ret_excp_count++;
#endif
        LOG_INT("kqemu: exception v=%02x e=%04x:\n",
                    env->exception_index, env->error_code);
        LOG_INT_STATE(env);
        return 1;
    } else if (ret == KQEMU_RET_INTR) {
#ifdef CONFIG_PROFILER
        kqemu_ret_intr_count++;
#endif
        LOG_INT_STATE(env);
        return 0;
    } else if (ret == KQEMU_RET_SOFTMMU) {
#ifdef CONFIG_PROFILER
        {
            unsigned long pc = env->eip + env->segs[R_CS].base;
            kqemu_record_pc(pc);
        }
#endif
        LOG_INT_STATE(env);
        return 2;
    } else {
        cpu_dump_state(env, stderr, fprintf, 0);
        fprintf(stderr, "Unsupported return value: 0x%x\n", ret);
        exit(1);
    }
    return 0;
}

void kqemu_cpu_interrupt(CPUState *env)
{
#if defined(_WIN32)
    /* cancelling the I/O request causes KQEMU to finish executing the
       current block and successfully returning. */
    CancelIo(kqemu_fd);
#endif
}

/* 
   QEMU paravirtualization interface. The current interface only
   allows to modify the IF and IOPL flags when running in
   kqemu.

   At this point it is not very satisfactory. I leave it for reference
   as it adds little complexity.
*/

#define QPI_COMM_PAGE_PHYS_ADDR 0xff000000

static uint32_t qpi_mem_readb(void *opaque, target_phys_addr_t addr)
{
    return 0;
}

static uint32_t qpi_mem_readw(void *opaque, target_phys_addr_t addr)
{
    return 0;
}

static void qpi_mem_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
}

static void qpi_mem_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
}

static uint32_t qpi_mem_readl(void *opaque, target_phys_addr_t addr)
{
    CPUState *env;

    env = cpu_single_env;
    if (!env)
        return 0;
    return env->eflags & (IF_MASK | IOPL_MASK);
}

/* Note: after writing to this address, the guest code must make sure
   it is exiting the current TB. pushf/popf can be used for that
   purpose. */
static void qpi_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    CPUState *env;

    env = cpu_single_env;
    if (!env)
        return;
    env->eflags = (env->eflags & ~(IF_MASK | IOPL_MASK)) | 
        (val & (IF_MASK | IOPL_MASK));
}

static CPUReadMemoryFunc *qpi_mem_read[3] = {
    qpi_mem_readb,
    qpi_mem_readw,
    qpi_mem_readl,
};

static CPUWriteMemoryFunc *qpi_mem_write[3] = {
    qpi_mem_writeb,
    qpi_mem_writew,
    qpi_mem_writel,
};

static void qpi_init(void)
{
    kqemu_comm_base = 0xff000000 | 1;
    qpi_io_memory = cpu_register_io_memory(0, 
                                           qpi_mem_read, 
                                           qpi_mem_write, NULL);
    cpu_register_physical_memory(kqemu_comm_base & ~0xfff, 
                                 0x1000, qpi_io_memory);
}
#endif
