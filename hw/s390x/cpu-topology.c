/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * CPU Topology
 *
 * Copyright IBM Corp. 2022, 2023
 * Author(s): Pierre Morel <pmorel@linux.ibm.com>
 *
 * S390 topology handling can be divided in two parts:
 *
 * - The first part in this file is taking care of all common functions
 *   used by KVM and TCG to create and modify the topology.
 *
 * - The second part, building the topology information data for the
 *   guest with CPU and KVM specificity will be implemented inside
 *   the target/s390/kvm sub tree.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "hw/boards.h"
#include "target/s390x/cpu.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "hw/s390x/cpu-topology.h"

/*
 * s390_topology is used to keep the topology information.
 * .cores_per_socket: tracks information on the count of cores
 *                    per socket.
 * .polarization: tracks machine polarization.
 */
S390Topology s390_topology = {
    /* will be initialized after the CPU model is realized */
    .cores_per_socket = NULL,
    .polarization = S390_CPU_POLARIZATION_HORIZONTAL,
};

/**
 * s390_socket_nb:
 * @cpu: s390x CPU
 *
 * Returns the socket number used inside the cores_per_socket array
 * for a topology tree entry
 */
static int s390_socket_nb_from_ids(int drawer_id, int book_id, int socket_id)
{
    return (drawer_id * current_machine->smp.books + book_id) *
           current_machine->smp.sockets + socket_id;
}

/**
 * s390_socket_nb:
 * @cpu: s390x CPU
 *
 * Returns the socket number used inside the cores_per_socket array
 * for a cpu.
 */
static int s390_socket_nb(S390CPU *cpu)
{
    return s390_socket_nb_from_ids(cpu->env.drawer_id, cpu->env.book_id,
                                   cpu->env.socket_id);
}

/**
 * s390_has_topology:
 *
 * Return: true if the topology is supported by the machine.
 */
bool s390_has_topology(void)
{
    return false;
}

/**
 * s390_topology_init:
 * @ms: the machine state where the machine topology is defined
 *
 * Keep track of the machine topology.
 *
 * Allocate an array to keep the count of cores per socket.
 * The index of the array starts at socket 0 from book 0 and
 * drawer 0 up to the maximum allowed by the machine topology.
 */
static void s390_topology_init(MachineState *ms)
{
    CpuTopology *smp = &ms->smp;

    s390_topology.cores_per_socket = g_new0(uint8_t, smp->sockets *
                                            smp->books * smp->drawers);
}

/**
 * s390_topology_cpu_default:
 * @cpu: pointer to a S390CPU
 * @errp: Error pointer
 *
 * Setup the default topology if no attributes are already set.
 * Passing a CPU with some, but not all, attributes set is considered
 * an error.
 *
 * The function calculates the (drawer_id, book_id, socket_id)
 * topology by filling the cores starting from the first socket
 * (0, 0, 0) up to the last (smp->drawers, smp->books, smp->sockets).
 *
 * CPU type and dedication have defaults values set in the
 * s390x_cpu_properties, entitlement must be adjust depending on the
 * dedication.
 *
 * Returns false if it is impossible to setup a default topology
 * true otherwise.
 */
static bool s390_topology_cpu_default(S390CPU *cpu, Error **errp)
{
    CpuTopology *smp = &current_machine->smp;
    CPUS390XState *env = &cpu->env;

    /* All geometry topology attributes must be set or all unset */
    if ((env->socket_id < 0 || env->book_id < 0 || env->drawer_id < 0) &&
        (env->socket_id >= 0 || env->book_id >= 0 || env->drawer_id >= 0)) {
        error_setg(errp,
                   "Please define all or none of the topology geometry attributes");
        return false;
    }

    /* If one value is unset all are unset -> calculate defaults */
    if (env->socket_id < 0) {
        env->socket_id = s390_std_socket(env->core_id, smp);
        env->book_id = s390_std_book(env->core_id, smp);
        env->drawer_id = s390_std_drawer(env->core_id, smp);
    }

    /*
     * When the user specifies the entitlement as 'auto' on the command line,
     * QEMU will set the entitlement as:
     * Medium when the CPU is not dedicated.
     * High when dedicated is true.
     */
    if (env->entitlement == S390_CPU_ENTITLEMENT_AUTO) {
        if (env->dedicated) {
            env->entitlement = S390_CPU_ENTITLEMENT_HIGH;
        } else {
            env->entitlement = S390_CPU_ENTITLEMENT_MEDIUM;
        }
    }
    return true;
}

