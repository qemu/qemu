/*
 * QEMU Sun4u/Sun4v System Emulator
 *
 * Copyright (c) 2005 Fabrice Bellard
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
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/datadir.h"
#include "cpu.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_host.h"
#include "hw/qdev-properties.h"
#include "hw/pci-host/sabre.h"
#include "hw/char/serial.h"
#include "hw/char/parallel-isa.h"
#include "hw/rtc/m48t59.h"
#include "migration/vmstate.h"
#include "hw/input/i8042.h"
#include "hw/block/fdc.h"
#include "net/net.h"
#include "qemu/timer.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/nvram/sun_nvram.h"
#include "hw/nvram/chrp_nvram.h"
#include "hw/sparc/sparc64.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/sysbus.h"
#include "hw/ide/pci.h"
#include "hw/loader.h"
#include "hw/fw-path-provider.h"
#include "elf.h"
#include "trace.h"
#include "qom/object.h"

#define KERNEL_LOAD_ADDR     0x00404000
#define CMDLINE_ADDR         0x003ff000
#define PROM_SIZE_MAX        (4 * MiB)
#define PROM_VADDR           0x000ffd00000ULL
#define PBM_SPECIAL_BASE     0x1fe00000000ULL
#define PBM_MEM_BASE         0x1ff00000000ULL
#define PBM_PCI_IO_BASE      (PBM_SPECIAL_BASE + 0x02000000ULL)
#define PROM_FILENAME        "openbios-sparc64"
#define NVRAM_SIZE           0x2000
#define BIOS_CFG_IOPORT      0x510
#define FW_CFG_SPARC64_WIDTH (FW_CFG_ARCH_LOCAL + 0x00)
#define FW_CFG_SPARC64_HEIGHT (FW_CFG_ARCH_LOCAL + 0x01)
#define FW_CFG_SPARC64_DEPTH (FW_CFG_ARCH_LOCAL + 0x02)

#define IVEC_MAX             0x40

struct hwdef {
    uint16_t machine_id;
    uint64_t prom_addr;
    uint64_t console_serial_base;
};

struct EbusState {
    /*< private >*/
    PCIDevice parent_obj;

    ISABus *isa_bus;
    qemu_irq *isa_irqs_in;
    qemu_irq isa_irqs_out[ISA_NUM_IRQS];
    uint64_t console_serial_base;
    MemoryRegion bar0;
    MemoryRegion bar1;
};

#define TYPE_EBUS "ebus"
OBJECT_DECLARE_SIMPLE_TYPE(EbusState, EBUS)

