/*
 * pcie.c
 *
 * Copyright (c) 2010 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pcie.h"
#include "hw/pci/msix.h"
#include "hw/pci/msi.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pcie_regs.h"
#include "hw/pci/pcie_port.h"
#include "qemu/range.h"

//#define DEBUG_PCIE
#ifdef DEBUG_PCIE
# define PCIE_DPRINTF(fmt, ...)                                         \
    fprintf(stderr, "%s:%d " fmt, __func__, __LINE__, ## __VA_ARGS__)
#else
# define PCIE_DPRINTF(fmt, ...) do {} while (0)
#endif
#define PCIE_DEV_PRINTF(dev, fmt, ...)                                  \
    PCIE_DPRINTF("%s:%x "fmt, (dev)->name, (dev)->devfn, ## __VA_ARGS__)


/***************************************************************************
 * pci express capability helper functions
 */

static void
pcie_cap_v1_fill(PCIDevice *dev, uint8_t port, uint8_t type, uint8_t version)
{
    uint8_t *exp_cap = dev->config + dev->exp.exp_cap;
    uint8_t *cmask = dev->cmask + dev->exp.exp_cap;

    /* capability register
    interrupt message number defaults to 0 */
    pci_set_word(exp_cap + PCI_EXP_FLAGS,
                 ((type << PCI_EXP_FLAGS_TYPE_SHIFT) & PCI_EXP_FLAGS_TYPE) |
                 version);

    /* device capability register
     * table 7-12:
     * roll based error reporting bit must be set by all
     * Functions conforming to the ECN, PCI Express Base
     * Specification, Revision 1.1., or subsequent PCI Express Base
     * Specification revisions.
     */
    pci_set_long(exp_cap + PCI_EXP_DEVCAP, PCI_EXP_DEVCAP_RBER);

    pci_set_long(exp_cap + PCI_EXP_LNKCAP,
                 (port << PCI_EXP_LNKCAP_PN_SHIFT) |
                 PCI_EXP_LNKCAP_ASPMS_0S |
                 QEMU_PCI_EXP_LNKCAP_MLW(QEMU_PCI_EXP_LNK_X1) |
                 QEMU_PCI_EXP_LNKCAP_MLS(QEMU_PCI_EXP_LNK_2_5GT));

    pci_set_word(exp_cap + PCI_EXP_LNKSTA,
                 QEMU_PCI_EXP_LNKSTA_NLW(QEMU_PCI_EXP_LNK_X1) |
                 QEMU_PCI_EXP_LNKSTA_CLS(QEMU_PCI_EXP_LNK_2_5GT));

    /* We changed link status bits over time, and changing them across
     * migrations is generally fine as hardware changes them too.
     * Let's not bother checking.
     */
    pci_set_word(cmask + PCI_EXP_LNKSTA, 0);
}

static void pcie_cap_fill_slot_lnk(PCIDevice *dev)
{
    PCIESlot *s = (PCIESlot *)object_dynamic_cast(OBJECT(dev), TYPE_PCIE_SLOT);
    uint8_t *exp_cap = dev->config + dev->exp.exp_cap;

    /* Skip anything that isn't a PCIESlot */
    if (!s) {
        return;
    }

    /* Clear and fill LNKCAP from what was configured above */
    pci_long_test_and_clear_mask(exp_cap + PCI_EXP_LNKCAP,
                                 PCI_EXP_LNKCAP_MLW | PCI_EXP_LNKCAP_SLS);
    pci_long_test_and_set_mask(exp_cap + PCI_EXP_LNKCAP,
                               QEMU_PCI_EXP_LNKCAP_MLW(s->width) |
                               QEMU_PCI_EXP_LNKCAP_MLS(s->speed));

    /*
     * Link bandwidth notification is required for all root ports and
     * downstream ports supporting links wider than x1 or multiple link
     * speeds.
     */
    if (s->width > QEMU_PCI_EXP_LNK_X1 ||
        s->speed > QEMU_PCI_EXP_LNK_2_5GT) {
        pci_long_test_and_set_mask(exp_cap + PCI_EXP_LNKCAP,
                                   PCI_EXP_LNKCAP_LBNC);
    }

    if (s->speed > QEMU_PCI_EXP_LNK_2_5GT) {
        /*
         * Hot-plug capable downstream ports and downstream ports supporting
         * link speeds greater than 5GT/s must hardwire PCI_EXP_LNKCAP_DLLLARC
         * to 1b.  PCI_EXP_LNKCAP_DLLLARC implies PCI_EXP_LNKSTA_DLLLA, which
         * we also hardwire to 1b here.  2.5GT/s hot-plug slots should also
         * technically implement this, but it's not done here for compatibility.
         */
        pci_long_test_and_set_mask(exp_cap + PCI_EXP_LNKCAP,
                                   PCI_EXP_LNKCAP_DLLLARC);
        /* the PCI_EXP_LNKSTA_DLLLA will be set in the hotplug function */

        /*
         * Target Link Speed defaults to the highest link speed supported by
         * the component.  2.5GT/s devices are permitted to hardwire to zero.
         */
        pci_word_test_and_clear_mask(exp_cap + PCI_EXP_LNKCTL2,
                                     PCI_EXP_LNKCTL2_TLS);
        pci_word_test_and_set_mask(exp_cap + PCI_EXP_LNKCTL2,
                                   QEMU_PCI_EXP_LNKCAP_MLS(s->speed) &
                                   PCI_EXP_LNKCTL2_TLS);
    }

    /*
     * 2.5 & 5.0GT/s can be fully described by LNKCAP, but 8.0GT/s is
     * actually a reference to the highest bit supported in this register.
     * We assume the device supports all link speeds.
     */
    if (s->speed > QEMU_PCI_EXP_LNK_5GT) {
        pci_long_test_and_clear_mask(exp_cap + PCI_EXP_LNKCAP2, ~0U);
        pci_long_test_and_set_mask(exp_cap + PCI_EXP_LNKCAP2,
                                   PCI_EXP_LNKCAP2_SLS_2_5GB |
                                   PCI_EXP_LNKCAP2_SLS_5_0GB |
                                   PCI_EXP_LNKCAP2_SLS_8_0GB);
        if (s->speed > QEMU_PCI_EXP_LNK_8GT) {
            pci_long_test_and_set_mask(exp_cap + PCI_EXP_LNKCAP2,
                                       PCI_EXP_LNKCAP2_SLS_16_0GB);
        }
    }
}

