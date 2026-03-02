/*
 * Nitro Enclaves accelerator
 *
 * Copyright © 2026 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors:
 *   Alexander Graf <graf@amazon.com>
 *
 * Nitro Enclaves are a confidential compute technology which
 * allows a parent instance to carve out resources from itself
 * and spawn a confidential sibling VM next to itself. Similar
 * to other confidential compute solutions, this sibling is
 * controlled by an underlying vmm, but still has a higher level
 * vmm (QEMU) to implement some of its I/O functionality and
 * lifecycle.
 *
 * This accelerator drives /dev/nitro_enclaves to spawn a Nitro
 * Enclave. It works in tandem with the nitro_enclaves machine
 * which ensures the correct backend devices are available and
 * that the initial seed (an EIF file) is loaded at the correct
 * offset in memory.
 *
 * The accel starts the enclave when the machine starts, after
 * all device setup is finished.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/module.h"
#include "qemu/rcu.h"
#include "qemu/accel.h"
#include "qemu/guest-random.h"
#include "qemu/main-loop.h"
#include "accel/accel-ops.h"
#include "accel/accel-cpu-ops.h"
#include "accel/dummy-cpus.h"
#include "system/cpus.h"
#include "hw/core/cpu.h"
#include "hw/core/boards.h"
#include "hw/nitro/nitro-vsock-bus.h"
#include "system/ramblock.h"
#include "system/nitro-accel.h"
#include "trace.h"

#include <sys/ioctl.h>
#include "standard-headers/linux/nitro_enclaves.h"

bool nitro_allowed;

typedef struct NitroAccelState {
    AccelState parent_obj;

    int ne_fd;
    int enclave_fd;
    uint64_t slot_uid;
    uint64_t enclave_cid;
    bool debug_mode;
} NitroAccelState;

static int nitro_init_machine(AccelState *as, MachineState *ms)
{
    NitroAccelState *s = NITRO_ACCEL(as);
    uint64_t slot_uid = 0;
    int ret;

    s->ne_fd = open("/dev/nitro_enclaves", O_RDWR | O_CLOEXEC);
    if (s->ne_fd < 0) {
        error_report("nitro: failed to open /dev/nitro_enclaves: %s",
                     strerror(errno));
        return -errno;
    }

    ret = ioctl(s->ne_fd, NE_CREATE_VM, &slot_uid);
    if (ret < 0) {
        error_report("nitro: NE_CREATE_VM failed: %s", strerror(errno));
        close(s->ne_fd);
        return -errno;
    }
    s->enclave_fd = ret;
    s->slot_uid = slot_uid;

    return 0;
}

static int nitro_donate_ram_block(RAMBlock *rb, void *opaque)
{
    NitroAccelState *s = opaque;
    struct ne_user_memory_region region = {
        .flags = 0,
        .memory_size = rb->used_length,
        .userspace_addr = (uint64_t)(uintptr_t)rb->host,
    };

    if (!rb->used_length) {
        return 0;
    }

    if (ioctl(s->enclave_fd, NE_SET_USER_MEMORY_REGION, &region) < 0) {
        error_report("nitro: NE_SET_USER_MEMORY_REGION failed for %s "
                     "(%" PRIu64 " bytes): %s", rb->idstr, rb->used_length,
                     strerror(errno));
        return -errno;
    }
    return 0;
}

/*
 * Start the Enclave. At this point memory is set up and the EIF is loaded.
 * This function donates memory, adds vCPUs, and starts the enclave.
 */