const char *fw_cfg_arch_key_name(uint16_t key)
{
    static const struct {
        uint16_t key;
        const char *name;
    } fw_cfg_arch_wellknown_keys[] = {
        {FW_CFG_SPARC64_WIDTH, "width"},
        {FW_CFG_SPARC64_HEIGHT, "height"},
        {FW_CFG_SPARC64_DEPTH, "depth"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(fw_cfg_arch_wellknown_keys); i++) {
        if (fw_cfg_arch_wellknown_keys[i].key == key) {
            return fw_cfg_arch_wellknown_keys[i].name;
        }
    }
    return NULL;
}

static void fw_cfg_boot_set(void *opaque, const char *boot_device,
                            Error **errp)
{
    fw_cfg_modify_i16(opaque, FW_CFG_BOOT_DEVICE, boot_device[0]);
}

static int sun4u_NVRAM_set_params(Nvram *nvram, uint16_t NVRAM_size,
                                  const char *arch, ram_addr_t RAM_size,
                                  const char *boot_devices,
                                  uint32_t kernel_image, uint32_t kernel_size,
                                  const char *cmdline,
                                  uint32_t initrd_image, uint32_t initrd_size,
                                  uint32_t NVRAM_image,
                                  int width, int height, int depth,
                                  const uint8_t *macaddr)
{
    unsigned int i;
    int sysp_end;
    uint8_t image[0x1ff0];
    NvramClass *k = NVRAM_GET_CLASS(nvram);

    memset(image, '\0', sizeof(image));

    /* OpenBIOS nvram variables partition */
    sysp_end = chrp_nvram_create_system_partition(image, 0, 0x1fd0);

    /* Free space partition */
    chrp_nvram_create_free_partition(&image[sysp_end], 0x1fd0 - sysp_end);

    Sun_init_header((struct Sun_nvram *)&image[0x1fd8], macaddr, 0x80);

    for (i = 0; i < sizeof(image); i++) {
        (k->write)(nvram, i, image[i]);
    }

    return 0;
}

static uint64_t sun4u_load_kernel(const char *kernel_filename,
                                  const char *initrd_filename,
                                  ram_addr_t RAM_size, uint64_t *initrd_size,
                                  uint64_t *initrd_addr, uint64_t *kernel_addr,
                                  uint64_t *kernel_entry)
{
    int linux_boot;
    unsigned int i;
    long kernel_size;
    uint8_t *ptr;
    uint64_t kernel_top = 0;

    linux_boot = (kernel_filename != NULL);

    kernel_size = 0;
    if (linux_boot) {
        int bswap_needed;

#ifdef BSWAP_NEEDED
        bswap_needed = 1;
#else
        bswap_needed = 0;
#endif
        kernel_size = load_elf(kernel_filename, NULL, NULL, NULL, kernel_entry,
                               kernel_addr, &kernel_top, NULL, 1, EM_SPARCV9, 0,
                               0);
        if (kernel_size < 0) {
            *kernel_addr = KERNEL_LOAD_ADDR;
            *kernel_entry = KERNEL_LOAD_ADDR;
            kernel_size = load_aout(kernel_filename, KERNEL_LOAD_ADDR,
                                    RAM_size - KERNEL_LOAD_ADDR, bswap_needed,
                                    TARGET_PAGE_SIZE);
        }
        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename,
                                              KERNEL_LOAD_ADDR,
                                              RAM_size - KERNEL_LOAD_ADDR);
        }
        if (kernel_size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }
        /* load initrd above kernel */
        *initrd_size = 0;
        if (initrd_filename && kernel_top) {
            *initrd_addr = TARGET_PAGE_ALIGN(kernel_top);

            *initrd_size = load_image_targphys(initrd_filename,
                                               *initrd_addr,
                                               RAM_size - *initrd_addr);
            if ((int)*initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             initrd_filename);
                exit(1);
            }
        }
        if (*initrd_size > 0) {
            for (i = 0; i < 64 * TARGET_PAGE_SIZE; i += TARGET_PAGE_SIZE) {
                ptr = rom_ptr(*kernel_addr + i, 32);
                if (ptr && ldl_p(ptr + 8) == 0x48647253) { /* HdrS */
                    stl_p(ptr + 24, *initrd_addr + *kernel_addr);
                    stl_p(ptr + 28, *initrd_size);
                    break;
                }
            }
        }
    }
    return kernel_size;
}

typedef struct ResetData {
    SPARCCPU *cpu;
    uint64_t prom_addr;
} ResetData;

#define TYPE_SUN4U_POWER "power"
OBJECT_DECLARE_SIMPLE_TYPE(PowerDevice, SUN4U_POWER)

struct PowerDevice {
    SysBusDevice parent_obj;

    MemoryRegion power_mmio;
};

/* Power */
static uint64_t power_mem_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void power_mem_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    /* According to a real Ultra 5, bit 24 controls the power */
    if (val & 0x1000000) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

static const MemoryRegionOps power_mem_ops = {
    .read = power_mem_read,
    .write = power_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void power_realize(DeviceState *dev, Error **errp)
{
    PowerDevice *d = SUN4U_POWER(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&d->power_mmio, OBJECT(dev), &power_mem_ops, d,
                          "power", sizeof(uint32_t));

    sysbus_init_mmio(sbd, &d->power_mmio);
}

static void power_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = power_realize;
}

static const TypeInfo power_info = {
    .name          = TYPE_SUN4U_POWER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PowerDevice),
    .class_init    = power_class_init,
};

static void ebus_isa_irq_handler(void *opaque, int n, int level)
{
    EbusState *s = EBUS(opaque);
    qemu_irq irq = s->isa_irqs_out[n];

    /* Pass ISA bus IRQs onto their gpio equivalent */
    trace_ebus_isa_irq_handler(n, level);
    if (irq) {
        qemu_set_irq(irq, level);
    }
}

