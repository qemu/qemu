/*
 * QEMU ICH9 Emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2009, 2010, 2011
 *               Isaku Yamahata <yamahata at valinux co jp>
 *               VA Linux Systems Japan K.K.
 * Copyright (C) 2012 Jason Baron <jbaron@redhat.com>
 *
 * This is based on piix.c, but heavily modified.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "qapi/visitor.h"
#include "qemu/range.h"
#include "hw/isa/isa.h"
#include "hw/sysbus.h"
#include "hw/i386/pc.h"
#include "hw/isa/apm.h"
#include "hw/i386/ioapic.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci/pci_bridge.h"
#include "hw/i386/ich9.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/ich9.h"
#include "hw/pci/pci_bus.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "qom/cpu.h"
#include "hw/nvram/fw_cfg.h"
#include "qemu/cutils.h"

/*****************************************************************************/
/* ICH9 LPC PCI to ISA bridge */

static void ich9_lpc_reset(DeviceState *qdev);

/* chipset configuration register
 * to access chipset configuration registers, pci_[sg]et_{byte, word, long}
 * are used.
 * Although it's not pci configuration space, it's little endian as Intel.
 */

static void ich9_cc_update_ir(uint8_t irr[PCI_NUM_PINS], uint16_t ir)
{
    int intx;
    for (intx = 0; intx < PCI_NUM_PINS; intx++) {
        irr[intx] = (ir >> (intx * ICH9_CC_DIR_SHIFT)) & ICH9_CC_DIR_MASK;
    }
}

static void ich9_cc_update(ICH9LPCState *lpc)
{
    int slot;
    int pci_intx;

    const int reg_offsets[] = {
        ICH9_CC_D25IR,
        ICH9_CC_D26IR,
        ICH9_CC_D27IR,
        ICH9_CC_D28IR,
        ICH9_CC_D29IR,
        ICH9_CC_D30IR,
        ICH9_CC_D31IR,
    };
    const int *offset;

    /* D{25 - 31}IR, but D30IR is read only to 0. */
    for (slot = 25, offset = reg_offsets; slot < 32; slot++, offset++) {
        if (slot == 30) {
            continue;
        }
        ich9_cc_update_ir(lpc->irr[slot],
                          pci_get_word(lpc->chip_config + *offset));
    }

    /*
     * D30: DMI2PCI bridge
     * It is arbitrarily decided how INTx lines of PCI devices behind
     * the bridge are connected to pirq lines. Our choice is PIRQ[E-H].
     * INT[A-D] are connected to PIRQ[E-H]
     */
    for (pci_intx = 0; pci_intx < PCI_NUM_PINS; pci_intx++) {
        lpc->irr[30][pci_intx] = pci_intx + 4;
    }
}

static void ich9_cc_init(ICH9LPCState *lpc)
{
    int slot;
    int intx;

    /* the default irq routing is arbitrary as long as it matches with
     * acpi irq routing table.
     * The one that is incompatible with piix_pci(= bochs) one is
     * intentionally chosen to let the users know that the different
     * board is used.
     *
     * int[A-D] -> pirq[E-F]
     * avoid pirq A-D because they are used for pci express port
     */
    for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
        for (intx = 0; intx < PCI_NUM_PINS; intx++) {
            lpc->irr[slot][intx] = (slot + intx) % 4 + 4;
        }
    }
    ich9_cc_update(lpc);
}

static void ich9_cc_reset(ICH9LPCState *lpc)
{
    uint8_t *c = lpc->chip_config;

    memset(lpc->chip_config, 0, sizeof(lpc->chip_config));

    pci_set_long(c + ICH9_CC_D31IR, ICH9_CC_DIR_DEFAULT);
    pci_set_long(c + ICH9_CC_D30IR, ICH9_CC_D30IR_DEFAULT);
    pci_set_long(c + ICH9_CC_D29IR, ICH9_CC_DIR_DEFAULT);
    pci_set_long(c + ICH9_CC_D28IR, ICH9_CC_DIR_DEFAULT);
    pci_set_long(c + ICH9_CC_D27IR, ICH9_CC_DIR_DEFAULT);
    pci_set_long(c + ICH9_CC_D26IR, ICH9_CC_DIR_DEFAULT);
    pci_set_long(c + ICH9_CC_D25IR, ICH9_CC_DIR_DEFAULT);
    pci_set_long(c + ICH9_CC_GCS, ICH9_CC_GCS_DEFAULT);

    ich9_cc_update(lpc);
}

