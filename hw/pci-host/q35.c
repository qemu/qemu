/*
 * QEMU MCH/ICH9 PCI Bridge Emulation
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
#include "qemu/log.h"
#include "hw/i386/pc.h"
#include "hw/pci-host/q35.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/module.h"

/****************************************************************************
 * Q35 host
 */

#define Q35_PCI_HOST_HOLE64_SIZE_DEFAULT (1ULL << 35)

static void q35_host_realize(DeviceState *dev, Error **errp)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    Q35PCIHost *s = Q35_HOST_DEVICE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_add_subregion(s->mch.address_space_io,
                                MCH_HOST_BRIDGE_CONFIG_ADDR, &pci->conf_mem);
    sysbus_init_ioports(sbd, MCH_HOST_BRIDGE_CONFIG_ADDR, 4);

    memory_region_add_subregion(s->mch.address_space_io,
                                MCH_HOST_BRIDGE_CONFIG_DATA, &pci->data_mem);
    sysbus_init_ioports(sbd, MCH_HOST_BRIDGE_CONFIG_DATA, 4);

    /* register q35 0xcf8 port as coalesced pio */
    memory_region_set_flush_coalesced(&pci->data_mem);
    memory_region_add_coalescing(&pci->conf_mem, 0, 4);

    pci->bus = pci_root_bus_new(DEVICE(s), "pcie.0",
                                s->mch.pci_address_space,
                                s->mch.address_space_io,
                                0, TYPE_PCIE_BUS);

    qdev_realize(DEVICE(&s->mch), BUS(pci->bus), &error_fatal);
}

static const char *q35_host_root_bus_path(PCIHostState *host_bridge,
                                          PCIBus *rootbus)
{
    return "0000:00";
}

static void q35_host_get_pci_hole_start(Object *obj, Visitor *v,
                                        const char *name, void *opaque,
                                        Error **errp)
{
    Q35PCIHost *s = Q35_HOST_DEVICE(obj);
    uint64_t val64;
    uint32_t value;

    val64 = range_is_empty(&s->mch.pci_hole)
        ? 0 : range_lob(&s->mch.pci_hole);
    value = val64;
    assert(value == val64);
    visit_type_uint32(v, name, &value, errp);
}

static void q35_host_get_pci_hole_end(Object *obj, Visitor *v,
                                      const char *name, void *opaque,
                                      Error **errp)
{
    Q35PCIHost *s = Q35_HOST_DEVICE(obj);
    uint64_t val64;
    uint32_t value;

    val64 = range_is_empty(&s->mch.pci_hole)
        ? 0 : range_upb(&s->mch.pci_hole) + 1;
    value = val64;
    assert(value == val64);
    visit_type_uint32(v, name, &value, errp);
}

/*
 * The 64bit PCI hole start is set by the Guest firmware
 * as the address of the first 64bit PCI MEM resource.
 * If no PCI device has resources on the 64bit area,
 * the 64bit PCI hole will start after "over 4G RAM" and the
 * reserved space for memory hotplug if any.
 */
static uint64_t q35_host_get_pci_hole64_start_value(Object *obj)
{
    PCIHostState *h = PCI_HOST_BRIDGE(obj);
    Q35PCIHost *s = Q35_HOST_DEVICE(obj);
    Range w64;
    uint64_t value;

    pci_bus_get_w64_range(h->bus, &w64);
    value = range_is_empty(&w64) ? 0 : range_lob(&w64);
    if (!value && s->pci_hole64_fix) {
        value = pc_pci_hole64_start();
    }
    return value;
}

static void q35_host_get_pci_hole64_start(Object *obj, Visitor *v,
                                          const char *name, void *opaque,
                                          Error **errp)
{
    uint64_t hole64_start = q35_host_get_pci_hole64_start_value(obj);

    visit_type_uint64(v, name, &hole64_start, errp);
}

/*
 * The 64bit PCI hole end is set by the Guest firmware
 * as the address of the last 64bit PCI MEM resource.
 * Then it is expanded to the PCI_HOST_PROP_PCI_HOLE64_SIZE
 * that can be configured by the user.
 */
