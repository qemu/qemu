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

static bool spapr_machine_using_legacy_numa(SpaprMachineState *spapr)
{
    MachineState *machine = MACHINE(spapr);
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(machine);

    return smc->pre_5_2_numa_associativity ||
           machine->numa_state->num_nodes <= 1;
}

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

/*
 * NVLink2-connected GPU RAM needs to be placed on a separate NUMA node.
 * We assign a new numa ID per GPU in spapr_pci_collect_nvgpu() which is
 * called from vPHB reset handler so we initialize the counter here.
 * If no NUMA is configured from the QEMU side, we start from 1 as GPU RAM
 * must be equally distant from any other node.
 * The final value of spapr->gpu_numa_id is going to be written to
 * max-associativity-domains in spapr_build_fdt().
 */
unsigned int spapr_numa_initial_nvgpu_numa_id(MachineState *machine)
{
    return MAX(1, machine->numa_state->num_nodes);
}

/*
 * This function will translate the user distances into
 * what the kernel understand as possible values: 10
 * (local distance), 20, 40, 80 and 160, and return the equivalent
 * NUMA level for each. Current heuristic is:
 *  - local distance (10) returns numa_level = 0x4, meaning there is
 *    no rounding for local distance
 *  - distances between 11 and 30 inclusive -> rounded to 20,
 *    numa_level = 0x3
 *  - distances between 31 and 60 inclusive -> rounded to 40,
 *    numa_level = 0x2
 *  - distances between 61 and 120 inclusive -> rounded to 80,
 *    numa_level = 0x1
 *  - everything above 120 returns numa_level = 0 to indicate that
 *    there is no match. This will be calculated as disntace = 160
 *    by the kernel (as of v5.9)
 */
static uint8_t spapr_numa_get_numa_level(uint8_t distance)
{
    if (distance == 10) {
        return 0x4;
    } else if (distance > 11 && distance <= 30) {
        return 0x3;
    } else if (distance > 31 && distance <= 60) {
        return 0x2;
    } else if (distance > 61 && distance <= 120) {
        return 0x1;
    }

    return 0;
}

static void spapr_numa_define_associativity_domains(SpaprMachineState *spapr)
{
    MachineState *ms = MACHINE(spapr);
    NodeInfo *numa_info = ms->numa_state->nodes;
    int nb_numa_nodes = ms->numa_state->num_nodes;
    int src, dst, i;

    for (src = 0; src < nb_numa_nodes; src++) {
        for (dst = src; dst < nb_numa_nodes; dst++) {
            /*
             * This is how the associativity domain between A and B
             * is calculated:
             *
             * - get the distance D between them
             * - get the correspondent NUMA level 'n_level' for D
             * - all associativity arrays were initialized with their own
             * numa_ids, and we're calculating the distance in node_id
             * ascending order, starting from node id 0 (the first node
             * retrieved by numa_state). This will have a cascade effect in
             * the algorithm because the associativity domains that node 0
             * defines will be carried over to other nodes, and node 1
             * associativities will be carried over after taking node 0
             * associativities into account, and so on. This happens because
             * we'll assign assoc_src as the associativity domain of dst
             * as well, for all NUMA levels beyond and including n_level.
             *
             * The PPC kernel expects the associativity domains of node 0 to
             * be always 0, and this algorithm will grant that by default.
             */
            uint8_t distance = numa_info[src].distance[dst];
            uint8_t n_level = spapr_numa_get_numa_level(distance);
            uint32_t assoc_src;

            /*
             * n_level = 0 means that the distance is greater than our last
             * rounded value (120). In this case there is no NUMA level match
             * between src and dst and we can skip the remaining of the loop.
             *
             * The Linux kernel will assume that the distance between src and
             * dst, in this case of no match, is 10 (local distance) doubled
             * for each NUMA it didn't match. We have MAX_DISTANCE_REF_POINTS
             * levels (4), so this gives us 10*2*2*2*2 = 160.
             *
             * This logic can be seen in the Linux kernel source code, as of
             * v5.9, in arch/powerpc/mm/numa.c, function __node_distance().
             */
            if (n_level == 0) {
                continue;
            }

            /*
             * We must assign all assoc_src to dst, starting from n_level
             * and going up to 0x1.
             */
            for (i = n_level; i > 0; i--) {
                assoc_src = spapr->numa_assoc_array[src][i];
                spapr->numa_assoc_array[dst][i] = assoc_src;
            }
        }
    }

}

void spapr_numa_associativity_init(SpaprMachineState *spapr,
                                   MachineState *machine)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    int nb_numa_nodes = machine->numa_state->num_nodes;
    int i, j, max_nodes_with_gpus;
    bool using_legacy_numa = spapr_machine_using_legacy_numa(spapr);

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

        /*
         * Fill all associativity domains of non-zero NUMA nodes with
         * node_id. This is required because the default value (0) is
         * considered a match with associativity domains of node 0.
         */
        if (!using_legacy_numa && i != 0) {
            for (j = 1; j < MAX_DISTANCE_REF_POINTS; j++) {
                spapr->numa_assoc_array[i][j] = cpu_to_be32(i);
            }
        }
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
    if (using_legacy_numa) {
        return;
    }

    if (!spapr_numa_is_symmetrical(machine)) {
        error_report("Asymmetrical NUMA topologies aren't supported "
                     "in the pSeries machine");
        exit(EXIT_FAILURE);
    }

    spapr_numa_define_associativity_domains(spapr);
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
    MachineState *ms = MACHINE(spapr);
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    uint32_t number_nvgpus_nodes = spapr->gpu_numa_id -
                                   spapr_numa_initial_nvgpu_numa_id(ms);
    uint32_t refpoints[] = {
        cpu_to_be32(0x4),
        cpu_to_be32(0x3),
        cpu_to_be32(0x2),
        cpu_to_be32(0x1),
    };
    uint32_t nr_refpoints = ARRAY_SIZE(refpoints);
    uint32_t maxdomain = ms->numa_state->num_nodes + number_nvgpus_nodes;
    uint32_t maxdomains[] = {
        cpu_to_be32(4),
        cpu_to_be32(maxdomain),
        cpu_to_be32(maxdomain),
        cpu_to_be32(maxdomain),
        cpu_to_be32(maxdomain)
    };

    if (spapr_machine_using_legacy_numa(spapr)) {
        uint32_t legacy_refpoints[] = {
            cpu_to_be32(0x4),
            cpu_to_be32(0x4),
            cpu_to_be32(0x2),
        };
        uint32_t legacy_maxdomain = spapr->gpu_numa_id > 1 ? 1 : 0;
        uint32_t legacy_maxdomains[] = {
            cpu_to_be32(4),
            cpu_to_be32(legacy_maxdomain),
            cpu_to_be32(legacy_maxdomain),
            cpu_to_be32(legacy_maxdomain),
            cpu_to_be32(spapr->gpu_numa_id),
        };

        G_STATIC_ASSERT(sizeof(legacy_refpoints) <= sizeof(refpoints));
        G_STATIC_ASSERT(sizeof(legacy_maxdomains) <= sizeof(maxdomains));

        nr_refpoints = 3;

        memcpy(refpoints, legacy_refpoints, sizeof(legacy_refpoints));
        memcpy(maxdomains, legacy_maxdomains, sizeof(legacy_maxdomains));

        /* pseries-5.0 and older reference-points array is {0x4, 0x4} */
        if (smc->pre_5_1_assoc_refpoints) {
            nr_refpoints = 2;
        }
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