/* EBUS (Eight bit bus) bridge */
static void ebus_realize(PCIDevice *pci_dev, Error **errp)
{
    EbusState *s = EBUS(pci_dev);
    ISADevice *isa_dev;
    SysBusDevice *sbd;
    DeviceState *dev;
    DriveInfo *fd[MAX_FD];
    int i;

    s->isa_bus = isa_bus_new(DEVICE(pci_dev), get_system_memory(),
                             pci_address_space_io(pci_dev), errp);
    if (!s->isa_bus) {
        error_setg(errp, "unable to instantiate EBUS ISA bus");
        return;
    }

    /* ISA bus */
    s->isa_irqs_in = qemu_allocate_irqs(ebus_isa_irq_handler, s, ISA_NUM_IRQS);
    isa_bus_register_input_irqs(s->isa_bus, s->isa_irqs_in);
    qdev_init_gpio_out_named(DEVICE(s), s->isa_irqs_out, "isa-irq",
                             ISA_NUM_IRQS);

    /* Serial ports */
    i = 0;
    if (s->console_serial_base) {
        serial_mm_init(pci_address_space(pci_dev), s->console_serial_base,
                       0, NULL, 115200, serial_hd(i), DEVICE_BIG_ENDIAN);
        i++;
    }
    serial_hds_isa_init(s->isa_bus, i, MAX_ISA_SERIAL_PORTS);

    /* Parallel ports */
    parallel_hds_isa_init(s->isa_bus, MAX_PARALLEL_PORTS);

    /* Keyboard */
    isa_create_simple(s->isa_bus, TYPE_I8042);

    /* Floppy */
    for (i = 0; i < MAX_FD; i++) {
        fd[i] = drive_get(IF_FLOPPY, 0, i);
    }
    isa_dev = isa_new(TYPE_ISA_FDC);
    dev = DEVICE(isa_dev);
    qdev_prop_set_uint32(dev, "dma", -1);
    isa_realize_and_unref(isa_dev, s->isa_bus, &error_fatal);
    isa_fdc_init_drives(isa_dev, fd);

    /* Power */
    dev = qdev_new(TYPE_SUN4U_POWER);
    sbd = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(sbd, &error_fatal);
    memory_region_add_subregion(pci_address_space_io(pci_dev), 0x7240,
                                sysbus_mmio_get_region(sbd, 0));

    /* PCI */
    pci_dev->config[0x04] = 0x06; // command = bus master, pci mem
    pci_dev->config[0x05] = 0x00;
    pci_dev->config[0x06] = 0xa0; // status = fast back-to-back, 66MHz, no error
    pci_dev->config[0x07] = 0x03; // status = medium devsel
    pci_dev->config[0x09] = 0x00; // programming i/f
    pci_dev->config[0x0D] = 0x0a; // latency_timer

    memory_region_init_alias(&s->bar0, OBJECT(s), "bar0", get_system_io(),
                             0, 0x1000000);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);
    memory_region_init_alias(&s->bar1, OBJECT(s), "bar1", get_system_io(),
                             0, 0x8000);
    pci_register_bar(pci_dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar1);
}