static void q35_host_get_pci_hole64_end(Object *obj, Visitor *v,
                                        const char *name, void *opaque,
                                        Error **errp)
{
    PCIHostState *h = PCI_HOST_BRIDGE(obj);
    Q35PCIHost *s = Q35_HOST_DEVICE(obj);
    uint64_t hole64_start = q35_host_get_pci_hole64_start_value(obj);
    Range w64;
    uint64_t value, hole64_end;

    pci_bus_get_w64_range(h->bus, &w64);
    value = range_is_empty(&w64) ? 0 : range_upb(&w64) + 1;
    hole64_end = ROUND_UP(hole64_start + s->mch.pci_hole64_size, 1ULL << 30);
    if (s->pci_hole64_fix && value < hole64_end) {
        value = hole64_end;
    }
    visit_type_uint64(v, name, &value, errp);
}

/*
 * NOTE: setting defaults for the mch.* fields in this table
 * doesn't work, because mch is a separate QOM object that is
 * zeroed by the object_initialize(&s->mch, ...) call inside
 * q35_host_initfn().  The default values for those
 * properties need to be initialized manually by
 * q35_host_initfn() after the object_initialize() call.
 */
static const Property q35_host_props[] = {
    DEFINE_PROP_UINT64(PCIE_HOST_MCFG_BASE, Q35PCIHost, parent_obj.base_addr,
                        MCH_HOST_BRIDGE_PCIEXBAR_DEFAULT),
    DEFINE_PROP_SIZE(PCI_HOST_PROP_PCI_HOLE64_SIZE, Q35PCIHost,
                     mch.pci_hole64_size, Q35_PCI_HOST_HOLE64_SIZE_DEFAULT),
    DEFINE_PROP_SIZE(PCI_HOST_BELOW_4G_MEM_SIZE, Q35PCIHost,
                     mch.below_4g_mem_size, 0),
    DEFINE_PROP_SIZE(PCI_HOST_ABOVE_4G_MEM_SIZE, Q35PCIHost,
                     mch.above_4g_mem_size, 0),
    DEFINE_PROP_BOOL(PCI_HOST_PROP_SMM_RANGES, Q35PCIHost,
                     mch.has_smm_ranges, true),
    DEFINE_PROP_BOOL("x-pci-hole64-fix", Q35PCIHost, pci_hole64_fix, true),
};

static void q35_host_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);

    hc->root_bus_path = q35_host_root_bus_path;
    dc->realize = q35_host_realize;
    device_class_set_props(dc, q35_host_props);
    /* Reason: needs to be wired up by pc_q35_init */
    dc->user_creatable = false;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->fw_name = "pci";
}

static void q35_host_initfn(Object *obj)
{
    Q35PCIHost *s = Q35_HOST_DEVICE(obj);
    PCIHostState *phb = PCI_HOST_BRIDGE(obj);
    PCIExpressHost *pehb = PCIE_HOST_BRIDGE(obj);

    memory_region_init_io(&phb->conf_mem, obj, &pci_host_conf_le_ops, phb,
                          "pci-conf-idx", 4);
    memory_region_init_io(&phb->data_mem, obj, &pci_host_data_le_ops, phb,
                          "pci-conf-data", 4);

    object_initialize_child(OBJECT(s), "mch", &s->mch, TYPE_MCH_PCI_DEVICE);
    qdev_prop_set_int32(DEVICE(&s->mch), "addr", PCI_DEVFN(0, 0));
    qdev_prop_set_bit(DEVICE(&s->mch), "multifunction", false);
    /* mch's object_initialize resets the default value, set it again */
    qdev_prop_set_uint64(DEVICE(s), PCI_HOST_PROP_PCI_HOLE64_SIZE,
                         Q35_PCI_HOST_HOLE64_SIZE_DEFAULT);

    object_property_add(obj, PCI_HOST_PROP_PCI_HOLE_START, "uint32",
                        q35_host_get_pci_hole_start,
                        NULL, NULL, NULL);

    object_property_add(obj, PCI_HOST_PROP_PCI_HOLE_END, "uint32",
                        q35_host_get_pci_hole_end,
                        NULL, NULL, NULL);

    object_property_add(obj, PCI_HOST_PROP_PCI_HOLE64_START, "uint64",
                        q35_host_get_pci_hole64_start,
                        NULL, NULL, NULL);

    object_property_add(obj, PCI_HOST_PROP_PCI_HOLE64_END, "uint64",
                        q35_host_get_pci_hole64_end,
                        NULL, NULL, NULL);

    object_property_add_uint64_ptr(obj, PCIE_HOST_MCFG_SIZE,
                                   &pehb->size, OBJ_PROP_FLAG_READ);

    object_property_add_link(obj, PCI_HOST_PROP_RAM_MEM, TYPE_MEMORY_REGION,
                             (Object **) &s->mch.ram_memory,
                             qdev_prop_allow_set_link_before_realize, 0);

    object_property_add_link(obj, PCI_HOST_PROP_PCI_MEM, TYPE_MEMORY_REGION,
                             (Object **) &s->mch.pci_address_space,
                             qdev_prop_allow_set_link_before_realize, 0);

    object_property_add_link(obj, PCI_HOST_PROP_SYSTEM_MEM, TYPE_MEMORY_REGION,
                             (Object **) &s->mch.system_memory,
                             qdev_prop_allow_set_link_before_realize, 0);

    object_property_add_link(obj, PCI_HOST_PROP_IO_MEM, TYPE_MEMORY_REGION,
                             (Object **) &s->mch.address_space_io,
                             qdev_prop_allow_set_link_before_realize, 0);
}

