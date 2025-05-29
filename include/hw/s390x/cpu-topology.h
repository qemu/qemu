/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * CPU Topology
 *
 * Copyright IBM Corp. 2022, 2023
 * Author(s): Pierre Morel <pmorel@linux.ibm.com>
 *
 */
#ifndef HW_S390X_CPU_TOPOLOGY_H
#define HW_S390X_CPU_TOPOLOGY_H

#ifndef CONFIG_USER_ONLY

#include "qemu/queue.h"
#include "hw/boards.h"
#include "qapi/qapi-types-machine-s390x.h"

#define S390_TOPOLOGY_CPU_IFL   0x03

typedef struct S390TopologyId {
    uint8_t sentinel;
    uint8_t drawer;
    uint8_t book;
    uint8_t socket;
    uint8_t type;
    uint8_t vertical:1;
    uint8_t entitlement:2;
    uint8_t dedicated;
    uint8_t origin;
} S390TopologyId;

typedef struct S390TopologyEntry {
    QTAILQ_ENTRY(S390TopologyEntry) next;
    S390TopologyId id;
    uint64_t mask;
} S390TopologyEntry;

typedef struct S390Topology {
    uint8_t *cores_per_socket;
    S390CpuPolarization polarization;
} S390Topology;

typedef QTAILQ_HEAD(, S390TopologyEntry) S390TopologyList;

#ifdef CONFIG_KVM
bool s390_has_topology(void);
void s390_topology_setup_cpu(MachineState *ms, S390CPU *cpu, Error **errp);
void s390_topology_reset(void);
#else
static inline bool s390_has_topology(void)
{
    return false;
}
static inline void s390_topology_setup_cpu(MachineState *ms,
                                           S390CPU *cpu,
                                           Error **errp) {}
static inline void s390_topology_reset(void)
{
    /* Unreachable, CPU topology not implemented for TCG */
    g_assert_not_reached();
}
#endif

extern S390Topology s390_topology;

static inline int s390_std_socket(int n, CpuTopology *smp)
{
    return (n / smp->cores) % smp->sockets;
}

static inline int s390_std_book(int n, CpuTopology *smp)
{
    return (n / (smp->cores * smp->sockets)) % smp->books;
}

static inline int s390_std_drawer(int n, CpuTopology *smp)
{
    return (n / (smp->cores * smp->sockets * smp->books)) % smp->drawers;
}

#endif /* CONFIG_USER_ONLY */

#endif
