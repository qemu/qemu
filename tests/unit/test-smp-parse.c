/*
 * SMP parsing unit-tests
 *
 * Copyright (c) 2021 Huawei Technologies Co., Ltd
 *
 * Authors:
 *  Yanan Wang <wangyanan55@huawei.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qom/object.h"
#include "qemu/module.h"
#include "qapi/error.h"

#include "hw/boards.h"

#define T true
#define F false

#define MIN_CPUS 1    /* set the min CPUs supported by the machine as 1 */
#define MAX_CPUS 4096 /* set the max CPUs supported by the machine as 4096 */

#define SMP_MACHINE_NAME "TEST-SMP"

/*
 * Used to define the generic 3-level CPU topology hierarchy
 *  -sockets/cores/threads
 */
#define SMP_CONFIG_GENERIC(ha, a, hb, b, hc, c, hd, d, he, e) \
        {                                                     \
            .has_cpus    = ha, .cpus    = a,                  \
            .has_sockets = hb, .sockets = b,                  \
            .has_cores   = hc, .cores   = c,                  \
            .has_threads = hd, .threads = d,                  \
            .has_maxcpus = he, .maxcpus = e,                  \
        }

#define CPU_TOPOLOGY_GENERIC(a, b, c, d, e)                   \
        {                                                     \
            .cpus     = a,                                    \
            .sockets  = b,                                    \
            .cores    = c,                                    \
            .threads  = d,                                    \
            .max_cpus = e,                                    \
        }

/*
 * Currently a 5-level topology hierarchy is supported on PC machines
 *  -sockets/dies/modules/cores/threads
 */
#define SMP_CONFIG_WITH_MODS_DIES(ha, a, hb, b, hc, c, hd, d, \
                                  he, e, hf, f, hg, g)        \
        {                                                     \
            .has_cpus    = ha, .cpus    = a,                  \
            .has_sockets = hb, .sockets = b,                  \
            .has_dies    = hc, .dies    = c,                  \
            .has_modules = hd, .modules = d,                  \
            .has_cores   = he, .cores   = e,                  \
            .has_threads = hf, .threads = f,                  \
            .has_maxcpus = hg, .maxcpus = g,                  \
        }

/*
 * Currently a 4-level topology hierarchy is supported on ARM virt machines
 *  -sockets/clusters/cores/threads
 */
#define SMP_CONFIG_WITH_CLUSTERS(ha, a, hb, b, hc, c, hd, d, he, e, hf, f) \
        {                                                     \
            .has_cpus     = ha, .cpus     = a,                \
            .has_sockets  = hb, .sockets  = b,                \
            .has_clusters = hc, .clusters = c,                \
            .has_cores    = hd, .cores    = d,                \
            .has_threads  = he, .threads  = e,                \
            .has_maxcpus  = hf, .maxcpus  = f,                \
        }

/*
 * Currently a 5-level topology hierarchy is supported on s390 ccw machines
 *  -drawers/books/sockets/cores/threads
 */
#define SMP_CONFIG_WITH_BOOKS_DRAWERS(ha, a, hb, b, hc, c, hd, \
                                      d, he, e, hf, f, hg, g)  \
        {                                                      \
            .has_cpus     = ha, .cpus     = a,                 \
            .has_drawers  = hb, .drawers  = b,                 \
            .has_books    = hc, .books    = c,                 \
            .has_sockets  = hd, .sockets  = d,                 \
            .has_cores    = he, .cores    = e,                 \
            .has_threads  = hf, .threads  = f,                 \
            .has_maxcpus  = hg, .maxcpus  = g,                 \
        }

/*
 * Currently QEMU supports up to a 8-level topology hierarchy, which is the
 * QEMU's unified abstract representation of CPU topology.
 *  -drawers/books/sockets/dies/clusters/modules/cores/threads
 */
#define SMP_CONFIG_WITH_FULL_TOPO(a, b, c, d, e, f, g, h, i, j) \
        {                                                       \
            .has_cpus     = true, .cpus     = a,                \
            .has_drawers  = true, .drawers  = b,                \
            .has_books    = true, .books    = c,                \
            .has_sockets  = true, .sockets  = d,                \
            .has_dies     = true, .dies     = e,                \
            .has_clusters = true, .clusters = f,                \
            .has_modules  = true, .modules  = g,                \
            .has_cores    = true, .cores    = h,                \
            .has_threads  = true, .threads  = i,                \
            .has_maxcpus  = true, .maxcpus  = j,                \
        }

/**
 * @config - the given SMP configuration
 * @expect_prefer_sockets - the expected parsing result for the
 * valid configuration, when sockets are preferred over cores
 * @expect_prefer_cores - the expected parsing result for the
 * valid configuration, when cores are preferred over sockets
 * @expect_error - the expected error report when the given
 * configuration is invalid
 */
typedef struct SMPTestData {
    SMPConfiguration config;
    CpuTopology expect_prefer_sockets;
    CpuTopology expect_prefer_cores;
    const char *expect_error;
} SMPTestData;

/*
 * List all the possible valid sub-collections of the generic 5
 * topology parameters (i.e. cpus/maxcpus/sockets/cores/threads),
 * then test the automatic calculation algorithm of the missing
 * values in the parser.
 */
