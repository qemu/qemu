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

#endif /* __KVM_PPC_H__ */
