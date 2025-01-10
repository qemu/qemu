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

    if (mc->smp_props.drawers_supported) {
        g_string_append_printf(s, "drawers (%u) * ", ms->smp.drawers);
    }

    if (mc->smp_props.books_supported) {
        g_string_append_printf(s, "books (%u) * ", ms->smp.books);
    }

    g_string_append_printf(s, "sockets (%u)", ms->smp.sockets);

    if (mc->smp_props.dies_supported) {
        g_string_append_printf(s, " * dies (%u)", ms->smp.dies);
    }

    if (mc->smp_props.clusters_supported) {
        g_string_append_printf(s, " * clusters (%u)", ms->smp.clusters);
    }

    if (mc->smp_props.modules_supported) {
        g_string_append_printf(s, " * modules (%u)", ms->smp.modules);
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
    unsigned drawers = config->has_drawers ? config->drawers : 0;
    unsigned books   = config->has_books ? config->books : 0;
    unsigned sockets = config->has_sockets ? config->sockets : 0;
    unsigned dies    = config->has_dies ? config->dies : 0;
    unsigned clusters = config->has_clusters ? config->clusters : 0;
    unsigned modules = config->has_modules ? config->modules : 0;
    unsigned cores   = config->has_cores ? config->cores : 0;
    unsigned threads = config->has_threads ? config->threads : 0;
    unsigned maxcpus = config->has_maxcpus ? config->maxcpus : 0;
    unsigned total_cpus;

    /*
     * Specified CPU topology parameters must be greater than zero,
     * explicit configuration like "cpus=0" is not allowed.
     */
    if ((config->has_cpus && config->cpus == 0) ||
        (config->has_drawers && config->drawers == 0) ||
        (config->has_books && config->books == 0) ||
        (config->has_sockets && config->sockets == 0) ||
        (config->has_dies && config->dies == 0) ||
        (config->has_clusters && config->clusters == 0) ||
        (config->has_modules && config->modules == 0) ||
        (config->has_cores && config->cores == 0) ||
        (config->has_threads && config->threads == 0) ||
        (config->has_maxcpus && config->maxcpus == 0)) {
        error_setg(errp, "Invalid CPU topology: "
                   "CPU topology parameters must be greater than zero");
        return;
    }

    /*
     * If not supported by the machine, a topology parameter must
     * not be set to a value greater than 1.
     */
    if (!mc->smp_props.modules_supported &&
        config->has_modules && config->modules > 1) {
        error_setg(errp,
                   "modules > 1 not supported by this machine's CPU topology");
        return;
    }
    modules = modules > 0 ? modules : 1;

    if (!mc->smp_props.clusters_supported &&
        config->has_clusters && config->clusters > 1) {
        error_setg(errp,
                   "clusters > 1 not supported by this machine's CPU topology");
        return;
    }
    clusters = clusters > 0 ? clusters : 1;

    if (!mc->smp_props.dies_supported &&
        config->has_dies && config->dies > 1) {
        error_setg(errp,
                   "dies > 1 not supported by this machine's CPU topology");
        return;
    }
    dies = dies > 0 ? dies : 1;

    if (!mc->smp_props.books_supported &&
        config->has_books && config->books > 1) {
        error_setg(errp,
                   "books > 1 not supported by this machine's CPU topology");
        return;
    }
    books = books > 0 ? books : 1;

    if (!mc->smp_props.drawers_supported &&
        config->has_drawers && config->drawers > 1) {
        error_setg(errp,
                   "drawers > 1 not supported by this machine's CPU topology");
        return;
    }
    drawers = drawers > 0 ? drawers : 1;

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
                sockets = maxcpus /
                          (drawers * books * dies * clusters *
                           modules * cores * threads);
            } else if (cores == 0) {
                threads = threads > 0 ? threads : 1;
                cores = maxcpus /
                        (drawers * books * sockets * dies *
                         clusters * modules * threads);
            }
        } else {
            /* prefer cores over sockets since 6.2 */
            if (cores == 0) {
                sockets = sockets > 0 ? sockets : 1;
                threads = threads > 0 ? threads : 1;
                cores = maxcpus /
                        (drawers * books * sockets * dies *
                         clusters * modules * threads);
            } else if (sockets == 0) {
                threads = threads > 0 ? threads : 1;
                sockets = maxcpus /
                          (drawers * books * dies * clusters *
                           modules * cores * threads);
            }
        }

        /* try to calculate omitted threads at last */
        if (threads == 0) {
            threads = maxcpus /
                      (drawers * books * sockets * dies *
                       clusters * modules * cores);
        }
    }

    total_cpus = drawers * books * sockets * dies *
                 clusters * modules * cores * threads;
    maxcpus = maxcpus > 0 ? maxcpus : total_cpus;
    cpus = cpus > 0 ? cpus : maxcpus;

    ms->smp.cpus = cpus;
    ms->smp.drawers = drawers;
    ms->smp.books = books;
    ms->smp.sockets = sockets;
    ms->smp.dies = dies;
    ms->smp.clusters = clusters;
    ms->smp.modules = modules;
    ms->smp.cores = cores;
    ms->smp.threads = threads;
    ms->smp.max_cpus = maxcpus;

    mc->smp_props.has_clusters = config->has_clusters;

    /* sanity-check of the computed topology */
    if (total_cpus != maxcpus) {
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

static bool machine_check_topo_support(MachineState *ms,
                                       CpuTopologyLevel topo,
                                       Error **errp)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);

    if ((topo == CPU_TOPOLOGY_LEVEL_MODULE && !mc->smp_props.modules_supported) ||
        (topo == CPU_TOPOLOGY_LEVEL_CLUSTER && !mc->smp_props.clusters_supported) ||
        (topo == CPU_TOPOLOGY_LEVEL_DIE && !mc->smp_props.dies_supported) ||
        (topo == CPU_TOPOLOGY_LEVEL_BOOK && !mc->smp_props.books_supported) ||
        (topo == CPU_TOPOLOGY_LEVEL_DRAWER && !mc->smp_props.drawers_supported)) {
        error_setg(errp,
                   "Invalid topology level: %s. "
                   "The topology level is not supported by this machine",
                   CpuTopologyLevel_str(topo));
        return false;
    }

    return true;
}