static const struct SMPTestData data_generic_valid[] = {
    {
        /* config: no configuration provided
         * expect: cpus=1,sockets=1,cores=1,threads=1,maxcpus=1 */
        .config = SMP_CONFIG_GENERIC(F, 0, F, 0, F, 0, F, 0, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(1, 1, 1, 1, 1),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(1, 1, 1, 1, 1),
    }, {
        /* config: -smp 8
         * prefer_sockets: cpus=8,sockets=8,cores=1,threads=1,maxcpus=8
         * prefer_cores: cpus=8,sockets=1,cores=8,threads=1,maxcpus=8 */
        .config = SMP_CONFIG_GENERIC(T, 8, F, 0, F, 0, F, 0, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 8, 1, 1, 8),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 1, 8, 1, 8),
    }, {
        /* config: -smp sockets=2
         * expect: cpus=2,sockets=2,cores=1,threads=1,maxcpus=2 */
        .config = SMP_CONFIG_GENERIC(F, 0, T, 2, F, 0, F, 0, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(2, 2, 1, 1, 2),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(2, 2, 1, 1, 2),
    }, {
        /* config: -smp cores=4
         * expect: cpus=4,sockets=1,cores=4,threads=1,maxcpus=4 */
        .config = SMP_CONFIG_GENERIC(F, 0, F, 0, T, 4, F, 0, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(4, 1, 4, 1, 4),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(4, 1, 4, 1, 4),
    }, {
        /* config: -smp threads=2
         * expect: cpus=2,sockets=1,cores=1,threads=2,maxcpus=2 */
        .config = SMP_CONFIG_GENERIC(F, 0, F, 0, F, 0, T, 2, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(2, 1, 1, 2, 2),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(2, 1, 1, 2, 2),
    }, {
        /* config: -smp maxcpus=16
         * prefer_sockets: cpus=16,sockets=16,cores=1,threads=1,maxcpus=16
         * prefer_cores: cpus=16,sockets=1,cores=16,threads=1,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(F, 0, F, 0, F, 0, F, 0, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(16, 16, 1, 1, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(16, 1, 16, 1, 16),
    }, {
        /* config: -smp 8,sockets=2
         * expect: cpus=8,sockets=2,cores=4,threads=1,maxcpus=8 */
        .config = SMP_CONFIG_GENERIC(T, 8, T, 2, F, 0, F, 0, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 2, 4, 1, 8),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 2, 4, 1, 8),
    }, {
        /* config: -smp 8,cores=4
         * expect: cpus=8,sockets=2,cores=4,threads=1,maxcpus=8 */
        .config = SMP_CONFIG_GENERIC(T, 8, F, 0, T, 4, F, 0, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 2, 4, 1, 8),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 2, 4, 1, 8),
    }, {
        /* config: -smp 8,threads=2
         * prefer_sockets: cpus=8,sockets=4,cores=1,threads=2,maxcpus=8
         * prefer_cores: cpus=8,sockets=1,cores=4,threads=2,maxcpus=8 */
        .config = SMP_CONFIG_GENERIC(T, 8, F, 0, F, 0, T, 2, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 4, 1, 2, 8),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 1, 4, 2, 8),
    }, {
        /* config: -smp 8,maxcpus=16
         * prefer_sockets: cpus=8,sockets=16,cores=1,threads=1,maxcpus=16
         * prefer_cores: cpus=8,sockets=1,cores=16,threads=1,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(T, 8, F, 0, F, 0, F, 0, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 16, 1, 1, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 1, 16, 1, 16),
    }, {
        /* config: -smp sockets=2,cores=4
         * expect: cpus=8,sockets=2,cores=4,threads=1,maxcpus=8 */
        .config = SMP_CONFIG_GENERIC(F, 0, T, 2, T, 4, F, 0, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 2, 4, 1, 8),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 2, 4, 1, 8),
    }, {
        /* config: -smp sockets=2,threads=2
         * expect: cpus=4,sockets=2,cores=1,threads=2,maxcpus=4 */
        .config = SMP_CONFIG_GENERIC(F, 0, T, 2, F, 0, T, 2, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(4, 2, 1, 2, 4),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(4, 2, 1, 2, 4),
    }, {
        /* config: -smp sockets=2,maxcpus=16
         * expect: cpus=16,sockets=2,cores=8,threads=1,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(F, 0, T, 2, F, 0, F, 0, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(16, 2, 8, 1, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(16, 2, 8, 1, 16),
    }, {
        /* config: -smp cores=4,threads=2
         * expect: cpus=8,sockets=1,cores=4,threads=2,maxcpus=8 */
        .config = SMP_CONFIG_GENERIC(F, 0, F, 0, T, 4, T, 2, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 1, 4, 2, 8),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 1, 4, 2, 8),
    }, {
        /* config: -smp cores=4,maxcpus=16
         * expect: cpus=16,sockets=4,cores=4,threads=1,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(F, 0, F, 0, T, 4, F, 0, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(16, 4, 4, 1, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(16, 4, 4, 1, 16),
    }, {
        /* config: -smp threads=2,maxcpus=16
         * prefer_sockets: cpus=16,sockets=8,cores=1,threads=2,maxcpus=16
         * prefer_cores: cpus=16,sockets=1,cores=8,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(F, 0, F, 0, F, 0, T, 2, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(16, 8, 1, 2, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(16, 1, 8, 2, 16),
    }, {
        /* config: -smp 8,sockets=2,cores=4
         * expect: cpus=8,sockets=2,cores=4,threads=1,maxcpus=8 */
        .config = SMP_CONFIG_GENERIC(T, 8, T, 2, T, 4, F, 0, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 2, 4, 1, 8),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 2, 4, 1, 8),
    }, {
        /* config: -smp 8,sockets=2,threads=2
         * expect: cpus=8,sockets=2,cores=2,threads=2,maxcpus=8 */
        .config = SMP_CONFIG_GENERIC(T, 8, T, 2, F, 0, T, 2, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 2, 2, 2, 8),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 2, 2, 2, 8),
    }, {
        /* config: -smp 8,sockets=2,maxcpus=16
         * expect: cpus=8,sockets=2,cores=8,threads=1,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(T, 8, T, 2, F, 0, F, 0, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 2, 8, 1, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 2, 8, 1, 16),
    }, {
        /* config: -smp 8,cores=4,threads=2
         * expect: cpus=8,sockets=1,cores=4,threads=2,maxcpus=8 */
        .config = SMP_CONFIG_GENERIC(T, 8, F, 0, T, 4, T, 2, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 1, 4, 2, 8),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 1, 4, 2, 8),
    }, {
        /* config: -smp 8,cores=4,maxcpus=16
         * expect: cpus=8,sockets=4,cores=4,threads=1,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(T, 8, F, 0, T, 4, F, 0, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 4, 4, 1, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 4, 4, 1, 16),
    }, {
        /* config: -smp 8,threads=2,maxcpus=16
         * prefer_sockets: cpus=8,sockets=8,cores=1,threads=2,maxcpus=16
         * prefer_cores: cpus=8,sockets=1,cores=8,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(T, 8, F, 0, F, 0, T, 2, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 8, 1, 2, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 1, 8, 2, 16),
    }, {
        /* config: -smp sockets=2,cores=4,threads=2
         * expect: cpus=16,sockets=2,cores=4,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(F, 0, T, 2, T, 4, T, 2, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(16, 2, 4, 2, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(16, 2, 4, 2, 16),
    }, {
        /* config: -smp sockets=2,cores=4,maxcpus=16
         * expect: cpus=16,sockets=2,cores=4,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(F, 0, T, 2, T, 4, F, 0, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(16, 2, 4, 2, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(16, 2, 4, 2, 16),
    }, {
        /* config: -smp sockets=2,threads=2,maxcpus=16
         * expect: cpus=16,sockets=2,cores=4,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(F, 0, T, 2, F, 0, T, 2, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(16, 2, 4, 2, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(16, 2, 4, 2, 16),
    }, {
        /* config: -smp cores=4,threads=2,maxcpus=16
         * expect: cpus=16,sockets=2,cores=4,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(F, 0, F, 0, T, 4, T, 2, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(16, 2, 4, 2, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(16, 2, 4, 2, 16),
    }, {
        /* config: -smp 8,sockets=2,cores=4,threads=1
         * expect: cpus=8,sockets=2,cores=4,threads=1,maxcpus=8 */
        .config = SMP_CONFIG_GENERIC(T, 8, T, 2, T, 4, T, 1, F, 0),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 2, 4, 1, 8),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 2, 4, 1, 8),
    }, {
        /* config: -smp 8,sockets=2,cores=4,maxcpus=16
         * expect: cpus=8,sockets=2,cores=4,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(T, 8, T, 2, T, 4, F, 0, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 2, 4, 2, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 2, 4, 2, 16),
    }, {
        /* config: -smp 8,sockets=2,threads=2,maxcpus=16
         * expect: cpus=8,sockets=2,cores=4,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(T, 8, T, 2, F, 0, T, 2, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 2, 4, 2, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 2, 4, 2, 16),
    }, {
        /* config: -smp 8,cores=4,threads=2,maxcpus=16
         * expect: cpus=8,sockets=2,cores=4,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(T, 8, F, 0, T, 4, T, 2, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 2, 4, 2, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 2, 4, 2, 16),
    }, {
        /* config: -smp sockets=2,cores=4,threads=2,maxcpus=16
         * expect: cpus=16,sockets=2,cores=4,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(F, 0, T, 2, T, 4, T, 2, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(16, 2, 4, 2, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(16, 2, 4, 2, 16),
    }, {
        /* config: -smp 8,sockets=2,cores=4,threads=2,maxcpus=16
         * expect: cpus=8,sockets=2,cores=4,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(T, 8, T, 2, T, 4, T, 2, T, 16),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 2, 4, 2, 16),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 2, 4, 2, 16),
    }, {
        /*
         * Unsupported parameters are always allowed to be set to '1'
         * config:
         *   -smp 8,drawers=1,books=1,sockets=2,dies=1,clusters=1,modules=1,\
         *        cores=2,threads=2,maxcpus=8
         * expect: cpus=8,sockets=2,cores=2,threads=2,maxcpus=8 */
        .config = SMP_CONFIG_WITH_FULL_TOPO(8, 1, 1, 2, 1, 1, 1, 2, 2, 8),
        .expect_prefer_sockets = CPU_TOPOLOGY_GENERIC(8, 2, 2, 2, 8),
        .expect_prefer_cores   = CPU_TOPOLOGY_GENERIC(8, 2, 2, 2, 8),
    },
};