int pcie_cap_init(PCIDevice *dev, uint8_t offset,
                  uint8_t type, uint8_t port,
                  Error **errp)
{
    /* PCIe cap v2 init */
    int pos;
    uint8_t *exp_cap;

    assert(pci_is_express(dev));

    pos = pci_add_capability(dev, PCI_CAP_ID_EXP, offset,
                             PCI_EXP_VER2_SIZEOF, errp);
    if (pos < 0) {
        return pos;
    }
    dev->exp.exp_cap = pos;
    exp_cap = dev->config + pos;

    /* Filling values common with v1 */
    pcie_cap_v1_fill(dev, port, type, PCI_EXP_FLAGS_VER2);

    /* Fill link speed and width options */
    pcie_cap_fill_slot_lnk(dev);

    /* Filling v2 specific values */
    pci_set_long(exp_cap + PCI_EXP_DEVCAP2,
                 PCI_EXP_DEVCAP2_EFF | PCI_EXP_DEVCAP2_EETLPP);

    pci_set_word(dev->wmask + pos + PCI_EXP_DEVCTL2, PCI_EXP_DEVCTL2_EETLPPB);

    if (dev->cap_present & QEMU_PCIE_EXTCAP_INIT) {
        /* read-only to behave like a 'NULL' Extended Capability Header */
        pci_set_long(dev->wmask + PCI_CONFIG_SPACE_SIZE, 0);
    }

    return pos;
}

int pcie_cap_v1_init(PCIDevice *dev, uint8_t offset, uint8_t type,
                     uint8_t port)
{
    /* PCIe cap v1 init */
    int pos;
    Error *local_err = NULL;

    assert(pci_is_express(dev));

    pos = pci_add_capability(dev, PCI_CAP_ID_EXP, offset,
                             PCI_EXP_VER1_SIZEOF, &local_err);
    if (pos < 0) {
        error_report_err(local_err);
        return pos;
    }
    dev->exp.exp_cap = pos;

    pcie_cap_v1_fill(dev, port, type, PCI_EXP_FLAGS_VER1);

    return pos;
}

static int
pcie_endpoint_cap_common_init(PCIDevice *dev, uint8_t offset, uint8_t cap_size)
{
    uint8_t type = PCI_EXP_TYPE_ENDPOINT;
    Error *local_err = NULL;
    int ret;

    /*
     * Windows guests will report Code 10, device cannot start, if
     * a regular Endpoint type is exposed on a root complex.  These
     * should instead be Root Complex Integrated Endpoints.
     */
    if (pci_bus_is_express(pci_get_bus(dev))
        && pci_bus_is_root(pci_get_bus(dev))) {
        type = PCI_EXP_TYPE_RC_END;
    }

    if (cap_size == PCI_EXP_VER1_SIZEOF) {
        return pcie_cap_v1_init(dev, offset, type, 0);
    } else {
        ret = pcie_cap_init(dev, offset, type, 0, &local_err);

        if (ret < 0) {
            error_report_err(local_err);
        }

        return ret;
    }
}

int pcie_endpoint_cap_init(PCIDevice *dev, uint8_t offset)
{
    return pcie_endpoint_cap_common_init(dev, offset, PCI_EXP_VER2_SIZEOF);
}

int pcie_endpoint_cap_v1_init(PCIDevice *dev, uint8_t offset)
{
    return pcie_endpoint_cap_common_init(dev, offset, PCI_EXP_VER1_SIZEOF);
}

void pcie_cap_exit(PCIDevice *dev)
{
    pci_del_capability(dev, PCI_CAP_ID_EXP, PCI_EXP_VER2_SIZEOF);
}

void pcie_cap_v1_exit(PCIDevice *dev)
{
    pci_del_capability(dev, PCI_CAP_ID_EXP, PCI_EXP_VER1_SIZEOF);
}

uint8_t pcie_cap_get_type(const PCIDevice *dev)
{
    uint32_t pos = dev->exp.exp_cap;
    assert(pos > 0);
    return (pci_get_word(dev->config + pos + PCI_EXP_FLAGS) &
            PCI_EXP_FLAGS_TYPE) >> PCI_EXP_FLAGS_TYPE_SHIFT;
}

/* MSI/MSI-X */
/* pci express interrupt message number */
/* 7.8.2 PCI Express Capabilities Register: Interrupt Message Number */
void pcie_cap_flags_set_vector(PCIDevice *dev, uint8_t vector)
{
    uint8_t *exp_cap = dev->config + dev->exp.exp_cap;
    assert(vector < 32);
    pci_word_test_and_clear_mask(exp_cap + PCI_EXP_FLAGS, PCI_EXP_FLAGS_IRQ);
    pci_word_test_and_set_mask(exp_cap + PCI_EXP_FLAGS,
                               vector << PCI_EXP_FLAGS_IRQ_SHIFT);
}

uint8_t pcie_cap_flags_get_vector(PCIDevice *dev)
{
    return (pci_get_word(dev->config + dev->exp.exp_cap + PCI_EXP_FLAGS) &
            PCI_EXP_FLAGS_IRQ) >> PCI_EXP_FLAGS_IRQ_SHIFT;
}

void pcie_cap_deverr_init(PCIDevice *dev)
{
    uint32_t pos = dev->exp.exp_cap;
    pci_long_test_and_set_mask(dev->config + pos + PCI_EXP_DEVCAP,
                               PCI_EXP_DEVCAP_RBER);
    pci_long_test_and_set_mask(dev->wmask + pos + PCI_EXP_DEVCTL,
                               PCI_EXP_DEVCTL_CERE | PCI_EXP_DEVCTL_NFERE |
                               PCI_EXP_DEVCTL_FERE | PCI_EXP_DEVCTL_URRE);
    pci_long_test_and_set_mask(dev->w1cmask + pos + PCI_EXP_DEVSTA,
                               PCI_EXP_DEVSTA_CED | PCI_EXP_DEVSTA_NFED |
                               PCI_EXP_DEVSTA_FED | PCI_EXP_DEVSTA_URD);
}

