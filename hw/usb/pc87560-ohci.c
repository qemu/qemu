// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NS PC87560 SuperI/O — USB OHCI emulation
 *
 * Copyright (c) 2026 Abizer abizerlokhandwalastd10@gmail.com
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "hw/usb/usb.h"
#include "migration/vmstate.h"
#include "hw/pci/pci_device.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "trace.h"
#include "hcd-ohci.h"
#include "qom/object.h"

#define TYPE_NSC_PCI_OHCI "pc87560-ohci"
OBJECT_DECLARE_SIMPLE_TYPE(OHCIPCIState, NSC_PCI_OHCI)

struct OHCIPCIState {

    PCIDevice parent_obj;
    OHCIState state;
    MemoryRegion nsc_bar;
    char *masterbus;
    uint32_t num_ports;
    uint32_t firstport;
    qemu_irq irq_out[1];

    /* NSC (NUSSBAR) vendor-specific register state */
    uint32_t nsc_control;
    uint32_t nsc_status;
};

#define NSC_CONTROL             0x00
#define NSC_COMMAND_STATUS      0x04
#define NSC_PORT_RX_STATUS      0x08
#define NSC_ROOT_HUB_STATUS     0x34
#define NSC_CTL_BIG_ENDIAN      (1U << 0)
#define NSC_CTL_CCE             (1U << 1)      /* Consistency Check Enable */
#define NSC_CTL_WORDCOUNT        (0x3fU << 2)   /* bits 7:2 */
#define NSC_CTL_MODE             (0x3U << 8)     /* bits 9:8 */
#define NSC_CTL_LIMIT_CONTROL   (1U << 16)
#define NSC_CTL_RISE_TIME       (0xffU << 24)  /* bits 31:24 */
#define NSC_CTL_WRMASK  (NSC_CTL_BIG_ENDIAN | NSC_CTL_CCE | NSC_CTL_WORDCOUNT |\
                          NSC_CTL_MODE | NSC_CTL_LIMIT_CONTROL | \
                          NSC_CTL_RISE_TIME)
#define NSC_CONTROL_RESET        0x90000014U
#define NSC_CS_MEDIA_ERROR      0x00000fffU    /* bits 11:0, 12-bit counter */
#define NSC_CS_MEDIA_ERR_OVFL   (1U << 16)
#define NSC_CS_UE_PCI           (1U << 17)
#define NSC_CS_UE_DESCRIPTOR    (1U << 18)
#define NSC_CS_UE_INTERNAL      (1U << 19)
#define NSC_CS_SCHEDULE_ERROR   (1U << 20)
#define NSC_CS_PHY_OVERRUN      (1U << 21)
#define NSC_CS_W1C_MASK  (NSC_CS_MEDIA_ERR_OVFL | NSC_CS_UE_PCI |\
                          NSC_CS_UE_DESCRIPTOR | NSC_CS_UE_INTERNAL |\
                          NSC_CS_SCHEDULE_ERROR | NSC_CS_PHY_OVERRUN)
#define NSC_OHCI_INTR_SO   (1U << 0)
#define NSC_RHPORT_CCS   (1U << 0)   /* CurrentConnectStatus */
#define NSC_RHPORT_PES   (1U << 1)   /* PortEnableStatus */
#define NSC_RHPORT_PSS   (1U << 2)   /* PortSuspendStatus */
#define NSC_RHPORT_PRS   (1U << 4)   /* PortResetStatus */
#define NSC_RHPORT_PPS   (1U << 8)   /* PortPowerStatus */

static uint32_t nsc_hub_port_state(OHCIState *s, int port)
{
    uint32_t ctrl = s->rhport[port].ctrl;

    if (!(ctrl & NSC_RHPORT_PPS)) {
        return 0x8;             /* POWER OFF */
    }
    if (!(ctrl & NSC_RHPORT_CCS)) {
        return 0xa;             /* DISCONNECTED */
    }
    if (ctrl & NSC_RHPORT_PRS) {
        return 0x2;             /* RESET */
    }
    if (ctrl & NSC_RHPORT_PSS) {
        return 0xc;             /* SUSPENDED */
    }
    if (ctrl & NSC_RHPORT_PES) {
        return 0xe;             /* ENABLED, Idle */
    }
    return 0xb;                 /* DISABLED */
}