static void ich9_cc_addr_len(uint64_t *addr, unsigned *len)
{
    *addr &= ICH9_CC_ADDR_MASK;
    if (*addr + *len >= ICH9_CC_SIZE) {
        *len = ICH9_CC_SIZE - *addr;
    }
}

/* val: little endian */
static void ich9_cc_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned len)
{
    ICH9LPCState *lpc = (ICH9LPCState *)opaque;

    ich9_cc_addr_len(&addr, &len);
    memcpy(lpc->chip_config + addr, &val, len);
    pci_bus_fire_intx_routing_notifier(lpc->d.bus);
    ich9_cc_update(lpc);
}

/* return value: little endian */
static uint64_t ich9_cc_read(void *opaque, hwaddr addr,
                              unsigned len)
{
    ICH9LPCState *lpc = (ICH9LPCState *)opaque;

    uint32_t val = 0;
    ich9_cc_addr_len(&addr, &len);
    memcpy(&val, lpc->chip_config + addr, len);
    return val;
}

/* IRQ routing */
/* */
static void ich9_lpc_rout(uint8_t pirq_rout, int *pic_irq, int *pic_dis)
{
    *pic_irq = pirq_rout & ICH9_LPC_PIRQ_ROUT_MASK;
    *pic_dis = pirq_rout & ICH9_LPC_PIRQ_ROUT_IRQEN;
}

static void ich9_lpc_pic_irq(ICH9LPCState *lpc, int pirq_num,
                             int *pic_irq, int *pic_dis)
{
    switch (pirq_num) {
    case 0 ... 3: /* A-D */
        ich9_lpc_rout(lpc->d.config[ICH9_LPC_PIRQA_ROUT + pirq_num],
                      pic_irq, pic_dis);
        return;
    case 4 ... 7: /* E-H */
        ich9_lpc_rout(lpc->d.config[ICH9_LPC_PIRQE_ROUT + (pirq_num - 4)],
                      pic_irq, pic_dis);
        return;
    default:
        break;
    }
    abort();
}

/* gsi: i8259+ioapic irq 0-15, otherwise assert */
static void ich9_lpc_update_pic(ICH9LPCState *lpc, int gsi)
{
    int i, pic_level;

    assert(gsi < ICH9_LPC_PIC_NUM_PINS);

    /* The pic level is the logical OR of all the PCI irqs mapped to it */
    pic_level = 0;
    for (i = 0; i < ICH9_LPC_NB_PIRQS; i++) {
        int tmp_irq;
        int tmp_dis;
        ich9_lpc_pic_irq(lpc, i, &tmp_irq, &tmp_dis);
        if (!tmp_dis && tmp_irq == gsi) {
            pic_level |= pci_bus_get_irq_level(lpc->d.bus, i);
        }
    }
    if (gsi == lpc->sci_gsi) {
        pic_level |= lpc->sci_level;
    }

    qemu_set_irq(lpc->gsi[gsi], pic_level);
}

/* APIC mode: GSIx: PIRQ[A-H] -> GSI 16, ... no pirq shares same APIC pins. */
static int ich9_pirq_to_gsi(int pirq)
{
    return pirq + ICH9_LPC_PIC_NUM_PINS;
}

static int ich9_gsi_to_pirq(int gsi)
{
    return gsi - ICH9_LPC_PIC_NUM_PINS;
}

/* gsi: ioapic irq 16-23, otherwise assert */
static void ich9_lpc_update_apic(ICH9LPCState *lpc, int gsi)
{
    int level = 0;

    assert(gsi >= ICH9_LPC_PIC_NUM_PINS);

    level |= pci_bus_get_irq_level(lpc->d.bus, ich9_gsi_to_pirq(gsi));
    if (gsi == lpc->sci_gsi) {
        level |= lpc->sci_level;
    }

    qemu_set_irq(lpc->gsi[gsi], level);
}

