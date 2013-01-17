/*
 * Virtio interfaces for s390
 *
 * Copyright 2012 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390_VIRTIO_H
#define HW_S390_VIRTIO_H 1

#define KVM_S390_VIRTIO_NOTIFY          0
#define KVM_S390_VIRTIO_RESET           1
#define KVM_S390_VIRTIO_SET_STATUS      2

typedef int (*s390_virtio_fn)(const uint64_t *args);
void s390_register_virtio_hypercall(uint64_t code, s390_virtio_fn fn);

#endif