void pcie_cap_deverr_reset(PCIDevice *dev)
{
    uint8_t *devctl = dev->config + dev->exp.exp_cap + PCI_EXP_DEVCTL;
    pci_long_test_and_clear_mask(devctl,
                                 PCI_EXP_DEVCTL_CERE | PCI_EXP_DEVCTL_NFERE |
                                 PCI_EXP_DEVCTL_FERE | PCI_EXP_DEVCTL_URRE);
}

void pcie_cap_lnkctl_init(PCIDevice *dev)
{
    uint32_t pos = dev->exp.exp_cap;
    pci_long_test_and_set_mask(dev->wmask + pos + PCI_EXP_LNKCTL,
                               PCI_EXP_LNKCTL_CCC | PCI_EXP_LNKCTL_ES);
}

void pcie_cap_lnkctl_reset(PCIDevice *dev)
{
    uint8_t *lnkctl = dev->config + dev->exp.exp_cap + PCI_EXP_LNKCTL;
    pci_long_test_and_clear_mask(lnkctl,
                                 PCI_EXP_LNKCTL_CCC | PCI_EXP_LNKCTL_ES);
}

static void hotplug_event_update_event_status(PCIDevice *dev)
{
    uint32_t pos = dev->exp.exp_cap;
    uint8_t *exp_cap = dev->config + pos;
    uint16_t sltctl = pci_get_word(exp_cap + PCI_EXP_SLTCTL);
    uint16_t sltsta = pci_get_word(exp_cap + PCI_EXP_SLTSTA);

    dev->exp.hpev_notified = (sltctl & PCI_EXP_SLTCTL_HPIE) &&
        (sltsta & sltctl & PCI_EXP_HP_EV_SUPPORTED);
}

static void hotplug_event_notify(PCIDevice *dev)
{
    bool prev = dev->exp.hpev_notified;

    hotplug_event_update_event_status(dev);

    if (prev == dev->exp.hpev_notified) {
        return;
    }

    /* Note: the logic above does not take into account whether interrupts
     * are masked. The result is that interrupt will be sent when it is
     * subsequently unmasked. This appears to be legal: Section 6.7.3.4:
     * The Port may optionally send an MSI when there are hot-plug events that
     * occur while interrupt generation is disabled, and interrupt generation is
     * subsequently enabled. */
    if (msix_enabled(dev)) {
        msix_notify(dev, pcie_cap_flags_get_vector(dev));
    } else if (msi_enabled(dev)) {
        msi_notify(dev, pcie_cap_flags_get_vector(dev));
    } else if (pci_intx(dev) != -1) {
        pci_set_irq(dev, dev->exp.hpev_notified);
    }
}

static void hotplug_event_clear(PCIDevice *dev)
{
    hotplug_event_update_event_status(dev);
    if (!msix_enabled(dev) && !msi_enabled(dev) && pci_intx(dev) != -1 &&
        !dev->exp.hpev_notified) {
        pci_irq_deassert(dev);
    }
}

void pcie_cap_slot_enable_power(PCIDevice *dev)
{
    uint8_t *exp_cap = dev->config + dev->exp.exp_cap;
    uint32_t sltcap = pci_get_long(exp_cap + PCI_EXP_SLTCAP);

    if (sltcap & PCI_EXP_SLTCAP_PCP) {
        pci_set_word_by_mask(exp_cap + PCI_EXP_SLTCTL,
                             PCI_EXP_SLTCTL_PCC, PCI_EXP_SLTCTL_PWR_ON);
    }
}

static void pcie_set_power_device(PCIBus *bus, PCIDevice *dev, void *opaque)
{
    bool *power = opaque;

    pci_set_power(dev, *power);
}

static void pcie_cap_update_power(PCIDevice *hotplug_dev)
{
    uint8_t *exp_cap = hotplug_dev->config + hotplug_dev->exp.exp_cap;
    PCIBus *sec_bus = pci_bridge_get_sec_bus(PCI_BRIDGE(hotplug_dev));
    uint32_t sltcap = pci_get_long(exp_cap + PCI_EXP_SLTCAP);
    uint16_t sltctl = pci_get_word(exp_cap + PCI_EXP_SLTCTL);
    bool power = true;

    if (sltcap & PCI_EXP_SLTCAP_PCP) {
        power = (sltctl & PCI_EXP_SLTCTL_PCC) == PCI_EXP_SLTCTL_PWR_ON;
    }

    pci_for_each_device(sec_bus, pci_bus_num(sec_bus),
                        pcie_set_power_device, &power);
}

/*
 * A PCI Express Hot-Plug Event has occurred, so update slot status register
 * and notify OS of the event if necessary.
 *
 * 6.7.3 PCI Express Hot-Plug Events
 * 6.7.3.4 Software Notification of Hot-Plug Events
 */
static void pcie_cap_slot_event(PCIDevice *dev, PCIExpressHotPlugEvent event)
{
    /* Minor optimization: if nothing changed - no event is needed. */
    if (pci_word_test_and_set_mask(dev->config + dev->exp.exp_cap +
                                   PCI_EXP_SLTSTA, event) == event) {
        return;
    }
    hotplug_event_notify(dev);
}

static void pcie_cap_slot_plug_common(PCIDevice *hotplug_dev, DeviceState *dev,
                                      Error **errp)
{
    uint8_t *exp_cap = hotplug_dev->config + hotplug_dev->exp.exp_cap;
    uint16_t sltsta = pci_get_word(exp_cap + PCI_EXP_SLTSTA);

    PCIE_DEV_PRINTF(PCI_DEVICE(dev), "hotplug state: 0x%x\n", sltsta);
    if (sltsta & PCI_EXP_SLTSTA_EIS) {
        /* the slot is electromechanically locked.
         * This error is propagated up to qdev and then to HMP/QMP.
         */
        error_setg_errno(errp, EBUSY, "slot is electromechanically locked");
    }
}