static Property ebus_properties[] = {
    DEFINE_PROP_UINT64("console-serial-base", EbusState,
                       console_serial_base, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void ebus_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize = ebus_realize;
    k->vendor_id = PCI_VENDOR_ID_SUN;
    k->device_id = PCI_DEVICE_ID_SUN_EBUS;
    k->revision = 0x01;
    k->class_id = PCI_CLASS_BRIDGE_OTHER;
    device_class_set_props(dc, ebus_properties);
}

static const TypeInfo ebus_info = {
    .name          = TYPE_EBUS,
    .parent        = TYPE_PCI_DEVICE,
    .class_init    = ebus_class_init,
    .instance_size = sizeof(EbusState),
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

#define TYPE_OPENPROM "openprom"
typedef struct PROMState PROMState;
DECLARE_INSTANCE_CHECKER(PROMState, OPENPROM,
                         TYPE_OPENPROM)

struct PROMState {
    SysBusDevice parent_obj;

    MemoryRegion prom;
};

static uint64_t translate_prom_address(void *opaque, uint64_t addr)
{
    hwaddr *base_addr = (hwaddr *)opaque;
    return addr + *base_addr - PROM_VADDR;
}

/* Boot PROM (OpenBIOS) */
static void prom_init(hwaddr addr, const char *bios_name)
{
    DeviceState *dev;
    SysBusDevice *s;
    char *filename;
    int ret;

    dev = qdev_new(TYPE_OPENPROM);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);

    sysbus_mmio_map(s, 0, addr);

    /* load boot prom */
    if (bios_name == NULL) {
        bios_name = PROM_FILENAME;
    }
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (filename) {
        ret = load_elf(filename, NULL, translate_prom_address, &addr,
                       NULL, NULL, NULL, NULL, 1, EM_SPARCV9, 0, 0);
        if (ret < 0 || ret > PROM_SIZE_MAX) {
            ret = load_image_targphys(filename, addr, PROM_SIZE_MAX);
        }
        g_free(filename);
    } else {
        ret = -1;
    }
    if (ret < 0 || ret > PROM_SIZE_MAX) {
        error_report("could not load prom '%s'", bios_name);
        exit(1);
    }
}

static void prom_realize(DeviceState *ds, Error **errp)
{
    PROMState *s = OPENPROM(ds);
    SysBusDevice *dev = SYS_BUS_DEVICE(ds);
    Error *local_err = NULL;

    memory_region_init_ram_nomigrate(&s->prom, OBJECT(ds), "sun4u.prom",
                                     PROM_SIZE_MAX, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    vmstate_register_ram_global(&s->prom);
    memory_region_set_readonly(&s->prom, true);
    sysbus_init_mmio(dev, &s->prom);
}

static Property prom_properties[] = {
    {/* end of property list */},
};

static void prom_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, prom_properties);
    dc->realize = prom_realize;
}

static const TypeInfo prom_info = {
    .name          = TYPE_OPENPROM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PROMState),
    .class_init    = prom_class_init,
};


#define TYPE_SUN4U_MEMORY "memory"
typedef struct RamDevice RamDevice;
DECLARE_INSTANCE_CHECKER(RamDevice, SUN4U_RAM,
                         TYPE_SUN4U_MEMORY)

struct RamDevice {
    SysBusDevice parent_obj;

    MemoryRegion ram;
    uint64_t size;
};