static const TypeInfo q35_host_info = {
    .name       = TYPE_Q35_HOST_DEVICE,
    .parent     = TYPE_PCIE_HOST_BRIDGE,
    .instance_size = sizeof(Q35PCIHost),
    .instance_init = q35_host_initfn,
    .class_init = q35_host_class_init,
};

/****************************************************************************
 * MCH D0:F0
 */

static uint64_t blackhole_read(void *ptr, hwaddr reg, unsigned size)
{
    return 0xffffffff;
}

static void blackhole_write(void *opaque, hwaddr addr, uint64_t val,
                            unsigned width)
{
    /* nothing */
}

static const MemoryRegionOps blackhole_ops = {
    .read = blackhole_read,
    .write = blackhole_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* PCIe MMCFG */
static void mch_update_pciexbar(MCHPCIState *mch)
{
    PCIDevice *pci_dev = PCI_DEVICE(mch);
    BusState *bus = qdev_get_parent_bus(DEVICE(mch));
    PCIExpressHost *pehb = PCIE_HOST_BRIDGE(bus->parent);

    uint64_t pciexbar;
    int enable;
    uint64_t addr;
    uint64_t addr_mask;
    uint32_t length;

    pciexbar = pci_get_quad(pci_dev->config + MCH_HOST_BRIDGE_PCIEXBAR);
    enable = pciexbar & MCH_HOST_BRIDGE_PCIEXBAREN;
    addr_mask = MCH_HOST_BRIDGE_PCIEXBAR_ADMSK;
    switch (pciexbar & MCH_HOST_BRIDGE_PCIEXBAR_LENGTH_MASK) {
    case MCH_HOST_BRIDGE_PCIEXBAR_LENGTH_256M:
        length = 256 * 1024 * 1024;
        break;
    case MCH_HOST_BRIDGE_PCIEXBAR_LENGTH_128M:
        length = 128 * 1024 * 1024;
        addr_mask |= MCH_HOST_BRIDGE_PCIEXBAR_128ADMSK |
            MCH_HOST_BRIDGE_PCIEXBAR_64ADMSK;
        break;
    case MCH_HOST_BRIDGE_PCIEXBAR_LENGTH_64M:
        length = 64 * 1024 * 1024;
        addr_mask |= MCH_HOST_BRIDGE_PCIEXBAR_64ADMSK;
        break;
    case MCH_HOST_BRIDGE_PCIEXBAR_LENGTH_RVD:
        qemu_log_mask(LOG_GUEST_ERROR, "Q35: Reserved PCIEXBAR LENGTH\n");
        return;
    default:
        abort();
    }
    addr = pciexbar & addr_mask;
    pcie_host_mmcfg_update(pehb, enable, addr, length);
}

/* PAM */
static void mch_update_pam(MCHPCIState *mch)
{
    PCIDevice *pd = PCI_DEVICE(mch);
    int i;

    memory_region_transaction_begin();
    for (i = 0; i < 13; i++) {
        pam_update(&mch->pam_regions[i], i,
                   pd->config[MCH_HOST_BRIDGE_PAM0 + DIV_ROUND_UP(i, 2)]);
    }
    memory_region_transaction_commit();
}

/* SMRAM */
static void mch_update_smram(MCHPCIState *mch)
{
    PCIDevice *pd = PCI_DEVICE(mch);
    bool h_smrame = (pd->config[MCH_HOST_BRIDGE_ESMRAMC] & MCH_HOST_BRIDGE_ESMRAMC_H_SMRAME);
    uint32_t tseg_size;

    /* implement SMRAM.D_LCK */
    if (pd->config[MCH_HOST_BRIDGE_SMRAM] & MCH_HOST_BRIDGE_SMRAM_D_LCK) {
        pd->config[MCH_HOST_BRIDGE_SMRAM] &= ~MCH_HOST_BRIDGE_SMRAM_D_OPEN;
        pd->wmask[MCH_HOST_BRIDGE_SMRAM] = MCH_HOST_BRIDGE_SMRAM_WMASK_LCK;
        pd->wmask[MCH_HOST_BRIDGE_ESMRAMC] = MCH_HOST_BRIDGE_ESMRAMC_WMASK_LCK;
    }

    memory_region_transaction_begin();

    if (pd->config[MCH_HOST_BRIDGE_SMRAM] & SMRAM_D_OPEN) {
        /* Hide (!) low SMRAM if H_SMRAME = 1 */
        memory_region_set_enabled(&mch->smram_region, h_smrame);
        /* Show high SMRAM if H_SMRAME = 1 */
        memory_region_set_enabled(&mch->open_high_smram, h_smrame);
    } else {
        /* Hide high SMRAM and low SMRAM */
        memory_region_set_enabled(&mch->smram_region, true);
        memory_region_set_enabled(&mch->open_high_smram, false);
    }

    if (pd->config[MCH_HOST_BRIDGE_SMRAM] & SMRAM_G_SMRAME) {
        memory_region_set_enabled(&mch->low_smram, !h_smrame);
        memory_region_set_enabled(&mch->high_smram, h_smrame);
    } else {
        memory_region_set_enabled(&mch->low_smram, false);
        memory_region_set_enabled(&mch->high_smram, false);
    }

    if ((pd->config[MCH_HOST_BRIDGE_ESMRAMC] & MCH_HOST_BRIDGE_ESMRAMC_T_EN) &&
        (pd->config[MCH_HOST_BRIDGE_SMRAM] & SMRAM_G_SMRAME)) {
        switch (pd->config[MCH_HOST_BRIDGE_ESMRAMC] &
                MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_MASK) {
        case MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_1MB:
            tseg_size = 1024 * 1024;
            break;
        case MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_2MB:
            tseg_size = 1024 * 1024 * 2;
            break;
        case MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_8MB:
            tseg_size = 1024 * 1024 * 8;
            break;
        default:
            tseg_size = 1024 * 1024 * (uint32_t)mch->ext_tseg_mbytes;
            break;
        }
    } else {
        tseg_size = 0;
    }
    memory_region_del_subregion(mch->system_memory, &mch->tseg_blackhole);
    memory_region_set_enabled(&mch->tseg_blackhole, tseg_size);
    memory_region_set_size(&mch->tseg_blackhole, tseg_size);
    memory_region_add_subregion_overlap(mch->system_memory,
                                        mch->below_4g_mem_size - tseg_size,
                                        &mch->tseg_blackhole, 1);

    memory_region_set_enabled(&mch->tseg_window, tseg_size);
    memory_region_set_size(&mch->tseg_window, tseg_size);
    memory_region_set_address(&mch->tseg_window,
                              mch->below_4g_mem_size - tseg_size);
    memory_region_set_alias_offset(&mch->tseg_window,
                                   mch->below_4g_mem_size - tseg_size);

    memory_region_transaction_commit();
}

static void mch_update_ext_tseg_mbytes(MCHPCIState *mch)
{
    PCIDevice *pd = PCI_DEVICE(mch);
    uint8_t *reg = pd->config + MCH_HOST_BRIDGE_EXT_TSEG_MBYTES;

    if (mch->ext_tseg_mbytes > 0 &&
        pci_get_word(reg) == MCH_HOST_BRIDGE_EXT_TSEG_MBYTES_QUERY) {
        pci_set_word(reg, mch->ext_tseg_mbytes);
    }
}

static void mch_update_smbase_smram(MCHPCIState *mch)
{
    PCIDevice *pd = PCI_DEVICE(mch);
    uint8_t *reg = pd->config + MCH_HOST_BRIDGE_F_SMBASE;
    bool lck;

    if (!mch->has_smram_at_smbase) {
        return;
    }

    if (*reg == MCH_HOST_BRIDGE_F_SMBASE_QUERY) {
        pd->wmask[MCH_HOST_BRIDGE_F_SMBASE] =
            MCH_HOST_BRIDGE_F_SMBASE_LCK;
        *reg = MCH_HOST_BRIDGE_F_SMBASE_IN_RAM;
        return;
    }

    /*
     * default/reset state, discard written value
     * which will disable SMRAM balackhole at SMBASE
     */
    if (pd->wmask[MCH_HOST_BRIDGE_F_SMBASE] == 0xff) {
        *reg = 0x00;
    }

    memory_region_transaction_begin();
    if (*reg & MCH_HOST_BRIDGE_F_SMBASE_LCK) {
        /* disable all writes */
        pd->wmask[MCH_HOST_BRIDGE_F_SMBASE] &=
            ~MCH_HOST_BRIDGE_F_SMBASE_LCK;
        *reg = MCH_HOST_BRIDGE_F_SMBASE_LCK;
        lck = true;
    } else {
        lck = false;
    }
    memory_region_set_enabled(&mch->smbase_blackhole, lck);
    memory_region_set_enabled(&mch->smbase_window, lck);
    memory_region_transaction_commit();
}

static void mch_write_config(PCIDevice *d,
                              uint32_t address, uint32_t val, int len)
{
    MCHPCIState *mch = MCH_PCI_DEVICE(d);

    pci_default_write_config(d, address, val, len);

    if (ranges_overlap(address, len, MCH_HOST_BRIDGE_PAM0,
                       MCH_HOST_BRIDGE_PAM_SIZE)) {
        mch_update_pam(mch);
    }

    if (ranges_overlap(address, len, MCH_HOST_BRIDGE_PCIEXBAR,
                       MCH_HOST_BRIDGE_PCIEXBAR_SIZE)) {
        mch_update_pciexbar(mch);
    }

    if (!mch->has_smm_ranges) {
        return;
    }

    if (ranges_overlap(address, len, MCH_HOST_BRIDGE_SMRAM,
                       MCH_HOST_BRIDGE_SMRAM_SIZE)) {
        mch_update_smram(mch);
    }

    if (ranges_overlap(address, len, MCH_HOST_BRIDGE_EXT_TSEG_MBYTES,
                       MCH_HOST_BRIDGE_EXT_TSEG_MBYTES_SIZE)) {
        mch_update_ext_tseg_mbytes(mch);
    }

    if (ranges_overlap(address, len, MCH_HOST_BRIDGE_F_SMBASE, 1)) {
        mch_update_smbase_smram(mch);
    }
}

static void mch_update(MCHPCIState *mch)
{
    mch_update_pciexbar(mch);

    mch_update_pam(mch);
    if (mch->has_smm_ranges) {
        mch_update_smram(mch);
        mch_update_ext_tseg_mbytes(mch);
        mch_update_smbase_smram(mch);
    }

    /*
     * pci hole goes from end-of-low-ram to io-apic.
     * mmconfig will be excluded by the dsdt builder.
     */
    range_set_bounds(&mch->pci_hole,
                     mch->below_4g_mem_size,
                     IO_APIC_DEFAULT_ADDRESS - 1);
}

static int mch_post_load(void *opaque, int version_id)
{
    MCHPCIState *mch = opaque;
    mch_update(mch);
    return 0;
}

static const VMStateDescription vmstate_mch = {
    .name = "mch",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = mch_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, MCHPCIState),
        /* Used to be smm_enabled, which was basically always zero because
         * SeaBIOS hardly uses SMM.  SMRAM is now handled by CPU code.
         */
        VMSTATE_UNUSED(1),
        VMSTATE_END_OF_LIST()
    }
};

