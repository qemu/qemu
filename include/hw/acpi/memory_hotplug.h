#ifndef QEMU_HW_ACPI_MEMORY_HOTPLUG_H
#define QEMU_HW_ACPI_MEMORY_HOTPLUG_H

#include "hw/qdev-core.h"
#include "hw/acpi/acpi.h"
#include "migration/vmstate.h"
#include "hw/acpi/aml-build.h"

/**
 * MemStatus:
 * @is_removing: the memory device in slot has been requested to be ejected.
 *
 * This structure stores memory device's status.
 */
typedef struct MemStatus {
    DeviceState *dimm;
    bool is_enabled;
    bool is_inserting;
    bool is_removing;
    uint32_t ost_event;
    uint32_t ost_status;
} MemStatus;

typedef struct MemHotplugState {
    bool is_enabled; /* true if memory hotplug is supported */
    MemoryRegion io;
    uint32_t selector;
    uint32_t dev_count;
    MemStatus *devs;
} MemHotplugState;

void acpi_memory_hotplug_init(MemoryRegion *as, Object *owner,
                              MemHotplugState *state);

void acpi_memory_plug_cb(ACPIREGS *ar, qemu_irq irq, MemHotplugState *mem_st,
                         DeviceState *dev, Error **errp);
void acpi_memory_unplug_request_cb(ACPIREGS *ar, qemu_irq irq,
                                   MemHotplugState *mem_st,
                                   DeviceState *dev, Error **errp);
void acpi_memory_unplug_cb(MemHotplugState *mem_st,
                           DeviceState *dev, Error **errp);

extern const VMStateDescription vmstate_memory_hotplug;
#define VMSTATE_MEMORY_HOTPLUG(memhp, state) \
    VMSTATE_STRUCT(memhp, state, 1, \
                   vmstate_memory_hotplug, MemHotplugState)

void acpi_memory_ospm_status(MemHotplugState *mem_st, ACPIOSTInfoList ***list);

#define MEMORY_HOTPLUG_DEVICE        "MHPD"
#define MEMORY_SLOT_SCAN_METHOD      "MSCN"
#define MEMORY_HOTPLUG_HANDLER_PATH "\\_SB.PCI0." \
     MEMORY_HOTPLUG_DEVICE "." MEMORY_SLOT_SCAN_METHOD

void build_memory_hotplug_aml(Aml *ctx, uint32_t nr_mem,
                              uint16_t io_base, uint16_t io_len);
#endif