static const struct SMPTestData data_generic_invalid[] = {
    {
        /* config: -smp 2,modules=2 */
        .config = SMP_CONFIG_WITH_MODS_DIES(T, 2, F, 0, F, 0, T, 2,
                                            F, 0, F, 0, F, 0),
        .expect_error = "modules > 1 not supported by this machine's CPU topology",
    }, {
        /* config: -smp 2,dies=2 */
        .config = SMP_CONFIG_WITH_MODS_DIES(T, 2, F, 0, T, 2, F, 0,
                                            F, 0, F, 0, F, 0),
        .expect_error = "dies > 1 not supported by this machine's CPU topology",
    }, {
        /* config: -smp 2,clusters=2 */
        .config = SMP_CONFIG_WITH_CLUSTERS(T, 2, F, 0, T, 2, F, 0, F, 0, F, 0),
        .expect_error = "clusters > 1 not supported by this machine's CPU topology",
    }, {
        /* config: -smp 2,books=2 */
        .config = SMP_CONFIG_WITH_BOOKS_DRAWERS(T, 2, F, 0, T, 2, F,
                                                0, F, 0, F, 0, F, 0),
        .expect_error = "books > 1 not supported by this machine's CPU topology",
    }, {
        /* config: -smp 2,drawers=2 */
        .config = SMP_CONFIG_WITH_BOOKS_DRAWERS(T, 2, T, 2, F, 0, F,
                                                0, F, 0, F, 0, F, 0),
        .expect_error = "drawers > 1 not supported by this machine's CPU topology",
    }, {
        /* config: -smp 8,sockets=2,cores=4,threads=2,maxcpus=8 */
        .config = SMP_CONFIG_GENERIC(T, 8, T, 2, T, 4, T, 2, T, 8),
        .expect_error = "Invalid CPU topology: "
                        "product of the hierarchy must match maxcpus: "
                        "sockets (2) * cores (4) * threads (2) "
                        "!= maxcpus (8)",
    }, {
        /* config: -smp 18,sockets=2,cores=4,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_GENERIC(T, 18, T, 2, T, 4, T, 2, T, 16),
        .expect_error = "Invalid CPU topology: "
                        "maxcpus must be equal to or greater than smp: "
                        "sockets (2) * cores (4) * threads (2) "
                        "== maxcpus (16) < smp_cpus (18)",
    }, {
        /*
         * config: -smp 1
         * The test machine should tweak the supported min CPUs to
         * 2 (MIN_CPUS + 1) for testing.
         */
        .config = SMP_CONFIG_GENERIC(T, MIN_CPUS, F, 0, F, 0, F, 0, F, 0),
        .expect_error = "Invalid SMP CPUs 1. The min CPUs supported "
                        "by machine '" SMP_MACHINE_NAME "' is 2",
    }, {
        /*
         * config: -smp 4096
         * The test machine should tweak the supported max CPUs to
         * 4095 (MAX_CPUS - 1) for testing.
         */
        .config = SMP_CONFIG_GENERIC(T, 4096, F, 0, F, 0, F, 0, F, 0),
        .expect_error = "Invalid SMP CPUs 4096. The max CPUs supported "
                        "by machine '" SMP_MACHINE_NAME "' is 4095",
    },
};

static const struct SMPTestData data_with_modules_invalid[] = {
    {
        /* config: -smp 16,sockets=2,modules=2,cores=4,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_WITH_MODS_DIES(T, 16, T, 2, F, 0, T, 2,
                                            T, 4, T, 2, T, 16),
        .expect_error = "Invalid CPU topology: "
                        "product of the hierarchy must match maxcpus: "
                        "sockets (2) * modules (2) * cores (4) * threads (2) "
                        "!= maxcpus (16)",
    }, {
        /* config: -smp 34,sockets=2,modules=2,cores=4,threads=2,maxcpus=32 */
        .config = SMP_CONFIG_WITH_MODS_DIES(T, 34, T, 2, F, 0, T, 2,
                                            T, 4, T, 2, T, 32),
        .expect_error = "Invalid CPU topology: "
                        "maxcpus must be equal to or greater than smp: "
                        "sockets (2) * modules (2) * cores (4) * threads (2) "
                        "== maxcpus (32) < smp_cpus (34)",
    },
};

static const struct SMPTestData data_with_dies_invalid[] = {
    {
        /* config: -smp 16,sockets=2,dies=2,cores=4,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_WITH_MODS_DIES(T, 16, T, 2, T, 2, F, 0,
                                            T, 4, T, 2, T, 16),
        .expect_error = "Invalid CPU topology: "
                        "product of the hierarchy must match maxcpus: "
                        "sockets (2) * dies (2) * cores (4) * threads (2) "
                        "!= maxcpus (16)",
    }, {
        /* config: -smp 34,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = SMP_CONFIG_WITH_MODS_DIES(T, 34, T, 2, T, 2, F, 0,
                                            T, 4, T, 2, T, 32),
        .expect_error = "Invalid CPU topology: "
                        "maxcpus must be equal to or greater than smp: "
                        "sockets (2) * dies (2) * cores (4) * threads (2) "
                        "== maxcpus (32) < smp_cpus (34)",
    },
};

static const struct SMPTestData data_with_modules_dies_invalid[] = {
    {
        /*
         * config: -smp 200,sockets=3,dies=5,modules=2,cores=4,\
         * threads=2,maxcpus=200
         */
        .config = SMP_CONFIG_WITH_MODS_DIES(T, 200, T, 3, T, 5, T,
                                            2, T, 4, T, 2, T, 200),
        .expect_error = "Invalid CPU topology: "
                        "product of the hierarchy must match maxcpus: "
                        "sockets (3) * dies (5) * modules (2) * "
                        "cores (4) * threads (2) != maxcpus (200)",
    }, {
        /*
         * config: -smp 242,sockets=3,dies=5,modules=2,cores=4,\
         * threads=2,maxcpus=240
         */
        .config = SMP_CONFIG_WITH_MODS_DIES(T, 242, T, 3, T, 5, T,
                                            2, T, 4, T, 2, T, 240),
        .expect_error = "Invalid CPU topology: "
                        "maxcpus must be equal to or greater than smp: "
                        "sockets (3) * dies (5) * modules (2) * "
                        "cores (4) * threads (2) "
                        "== maxcpus (240) < smp_cpus (242)",
    },
};

static const struct SMPTestData data_with_clusters_invalid[] = {
    {
        /* config: -smp 16,sockets=2,clusters=2,cores=4,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_WITH_CLUSTERS(T, 16, T, 2, T, 2, T, 4, T, 2, T, 16),
        .expect_error = "Invalid CPU topology: "
                        "product of the hierarchy must match maxcpus: "
                        "sockets (2) * clusters (2) * cores (4) * threads (2) "
                        "!= maxcpus (16)",
    }, {
        /* config: -smp 34,sockets=2,clusters=2,cores=4,threads=2,maxcpus=32 */
        .config = SMP_CONFIG_WITH_CLUSTERS(T, 34, T, 2, T, 2, T, 4, T, 2, T, 32),
        .expect_error = "Invalid CPU topology: "
                        "maxcpus must be equal to or greater than smp: "
                        "sockets (2) * clusters (2) * cores (4) * threads (2) "
                        "== maxcpus (32) < smp_cpus (34)",
    },
};

static const struct SMPTestData data_with_books_invalid[] = {
    {
        /* config: -smp 16,books=2,sockets=2,cores=4,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_WITH_BOOKS_DRAWERS(T, 16, F, 0, T, 2, T,
                                                2, T, 4, T, 2, T, 16),
        .expect_error = "Invalid CPU topology: "
                        "product of the hierarchy must match maxcpus: "
                        "books (2) * sockets (2) * cores (4) * threads (2) "
                        "!= maxcpus (16)",
    }, {
        /* config: -smp 34,books=2,sockets=2,cores=4,threads=2,maxcpus=32 */
        .config = SMP_CONFIG_WITH_BOOKS_DRAWERS(T, 34, F, 0, T, 2, T,
                                                2, T, 4, T, 2, T, 32),
        .expect_error = "Invalid CPU topology: "
                        "maxcpus must be equal to or greater than smp: "
                        "books (2) * sockets (2) * cores (4) * threads (2) "
                        "== maxcpus (32) < smp_cpus (34)",
    },
};

