/*
 * Copyright 2008 IBM Corporation.
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#ifndef KVM_PPC_H
#define KVM_PPC_H

#include "sysemu/kvm.h"
#include "exec/hwaddr.h"
#include "cpu.h"

#ifdef CONFIG_KVM

uint32_t kvmppc_get_tbfreq(void);
uint64_t kvmppc_get_clockfreq(void);
bool kvmppc_get_host_model(char **buf);
bool kvmppc_get_host_serial(char **buf);
int kvmppc_get_hasidle(CPUPPCState *env);
int kvmppc_get_hypercall(CPUPPCState *env, uint8_t *buf, int buf_len);
int kvmppc_set_interrupt(PowerPCCPU *cpu, int irq, int level);
void kvmppc_enable_logical_ci_hcalls(void);
void kvmppc_enable_set_mode_hcall(void);
void kvmppc_enable_clear_ref_mod_hcalls(void);
void kvmppc_enable_h_page_init(void);
void kvmppc_enable_h_rpt_invalidate(void);
void kvmppc_set_papr(PowerPCCPU *cpu);
int kvmppc_set_compat(PowerPCCPU *cpu, uint32_t compat_pvr);
void kvmppc_set_mpic_proxy(PowerPCCPU *cpu, int mpic_proxy);
bool kvmppc_get_fwnmi(void);
int kvmppc_set_fwnmi(PowerPCCPU *cpu);
int kvmppc_smt_threads(void);
void kvmppc_error_append_smt_possible_hint(Error *const *errp);
int kvmppc_set_smt_threads(int smt);
int kvmppc_clear_tsr_bits(PowerPCCPU *cpu, uint32_t tsr_bits);
int kvmppc_or_tsr_bits(PowerPCCPU *cpu, uint32_t tsr_bits);
int kvmppc_set_tcr(PowerPCCPU *cpu);
int kvmppc_booke_watchdog_enable(PowerPCCPU *cpu);
target_ulong kvmppc_configure_v3_mmu(PowerPCCPU *cpu,
                                     bool radix, bool gtse,
                                     uint64_t proc_tbl);
bool kvmppc_spapr_use_multitce(void);
int kvmppc_spapr_enable_inkernel_multitce(void);
void *kvmppc_create_spapr_tce(uint32_t liobn, uint32_t page_shift,
                              uint64_t bus_offset, uint32_t nb_table,
                              int *pfd, bool need_vfio);
int kvmppc_remove_spapr_tce(void *table, int pfd, uint32_t window_size);
int kvmppc_reset_htab(int shift_hint);
uint64_t kvmppc_vrma_limit(unsigned int hash_shift);
bool kvmppc_has_cap_spapr_vfio(void);
bool kvmppc_has_cap_epr(void);
int kvmppc_define_rtas_kernel_token(uint32_t token, const char *function);
int kvmppc_get_htab_fd(bool write, uint64_t index, Error **errp);
int kvmppc_save_htab(QEMUFile *f, int fd, size_t bufsize, int64_t max_ns);
int kvmppc_load_htab_chunk(QEMUFile *f, int fd, uint32_t index,
                           uint16_t n_valid, uint16_t n_invalid, Error **errp);
void kvmppc_read_hptes(ppc_hash_pte64_t *hptes, hwaddr ptex, int n);
void kvmppc_write_hpte(hwaddr ptex, uint64_t pte0, uint64_t pte1);
bool kvmppc_has_cap_fixup_hcalls(void);
bool kvmppc_has_cap_htm(void);
bool kvmppc_has_cap_mmu_radix(void);
bool kvmppc_has_cap_mmu_hash_v3(void);
bool kvmppc_has_cap_xive(void);
int kvmppc_get_cap_safe_cache(void);
int kvmppc_get_cap_safe_bounds_check(void);
int kvmppc_get_cap_safe_indirect_branch(void);
int kvmppc_get_cap_count_cache_flush_assist(void);
bool kvmppc_has_cap_nested_kvm_hv(void);
int kvmppc_set_cap_nested_kvm_hv(int enable);
int kvmppc_get_cap_large_decr(void);
int kvmppc_enable_cap_large_decr(PowerPCCPU *cpu, int enable);
int kvmppc_has_cap_rpt_invalidate(void);
bool kvmppc_supports_ail_3(void);
int kvmppc_enable_hwrng(void);
int kvmppc_put_books_sregs(PowerPCCPU *cpu);
PowerPCCPUClass *kvm_ppc_get_host_cpu_class(void);
void kvmppc_check_papr_resize_hpt(Error **errp);
int kvmppc_resize_hpt_prepare(PowerPCCPU *cpu, target_ulong flags, int shift);
int kvmppc_resize_hpt_commit(PowerPCCPU *cpu, target_ulong flags, int shift);
bool kvmppc_pvr_workaround_required(PowerPCCPU *cpu);

bool kvmppc_hpt_needs_host_contiguous_pages(void);
void kvm_check_mmu(PowerPCCPU *cpu, Error **errp);
void kvmppc_set_reg_ppc_online(PowerPCCPU *cpu, unsigned int online);
void kvmppc_set_reg_tb_offset(PowerPCCPU *cpu, int64_t tb_offset);

int kvm_handle_nmi(PowerPCCPU *cpu, struct kvm_run *run);

#define kvmppc_eieio() \
    do {                                          \
        if (kvm_enabled()) {                          \
            asm volatile("eieio" : : : "memory"); \
        } \
    } while (0)

/* Store data cache blocks back to memory */
static inline void kvmppc_dcbst_range(PowerPCCPU *cpu, uint8_t *addr, int len)
{
    uint8_t *p;

    for (p = addr; p < addr + len; p += cpu->env.dcache_line_size) {
        asm volatile("dcbst 0,%0" : : "r"(p) : "memory");
    }
}

