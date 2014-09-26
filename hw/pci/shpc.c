#include "qemu-common.h"
#include <strings.h>
#include <stdint.h>
#include "qemu/range.h"
#include "qemu/error-report.h"
#include "hw/pci/shpc.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/msi.h"
#include "qapi/qmp/qerror.h"

/* TODO: model power only and disabled slot states. */
/* TODO: handle SERR and wakeups */
/* TODO: consider enabling 66MHz support */

/* TODO: remove fully only on state DISABLED and LED off.
 * track state to properly record this. */

/* SHPC Working Register Set */
#define SHPC_BASE_OFFSET  0x00 /* 4 bytes */
#define SHPC_SLOTS_33     0x04 /* 4 bytes. Also encodes PCI-X slots. */
#define SHPC_SLOTS_66     0x08 /* 4 bytes. */
#define SHPC_NSLOTS       0x0C /* 1 byte */
#define SHPC_FIRST_DEV    0x0D /* 1 byte */
#define SHPC_PHYS_SLOT    0x0E /* 2 byte */
#define SHPC_PHYS_NUM_MAX 0x7ff
#define SHPC_PHYS_NUM_UP  0x2000
#define SHPC_PHYS_MRL     0x4000
#define SHPC_PHYS_BUTTON  0x8000
#define SHPC_SEC_BUS      0x10 /* 2 bytes */
#define SHPC_SEC_BUS_33   0x0
#define SHPC_SEC_BUS_66   0x1 /* Unused */
#define SHPC_SEC_BUS_MASK 0x7
#define SHPC_MSI_CTL      0x12 /* 1 byte */
#define SHPC_PROG_IFC     0x13 /* 1 byte */
#define SHPC_PROG_IFC_1_0 0x1
#define SHPC_CMD_CODE     0x14 /* 1 byte */
#define SHPC_CMD_TRGT     0x15 /* 1 byte */
#define SHPC_CMD_TRGT_MIN 0x1
#define SHPC_CMD_TRGT_MAX 0x1f
#define SHPC_CMD_STATUS   0x16 /* 2 bytes */
#define SHPC_CMD_STATUS_BUSY          0x1
#define SHPC_CMD_STATUS_MRL_OPEN      0x2
#define SHPC_CMD_STATUS_INVALID_CMD   0x4
#define SHPC_CMD_STATUS_INVALID_MODE  0x8
#define SHPC_INT_LOCATOR  0x18 /* 4 bytes */
#define SHPC_INT_COMMAND  0x1
#define SHPC_SERR_LOCATOR 0x1C /* 4 bytes */
#define SHPC_SERR_INT     0x20 /* 4 bytes */
#define SHPC_INT_DIS      0x1
#define SHPC_SERR_DIS     0x2
#define SHPC_CMD_INT_DIS  0x4
#define SHPC_ARB_SERR_DIS 0x8
#define SHPC_CMD_DETECTED 0x10000
#define SHPC_ARB_DETECTED 0x20000
 /* 4 bytes * slot # (start from 0) */
#define SHPC_SLOT_REG(s)         (0x24 + (s) * 4)
 /* 2 bytes */
#define SHPC_SLOT_STATUS(s)       (0x0 + SHPC_SLOT_REG(s))

/* Same slot state masks are used for command and status registers */
#define SHPC_SLOT_STATE_MASK     0x03
#define SHPC_SLOT_STATE_SHIFT \
    (ffs(SHPC_SLOT_STATE_MASK) - 1)

#define SHPC_STATE_NO       0x0
#define SHPC_STATE_PWRONLY  0x1
#define SHPC_STATE_ENABLED  0x2
#define SHPC_STATE_DISABLED 0x3

#define SHPC_SLOT_PWR_LED_MASK   0xC
#define SHPC_SLOT_PWR_LED_SHIFT \
    (ffs(SHPC_SLOT_PWR_LED_MASK) - 1)
#define SHPC_SLOT_ATTN_LED_MASK  0x30
#define SHPC_SLOT_ATTN_LED_SHIFT \
    (ffs(SHPC_SLOT_ATTN_LED_MASK) - 1)

#define SHPC_LED_NO     0x0
#define SHPC_LED_ON     0x1
#define SHPC_LED_BLINK  0x2
#define SHPC_LED_OFF    0x3

