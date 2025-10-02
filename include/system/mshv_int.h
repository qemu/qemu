/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors: Ziqiao Zhou  <ziqiaozhou@microsoft.com>
 *          Magnus Kulke <magnuskulke@microsoft.com>
 *          Jinank Jain  <jinankjain@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef QEMU_MSHV_INT_H
#define QEMU_MSHV_INT_H

struct AccelCPUState {
    int cpufd;
    bool dirty;
};

typedef struct MshvMemoryListener {
    MemoryListener listener;
    int as_id;
} MshvMemoryListener;

typedef struct MshvAddressSpace {
    MshvMemoryListener *ml;
    AddressSpace *as;
} MshvAddressSpace;

struct MshvState {
    AccelState parent_obj;
    int vm;
    MshvMemoryListener memory_listener;
    /* number of listeners */
    int nr_as;
    MshvAddressSpace *as;
    int fd;
};

typedef struct MshvMsiControl {
    bool updated;
    GHashTable *gsi_routes;
} MshvMsiControl;

void mshv_arch_amend_proc_features(
    union hv_partition_synthetic_processor_features *features);
int mshv_arch_post_init_vm(int vm_fd);

#if defined COMPILING_PER_TARGET && defined CONFIG_MSHV_IS_POSSIBLE
int mshv_hvcall(int fd, const struct mshv_root_hvcall *args);
#endif

/* memory */
typedef struct MshvMemoryRegion {
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
    bool readonly;
} MshvMemoryRegion;

void mshv_set_phys_mem(MshvMemoryListener *mml, MemoryRegionSection *section,
                       bool add);

/* interrupt */
void mshv_init_msicontrol(void);
int mshv_reserve_ioapic_msi_routes(int vm_fd);

#endif