void pcie_cap_slot_pre_plug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                               Error **errp)
{
    PCIDevice *hotplug_pdev = PCI_DEVICE(hotplug_dev);
    uint8_t *exp_cap = hotplug_pdev->config + hotplug_pdev->exp.exp_cap;
    uint32_t sltcap = pci_get_word(exp_cap + PCI_EXP_SLTCAP);

    /* Check if hot-plug is disabled on the slot */
    if (dev->hotplugged && (sltcap & PCI_EXP_SLTCAP_HPC) == 0) {
        error_setg(errp, "Hot-plug failed: unsupported by the port device '%s'",
                         DEVICE(hotplug_pdev)->id);
        return;
    }

    pcie_cap_slot_plug_common(PCI_DEVICE(hotplug_dev), dev, errp);
}

void pcie_cap_slot_plug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                           Error **errp)
{
    PCIDevice *hotplug_pdev = PCI_DEVICE(hotplug_dev);
    uint8_t *exp_cap = hotplug_pdev->config + hotplug_pdev->exp.exp_cap;
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    uint32_t lnkcap = pci_get_long(exp_cap + PCI_EXP_LNKCAP);

    if (pci_is_vf(pci_dev)) {
        /* Virtual function cannot be physically disconnected */
        return;
    }

    /* Don't send event when device is enabled during qemu machine creation:
     * it is present on boot, no hotplug event is necessary. We do send an
     * event when the device is disabled later. */
    if (!dev->hotplugged) {
        pci_word_test_and_set_mask(exp_cap + PCI_EXP_SLTSTA,
                                   PCI_EXP_SLTSTA_PDS);
        if (pci_dev->cap_present & QEMU_PCIE_LNKSTA_DLLLA ||
            (lnkcap & PCI_EXP_LNKCAP_DLLLARC)) {
            pci_word_test_and_set_mask(exp_cap + PCI_EXP_LNKSTA,
                                       PCI_EXP_LNKSTA_DLLLA);
        }
        pcie_cap_update_power(hotplug_pdev);
        return;
    }

    /* To enable multifunction hot-plug, we just ensure the function
     * 0 added last. When function 0 is added, we set the sltsta and
     * inform OS via event notification.
     */
    if (pci_get_function_0(pci_dev)) {
        pci_word_test_and_set_mask(exp_cap + PCI_EXP_SLTSTA,
                                   PCI_EXP_SLTSTA_PDS);
        if (pci_dev->cap_present & QEMU_PCIE_LNKSTA_DLLLA ||
            (lnkcap & PCI_EXP_LNKCAP_DLLLARC)) {
            pci_word_test_and_set_mask(exp_cap + PCI_EXP_LNKSTA,
                                       PCI_EXP_LNKSTA_DLLLA);
        }
        pcie_cap_slot_event(hotplug_pdev,
                            PCI_EXP_HP_EV_PDC | PCI_EXP_HP_EV_ABP);
        pcie_cap_update_power(hotplug_pdev);
    }
}

void pcie_cap_slot_unplug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                             Error **errp)
{
    qdev_unrealize(dev);
}

static void pcie_unplug_device(PCIBus *bus, PCIDevice *dev, void *opaque)
{
    HotplugHandler *hotplug_ctrl = qdev_get_hotplug_handler(DEVICE(dev));

    if (dev->partially_hotplugged) {
        dev->qdev.pending_deleted_event = false;
        return;
    }
    hotplug_handler_unplug(hotplug_ctrl, DEVICE(dev), &error_abort);
    object_unparent(OBJECT(dev));
}

static void pcie_cap_slot_do_unplug(PCIDevice *dev)
{
    PCIBus *sec_bus = pci_bridge_get_sec_bus(PCI_BRIDGE(dev));
    uint8_t *exp_cap = dev->config + dev->exp.exp_cap;
    uint32_t lnkcap = pci_get_long(exp_cap + PCI_EXP_LNKCAP);

    pci_for_each_device_under_bus(sec_bus, pcie_unplug_device, NULL);

    pci_word_test_and_clear_mask(exp_cap + PCI_EXP_SLTSTA,
                                 PCI_EXP_SLTSTA_PDS);
    if (dev->cap_present & QEMU_PCIE_LNKSTA_DLLLA ||
        (lnkcap & PCI_EXP_LNKCAP_DLLLARC)) {
        pci_word_test_and_clear_mask(exp_cap + PCI_EXP_LNKSTA,
                                     PCI_EXP_LNKSTA_DLLLA);
    }
    pci_word_test_and_set_mask(exp_cap + PCI_EXP_SLTSTA,
                               PCI_EXP_SLTSTA_PDC);
}