static const struct SMPTestData data_with_drawers_invalid[] = {
    {
        /* config: -smp 16,drawers=2,sockets=2,cores=4,threads=2,maxcpus=16 */
        .config = SMP_CONFIG_WITH_BOOKS_DRAWERS(T, 16, T, 2, F, 0, T,
                                                2, T, 4, T, 2, T, 16),
        .expect_error = "Invalid CPU topology: "
                        "product of the hierarchy must match maxcpus: "
                        "drawers (2) * sockets (2) * cores (4) * threads (2) "
                        "!= maxcpus (16)",
    }, {
        /* config: -smp 34,drawers=2,sockets=2,cores=4,threads=2,maxcpus=32 */
        .config = SMP_CONFIG_WITH_BOOKS_DRAWERS(T, 34, T, 2, F, 0, T,
                                                2, T, 4, T, 2, T, 32),
        .expect_error = "Invalid CPU topology: "
                        "maxcpus must be equal to or greater than smp: "
                        "drawers (2) * sockets (2) * cores (4) * threads (2) "
                        "== maxcpus (32) < smp_cpus (34)",
    },
};

static const struct SMPTestData data_with_drawers_books_invalid[] = {
    {
        /*
         * config: -smp 200,drawers=3,books=5,sockets=2,cores=4,\
         *              threads=2,maxcpus=200
         */
        .config = SMP_CONFIG_WITH_BOOKS_DRAWERS(T, 200, T, 3, T, 5, T,
                                                2, T, 4, T, 2, T, 200),
        .expect_error = "Invalid CPU topology: "
                        "product of the hierarchy must match maxcpus: "
                        "drawers (3) * books (5) * sockets (2) * "
                        "cores (4) * threads (2) != maxcpus (200)",
    }, {
        /*
         * config: -smp 242,drawers=3,books=5,sockets=2,cores=4,\
         *              threads=2,maxcpus=240
         */
        .config = SMP_CONFIG_WITH_BOOKS_DRAWERS(T, 242, T, 3, T, 5, T,
                                                2, T, 4, T, 2, T, 240),
        .expect_error = "Invalid CPU topology: "
                        "maxcpus must be equal to or greater than smp: "
                        "drawers (3) * books (5) * sockets (2) * "
                        "cores (4) * threads (2) "
                        "== maxcpus (240) < smp_cpus (242)",
    },
};

static const struct SMPTestData data_full_topo_invalid[] = {
    {
        /*
         * config: -smp 200,drawers=3,books=5,sockets=2,dies=4,\
         *              clusters=2,modules=3,cores=7,threads=2,\
         *              maxcpus=200
         */
        .config = SMP_CONFIG_WITH_FULL_TOPO(200, 3, 5, 2, 4, 2, 3, 7, 2, 200),
        .expect_error = "Invalid CPU topology: "
                        "product of the hierarchy must match maxcpus: "
                        "drawers (3) * books (5) * sockets (2) * dies (4) * "
                        "clusters (2) * modules (3) * cores (7) * threads (2) "
                        "!= maxcpus (200)",
    }, {
        /*
         * config: -smp 2881,drawers=3,books=5,sockets=2,dies=4,\
         *              clusters=2,modules=3,cores=2,threads=2,
         *              maxcpus=2880
         */
        .config = SMP_CONFIG_WITH_FULL_TOPO(2881, 3, 5, 2, 4,
                                            2, 3, 2, 2, 2880),
        .expect_error = "Invalid CPU topology: "
                        "maxcpus must be equal to or greater than smp: "
                        "drawers (3) * books (5) * sockets (2) * "
                        "dies (4) * clusters (2) * modules (3) * "
                        "cores (2) * threads (2) == maxcpus (2880) "
                        "< smp_cpus (2881)",
    }, {
        /*
         * config: -smp 1,drawers=3,books=5,sockets=2,dies=4,\
         *              clusters=2,modules=3,cores=3,threads=3,\
         *              maxcpus=6480
         */
        .config = SMP_CONFIG_WITH_FULL_TOPO(1, 3, 5, 2, 4, 2, 3, 3, 3, 6480),
        .expect_error = "Invalid SMP CPUs 6480. The max CPUs supported "
                        "by machine '" SMP_MACHINE_NAME "' is 4096",
    },
};

static const struct SMPTestData data_zero_topo_invalid[] = {
    {
        /*
         * Test "cpus=0".
         * config: -smp 0,drawers=1,books=1,sockets=1,dies=1,\
         *              clusters=1,modules=1,cores=1,threads=1,\
         *              maxcpus=1
         */
        .config = SMP_CONFIG_WITH_FULL_TOPO(0, 1, 1, 1, 1, 1, 1, 1, 1, 1),
        .expect_error = "Invalid CPU topology: CPU topology parameters must "
                        "be greater than zero",
    }, {
        /*
         * Test "drawers=0".
         * config: -smp 1,drawers=0,books=1,sockets=1,dies=1,\
         *              clusters=1,modules=1,cores=1,threads=1,\
         *              maxcpus=1
         */
        .config = SMP_CONFIG_WITH_FULL_TOPO(1, 0, 1, 1, 1, 1, 1, 1, 1, 1),
        .expect_error = "Invalid CPU topology: CPU topology parameters must "
                        "be greater than zero",
    }, {
        /*
         * Test "books=0".
         * config: -smp 1,drawers=1,books=0,sockets=1,dies=1,\
         *              clusters=1,modules=1,cores=1,threads=1,\
         *              maxcpus=1
         */
        .config = SMP_CONFIG_WITH_FULL_TOPO(1, 1, 0, 1, 1, 1, 1, 1, 1, 1),
        .expect_error = "Invalid CPU topology: CPU topology parameters must "
                        "be greater than zero",
    }, {
        /*
         * Test "sockets=0".
         * config: -smp 1,drawers=1,books=1,sockets=0,dies=1,\
         *              clusters=1,modules=1,cores=1,threads=1,
         *              maxcpus=1
         */
        .config = SMP_CONFIG_WITH_FULL_TOPO(1, 1, 1, 0, 1, 1, 1, 1, 1, 1),
        .expect_error = "Invalid CPU topology: CPU topology parameters must "
                        "be greater than zero",
    }, {
        /*
         * Test "dies=0".
         * config: -smp 1,drawers=1,books=1,sockets=1,dies=0,\
         *              clusters=1,modules=1,cores=1,threads=1,\
         *              maxcpus=1
         */
        .config = SMP_CONFIG_WITH_FULL_TOPO(1, 1, 1, 1, 0, 1, 1, 1, 1, 1),
        .expect_error = "Invalid CPU topology: CPU topology parameters must "
                        "be greater than zero",
    }, {
        /*
         * Test "clusters=0".
         * config: -smp 1,drawers=1,books=1,sockets=1,dies=1,\
         *              clusters=0,modules=1,cores=1,threads=1,\
         *              maxcpus=1
         */
        .config = SMP_CONFIG_WITH_FULL_TOPO(1, 1, 1, 1, 1, 0, 1, 1, 1, 1),
        .expect_error = "Invalid CPU topology: CPU topology parameters must "
                        "be greater than zero",
    }, {
        /*
         * Test "modules=0".
         * config: -smp 1,drawers=1,books=1,sockets=1,dies=1,\
         *              clusters=1,modules=0,cores=1,threads=1,\
         *              maxcpus=1
         */
        .config = SMP_CONFIG_WITH_FULL_TOPO(1, 1, 1, 1, 1, 1, 0, 1, 1, 1),
        .expect_error = "Invalid CPU topology: CPU topology parameters must "
                        "be greater than zero",
    }, {
        /*
         * Test "cores=0".
         * config: -smp 1,drawers=1,books=1,sockets=1,dies=1,\
         *              clusters=1,modules=1,cores=0,threads=1,
         *              maxcpus=1
         */
        .config = SMP_CONFIG_WITH_FULL_TOPO(1, 1, 1, 1, 1, 1, 1, 0, 1, 1),
        .expect_error = "Invalid CPU topology: CPU topology parameters must "
                        "be greater than zero",
    }, {
        /*
         * Test "threads=0".
         * config: -smp 1,drawers=1,books=1,sockets=1,dies=1,\
         *              clusters=1,modules=1,cores=1,threads=0,\
         *              maxcpus=1
         */
        .config = SMP_CONFIG_WITH_FULL_TOPO(1, 1, 1, 1, 1, 1, 1, 1, 0, 1),
        .expect_error = "Invalid CPU topology: CPU topology parameters must "
                        "be greater than zero",
    }, {
        /*
         * Test "maxcpus=0".
         * config: -smp 1,drawers=1,books=1,sockets=1,dies=1,\
         *              clusters=1,modules=1,cores=1,threads=1,\
         *              maxcpus=0
         */
        .config = SMP_CONFIG_WITH_FULL_TOPO(1, 1, 1, 1, 1, 1, 1, 1, 1, 0),
        .expect_error = "Invalid CPU topology: CPU topology parameters must "
                        "be greater than zero",
    },
};

