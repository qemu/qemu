/*
 * NUMA parameter parsing routines
 *
 * Copyright (c) 2014 Fujitsu Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "sysemu/sysemu.h"
#include "exec/cpu-common.h"
#include "qemu/bitmap.h"
#include "qom/cpu.h"
#include "qemu/error-report.h"
#include "include/exec/cpu-common.h" /* for RAM_ADDR_FMT */

static void numa_node_parse_cpus(int nodenr, const char *cpus)
{
    char *endptr;
    unsigned long long value, endvalue;

    /* Empty CPU range strings will be considered valid, they will simply
     * not set any bit in the CPU bitmap.
     */
    if (!*cpus) {
        return;
    }

    if (parse_uint(cpus, &value, &endptr, 10) < 0) {
        goto error;
    }
    if (*endptr == '-') {
        if (parse_uint_full(endptr + 1, &endvalue, 10) < 0) {
            goto error;
        }
    } else if (*endptr == '\0') {
        endvalue = value;
    } else {
        goto error;
    }

    if (endvalue >= MAX_CPUMASK_BITS) {
        endvalue = MAX_CPUMASK_BITS - 1;
        fprintf(stderr,
            "qemu: NUMA: A max of %d VCPUs are supported\n",
             MAX_CPUMASK_BITS);
    }

    if (endvalue < value) {
        goto error;
    }

    bitmap_set(numa_info[nodenr].node_cpu, value, endvalue-value+1);
    return;

error:
    fprintf(stderr, "qemu: Invalid NUMA CPU range: %s\n", cpus);
    exit(1);
}

void numa_add(const char *optarg)
{
    char option[128];
    char *endptr;
    unsigned long long nodenr;

    optarg = get_opt_name(option, 128, optarg, ',');
    if (*optarg == ',') {
        optarg++;
    }
    if (!strcmp(option, "node")) {

        if (nb_numa_nodes >= MAX_NODES) {
            fprintf(stderr, "qemu: too many NUMA nodes\n");
            exit(1);
        }

        if (get_param_value(option, 128, "nodeid", optarg) == 0) {
            nodenr = nb_numa_nodes;
        } else {
            if (parse_uint_full(option, &nodenr, 10) < 0) {
                fprintf(stderr, "qemu: Invalid NUMA nodeid: %s\n", option);
                exit(1);
            }
        }

        if (nodenr >= MAX_NODES) {
            fprintf(stderr, "qemu: invalid NUMA nodeid: %llu\n", nodenr);
            exit(1);
        }

        if (get_param_value(option, 128, "mem", optarg) == 0) {
            numa_info[nodenr].node_mem = 0;
        } else {
            int64_t sval;
            sval = strtosz(option, &endptr);
            if (sval < 0 || *endptr) {
                fprintf(stderr, "qemu: invalid numa mem size: %s\n", optarg);
                exit(1);
            }
            numa_info[nodenr].node_mem = sval;
        }
        if (get_param_value(option, 128, "cpus", optarg) != 0) {
            numa_node_parse_cpus(nodenr, option);
        }
        nb_numa_nodes++;
    } else {
        fprintf(stderr, "Invalid -numa option: %s\n", option);
        exit(1);
    }
}

void set_numa_nodes(void)
{
    if (nb_numa_nodes > 0) {
        uint64_t numa_total;
        int i;

        if (nb_numa_nodes > MAX_NODES) {
            nb_numa_nodes = MAX_NODES;
        }

        /* If no memory size if given for any node, assume the default case
         * and distribute the available memory equally across all nodes
         */
        for (i = 0; i < nb_numa_nodes; i++) {
            if (numa_info[i].node_mem != 0) {
                break;
            }
        }
        if (i == nb_numa_nodes) {
            uint64_t usedmem = 0;

            /* On Linux, the each node's border has to be 8MB aligned,
             * the final node gets the rest.
             */
            for (i = 0; i < nb_numa_nodes - 1; i++) {
                numa_info[i].node_mem = (ram_size / nb_numa_nodes) &
                                        ~((1 << 23UL) - 1);
                usedmem += numa_info[i].node_mem;
            }
            numa_info[i].node_mem = ram_size - usedmem;
        }

        numa_total = 0;
        for (i = 0; i < nb_numa_nodes; i++) {
            numa_total += numa_info[i].node_mem;
        }
        if (numa_total != ram_size) {
            error_report("total memory for NUMA nodes (%" PRIu64 ")"
                         " should equal RAM size (" RAM_ADDR_FMT ")",
                         numa_total, ram_size);
            exit(1);
        }

        for (i = 0; i < nb_numa_nodes; i++) {
            if (!bitmap_empty(numa_info[i].node_cpu, MAX_CPUMASK_BITS)) {
                break;
            }
        }
        /* assigning the VCPUs round-robin is easier to implement, guest OSes
         * must cope with this anyway, because there are BIOSes out there in
         * real machines which also use this scheme.
         */
        if (i == nb_numa_nodes) {
            for (i = 0; i < max_cpus; i++) {
                set_bit(i, numa_info[i % nb_numa_nodes].node_cpu);
            }
        }
    }
}

void set_numa_modes(void)
{
    CPUState *cpu;
    int i;

    CPU_FOREACH(cpu) {
        for (i = 0; i < nb_numa_nodes; i++) {
            if (test_bit(cpu->cpu_index, numa_info[i].node_cpu)) {
                cpu->numa_node = i;
            }
        }
    }
}
