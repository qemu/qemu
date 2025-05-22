#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/typedefs.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "hw/qdev-properties.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"
#include "standard-headers/linux/pci_regs.h"
#include "system/memory.h"
#include "system/hostmem.h"
#include <sys/types.h>

#define CXL_SWITCH_DEBUG 1
#define CXL_SWITCH_DPRINTF(fmt, ...)                       \
    do {                                                   \
        if (CXL_SWITCH_DEBUG) {                            \
            printf("CXL Switch: " fmt, ## __VA_ARGS__);    \
        }                                                  \
    } while (0)


#define TYPE_PCI_CXL_SWITCH "cxl-switch"

typedef struct CXLSwitchState CXLSwitchState;
DECLARE_INSTANCE_CHECKER(CXLSwitchState, CXL_SWITCH, TYPE_PCI_CXL_SWITCH)

#define PCI_VENDOR_ID_QEMU_CXL_SWITCH 0x1AF4
#define PCI_CXL_DEVICE_ID 0x1337

typedef enum {
    BACKEND_STATUS_HEALTHY = 0,
    BACKEND_STATUS_FAILED = 1,
} BackendHealthStatus;

/**
    TODO: We will need a way to keep track of active memory regions
          for allocation/deallocation.
          At the moment, we are just using a static array of 3 and concerned
          with getting the replication right.
*/

struct CXLSwitchState {
    PCIDevice pdev;

    /* The total replicated memory */
    uint64_t mem_size;
    MemoryRegion replicated_mr;  // BAR2: Guest access

    /* 
        The "emulated" CXL memory devices
        TODO: Support more than 3 memory devices in the future. 
    */
    HostMemoryBackend *backing_hmb[3];
    MemoryRegion *backing_mr[3];
    char *backing_mem_id[3]; // Initialized with 3 memdevs, identifies them
    BackendHealthStatus health_status[3];

    QemuMutex lock; // Protects backing_health which can be concurrently modified
};

static void pci_cxl_switch_register_types(void);
static void cxl_switch_instance_init(Object *obj);
static void cxl_switch_class_init(ObjectClass *class, const void *data);
static void pci_cxl_switch_realize(PCIDevice *pdev, Error **errp);
static void pci_cxl_switch_uninit(PCIDevice *pdev);

/* BAR2 Replicated Memory Operations */

static uint64_t cxl_switch_mem_read(void *opaque, hwaddr addr, unsigned size)
{
    CXLSwitchState *s = opaque;
    uint64_t data = ~0ULL; // Default error value (all FFs)
    
    int replica_to_read = -1;
    if ((addr + size) > s->mem_size) {
        CXL_SWITCH_DPRINTF("GuestError: Read out of bounds (offset=0x%"PRIx64", size=%u, limit=0x%"PRIx64")\n",
                      addr, size, s->mem_size);
        return data;
    }

    /* We lock here as multiple VMs could perform read ops concurrently */
    qemu_mutex_lock(&s->lock);

    /* Find a healthy backend */
    for (int i = 0; i < 3; i++) {
        if (s->health_status[i] == BACKEND_STATUS_HEALTHY) {
            replica_to_read = i;
            break;
        }
    }

    if (replica_to_read == -1) {
        CXL_SWITCH_DPRINTF("GuestError: No healthy backend found for read (offset=0x%"PRIx64", size=%u)\n",
                      addr, size);
        qemu_mutex_unlock(&s->lock);
        return data;
    }

    if (s->backing_mr[replica_to_read]) {
        uint8_t *ram_ptr = memory_region_get_ram_ptr(s->backing_mr[replica_to_read]);
        if (ram_ptr) {
            switch (size) {
                case 1: data = ldub_p(ram_ptr + addr); break;
                case 2: data = lduw_le_p(ram_ptr + addr); break;
                case 4: data = ldl_le_p(ram_ptr + addr); break;
                case 8: data = ldq_le_p(ram_ptr + addr); break;
                default:
                    CXL_SWITCH_DPRINTF("GuestError: Unsupported read size %u from replica %d at offset 0x%"PRIx64"\n",
                                  size, replica_to_read, addr);
                    data = ~0ULL;
                    break;
            }
        } else {
            /* TODO: This should not happen, but if it does, we should try again */
            CXL_SWITCH_DPRINTF("GuestError: Replica %d backing RAM not available for read at offset 0x%"PRIx64". Marking FAILED.\n",
                          replica_to_read, addr);
            s->health_status[replica_to_read] = BACKEND_STATUS_FAILED;
            data = ~0ULL;
        }
    }

    qemu_mutex_unlock(&s->lock);
    return data;
}

static void cxl_switch_mem_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    CXLSwitchState *s = opaque;
    int successful_writes = 0;
    int num_healthy_backends_attempted = 0;

    if ((addr + size) > s->mem_size) {
        CXL_SWITCH_DPRINTF("GuestError: Write out of bounds (offset=0x%"PRIx64", size=%u, limit=0x%"PRIx64")\n",
                      addr, size, s->mem_size);
        return;
    }

    qemu_mutex_lock(&s->lock);

    for (int i = 0; i < 3; i++) {
        if (s->health_status[i] != BACKEND_STATUS_HEALTHY) {
            continue;
        }
        num_healthy_backends_attempted++;

        if (s->backing_mr[i]) {
            uint8_t *ram_ptr = memory_region_get_ram_ptr(s->backing_mr[i]);
            if (ram_ptr) {
                bool write_ok = true;
                switch (size) {
                    case 1: stl_p(ram_ptr + addr, (uint8_t) val); break;
                    case 2: stw_le_p(ram_ptr + addr, (uint16_t) val); break;
                    case 4: stl_le_p(ram_ptr + addr, (uint32_t) val); break;
                    case 8: stq_le_p(ram_ptr + addr, val); break;
                    default:
                        CXL_SWITCH_DPRINTF("GuestError: Unsupported write size %u to replica %d at offset 0x%"PRIx64"\n",
                                      size, i, addr);
                        write_ok = false;
                        break;
                }
                if (write_ok) {
                    successful_writes++;
                }
            } else {
                CXL_SWITCH_DPRINTF("GuestError: Replica %d backing RAM not available for write at offset 0x%"PRIx64". Marking FAILED.\n",
                              i, addr);
                s->health_status[i] = BACKEND_STATUS_FAILED;
            }
        } else {
            CXL_SWITCH_DPRINTF("GuestError: Replica %d backing memory region not available for write at offset 0x%"PRIx64"\n",
                          i, addr);
            s->health_status[i] = BACKEND_STATUS_FAILED;
        }
    }

    qemu_mutex_unlock(&s->lock);

    if (num_healthy_backends_attempted > 0 && successful_writes < num_healthy_backends_attempted) {
        CXL_SWITCH_DPRINTF("GuestError: Write to offset 0x%"PRIx64" succeeded on %d/%d healthy backends.\n",
                      addr, successful_writes, num_healthy_backends_attempted);
    } else if (num_healthy_backends_attempted == 0 && s->mem_size > 0) {
         CXL_SWITCH_DPRINTF("GuestError: Write to offset 0x%"PRIx64" failed: No healthy backends available.\n",
                      addr);
    }
}