void ich9_lpc_set_irq(void *opaque, int pirq, int level)
{
    ICH9LPCState *lpc = opaque;
    int pic_irq, pic_dis;

    assert(0 <= pirq);
    assert(pirq < ICH9_LPC_NB_PIRQS);

    ich9_lpc_update_apic(lpc, ich9_pirq_to_gsi(pirq));
    ich9_lpc_pic_irq(lpc, pirq, &pic_irq, &pic_dis);
    ich9_lpc_update_pic(lpc, pic_irq);
}

/* return the pirq number (PIRQ[A-H]:0-7) corresponding to
 * a given device irq pin.
 */
int ich9_lpc_map_irq(PCIDevice *pci_dev, int intx)
{
    BusState *bus = qdev_get_parent_bus(&pci_dev->qdev);
    PCIBus *pci_bus = PCI_BUS(bus);
    PCIDevice *lpc_pdev =
            pci_bus->devices[PCI_DEVFN(ICH9_LPC_DEV, ICH9_LPC_FUNC)];
    ICH9LPCState *lpc = ICH9_LPC_DEVICE(lpc_pdev);

    return lpc->irr[PCI_SLOT(pci_dev->devfn)][intx];
}

PCIINTxRoute ich9_route_intx_pin_to_irq(void *opaque, int pirq_pin)
{
    ICH9LPCState *lpc = opaque;
    PCIINTxRoute route;
    int pic_irq;
    int pic_dis;

    assert(0 <= pirq_pin);
    assert(pirq_pin < ICH9_LPC_NB_PIRQS);

    route.mode = PCI_INTX_ENABLED;
    ich9_lpc_pic_irq(lpc, pirq_pin, &pic_irq, &pic_dis);
    if (!pic_dis) {
        if (pic_irq < ICH9_LPC_PIC_NUM_PINS) {
            route.irq = pic_irq;
        } else {
            route.mode = PCI_INTX_DISABLED;
            route.irq = -1;
        }
    } else {
        route.irq = ich9_pirq_to_gsi(pirq_pin);
    }

    return route;
}

void ich9_generate_smi(void)
{
    cpu_interrupt(first_cpu, CPU_INTERRUPT_SMI);
}

static int ich9_lpc_sci_irq(ICH9LPCState *lpc)
{
    switch (lpc->d.config[ICH9_LPC_ACPI_CTRL] &
            ICH9_LPC_ACPI_CTRL_SCI_IRQ_SEL_MASK) {
    case ICH9_LPC_ACPI_CTRL_9:
        return 9;
    case ICH9_LPC_ACPI_CTRL_10:
        return 10;
    case ICH9_LPC_ACPI_CTRL_11:
        return 11;
    case ICH9_LPC_ACPI_CTRL_20:
        return 20;
    case ICH9_LPC_ACPI_CTRL_21:
        return 21;
    default:
        /* reserved */
        break;
    }
    return -1;
}

static void ich9_set_sci(void *opaque, int irq_num, int level)
{
    ICH9LPCState *lpc = opaque;
    int irq;

    assert(irq_num == 0);
    level = !!level;
    if (level == lpc->sci_level) {
        return;
    }
    lpc->sci_level = level;

    irq = lpc->sci_gsi;
    if (irq < 0) {
        return;
    }

    if (irq >= ICH9_LPC_PIC_NUM_PINS) {
        ich9_lpc_update_apic(lpc, irq);
    } else {
        ich9_lpc_update_pic(lpc, irq);
    }
}

static void smi_features_ok_callback(void *opaque)
{
    ICH9LPCState *lpc = opaque;
    uint64_t guest_features;

    if (lpc->smi_features_ok) {
        /* negotiation already complete, features locked */
        return;
    }

    memcpy(&guest_features, lpc->smi_guest_features_le, sizeof guest_features);
    le64_to_cpus(&guest_features);
    if (guest_features & ~lpc->smi_host_features) {
        /* guest requests invalid features, leave @features_ok at zero */
        return;
    }

    /* valid feature subset requested, lock it down, report success */
    lpc->smi_negotiated_features = guest_features;
    lpc->smi_features_ok = 1;
}