#define SHPC_SLOT_STATUS_PWR_FAULT      0x40
#define SHPC_SLOT_STATUS_BUTTON         0x80
#define SHPC_SLOT_STATUS_MRL_OPEN       0x100
#define SHPC_SLOT_STATUS_66             0x200
#define SHPC_SLOT_STATUS_PRSNT_MASK     0xC00
#define SHPC_SLOT_STATUS_PRSNT_EMPTY    0x3
#define SHPC_SLOT_STATUS_PRSNT_25W      0x1
#define SHPC_SLOT_STATUS_PRSNT_15W      0x2
#define SHPC_SLOT_STATUS_PRSNT_7_5W     0x0

#define SHPC_SLOT_STATUS_PRSNT_PCIX     0x3000


 /* 1 byte */
#define SHPC_SLOT_EVENT_LATCH(s)        (0x2 + SHPC_SLOT_REG(s))
 /* 1 byte */
#define SHPC_SLOT_EVENT_SERR_INT_DIS(d, s) (0x3 + SHPC_SLOT_REG(s))
#define SHPC_SLOT_EVENT_PRESENCE        0x01
#define SHPC_SLOT_EVENT_ISOLATED_FAULT  0x02
#define SHPC_SLOT_EVENT_BUTTON          0x04
#define SHPC_SLOT_EVENT_MRL             0x08
#define SHPC_SLOT_EVENT_CONNECTED_FAULT 0x10
/* Bits below are used for Serr/Int disable only */
#define SHPC_SLOT_EVENT_MRL_SERR_DIS    0x20
#define SHPC_SLOT_EVENT_CONNECTED_FAULT_SERR_DIS 0x40

#define SHPC_MIN_SLOTS        1
#define SHPC_MAX_SLOTS        31
#define SHPC_SIZEOF(d)    SHPC_SLOT_REG((d)->shpc->nslots)

/* SHPC Slot identifiers */

/* Hotplug supported at 31 slots out of the total 32.  We reserve slot 0,
   and give the rest of them physical *and* pci numbers starting from 1, so
   they match logical numbers.  Note: this means that multiple slots must have
   different chassis number values, to make chassis+physical slot unique.
   TODO: make this configurable? */
#define SHPC_IDX_TO_LOGICAL(slot) ((slot) + 1)
#define SHPC_LOGICAL_TO_IDX(target) ((target) - 1)
#define SHPC_IDX_TO_PCI(slot) ((slot) + 1)
#define SHPC_PCI_TO_IDX(pci_slot) ((pci_slot) - 1)
#define SHPC_IDX_TO_PHYSICAL(slot) ((slot) + 1)

static int roundup_pow_of_two(int x)
{
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    return x + 1;
}

static uint16_t shpc_get_status(SHPCDevice *shpc, int slot, uint16_t msk)
{
    uint8_t *status = shpc->config + SHPC_SLOT_STATUS(slot);
    return (pci_get_word(status) & msk) >> (ffs(msk) - 1);
}

static void shpc_set_status(SHPCDevice *shpc,
                            int slot, uint8_t value, uint16_t msk)
{
    uint8_t *status = shpc->config + SHPC_SLOT_STATUS(slot);
    pci_word_test_and_clear_mask(status, msk);
    pci_word_test_and_set_mask(status, value << (ffs(msk) - 1));
}

static void shpc_interrupt_update(PCIDevice *d)
{
    SHPCDevice *shpc = d->shpc;
    int slot;
    int level = 0;
    uint32_t serr_int;
    uint32_t int_locator = 0;

    /* Update interrupt locator register */
    for (slot = 0; slot < shpc->nslots; ++slot) {
        uint8_t event = shpc->config[SHPC_SLOT_EVENT_LATCH(slot)];
        uint8_t disable = shpc->config[SHPC_SLOT_EVENT_SERR_INT_DIS(d, slot)];
        uint32_t mask = 1 << SHPC_IDX_TO_LOGICAL(slot);
        if (event & ~disable) {
            int_locator |= mask;
        }
    }
    serr_int = pci_get_long(shpc->config + SHPC_SERR_INT);
    if ((serr_int & SHPC_CMD_DETECTED) && !(serr_int & SHPC_CMD_INT_DIS)) {
        int_locator |= SHPC_INT_COMMAND;
    }
    pci_set_long(shpc->config + SHPC_INT_LOCATOR, int_locator);
    level = (!(serr_int & SHPC_INT_DIS) && int_locator) ? 1 : 0;
    if (msi_enabled(d) && shpc->msi_requested != level)
        msi_notify(d, 0);
    else
        pci_set_irq(d, level);
    shpc->msi_requested = level;
}