static void mch_reset(DeviceState *qdev)
{
    PCIDevice *d = PCI_DEVICE(qdev);
    MCHPCIState *mch = MCH_PCI_DEVICE(d);

    pci_set_quad(d->config + MCH_HOST_BRIDGE_PCIEXBAR,
                 MCH_HOST_BRIDGE_PCIEXBAR_DEFAULT);

    if (mch->has_smm_ranges) {
        d->config[MCH_HOST_BRIDGE_SMRAM] = MCH_HOST_BRIDGE_SMRAM_DEFAULT;
        d->config[MCH_HOST_BRIDGE_ESMRAMC] = MCH_HOST_BRIDGE_ESMRAMC_DEFAULT;
        d->wmask[MCH_HOST_BRIDGE_SMRAM] = MCH_HOST_BRIDGE_SMRAM_WMASK;
        d->wmask[MCH_HOST_BRIDGE_ESMRAMC] = MCH_HOST_BRIDGE_ESMRAMC_WMASK;

        if (mch->ext_tseg_mbytes > 0) {
            pci_set_word(d->config + MCH_HOST_BRIDGE_EXT_TSEG_MBYTES,
                        MCH_HOST_BRIDGE_EXT_TSEG_MBYTES_QUERY);
        }

        d->config[MCH_HOST_BRIDGE_F_SMBASE] = 0;
        d->wmask[MCH_HOST_BRIDGE_F_SMBASE] = 0xff;
    }

    mch_update(mch);
}