/* System RAM */
static void ram_realize(DeviceState *dev, Error **errp)
{
    RamDevice *d = SUN4U_RAM(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_ram_nomigrate(&d->ram, OBJECT(d), "sun4u.ram", d->size,
                           &error_fatal);
    vmstate_register_ram_global(&d->ram);
    sysbus_init_mmio(sbd, &d->ram);
}

static void ram_init(hwaddr addr, ram_addr_t RAM_size)
{
    DeviceState *dev;
    SysBusDevice *s;
    RamDevice *d;

    /* allocate RAM */
    dev = qdev_new(TYPE_SUN4U_MEMORY);
    s = SYS_BUS_DEVICE(dev);

    d = SUN4U_RAM(dev);
    d->size = RAM_size;
    sysbus_realize_and_unref(s, &error_fatal);

    sysbus_mmio_map(s, 0, addr);
}

static Property ram_properties[] = {
    DEFINE_PROP_UINT64("size", RamDevice, size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void ram_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ram_realize;
    device_class_set_props(dc, ram_properties);
}

static const TypeInfo ram_info = {
    .name          = TYPE_SUN4U_MEMORY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RamDevice),
    .class_init    = ram_class_init,
};

static void sun4uv_init(MemoryRegion *address_space_mem,
                        MachineState *machine,
                        const struct hwdef *hwdef)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    SPARCCPU *cpu;
    Nvram *nvram;
    unsigned int i;
    uint64_t initrd_addr, initrd_size, kernel_addr, kernel_size, kernel_entry;
    SabreState *sabre;
    PCIBus *pci_bus, *pci_busA, *pci_busB;
    PCIDevice *ebus, *pci_dev;
    SysBusDevice *s;
    DeviceState *iommu, *dev;
    FWCfgState *fw_cfg;
    NICInfo *nd;
    MACAddr macaddr;
    bool onboard_nic;

    /* init CPUs */
    cpu = sparc64_cpu_devinit(machine->cpu_type, hwdef->prom_addr);

    /* IOMMU */
    iommu = qdev_new(TYPE_SUN4U_IOMMU);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(iommu), &error_fatal);

    /* set up devices */
    ram_init(0, machine->ram_size);

    prom_init(hwdef->prom_addr, machine->firmware);

    /* Init sabre (PCI host bridge) */
    sabre = SABRE(qdev_new(TYPE_SABRE));
    qdev_prop_set_uint64(DEVICE(sabre), "special-base", PBM_SPECIAL_BASE);
    qdev_prop_set_uint64(DEVICE(sabre), "mem-base", PBM_MEM_BASE);
    object_property_set_link(OBJECT(sabre), "iommu", OBJECT(iommu),
                             &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(sabre), &error_fatal);

    /* sabre_config */
    sysbus_mmio_map(SYS_BUS_DEVICE(sabre), 0, PBM_SPECIAL_BASE);
    /* PCI configuration space */
    sysbus_mmio_map(SYS_BUS_DEVICE(sabre), 1, PBM_SPECIAL_BASE + 0x1000000ULL);
    /* pci_ioport */
    sysbus_mmio_map(SYS_BUS_DEVICE(sabre), 2, PBM_SPECIAL_BASE + 0x2000000ULL);

    /* Wire up PCI interrupts to CPU */
    for (i = 0; i < IVEC_MAX; i++) {
        qdev_connect_gpio_out_named(DEVICE(sabre), "ivec-irq", i,
            qdev_get_gpio_in_named(DEVICE(cpu), "ivec-irq", i));
    }

    pci_bus = PCI_HOST_BRIDGE(sabre)->bus;
    pci_busA = pci_bridge_get_sec_bus(sabre->bridgeA);
    pci_busB = pci_bridge_get_sec_bus(sabre->bridgeB);

    /* Only in-built Simba APBs can exist on the root bus, slot 0 on busA is
       reserved (leaving no slots free after on-board devices) however slots
       0-3 are free on busB */
    pci_bus_set_slot_reserved_mask(pci_bus, 0xfffffffc);
    pci_bus_set_slot_reserved_mask(pci_busA, 0xfffffff1);
    pci_bus_set_slot_reserved_mask(pci_busB, 0xfffffff0);

    ebus = pci_new_multifunction(PCI_DEVFN(1, 0), true, TYPE_EBUS);
    qdev_prop_set_uint64(DEVICE(ebus), "console-serial-base",
                         hwdef->console_serial_base);
    pci_realize_and_unref(ebus, pci_busA, &error_fatal);

    /* Wire up "well-known" ISA IRQs to PBM legacy obio IRQs */
    qdev_connect_gpio_out_named(DEVICE(ebus), "isa-irq", 7,
        qdev_get_gpio_in_named(DEVICE(sabre), "pbm-irq", OBIO_LPT_IRQ));
    qdev_connect_gpio_out_named(DEVICE(ebus), "isa-irq", 6,
        qdev_get_gpio_in_named(DEVICE(sabre), "pbm-irq", OBIO_FDD_IRQ));
    qdev_connect_gpio_out_named(DEVICE(ebus), "isa-irq", 1,
        qdev_get_gpio_in_named(DEVICE(sabre), "pbm-irq", OBIO_KBD_IRQ));
    qdev_connect_gpio_out_named(DEVICE(ebus), "isa-irq", 12,
        qdev_get_gpio_in_named(DEVICE(sabre), "pbm-irq", OBIO_MSE_IRQ));
    qdev_connect_gpio_out_named(DEVICE(ebus), "isa-irq", 4,
        qdev_get_gpio_in_named(DEVICE(sabre), "pbm-irq", OBIO_SER_IRQ));

    switch (vga_interface_type) {
    case VGA_STD:
        pci_create_simple(pci_busA, PCI_DEVFN(2, 0), "VGA");
        vga_interface_created = true;
        break;
    case VGA_NONE:
        break;
    default:
        abort();   /* Should not happen - types are checked in vl.c already */
    }

    memset(&macaddr, 0, sizeof(MACAddr));
    onboard_nic = false;
    for (i = 0; i < nb_nics; i++) {
        PCIBus *bus;
        nd = &nd_table[i];

        if (!nd->model || strcmp(nd->model, mc->default_nic) == 0) {
            if (!onboard_nic) {
                pci_dev = pci_new_multifunction(PCI_DEVFN(1, 1),
                                                   true, mc->default_nic);
                bus = pci_busA;
                memcpy(&macaddr, &nd->macaddr.a, sizeof(MACAddr));
                onboard_nic = true;
            } else {
                pci_dev = pci_new(-1, mc->default_nic);
                bus = pci_busB;
            }
        } else {
            pci_dev = pci_new(-1, nd->model);
            bus = pci_busB;
        }

        dev = &pci_dev->qdev;
        qdev_set_nic_properties(dev, nd);
        pci_realize_and_unref(pci_dev, bus, &error_fatal);
    }

    /* If we don't have an onboard NIC, grab a default MAC address so that
     * we have a valid machine id */
    if (!onboard_nic) {
        qemu_macaddr_default_if_unset(&macaddr);
    }

    pci_dev = pci_new(PCI_DEVFN(3, 0), "cmd646-ide");
    qdev_prop_set_uint32(&pci_dev->qdev, "secondary", 1);
    pci_realize_and_unref(pci_dev, pci_busA, &error_fatal);
    pci_ide_create_devs(pci_dev);

    /* Map NVRAM into I/O (ebus) space */
    dev = qdev_new("sysbus-m48t59");
    qdev_prop_set_int32(dev, "base-year", 1968);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    memory_region_add_subregion(pci_address_space_io(ebus), 0x2000,
                                sysbus_mmio_get_region(s, 0));
    nvram = NVRAM(dev);
 
    initrd_size = 0;
    initrd_addr = 0;
    kernel_size = sun4u_load_kernel(machine->kernel_filename,
                                    machine->initrd_filename,
                                    machine->ram_size, &initrd_size, &initrd_addr,
                                    &kernel_addr, &kernel_entry);

    sun4u_NVRAM_set_params(nvram, NVRAM_SIZE, "Sun4u", machine->ram_size,
                           machine->boot_config.order,
                           kernel_addr, kernel_size,
                           machine->kernel_cmdline,
                           initrd_addr, initrd_size,
                           /* XXX: need an option to load a NVRAM image */
                           0,
                           graphic_width, graphic_height, graphic_depth,
                           (uint8_t *)&macaddr);

    dev = qdev_new(TYPE_FW_CFG_IO);
    qdev_prop_set_bit(dev, "dma_enabled", false);
    object_property_add_child(OBJECT(ebus), TYPE_FW_CFG, OBJECT(dev));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    memory_region_add_subregion(pci_address_space_io(ebus), BIOS_CFG_IOPORT,
                                &FW_CFG_IO(dev)->comb_iomem);

    fw_cfg = FW_CFG(dev);
    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, (uint16_t)machine->smp.cpus);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, (uint16_t)machine->smp.max_cpus);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)machine->ram_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MACHINE_ID, hwdef->machine_id);
    fw_cfg_add_i64(fw_cfg, FW_CFG_KERNEL_ADDR, kernel_entry);
    fw_cfg_add_i64(fw_cfg, FW_CFG_KERNEL_SIZE, kernel_size);
    if (machine->kernel_cmdline) {
        fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE,
                       strlen(machine->kernel_cmdline) + 1);
        fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA, machine->kernel_cmdline);
    } else {
        fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE, 0);
    }
    fw_cfg_add_i64(fw_cfg, FW_CFG_INITRD_ADDR, initrd_addr);
    fw_cfg_add_i64(fw_cfg, FW_CFG_INITRD_SIZE, initrd_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_BOOT_DEVICE, machine->boot_config.order[0]);

    fw_cfg_add_i16(fw_cfg, FW_CFG_SPARC64_WIDTH, graphic_width);
    fw_cfg_add_i16(fw_cfg, FW_CFG_SPARC64_HEIGHT, graphic_height);
    fw_cfg_add_i16(fw_cfg, FW_CFG_SPARC64_DEPTH, graphic_depth);

    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);
}