static void shpc_set_sec_bus_speed(SHPCDevice *shpc, uint8_t speed)
{
    switch (speed) {
    case SHPC_SEC_BUS_33:
        shpc->config[SHPC_SEC_BUS] &= ~SHPC_SEC_BUS_MASK;
        shpc->config[SHPC_SEC_BUS] |= speed;
        break;
    default:
        pci_word_test_and_set_mask(shpc->config + SHPC_CMD_STATUS,
                                   SHPC_CMD_STATUS_INVALID_MODE);
    }
}

void shpc_reset(PCIDevice *d)
{
    SHPCDevice *shpc = d->shpc;
    int nslots = shpc->nslots;
    int i;
    memset(shpc->config, 0, SHPC_SIZEOF(d));
    pci_set_byte(shpc->config + SHPC_NSLOTS, nslots);
    pci_set_long(shpc->config + SHPC_SLOTS_33, nslots);
    pci_set_long(shpc->config + SHPC_SLOTS_66, 0);
    pci_set_byte(shpc->config + SHPC_FIRST_DEV, SHPC_IDX_TO_PCI(0));
    pci_set_word(shpc->config + SHPC_PHYS_SLOT,
                 SHPC_IDX_TO_PHYSICAL(0) |
                 SHPC_PHYS_NUM_UP |
                 SHPC_PHYS_MRL |
                 SHPC_PHYS_BUTTON);
    pci_set_long(shpc->config + SHPC_SERR_INT, SHPC_INT_DIS |
                 SHPC_SERR_DIS |
                 SHPC_CMD_INT_DIS |
                 SHPC_ARB_SERR_DIS);
    pci_set_byte(shpc->config + SHPC_PROG_IFC, SHPC_PROG_IFC_1_0);
    pci_set_word(shpc->config + SHPC_SEC_BUS, SHPC_SEC_BUS_33);
    for (i = 0; i < shpc->nslots; ++i) {
        pci_set_byte(shpc->config + SHPC_SLOT_EVENT_SERR_INT_DIS(d, i),
                     SHPC_SLOT_EVENT_PRESENCE |
                     SHPC_SLOT_EVENT_ISOLATED_FAULT |
                     SHPC_SLOT_EVENT_BUTTON |
                     SHPC_SLOT_EVENT_MRL |
                     SHPC_SLOT_EVENT_CONNECTED_FAULT |
                     SHPC_SLOT_EVENT_MRL_SERR_DIS |
                     SHPC_SLOT_EVENT_CONNECTED_FAULT_SERR_DIS);
        if (shpc->sec_bus->devices[PCI_DEVFN(SHPC_IDX_TO_PCI(i), 0)]) {
            shpc_set_status(shpc, i, SHPC_STATE_ENABLED, SHPC_SLOT_STATE_MASK);
            shpc_set_status(shpc, i, 0, SHPC_SLOT_STATUS_MRL_OPEN);
            shpc_set_status(shpc, i, SHPC_SLOT_STATUS_PRSNT_7_5W,
                            SHPC_SLOT_STATUS_PRSNT_MASK);
            shpc_set_status(shpc, i, SHPC_LED_ON, SHPC_SLOT_PWR_LED_MASK);
        } else {
            shpc_set_status(shpc, i, SHPC_STATE_DISABLED, SHPC_SLOT_STATE_MASK);
            shpc_set_status(shpc, i, 1, SHPC_SLOT_STATUS_MRL_OPEN);
            shpc_set_status(shpc, i, SHPC_SLOT_STATUS_PRSNT_EMPTY,
                            SHPC_SLOT_STATUS_PRSNT_MASK);
            shpc_set_status(shpc, i, SHPC_LED_OFF, SHPC_SLOT_PWR_LED_MASK);
        }
        shpc_set_status(shpc, i, 0, SHPC_SLOT_STATUS_66);
    }
    shpc_set_sec_bus_speed(shpc, SHPC_SEC_BUS_33);
    shpc->msi_requested = 0;
    shpc_interrupt_update(d);
}