static void mch_realize(PCIDevice *d, Error **errp)
{
    int i;
    MCHPCIState *mch = MCH_PCI_DEVICE(d);

    if (mch->ext_tseg_mbytes > MCH_HOST_BRIDGE_EXT_TSEG_MBYTES_MAX) {
        error_setg(errp, "invalid extended-tseg-mbytes value: %" PRIu16,
                   mch->ext_tseg_mbytes);
        return;
    }

    /* setup pci memory mapping */
    pc_pci_as_mapping_init(mch->system_memory, mch->pci_address_space);

    /* PAM */
    init_pam(&mch->pam_regions[0], OBJECT(mch), mch->ram_memory,
             mch->system_memory, mch->pci_address_space,
             PAM_BIOS_BASE, PAM_BIOS_SIZE);
    for (i = 0; i < ARRAY_SIZE(mch->pam_regions) - 1; ++i) {
        init_pam(&mch->pam_regions[i + 1], OBJECT(mch), mch->ram_memory,
                 mch->system_memory, mch->pci_address_space,
                 PAM_EXPAN_BASE + i * PAM_EXPAN_SIZE, PAM_EXPAN_SIZE);
    }

    if (!mch->has_smm_ranges) {
        return;
    }

    /* if *disabled* show SMRAM to all CPUs */
    memory_region_init_alias(&mch->smram_region, OBJECT(mch), "smram-region",
                             mch->pci_address_space, MCH_HOST_BRIDGE_SMRAM_C_BASE,
                             MCH_HOST_BRIDGE_SMRAM_C_SIZE);
    memory_region_add_subregion_overlap(mch->system_memory, MCH_HOST_BRIDGE_SMRAM_C_BASE,
                                        &mch->smram_region, 1);
    memory_region_set_enabled(&mch->smram_region, true);

    memory_region_init_alias(&mch->open_high_smram, OBJECT(mch), "smram-open-high",
                             mch->ram_memory, MCH_HOST_BRIDGE_SMRAM_C_BASE,
                             MCH_HOST_BRIDGE_SMRAM_C_SIZE);
    memory_region_add_subregion_overlap(mch->system_memory, 0xfeda0000,
                                        &mch->open_high_smram, 1);
    memory_region_set_enabled(&mch->open_high_smram, false);

    /* smram, as seen by SMM CPUs */
    memory_region_init(&mch->smram, OBJECT(mch), "smram", 4 * GiB);
    memory_region_set_enabled(&mch->smram, true);
    memory_region_init_alias(&mch->low_smram, OBJECT(mch), "smram-low",
                             mch->ram_memory, MCH_HOST_BRIDGE_SMRAM_C_BASE,
                             MCH_HOST_BRIDGE_SMRAM_C_SIZE);
    memory_region_set_enabled(&mch->low_smram, true);
    memory_region_add_subregion(&mch->smram, MCH_HOST_BRIDGE_SMRAM_C_BASE,
                                &mch->low_smram);
    memory_region_init_alias(&mch->high_smram, OBJECT(mch), "smram-high",
                             mch->ram_memory, MCH_HOST_BRIDGE_SMRAM_C_BASE,
                             MCH_HOST_BRIDGE_SMRAM_C_SIZE);
    memory_region_set_enabled(&mch->high_smram, true);
    memory_region_add_subregion(&mch->smram, 0xfeda0000, &mch->high_smram);

    memory_region_init_io(&mch->tseg_blackhole, OBJECT(mch),
                          &blackhole_ops, NULL,
                          "tseg-blackhole", 0);
    memory_region_set_enabled(&mch->tseg_blackhole, false);
    memory_region_add_subregion_overlap(mch->system_memory,
                                        mch->below_4g_mem_size,
                                        &mch->tseg_blackhole, 1);

    memory_region_init_alias(&mch->tseg_window, OBJECT(mch), "tseg-window",
                             mch->ram_memory, mch->below_4g_mem_size, 0);
    memory_region_set_enabled(&mch->tseg_window, false);
    memory_region_add_subregion(&mch->smram, mch->below_4g_mem_size,
                                &mch->tseg_window);

    /*
     * This is not what hardware does, so it's QEMU specific hack.
     * See commit message for details.
     */
    memory_region_init_io(&mch->smbase_blackhole, OBJECT(mch), &blackhole_ops,
                          NULL, "smbase-blackhole",
                          MCH_HOST_BRIDGE_SMBASE_SIZE);
    memory_region_set_enabled(&mch->smbase_blackhole, false);
    memory_region_add_subregion_overlap(mch->system_memory,
                                        MCH_HOST_BRIDGE_SMBASE_ADDR,
                                        &mch->smbase_blackhole, 1);

    memory_region_init_alias(&mch->smbase_window, OBJECT(mch),
                             "smbase-window", mch->ram_memory,
                             MCH_HOST_BRIDGE_SMBASE_ADDR,
                             MCH_HOST_BRIDGE_SMBASE_SIZE);
    memory_region_set_enabled(&mch->smbase_window, false);
    memory_region_add_subregion(&mch->smram, MCH_HOST_BRIDGE_SMBASE_ADDR,
                                &mch->smbase_window);

    object_property_add_const_link(qdev_get_machine(), "smram",
                                   OBJECT(&mch->smram));
}