static const MemoryRegionOps cxl_switch_mem_ops = {
    .read = cxl_switch_mem_read,
    .write = cxl_switch_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

/* PCI Device Lifecycle */

static void pci_cxl_switch_realize(PCIDevice *pdev, Error **errp)
{
    CXLSwitchState *s = CXL_SWITCH(pdev);
    CXL_SWITCH_DPRINTF("Info: Realizing device.\n"); // Added device ID manually

    if (s->mem_size == 0) {
        error_setg(errp, "CXL Switch (%s): mem-size property must be set", object_get_canonical_path_component(OBJECT(s)));
        return;
    }

    qemu_mutex_init(&s->lock);

    /* Init all backing stores to healthy */
    for (int i = 0; i < 3; i++) {
        s->health_status[i] = BACKEND_STATUS_HEALTHY;
        if (!s->backing_mem_id[i] || strlen(s->backing_mem_id[i]) == 0) {
            error_setg(errp, "CXL Switch (%s): memdev%d property must be set for backend %d", object_get_canonical_path_component(OBJECT(s)), i, i);
            return;
        }
        bool ambiguous;
        Object *obj = object_resolve_path(s->backing_mem_id[i], &ambiguous);
        // TODO: handle ambiguous case
        if (!obj) {
            error_setg(errp, "CXL Switch (%s): Unable to find HostMemoryBackend '%s' for backend %d", object_get_canonical_path_component(OBJECT(s)), s->backing_mem_id[i], i);
            return;
        }
        s->backing_hmb[i] = MEMORY_BACKEND(obj);
        if (!s->backing_hmb[i]) {
            error_setg(errp, "CXL Switch (%s): Object '%s' is not a HostMemoryBackend for backend %d", object_get_canonical_path_component(OBJECT(s)), s->backing_mem_id[i], i);
            return;
        }
        if (host_memory_backend_is_mapped(s->backing_hmb[i])) {
            error_setg(errp, "CXL Switch (%s): HostMemoryBackend '%s' for backend %d is already in use.", object_get_canonical_path_component(OBJECT(s)), s->backing_mem_id[i], i);
            return;
        }
        s->backing_mr[i] = host_memory_backend_get_memory(s->backing_hmb[i]);
        if (!s->backing_mr[i] || memory_region_size(s->backing_mr[i]) < s->mem_size) {
            error_setg(errp, "CXL Switch (%s): Backend %d ('%s') memory region is too small or invalid (size: %"PRIu64", required: %"PRIu64")", 
                object_get_canonical_path_component(OBJECT(s)), i, s->backing_mem_id[i], s->backing_mr[i] ? memory_region_size(s->backing_mr[i]) : 0, s->mem_size);
            return;
        }
        host_memory_backend_set_mapped(s->backing_hmb[i], true);
        CXL_SWITCH_DPRINTF("Info: Backend %d ('%s') initialized, size %"PRIu64".\n",
                    i, s->backing_mem_id[i], memory_region_size(s->backing_mr[i]));
    }

    // BAR2: replicated memory pool
    memory_region_init_io(&s->replicated_mr, OBJECT(s), &cxl_switch_mem_ops, s, "cxl-switch-replicated-mem", s->mem_size);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64 | PCI_BASE_ADDRESS_MEM_PREFETCH, &s->replicated_mr);
    CXL_SWITCH_DPRINTF("Info: BAR0 (effectively BAR2) registered for replication, size %"PRIu64".\n", s->mem_size);

    // TODO: No BAR0 for MMIO commands here
    // TODO: No chardev here as well
}