static void shpc_invalid_command(SHPCDevice *shpc)
{
    pci_word_test_and_set_mask(shpc->config + SHPC_CMD_STATUS,
                               SHPC_CMD_STATUS_INVALID_CMD);
}

static void shpc_free_devices_in_slot(SHPCDevice *shpc, int slot)
{
    int devfn;
    int pci_slot = SHPC_IDX_TO_PCI(slot);
    for (devfn = PCI_DEVFN(pci_slot, 0);
         devfn <= PCI_DEVFN(pci_slot, PCI_FUNC_MAX - 1);
         ++devfn) {
        PCIDevice *affected_dev = shpc->sec_bus->devices[devfn];
        if (affected_dev) {
            object_unparent(OBJECT(affected_dev));
        }
    }
}

static void shpc_slot_command(SHPCDevice *shpc, uint8_t target,
                              uint8_t state, uint8_t power, uint8_t attn)
{
    uint8_t current_state;
    int slot = SHPC_LOGICAL_TO_IDX(target);
    if (target < SHPC_CMD_TRGT_MIN || slot >= shpc->nslots) {
        shpc_invalid_command(shpc);
        return;
    }
    current_state = shpc_get_status(shpc, slot, SHPC_SLOT_STATE_MASK);
    if (current_state == SHPC_STATE_ENABLED && state == SHPC_STATE_PWRONLY) {
        shpc_invalid_command(shpc);
        return;
    }

    switch (power) {
    case SHPC_LED_NO:
        break;
    default:
        /* TODO: send event to monitor */
        shpc_set_status(shpc, slot, power, SHPC_SLOT_PWR_LED_MASK);
    }
    switch (attn) {
    case SHPC_LED_NO:
        break;
    default:
        /* TODO: send event to monitor */
        shpc_set_status(shpc, slot, attn, SHPC_SLOT_ATTN_LED_MASK);
    }

    if ((current_state == SHPC_STATE_DISABLED && state == SHPC_STATE_PWRONLY) ||
        (current_state == SHPC_STATE_DISABLED && state == SHPC_STATE_ENABLED)) {
        shpc_set_status(shpc, slot, state, SHPC_SLOT_STATE_MASK);
    } else if ((current_state == SHPC_STATE_ENABLED ||
                current_state == SHPC_STATE_PWRONLY) &&
               state == SHPC_STATE_DISABLED) {
        shpc_set_status(shpc, slot, state, SHPC_SLOT_STATE_MASK);
        power = shpc_get_status(shpc, slot, SHPC_SLOT_PWR_LED_MASK);
        /* TODO: track what monitor requested. */
        /* Look at LED to figure out whether it's ok to remove the device. */
        if (power == SHPC_LED_OFF) {
            shpc_free_devices_in_slot(shpc, slot);
            shpc_set_status(shpc, slot, 1, SHPC_SLOT_STATUS_MRL_OPEN);
            shpc_set_status(shpc, slot, SHPC_SLOT_STATUS_PRSNT_EMPTY,
                            SHPC_SLOT_STATUS_PRSNT_MASK);
            shpc->config[SHPC_SLOT_EVENT_LATCH(slot)] |=
                SHPC_SLOT_EVENT_BUTTON |
                SHPC_SLOT_EVENT_MRL |
                SHPC_SLOT_EVENT_PRESENCE;
        }
    }
}

