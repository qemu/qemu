/*
 * QEMU PowerPC pSeries Logical Partition NUMA associativity handling
 *
 * Copyright IBM Corp. 2020
 *
 * Authors:
 *  Daniel Henrique Barboza      <danielhb413@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/ppc/spapr_numa.h"
#include "hw/pci-host/spapr.h"
#include "hw/ppc/fdt.h"

/* Moved from hw/ppc/spapr_pci_nvlink2.c */
#define SPAPR_GPU_NUMA_ID           (cpu_to_be32(1))

static bool spapr_numa_is_symmetrical(MachineState *ms)
{
    int src, dst;
    int nb_numa_nodes = ms->numa_state->num_nodes;
    NodeInfo *numa_info = ms->numa_state->nodes;

    for (src = 0; src < nb_numa_nodes; src++) {
        for (dst = src; dst < nb_numa_nodes; dst++) {
            if (numa_info[src].distance[dst] !=
                numa_info[dst].distance[src]) {
                return false;
            }
        }
    }

    return true;
}

void spapr_numa_associativity_init(SpaprMachineState *spapr,
                                   MachineState *machine)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    int nb_numa_nodes = machine->numa_state->num_nodes;
    int i, j, max_nodes_with_gpus;

    /*
     * For all associativity arrays: first position is the size,
     * position MAX_DISTANCE_REF_POINTS is always the numa_id,
     * represented by the index 'i'.
     *
     * This will break on sparse NUMA setups, when/if QEMU starts
     * to support it, because there will be no more guarantee that
     * 'i' will be a valid node_id set by the user.
     */
    for (i = 0; i < nb_numa_nodes; i++) {
        spapr->numa_assoc_array[i][0] = cpu_to_be32(MAX_DISTANCE_REF_POINTS);
        spapr->numa_assoc_array[i][MAX_DISTANCE_REF_POINTS] = cpu_to_be32(i);
    }

    /*
     * Initialize NVLink GPU associativity arrays. We know that
     * the first GPU will take the first available NUMA id, and
     * we'll have a maximum of NVGPU_MAX_NUM GPUs in the machine.
     * At this point we're not sure if there are GPUs or not, but
     * let's initialize the associativity arrays and allow NVLink
     * GPUs to be handled like regular NUMA nodes later on.
     */
    max_nodes_with_gpus = nb_numa_nodes + NVGPU_MAX_NUM;

    for (i = nb_numa_nodes; i < max_nodes_with_gpus; i++) {
        spapr->numa_assoc_array[i][0] = cpu_to_be32(MAX_DISTANCE_REF_POINTS);

        for (j = 1; j < MAX_DISTANCE_REF_POINTS; j++) {
            uint32_t gpu_assoc = smc->pre_5_1_assoc_refpoints ?
                                 SPAPR_GPU_NUMA_ID : cpu_to_be32(i);
            spapr->numa_assoc_array[i][j] = gpu_assoc;
        }

        spapr->numa_assoc_array[i][MAX_DISTANCE_REF_POINTS] = cpu_to_be32(i);
    }

    /*
     * Legacy NUMA guests (pseries-5.1 and older, or guests with only
     * 1 NUMA node) will not benefit from anything we're going to do
     * after this point.
     */
    if (spapr_machine_using_legacy_numa(spapr)) {
        return;
    }

    if (!spapr_numa_is_symmetrical(machine)) {
        error_report("Asymmetrical NUMA topologies aren't supported "
                     "in the pSeries machine");
        exit(EXIT_FAILURE);
    }

}

void spapr_numa_write_associativity_dt(SpaprMachineState *spapr, void *fdt,
                                       int offset, int nodeid)
{
    _FDT((fdt_setprop(fdt, offset, "ibm,associativity",
                      spapr->numa_assoc_array[nodeid],
                      sizeof(spapr->numa_assoc_array[nodeid]))));
}

static uint32_t *spapr_numa_get_vcpu_assoc(SpaprMachineState *spapr,
                                           PowerPCCPU *cpu)
{
    uint32_t *vcpu_assoc = g_new(uint32_t, VCPU_ASSOC_SIZE);
    int index = spapr_get_vcpu_id(cpu);

    /*
     * VCPUs have an extra 'cpu_id' value in ibm,associativity
     * compared to other resources. Increment the size at index
     * 0, put cpu_id last, then copy the remaining associativity
     * domains.
     */
    vcpu_assoc[0] = cpu_to_be32(MAX_DISTANCE_REF_POINTS + 1);
    vcpu_assoc[VCPU_ASSOC_SIZE - 1] = cpu_to_be32(index);
    memcpy(vcpu_assoc + 1, spapr->numa_assoc_array[cpu->node_id] + 1,
           (VCPU_ASSOC_SIZE - 2) * sizeof(uint32_t));

    return vcpu_assoc;
}

int spapr_numa_fixup_cpu_dt(SpaprMachineState *spapr, void *fdt,
                            int offset, PowerPCCPU *cpu)
{
    g_autofree uint32_t *vcpu_assoc = NULL;

    vcpu_assoc = spapr_numa_get_vcpu_assoc(spapr, cpu);

    /* Advertise NUMA via ibm,associativity */
    return fdt_setprop(fdt, offset, "ibm,associativity", vcpu_assoc,
                       VCPU_ASSOC_SIZE * sizeof(uint32_t));
}


