/*
 * QEMU Machine core (related to -smp parsing)
 *
 * Copyright (c) 2021 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "qemu/error-report.h"


/*
 * Report information of a machine's supported CPU topology hierarchy.
 * Topology members will be ordered from the largest to the smallest
 * in the string.
 */
static char *cpu_hierarchy_to_string(MachineState *ms)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    GString *s = g_string_new(NULL);

    g_string_append_printf(s, "sockets (%u)", ms->smp.sockets);

    if (mc->smp_props.dies_supported) {
        g_string_append_printf(s, " * dies (%u)", ms->smp.dies);
    }

    if (mc->smp_props.clusters_supported) {
        g_string_append_printf(s, " * clusters (%u)", ms->smp.clusters);
    }

    g_string_append_printf(s, " * cores (%u)", ms->smp.cores);
    g_string_append_printf(s, " * threads (%u)", ms->smp.threads);

    return g_string_free(s, false);
}

/*
 * machine_parse_smp_config: Generic function used to parse the given
 *                           SMP configuration
 *
 * Any missing parameter in "cpus/maxcpus/sockets/cores/threads" will be
 * automatically computed based on the provided ones.
 *
 * In the calculation of omitted sockets/cores/threads: we prefer sockets
 * over cores over threads before 6.2, while preferring cores over sockets
 * over threads since 6.2.
 *
 * In the calculation of cpus/maxcpus: When both maxcpus and cpus are omitted,
 * maxcpus will be computed from the given parameters and cpus will be set
 * equal to maxcpus. When only one of maxcpus and cpus is given then the
 * omitted one will be set to its given counterpart's value. Both maxcpus and
 * cpus may be specified, but maxcpus must be equal to or greater than cpus.
 *
 * For compatibility, apart from the parameters that will be computed, newly
 * introduced topology members which are likely to be target specific should
 * be directly set as 1 if they are omitted (e.g. dies for PC since 4.1).
 */
void machine_parse_smp_config(MachineState *ms,
                              const SMPConfiguration *config, Error **errp)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    unsigned cpus    = config->has_cpus ? config->cpus : 0;
    unsigned sockets = config->has_sockets ? config->sockets : 0;
    unsigned dies    = config->has_dies ? config->dies : 0;
    unsigned clusters = config->has_clusters ? config->clusters : 0;
    unsigned cores   = config->has_cores ? config->cores : 0;
    unsigned threads = config->has_threads ? config->threads : 0;
    unsigned maxcpus = config->has_maxcpus ? config->maxcpus : 0;

    /*
     * Specified CPU topology parameters must be greater than zero,
     * explicit configuration like "cpus=0" is not allowed.
     */
    if ((config->has_cpus && config->cpus == 0) ||
        (config->has_sockets && config->sockets == 0) ||
        (config->has_dies && config->dies == 0) ||
        (config->has_clusters && config->clusters == 0) ||
        (config->has_cores && config->cores == 0) ||
        (config->has_threads && config->threads == 0) ||
        (config->has_maxcpus && config->maxcpus == 0)) {
        warn_report("Deprecated CPU topology (considered invalid): "
                    "CPU topology parameters must be greater than zero");
    }

    /*
     * If not supported by the machine, a topology parameter must be
     * omitted or specified equal to 1.
     */
    if (!mc->smp_props.dies_supported && dies > 1) {
        error_setg(errp, "dies not supported by this machine's CPU topology");
        return;
    }
    if (!mc->smp_props.clusters_supported && clusters > 1) {
        error_setg(errp, "clusters not supported by this machine's CPU topology");
        return;
    }

    dies = dies > 0 ? dies : 1;
    clusters = clusters > 0 ? clusters : 1;

    /* compute missing values based on the provided ones */
    if (cpus == 0 && maxcpus == 0) {
        sockets = sockets > 0 ? sockets : 1;
        cores = cores > 0 ? cores : 1;
        threads = threads > 0 ? threads : 1;
    } else {
        maxcpus = maxcpus > 0 ? maxcpus : cpus;

        if (mc->smp_props.prefer_sockets) {
            /* prefer sockets over cores before 6.2 */
            if (sockets == 0) {
                cores = cores > 0 ? cores : 1;
                threads = threads > 0 ? threads : 1;
                sockets = maxcpus / (dies * clusters * cores * threads);
            } else if (cores == 0) {
                threads = threads > 0 ? threads : 1;
                cores = maxcpus / (sockets * dies * clusters * threads);
            }
        } else {
            /* prefer cores over sockets since 6.2 */
            if (cores == 0) {
                sockets = sockets > 0 ? sockets : 1;
                threads = threads > 0 ? threads : 1;
                cores = maxcpus / (sockets * dies * clusters * threads);
            } else if (sockets == 0) {
                threads = threads > 0 ? threads : 1;
                sockets = maxcpus / (dies * clusters * cores * threads);
            }
        }

        /* try to calculate omitted threads at last */
        if (threads == 0) {
            threads = maxcpus / (sockets * dies * clusters * cores);
        }
    }

    maxcpus = maxcpus > 0 ? maxcpus : sockets * dies * clusters * cores * threads;
    cpus = cpus > 0 ? cpus : maxcpus;

    ms->smp.cpus = cpus;
    ms->smp.sockets = sockets;
    ms->smp.dies = dies;
    ms->smp.clusters = clusters;
    ms->smp.cores = cores;
    ms->smp.threads = threads;
    ms->smp.max_cpus = maxcpus;

    mc->smp_props.has_clusters = config->has_clusters;

    /* sanity-check of the computed topology */
    if (sockets * dies * clusters * cores * threads != maxcpus) {
        g_autofree char *topo_msg = cpu_hierarchy_to_string(ms);
        error_setg(errp, "Invalid CPU topology: "
                   "product of the hierarchy must match maxcpus: "
                   "%s != maxcpus (%u)",
                   topo_msg, maxcpus);
        return;
    }

    if (maxcpus < cpus) {
        g_autofree char *topo_msg = cpu_hierarchy_to_string(ms);
        error_setg(errp, "Invalid CPU topology: "
                   "maxcpus must be equal to or greater than smp: "
                   "%s == maxcpus (%u) < smp_cpus (%u)",
                   topo_msg, maxcpus, cpus);
        return;
    }

    if (ms->smp.cpus < mc->min_cpus) {
        error_setg(errp, "Invalid SMP CPUs %d. The min CPUs "
                   "supported by machine '%s' is %d",
                   ms->smp.cpus,
                   mc->name, mc->min_cpus);
        return;
    }

    if (ms->smp.max_cpus > mc->max_cpus) {
        error_setg(errp, "Invalid SMP CPUs %d. The max CPUs "
                   "supported by machine '%s' is %d",
                   ms->smp.max_cpus,
                   mc->name, mc->max_cpus);
        return;
    }
}

unsigned int machine_topo_get_cores_per_socket(const MachineState *ms)
{
    return ms->smp.cores * ms->smp.clusters * ms->smp.dies;
}

unsigned int machine_topo_get_threads_per_socket(const MachineState *ms)
{
    return ms->smp.threads * machine_topo_get_cores_per_socket(ms);
}