static const Property mch_props[] = {
    DEFINE_PROP_UINT16("extended-tseg-mbytes", MCHPCIState, ext_tseg_mbytes,
                       64),
    DEFINE_PROP_BOOL("smbase-smram", MCHPCIState, has_smram_at_smbase, true),
};

static void mch_class_init(ObjectClass *klass, const void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize = mch_realize;
    k->config_write = mch_write_config;
    device_class_set_legacy_reset(dc, mch_reset);
    device_class_set_props(dc, mch_props);
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "Host bridge";
    dc->vmsd = &vmstate_mch;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    /*
     * The 'q35' machine type implements an Intel Series 3 chipset,
     * of which there are several variants. The key difference between
     * the 82P35 MCH ('p35') and 82Q35 GMCH ('q35') variants is that
     * the latter has an integrated graphics adapter. QEMU does not
     * implement integrated graphics, so uses the PCI ID for the 82P35
     * chipset.
     */
    k->device_id = PCI_DEVICE_ID_INTEL_P35_MCH;
    k->revision = MCH_HOST_BRIDGE_REVISION_DEFAULT;
    k->class_id = PCI_CLASS_BRIDGE_HOST;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo mch_info = {
    .name = TYPE_MCH_PCI_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MCHPCIState),
    .class_init = mch_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void q35_register(void)
{
    type_register_static(&mch_info);
    type_register_static(&q35_host_info);
}

type_init(q35_register);
