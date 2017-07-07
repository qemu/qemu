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
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/apb.h"
#include "hw/i386/pc.h"
#include "hw/char/serial.h"
#include "hw/timer/m48t59.h"
#include "hw/block/fdc.h"
#include "net/net.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/nvram/sun_nvram.h"
#include "hw/nvram/chrp_nvram.h"
#include "hw/sparc/sparc64.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/sysbus.h"
#include "hw/ide.h"
#include "hw/loader.h"
#include "elf.h"
#include "qemu/cutils.h"

//#define DEBUG_EBUS

#ifdef DEBUG_EBUS
#define EBUS_DPRINTF(fmt, ...)                                  \
    do { printf("EBUS: " fmt , ## __VA_ARGS__); } while (0)
#else
#define EBUS_DPRINTF(fmt, ...)
#endif

#define KERNEL_LOAD_ADDR     0x00404000
#define CMDLINE_ADDR         0x003ff000
#define PROM_SIZE_MAX        (4 * 1024 * 1024)
#define PROM_VADDR           0x000ffd00000ULL
#define APB_SPECIAL_BASE     0x1fe00000000ULL
#define APB_MEM_BASE         0x1ff00000000ULL
#define APB_PCI_IO_BASE      (APB_SPECIAL_BASE + 0x02000000ULL)
#define PROM_FILENAME        "openbios-sparc64"
#define NVRAM_SIZE           0x2000
#define MAX_IDE_BUS          2
#define BIOS_CFG_IOPORT      0x510
#define FW_CFG_SPARC64_WIDTH (FW_CFG_ARCH_LOCAL + 0x00)
#define FW_CFG_SPARC64_HEIGHT (FW_CFG_ARCH_LOCAL + 0x01)
#define FW_CFG_SPARC64_DEPTH (FW_CFG_ARCH_LOCAL + 0x02)

#define IVEC_MAX             0x40

struct hwdef {
    const char * const default_cpu_model;
    uint16_t machine_id;
    uint64_t prom_addr;
    uint64_t console_serial_base;
};

typedef struct EbusState {
    PCIDevice pci_dev;
    MemoryRegion bar0;
    MemoryRegion bar1;
} EbusState;

void DMA_init(ISABus *bus, int high_page_enable)
{
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
    sysp_end = chrp_nvram_create_system_partition(image, 0);

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
    uint64_t kernel_top;

    linux_boot = (kernel_filename != NULL);

    kernel_size = 0;
    if (linux_boot) {
        int bswap_needed;

#ifdef BSWAP_NEEDED
        bswap_needed = 1;
#else
        bswap_needed = 0;
#endif
        kernel_size = load_elf(kernel_filename, NULL, NULL, kernel_entry,
                               kernel_addr, &kernel_top, 1, EM_SPARCV9, 0, 0);
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
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
        /* load initrd above kernel */
        *initrd_size = 0;
        if (initrd_filename) {
            *initrd_addr = TARGET_PAGE_ALIGN(kernel_top);

            *initrd_size = load_image_targphys(initrd_filename,
                                               *initrd_addr,
                                               RAM_size - *initrd_addr);
            if ((int)*initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                        initrd_filename);
                exit(1);
            }
        }
        if (*initrd_size > 0) {
            for (i = 0; i < 64 * TARGET_PAGE_SIZE; i += TARGET_PAGE_SIZE) {
                ptr = rom_ptr(*kernel_addr + i);
                if (ldl_p(ptr + 8) == 0x48647253) { /* HdrS */
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

static void isa_irq_handler(void *opaque, int n, int level)
{
    static const int isa_irq_to_ivec[16] = {
        [1] = 0x29, /* keyboard */
        [4] = 0x2b, /* serial */
        [6] = 0x27, /* floppy */
        [7] = 0x22, /* parallel */
        [12] = 0x2a, /* mouse */
    };
    qemu_irq *irqs = opaque;
    int ivec;

    assert(n < ARRAY_SIZE(isa_irq_to_ivec));
    ivec = isa_irq_to_ivec[n];
    EBUS_DPRINTF("Set ISA IRQ %d level %d -> ivec 0x%x\n", n, level, ivec);
    if (ivec) {
        qemu_set_irq(irqs[ivec], level);
    }
}

/* EBUS (Eight bit bus) bridge */
static ISABus *
pci_ebus_init(PCIBus *bus, int devfn, qemu_irq *irqs)
{
    qemu_irq *isa_irq;
    PCIDevice *pci_dev;
    ISABus *isa_bus;

    pci_dev = pci_create_simple(bus, devfn, "ebus");
    isa_bus = ISA_BUS(qdev_get_child_bus(DEVICE(pci_dev), "isa.0"));
    isa_irq = qemu_allocate_irqs(isa_irq_handler, irqs, 16);
    isa_bus_irqs(isa_bus, isa_irq);
    return isa_bus;
}

static void pci_ebus_realize(PCIDevice *pci_dev, Error **errp)
{
    EbusState *s = DO_UPCAST(EbusState, pci_dev, pci_dev);

    if (!isa_bus_new(DEVICE(pci_dev), get_system_memory(),
                     pci_address_space_io(pci_dev), errp)) {
        return;
    }

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
                             0, 0x4000);
    pci_register_bar(pci_dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar1);
}

static void ebus_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_ebus_realize;
    k->vendor_id = PCI_VENDOR_ID_SUN;
    k->device_id = PCI_DEVICE_ID_SUN_EBUS;
    k->revision = 0x01;
    k->class_id = PCI_CLASS_BRIDGE_OTHER;
}

static const TypeInfo ebus_info = {
    .name          = "ebus",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(EbusState),
    .class_init    = ebus_class_init,
};

#define TYPE_OPENPROM "openprom"
#define OPENPROM(obj) OBJECT_CHECK(PROMState, (obj), TYPE_OPENPROM)

typedef struct PROMState {
    SysBusDevice parent_obj;

    MemoryRegion prom;
} PROMState;

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

    dev = qdev_create(NULL, TYPE_OPENPROM);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);

    sysbus_mmio_map(s, 0, addr);

    /* load boot prom */
    if (bios_name == NULL) {
        bios_name = PROM_FILENAME;
    }
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (filename) {
        ret = load_elf(filename, translate_prom_address, &addr,
                       NULL, NULL, NULL, 1, EM_SPARCV9, 0, 0);
        if (ret < 0 || ret > PROM_SIZE_MAX) {
            ret = load_image_targphys(filename, addr, PROM_SIZE_MAX);
        }
        g_free(filename);
    } else {
        ret = -1;
    }
    if (ret < 0 || ret > PROM_SIZE_MAX) {
        fprintf(stderr, "qemu: could not load prom '%s'\n", bios_name);
        exit(1);
    }
}

static void prom_init1(Object *obj)
{
    PROMState *s = OPENPROM(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_ram_nomigrate(&s->prom, obj, "sun4u.prom", PROM_SIZE_MAX,
                           &error_fatal);
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

    dc->props = prom_properties;
}

static const TypeInfo prom_info = {
    .name          = TYPE_OPENPROM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PROMState),
    .class_init    = prom_class_init,
    .instance_init = prom_init1,
};


#define TYPE_SUN4U_MEMORY "memory"
#define SUN4U_RAM(obj) OBJECT_CHECK(RamDevice, (obj), TYPE_SUN4U_MEMORY)

typedef struct RamDevice {
    SysBusDevice parent_obj;

    MemoryRegion ram;
    uint64_t size;
} RamDevice;

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
    dev = qdev_create(NULL, TYPE_SUN4U_MEMORY);
    s = SYS_BUS_DEVICE(dev);

    d = SUN4U_RAM(dev);
    d->size = RAM_size;
    qdev_init_nofail(dev);

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
    dc->props = ram_properties;
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
    SPARCCPU *cpu;
    Nvram *nvram;
    unsigned int i;
    uint64_t initrd_addr, initrd_size, kernel_addr, kernel_size, kernel_entry;
    PCIBus *pci_bus, *pci_bus2, *pci_bus3;
    ISABus *isa_bus;
    SysBusDevice *s;
    qemu_irq *ivec_irqs, *pbm_irqs;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    DriveInfo *fd[MAX_FD];
    DeviceState *dev;
    FWCfgState *fw_cfg;

    /* init CPUs */
    cpu = sparc64_cpu_devinit(machine->cpu_model, hwdef->default_cpu_model,
                              hwdef->prom_addr);

    /* set up devices */
    ram_init(0, machine->ram_size);

    prom_init(hwdef->prom_addr, bios_name);

    ivec_irqs = qemu_allocate_irqs(sparc64_cpu_set_ivec_irq, cpu, IVEC_MAX);
    pci_bus = pci_apb_init(APB_SPECIAL_BASE, APB_MEM_BASE, ivec_irqs, &pci_bus2,
                           &pci_bus3, &pbm_irqs);
    pci_vga_init(pci_bus);

    // XXX Should be pci_bus3
    isa_bus = pci_ebus_init(pci_bus, -1, pbm_irqs);

    i = 0;
    if (hwdef->console_serial_base) {
        serial_mm_init(address_space_mem, hwdef->console_serial_base, 0,
                       NULL, 115200, serial_hds[i], DEVICE_BIG_ENDIAN);
        i++;
    }

    serial_hds_isa_init(isa_bus, i, MAX_SERIAL_PORTS);
    parallel_hds_isa_init(isa_bus, MAX_PARALLEL_PORTS);

    for(i = 0; i < nb_nics; i++)
        pci_nic_init_nofail(&nd_table[i], pci_bus, "ne2k_pci", NULL);

    ide_drive_get(hd, ARRAY_SIZE(hd));

    pci_cmd646_ide_init(pci_bus, hd, 1);

    isa_create_simple(isa_bus, "i8042");

    /* Floppy */
    for(i = 0; i < MAX_FD; i++) {
        fd[i] = drive_get(IF_FLOPPY, 0, i);
    }
    dev = DEVICE(isa_create(isa_bus, TYPE_ISA_FDC));
    if (fd[0]) {
        qdev_prop_set_drive(dev, "driveA", blk_by_legacy_dinfo(fd[0]),
                            &error_abort);
    }
    if (fd[1]) {
        qdev_prop_set_drive(dev, "driveB", blk_by_legacy_dinfo(fd[1]),
                            &error_abort);
    }
    qdev_prop_set_uint32(dev, "dma", -1);
    qdev_init_nofail(dev);

    /* Map NVRAM into I/O (ebus) space */
    nvram = m48t59_init(NULL, 0, 0, NVRAM_SIZE, 1968, 59);
    s = SYS_BUS_DEVICE(nvram);
    memory_region_add_subregion(get_system_io(), 0x2000,
                                sysbus_mmio_get_region(s, 0));
 
    initrd_size = 0;
    initrd_addr = 0;
    kernel_size = sun4u_load_kernel(machine->kernel_filename,
                                    machine->initrd_filename,
                                    ram_size, &initrd_size, &initrd_addr,
                                    &kernel_addr, &kernel_entry);

    sun4u_NVRAM_set_params(nvram, NVRAM_SIZE, "Sun4u", machine->ram_size,
                           machine->boot_order,
                           kernel_addr, kernel_size,
                           machine->kernel_cmdline,
                           initrd_addr, initrd_size,
                           /* XXX: need an option to load a NVRAM image */
                           0,
                           graphic_width, graphic_height, graphic_depth,
                           (uint8_t *)&nd_table[0].macaddr);

    fw_cfg = fw_cfg_init_io(BIOS_CFG_IOPORT);
    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, (uint16_t)smp_cpus);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, (uint16_t)max_cpus);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
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
    fw_cfg_add_i16(fw_cfg, FW_CFG_BOOT_DEVICE, machine->boot_order[0]);

    fw_cfg_add_i16(fw_cfg, FW_CFG_SPARC64_WIDTH, graphic_width);
    fw_cfg_add_i16(fw_cfg, FW_CFG_SPARC64_HEIGHT, graphic_height);
    fw_cfg_add_i16(fw_cfg, FW_CFG_SPARC64_DEPTH, graphic_depth);

    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);
}

