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
                              MemHotplugState *state, uint16_t io_base);

void acpi_memory_plug_cb(HotplugHandler *hotplug_dev, MemHotplugState *mem_st,
                         DeviceState *dev, Error **errp);
void acpi_memory_unplug_request_cb(HotplugHandler *hotplug_dev,
                                   MemHotplugState *mem_st,
                                   DeviceState *dev, Error **errp);
void acpi_memory_unplug_cb(MemHotplugState *mem_st,
                           DeviceState *dev, Error **errp);

extern const VMStateDescription vmstate_memory_hotplug;
#define VMSTATE_MEMORY_HOTPLUG(memhp, state) \
    VMSTATE_STRUCT(memhp, state, 1, \
                   vmstate_memory_hotplug, MemHotplugState)

void acpi_memory_ospm_status(MemHotplugState *mem_st, ACPIOSTInfoList ***list);

void build_memory_hotplug_aml(Aml *table, uint32_t nr_mem,
                              const char *res_root,
                              const char *event_handler_method);
#endif