void pcie_cap_slot_unplug_request_cb(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp)
{
    Error *local_err = NULL;
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    PCIBus *bus = pci_get_bus(pci_dev);
    PCIDevice *hotplug_pdev = PCI_DEVICE(hotplug_dev);
    uint8_t *exp_cap = hotplug_pdev->config + hotplug_pdev->exp.exp_cap;
    uint32_t sltcap = pci_get_word(exp_cap + PCI_EXP_SLTCAP);
    uint16_t sltctl = pci_get_word(exp_cap + PCI_EXP_SLTCTL);

    /* Check if hot-unplug is disabled on the slot */
    if ((sltcap & PCI_EXP_SLTCAP_HPC) == 0) {
        error_setg(errp, "Hot-unplug failed: "
                         "unsupported by the port device '%s'",
                         DEVICE(hotplug_pdev)->id);
        return;
    }

    pcie_cap_slot_plug_common(hotplug_pdev, dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if ((sltctl & PCI_EXP_SLTCTL_PIC) == PCI_EXP_SLTCTL_PWR_IND_BLINK) {
        error_setg(errp, "Hot-unplug failed: "
                   "guest is busy (power indicator blinking)");
        return;
    }

    dev->pending_deleted_event = true;
    dev->pending_deleted_expires_ms =
        qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 5000; /* 5 secs */

    /* In case user cancel the operation of multi-function hot-add,
     * remove the function that is unexposed to guest individually,
     * without interaction with guest.
     */
    if (pci_dev->devfn &&
        !bus->devices[0]) {
        pcie_unplug_device(bus, pci_dev, NULL);

        return;
    }

    if (((sltctl & PCI_EXP_SLTCTL_PIC) == PCI_EXP_SLTCTL_PWR_IND_OFF) &&
        ((sltctl & PCI_EXP_SLTCTL_PCC) == PCI_EXP_SLTCTL_PWR_OFF)) {
        /* slot is powered off -> unplug without round-trip to the guest */
        pcie_cap_slot_do_unplug(hotplug_pdev);
        hotplug_event_notify(hotplug_pdev);
        pci_word_test_and_clear_mask(exp_cap + PCI_EXP_SLTSTA,
                                     PCI_EXP_SLTSTA_ABP);
        return;
    }

    pcie_cap_slot_push_attention_button(hotplug_pdev);
}

/* pci express slot for pci express root/downstream port
   PCI express capability slot registers */
void pcie_cap_slot_init(PCIDevice *dev, PCIESlot *s)
{
    uint32_t pos = dev->exp.exp_cap;

    pci_word_test_and_set_mask(dev->config + pos + PCI_EXP_FLAGS,
                               PCI_EXP_FLAGS_SLOT);

    pci_long_test_and_clear_mask(dev->config + pos + PCI_EXP_SLTCAP,
                                 ~PCI_EXP_SLTCAP_PSN);
    pci_long_test_and_set_mask(dev->config + pos + PCI_EXP_SLTCAP,
                               (s->slot << PCI_EXP_SLTCAP_PSN_SHIFT) |
                               PCI_EXP_SLTCAP_EIP |
                               PCI_EXP_SLTCAP_PIP |
                               PCI_EXP_SLTCAP_AIP |
                               PCI_EXP_SLTCAP_ABP);

    /*
     * Expose native hot-plug on all bridges if hot-plug is enabled on the slot.
     * (unless broken 6.1 ABI is enforced for compat reasons)
     */
    if (s->hotplug &&
        (!s->hide_native_hotplug_cap || DEVICE(dev)->hotplugged)) {
        pci_long_test_and_set_mask(dev->config + pos + PCI_EXP_SLTCAP,
                                   PCI_EXP_SLTCAP_HPS |
                                   PCI_EXP_SLTCAP_HPC);
    }

    if (dev->cap_present & QEMU_PCIE_SLTCAP_PCP) {
        pci_long_test_and_set_mask(dev->config + pos + PCI_EXP_SLTCAP,
                                   PCI_EXP_SLTCAP_PCP);
        pci_word_test_and_clear_mask(dev->config + pos + PCI_EXP_SLTCTL,
                                     PCI_EXP_SLTCTL_PCC);
        pci_word_test_and_set_mask(dev->wmask + pos + PCI_EXP_SLTCTL,
                                   PCI_EXP_SLTCTL_PCC);
    }

    pci_word_test_and_clear_mask(dev->config + pos + PCI_EXP_SLTCTL,
                                 PCI_EXP_SLTCTL_PIC |
                                 PCI_EXP_SLTCTL_AIC);
    pci_word_test_and_set_mask(dev->config + pos + PCI_EXP_SLTCTL,
                               PCI_EXP_SLTCTL_PIC_OFF |
                               PCI_EXP_SLTCTL_AIC_OFF);
    pci_word_test_and_set_mask(dev->wmask + pos + PCI_EXP_SLTCTL,
                               PCI_EXP_SLTCTL_PIC |
                               PCI_EXP_SLTCTL_AIC |
                               PCI_EXP_SLTCTL_HPIE |
                               PCI_EXP_SLTCTL_CCIE |
                               PCI_EXP_SLTCTL_PDCE |
                               PCI_EXP_SLTCTL_ABPE);
    /* Although reading PCI_EXP_SLTCTL_EIC returns always 0,
     * make the bit writable here in order to detect 1b is written.
     * pcie_cap_slot_write_config() test-and-clear the bit, so
     * this bit always returns 0 to the guest.
     */
    pci_word_test_and_set_mask(dev->wmask + pos + PCI_EXP_SLTCTL,
                               PCI_EXP_SLTCTL_EIC);

    pci_word_test_and_set_mask(dev->w1cmask + pos + PCI_EXP_SLTSTA,
                               PCI_EXP_HP_EV_SUPPORTED);

    dev->exp.hpev_notified = false;

    qbus_set_hotplug_handler(BUS(pci_bridge_get_sec_bus(PCI_BRIDGE(dev))),
                             OBJECT(dev));
}

void pcie_cap_slot_reset(PCIDevice *dev)
{
    uint8_t *exp_cap = dev->config + dev->exp.exp_cap;
    uint8_t port_type = pcie_cap_get_type(dev);

    assert(port_type == PCI_EXP_TYPE_DOWNSTREAM ||
           port_type == PCI_EXP_TYPE_ROOT_PORT);

    PCIE_DEV_PRINTF(dev, "reset\n");

    pci_word_test_and_clear_mask(exp_cap + PCI_EXP_SLTCTL,
                                 PCI_EXP_SLTCTL_EIC |
                                 PCI_EXP_SLTCTL_PIC |
                                 PCI_EXP_SLTCTL_AIC |
                                 PCI_EXP_SLTCTL_HPIE |
                                 PCI_EXP_SLTCTL_CCIE |
                                 PCI_EXP_SLTCTL_PDCE |
                                 PCI_EXP_SLTCTL_ABPE);
    pci_word_test_and_set_mask(exp_cap + PCI_EXP_SLTCTL,
                               PCI_EXP_SLTCTL_AIC_OFF);

    if (dev->cap_present & QEMU_PCIE_SLTCAP_PCP) {
        /* Downstream ports enforce device number 0. */
        bool populated = pci_bridge_get_sec_bus(PCI_BRIDGE(dev))->devices[0];
        uint16_t pic;

        if (populated) {
            pci_word_test_and_clear_mask(exp_cap + PCI_EXP_SLTCTL,
                                         PCI_EXP_SLTCTL_PCC);
        } else {
            pci_word_test_and_set_mask(exp_cap + PCI_EXP_SLTCTL,
                                       PCI_EXP_SLTCTL_PCC);
        }

        pic = populated ? PCI_EXP_SLTCTL_PIC_ON : PCI_EXP_SLTCTL_PIC_OFF;
        pci_word_test_and_set_mask(exp_cap + PCI_EXP_SLTCTL, pic);
    }

    pci_word_test_and_clear_mask(exp_cap + PCI_EXP_SLTSTA,
                                 PCI_EXP_SLTSTA_EIS |/* on reset,
                                                        the lock is released */
                                 PCI_EXP_SLTSTA_CC |
                                 PCI_EXP_SLTSTA_PDC |
                                 PCI_EXP_SLTSTA_ABP);

    pcie_cap_update_power(dev);
    hotplug_event_update_event_status(dev);
}

void pcie_cap_slot_get(PCIDevice *dev, uint16_t *slt_ctl, uint16_t *slt_sta)
{
    uint32_t pos = dev->exp.exp_cap;
    uint8_t *exp_cap = dev->config + pos;
    *slt_ctl = pci_get_word(exp_cap + PCI_EXP_SLTCTL);
    *slt_sta = pci_get_word(exp_cap + PCI_EXP_SLTSTA);
}

void pcie_cap_slot_write_config(PCIDevice *dev,
                                uint16_t old_slt_ctl, uint16_t old_slt_sta,
                                uint32_t addr, uint32_t val, int len)
{
    uint32_t pos = dev->exp.exp_cap;
    uint8_t *exp_cap = dev->config + pos;
    uint16_t sltsta = pci_get_word(exp_cap + PCI_EXP_SLTSTA);

    if (ranges_overlap(addr, len, pos + PCI_EXP_SLTSTA, 2)) {
        /*
         * Guests tend to clears all bits during init.
         * If they clear bits that weren't set this is racy and will lose events:
         * not a big problem for manual button presses, but a problem for us.
         * As a work-around, detect this and revert status to what it was
         * before the write.
         *
         * Note: in theory this can be detected as a duplicate button press
         * which cancels the previous press. Does not seem to happen in
         * practice as guests seem to only have this bug during init.
         */
#define PCIE_SLOT_EVENTS (PCI_EXP_SLTSTA_ABP | PCI_EXP_SLTSTA_PFD | \
                          PCI_EXP_SLTSTA_MRLSC | PCI_EXP_SLTSTA_PDC | \
                          PCI_EXP_SLTSTA_CC)

        if (val & ~old_slt_sta & PCIE_SLOT_EVENTS) {
            sltsta = (sltsta & ~PCIE_SLOT_EVENTS) | (old_slt_sta & PCIE_SLOT_EVENTS);
            pci_set_word(exp_cap + PCI_EXP_SLTSTA, sltsta);
        }
        hotplug_event_clear(dev);
    }

    if (!ranges_overlap(addr, len, pos + PCI_EXP_SLTCTL, 2)) {
        return;
    }

    if (pci_word_test_and_clear_mask(exp_cap + PCI_EXP_SLTCTL,
                                     PCI_EXP_SLTCTL_EIC)) {
        sltsta ^= PCI_EXP_SLTSTA_EIS; /* toggle PCI_EXP_SLTSTA_EIS bit */
        pci_set_word(exp_cap + PCI_EXP_SLTSTA, sltsta);
        PCIE_DEV_PRINTF(dev, "PCI_EXP_SLTCTL_EIC: "
                        "sltsta -> 0x%02"PRIx16"\n",
                        sltsta);
    }

    /*
     * If the slot is populated, power indicator is off and power
     * controller is off, it is safe to detach the devices.
     *
     * Note: don't detach if condition was already true:
     * this is a work around for guests that overwrite
     * control of powered off slots before powering them on.
     */
    if ((sltsta & PCI_EXP_SLTSTA_PDS) && (val & PCI_EXP_SLTCTL_PCC) &&
        (val & PCI_EXP_SLTCTL_PIC_OFF) == PCI_EXP_SLTCTL_PIC_OFF &&
        (!(old_slt_ctl & PCI_EXP_SLTCTL_PCC) ||
        (old_slt_ctl & PCI_EXP_SLTCTL_PIC_OFF) != PCI_EXP_SLTCTL_PIC_OFF)) {
        pcie_cap_slot_do_unplug(dev);
    }
    pcie_cap_update_power(dev);

    hotplug_event_notify(dev);

    /* 
     * 6.7.3.2 Command Completed Events
     *
     * Software issues a command to a hot-plug capable Downstream Port by
     * issuing a write transaction that targets any portion of the Port’s Slot
     * Control register. A single write to the Slot Control register is
     * considered to be a single command, even if the write affects more than
     * one field in the Slot Control register. In response to this transaction,
     * the Port must carry out the requested actions and then set the
     * associated status field for the command completed event. */

    /* Real hardware might take a while to complete requested command because
     * physical movement would be involved like locking the electromechanical
     * lock.  However in our case, command is completed instantaneously above,
     * so send a command completion event right now.
     */
    pcie_cap_slot_event(dev, PCI_EXP_HP_EV_CCI);
}