void ich9_lpc_pm_init(PCIDevice *lpc_pci, bool smm_enabled)
{
    ICH9LPCState *lpc = ICH9_LPC_DEVICE(lpc_pci);
    qemu_irq sci_irq;
    FWCfgState *fw_cfg = fw_cfg_find();

    sci_irq = qemu_allocate_irq(ich9_set_sci, lpc, 0);
    ich9_pm_init(lpc_pci, &lpc->pm, smm_enabled, sci_irq);

    if (lpc->smi_host_features && fw_cfg) {
        uint64_t host_features_le;

        host_features_le = cpu_to_le64(lpc->smi_host_features);
        memcpy(lpc->smi_host_features_le, &host_features_le,
               sizeof host_features_le);
        fw_cfg_add_file(fw_cfg, "etc/smi/supported-features",
                        lpc->smi_host_features_le,
                        sizeof lpc->smi_host_features_le);

        /* The other two guest-visible fields are cleared on device reset, we
         * just link them into fw_cfg here.
         */
        fw_cfg_add_file_callback(fw_cfg, "etc/smi/requested-features",
                                 NULL, NULL,
                                 lpc->smi_guest_features_le,
                                 sizeof lpc->smi_guest_features_le,
                                 false);
        fw_cfg_add_file_callback(fw_cfg, "etc/smi/features-ok",
                                 smi_features_ok_callback, lpc,
                                 &lpc->smi_features_ok,
                                 sizeof lpc->smi_features_ok,
                                 true);
    }

    ich9_lpc_reset(&lpc->d.qdev);
}

/* APM */

static void ich9_apm_ctrl_changed(uint32_t val, void *arg)
{
    ICH9LPCState *lpc = arg;

    /* ACPI specs 3.0, 4.7.2.5 */
    acpi_pm1_cnt_update(&lpc->pm.acpi_regs,
                        val == ICH9_APM_ACPI_ENABLE,
                        val == ICH9_APM_ACPI_DISABLE);
    if (val == ICH9_APM_ACPI_ENABLE || val == ICH9_APM_ACPI_DISABLE) {
        return;
    }

    /* SMI_EN = PMBASE + 30. SMI control and enable register */
    if (lpc->pm.smi_en & ICH9_PMIO_SMI_EN_APMC_EN) {
        if (lpc->smi_negotiated_features &
            (UINT64_C(1) << ICH9_LPC_SMI_F_BROADCAST_BIT)) {
            CPUState *cs;
            CPU_FOREACH(cs) {
                cpu_interrupt(cs, CPU_INTERRUPT_SMI);
            }
        } else {
            cpu_interrupt(current_cpu, CPU_INTERRUPT_SMI);
        }
    }
}

/* config:PMBASE */
static void
ich9_lpc_pmbase_sci_update(ICH9LPCState *lpc)
{
    uint32_t pm_io_base = pci_get_long(lpc->d.config + ICH9_LPC_PMBASE);
    uint8_t acpi_cntl = pci_get_long(lpc->d.config + ICH9_LPC_ACPI_CTRL);
    uint8_t new_gsi;

    if (acpi_cntl & ICH9_LPC_ACPI_CTRL_ACPI_EN) {
        pm_io_base &= ICH9_LPC_PMBASE_BASE_ADDRESS_MASK;
    } else {
        pm_io_base = 0;
    }

    ich9_pm_iospace_update(&lpc->pm, pm_io_base);

    new_gsi = ich9_lpc_sci_irq(lpc);
    if (lpc->sci_level && new_gsi != lpc->sci_gsi) {
        qemu_set_irq(lpc->pm.irq, 0);
        lpc->sci_gsi = new_gsi;
        qemu_set_irq(lpc->pm.irq, 1);
    }
    lpc->sci_gsi = new_gsi;
}

/* config:RCBA */
static void ich9_lpc_rcba_update(ICH9LPCState *lpc, uint32_t rcba_old)
{
    uint32_t rcba = pci_get_long(lpc->d.config + ICH9_LPC_RCBA);

    if (rcba_old & ICH9_LPC_RCBA_EN) {
        memory_region_del_subregion(get_system_memory(), &lpc->rcrb_mem);
    }
    if (rcba & ICH9_LPC_RCBA_EN) {
        memory_region_add_subregion_overlap(get_system_memory(),
                                            rcba & ICH9_LPC_RCBA_BA_MASK,
                                            &lpc->rcrb_mem, 1);
    }
}

