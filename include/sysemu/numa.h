#ifndef SYSEMU_NUMA_H
#define SYSEMU_NUMA_H

#include <stdint.h>
#include "qemu/bitmap.h"
#include "qemu/option.h"
#include "sysemu/sysemu.h"
#include "sysemu/hostmem.h"
#include "hw/boards.h"

extern int nb_numa_nodes;   /* Number of NUMA nodes */

typedef struct node_info {
    uint64_t node_mem;
    DECLARE_BITMAP(node_cpu, MAX_CPUMASK_BITS);
    struct HostMemoryBackend *node_memdev;
    bool present;
} NodeInfo;
extern NodeInfo numa_info[MAX_NODES];
void parse_numa_opts(MachineClass *mc);
void numa_post_machine_init(void);
void query_numa_node_mem(uint64_t node_mem[]);
extern QemuOptsList qemu_numa_opts;

#endif