/**
 * s390_topology_check:
 * @socket_id: socket to check
 * @book_id: book to check
 * @drawer_id: drawer to check
 * @entitlement: entitlement to check
 * @dedicated: dedication to check
 * @errp: Error pointer
 *
 * The function checks if the topology
 * attributes fits inside the system topology.
 *
 * Returns false if the specified topology does not match with
 * the machine topology.
 */
static bool s390_topology_check(uint16_t socket_id, uint16_t book_id,
                                uint16_t drawer_id, uint16_t entitlement,
                                bool dedicated, Error **errp)
{
    CpuTopology *smp = &current_machine->smp;

    if (socket_id >= smp->sockets) {
        error_setg(errp, "Unavailable socket: %d", socket_id);
        return false;
    }
    if (book_id >= smp->books) {
        error_setg(errp, "Unavailable book: %d", book_id);
        return false;
    }
    if (drawer_id >= smp->drawers) {
        error_setg(errp, "Unavailable drawer: %d", drawer_id);
        return false;
    }
    if (entitlement >= S390_CPU_ENTITLEMENT__MAX) {
        error_setg(errp, "Unknown entitlement: %d", entitlement);
        return false;
    }
    if (dedicated && (entitlement == S390_CPU_ENTITLEMENT_LOW ||
                      entitlement == S390_CPU_ENTITLEMENT_MEDIUM)) {
        error_setg(errp, "A dedicated CPU implies high entitlement");
        return false;
    }
    return true;
}

/**
 * s390_update_cpu_props:
 * @ms: the machine state
 * @cpu: the CPU for which to update the properties from the environment.
 *
 */
static void s390_update_cpu_props(MachineState *ms, S390CPU *cpu)
{
    CpuInstanceProperties *props;

    props = &ms->possible_cpus->cpus[cpu->env.core_id].props;

    props->socket_id = cpu->env.socket_id;
    props->book_id = cpu->env.book_id;
    props->drawer_id = cpu->env.drawer_id;
}

/**
 * s390_topology_setup_cpu:
 * @ms: MachineState used to initialize the topology structure on
 *      first call.
 * @cpu: the new S390CPU to insert in the topology structure
 * @errp: the error pointer
 *
 * Called from CPU hotplug to check and setup the CPU attributes
 * before the CPU is inserted in the topology.
 * There is no need to update the MTCR explicitly here because it
 * will be updated by KVM on creation of the new CPU.
 */
void s390_topology_setup_cpu(MachineState *ms, S390CPU *cpu, Error **errp)
{
    int entry;

    /*
     * We do not want to initialize the topology if the CPU model
     * does not support topology, consequently, we have to wait for
     * the first CPU to be realized, which realizes the CPU model
     * to initialize the topology structures.
     *
     * s390_topology_setup_cpu() is called from the CPU hotplug.
     */
    if (!s390_topology.cores_per_socket) {
        s390_topology_init(ms);
    }

    if (!s390_topology_cpu_default(cpu, errp)) {
        return;
    }

    if (!s390_topology_check(cpu->env.socket_id, cpu->env.book_id,
                             cpu->env.drawer_id, cpu->env.entitlement,
                             cpu->env.dedicated, errp)) {
        return;
    }

    /* Do we still have space in the socket */
    entry = s390_socket_nb(cpu);
    if (s390_topology.cores_per_socket[entry] >= ms->smp.cores) {
        error_setg(errp, "No more space on this socket");
        return;
    }

    /* Update the count of cores in sockets */
    s390_topology.cores_per_socket[entry] += 1;

    /* topology tree is reflected in props */
    s390_update_cpu_props(ms, cpu);
}