/* Invalidate instruction cache blocks */
static inline void kvmppc_icbi_range(PowerPCCPU *cpu, uint8_t *addr, int len)
{
    uint8_t *p;

    for (p = addr; p < addr + len; p += cpu->env.icache_line_size) {
        asm volatile("icbi 0,%0" : : "r"(p));
    }
}

#else /* !CONFIG_KVM */

static inline uint32_t kvmppc_get_tbfreq(void)
{
    return 0;
}

static inline bool kvmppc_get_host_model(char **buf)
{
    return false;
}

static inline bool kvmppc_get_host_serial(char **buf)
{
    return false;
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

static inline int kvmppc_get_hypercall(CPUPPCState *env,
                                       uint8_t *buf, int buf_len)
{
    return -1;
}

static inline int kvmppc_set_interrupt(PowerPCCPU *cpu, int irq, int level)
{
    return -1;
}

static inline void kvmppc_enable_logical_ci_hcalls(void)
{
}

static inline void kvmppc_enable_set_mode_hcall(void)
{
}

static inline void kvmppc_enable_clear_ref_mod_hcalls(void)
{
}

static inline void kvmppc_enable_h_page_init(void)
{
}

static inline void kvmppc_enable_h_rpt_invalidate(void)
{
    g_assert_not_reached();
}

static inline void kvmppc_set_papr(PowerPCCPU *cpu)
{
}

static inline int kvmppc_set_compat(PowerPCCPU *cpu, uint32_t compat_pvr)
{
    return 0;
}

static inline void kvmppc_set_mpic_proxy(PowerPCCPU *cpu, int mpic_proxy)
{
}

static inline bool kvmppc_get_fwnmi(void)
{
    return false;
}

static inline int kvmppc_set_fwnmi(PowerPCCPU *cpu)
{
    return -1;
}

static inline int kvmppc_smt_threads(void)
{
    return 1;
}

static inline void kvmppc_error_append_smt_possible_hint(Error *const *errp)
{
    return;
}

static inline int kvmppc_set_smt_threads(int smt)
{
    return 0;
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

static inline target_ulong kvmppc_configure_v3_mmu(PowerPCCPU *cpu,
                                     bool radix, bool gtse,
                                     uint64_t proc_tbl)
{
    return 0;
}

static inline void kvmppc_set_reg_ppc_online(PowerPCCPU *cpu,
                                             unsigned int online)
{
    return;
}

static inline void kvmppc_set_reg_tb_offset(PowerPCCPU *cpu, int64_t tb_offset)
{
}

static inline bool kvmppc_spapr_use_multitce(void)
{
    return false;
}

static inline int kvmppc_spapr_enable_inkernel_multitce(void)
{
    return -1;
}

static inline void *kvmppc_create_spapr_tce(uint32_t liobn, uint32_t page_shift,
                                            uint64_t bus_offset,
                                            uint32_t nb_table,
                                            int *pfd, bool need_vfio)
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
    return 0;
}

static inline uint64_t kvmppc_vrma_limit(unsigned int hash_shift)
{
    g_assert_not_reached();
}

static inline bool kvmppc_hpt_needs_host_contiguous_pages(void)
{
    return false;
}

static inline void kvm_check_mmu(PowerPCCPU *cpu, Error **errp)
{
}

static inline bool kvmppc_has_cap_spapr_vfio(void)
{
    return false;
}

static inline void kvmppc_read_hptes(ppc_hash_pte64_t *hptes,
                                     hwaddr ptex, int n)
{
    abort();
}

static inline void kvmppc_write_hpte(hwaddr ptex, uint64_t pte0, uint64_t pte1)
{
    abort();
}

static inline bool kvmppc_has_cap_epr(void)
{
    return false;
}

static inline int kvmppc_define_rtas_kernel_token(uint32_t token,
                                                  const char *function)
{
    return -1;
}

static inline int kvmppc_get_htab_fd(bool write, uint64_t index, Error **errp)
{
    return -1;
}

static inline int kvmppc_save_htab(QEMUFile *f, int fd, size_t bufsize,
                                   int64_t max_ns)
{
    abort();
}

static inline int kvmppc_load_htab_chunk(QEMUFile *f, int fd, uint32_t index,
                                         uint16_t n_valid, uint16_t n_invalid,
                                         Error **errp)
{
    abort();
}

static inline bool kvmppc_has_cap_fixup_hcalls(void)
{
    abort();
}

static inline bool kvmppc_has_cap_htm(void)
{
    return false;
}

static inline bool kvmppc_has_cap_mmu_radix(void)
{
    return false;
}

static inline bool kvmppc_has_cap_mmu_hash_v3(void)
{
    return false;
}

static inline bool kvmppc_has_cap_xive(void)
{
    return false;
}

static inline int kvmppc_get_cap_safe_cache(void)
{
    return 0;
}

static inline int kvmppc_get_cap_safe_bounds_check(void)
{
    return 0;
}

static inline int kvmppc_get_cap_safe_indirect_branch(void)
{
    return 0;
}

static inline int kvmppc_get_cap_count_cache_flush_assist(void)
{
    return 0;
}

static inline bool kvmppc_has_cap_nested_kvm_hv(void)
{
    return false;
}

static inline int kvmppc_set_cap_nested_kvm_hv(int enable)
{
    return -1;
}

static inline int kvmppc_get_cap_large_decr(void)
{
    return 0;
}

static inline int kvmppc_enable_cap_large_decr(PowerPCCPU *cpu, int enable)
{
    return -1;
}

static inline int kvmppc_has_cap_rpt_invalidate(void)
{
    return false;
}

static inline bool kvmppc_supports_ail_3(void)
{
    return false;
}

static inline int kvmppc_enable_hwrng(void)
{
    return -1;
}

static inline int kvmppc_put_books_sregs(PowerPCCPU *cpu)
{
    abort();
}

static inline PowerPCCPUClass *kvm_ppc_get_host_cpu_class(void)
{
    return NULL;
}

static inline void kvmppc_check_papr_resize_hpt(Error **errp)
{
    return;
}

static inline int kvmppc_resize_hpt_prepare(PowerPCCPU *cpu,
                                            target_ulong flags, int shift)
{
    return -ENOSYS;
}

static inline int kvmppc_resize_hpt_commit(PowerPCCPU *cpu,
                                           target_ulong flags, int shift)
{
    return -ENOSYS;
}

static inline bool kvmppc_pvr_workaround_required(PowerPCCPU *cpu)
{
    return false;
}

#define kvmppc_eieio() do { } while (0)

static inline void kvmppc_dcbst_range(PowerPCCPU *cpu, uint8_t *addr, int len)
{
}

static inline void kvmppc_icbi_range(PowerPCCPU *cpu, uint8_t *addr, int len)
{
}

#endif  /* CONFIG_KVM */

#endif /* KVM_PPC_H */
