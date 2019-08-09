#ifndef SYSEMU_NUMA_H
#define SYSEMU_NUMA_H

#include "qemu/bitmap.h"
#include "qapi/qapi-types-machine.h"
#include "exec/cpu-common.h"

struct CPUArchId;

#define MAX_NODES 128
#define NUMA_NODE_UNASSIGNED MAX_NODES
#define NUMA_DISTANCE_MIN         10
#define NUMA_DISTANCE_DEFAULT     20
#define NUMA_DISTANCE_MAX         254
#define NUMA_DISTANCE_UNREACHABLE 255

struct NodeInfo {
    uint64_t node_mem;
    struct HostMemoryBackend *node_memdev;
    bool present;
    uint8_t distance[MAX_NODES];
};

struct NumaNodeMem {
    uint64_t node_mem;
    uint64_t node_plugged_mem;
};

struct NumaState {
    /* Number of NUMA nodes */
    int num_nodes;

    /* Allow setting NUMA distance for different NUMA nodes */
    bool have_numa_distance;

    /* NUMA nodes information */
    NodeInfo nodes[MAX_NODES];
};
typedef struct NumaState NumaState;

void set_numa_options(MachineState *ms, NumaOptions *object, Error **errp);
void parse_numa_opts(MachineState *ms);
void numa_complete_configuration(MachineState *ms);
void query_numa_node_mem(NumaNodeMem node_mem[], MachineState *ms);
extern QemuOptsList qemu_numa_opts;
void numa_legacy_auto_assign_ram(MachineClass *mc, NodeInfo *nodes,
                                 int nb_nodes, ram_addr_t size);
void numa_default_auto_assign_ram(MachineClass *mc, NodeInfo *nodes,
                                  int nb_nodes, ram_addr_t size);
void numa_cpu_pre_plug(const struct CPUArchId *slot, DeviceState *dev,
                       Error **errp);

#endif
