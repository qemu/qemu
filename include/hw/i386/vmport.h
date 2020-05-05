#ifndef HW_VMPORT_H
#define HW_VMPORT_H

#include "hw/isa/isa.h"

#define TYPE_VMPORT "vmport"
typedef uint32_t VMPortReadFunc(void *opaque, uint32_t address);

typedef enum {
    VMPORT_CMD_GETVERSION       = 10,
    VMPORT_CMD_GETBIOSUUID      = 19,
    VMPORT_CMD_GETRAMSIZE       = 20,
    VMPORT_CMD_VMMOUSE_DATA     = 39,
    VMPORT_CMD_VMMOUSE_STATUS   = 40,
    VMPORT_CMD_VMMOUSE_COMMAND  = 41,
    VMPORT_CMD_GETHZ            = 45,
    VMPORT_CMD_GET_VCPU_INFO    = 68,
    VMPORT_ENTRIES
} VMPortCommand;

static inline void vmport_init(ISABus *bus)
{
    isa_create_simple(bus, TYPE_VMPORT);
}

void vmport_register(VMPortCommand command, VMPortReadFunc *func, void *opaque);

#endif