static void nitro_setup_post(AccelState *as)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    NitroAccelState *s = NITRO_ACCEL(as);
    int nr_cpus = ms->smp.cpus;
    int i, ret;
    struct ne_enclave_start_info start_info = {
        .flags = s->debug_mode ? NE_ENCLAVE_DEBUG_MODE : 0,
        .enclave_cid = s->enclave_cid,
    };

    ret = qemu_ram_foreach_block(nitro_donate_ram_block, s);
    if (ret < 0) {
        error_report("nitro: failed to donate memory");
        exit(1);
    }

    for (i = 0; i < nr_cpus; i++) {
        uint32_t cpu_id = 0;
        if (ioctl(s->enclave_fd, NE_ADD_VCPU, &cpu_id) < 0) {
            error_report("nitro: NE_ADD_VCPU failed: %s", strerror(errno));
            exit(1);
        }
    }

    ret = ioctl(s->enclave_fd, NE_START_ENCLAVE, &start_info);
    if (ret < 0) {
        switch (errno) {
        case NE_ERR_NO_MEM_REGIONS_ADDED:
            error_report("nitro: no memory regions added");
            break;
        case NE_ERR_NO_VCPUS_ADDED:
            error_report("nitro: no vCPUs added");
            break;
        case NE_ERR_ENCLAVE_MEM_MIN_SIZE:
            error_report("nitro: memory is below the minimum "
                         "required size. Try increasing -m");
            break;
        case NE_ERR_FULL_CORES_NOT_USED:
            error_report("nitro: requires full CPU cores. "
                         "Try increasing -smp to a multiple of threads "
                         "per core on this host (e.g. -smp 2)");
            break;
        case NE_ERR_NOT_IN_INIT_STATE:
            error_report("nitro: not in init state");
            break;
        case NE_ERR_INVALID_FLAG_VALUE:
            error_report("nitro: invalid flag value for NE_START_ENCLAVE");
            break;
        case NE_ERR_INVALID_ENCLAVE_CID:
            error_report("nitro: invalid enclave CID");
            break;
        default:
            error_report("nitro: NE_START_ENCLAVE failed: %s (errno %d)",
                         strerror(errno), errno);
            break;
        }
        exit(1);
    }

    s->enclave_cid = start_info.enclave_cid;
    trace_nitro_enclave_started(s->enclave_cid);

    /*
     * Notify all Nitro vsock bus devices that the enclave has started
     * and provide them with the CID for vsock connections.
     */
    {
        NitroVsockBridge *bridge = nitro_vsock_bridge_find();
        Error *err = NULL;

        if (bridge) {
            nitro_vsock_bridge_start_enclave(bridge,
                                             (uint32_t)s->enclave_cid, &err);
            if (err) {
                error_report_err(err);
                exit(1);
            }
        }
    }
}

/* QOM properties */

static bool nitro_get_debug_mode(Object *obj, Error **errp)
{
    return NITRO_ACCEL(obj)->debug_mode;
}

static void nitro_set_debug_mode(Object *obj, bool value, Error **errp)
{
    NITRO_ACCEL(obj)->debug_mode = value;
}

static void nitro_get_enclave_cid(Object *obj, Visitor *v,
                                  const char *name, void *opaque,
                                  Error **errp)
{
    uint64_t val = NITRO_ACCEL(obj)->enclave_cid;
    visit_type_uint64(v, name, &val, errp);
}

static void nitro_set_enclave_cid(Object *obj, Visitor *v,
                                  const char *name, void *opaque,
                                  Error **errp)
{
    uint64_t val;
    if (visit_type_uint64(v, name, &val, errp)) {
        NITRO_ACCEL(obj)->enclave_cid = val;
    }
}

static void nitro_accel_class_init(ObjectClass *oc, const void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "Nitro";
    ac->init_machine = nitro_init_machine;
    ac->setup_post = nitro_setup_post;
    ac->allowed = &nitro_allowed;

    object_class_property_add_bool(oc, "debug-mode",
                                   nitro_get_debug_mode,
                                   nitro_set_debug_mode);
    object_class_property_set_description(oc, "debug-mode",
        "Start enclave in debug mode (enables console output)");

    object_class_property_add(oc, "enclave-cid", "uint64",
                              nitro_get_enclave_cid,
                              nitro_set_enclave_cid,
                              NULL, NULL);
    object_class_property_set_description(oc, "enclave-cid",
        "Enclave CID (0 = auto-assigned by Nitro)");
}

static const TypeInfo nitro_accel_type = {
    .name = TYPE_NITRO_ACCEL,
    .parent = TYPE_ACCEL,
    .instance_size = sizeof(NitroAccelState),
    .class_init = nitro_accel_class_init,
};
module_obj(TYPE_NITRO_ACCEL);

static bool nitro_cpus_are_resettable(void)
{
    return false;
}

static void nitro_accel_ops_class_init(ObjectClass *oc, const void *data)
{
    AccelOpsClass *ops = ACCEL_OPS_CLASS(oc);
    ops->create_vcpu_thread = dummy_start_vcpu_thread;
    ops->handle_interrupt = generic_handle_interrupt;
    ops->cpus_are_resettable = nitro_cpus_are_resettable;
}

static const TypeInfo nitro_accel_ops_type = {
    .name = ACCEL_OPS_NAME("nitro"),
    .parent = TYPE_ACCEL_OPS,
    .class_init = nitro_accel_ops_class_init,
    .abstract = true,
};
module_obj(ACCEL_OPS_NAME("nitro"));

static void nitro_type_init(void)
{
    type_register_static(&nitro_accel_type);
    type_register_static(&nitro_accel_ops_type);
}

type_init(nitro_type_init);