/* config:GEN_PMCON* */
static void
ich9_lpc_pmcon_update(ICH9LPCState *lpc)
{
    uint16_t gen_pmcon_1 = pci_get_word(lpc->d.config + ICH9_LPC_GEN_PMCON_1);
    uint16_t wmask;

    if (gen_pmcon_1 & ICH9_LPC_GEN_PMCON_1_SMI_LOCK) {
        wmask = pci_get_word(lpc->d.wmask + ICH9_LPC_GEN_PMCON_1);
        wmask &= ~ICH9_LPC_GEN_PMCON_1_SMI_LOCK;
        pci_set_word(lpc->d.wmask + ICH9_LPC_GEN_PMCON_1, wmask);
        lpc->pm.smi_en_wmask &= ~1;
    }
}

static int ich9_lpc_post_load(void *opaque, int version_id)
{
    ICH9LPCState *lpc = opaque;

    ich9_lpc_pmbase_sci_update(lpc);
    ich9_lpc_rcba_update(lpc, 0 /* disabled ICH9_LPC_RCBA_EN */);
    ich9_lpc_pmcon_update(lpc);
    return 0;
}

static void ich9_lpc_config_write(PCIDevice *d,
                                  uint32_t addr, uint32_t val, int len)
{
    ICH9LPCState *lpc = ICH9_LPC_DEVICE(d);
    uint32_t rcba_old = pci_get_long(d->config + ICH9_LPC_RCBA);

    pci_default_write_config(d, addr, val, len);
    if (ranges_overlap(addr, len, ICH9_LPC_PMBASE, 4) ||
        ranges_overlap(addr, len, ICH9_LPC_ACPI_CTRL, 1)) {
        ich9_lpc_pmbase_sci_update(lpc);
    }
    if (ranges_overlap(addr, len, ICH9_LPC_RCBA, 4)) {
        ich9_lpc_rcba_update(lpc, rcba_old);
    }
    if (ranges_overlap(addr, len, ICH9_LPC_PIRQA_ROUT, 4)) {
        pci_bus_fire_intx_routing_notifier(lpc->d.bus);
    }
    if (ranges_overlap(addr, len, ICH9_LPC_PIRQE_ROUT, 4)) {
        pci_bus_fire_intx_routing_notifier(lpc->d.bus);
    }
    if (ranges_overlap(addr, len, ICH9_LPC_GEN_PMCON_1, 8)) {
        ich9_lpc_pmcon_update(lpc);
    }
}

static void ich9_lpc_reset(DeviceState *qdev)
{
    PCIDevice *d = PCI_DEVICE(qdev);
    ICH9LPCState *lpc = ICH9_LPC_DEVICE(d);
    uint32_t rcba_old = pci_get_long(d->config + ICH9_LPC_RCBA);
    int i;

    for (i = 0; i < 4; i++) {
        pci_set_byte(d->config + ICH9_LPC_PIRQA_ROUT + i,
                     ICH9_LPC_PIRQ_ROUT_DEFAULT);
    }
    for (i = 0; i < 4; i++) {
        pci_set_byte(d->config + ICH9_LPC_PIRQE_ROUT + i,
                     ICH9_LPC_PIRQ_ROUT_DEFAULT);
    }
    pci_set_byte(d->config + ICH9_LPC_ACPI_CTRL, ICH9_LPC_ACPI_CTRL_DEFAULT);

    pci_set_long(d->config + ICH9_LPC_PMBASE, ICH9_LPC_PMBASE_DEFAULT);
    pci_set_long(d->config + ICH9_LPC_RCBA, ICH9_LPC_RCBA_DEFAULT);

    ich9_cc_reset(lpc);

    ich9_lpc_pmbase_sci_update(lpc);
    ich9_lpc_rcba_update(lpc, rcba_old);

    lpc->sci_level = 0;
    lpc->rst_cnt = 0;

    memset(lpc->smi_guest_features_le, 0, sizeof lpc->smi_guest_features_le);
    lpc->smi_features_ok = 0;
    lpc->smi_negotiated_features = 0;
}