static void shpc_command(SHPCDevice *shpc)
{
    uint8_t code = pci_get_byte(shpc->config + SHPC_CMD_CODE);
    uint8_t speed;
    uint8_t target;
    uint8_t attn;
    uint8_t power;
    uint8_t state;
    int i;

    /* Clear status from the previous command. */
    pci_word_test_and_clear_mask(shpc->config + SHPC_CMD_STATUS,
                                 SHPC_CMD_STATUS_BUSY |
                                 SHPC_CMD_STATUS_MRL_OPEN |
                                 SHPC_CMD_STATUS_INVALID_CMD |
                                 SHPC_CMD_STATUS_INVALID_MODE);
    switch (code) {
    case 0x00 ... 0x3f:
        target = shpc->config[SHPC_CMD_TRGT] & SHPC_CMD_TRGT_MAX;
        state = (code & SHPC_SLOT_STATE_MASK) >> SHPC_SLOT_STATE_SHIFT;
        power = (code & SHPC_SLOT_PWR_LED_MASK) >> SHPC_SLOT_PWR_LED_SHIFT;
        attn = (code & SHPC_SLOT_ATTN_LED_MASK) >> SHPC_SLOT_ATTN_LED_SHIFT;
        shpc_slot_command(shpc, target, state, power, attn);
        break;
    case 0x40 ... 0x47:
        speed = code & SHPC_SEC_BUS_MASK;
        shpc_set_sec_bus_speed(shpc, speed);
        break;
    case 0x48:
        /* Power only all slots */
        /* first verify no slots are enabled */
        for (i = 0; i < shpc->nslots; ++i) {
            state = shpc_get_status(shpc, i, SHPC_SLOT_STATE_MASK);
            if (state == SHPC_STATE_ENABLED) {
                shpc_invalid_command(shpc);
                goto done;
            }
        }
        for (i = 0; i < shpc->nslots; ++i) {
            if (!(shpc_get_status(shpc, i, SHPC_SLOT_STATUS_MRL_OPEN))) {
                shpc_slot_command(shpc, i + SHPC_CMD_TRGT_MIN,
                                  SHPC_STATE_PWRONLY, SHPC_LED_ON, SHPC_LED_NO);
            } else {
                shpc_slot_command(shpc, i + SHPC_CMD_TRGT_MIN,
                                  SHPC_STATE_NO, SHPC_LED_OFF, SHPC_LED_NO);
            }
        }
        break;
    case 0x49:
        /* Enable all slots */
        /* TODO: Spec says this shall fail if some are already enabled.
         * This doesn't make sense - why not? a spec bug? */
        for (i = 0; i < shpc->nslots; ++i) {
            state = shpc_get_status(shpc, i, SHPC_SLOT_STATE_MASK);
            if (state == SHPC_STATE_ENABLED) {
                shpc_invalid_command(shpc);
                goto done;
            }
        }
        for (i = 0; i < shpc->nslots; ++i) {
            if (!(shpc_get_status(shpc, i, SHPC_SLOT_STATUS_MRL_OPEN))) {
                shpc_slot_command(shpc, i + SHPC_CMD_TRGT_MIN,
                                  SHPC_STATE_ENABLED, SHPC_LED_ON, SHPC_LED_NO);
            } else {
                shpc_slot_command(shpc, i + SHPC_CMD_TRGT_MIN,
                                  SHPC_STATE_NO, SHPC_LED_OFF, SHPC_LED_NO);
            }
        }
        break;
    default:
        shpc_invalid_command(shpc);
        break;
    }
done:
    pci_long_test_and_set_mask(shpc->config + SHPC_SERR_INT, SHPC_CMD_DETECTED);
}

static void shpc_write(PCIDevice *d, unsigned addr, uint64_t val, int l)
{
    SHPCDevice *shpc = d->shpc;
    int i;
    if (addr >= SHPC_SIZEOF(d)) {
        return;
    }
    l = MIN(l, SHPC_SIZEOF(d) - addr);

    /* TODO: code duplicated from pci.c */
    for (i = 0; i < l; val >>= 8, ++i) {
        unsigned a = addr + i;
        uint8_t wmask = shpc->wmask[a];
        uint8_t w1cmask = shpc->w1cmask[a];
        assert(!(wmask & w1cmask));
        shpc->config[a] = (shpc->config[a] & ~wmask) | (val & wmask);
        shpc->config[a] &= ~(val & w1cmask); /* W1C: Write 1 to Clear */
    }
    if (ranges_overlap(addr, l, SHPC_CMD_CODE, 2)) {
        shpc_command(shpc);
    }
    shpc_interrupt_update(d);
}

static uint64_t shpc_read(PCIDevice *d, unsigned addr, int l)
{
    uint64_t val = 0x0;
    if (addr >= SHPC_SIZEOF(d)) {
        return val;
    }
    l = MIN(l, SHPC_SIZEOF(d) - addr);
    memcpy(&val, d->shpc->config + addr, l);
    return val;
}

