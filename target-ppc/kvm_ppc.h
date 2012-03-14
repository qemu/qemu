/*
 * Copyright 2008 IBM Corporation.
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#ifndef __KVM_PPC_H__
#define __KVM_PPC_H__

#include "memory.h"

void kvmppc_init(void);

#ifdef CONFIG_KVM

uint32_t kvmppc_get_tbfreq(void);
uint64_t kvmppc_get_clockfreq(void);
uint32_t kvmppc_get_vmx(void);
uint32_t kvmppc_get_dfp(void);
int kvmppc_get_hypercall(CPUPPCState *env, uint8_t *buf, int buf_len);
int kvmppc_set_interrupt(CPUPPCState *env, int irq, int level);
void kvmppc_set_papr(CPUPPCState *env);
int kvmppc_smt_threads(void);
#ifndef CONFIG_USER_ONLY
off_t kvmppc_alloc_rma(const char *name, MemoryRegion *sysmem);
void *kvmppc_create_spapr_tce(uint32_t liobn, uint32_t window_size, int *pfd);
int kvmppc_remove_spapr_tce(void *table, int pfd, uint32_t window_size);
#endif /* !CONFIG_USER_ONLY */
const ppc_def_t *kvmppc_host_cpu_def(void);

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

static inline int kvmppc_get_hypercall(CPUPPCState *env, uint8_t *buf, int buf_len)
{
    return -1;
}

static inline int kvmppc_set_interrupt(CPUPPCState *env, int irq, int level)
{
    return -1;
}

static inline void kvmppc_set_papr(CPUPPCState *env)
{
}

static inline int kvmppc_smt_threads(void)
{
    return 1;
}

#ifndef CONFIG_USER_ONLY
static inline off_t kvmppc_alloc_rma(const char *name, MemoryRegion *sysmem)
{
    return 0;
}

static inline void *kvmppc_create_spapr_tce(uint32_t liobn,
                                            uint32_t window_size, int *fd)
{
    return NULL;
}

static inline int kvmppc_remove_spapr_tce(void *table, int pfd,
                                          uint32_t window_size)
{
    return -1;
}
#endif /* !CONFIG_USER_ONLY */

static inline const ppc_def_t *kvmppc_host_cpu_def(void)
{
    return NULL;
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
