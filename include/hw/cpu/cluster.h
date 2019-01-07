/*
 * QEMU CPU cluster
 *
 * Copyright (c) 2018 GreenSocs SAS
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */
#ifndef HW_CPU_CLUSTER_H
#define HW_CPU_CLUSTER_H

#include "qemu/osdep.h"
#include "hw/qdev.h"

/*
 * CPU Cluster type
 *
 * A cluster is a group of CPUs which are all identical and have the same view
 * of the rest of the system. It is mainly an internal QEMU representation and
 * does not necessarily match with the notion of clusters on the real hardware.
 *
 * If CPUs are not identical (for example, Cortex-A53 and Cortex-A57 CPUs in an
 * Arm big.LITTLE system) they should be in different clusters. If the CPUs do
 * not have the same view of memory (for example the main CPU and a management
 * controller processor) they should be in different clusters.
 */

#define TYPE_CPU_CLUSTER "cpu-cluster"
#define CPU_CLUSTER(obj) \
    OBJECT_CHECK(CPUClusterState, (obj), TYPE_CPU_CLUSTER)

/**
 * CPUClusterState:
 * @cluster_id: The cluster ID. This value is for internal use only and should
 *   not be exposed directly to the user or to the guest.
 *
 * State of a CPU cluster.
 */
typedef struct CPUClusterState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    uint32_t cluster_id;
} CPUClusterState;

#endif
