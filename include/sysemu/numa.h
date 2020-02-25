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

/* the value of AcpiHmatLBInfo flags */
enum {
    HMAT_LB_MEM_MEMORY           = 0,
    HMAT_LB_MEM_CACHE_1ST_LEVEL  = 1,
    HMAT_LB_MEM_CACHE_2ND_LEVEL  = 2,
    HMAT_LB_MEM_CACHE_3RD_LEVEL  = 3,
    HMAT_LB_LEVELS   /* must be the last entry */
};

/* the value of AcpiHmatLBInfo data type */
enum {
    HMAT_LB_DATA_ACCESS_LATENCY   = 0,
    HMAT_LB_DATA_READ_LATENCY     = 1,
    HMAT_LB_DATA_WRITE_LATENCY    = 2,
    HMAT_LB_DATA_ACCESS_BANDWIDTH = 3,
    HMAT_LB_DATA_READ_BANDWIDTH   = 4,
    HMAT_LB_DATA_WRITE_BANDWIDTH  = 5,
    HMAT_LB_TYPES   /* must be the last entry */
};

#define UINT16_BITS       16

struct NodeInfo {
    uint64_t node_mem;
    struct HostMemoryBackend *node_memdev;
    bool present;
    bool has_cpu;
    uint8_t lb_info_provided;
    uint16_t initiator;
    uint8_t distance[MAX_NODES];
};

struct NumaNodeMem {
    uint64_t node_mem;
    uint64_t node_plugged_mem;
};

struct HMAT_LB_Data {
    uint8_t     initiator;
    uint8_t     target;
    uint64_t    data;
};
typedef struct HMAT_LB_Data HMAT_LB_Data;

struct HMAT_LB_Info {
    /* Indicates it's memory or the specified level memory side cache. */
    uint8_t     hierarchy;

    /* Present the type of data, access/read/write latency or bandwidth. */
    uint8_t     data_type;

    /* The range bitmap of bandwidth for calculating common base */
    uint64_t    range_bitmap;

    /* The common base unit for latencies or bandwidths */
    uint64_t    base;

    /* Array to store the latencies or bandwidths */
    GArray      *list;
};
typedef struct HMAT_LB_Info HMAT_LB_Info;

struct NumaState {
    /* Number of NUMA nodes */
    int num_nodes;

    /* Allow setting NUMA distance for different NUMA nodes */
    bool have_numa_distance;

    /* Detect if HMAT support is enabled. */
    bool hmat_enabled;

    /* NUMA nodes information */
    NodeInfo nodes[MAX_NODES];

    /* NUMA nodes HMAT Locality Latency and Bandwidth Information */
    HMAT_LB_Info *hmat_lb[HMAT_LB_LEVELS][HMAT_LB_TYPES];

    /* Memory Side Cache Information Structure */
    NumaHmatCacheOptions *hmat_cache[MAX_NODES][HMAT_LB_LEVELS];
};
typedef struct NumaState NumaState;

void set_numa_options(MachineState *ms, NumaOptions *object, Error **errp);
void parse_numa_opts(MachineState *ms);
void parse_numa_hmat_lb(NumaState *numa_state, NumaHmatLBOptions *node,
                        Error **errp);
void parse_numa_hmat_cache(MachineState *ms, NumaHmatCacheOptions *node,
                           Error **errp);
void numa_complete_configuration(MachineState *ms);
void query_numa_node_mem(NumaNodeMem node_mem[], MachineState *ms);
extern QemuOptsList qemu_numa_opts;
void numa_legacy_auto_assign_ram(MachineClass *mc, NodeInfo *nodes,
                                 int nb_nodes, ram_addr_t size);
void numa_default_auto_assign_ram(MachineClass *mc, NodeInfo *nodes,
                                  int nb_nodes, ram_addr_t size);
void numa_cpu_pre_plug(const struct CPUArchId *slot, DeviceState *dev,
                       Error **errp);
bool numa_uses_legacy_mem(void);

#endif
