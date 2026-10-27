/*
 * Qualcomm DSP subsystem machines: the board layer shared by the SoC models.
 *
 * Each SoC provides a QcomDspMachineInfo describing its subsystem, and the
 * layer supplies what every one of them has in common: the CPUs, a QEMU-only
 * PL011 console, virtio-mmio slots, a generated device tree, and the
 * -bios/-kernel/-initrd boot flow.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_HEXAGON_QCOM_DSP_H
#define HW_HEXAGON_QCOM_DSP_H

#include "hw/hexagon/hexagon.h"
#include "qom/object.h"

#define TYPE_QCOM_DSP_MACHINE MACHINE_TYPE_NAME("qcom-dsp")
OBJECT_DECLARE_TYPE(QcomDspMachineState, QcomDspMachineClass, QCOM_DSP_MACHINE)

/* Number of virtio-mmio transports every machine provides. */
#define QCOM_DSP_VIRTIO_COUNT 8

typedef struct QcomDspMachineInfo {
    const char *desc;
    const char *cpu_type;
    unsigned int cpus;
    Rev_t rev;
    const struct hexagon_machine_config *cfg;
    /* Where the H2 loadlinux of this SoC expects to find the Linux kernel. */
    hwaddr kernel_addr;
    /* Add the devices that are specific to this SoC; may be NULL. */
    void (*init_devices)(MachineState *ms);
} QcomDspMachineInfo;

struct QcomDspMachineState {
    HexagonCommonMachineState parent_obj;

    MemoryRegion tcm;
    MemoryRegion l2_ecc;
    int fdt_size;
    hwaddr fdt_addr;
    char *firmware_path;
};

struct QcomDspMachineClass {
    MachineClass parent_class;

    const QcomDspMachineInfo *info;
};

/* Describes a register window the machine stubs out as unimplemented. */
typedef struct QcomDspRegion {
    const char *name;
    hwaddr base;
    /* Zero means one 4 KiB page. */
    hwaddr size;
} QcomDspRegion;

/*
 * Stub out register windows the guest may probe, named
 * "<machine>.<name>@<base>".
 */
void qcom_dsp_add_regions(MachineState *ms, const QcomDspRegion *regions,
                          unsigned int count);

/* Create the sysbus device @type and map it at @base. */
void qcom_dsp_add_device(const char *type, hwaddr base);

/*
 * Stub out @ports consecutive QUP GENI serial engines of @size bytes.  They
 * belong to the SoC @soc rather than to the DSP.
 */
void qcom_soc_add_qup_geni_ports(const char *soc, hwaddr base,
                                 unsigned int ports, hwaddr size);

/* Class init for a machine type whose class_data is a QcomDspMachineInfo. */
void qcom_dsp_class_init(ObjectClass *oc, const void *data);

#endif /* HW_HEXAGON_QCOM_DSP_H */