/* SHPC Bridge Capability */
#define SHPC_CAP_LENGTH 0x08
#define SHPC_CAP_DWORD_SELECT 0x2 /* 1 byte */
#define SHPC_CAP_CxP 0x3 /* 1 byte: CSP, CIP */
#define SHPC_CAP_DWORD_DATA 0x4 /* 4 bytes */
#define SHPC_CAP_CSP_MASK 0x4
#define SHPC_CAP_CIP_MASK 0x8

static uint8_t shpc_cap_dword(PCIDevice *d)
{
    return pci_get_byte(d->config + d->shpc->cap + SHPC_CAP_DWORD_SELECT);
}

/* Update dword data capability register */
static void shpc_cap_update_dword(PCIDevice *d)
{
    unsigned data;
    data = shpc_read(d, shpc_cap_dword(d) * 4, 4);
    pci_set_long(d->config  + d->shpc->cap + SHPC_CAP_DWORD_DATA, data);
}

/* Add SHPC capability to the config space for the device. */
static int shpc_cap_add_config(PCIDevice *d)
{
    uint8_t *config;
    int config_offset;
    config_offset = pci_add_capability(d, PCI_CAP_ID_SHPC,
                                       0, SHPC_CAP_LENGTH);
    if (config_offset < 0) {
        return config_offset;
    }
    config = d->config + config_offset;

    pci_set_byte(config + SHPC_CAP_DWORD_SELECT, 0);
    pci_set_byte(config + SHPC_CAP_CxP, 0);
    pci_set_long(config + SHPC_CAP_DWORD_DATA, 0);
    d->shpc->cap = config_offset;
    /* Make dword select and data writeable. */
    pci_set_byte(d->wmask + config_offset + SHPC_CAP_DWORD_SELECT, 0xff);
    pci_set_long(d->wmask + config_offset + SHPC_CAP_DWORD_DATA, 0xffffffff);
    return 0;
}

static uint64_t shpc_mmio_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    return shpc_read(opaque, addr, size);
}

static void shpc_mmio_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    shpc_write(opaque, addr, val, size);
}

static const MemoryRegionOps shpc_mmio_ops = {
    .read = shpc_mmio_read,
    .write = shpc_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        /* SHPC ECN requires dword accesses, but the original 1.0 spec doesn't.
         * It's easier to suppport all sizes than worry about it. */
        .min_access_size = 1,
        .max_access_size = 4,
    },
};
static void shpc_device_hotplug_common(PCIDevice *affected_dev, int *slot,
                                       SHPCDevice *shpc, Error **errp)
{
    int pci_slot = PCI_SLOT(affected_dev->devfn);
    *slot = SHPC_PCI_TO_IDX(pci_slot);

    if (pci_slot < SHPC_IDX_TO_PCI(0) || *slot >= shpc->nslots) {
        error_setg(errp, "Unsupported PCI slot %d for standard hotplug "
                   "controller. Valid slots are between %d and %d.",
                   pci_slot, SHPC_IDX_TO_PCI(0),
                   SHPC_IDX_TO_PCI(shpc->nslots) - 1);
        return;
    }
}