enum {
    sun4u_id = 0,
    sun4v_id = 64,
};

static const struct hwdef hwdefs[] = {
    /* Sun4u generic PC-like machine */
    {
        .default_cpu_model = "TI UltraSparc IIi",
        .machine_id = sun4u_id,
        .prom_addr = 0x1fff0000000ULL,
        .console_serial_base = 0,
    },
    /* Sun4v generic PC-like machine */
    {
        .default_cpu_model = "Sun UltraSparc T1",
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

    mc->desc = "Sun4u platform";
    mc->init = sun4u_init;
    mc->block_default_type = IF_IDE;
    mc->max_cpus = 1; /* XXX for now */
    mc->is_default = 1;
    mc->default_boot_order = "c";
}

static const TypeInfo sun4u_type = {
    .name = MACHINE_TYPE_NAME("sun4u"),
    .parent = TYPE_MACHINE,
    .class_init = sun4u_class_init,
};

static void sun4v_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4v platform";
    mc->init = sun4v_init;
    mc->block_default_type = IF_IDE;
    mc->max_cpus = 1; /* XXX for now */
    mc->default_boot_order = "c";
}

static const TypeInfo sun4v_type = {
    .name = MACHINE_TYPE_NAME("sun4v"),
    .parent = TYPE_MACHINE,
    .class_init = sun4v_class_init,
};

static void sun4u_register_types(void)
{
    type_register_static(&ebus_info);
    type_register_static(&prom_info);
    type_register_static(&ram_info);

    type_register_static(&sun4u_type);
    type_register_static(&sun4v_type);
}

type_init(sun4u_register_types)
