/*
 * QEMU Firmware configuration device emulation (QOM interfaces)
 *
 * Copyright 2020 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/nvram/fw_cfg.h"

static const TypeInfo fw_cfg_data_generator_interface_info = {
    .parent = TYPE_INTERFACE,
    .name = TYPE_FW_CFG_DATA_GENERATOR_INTERFACE,
    .class_size = sizeof(FWCfgDataGeneratorClass),
};

static void fw_cfg_register_interfaces(void)
{
    type_register_static(&fw_cfg_data_generator_interface_info);
}

type_init(fw_cfg_register_interfaces)
