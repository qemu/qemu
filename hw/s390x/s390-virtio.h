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
#define HW_S390_VIRTIO_H

#include "hw/nmi.h"
#include "standard-headers/asm-s390/kvm_virtio.h"
#include "standard-headers/asm-s390/virtio-ccw.h"

typedef int (*s390_virtio_fn)(const uint64_t *args);
void s390_register_virtio_hypercall(uint64_t code, s390_virtio_fn fn);

void s390_init_cpus(MachineState *machine);
void s390_init_ipl_dev(const char *kernel_filename,
                       const char *kernel_cmdline,
                       const char *initrd_filename,
                       const char *firmware,
                       const char *netboot_fw,
                       bool enforce_bios);
void s390_create_virtio_net(BusState *bus, const char *name);
void s390_nmi(NMIState *n, int cpu_index, Error **errp);
void s390_machine_reset(void);
void s390_memory_init(ram_addr_t mem_size);
#endif