static char *smp_config_to_string(const SMPConfiguration *config)
{
    return g_strdup_printf(
        "(SMPConfiguration) {\n"
        "    .has_cpus     = %5s, cpus     = %" PRId64 ",\n"
        "    .has_drawers  = %5s, drawers  = %" PRId64 ",\n"
        "    .has_books    = %5s, books    = %" PRId64 ",\n"
        "    .has_sockets  = %5s, sockets  = %" PRId64 ",\n"
        "    .has_dies     = %5s, dies     = %" PRId64 ",\n"
        "    .has_clusters = %5s, clusters = %" PRId64 ",\n"
        "    .has_modules  = %5s, modules  = %" PRId64 ",\n"
        "    .has_cores    = %5s, cores    = %" PRId64 ",\n"
        "    .has_threads  = %5s, threads  = %" PRId64 ",\n"
        "    .has_maxcpus  = %5s, maxcpus  = %" PRId64 ",\n"
        "}",
        config->has_cpus ? "true" : "false", config->cpus,
        config->has_drawers ? "true" : "false", config->drawers,
        config->has_books ? "true" : "false", config->books,
        config->has_sockets ? "true" : "false", config->sockets,
        config->has_dies ? "true" : "false", config->dies,
        config->has_clusters ? "true" : "false", config->clusters,
        config->has_modules ? "true" : "false", config->modules,
        config->has_cores ? "true" : "false", config->cores,
        config->has_threads ? "true" : "false", config->threads,
        config->has_maxcpus ? "true" : "false", config->maxcpus);
}

/* Use the different calculation than machine_topo_get_threads_per_socket(). */
static unsigned int cpu_topology_get_threads_per_socket(const CpuTopology *topo)
{
    /* Check the divisor to avoid invalid topology examples causing SIGFPE. */
    if (!topo->drawers || !topo->books || !topo->sockets) {
        return 0;
    } else {
        return topo->max_cpus / topo->drawers / topo->books / topo->sockets;
    }
}

/* Use the different calculation than machine_topo_get_cores_per_socket(). */
static unsigned int cpu_topology_get_cores_per_socket(const CpuTopology *topo)
{
    /* Check the divisor to avoid invalid topology examples causing SIGFPE. */
    if (!topo->threads) {
        return 0;
    } else {
        return cpu_topology_get_threads_per_socket(topo) / topo->threads;
    }
}

static char *cpu_topology_to_string(const CpuTopology *topo,
                                    unsigned int threads_per_socket,
                                    unsigned int cores_per_socket,
                                    bool has_clusters)
{
    return g_strdup_printf(
        "(CpuTopology) {\n"
        "    .cpus               = %u,\n"
        "    .drawers            = %u,\n"
        "    .books              = %u,\n"
        "    .sockets            = %u,\n"
        "    .dies               = %u,\n"
        "    .clusters           = %u,\n"
        "    .modules            = %u,\n"
        "    .cores              = %u,\n"
        "    .threads            = %u,\n"
        "    .max_cpus           = %u,\n"
        "    .threads_per_socket = %u,\n"
        "    .cores_per_socket   = %u,\n"
        "    .has_clusters       = %s,\n"
        "}",
        topo->cpus, topo->drawers, topo->books,
        topo->sockets, topo->dies, topo->clusters,
        topo->modules, topo->cores, topo->threads,
        topo->max_cpus, threads_per_socket, cores_per_socket,
        has_clusters ? "true" : "false");
}

static void check_parse(MachineState *ms, const SMPConfiguration *config,
                        const CpuTopology *expect_topo, const char *expect_err,
                        bool is_valid)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    g_autofree char *config_str = smp_config_to_string(config);
    g_autofree char *expect_topo_str = NULL, *output_topo_str = NULL;
    unsigned int expect_threads_per_socket, expect_cores_per_socket;
    unsigned int ms_threads_per_socket, ms_cores_per_socket;
    Error *err = NULL;

    expect_threads_per_socket =
                        cpu_topology_get_threads_per_socket(expect_topo);
    expect_cores_per_socket =
                        cpu_topology_get_cores_per_socket(expect_topo);
    expect_topo_str = cpu_topology_to_string(expect_topo,
                                             expect_threads_per_socket,
                                             expect_cores_per_socket,
                                             config->has_clusters);

    /* call the generic parser */
    machine_parse_smp_config(ms, config, &err);

    ms_threads_per_socket = machine_topo_get_threads_per_socket(ms);
    ms_cores_per_socket = machine_topo_get_cores_per_socket(ms);
    output_topo_str = cpu_topology_to_string(&ms->smp,
                                             ms_threads_per_socket,
                                             ms_cores_per_socket,
                                             mc->smp_props.has_clusters);

    /* when the configuration is supposed to be valid */
    if (is_valid) {
        if ((err == NULL) &&
            (ms->smp.cpus == expect_topo->cpus) &&
            (ms->smp.drawers == expect_topo->drawers) &&
            (ms->smp.books == expect_topo->books) &&
            (ms->smp.sockets == expect_topo->sockets) &&
            (ms->smp.dies == expect_topo->dies) &&
            (ms->smp.clusters == expect_topo->clusters) &&
            (ms->smp.modules == expect_topo->modules) &&
            (ms->smp.cores == expect_topo->cores) &&
            (ms->smp.threads == expect_topo->threads) &&
            (ms->smp.max_cpus == expect_topo->max_cpus) &&
            (ms_threads_per_socket == expect_threads_per_socket) &&
            (ms_cores_per_socket == expect_cores_per_socket) &&
            (mc->smp_props.has_clusters == config->has_clusters)) {
            return;
        }

        if (err != NULL) {
            g_printerr("Test smp_parse failed!\n"
                       "Input configuration: %s\n"
                       "Should be valid: yes\n"
                       "Expected topology: %s\n\n"
                       "Result is valid: no\n"
                       "Output error report: %s\n",
                       config_str, expect_topo_str, error_get_pretty(err));
            goto end;
        }

        g_printerr("Test smp_parse failed!\n"
                   "Input configuration: %s\n"
                   "Should be valid: yes\n"
                   "Expected topology: %s\n\n"
                   "Result is valid: yes\n"
                   "Output topology: %s\n",
                   config_str, expect_topo_str, output_topo_str);
        goto end;
    }

    /* when the configuration is supposed to be invalid */
    if (err != NULL) {
        if (expect_err == NULL ||
            g_str_equal(expect_err, error_get_pretty(err))) {
            error_free(err);
            return;
        }

        g_printerr("Test smp_parse failed!\n"
                   "Input configuration: %s\n"
                   "Should be valid: no\n"
                   "Expected error report: %s\n\n"
                   "Result is valid: no\n"
                   "Output error report: %s\n",
                   config_str, expect_err, error_get_pretty(err));
        goto end;
    }

    g_printerr("Test smp_parse failed!\n"
               "Input configuration: %s\n"
               "Should be valid: no\n"
               "Expected error report: %s\n\n"
               "Result is valid: yes\n"
               "Output topology: %s\n",
               config_str, expect_err, output_topo_str);