int spapr_numa_write_assoc_lookup_arrays(SpaprMachineState *spapr, void *fdt,
                                         int offset)
{
    MachineState *machine = MACHINE(spapr);
    int nb_numa_nodes = machine->numa_state->num_nodes;
    int nr_nodes = nb_numa_nodes ? nb_numa_nodes : 1;
    uint32_t *int_buf, *cur_index, buf_len;
    int ret, i;

    /* ibm,associativity-lookup-arrays */
    buf_len = (nr_nodes * MAX_DISTANCE_REF_POINTS + 2) * sizeof(uint32_t);
    cur_index = int_buf = g_malloc0(buf_len);
    int_buf[0] = cpu_to_be32(nr_nodes);
     /* Number of entries per associativity list */
    int_buf[1] = cpu_to_be32(MAX_DISTANCE_REF_POINTS);
    cur_index += 2;
    for (i = 0; i < nr_nodes; i++) {
        /*
         * For the lookup-array we use the ibm,associativity array,
         * from numa_assoc_array. without the first element (size).
         */
        uint32_t *associativity = spapr->numa_assoc_array[i];
        memcpy(cur_index, ++associativity,
               sizeof(uint32_t) * MAX_DISTANCE_REF_POINTS);
        cur_index += MAX_DISTANCE_REF_POINTS;
    }
    ret = fdt_setprop(fdt, offset, "ibm,associativity-lookup-arrays", int_buf,
                      (cur_index - int_buf) * sizeof(uint32_t));
    g_free(int_buf);

    return ret;
}

/*
 * Helper that writes ibm,associativity-reference-points and
 * max-associativity-domains in the RTAS pointed by @rtas
 * in the DT @fdt.
 */
void spapr_numa_write_rtas_dt(SpaprMachineState *spapr, void *fdt, int rtas)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    uint32_t refpoints[] = {
        cpu_to_be32(0x4),
        cpu_to_be32(0x4),
        cpu_to_be32(0x2),
    };
    uint32_t nr_refpoints = ARRAY_SIZE(refpoints);
    uint32_t maxdomain = cpu_to_be32(spapr->gpu_numa_id > 1 ? 1 : 0);
    uint32_t maxdomains[] = {
        cpu_to_be32(4),
        maxdomain,
        maxdomain,
        maxdomain,
        cpu_to_be32(spapr->gpu_numa_id),
    };

    if (smc->pre_5_1_assoc_refpoints) {
        nr_refpoints = 2;
    }

    _FDT(fdt_setprop(fdt, rtas, "ibm,associativity-reference-points",
                     refpoints, nr_refpoints * sizeof(refpoints[0])));

    _FDT(fdt_setprop(fdt, rtas, "ibm,max-associativity-domains",
                     maxdomains, sizeof(maxdomains)));
}

static target_ulong h_home_node_associativity(PowerPCCPU *cpu,
                                              SpaprMachineState *spapr,
                                              target_ulong opcode,
                                              target_ulong *args)
{
    g_autofree uint32_t *vcpu_assoc = NULL;
    target_ulong flags = args[0];
    target_ulong procno = args[1];
    PowerPCCPU *tcpu;
    int idx, assoc_idx;

    /* only support procno from H_REGISTER_VPA */
    if (flags != 0x1) {
        return H_FUNCTION;
    }

    tcpu = spapr_find_cpu(procno);
    if (tcpu == NULL) {
        return H_P2;
    }

    /*
     * Given that we want to be flexible with the sizes and indexes,
     * we must consider that there is a hard limit of how many
     * associativities domain we can fit in R4 up to R9, which would be
     * 12 associativity domains for vcpus. Assert and bail if that's
     * not the case.
     */
    G_STATIC_ASSERT((VCPU_ASSOC_SIZE - 1) <= 12);

    vcpu_assoc = spapr_numa_get_vcpu_assoc(spapr, tcpu);
    /* assoc_idx starts at 1 to skip associativity size */
    assoc_idx = 1;

#define ASSOCIATIVITY(a, b) (((uint64_t)(a) << 32) | \
                             ((uint64_t)(b) & 0xffffffff))

    for (idx = 0; idx < 6; idx++) {
        int32_t a, b;

        /*
         * vcpu_assoc[] will contain the associativity domains for tcpu,
         * including tcpu->node_id and procno, meaning that we don't
         * need to use these variables here.
         *
         * We'll read 2 values at a time to fill up the ASSOCIATIVITY()
         * macro. The ternary will fill the remaining registers with -1
         * after we went through vcpu_assoc[].
         */
        a = assoc_idx < VCPU_ASSOC_SIZE ?
            be32_to_cpu(vcpu_assoc[assoc_idx++]) : -1;
        b = assoc_idx < VCPU_ASSOC_SIZE ?
            be32_to_cpu(vcpu_assoc[assoc_idx++]) : -1;

        args[idx] = ASSOCIATIVITY(a, b);
    }
#undef ASSOCIATIVITY

    return H_SUCCESS;
}

static void spapr_numa_register_types(void)
{
    /* Virtual Processor Home Node */
    spapr_register_hypercall(H_HOME_NODE_ASSOCIATIVITY,
                             h_home_node_associativity);
}

type_init(spapr_numa_register_types)
