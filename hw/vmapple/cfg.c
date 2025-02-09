/*
 * VMApple Configuration Region
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/vmapple/vmapple.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "net/net.h"

OBJECT_DECLARE_SIMPLE_TYPE(VMAppleCfgState, VMAPPLE_CFG)

#define VMAPPLE_CFG_SIZE 0x00010000

typedef struct VMAppleCfg {
    uint32_t version;         /* 0x000 */
    uint32_t nr_cpus;         /* 0x004 */
    uint32_t unk1;            /* 0x008 */
    uint32_t unk2;            /* 0x00c */
    uint32_t unk3;            /* 0x010 */
    uint32_t unk4;            /* 0x014 */
    uint64_t ecid;            /* 0x018 */
    uint64_t ram_size;        /* 0x020 */
    uint32_t run_installer1;  /* 0x028 */
    uint32_t unk5;            /* 0x02c */
    uint32_t unk6;            /* 0x030 */
    uint32_t run_installer2;  /* 0x034 */
    uint32_t rnd;             /* 0x038 */
    uint32_t unk7;            /* 0x03c */
    MACAddr mac_en0;          /* 0x040 */
    uint8_t pad1[2];
    MACAddr mac_en1;          /* 0x048 */
    uint8_t pad2[2];
    MACAddr mac_wifi0;        /* 0x050 */
    uint8_t pad3[2];
    MACAddr mac_bt0;          /* 0x058 */
    uint8_t pad4[2];
    uint8_t reserved[0xa0];   /* 0x060 */
    uint32_t cpu_ids[0x80];   /* 0x100 */
    uint8_t scratch[0x200];   /* 0x180 */
    char serial[32];          /* 0x380 */
    char unk8[32];            /* 0x3a0 */
    char model[32];           /* 0x3c0 */
    uint8_t unk9[32];         /* 0x3e0 */
    uint32_t unk10;           /* 0x400 */
    char soc_name[32];        /* 0x404 */
} VMAppleCfg;

struct VMAppleCfgState {
    SysBusDevice parent_obj;
    VMAppleCfg cfg;

    MemoryRegion mem;
    char *serial;
    char *model;
    char *soc_name;
};

static void vmapple_cfg_reset(Object *obj, ResetType type)
{
    VMAppleCfgState *s = VMAPPLE_CFG(obj);
    VMAppleCfg *cfg;

    cfg = memory_region_get_ram_ptr(&s->mem);
    memset(cfg, 0, VMAPPLE_CFG_SIZE);
    *cfg = s->cfg;
}

static bool set_fixlen_property_or_error(char *restrict dst,
                                         const char *restrict src,
                                         size_t dst_size, Error **errp,
                                         const char *property_name)
{
    ERRP_GUARD();
    size_t len;

    len = g_strlcpy(dst, src, dst_size);
    if (len < dst_size) { /* len does not count nul terminator */
        return true;
    }

    error_setg(errp, "Provided value too long for property '%s'", property_name);
    error_append_hint(errp, "length (%zu) exceeds maximum of %zu\n",
                      len, dst_size - 1);
    return false;
}

#define set_fixlen_property_or_return(dst_array, src, errp, property_name) \
    do { \
        if (!set_fixlen_property_or_error((dst_array), (src), \
                                          ARRAY_SIZE(dst_array), \
                                          (errp), (property_name))) { \
            return; \
        } \
    } while (0)

static void vmapple_cfg_realize(DeviceState *dev, Error **errp)
{
    VMAppleCfgState *s = VMAPPLE_CFG(dev);
    uint32_t i;

    if (!s->serial) {
        s->serial = g_strdup("1234");
    }
    if (!s->model) {
        s->model = g_strdup("VM0001");
    }
    if (!s->soc_name) {
        s->soc_name = g_strdup("Apple M1 (Virtual)");
    }

    set_fixlen_property_or_return(s->cfg.serial, s->serial, errp, "serial");
    set_fixlen_property_or_return(s->cfg.model, s->model, errp, "model");
    set_fixlen_property_or_return(s->cfg.soc_name, s->soc_name, errp, "soc_name");
    set_fixlen_property_or_return(s->cfg.unk8, "D/A", errp, "unk8");
    s->cfg.version = 2;
    s->cfg.unk1 = 1;
    s->cfg.unk2 = 1;
    s->cfg.unk3 = 0x20;
    s->cfg.unk4 = 0;
    s->cfg.unk5 = 1;
    s->cfg.unk6 = 1;
    s->cfg.unk7 = 0;
    s->cfg.unk10 = 1;

    if (s->cfg.nr_cpus > ARRAY_SIZE(s->cfg.cpu_ids)) {
        error_setg(errp,
                   "Failed to create %u CPUs, vmapple machine supports %zu max",
                   s->cfg.nr_cpus, ARRAY_SIZE(s->cfg.cpu_ids));
        return;
    }
    for (i = 0; i < s->cfg.nr_cpus; i++) {
        s->cfg.cpu_ids[i] = i;
    }
}

static void vmapple_cfg_init(Object *obj)
{
    VMAppleCfgState *s = VMAPPLE_CFG(obj);

    memory_region_init_ram(&s->mem, obj, "VMApple Config", VMAPPLE_CFG_SIZE,
                           &error_fatal);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mem);
}

static const Property vmapple_cfg_properties[] = {
    DEFINE_PROP_UINT32("nr-cpus", VMAppleCfgState, cfg.nr_cpus, 1),
    DEFINE_PROP_UINT64("ecid", VMAppleCfgState, cfg.ecid, 0),
    DEFINE_PROP_UINT64("ram-size", VMAppleCfgState, cfg.ram_size, 0),
    DEFINE_PROP_UINT32("run_installer1", VMAppleCfgState, cfg.run_installer1, 0),
    DEFINE_PROP_UINT32("run_installer2", VMAppleCfgState, cfg.run_installer2, 0),
    DEFINE_PROP_UINT32("rnd", VMAppleCfgState, cfg.rnd, 0),
    DEFINE_PROP_MACADDR("mac-en0", VMAppleCfgState, cfg.mac_en0),
    DEFINE_PROP_MACADDR("mac-en1", VMAppleCfgState, cfg.mac_en1),
    DEFINE_PROP_MACADDR("mac-wifi0", VMAppleCfgState, cfg.mac_wifi0),
    DEFINE_PROP_MACADDR("mac-bt0", VMAppleCfgState, cfg.mac_bt0),
    DEFINE_PROP_STRING("serial", VMAppleCfgState, serial),
    DEFINE_PROP_STRING("model", VMAppleCfgState, model),
    DEFINE_PROP_STRING("soc_name", VMAppleCfgState, soc_name),
};

static void vmapple_cfg_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = vmapple_cfg_realize;
    dc->desc = "VMApple Configuration Region";
    device_class_set_props(dc, vmapple_cfg_properties);
    rc->phases.hold = vmapple_cfg_reset;
}

static const TypeInfo vmapple_cfg_info = {
    .name          = TYPE_VMAPPLE_CFG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VMAppleCfgState),
    .instance_init = vmapple_cfg_init,
    .class_init    = vmapple_cfg_class_init,
};

static void vmapple_cfg_register_types(void)
{
    type_register_static(&vmapple_cfg_info);
}

type_init(vmapple_cfg_register_types)