end:
    if (err != NULL) {
        error_free(err);
    }

    abort();
}

static void smp_parse_test(MachineState *ms, SMPTestData *data, bool is_valid)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);

    mc->smp_props.prefer_sockets = true;
    check_parse(ms, &data->config, &data->expect_prefer_sockets,
                data->expect_error, is_valid);

    mc->smp_props.prefer_sockets = false;
    check_parse(ms, &data->config, &data->expect_prefer_cores,
                data->expect_error, is_valid);
}

/* The parsed results of the unsupported parameters should be 1 */
static void unsupported_params_init(const MachineClass *mc, SMPTestData *data)
{
    if (!mc->smp_props.modules_supported) {
        data->expect_prefer_sockets.modules = 1;
        data->expect_prefer_cores.modules = 1;
    }

    if (!mc->smp_props.dies_supported) {
        data->expect_prefer_sockets.dies = 1;
        data->expect_prefer_cores.dies = 1;
    }

    if (!mc->smp_props.clusters_supported) {
        data->expect_prefer_sockets.clusters = 1;
        data->expect_prefer_cores.clusters = 1;
    }

    if (!mc->smp_props.books_supported) {
        data->expect_prefer_sockets.books = 1;
        data->expect_prefer_cores.books = 1;
    }

    if (!mc->smp_props.drawers_supported) {
        data->expect_prefer_sockets.drawers = 1;
        data->expect_prefer_cores.drawers = 1;
    }
}

static void machine_base_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->min_cpus = MIN_CPUS;
    mc->max_cpus = MAX_CPUS;

    mc->name = g_strdup(SMP_MACHINE_NAME);
}

static void machine_generic_invalid_class_init(ObjectClass *oc,
                                               const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    /* Force invalid min CPUs and max CPUs */
    mc->min_cpus = MIN_CPUS + 1;
    mc->max_cpus = MAX_CPUS - 1;
}

static void machine_with_modules_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->smp_props.modules_supported = true;
}

static void machine_with_dies_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->smp_props.dies_supported = true;
}

static void machine_with_modules_dies_class_init(ObjectClass *oc,
                                                 const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->smp_props.modules_supported = true;
    mc->smp_props.dies_supported = true;
}

static void machine_with_clusters_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->smp_props.clusters_supported = true;
}

static void machine_with_books_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->smp_props.books_supported = true;
}

static void machine_with_drawers_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->smp_props.drawers_supported = true;
}

static void machine_with_drawers_books_class_init(ObjectClass *oc,
                                                  const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->smp_props.drawers_supported = true;
    mc->smp_props.books_supported = true;
}

static void machine_full_topo_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->smp_props.drawers_supported = true;
    mc->smp_props.books_supported = true;
    mc->smp_props.dies_supported = true;
    mc->smp_props.clusters_supported = true;
    mc->smp_props.modules_supported = true;
}

static void test_generic_valid(const void *opaque)
{
    const char *machine_type = opaque;
    Object *obj = object_new(machine_type);
    MachineState *ms = MACHINE(obj);
    MachineClass *mc = MACHINE_GET_CLASS(obj);
    SMPTestData data = {};
    int i;

    for (i = 0; i < ARRAY_SIZE(data_generic_valid); i++) {
        data = data_generic_valid[i];
        unsupported_params_init(mc, &data);

        smp_parse_test(ms, &data, true);
    }

    object_unref(obj);
}

static void test_generic_invalid(const void *opaque)
{
    const char *machine_type = opaque;
    Object *obj = object_new(machine_type);
    MachineState *ms = MACHINE(obj);
    MachineClass *mc = MACHINE_GET_CLASS(obj);
    SMPTestData data = {};
    int i;

    for (i = 0; i < ARRAY_SIZE(data_generic_invalid); i++) {
        data = data_generic_invalid[i];
        unsupported_params_init(mc, &data);

        smp_parse_test(ms, &data, false);
    }

    object_unref(obj);
}

static void test_with_modules(const void *opaque)
{
    const char *machine_type = opaque;
    Object *obj = object_new(machine_type);
    MachineState *ms = MACHINE(obj);
    MachineClass *mc = MACHINE_GET_CLASS(obj);
    SMPTestData data = {};
    unsigned int num_modules = 2;
    int i;

    for (i = 0; i < ARRAY_SIZE(data_generic_valid); i++) {
        data = data_generic_valid[i];
        unsupported_params_init(mc, &data);

        /* when modules parameter is omitted, it will be set as 1 */
        data.expect_prefer_sockets.modules = 1;
        data.expect_prefer_cores.modules = 1;

        smp_parse_test(ms, &data, true);

        /* when modules parameter is specified */
        data.config.has_modules = true;
        data.config.modules = num_modules;
        if (data.config.has_cpus) {
            data.config.cpus *= num_modules;
        }
        if (data.config.has_maxcpus) {
            data.config.maxcpus *= num_modules;
        }

        data.expect_prefer_sockets.modules = num_modules;
        data.expect_prefer_sockets.cpus *= num_modules;
        data.expect_prefer_sockets.max_cpus *= num_modules;
        data.expect_prefer_cores.modules = num_modules;
        data.expect_prefer_cores.cpus *= num_modules;
        data.expect_prefer_cores.max_cpus *= num_modules;

        smp_parse_test(ms, &data, true);
    }

    for (i = 0; i < ARRAY_SIZE(data_with_modules_invalid); i++) {
        data = data_with_modules_invalid[i];
        unsupported_params_init(mc, &data);

        smp_parse_test(ms, &data, false);
    }

    object_unref(obj);
}

static void test_with_dies(const void *opaque)
{
    const char *machine_type = opaque;
    Object *obj = object_new(machine_type);
    MachineState *ms = MACHINE(obj);
    MachineClass *mc = MACHINE_GET_CLASS(obj);
    SMPTestData data = {};
    unsigned int num_dies = 2;
    int i;

    for (i = 0; i < ARRAY_SIZE(data_generic_valid); i++) {
        data = data_generic_valid[i];
        unsupported_params_init(mc, &data);

        /* when dies parameter is omitted, it will be set as 1 */
        data.expect_prefer_sockets.dies = 1;
        data.expect_prefer_cores.dies = 1;

        smp_parse_test(ms, &data, true);

        /* when dies parameter is specified */
        data.config.has_dies = true;
        data.config.dies = num_dies;
        if (data.config.has_cpus) {
            data.config.cpus *= num_dies;
        }
        if (data.config.has_maxcpus) {
            data.config.maxcpus *= num_dies;
        }

        data.expect_prefer_sockets.dies = num_dies;
        data.expect_prefer_sockets.cpus *= num_dies;
        data.expect_prefer_sockets.max_cpus *= num_dies;
        data.expect_prefer_cores.dies = num_dies;
        data.expect_prefer_cores.cpus *= num_dies;
        data.expect_prefer_cores.max_cpus *= num_dies;

        smp_parse_test(ms, &data, true);
    }

    for (i = 0; i < ARRAY_SIZE(data_with_dies_invalid); i++) {
        data = data_with_dies_invalid[i];
        unsupported_params_init(mc, &data);

        smp_parse_test(ms, &data, false);
    }

    object_unref(obj);
}