bool machine_parse_smp_cache(MachineState *ms,
                             const SmpCachePropertiesList *caches,
                             Error **errp)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const SmpCachePropertiesList *node;
    DECLARE_BITMAP(caches_bitmap, CACHE_LEVEL_AND_TYPE__MAX);

    bitmap_zero(caches_bitmap, CACHE_LEVEL_AND_TYPE__MAX);
    for (node = caches; node; node = node->next) {
        /* Prohibit users from repeating settings. */
        if (test_bit(node->value->cache, caches_bitmap)) {
            error_setg(errp,
                       "Invalid cache properties: %s. "
                       "The cache properties are duplicated",
                       CacheLevelAndType_str(node->value->cache));
            return false;
        }

        machine_set_cache_topo_level(ms, node->value->cache,
                                     node->value->topology);
        set_bit(node->value->cache, caches_bitmap);
    }

    for (int i = 0; i < CACHE_LEVEL_AND_TYPE__MAX; i++) {
        const SmpCacheProperties *props = &ms->smp_cache.props[i];

        /*
         * Reject non "default" topology level if the cache isn't
         * supported by the machine.
         */
        if (props->topology != CPU_TOPOLOGY_LEVEL_DEFAULT &&
            !mc->smp_props.cache_supported[props->cache]) {
            error_setg(errp,
                       "%s cache topology not supported by this machine",
                       CacheLevelAndType_str(props->cache));
            return false;
        }

        if (props->topology == CPU_TOPOLOGY_LEVEL_THREAD) {
            error_setg(errp,
                       "%s level cache not supported by this machine",
                       CpuTopologyLevel_str(props->topology));
            return false;
        }

        if (!machine_check_topo_support(ms, props->topology, errp)) {
            return false;
        }
    }

    mc->smp_props.has_caches = true;
    return true;
}

unsigned int machine_topo_get_cores_per_socket(const MachineState *ms)
{
    return ms->smp.cores * ms->smp.modules * ms->smp.clusters * ms->smp.dies;
}

unsigned int machine_topo_get_threads_per_socket(const MachineState *ms)
{
    return ms->smp.threads * machine_topo_get_cores_per_socket(ms);
}

CpuTopologyLevel machine_get_cache_topo_level(const MachineState *ms,
                                              CacheLevelAndType cache)
{
    return ms->smp_cache.props[cache].topology;
}

void machine_set_cache_topo_level(MachineState *ms, CacheLevelAndType cache,
                                  CpuTopologyLevel level)
{
    ms->smp_cache.props[cache].topology = level;
}

/*
 * When both cache1 and cache2 are configured with specific topology levels
 * (not default level), is cache1's topology level higher than cache2?
 */
static bool smp_cache_topo_cmp(const SmpCache *smp_cache,
                               CacheLevelAndType cache1,
                               CacheLevelAndType cache2)
{
    /*
     * Before comparing, the "default" topology level should be replaced
     * with the specific level.
     */
    assert(smp_cache->props[cache1].topology != CPU_TOPOLOGY_LEVEL_DEFAULT);

    return smp_cache->props[cache1].topology > smp_cache->props[cache2].topology;
}

/*
 * Currently, we have no way to expose the arch-specific default cache model
 * because the cache model is sometimes related to the CPU model (e.g., i386).
 *
 * We can only check the correctness of the cache topology after the arch loads
 * the user-configured cache model from MachineState and consumes the special
 * "default" level by replacing it with the specific level.
 */
bool machine_check_smp_cache(const MachineState *ms, Error **errp)
{
    if (smp_cache_topo_cmp(&ms->smp_cache, CACHE_LEVEL_AND_TYPE_L1D,
                           CACHE_LEVEL_AND_TYPE_L2) ||
        smp_cache_topo_cmp(&ms->smp_cache, CACHE_LEVEL_AND_TYPE_L1I,
                           CACHE_LEVEL_AND_TYPE_L2)) {
        error_setg(errp,
                   "Invalid smp cache topology. "
                   "L2 cache topology level shouldn't be lower than L1 cache");
        return false;
    }

    if (smp_cache_topo_cmp(&ms->smp_cache, CACHE_LEVEL_AND_TYPE_L2,
                           CACHE_LEVEL_AND_TYPE_L3)) {
        error_setg(errp,
                   "Invalid smp cache topology. "
                   "L3 cache topology level shouldn't be lower than L2 cache");
        return false;
    }

    return true;
}