int pcie_cap_slot_post_load(void *opaque, int version_id)
{
    PCIDevice *dev = opaque;
    hotplug_event_update_event_status(dev);
    pcie_cap_update_power(dev);
    return 0;
}

void pcie_cap_slot_push_attention_button(PCIDevice *dev)
{
    pcie_cap_slot_event(dev, PCI_EXP_HP_EV_ABP);
}

/* root control/capabilities/status. PME isn't emulated for now */
void pcie_cap_root_init(PCIDevice *dev)
{
    pci_set_word(dev->wmask + dev->exp.exp_cap + PCI_EXP_RTCTL,
                 PCI_EXP_RTCTL_SECEE | PCI_EXP_RTCTL_SENFEE |
                 PCI_EXP_RTCTL_SEFEE);
}

void pcie_cap_root_reset(PCIDevice *dev)
{
    pci_set_word(dev->config + dev->exp.exp_cap + PCI_EXP_RTCTL, 0);
}

/* function level reset(FLR) */
void pcie_cap_flr_init(PCIDevice *dev)
{
    pci_long_test_and_set_mask(dev->config + dev->exp.exp_cap + PCI_EXP_DEVCAP,
                               PCI_EXP_DEVCAP_FLR);

    /* Although reading BCR_FLR returns always 0,
     * the bit is made writable here in order to detect the 1b is written
     * pcie_cap_flr_write_config() test-and-clear the bit, so
     * this bit always returns 0 to the guest.
     */
    pci_word_test_and_set_mask(dev->wmask + dev->exp.exp_cap + PCI_EXP_DEVCTL,
                               PCI_EXP_DEVCTL_BCR_FLR);
}