static void test_with_modules_dies(const void *opaque)
{
    const char *machine_type = opaque;
    Object *obj = object_new(machine_type);
    MachineState *ms = MACHINE(obj);
    MachineClass *mc = MACHINE_GET_CLASS(obj);
    SMPTestData data = {};
    unsigned int num_modules = 5, num_dies = 3;
    int i;

    for (i = 0; i < ARRAY_SIZE(data_generic_valid); i++) {
        data = data_generic_valid[i];
        unsupported_params_init(mc, &data);

        /*
         * when modules and dies parameters are omitted, they will
         * be both set as 1.
         */
        data.expect_prefer_sockets.modules = 1;
        data.expect_prefer_sockets.dies = 1;
        data.expect_prefer_cores.modules = 1;
        data.expect_prefer_cores.dies = 1;

        smp_parse_test(ms, &data, true);

        /* when modules and dies parameters are both specified */
        data.config.has_modules = true;
        data.config.modules = num_modules;
        data.config.has_dies = true;
        data.config.dies = num_dies;

        if (data.config.has_cpus) {
            data.config.cpus *= num_modules * num_dies;
        }
        if (data.config.has_maxcpus) {
            data.config.maxcpus *= num_modules * num_dies;
        }

        data.expect_prefer_sockets.modules = num_modules;
        data.expect_prefer_sockets.dies = num_dies;
        data.expect_prefer_sockets.cpus *= num_modules * num_dies;
        data.expect_prefer_sockets.max_cpus *= num_modules * num_dies;

        data.expect_prefer_cores.modules = num_modules;
        data.expect_prefer_cores.dies = num_dies;
        data.expect_prefer_cores.cpus *= num_modules * num_dies;
        data.expect_prefer_cores.max_cpus *= num_modules * num_dies;

        smp_parse_test(ms, &data, true);
    }

    for (i = 0; i < ARRAY_SIZE(data_with_modules_dies_invalid); i++) {
        data = data_with_modules_dies_invalid[i];
        unsupported_params_init(mc, &data);

        smp_parse_test(ms, &data, false);
    }

    object_unref(obj);
}

static void test_with_clusters(const void *opaque)
{
    const char *machine_type = opaque;
    Object *obj = object_new(machine_type);
    MachineState *ms = MACHINE(obj);
    MachineClass *mc = MACHINE_GET_CLASS(obj);
    SMPTestData data = {};
    unsigned int num_clusters = 2;
    int i;

    for (i = 0; i < ARRAY_SIZE(data_generic_valid); i++) {
        data = data_generic_valid[i];
        unsupported_params_init(mc, &data);

        /* when clusters parameter is omitted, it will be set as 1 */
        data.expect_prefer_sockets.clusters = 1;
        data.expect_prefer_cores.clusters = 1;

        smp_parse_test(ms, &data, true);

        /* when clusters parameter is specified */
        data.config.has_clusters = true;
        data.config.clusters = num_clusters;
        if (data.config.has_cpus) {
            data.config.cpus *= num_clusters;
        }
        if (data.config.has_maxcpus) {
            data.config.maxcpus *= num_clusters;
        }

        data.expect_prefer_sockets.clusters = num_clusters;
        data.expect_prefer_sockets.cpus *= num_clusters;
        data.expect_prefer_sockets.max_cpus *= num_clusters;
        data.expect_prefer_cores.clusters = num_clusters;
        data.expect_prefer_cores.cpus *= num_clusters;
        data.expect_prefer_cores.max_cpus *= num_clusters;

        smp_parse_test(ms, &data, true);
    }

    for (i = 0; i < ARRAY_SIZE(data_with_clusters_invalid); i++) {
        data = data_with_clusters_invalid[i];
        unsupported_params_init(mc, &data);

        smp_parse_test(ms, &data, false);
    }

    object_unref(obj);
}

static void test_with_books(const void *opaque)
{
    const char *machine_type = opaque;
    Object *obj = object_new(machine_type);
    MachineState *ms = MACHINE(obj);
    MachineClass *mc = MACHINE_GET_CLASS(obj);
    SMPTestData data = {};
    unsigned int num_books = 2;
    int i;

    for (i = 0; i < ARRAY_SIZE(data_generic_valid); i++) {
        data = data_generic_valid[i];
        unsupported_params_init(mc, &data);

        /* when books parameter is omitted, it will be set as 1 */
        data.expect_prefer_sockets.books = 1;
        data.expect_prefer_cores.books = 1;

        smp_parse_test(ms, &data, true);

        /* when books parameter is specified */
        data.config.has_books = true;
        data.config.books = num_books;
        if (data.config.has_cpus) {
            data.config.cpus *= num_books;
        }
        if (data.config.has_maxcpus) {
            data.config.maxcpus *= num_books;
        }

        data.expect_prefer_sockets.books = num_books;
        data.expect_prefer_sockets.cpus *= num_books;
        data.expect_prefer_sockets.max_cpus *= num_books;
        data.expect_prefer_cores.books = num_books;
        data.expect_prefer_cores.cpus *= num_books;
        data.expect_prefer_cores.max_cpus *= num_books;

        smp_parse_test(ms, &data, true);
    }

    for (i = 0; i < ARRAY_SIZE(data_with_books_invalid); i++) {
        data = data_with_books_invalid[i];
        unsupported_params_init(mc, &data);

        smp_parse_test(ms, &data, false);
    }

    object_unref(obj);
}

static void test_with_drawers(const void *opaque)
{
    const char *machine_type = opaque;
    Object *obj = object_new(machine_type);
    MachineState *ms = MACHINE(obj);
    MachineClass *mc = MACHINE_GET_CLASS(obj);
    SMPTestData data = {};
    unsigned int num_drawers = 2;
    int i;

    for (i = 0; i < ARRAY_SIZE(data_generic_valid); i++) {
        data = data_generic_valid[i];
        unsupported_params_init(mc, &data);

        /* when drawers parameter is omitted, it will be set as 1 */
        data.expect_prefer_sockets.drawers = 1;
        data.expect_prefer_cores.drawers = 1;

        smp_parse_test(ms, &data, true);

        /* when drawers parameter is specified */
        data.config.has_drawers = true;
        data.config.drawers = num_drawers;
        if (data.config.has_cpus) {
            data.config.cpus *= num_drawers;
        }
        if (data.config.has_maxcpus) {
            data.config.maxcpus *= num_drawers;
        }

        data.expect_prefer_sockets.drawers = num_drawers;
        data.expect_prefer_sockets.cpus *= num_drawers;
        data.expect_prefer_sockets.max_cpus *= num_drawers;
        data.expect_prefer_cores.drawers = num_drawers;
        data.expect_prefer_cores.cpus *= num_drawers;
        data.expect_prefer_cores.max_cpus *= num_drawers;

        smp_parse_test(ms, &data, true);
    }

    for (i = 0; i < ARRAY_SIZE(data_with_drawers_invalid); i++) {
        data = data_with_drawers_invalid[i];
        unsupported_params_init(mc, &data);

        smp_parse_test(ms, &data, false);
    }

    object_unref(obj);
}

