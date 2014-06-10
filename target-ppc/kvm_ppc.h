/*
 * Copyright 2008 IBM Corporation.
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#ifndef __KVM_PPC_H__
#define __KVM_PPC_H__

#define TYPE_HOST_POWERPC_CPU "host-" TYPE_POWERPC_CPU

void kvmppc_init(void);

#ifdef CONFIG_KVM

uint32_t kvmppc_get_tbfreq(void);
uint64_t kvmppc_get_clockfreq(void);
uint32_t kvmppc_get_vmx(void);
uint32_t kvmppc_get_dfp(void);
int kvmppc_get_hasidle(CPUPPCState *env);
int kvmppc_get_hypercall(CPUPPCState *env, uint8_t *buf, int buf_len);
int kvmppc_set_interrupt(PowerPCCPU *cpu, int irq, int level);
void kvmppc_set_papr(PowerPCCPU *cpu);
int kvmppc_set_compat(PowerPCCPU *cpu, uint32_t cpu_version);
void kvmppc_set_mpic_proxy(PowerPCCPU *cpu, int mpic_proxy);
int kvmppc_smt_threads(void);
int kvmppc_clear_tsr_bits(PowerPCCPU *cpu, uint32_t tsr_bits);
int kvmppc_or_tsr_bits(PowerPCCPU *cpu, uint32_t tsr_bits);
int kvmppc_set_tcr(PowerPCCPU *cpu);
int kvmppc_booke_watchdog_enable(PowerPCCPU *cpu);
#ifndef CONFIG_USER_ONLY
off_t kvmppc_alloc_rma(const char *name, MemoryRegion *sysmem);
bool kvmppc_spapr_use_multitce(void);
void *kvmppc_create_spapr_tce(uint32_t liobn, uint32_t window_size, int *pfd,
                              bool vfio_accel);
int kvmppc_remove_spapr_tce(void *table, int pfd, uint32_t window_size);
int kvmppc_reset_htab(int shift_hint);
uint64_t kvmppc_rma_size(uint64_t current_size, unsigned int hash_shift);
#endif /* !CONFIG_USER_ONLY */
bool kvmppc_has_cap_epr(void);
int kvmppc_define_rtas_kernel_token(uint32_t token, const char *function);
bool kvmppc_has_cap_htab_fd(void);
int kvmppc_get_htab_fd(bool write);
int kvmppc_save_htab(QEMUFile *f, int fd, size_t bufsize, int64_t max_ns);
int kvmppc_load_htab_chunk(QEMUFile *f, int fd, uint32_t index,
                           uint16_t n_valid, uint16_t n_invalid);
uint64_t kvmppc_hash64_read_pteg(PowerPCCPU *cpu, target_ulong pte_index);
void kvmppc_hash64_free_pteg(uint64_t token);

void kvmppc_hash64_write_pte(CPUPPCState *env, target_ulong pte_index,
                             target_ulong pte0, target_ulong pte1);
bool kvmppc_has_cap_fixup_hcalls(void);

#else

static inline uint32_t kvmppc_get_tbfreq(void)
{
    return 0;
}

static inline uint64_t kvmppc_get_clockfreq(void)
{
    return 0;
}

static inline uint32_t kvmppc_get_vmx(void)
{
    return 0;
}

static inline uint32_t kvmppc_get_dfp(void)
{
    return 0;
}

static inline int kvmppc_get_hasidle(CPUPPCState *env)
{
    return 0;
}

static inline int kvmppc_get_hypercall(CPUPPCState *env, uint8_t *buf, int buf_len)
{
    return -1;
}

static inline int kvmppc_read_segment_page_sizes(uint32_t *prop, int maxcells)
{
    return -1;
}

static inline int kvmppc_set_interrupt(PowerPCCPU *cpu, int irq, int level)
{
    return -1;
}

static inline void kvmppc_set_papr(PowerPCCPU *cpu)
{
}

static inline int kvmppc_set_compat(PowerPCCPU *cpu, uint32_t cpu_version)
{
    return 0;
}

static inline void kvmppc_set_mpic_proxy(PowerPCCPU *cpu, int mpic_proxy)
{
}

static inline int kvmppc_smt_threads(void)
{
    return 1;
}

static inline int kvmppc_or_tsr_bits(PowerPCCPU *cpu, uint32_t tsr_bits)
{
    return 0;
}

static inline int kvmppc_clear_tsr_bits(PowerPCCPU *cpu, uint32_t tsr_bits)
{
    return 0;
}

static inline int kvmppc_set_tcr(PowerPCCPU *cpu)
{
    return 0;
}

static inline int kvmppc_booke_watchdog_enable(PowerPCCPU *cpu)
{
    return -1;
}

#ifndef CONFIG_USER_ONLY
static inline off_t kvmppc_alloc_rma(const char *name, MemoryRegion *sysmem)
{
    return 0;
}

static inline bool kvmppc_spapr_use_multitce(void)
{
    return false;
}

static inline void *kvmppc_create_spapr_tce(uint32_t liobn,
                                            uint32_t window_size, int *fd,
                                            bool vfio_accel)
{
    return NULL;
}

static inline int kvmppc_remove_spapr_tce(void *table, int pfd,
                                          uint32_t nb_table)
{
    return -1;
}

static inline int kvmppc_reset_htab(int shift_hint)
{
    return -1;
}

static inline uint64_t kvmppc_rma_size(uint64_t current_size,
                                       unsigned int hash_shift)
{
    return ram_size;
}

static inline int kvmppc_update_sdr1(CPUPPCState *env)
{
    return 0;
}

#endif /* !CONFIG_USER_ONLY */

static inline bool kvmppc_has_cap_epr(void)
{
    return false;
}

static inline int kvmppc_define_rtas_kernel_token(uint32_t token,
                                                  const char *function)
{
    return -1;
}

static inline bool kvmppc_has_cap_htab_fd(void)
{
    return false;
}

static inline int kvmppc_get_htab_fd(bool write)
{
    return -1;
}

static inline int kvmppc_save_htab(QEMUFile *f, int fd, size_t bufsize,
                                   int64_t max_ns)
{
    abort();
}

static inline int kvmppc_load_htab_chunk(QEMUFile *f, int fd, uint32_t index,
                                         uint16_t n_valid, uint16_t n_invalid)
{
    abort();
}

static inline uint64_t kvmppc_hash64_read_pteg(PowerPCCPU *cpu,
                                               target_ulong pte_index)
{
    abort();
}

static inline void kvmppc_hash64_free_pteg(uint64_t token)
{
    abort();
}

static inline void kvmppc_hash64_write_pte(CPUPPCState *env,
                                           target_ulong pte_index,
                                           target_ulong pte0, target_ulong pte1)
{
    abort();
}

static inline bool kvmppc_has_cap_fixup_hcalls(void)
{
    abort();
}

#endif

#ifndef CONFIG_KVM
#define kvmppc_eieio() do { } while (0)
#else
#define kvmppc_eieio() \
    do {                                          \
        if (kvm_enabled()) {                          \
            asm volatile("eieio" : : : "memory"); \
        } \
    } while (0)
#endif

#ifndef KVM_INTERRUPT_SET
#define KVM_INTERRUPT_SET -1
#endif

#ifndef KVM_INTERRUPT_UNSET
#define KVM_INTERRUPT_UNSET -2
#endif

#ifndef KVM_INTERRUPT_SET_LEVEL
#define KVM_INTERRUPT_SET_LEVEL -3
#endif

#endif /* __KVM_PPC_H__ */
