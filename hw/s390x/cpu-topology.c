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
#include "qapi/qapi-commands-machine-target.h"
#include "qapi/qapi-events-machine-target.h"

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
    return s390_has_feat(S390_FEAT_CONFIGURATION_TOPOLOGY);
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

/*
 * s390_handle_ptf:
 *
 * @register 1: contains the function code
 *
 * Function codes 0 (horizontal) and 1 (vertical) define the CPU
 * polarization requested by the guest.
 *
 * Function code 2 is handling topology changes and is interpreted
 * by the SIE.
 */
void s390_handle_ptf(S390CPU *cpu, uint8_t r1, uintptr_t ra)
{
    CpuS390Polarization polarization;
    CPUS390XState *env = &cpu->env;
    uint64_t reg = env->regs[r1];
    int fc = reg & S390_TOPO_FC_MASK;

    if (!s390_has_feat(S390_FEAT_CONFIGURATION_TOPOLOGY)) {
        s390_program_interrupt(env, PGM_OPERATION, ra);
        return;
    }

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return;
    }

    if (reg & ~S390_TOPO_FC_MASK) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    polarization = S390_CPU_POLARIZATION_VERTICAL;
    switch (fc) {
    case 0:
        polarization = S390_CPU_POLARIZATION_HORIZONTAL;
        /* fallthrough */
    case 1:
        if (s390_topology.polarization == polarization) {
            env->regs[r1] |= S390_PTF_REASON_DONE;
            setcc(cpu, 2);
        } else {
            s390_topology.polarization = polarization;
            s390_cpu_topology_set_changed(true);
            qapi_event_send_cpu_polarization_change(polarization);
            setcc(cpu, 0);
        }
        break;
    default:
        /* Note that fc == 2 is interpreted by the SIE */
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }
}

/**
 * s390_topology_reset:
 *
 * Generic reset for CPU topology, calls s390_topology_reset()
 * to reset the kernel Modified Topology Change Record.
 */
void s390_topology_reset(void)
{
    s390_cpu_topology_set_changed(false);
    s390_topology.polarization = S390_CPU_POLARIZATION_HORIZONTAL;
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
 * s390_topology_need_report
 * @cpu: Current cpu
 * @drawer_id: future drawer ID
 * @book_id: future book ID
 * @socket_id: future socket ID
 * @entitlement: future entitlement
 * @dedicated: future dedicated
 *
 * A modified topology change report is needed if the topology
 * tree or the topology attributes change.
 */
static bool s390_topology_need_report(S390CPU *cpu, int drawer_id,
                                      int book_id, int socket_id,
                                      uint16_t entitlement, bool dedicated)
{
    return cpu->env.drawer_id != drawer_id ||
           cpu->env.book_id != book_id ||
           cpu->env.socket_id != socket_id ||
           cpu->env.entitlement != entitlement ||
           cpu->env.dedicated != dedicated;
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

static void s390_change_topology(uint16_t core_id,
                                 bool has_socket_id, uint16_t socket_id,
                                 bool has_book_id, uint16_t book_id,
                                 bool has_drawer_id, uint16_t drawer_id,
                                 bool has_entitlement,
                                 CpuS390Entitlement entitlement,
                                 bool has_dedicated, bool dedicated,
                                 Error **errp)
{
    MachineState *ms = current_machine;
    int old_socket_entry;
    int new_socket_entry;
    bool report_needed;
    S390CPU *cpu;

    cpu = s390_cpu_addr2state(core_id);
    if (!cpu) {
        error_setg(errp, "Core-id %d does not exist!", core_id);
        return;
    }

    /* Get attributes not provided from cpu and verify the new topology */
    if (!has_socket_id) {
        socket_id = cpu->env.socket_id;
    }
    if (!has_book_id) {
        book_id = cpu->env.book_id;
    }
    if (!has_drawer_id) {
        drawer_id = cpu->env.drawer_id;
    }
    if (!has_dedicated) {
        dedicated = cpu->env.dedicated;
    }

    /*
     * When the user specifies the entitlement as 'auto' on the command line,
     * QEMU will set the entitlement as:
     * Medium when the CPU is not dedicated.
     * High when dedicated is true.
     */
    if (!has_entitlement || entitlement == S390_CPU_ENTITLEMENT_AUTO) {
        if (dedicated) {
            entitlement = S390_CPU_ENTITLEMENT_HIGH;
        } else {
            entitlement = S390_CPU_ENTITLEMENT_MEDIUM;
        }
    }

    if (!s390_topology_check(socket_id, book_id, drawer_id,
                             entitlement, dedicated, errp)) {
        return;
    }

    /* Check for space on new socket */
    old_socket_entry = s390_socket_nb(cpu);
    new_socket_entry = s390_socket_nb_from_ids(drawer_id, book_id, socket_id);

    if (new_socket_entry != old_socket_entry) {
        if (s390_topology.cores_per_socket[new_socket_entry] >=
            ms->smp.cores) {
            error_setg(errp, "No more space on this socket");
            return;
        }
        /* Update the count of cores in sockets */
        s390_topology.cores_per_socket[new_socket_entry] += 1;
        s390_topology.cores_per_socket[old_socket_entry] -= 1;
    }

    /* Check if we will need to report the modified topology */
    report_needed = s390_topology_need_report(cpu, drawer_id, book_id,
                                              socket_id, entitlement,
                                              dedicated);

    /* All checks done, report new topology into the vCPU */
    cpu->env.drawer_id = drawer_id;
    cpu->env.book_id = book_id;
    cpu->env.socket_id = socket_id;
    cpu->env.dedicated = dedicated;
    cpu->env.entitlement = entitlement;

    /* topology tree is reflected in props */
    s390_update_cpu_props(ms, cpu);

    /* Advertise the topology change */
    if (report_needed) {
        s390_cpu_topology_set_changed(true);
    }
}

void qmp_set_cpu_topology(uint16_t core,
                          bool has_socket, uint16_t socket,
                          bool has_book, uint16_t book,
                          bool has_drawer, uint16_t drawer,
                          bool has_entitlement, CpuS390Entitlement entitlement,
                          bool has_dedicated, bool dedicated,
                          Error **errp)
{
    if (!s390_has_topology()) {
        error_setg(errp, "This machine doesn't support topology");
        return;
    }

    s390_change_topology(core, has_socket, socket, has_book, book,
                         has_drawer, drawer, has_entitlement, entitlement,
                         has_dedicated, dedicated, errp);
}

CpuPolarizationInfo *qmp_query_s390x_cpu_polarization(Error **errp)
{
    CpuPolarizationInfo *info = g_new0(CpuPolarizationInfo, 1);

    info->polarization = s390_topology.polarization;
    return info;
}
