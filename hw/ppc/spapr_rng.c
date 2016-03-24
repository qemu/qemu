/*
 * QEMU sPAPR random number generator "device" for H_RANDOM hypercall
 *
 * Copyright 2015 Thomas Huth, Red Hat Inc.
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
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "qemu/error-report.h"
#include "sysemu/sysemu.h"
#include "sysemu/device_tree.h"
#include "sysemu/rng.h"
#include "hw/ppc/spapr.h"
#include "kvm_ppc.h"

#define SPAPR_RNG(obj) \
    OBJECT_CHECK(sPAPRRngState, (obj), TYPE_SPAPR_RNG)

struct sPAPRRngState {
    /*< private >*/
    DeviceState ds;
    RngBackend *backend;
    bool use_kvm;
};
typedef struct sPAPRRngState sPAPRRngState;

struct HRandomData {
    QemuSemaphore sem;
    union {
        uint64_t v64;
        uint8_t v8[8];
    } val;
    int received;
};
typedef struct HRandomData HRandomData;

/* Callback function for the RngBackend */
static void random_recv(void *dest, const void *src, size_t size)
{
    HRandomData *hrdp = dest;

    if (src && size > 0) {
        assert(size + hrdp->received <= sizeof(hrdp->val.v8));
        memcpy(&hrdp->val.v8[hrdp->received], src, size);
        hrdp->received += size;
    }

    qemu_sem_post(&hrdp->sem);
}

/* Handler for the H_RANDOM hypercall */
static target_ulong h_random(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                             target_ulong opcode, target_ulong *args)
{
    sPAPRRngState *rngstate;
    HRandomData hrdata;

    rngstate = SPAPR_RNG(object_resolve_path_type("", TYPE_SPAPR_RNG, NULL));

    if (!rngstate || !rngstate->backend) {
        return H_HARDWARE;
    }

    qemu_sem_init(&hrdata.sem, 0);
    hrdata.val.v64 = 0;
    hrdata.received = 0;

    while (hrdata.received < 8) {
        rng_backend_request_entropy(rngstate->backend, 8 - hrdata.received,
                                    random_recv, &hrdata);
        qemu_mutex_unlock_iothread();
        qemu_sem_wait(&hrdata.sem);
        qemu_mutex_lock_iothread();
    }

    qemu_sem_destroy(&hrdata.sem);
    args[0] = hrdata.val.v64;

    return H_SUCCESS;
}

static void spapr_rng_instance_init(Object *obj)
{
    sPAPRRngState *rngstate = SPAPR_RNG(obj);

    if (object_resolve_path_type("", TYPE_SPAPR_RNG, NULL) != NULL) {
        error_report("spapr-rng can not be instantiated twice!");
        return;
    }

    object_property_add_link(obj, "rng", TYPE_RNG_BACKEND,
                             (Object **)&rngstate->backend,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE, NULL);
    object_property_set_description(obj, "rng",
                                    "ID of the random number generator backend",
                                    NULL);
}

static void spapr_rng_realize(DeviceState *dev, Error **errp)
{

    sPAPRRngState *rngstate = SPAPR_RNG(dev);

    if (rngstate->use_kvm) {
        if (kvmppc_enable_hwrng() == 0) {
            return;
        }
        /*
         * If user specified both, use-kvm and a backend, we fall back to
         * the backend now. If not, provide an appropriate error message.
         */
        if (!rngstate->backend) {
            error_setg(errp, "Could not initialize in-kernel H_RANDOM call!");
            return;
        }
    }

    if (rngstate->backend) {
        spapr_register_hypercall(H_RANDOM, h_random);
    } else {
        error_setg(errp, "spapr-rng needs an RNG backend!");
    }
}

int spapr_rng_populate_dt(void *fdt)
{
    int node;
    int ret;

    node = qemu_fdt_add_subnode(fdt, "/ibm,platform-facilities");
    if (node <= 0) {
        return -1;
    }
    ret = fdt_setprop_string(fdt, node, "device_type",
                             "ibm,platform-facilities");
    ret |= fdt_setprop_cell(fdt, node, "#address-cells", 0x1);
    ret |= fdt_setprop_cell(fdt, node, "#size-cells", 0x0);

    node = fdt_add_subnode(fdt, node, "ibm,random-v1");
    if (node <= 0) {
        return -1;
    }
    ret |= fdt_setprop_string(fdt, node, "compatible", "ibm,random");

    return ret ? -1 : 0;
}

static Property spapr_rng_properties[] = {
    DEFINE_PROP_BOOL("use-kvm", sPAPRRngState, use_kvm, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void spapr_rng_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = spapr_rng_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->props = spapr_rng_properties;
    dc->hotpluggable = false;
}

static const TypeInfo spapr_rng_info = {
    .name          = TYPE_SPAPR_RNG,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(sPAPRRngState),
    .instance_init = spapr_rng_instance_init,
    .class_init    = spapr_rng_class_init,
};

static void spapr_rng_register_type(void)
{
    type_register_static(&spapr_rng_info);
}
type_init(spapr_rng_register_type)
