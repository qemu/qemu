/*
 * Support for virtio hypercalls on s390x
 *
 * Copyright 2012 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390_VIRTIO_HCALL_H
#define HW_S390_VIRTIO_HCALL_H

#include "standard-headers/asm-s390/kvm_virtio.h"
#include "standard-headers/asm-s390/virtio-ccw.h"

typedef int (*s390_virtio_fn)(const uint64_t *args);
void s390_register_virtio_hypercall(uint64_t code, s390_virtio_fn fn);
int s390_virtio_hypercall(CPUS390XState *env);
#endif /* HW_S390_VIRTIO_HCALL_H */