static void test_with_drawers_books(const void *opaque)
{
    const char *machine_type = opaque;
    Object *obj = object_new(machine_type);
    MachineState *ms = MACHINE(obj);
    MachineClass *mc = MACHINE_GET_CLASS(obj);
    SMPTestData data = {};
    unsigned int num_drawers = 5, num_books = 3;
    int i;

    for (i = 0; i < ARRAY_SIZE(data_generic_valid); i++) {
        data = data_generic_valid[i];
        unsupported_params_init(mc, &data);

        /*
         * when drawers and books parameters are omitted, they will
         * be both set as 1.
         */
        data.expect_prefer_sockets.drawers = 1;
        data.expect_prefer_sockets.books = 1;
        data.expect_prefer_cores.drawers = 1;
        data.expect_prefer_cores.books = 1;

        smp_parse_test(ms, &data, true);

        /* when drawers and books parameters are both specified */
        data.config.has_drawers = true;
        data.config.drawers = num_drawers;
        data.config.has_books = true;
        data.config.books = num_books;

        if (data.config.has_cpus) {
            data.config.cpus *= num_drawers * num_books;
        }
        if (data.config.has_maxcpus) {
            data.config.maxcpus *= num_drawers * num_books;
        }

        data.expect_prefer_sockets.drawers = num_drawers;
        data.expect_prefer_sockets.books = num_books;
        data.expect_prefer_sockets.cpus *= num_drawers * num_books;
        data.expect_prefer_sockets.max_cpus *= num_drawers * num_books;

        data.expect_prefer_cores.drawers = num_drawers;
        data.expect_prefer_cores.books = num_books;
        data.expect_prefer_cores.cpus *= num_drawers * num_books;
        data.expect_prefer_cores.max_cpus *= num_drawers * num_books;

        smp_parse_test(ms, &data, true);
    }

    for (i = 0; i < ARRAY_SIZE(data_with_drawers_books_invalid); i++) {
        data = data_with_drawers_books_invalid[i];
        unsupported_params_init(mc, &data);

        smp_parse_test(ms, &data, false);
    }

    object_unref(obj);
}

static void test_full_topo(const void *opaque)
{
    const char *machine_type = opaque;
    Object *obj = object_new(machine_type);
    MachineState *ms = MACHINE(obj);
    MachineClass *mc = MACHINE_GET_CLASS(obj);
    SMPTestData data = {};
    unsigned int drawers, books, dies, clusters, modules, multiplier;
    int i;

    drawers = 5;
    books = 3;
    dies = 2;
    clusters = 3;
    modules = 2;

    multiplier = drawers * books * dies * clusters * modules;
    for (i = 0; i < ARRAY_SIZE(data_generic_valid); i++) {
        data = data_generic_valid[i];
        unsupported_params_init(mc, &data);

        /*
         * when drawers, books, dies, clusters and modules parameters are
         * omitted, they will be set as 1.
         */
        data.expect_prefer_sockets.drawers = 1;
        data.expect_prefer_sockets.books = 1;
        data.expect_prefer_sockets.dies = 1;
        data.expect_prefer_sockets.clusters = 1;
        data.expect_prefer_sockets.modules = 1;
        data.expect_prefer_cores.drawers = 1;
        data.expect_prefer_cores.books = 1;
        data.expect_prefer_cores.dies = 1;
        data.expect_prefer_cores.clusters = 1;
        data.expect_prefer_cores.modules = 1;

        smp_parse_test(ms, &data, true);

        /*
         * when drawers, books, dies, clusters and modules parameters
         * are specified.
         */
        data.config.has_drawers = true;
        data.config.drawers = drawers;
        data.config.has_books = true;
        data.config.books = books;
        data.config.has_dies = true;
        data.config.dies = dies;
        data.config.has_clusters = true;
        data.config.clusters = clusters;
        data.config.has_modules = true;
        data.config.modules = modules;

        if (data.config.has_cpus) {
            data.config.cpus *= multiplier;
        }
        if (data.config.has_maxcpus) {
            data.config.maxcpus *= multiplier;
        }

        data.expect_prefer_sockets.drawers = drawers;
        data.expect_prefer_sockets.books = books;
        data.expect_prefer_sockets.dies = dies;
        data.expect_prefer_sockets.clusters = clusters;
        data.expect_prefer_sockets.modules = modules;
        data.expect_prefer_sockets.cpus *= multiplier;
        data.expect_prefer_sockets.max_cpus *= multiplier;

        data.expect_prefer_cores.drawers = drawers;
        data.expect_prefer_cores.books = books;
        data.expect_prefer_cores.dies = dies;
        data.expect_prefer_cores.clusters = clusters;
        data.expect_prefer_cores.modules = modules;
        data.expect_prefer_cores.cpus *= multiplier;
        data.expect_prefer_cores.max_cpus *= multiplier;

        smp_parse_test(ms, &data, true);
    }

    for (i = 0; i < ARRAY_SIZE(data_full_topo_invalid); i++) {
        data = data_full_topo_invalid[i];
        unsupported_params_init(mc, &data);

        smp_parse_test(ms, &data, false);
    }

    for (i = 0; i < ARRAY_SIZE(data_zero_topo_invalid); i++) {
        data = data_zero_topo_invalid[i];
        unsupported_params_init(mc, &data);

        smp_parse_test(ms, &data, false);
    }

    object_unref(obj);
}

/* Type info of the tested machine */
static const TypeInfo smp_machine_types[] = {
    {
        .name           = TYPE_MACHINE,
        .parent         = TYPE_OBJECT,
        .abstract       = true,
        .class_init     = machine_base_class_init,
        .class_size     = sizeof(MachineClass),
        .instance_size  = sizeof(MachineState),
    }, {
        .name           = MACHINE_TYPE_NAME("smp-generic-valid"),
        .parent         = TYPE_MACHINE,
    }, {
        .name           = MACHINE_TYPE_NAME("smp-generic-invalid"),
        .parent         = TYPE_MACHINE,
        .class_init     = machine_generic_invalid_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("smp-with-modules"),
        .parent         = TYPE_MACHINE,
        .class_init     = machine_with_modules_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("smp-with-dies"),
        .parent         = TYPE_MACHINE,
        .class_init     = machine_with_dies_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("smp-with-modules-dies"),
        .parent         = TYPE_MACHINE,
        .class_init     = machine_with_modules_dies_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("smp-with-clusters"),
        .parent         = TYPE_MACHINE,
        .class_init     = machine_with_clusters_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("smp-with-books"),
        .parent         = TYPE_MACHINE,
        .class_init     = machine_with_books_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("smp-with-drawers"),
        .parent         = TYPE_MACHINE,
        .class_init     = machine_with_drawers_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("smp-with-drawers-books"),
        .parent         = TYPE_MACHINE,
        .class_init     = machine_with_drawers_books_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("smp-full-topo"),
        .parent         = TYPE_MACHINE,
        .class_init     = machine_full_topo_class_init,
    }
};

DEFINE_TYPES(smp_machine_types)

int main(int argc, char *argv[])
{
    module_call_init(MODULE_INIT_QOM);

    g_test_init(&argc, &argv, NULL);

    g_test_add_data_func("/test-smp-parse/generic/valid",
                         MACHINE_TYPE_NAME("smp-generic-valid"),
                         test_generic_valid);
    g_test_add_data_func("/test-smp-parse/generic/invalid",
                         MACHINE_TYPE_NAME("smp-generic-invalid"),
                         test_generic_invalid);
    g_test_add_data_func("/test-smp-parse/with_modules",
                         MACHINE_TYPE_NAME("smp-with-modules"),
                         test_with_modules);
    g_test_add_data_func("/test-smp-parse/with_dies",
                         MACHINE_TYPE_NAME("smp-with-dies"),
                         test_with_dies);
    g_test_add_data_func("/test-smp-parse/with_modules_dies",
                         MACHINE_TYPE_NAME("smp-with-modules-dies"),
                         test_with_modules_dies);
    g_test_add_data_func("/test-smp-parse/with_clusters",
                         MACHINE_TYPE_NAME("smp-with-clusters"),
                         test_with_clusters);
    g_test_add_data_func("/test-smp-parse/with_books",
                         MACHINE_TYPE_NAME("smp-with-books"),
                         test_with_books);
    g_test_add_data_func("/test-smp-parse/with_drawers",
                         MACHINE_TYPE_NAME("smp-with-drawers"),
                         test_with_drawers);
    g_test_add_data_func("/test-smp-parse/with_drawers_books",
                         MACHINE_TYPE_NAME("smp-with-drawers-books"),
                         test_with_drawers_books);
    g_test_add_data_func("/test-smp-parse/full",
                         MACHINE_TYPE_NAME("smp-full-topo"),
                         test_full_topo);

    g_test_run();

    return 0;
}