void shpc_device_hotplug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                            Error **errp)
{
    Error *local_err = NULL;
    PCIDevice *pci_hotplug_dev = PCI_DEVICE(hotplug_dev);
    SHPCDevice *shpc = pci_hotplug_dev->shpc;
    int slot;

    shpc_device_hotplug_common(PCI_DEVICE(dev), &slot, shpc, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Don't send event when device is enabled during qemu machine creation:
     * it is present on boot, no hotplug event is necessary. We do send an
     * event when the device is disabled later. */
    if (!dev->hotplugged) {
        shpc_set_status(shpc, slot, 0, SHPC_SLOT_STATUS_MRL_OPEN);
        shpc_set_status(shpc, slot, SHPC_SLOT_STATUS_PRSNT_7_5W,
                        SHPC_SLOT_STATUS_PRSNT_MASK);
        return;
    }

    /* This could be a cancellation of the previous removal.
     * We check MRL state to figure out. */
    if (shpc_get_status(shpc, slot, SHPC_SLOT_STATUS_MRL_OPEN)) {
        shpc_set_status(shpc, slot, 0, SHPC_SLOT_STATUS_MRL_OPEN);
        shpc_set_status(shpc, slot, SHPC_SLOT_STATUS_PRSNT_7_5W,
                        SHPC_SLOT_STATUS_PRSNT_MASK);
        shpc->config[SHPC_SLOT_EVENT_LATCH(slot)] |=
            SHPC_SLOT_EVENT_BUTTON |
            SHPC_SLOT_EVENT_MRL |
            SHPC_SLOT_EVENT_PRESENCE;
    } else {
        /* Press attention button to cancel removal */
        shpc->config[SHPC_SLOT_EVENT_LATCH(slot)] |=
            SHPC_SLOT_EVENT_BUTTON;
    }
    shpc_set_status(shpc, slot, 0, SHPC_SLOT_STATUS_66);
    shpc_interrupt_update(pci_hotplug_dev);
}

void shpc_device_hot_unplug_request_cb(HotplugHandler *hotplug_dev,
                                       DeviceState *dev, Error **errp)
{
    Error *local_err = NULL;
    PCIDevice *pci_hotplug_dev = PCI_DEVICE(hotplug_dev);
    SHPCDevice *shpc = pci_hotplug_dev->shpc;
    uint8_t state;
    uint8_t led;
    int slot;

    shpc_device_hotplug_common(PCI_DEVICE(dev), &slot, shpc, errp);
    if (local_err) {
        return;
    }

    shpc->config[SHPC_SLOT_EVENT_LATCH(slot)] |= SHPC_SLOT_EVENT_BUTTON;
    state = shpc_get_status(shpc, slot, SHPC_SLOT_STATE_MASK);
    led = shpc_get_status(shpc, slot, SHPC_SLOT_PWR_LED_MASK);
    if (state == SHPC_STATE_DISABLED && led == SHPC_LED_OFF) {
        shpc_free_devices_in_slot(shpc, slot);
        shpc_set_status(shpc, slot, 1, SHPC_SLOT_STATUS_MRL_OPEN);
        shpc_set_status(shpc, slot, SHPC_SLOT_STATUS_PRSNT_EMPTY,
                        SHPC_SLOT_STATUS_PRSNT_MASK);
        shpc->config[SHPC_SLOT_EVENT_LATCH(slot)] |=
            SHPC_SLOT_EVENT_MRL |
            SHPC_SLOT_EVENT_PRESENCE;
    }
    shpc_set_status(shpc, slot, 0, SHPC_SLOT_STATUS_66);
    shpc_interrupt_update(pci_hotplug_dev);
}

/* Initialize the SHPC structure in bridge's BAR. */
int shpc_init(PCIDevice *d, PCIBus *sec_bus, MemoryRegion *bar, unsigned offset)
{
    int i, ret;
    int nslots = SHPC_MAX_SLOTS; /* TODO: qdev property? */
    SHPCDevice *shpc = d->shpc = g_malloc0(sizeof(*d->shpc));
    shpc->sec_bus = sec_bus;
    ret = shpc_cap_add_config(d);
    if (ret) {
        g_free(d->shpc);
        return ret;
    }
    if (nslots < SHPC_MIN_SLOTS) {
        return 0;
    }
    if (nslots > SHPC_MAX_SLOTS ||
        SHPC_IDX_TO_PCI(nslots) > PCI_SLOT_MAX) {
        /* TODO: report an error mesage that makes sense. */
        return -EINVAL;
    }
    shpc->nslots = nslots;
    shpc->config = g_malloc0(SHPC_SIZEOF(d));
    shpc->cmask = g_malloc0(SHPC_SIZEOF(d));
    shpc->wmask = g_malloc0(SHPC_SIZEOF(d));
    shpc->w1cmask = g_malloc0(SHPC_SIZEOF(d));

    shpc_reset(d);

    pci_set_long(shpc->config + SHPC_BASE_OFFSET, offset);

    pci_set_byte(shpc->wmask + SHPC_CMD_CODE, 0xff);
    pci_set_byte(shpc->wmask + SHPC_CMD_TRGT, SHPC_CMD_TRGT_MAX);
    pci_set_byte(shpc->wmask + SHPC_CMD_TRGT, SHPC_CMD_TRGT_MAX);
    pci_set_long(shpc->wmask + SHPC_SERR_INT,
                 SHPC_INT_DIS |
                 SHPC_SERR_DIS |
                 SHPC_CMD_INT_DIS |
                 SHPC_ARB_SERR_DIS);
    pci_set_long(shpc->w1cmask + SHPC_SERR_INT,
                 SHPC_CMD_DETECTED |
                 SHPC_ARB_DETECTED);
    for (i = 0; i < nslots; ++i) {
        pci_set_byte(shpc->wmask +
                     SHPC_SLOT_EVENT_SERR_INT_DIS(d, i),
                     SHPC_SLOT_EVENT_PRESENCE |
                     SHPC_SLOT_EVENT_ISOLATED_FAULT |
                     SHPC_SLOT_EVENT_BUTTON |
                     SHPC_SLOT_EVENT_MRL |
                     SHPC_SLOT_EVENT_CONNECTED_FAULT |
                     SHPC_SLOT_EVENT_MRL_SERR_DIS |
                     SHPC_SLOT_EVENT_CONNECTED_FAULT_SERR_DIS);
        pci_set_byte(shpc->w1cmask +
                     SHPC_SLOT_EVENT_LATCH(i),
                     SHPC_SLOT_EVENT_PRESENCE |
                     SHPC_SLOT_EVENT_ISOLATED_FAULT |
                     SHPC_SLOT_EVENT_BUTTON |
                     SHPC_SLOT_EVENT_MRL |
                     SHPC_SLOT_EVENT_CONNECTED_FAULT);
    }

    /* TODO: init cmask */
    memory_region_init_io(&shpc->mmio, OBJECT(d), &shpc_mmio_ops,
                          d, "shpc-mmio", SHPC_SIZEOF(d));
    shpc_cap_update_dword(d);
    memory_region_add_subregion(bar, offset, &shpc->mmio);

    qbus_set_hotplug_handler(BUS(sec_bus), DEVICE(d), NULL);

    d->cap_present |= QEMU_PCI_CAP_SHPC;
    return 0;
}

int shpc_bar_size(PCIDevice *d)
{
    return roundup_pow_of_two(SHPC_SLOT_REG(SHPC_MAX_SLOTS));
}

void shpc_cleanup(PCIDevice *d, MemoryRegion *bar)
{
    SHPCDevice *shpc = d->shpc;
    d->cap_present &= ~QEMU_PCI_CAP_SHPC;
    memory_region_del_subregion(bar, &shpc->mmio);
    /* TODO: cleanup config space changes? */
    g_free(shpc->config);
    g_free(shpc->cmask);
    g_free(shpc->wmask);
    g_free(shpc->w1cmask);
    g_free(shpc);
}

void shpc_cap_write_config(PCIDevice *d, uint32_t addr, uint32_t val, int l)
{
    if (!ranges_overlap(addr, l, d->shpc->cap, SHPC_CAP_LENGTH)) {
        return;
    }
    if (ranges_overlap(addr, l, d->shpc->cap + SHPC_CAP_DWORD_DATA, 4)) {
        unsigned dword_data;
        dword_data = pci_get_long(d->shpc->config + d->shpc->cap
                                  + SHPC_CAP_DWORD_DATA);
        shpc_write(d, shpc_cap_dword(d) * 4, dword_data, 4);
    }
    /* Update cap dword data in case guest is going to read it. */
    shpc_cap_update_dword(d);
}

static void shpc_save(QEMUFile *f, void *pv, size_t size)
{
    PCIDevice *d = container_of(pv, PCIDevice, shpc);
    qemu_put_buffer(f, d->shpc->config, SHPC_SIZEOF(d));
}

static int shpc_load(QEMUFile *f, void *pv, size_t size)
{
    PCIDevice *d = container_of(pv, PCIDevice, shpc);
    int ret = qemu_get_buffer(f, d->shpc->config, SHPC_SIZEOF(d));
    if (ret != SHPC_SIZEOF(d)) {
        return -EINVAL;
    }
    /* Make sure we don't lose notifications. An extra interrupt is harmless. */
    d->shpc->msi_requested = 0;
    shpc_interrupt_update(d);
    return 0;
}

VMStateInfo shpc_vmstate_info = {
    .name = "shpc",
    .get  = shpc_load,
    .put  = shpc_save,
};
