/*
 * Copyright 2008 IBM Corporation.
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#ifndef __KVM_PPC_H__
#define __KVM_PPC_H__

void kvmppc_init(void);
void kvmppc_fdt_update(void *fdt);
int kvmppc_read_host_property(const char *node_path, const char *prop,
                                     void *val, size_t len);

uint32_t kvmppc_get_tbfreq(void);
int kvmppc_get_hypercall(CPUState *env, uint8_t *buf, int buf_len);
int kvmppc_set_interrupt(CPUState *env, int irq, int level);

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