/* root complex register block is mapped into memory space */
static const MemoryRegionOps rcrb_mmio_ops = {
    .read = ich9_cc_read,
    .write = ich9_cc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void ich9_lpc_machine_ready(Notifier *n, void *opaque)
{
    ICH9LPCState *s = container_of(n, ICH9LPCState, machine_ready);
    MemoryRegion *io_as = pci_address_space_io(&s->d);
    uint8_t *pci_conf;

    pci_conf = s->d.config;
    if (memory_region_present(io_as, 0x3f8)) {
        /* com1 */
        pci_conf[0x82] |= 0x01;
    }
    if (memory_region_present(io_as, 0x2f8)) {
        /* com2 */
        pci_conf[0x82] |= 0x02;
    }
    if (memory_region_present(io_as, 0x378)) {
        /* lpt */
        pci_conf[0x82] |= 0x04;
    }
    if (memory_region_present(io_as, 0x3f2)) {
        /* floppy */
        pci_conf[0x82] |= 0x08;
    }
}

/* reset control */
static void ich9_rst_cnt_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned len)
{
    ICH9LPCState *lpc = opaque;

    if (val & 4) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        return;
    }
    lpc->rst_cnt = val & 0xA; /* keep FULL_RST (bit 3) and SYS_RST (bit 1) */
}

static uint64_t ich9_rst_cnt_read(void *opaque, hwaddr addr, unsigned len)
{
    ICH9LPCState *lpc = opaque;

    return lpc->rst_cnt;
}

static const MemoryRegionOps ich9_rst_cnt_ops = {
    .read = ich9_rst_cnt_read,
    .write = ich9_rst_cnt_write,
    .endianness = DEVICE_LITTLE_ENDIAN
};

Object *ich9_lpc_find(void)
{
    bool ambig;
    Object *o = object_resolve_path_type("", TYPE_ICH9_LPC_DEVICE, &ambig);

    if (ambig) {
        return NULL;
    }
    return o;
}

static void ich9_lpc_get_sci_int(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    ICH9LPCState *lpc = ICH9_LPC_DEVICE(obj);
    uint32_t value = lpc->sci_gsi;

    visit_type_uint32(v, name, &value, errp);
}

static void ich9_lpc_add_properties(ICH9LPCState *lpc)
{
    static const uint8_t acpi_enable_cmd = ICH9_APM_ACPI_ENABLE;
    static const uint8_t acpi_disable_cmd = ICH9_APM_ACPI_DISABLE;

    object_property_add(OBJECT(lpc), ACPI_PM_PROP_SCI_INT, "uint32",
                        ich9_lpc_get_sci_int,
                        NULL, NULL, NULL, NULL);
    object_property_add_uint8_ptr(OBJECT(lpc), ACPI_PM_PROP_ACPI_ENABLE_CMD,
                                  &acpi_enable_cmd, NULL);
    object_property_add_uint8_ptr(OBJECT(lpc), ACPI_PM_PROP_ACPI_DISABLE_CMD,
                                  &acpi_disable_cmd, NULL);

    ich9_pm_add_properties(OBJECT(lpc), &lpc->pm, NULL);
}

static void ich9_lpc_initfn(Object *obj)
{
    ICH9LPCState *lpc = ICH9_LPC_DEVICE(obj);

    ich9_lpc_add_properties(lpc);
}

static void ich9_lpc_realize(PCIDevice *d, Error **errp)
{
    ICH9LPCState *lpc = ICH9_LPC_DEVICE(d);
    DeviceState *dev = DEVICE(d);
    ISABus *isa_bus;

    isa_bus = isa_bus_new(DEVICE(d), get_system_memory(), get_system_io(),
                          errp);
    if (!isa_bus) {
        return;
    }

    pci_set_long(d->wmask + ICH9_LPC_PMBASE,
                 ICH9_LPC_PMBASE_BASE_ADDRESS_MASK);
    pci_set_byte(d->wmask + ICH9_LPC_PMBASE,
                 ICH9_LPC_ACPI_CTRL_ACPI_EN |
                 ICH9_LPC_ACPI_CTRL_SCI_IRQ_SEL_MASK);

    memory_region_init_io(&lpc->rcrb_mem, OBJECT(d), &rcrb_mmio_ops, lpc,
                          "lpc-rcrb-mmio", ICH9_CC_SIZE);

    lpc->isa_bus = isa_bus;

    ich9_cc_init(lpc);
    apm_init(d, &lpc->apm, ich9_apm_ctrl_changed, lpc);

    lpc->machine_ready.notify = ich9_lpc_machine_ready;
    qemu_add_machine_init_done_notifier(&lpc->machine_ready);

    memory_region_init_io(&lpc->rst_cnt_mem, OBJECT(d), &ich9_rst_cnt_ops, lpc,
                          "lpc-reset-control", 1);
    memory_region_add_subregion_overlap(pci_address_space_io(d),
                                        ICH9_RST_CNT_IOPORT, &lpc->rst_cnt_mem,
                                        1);

    qdev_init_gpio_out_named(dev, lpc->gsi, ICH9_GPIO_GSI, GSI_NUM_PINS);

    isa_bus_irqs(isa_bus, lpc->gsi);
}