void pcie_cap_flr_write_config(PCIDevice *dev,
                               uint32_t addr, uint32_t val, int len)
{
    uint8_t *devctl = dev->config + dev->exp.exp_cap + PCI_EXP_DEVCTL;
    if (pci_get_word(devctl) & PCI_EXP_DEVCTL_BCR_FLR) {
        /* Clear PCI_EXP_DEVCTL_BCR_FLR after invoking the reset handler
           so the handler can detect FLR by looking at this bit. */
        pci_device_reset(dev);
        pci_word_test_and_clear_mask(devctl, PCI_EXP_DEVCTL_BCR_FLR);
    }
}

/* Alternative Routing-ID Interpretation (ARI)
 * forwarding support for root and downstream ports
 */
void pcie_cap_arifwd_init(PCIDevice *dev)
{
    uint32_t pos = dev->exp.exp_cap;
    pci_long_test_and_set_mask(dev->config + pos + PCI_EXP_DEVCAP2,
                               PCI_EXP_DEVCAP2_ARI);
    pci_long_test_and_set_mask(dev->wmask + pos + PCI_EXP_DEVCTL2,
                               PCI_EXP_DEVCTL2_ARI);
}

void pcie_cap_arifwd_reset(PCIDevice *dev)
{
    uint8_t *devctl2 = dev->config + dev->exp.exp_cap + PCI_EXP_DEVCTL2;
    pci_long_test_and_clear_mask(devctl2, PCI_EXP_DEVCTL2_ARI);
}

bool pcie_cap_is_arifwd_enabled(const PCIDevice *dev)
{
    if (!pci_is_express(dev)) {
        return false;
    }
    if (!dev->exp.exp_cap) {
        return false;
    }

    return pci_get_long(dev->config + dev->exp.exp_cap + PCI_EXP_DEVCTL2) &
        PCI_EXP_DEVCTL2_ARI;
}

/**************************************************************************
 * pci express extended capability list management functions
 * uint16_t ext_cap_id (16 bit)
 * uint8_t cap_ver (4 bit)
 * uint16_t cap_offset (12 bit)
 * uint16_t ext_cap_size
 */

/* Passing a cap_id value > 0xffff will return 0 and put end of list in prev */
static uint16_t pcie_find_capability_list(PCIDevice *dev, uint32_t cap_id,
                                          uint16_t *prev_p)
{
    uint16_t prev = 0;
    uint16_t next;
    uint32_t header = pci_get_long(dev->config + PCI_CONFIG_SPACE_SIZE);

    if (!header) {
        /* no extended capability */
        next = 0;
        goto out;
    }
    for (next = PCI_CONFIG_SPACE_SIZE; next;
         prev = next, next = PCI_EXT_CAP_NEXT(header)) {

        assert(next >= PCI_CONFIG_SPACE_SIZE);
        assert(next <= PCIE_CONFIG_SPACE_SIZE - 8);

        header = pci_get_long(dev->config + next);
        if (PCI_EXT_CAP_ID(header) == cap_id) {
            break;
        }
    }

out:
    if (prev_p) {
        *prev_p = prev;
    }
    return next;
}

uint16_t pcie_find_capability(PCIDevice *dev, uint16_t cap_id)
{
    return pcie_find_capability_list(dev, cap_id, NULL);
}

static void pcie_ext_cap_set_next(PCIDevice *dev, uint16_t pos, uint16_t next)
{
    uint32_t header = pci_get_long(dev->config + pos);
    assert(!(next & (PCI_EXT_CAP_ALIGN - 1)));
    header = (header & ~PCI_EXT_CAP_NEXT_MASK) |
        ((next << PCI_EXT_CAP_NEXT_SHIFT) & PCI_EXT_CAP_NEXT_MASK);
    pci_set_long(dev->config + pos, header);
}

/*
 * Caller must supply valid (offset, size) such that the range wouldn't
 * overlap with other capability or other registers.
 * This function doesn't check it.
 */
void pcie_add_capability(PCIDevice *dev,
                         uint16_t cap_id, uint8_t cap_ver,
                         uint16_t offset, uint16_t size)
{
    assert(offset >= PCI_CONFIG_SPACE_SIZE);
    assert(offset < (uint16_t)(offset + size));
    assert((uint16_t)(offset + size) <= PCIE_CONFIG_SPACE_SIZE);
    assert(size >= 8);
    assert(pci_is_express(dev));

    if (offset != PCI_CONFIG_SPACE_SIZE) {
        uint16_t prev;

        /*
         * 0xffffffff is not a valid cap id (it's a 16 bit field). use
         * internally to find the last capability in the linked list.
         */
        pcie_find_capability_list(dev, 0xffffffff, &prev);
        assert(prev >= PCI_CONFIG_SPACE_SIZE);
        pcie_ext_cap_set_next(dev, prev, offset);
    }
    pci_set_long(dev->config + offset, PCI_EXT_CAP(cap_id, cap_ver, 0));

    /* Make capability read-only by default */
    memset(dev->wmask + offset, 0, size);
    memset(dev->w1cmask + offset, 0, size);
    /* Check capability by default */
    memset(dev->cmask + offset, 0xFF, size);
}