static uint32_t nsc_root_hub_status(OHCIPCIState *ohci)
{
    OHCIState *s = &ohci->state;
    uint32_t val = 0;

    if (s->num_ports >= 1) {
        val |= nsc_hub_port_state(s, 0);       /* Port 1 Status[3:0] */
    }
    if (s->num_ports >= 2) {
        val |= nsc_hub_port_state(s, 1) << 8;  /* Port 2 Status[3:0] */
    }
    return val;
}

static void nsc_ohci_media_error(OHCIState *ohci)
{
    OHCIPCIState *dev = container_of(ohci, OHCIPCIState, state);
    uint32_t count = dev->nsc_status & NSC_CS_MEDIA_ERROR;

    if (count == NSC_CS_MEDIA_ERROR) {
        dev->nsc_status |= NSC_CS_MEDIA_ERR_OVFL;
    } else {
        dev->nsc_status = (dev->nsc_status & ~NSC_CS_MEDIA_ERROR) | (count + 1);
    }
}

static void nsc_ohci_descriptor_error(OHCIState *ohci)
{
    OHCIPCIState *dev = container_of(ohci, OHCIPCIState, state);
    dev->nsc_status |= NSC_CS_UE_DESCRIPTOR;
}

static uint64_t nsc_usb_read(void *opaque, hwaddr addr, unsigned size)
{
    OHCIPCIState *ohci = opaque;
    uint32_t val;

    switch (addr & ~3) {
    case NSC_CONTROL:
        val = ohci->nsc_control;
        break;
    case NSC_COMMAND_STATUS:
        val = ohci->nsc_status;
        if (ohci->state.intr_status & NSC_OHCI_INTR_SO) {
            val |= NSC_CS_SCHEDULE_ERROR;
        }
        break;
    case NSC_PORT_RX_STATUS:
        val = 0;
        break;
    case NSC_ROOT_HUB_STATUS:
        val = nsc_root_hub_status(ohci);
        break;
    default:
        val = 0;
        break;
    }

    val >>= (addr & 3) * 8;
    if (size < 4) {
        val &= (1U << (size * 8)) - 1;
    }
    return val;
}

static void pc87560_usb_irq_relay(void *opaque, int n, int level)
{
    OHCIPCIState *ohci = opaque;
    qemu_set_irq(ohci->irq_out[0], level);
}

static void nsc_usb_write(void *opaque, hwaddr addr, uint64_t val,
                          unsigned size)
{
    OHCIPCIState *ohci = opaque;
    uint32_t shift = (addr & 3) * 8;
    uint32_t lane = (size < 4) ? (((1U << (size * 8)) - 1) << shift)
                                : 0xffffffffU;
    uint32_t wval = ((uint32_t)val << shift) & lane;

    switch (addr & ~3) {
    case NSC_CONTROL:
        ohci->nsc_control = (ohci->nsc_control & ~lane) |
                            (wval & NSC_CTL_WRMASK);
        ohci->state.big_endian = (ohci->nsc_control & NSC_CTL_BIG_ENDIAN) != 0;
        ohci->state.consistency_check = (ohci->nsc_control & NSC_CTL_CCE) != 0;
        break;

    case NSC_COMMAND_STATUS:
        if (wval & NSC_CS_MEDIA_ERROR) {
            ohci->nsc_status &= ~NSC_CS_MEDIA_ERROR;
        }
        ohci->nsc_status &= ~(wval & NSC_CS_W1C_MASK);
        if (wval & NSC_CS_SCHEDULE_ERROR) {
            ohci->state.intr_status &= ~NSC_OHCI_INTR_SO;
        }
        break;

    case NSC_PORT_RX_STATUS:
    case NSC_ROOT_HUB_STATUS:
        break;

    default:
        break;
    }
}

static const MemoryRegionOps nsc_usb_ops = {
    .read = nsc_usb_read,
    .write = nsc_usb_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void ohci_pci_die(struct OHCIState *ohci)
{
    OHCIPCIState *dev = container_of(ohci, OHCIPCIState, state);

    ohci_sysbus_die(ohci);

    dev->nsc_status |= NSC_CS_UE_INTERNAL;

    pci_set_word(dev->parent_obj.config + PCI_STATUS,
                 PCI_STATUS_DETECTED_PARITY);
}

static uint32_t pc87560_ohci_config_read(PCIDevice *pci, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pci, addr, len);
    return val;
}