static bool ich9_rst_cnt_needed(void *opaque)
{
    ICH9LPCState *lpc = opaque;

    return (lpc->rst_cnt != 0);
}

static const VMStateDescription vmstate_ich9_rst_cnt = {
    .name = "ICH9LPC/rst_cnt",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = ich9_rst_cnt_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(rst_cnt, ICH9LPCState),
        VMSTATE_END_OF_LIST()
    }
};

static bool ich9_smi_feat_needed(void *opaque)
{
    ICH9LPCState *lpc = opaque;

    return !buffer_is_zero(lpc->smi_guest_features_le,
                           sizeof lpc->smi_guest_features_le) ||
           lpc->smi_features_ok;
}

static const VMStateDescription vmstate_ich9_smi_feat = {
    .name = "ICH9LPC/smi_feat",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = ich9_smi_feat_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(smi_guest_features_le, ICH9LPCState,
                            sizeof(uint64_t)),
        VMSTATE_UINT8(smi_features_ok, ICH9LPCState),
        VMSTATE_UINT64(smi_negotiated_features, ICH9LPCState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ich9_lpc = {
    .name = "ICH9LPC",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = ich9_lpc_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(d, ICH9LPCState),
        VMSTATE_STRUCT(apm, ICH9LPCState, 0, vmstate_apm, APMState),
        VMSTATE_STRUCT(pm, ICH9LPCState, 0, vmstate_ich9_pm, ICH9LPCPMRegs),
        VMSTATE_UINT8_ARRAY(chip_config, ICH9LPCState, ICH9_CC_SIZE),
        VMSTATE_UINT32(sci_level, ICH9LPCState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_ich9_rst_cnt,
        &vmstate_ich9_smi_feat,
        NULL
    }
};

static Property ich9_lpc_properties[] = {
    DEFINE_PROP_BOOL("noreboot", ICH9LPCState, pin_strap.spkr_hi, true),
    DEFINE_PROP_BIT64("x-smi-broadcast", ICH9LPCState, smi_host_features,
                      ICH9_LPC_SMI_F_BROADCAST_BIT, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void ich9_send_gpe(AcpiDeviceIf *adev, AcpiEventStatusBits ev)
{
    ICH9LPCState *s = ICH9_LPC_DEVICE(adev);

    acpi_send_gpe_event(&s->pm.acpi_regs, s->pm.irq, ev);
}

static void ich9_lpc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->reset = ich9_lpc_reset;
    k->realize = ich9_lpc_realize;
    dc->vmsd = &vmstate_ich9_lpc;
    dc->props = ich9_lpc_properties;
    k->config_write = ich9_lpc_config_write;
    dc->desc = "ICH9 LPC bridge";
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_ICH9_8;
    k->revision = ICH9_A2_LPC_REVISION;
    k->class_id = PCI_CLASS_BRIDGE_ISA;
    /*
     * Reason: part of ICH9 southbridge, needs to be wired up by
     * pc_q35_init()
     */
    dc->user_creatable = false;
    hc->plug = ich9_pm_device_plug_cb;
    hc->unplug_request = ich9_pm_device_unplug_request_cb;
    hc->unplug = ich9_pm_device_unplug_cb;
    adevc->ospm_status = ich9_pm_ospm_status;
    adevc->send_event = ich9_send_gpe;
    adevc->madt_cpu = pc_madt_cpu_entry;
}

static const TypeInfo ich9_lpc_info = {
    .name       = TYPE_ICH9_LPC_DEVICE,
    .parent     = TYPE_PCI_DEVICE,
    .instance_size = sizeof(struct ICH9LPCState),
    .instance_init = ich9_lpc_initfn,
    .class_init  = ich9_lpc_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { TYPE_ACPI_DEVICE_IF },
        { }
    }
};

static void ich9_lpc_register(void)
{
    type_register_static(&ich9_lpc_info);
}

type_init(ich9_lpc_register);