enum {
    sun4u_id = 0,
    sun4v_id = 64,
};

/*
 * Implementation of an interface to adjust firmware path
 * for the bootindex property handling.
 */
static char *sun4u_fw_dev_path(FWPathProvider *p, BusState *bus,
                               DeviceState *dev)
{
    PCIDevice *pci;

    if (!strcmp(object_get_typename(OBJECT(dev)), "pbm-bridge")) {
        pci = PCI_DEVICE(dev);

        if (PCI_FUNC(pci->devfn)) {
            return g_strdup_printf("pci@%x,%x", PCI_SLOT(pci->devfn),
                                   PCI_FUNC(pci->devfn));
        } else {
            return g_strdup_printf("pci@%x", PCI_SLOT(pci->devfn));
        }
    }

    if (!strcmp(object_get_typename(OBJECT(dev)), "ide-hd")) {
        return g_strdup("disk");
    }

    if (!strcmp(object_get_typename(OBJECT(dev)), "ide-cd")) {
        return g_strdup("cdrom");
    }

    if (!strcmp(object_get_typename(OBJECT(dev)), "virtio-blk-device")) {
        return g_strdup("disk");
    }

    return NULL;
}

static const struct hwdef hwdefs[] = {
    /* Sun4u generic PC-like machine */
    {
        .machine_id = sun4u_id,
        .prom_addr = 0x1fff0000000ULL,
        .console_serial_base = 0,
    },
    /* Sun4v generic PC-like machine */
    {
        .machine_id = sun4v_id,
        .prom_addr = 0x1fff0000000ULL,
        .console_serial_base = 0,
    },
};