static void pc87560_ohci_config_write(PCIDevice *pci, uint32_t addr,
                                       uint32_t val, int len)
{
    pci_default_write_config(pci, addr, val, len);
}

static void usb_ohci_realize_pci(PCIDevice *dev, Error **errp)
{
    Error *err = NULL;
    OHCIPCIState *ohci = NSC_PCI_OHCI(dev);

    dev->config[PCI_CLASS_PROG]    = 0x10;
    dev->wmask[PCI_CLASS_PROG]     = 0xff;
    dev->config[PCI_INTERRUPT_PIN] = 0x01;

    qdev_init_gpio_out(DEVICE(dev), ohci->irq_out, 1);
    ohci->state.irq = qemu_allocate_irq(pc87560_usb_irq_relay, ohci, 0);

    usb_ohci_init(&ohci->state, DEVICE(dev), ohci->num_ports, 0,
                  ohci->masterbus, ohci->firstport,
                  pci_get_address_space(dev), ohci_pci_die, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    ohci->state.media_error = nsc_ohci_media_error;
    ohci->state.descriptor_error = nsc_ohci_descriptor_error;

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &ohci->state.mem);

    memory_region_init_io(&ohci->nsc_bar, OBJECT(dev), &nsc_usb_ops, ohci,
                          "nsc-usb", 0x1000);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &ohci->nsc_bar);
}

static void usb_ohci_exit(PCIDevice *dev)
{
    OHCIPCIState *ohci = NSC_PCI_OHCI(dev);
    OHCIState *s = &ohci->state;

    trace_usb_ohci_exit(s->name);
    ohci_bus_stop(s);

    if (s->async_td) {
        usb_cancel_packet(&s->usb_packet);
        s->async_td = 0;
    }
    ohci_stop_endpoints(s);

    if (!ohci->masterbus) {
        usb_bus_release(&s->bus);
    }

    timer_free(s->eof_timer);
}

static void usb_ohci_reset_pci(DeviceState *d)
{
    PCIDevice *dev = PCI_DEVICE(d);
    OHCIPCIState *ohci = NSC_PCI_OHCI(dev);
    OHCIState *s = &ohci->state;

    ohci_hard_reset(s);

    ohci->nsc_control = NSC_CONTROL_RESET;
    ohci->nsc_status = 0;
}

static const Property ohci_pci_properties[] = {
    DEFINE_PROP_STRING("masterbus", OHCIPCIState, masterbus),
    DEFINE_PROP_UINT32("num-ports", OHCIPCIState, num_ports, 3),
    DEFINE_PROP_UINT32("firstport", OHCIPCIState, firstport, 0),
};

static const VMStateDescription vmstate_ohci = {
    .name = "pc87560-ohci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, OHCIPCIState),
        VMSTATE_STRUCT(state, OHCIPCIState, 1, vmstate_ohci_state, OHCIState),
        VMSTATE_UINT32(nsc_control, OHCIPCIState),
        VMSTATE_UINT32(nsc_status, OHCIPCIState),
        VMSTATE_END_OF_LIST()
    }
};

static void ohci_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize       = usb_ohci_realize_pci;
    k->exit          = usb_ohci_exit;
    k->config_read   = pc87560_ohci_config_read;
    k->config_write  = pc87560_ohci_config_write;
    k->vendor_id     = PCI_VENDOR_ID_NS;
    k->device_id     = PCI_DEVICE_ID_NS_87560_USB;
    k->revision      = 0x01;
    k->class_id      = PCI_CLASS_SERIAL_USB;
    k->subsystem_vendor_id = PCI_VENDOR_ID_HP;
    k->subsystem_id        = 0x10A7;

    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    dc->desc         = "National Semiconductor USB Controller";
    device_class_set_props(dc, ohci_pci_properties);
    dc->hotpluggable = false;
    dc->user_creatable = false;
    dc->vmsd         = &vmstate_ohci;
    device_class_set_legacy_reset(dc, usb_ohci_reset_pci);
}

static const TypeInfo ohci_pci_info = {
    .name          = TYPE_NSC_PCI_OHCI,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(OHCIPCIState),
    .class_init    = ohci_pci_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void ohci_pci_register_types(void)
{
    type_register_static(&ohci_pci_info);
}

type_init(ohci_pci_register_types)