static void pci_cxl_switch_uninit(PCIDevice *pdev)
{
    CXLSwitchState *s = CXL_SWITCH(pdev);
    CXL_SWITCH_DPRINTF("Info: Uninitializing device.\n");

    for (int i = 0; i < 3; i++) {
        if (s->backing_hmb[i] && host_memory_backend_is_mapped(s->backing_hmb[i])) {
            host_memory_backend_set_mapped(s->backing_hmb[i], false);
        }
    }

    qemu_mutex_destroy(&s->lock);
}

/** QOM Type Registration */
static Property cxl_switch_properties[] = {
    DEFINE_PROP_SIZE("mem-size", CXLSwitchState, mem_size, 0),
    DEFINE_PROP_STRING("memdev0", CXLSwitchState, backing_mem_id[0]),
    DEFINE_PROP_STRING("memdev1", CXLSwitchState, backing_mem_id[1]),
    DEFINE_PROP_STRING("memdev2", CXLSwitchState, backing_mem_id[2]),
};

static void cxl_switch_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_cxl_switch_realize;
    k->exit = pci_cxl_switch_uninit;
    k->vendor_id = PCI_VENDOR_ID_QEMU_CXL_SWITCH;
    k->device_id = PCI_CXL_DEVICE_ID;
    k->class_id = PCI_CLASS_MEMORY_RAM;
    k->revision = 1;

    device_class_set_props(dc, cxl_switch_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "CXL Switch";
}

static void cxl_switch_instance_init(Object *obj)
{
    CXLSwitchState *s = CXL_SWITCH(obj);
    // TODO: Let user specify this
    s->mem_size = 128 * MiB; // Default size
    for (int i = 0; i < 3; i++) {
        s->backing_mem_id[i] = NULL;
    }
}

static InterfaceInfo interfaces[] = {
    { INTERFACE_CONVENTIONAL_PCI_DEVICE },
    { },
};

static const TypeInfo cxl_switch_info = {
    .name = TYPE_PCI_CXL_SWITCH,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(CXLSwitchState),
    .instance_init = cxl_switch_instance_init,
    .class_init = cxl_switch_class_init,
    .interfaces = interfaces,
};

static void pci_cxl_switch_register_types(void)
{
    type_register_static(&cxl_switch_info);
}

type_init(pci_cxl_switch_register_types);