/* Sun4u hardware initialisation */
static void sun4u_init(MachineState *machine)
{
    sun4uv_init(get_system_memory(), machine, &hwdefs[0]);
}

/* Sun4v hardware initialisation */
static void sun4v_init(MachineState *machine)
{
    sun4uv_init(get_system_memory(), machine, &hwdefs[1]);
}

static void sun4u_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    FWPathProviderClass *fwc = FW_PATH_PROVIDER_CLASS(oc);

    mc->desc = "Sun4u platform";
    mc->init = sun4u_init;
    mc->block_default_type = IF_IDE;
    mc->max_cpus = 1; /* XXX for now */
    mc->is_default = true;
    mc->default_boot_order = "c";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("TI-UltraSparc-IIi");
    mc->ignore_boot_device_suffixes = true;
    mc->default_display = "std";
    mc->default_nic = "sunhme";
    mc->no_parallel = !module_object_class_by_name(TYPE_ISA_PARALLEL);
    fwc->get_dev_path = sun4u_fw_dev_path;
}

static const TypeInfo sun4u_type = {
    .name = MACHINE_TYPE_NAME("sun4u"),
    .parent = TYPE_MACHINE,
    .class_init = sun4u_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_FW_PATH_PROVIDER },
        { }
    },
};

static void sun4v_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4v platform";
    mc->init = sun4v_init;
    mc->block_default_type = IF_IDE;
    mc->max_cpus = 1; /* XXX for now */
    mc->default_boot_order = "c";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("Sun-UltraSparc-T1");
    mc->default_display = "std";
    mc->default_nic = "sunhme";
    mc->no_parallel = !module_object_class_by_name(TYPE_ISA_PARALLEL);
}

static const TypeInfo sun4v_type = {
    .name = MACHINE_TYPE_NAME("sun4v"),
    .parent = TYPE_MACHINE,
    .class_init = sun4v_class_init,
};

static void sun4u_register_types(void)
{
    type_register_static(&power_info);
    type_register_static(&ebus_info);
    type_register_static(&prom_info);
    type_register_static(&ram_info);

    type_register_static(&sun4u_type);
    type_register_static(&sun4v_type);
}

type_init(sun4u_register_types)