/*
 * Sync the PCIe Link Status negotiated speed and width of a bridge with the
 * downstream device.  If downstream device is not present, re-write with the
 * Link Capability fields.  If downstream device reports invalid width or
 * speed, replace with minimum values (LnkSta fields are RsvdZ on VFs but such
 * values interfere with PCIe native hotplug detecting new devices).  Limit
 * width and speed to bridge capabilities for compatibility.  Use config_read
 * to access the downstream device since it could be an assigned device with
 * volatile link information.
 */
void pcie_sync_bridge_lnk(PCIDevice *bridge_dev)
{
    PCIBridge *br = PCI_BRIDGE(bridge_dev);
    PCIBus *bus = pci_bridge_get_sec_bus(br);
    PCIDevice *target = bus->devices[0];
    uint8_t *exp_cap = bridge_dev->config + bridge_dev->exp.exp_cap;
    uint16_t lnksta, lnkcap = pci_get_word(exp_cap + PCI_EXP_LNKCAP);

    if (!target || !target->exp.exp_cap) {
        lnksta = lnkcap;
    } else {
        lnksta = target->config_read(target,
                                     target->exp.exp_cap + PCI_EXP_LNKSTA,
                                     sizeof(lnksta));

        if ((lnksta & PCI_EXP_LNKSTA_NLW) > (lnkcap & PCI_EXP_LNKCAP_MLW)) {
            lnksta &= ~PCI_EXP_LNKSTA_NLW;
            lnksta |= lnkcap & PCI_EXP_LNKCAP_MLW;
        } else if (!(lnksta & PCI_EXP_LNKSTA_NLW)) {
            lnksta |= QEMU_PCI_EXP_LNKSTA_NLW(QEMU_PCI_EXP_LNK_X1);
        }

        if ((lnksta & PCI_EXP_LNKSTA_CLS) > (lnkcap & PCI_EXP_LNKCAP_SLS)) {
            lnksta &= ~PCI_EXP_LNKSTA_CLS;
            lnksta |= lnkcap & PCI_EXP_LNKCAP_SLS;
        } else if (!(lnksta & PCI_EXP_LNKSTA_CLS)) {
            lnksta |= QEMU_PCI_EXP_LNKSTA_CLS(QEMU_PCI_EXP_LNK_2_5GT);
        }
    }

    pci_word_test_and_clear_mask(exp_cap + PCI_EXP_LNKSTA,
                                 PCI_EXP_LNKSTA_CLS | PCI_EXP_LNKSTA_NLW);
    pci_word_test_and_set_mask(exp_cap + PCI_EXP_LNKSTA, lnksta &
                               (PCI_EXP_LNKSTA_CLS | PCI_EXP_LNKSTA_NLW));
}

/**************************************************************************
 * pci express extended capability helper functions
 */

/* ARI */
void pcie_ari_init(PCIDevice *dev, uint16_t offset, uint16_t nextfn)
{
    pcie_add_capability(dev, PCI_EXT_CAP_ID_ARI, PCI_ARI_VER,
                        offset, PCI_ARI_SIZEOF);
    pci_set_long(dev->config + offset + PCI_ARI_CAP, (nextfn & 0xff) << 8);
}

void pcie_dev_ser_num_init(PCIDevice *dev, uint16_t offset, uint64_t ser_num)
{
    static const int pci_dsn_ver = 1;
    static const int pci_dsn_cap = 4;

    pcie_add_capability(dev, PCI_EXT_CAP_ID_DSN, pci_dsn_ver, offset,
                        PCI_EXT_CAP_DSN_SIZEOF);
    pci_set_quad(dev->config + offset + pci_dsn_cap, ser_num);
}

void pcie_ats_init(PCIDevice *dev, uint16_t offset, bool aligned)
{
    pcie_add_capability(dev, PCI_EXT_CAP_ID_ATS, 0x1,
                        offset, PCI_EXT_CAP_ATS_SIZEOF);

    dev->exp.ats_cap = offset;

    /* Invalidate Queue Depth 0 */
    if (aligned) {
        pci_set_word(dev->config + offset + PCI_ATS_CAP,
                     PCI_ATS_CAP_PAGE_ALIGNED);
    }
    /* STU 0, Disabled by default */
    pci_set_word(dev->config + offset + PCI_ATS_CTRL, 0);

    pci_set_word(dev->wmask + dev->exp.ats_cap + PCI_ATS_CTRL, 0x800f);
}

/* ACS (Access Control Services) */
void pcie_acs_init(PCIDevice *dev, uint16_t offset)
{
    bool is_downstream = pci_is_express_downstream_port(dev);
    uint16_t cap_bits = 0;

    /* For endpoints, only multifunction devs may have an ACS capability: */
    assert(is_downstream ||
           (dev->cap_present & QEMU_PCI_CAP_MULTIFUNCTION) ||
           PCI_FUNC(dev->devfn));

    pcie_add_capability(dev, PCI_EXT_CAP_ID_ACS, PCI_ACS_VER, offset,
                        PCI_ACS_SIZEOF);
    dev->exp.acs_cap = offset;

    if (is_downstream) {
        /*
         * Downstream ports must implement SV, TB, RR, CR, UF, and DT (with
         * caveats on the latter four that we ignore for simplicity).
         * Endpoints may also implement a subset of ACS capabilities,
         * but these are optional if the endpoint does not support
         * peer-to-peer between functions and thus omitted here.
         */
        cap_bits = PCI_ACS_SV | PCI_ACS_TB | PCI_ACS_RR |
            PCI_ACS_CR | PCI_ACS_UF | PCI_ACS_DT;
    }

    pci_set_word(dev->config + offset + PCI_ACS_CAP, cap_bits);
    pci_set_word(dev->wmask + offset + PCI_ACS_CTRL, cap_bits);
}

void pcie_acs_reset(PCIDevice *dev)
{
    if (dev->exp.acs_cap) {
        pci_set_word(dev->config + dev->exp.acs_cap + PCI_ACS_CTRL, 0);
    }
}
