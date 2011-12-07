/* hw/bast.c
 *
 * System emulation for the Simtec Electronics BAST
 *
 * Copyright 2006, 2008 Daniel Silverstone and Vincent Sanders
 *
 * This file is under the terms of the GNU General Public
 * License Version 2.
 *
 * TODO:
 * * Undefined r/w at address 0x118002f9 (serial i/o?).
 * * Undefined r/w at address 0x118003f9 (serial i/o?).
 * * Undefined r/w at address 0x29000000 ff (ax88796).
 * * Undefined r/w at address 0x4b000000 ff.
 * * Undefined r/w at address 0x55000000 ff (iis).
 * * eth1 is 10 Mbps half duplex only.
 */

#include "hw.h"
#include "blockdev.h"           /* drive_get */
#include "sysbus.h"             /* sysbus_from_qdev, ... */
#include "sysemu.h"
#include "arm-misc.h"
#include "loader.h"             /* load_image_targphys */
#include "net.h"
#include "smbus.h"
#include "devices.h"
#include "boards.h"
#include "pc.h"                 /* serial_isa_init */
#include "dma.h"                /* QEMUSGList (in ide/internal.h) */
#include "exec-memory.h"        /* get_system_memory */
#include "ide/internal.h"       /* ide_cmd_write, ... */

#include "s3c2410x.h"

#define BIOS_FILENAME "able.bin"

#define S3C24XX_DBF(format, ...) (void)0

static int bigendian = 0;

typedef struct {
    S3CState *soc;
    DeviceState *nand[4];
    uint8_t cpld_ctrl2;
} STCBState;

/* Useful defines */
#define BAST_NOR_RO_BASE CPU_S3C2410X_CS0
#define BAST_NOR_RW_BASE (CPU_S3C2410X_CS1 + 0x4000000)
#define BAST_NOR_SIZE    (2 * MiB)
#define BAST_BOARD_ID 331

#define BAST_CS1_CPLD_BASE (CPU_S3C2410X_CS1 | (0xc << 23))
#define BAST_CS5_CPLD_BASE (CPU_S3C2410X_CS5 | (0xc << 23))
#define BAST_CPLD_SIZE (4<<23)

static uint32_t cpld_read(void *opaque, target_phys_addr_t address)
{
    STCBState *stcb = opaque;
    int reg = (address >> 23) & 0xf;
    if (reg == 0xc) {
        return stcb->cpld_ctrl2;
    }
    return 0;
}

static void cpld_write(void *opaque, target_phys_addr_t address,
                       uint32_t value)
{
    STCBState *stcb = opaque;
    int reg = (address >> 23) & 0xf;
    if (reg == 0xc) {
        stcb->cpld_ctrl2 = value;
        s3c24xx_nand_attach(stcb->soc->nand, stcb->nand[stcb->cpld_ctrl2 & 3]);
    }
}

static CPUReadMemoryFunc * const cpld_readfn[] = {
    cpld_read,
    cpld_read,
    cpld_read
};

static CPUWriteMemoryFunc * const cpld_writefn[] = {
    cpld_write,
    cpld_write,
    cpld_write
};

static void stcb_cpld_register(STCBState *stcb)
{
    int tag = cpu_register_io_memory(cpld_readfn, cpld_writefn, stcb,
                                     DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(BAST_CS1_CPLD_BASE, BAST_CPLD_SIZE, tag);
    cpu_register_physical_memory(BAST_CS5_CPLD_BASE, BAST_CPLD_SIZE, tag);
    stcb->cpld_ctrl2 = 0;
}

#define BAST_IDE_PRI_SLOW    (CPU_S3C2410X_CS3 | 0x02000000)
#define BAST_IDE_SEC_SLOW    (CPU_S3C2410X_CS3 | 0x03000000)
#define BAST_IDE_PRI_FAST    (CPU_S3C2410X_CS5 | 0x02000000)
#define BAST_IDE_SEC_FAST    (CPU_S3C2410X_CS5 | 0x03000000)

#define BAST_IDE_PRI_SLOW_BYTE    (CPU_S3C2410X_CS2 | 0x02000000)
#define BAST_IDE_SEC_SLOW_BYTE    (CPU_S3C2410X_CS2 | 0x03000000)
#define BAST_IDE_PRI_FAST_BYTE    (CPU_S3C2410X_CS4 | 0x02000000)
#define BAST_IDE_SEC_FAST_BYTE    (CPU_S3C2410X_CS4 | 0x03000000)

/* MMIO interface to IDE on Simtec's BAST
 *
 * Copyright Daniel Silverstone and Vincent Sanders
 *
 * This section of this file is under the terms of
 * the GNU General Public License Version 2
 */

/* Each BAST IDE region is 0x01000000 bytes long,
 * the second half is the "alternate" register set
 */

typedef struct {
    IDEBus bus;
    int shift;
} MMIOState;

static void stcb_ide_write_f(void *opaque,
                             target_phys_addr_t addr, uint32_t val)
{
    MMIOState *s= opaque;
    int reg = (addr & 0x3ff) >> 5; /* 0x200 long, 0x20 stride */
    int alt = (addr & 0x800000) != 0;
    S3C24XX_DBF("IDE write to addr %08x (reg %d) of value %04x\n", (unsigned int)addr, reg, val);
    if (alt) {
        ide_cmd_write(&s->bus, 0, val);
    }
    if (reg == 0) {
        /* Data register */
        ide_data_writew(&s->bus, 0, val);
    } else {
        /* Everything else */
        ide_ioport_write(&s->bus, reg, val);
    }
}

static uint32_t stcb_ide_read_f(void *opaque, target_phys_addr_t addr)
{
    MMIOState *s= opaque;
    int reg = (addr & 0x3ff) >> 5; /* 0x200 long, 0x20 stride */
    int alt = (addr & 0x800000) != 0;
    S3C24XX_DBF("IDE read of addr %08x (reg %d)\n", (unsigned int)addr, reg);
    if (alt) {
        return ide_status_read(&s->bus, 0);
    }
    if (reg == 0) {
        return ide_data_readw(&s->bus, 0);
    } else {
        return ide_ioport_read(&s->bus, reg);
    }
}


static CPUWriteMemoryFunc * const stcb_ide_write[] = {
    stcb_ide_write_f,
    stcb_ide_write_f,
    stcb_ide_write_f,
};

static CPUReadMemoryFunc * const stcb_ide_read[] = {
    stcb_ide_read_f,
    stcb_ide_read_f,
    stcb_ide_read_f,
};

/* hd_table must contain 2 block drivers */
/* BAST uses memory mapped registers, not I/O. Return the memory
 * I/O tag to access the ide.
 * The BAST description will register it into the map in the right place.
 */
static int stcb_ide_init(DriveInfo *dinfo0, DriveInfo *dinfo1, qemu_irq irq)
{
    MMIOState *s = g_malloc0(sizeof(MMIOState));
    int stcb_ide_memory;
    ide_init2_with_non_qdev_drives(&s->bus, dinfo0, dinfo1, irq);

    stcb_ide_memory = cpu_register_io_memory(stcb_ide_read, stcb_ide_write,
                                             s, DEVICE_NATIVE_ENDIAN);
    return stcb_ide_memory;
}

static void stcb_register_ide(STCBState *stcb)
{
    int ide0_mem;
    int ide1_mem;
    DriveInfo *dinfo0;
    DriveInfo *dinfo1;

    if (drive_get_max_bus(IF_IDE) >= 2) {
        fprintf(stderr, "qemu: too many IDE busses\n");
        exit(1);
    }

    dinfo0 = drive_get(IF_IDE, 0, 0);
    dinfo1 = drive_get(IF_IDE, 0, 1);
    ide0_mem = stcb_ide_init(dinfo0, dinfo1, s3c24xx_get_eirq(stcb->soc->gpio, 16));
    cpu_register_physical_memory(BAST_IDE_PRI_SLOW, 0x1000000, ide0_mem);
    cpu_register_physical_memory(BAST_IDE_PRI_FAST, 0x1000000, ide0_mem);
    cpu_register_physical_memory(BAST_IDE_PRI_SLOW_BYTE, 0x1000000, ide0_mem);
    cpu_register_physical_memory(BAST_IDE_PRI_FAST_BYTE, 0x1000000, ide0_mem);

    dinfo0 = drive_get(IF_IDE, 1, 0);
    dinfo1 = drive_get(IF_IDE, 1, 1);
    ide1_mem = stcb_ide_init(dinfo0, dinfo1, s3c24xx_get_eirq(stcb->soc->gpio, 17));
    cpu_register_physical_memory(BAST_IDE_SEC_SLOW, 0x1000000, ide1_mem);
    cpu_register_physical_memory(BAST_IDE_SEC_FAST, 0x1000000, ide1_mem);
    cpu_register_physical_memory(BAST_IDE_SEC_SLOW_BYTE, 0x1000000, ide1_mem);
    cpu_register_physical_memory(BAST_IDE_SEC_FAST_BYTE, 0x1000000, ide1_mem);
}

#define BAST_PA_ASIXNET 0x01000000
#define BAST_PA_SUPERIO 0x01800000

#define SERIAL_BASE     (CPU_S3C2410X_CS2 + BAST_PA_SUPERIO)
#define SERIAL_CLK      1843200

#define ASIXNET_BASE    (CPU_S3C2410X_CS5 + BAST_PA_ASIXNET)
#define ASIXNET_SIZE    (0x400)
#define AX88796_BASE    (CPU_S3C2410X_CS5 + BAST_PA_ASIXNET + (0x18 * 0x20))
#define AX88796_SIZE    (3 * 0x20)

#define DM9000_BASE     (0x2d000000)
#define DM9000_IRQ      10

#define logout(fmt, ...) \
    fprintf(stderr, "AX88796\t%-24s" fmt, __func__, ##__VA_ARGS__)

typedef struct {
    SysBusDevice busdev;
    MemoryRegion mmio;
    NICState *nic;
    NICConf conf;
} AX88796State;

static uint64_t ax88796_read(void *opaque, target_phys_addr_t offset,
                             unsigned size)
{
    //~ AX88796State *s = opaque;
    uint32_t value = 0;

    switch (offset) {
        case 0x0000:
        case 0x000d:
        case 0x0020:
        case 0x0040:
        case 0x0060:
        case 0x0080:
        case 0x00a0:
        case 0x00c0:
        case 0x00e0:
        case 0x02e0:
        case 0x03e0:
            ;
            //~ return 0; // FIXME
    }

    logout("0x" TARGET_FMT_plx " 0x%08x\n", offset, value);
    return value;
}

static void ax88796_write(void *opaque, target_phys_addr_t offset,
                          uint64_t value, unsigned size)
{
    //~ AX88796State *s = opaque;
    switch (offset) {
        case 0x0000:
        case 0x000d:
        case 0x0020:
        case 0x0040:
        case 0x0060:
        case 0x0080:
        case 0x00a0:
        case 0x00c0:
        case 0x00e0:
        case 0x02e0:
        case 0x03e0:
            ;
            //~ return; // FIXME
    }
    logout("0x" TARGET_FMT_plx " 0x%08" PRIx64 "\n", offset, value);
}

static const MemoryRegionOps ax88796_ops = {
    .read = ax88796_read,
    .write = ax88796_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static int ax88796_init(SysBusDevice *dev)
{
    AX88796State *s = FROM_SYSBUS(AX88796State, dev);

    logout("\n");

    memory_region_init_io(&s->mmio, &ax88796_ops, s, "ax88796", ASIXNET_SIZE);
    //~ sysbus_init_mmio(dev, AX88796_SIZE, iomemtype);
    sysbus_init_mmio(dev, &s->mmio);
    //~ sysbus_init_irq(dev, &s->irq);
    //~ ax88796_reset(s);
#if 0
    isa_ne2000_init(ne2000_io[i], ne2000_irq[i], &nd_table[i]);
    ISANE2000State *isa = DO_UPCAST(ISANE2000State, dev, dev);
    NE2000State *s = &isa->ne2000;

    register_ioport_write(isa->iobase, 16, 1, ne2000_ioport_write, s);
    register_ioport_read(isa->iobase, 16, 1, ne2000_ioport_read, s);

    register_ioport_write(isa->iobase + 0x10, 1, 1, ne2000_asic_ioport_write, s);
    register_ioport_read(isa->iobase + 0x10, 1, 1, ne2000_asic_ioport_read, s);
    register_ioport_write(isa->iobase + 0x10, 2, 2, ne2000_asic_ioport_write, s);
    register_ioport_read(isa->iobase + 0x10, 2, 2, ne2000_asic_ioport_read, s);

    register_ioport_write(isa->iobase + 0x1f, 1, 1, ne2000_reset_ioport_write, s);
    register_ioport_read(isa->iobase + 0x1f, 1, 1, ne2000_reset_ioport_read, s);

    isa_init_irq(dev, &s->irq, isa->isairq);

    qemu_macaddr_default_if_unset(&s->c.macaddr);
    ne2000_reset(s);

    s->nic = qemu_new_nic(&net_ne2000_isa_info, &s->c,
                          dev->qdev.info->name, dev->qdev.id, s);
    qemu_format_nic_info_str(&s->nic->nc, s->c.macaddr.a);
#endif
    return 0;
}

static const VMStateDescription ax88796_vmsd = {
    .name = "ax88796",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo ax88796_info = {
    .init = ax88796_init,
    .qdev.name = "ax88796",
    .qdev.size = sizeof(AX88796State),
    .qdev.vmsd = &ax88796_vmsd,
    .qdev.props = (Property[]) {
        DEFINE_NIC_PROPERTIES(AX88796State, conf),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void ax88796_register_device(void)
{
    sysbus_register_withprop(&ax88796_info);
}

device_init(ax88796_register_device)


static void stcb_i2c_setup(STCBState *stcb)
{
    i2c_bus *bus = s3c24xx_i2c_bus(stcb->soc->iic);
    uint8_t *eeprom_buf = g_malloc0(256);
    DeviceState *eeprom;
    eeprom = qdev_create((BusState *)bus, "smbus-eeprom");
    qdev_prop_set_uint8(eeprom, "address", 0x50);
    qdev_prop_set_ptr(eeprom, "data", eeprom_buf);
    qdev_init_nofail(eeprom);

    i2c_create_slave(bus, "ch7xxx", 0x75);
    i2c_create_slave(bus, "stcpmu", 0x6B);
}


static struct arm_boot_info bast_binfo = {
    .board_id = BAST_BOARD_ID,
    .ram_size = 0x10000000, /* 256MB */
};

static void stcb_init(ram_addr_t _ram_size,
                      const char *boot_device,
                      const char *kernel_filename, const char *kernel_cmdline,
                      const char *initrd_filename, const char *cpu_model)
{
    MemoryRegion *address_space_mem = get_system_memory();
    STCBState *stcb;
    CharDriverState *chr;
    DeviceState *dev;
    DriveInfo *dinfo;
    NICInfo *nd;
    SysBusDevice *s;
    int i;
    int ret;
    BlockDriverState *flash_bds = NULL;
    //~ qemu_irq *i8259;

    /* ensure memory is limited to 256MB */
    if (_ram_size > (256 * MiB)) {
        _ram_size = 256 * MiB;
    }
    ram_size = _ram_size;

    /* initialise board informations */
    bast_binfo.ram_size = ram_size;
    bast_binfo.kernel_filename = kernel_filename;
    bast_binfo.kernel_cmdline = kernel_cmdline;
    bast_binfo.initrd_filename = initrd_filename;
    bast_binfo.nb_cpus = 1;
    bast_binfo.loader_start = BAST_NOR_RO_BASE;

    /* allocate storage for board state */
    stcb = g_malloc0(sizeof(STCBState));

    /* Make sure all serial ports are associated with a device. */
    for (i = 0; i < MAX_SERIAL_PORTS; i++) {
        if (!serial_hds[i]) {
            char label[32];
            snprintf(label, sizeof(label), "serial%d", i);
            serial_hds[i] = qemu_chr_new(label, "vc:80Cx24C", NULL);
        }
    }

    /* initialise SOC */
    stcb->soc = s3c2410x_init(ram_size);

    stcb_register_ide(stcb);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    /* Aquire flash contents and register pflash device */
    if (dinfo) {
        /* load from specified flash device */
        flash_bds = dinfo->bdrv;
    } else {
        /* Try and load default bootloader image */
        char *filename= qemu_find_file(QEMU_FILE_TYPE_BIOS, BIOS_FILENAME);
        if (filename) {
            ret = load_image_targphys(filename,
                                      BAST_NOR_RO_BASE, BAST_NOR_SIZE);
            g_free(filename);
            (void)ret;
        }
    }

    pflash_cfi02_register(BAST_NOR_RW_BASE, NULL, "bast.flash",
                          BAST_NOR_SIZE, flash_bds,
                          65536, 32, 1, 2,
                          0x00BF, 0x234B, 0x0000, 0x0000, 0x5555, 0x2AAA,
                          bigendian);
    /* TODO: Read only ROM type mapping */
    //~ cpu_register_physical_memory(BAST_NOR_RO_BASE,
                                 //~ BAST_NOR_SIZE,
                                 //~ flash_mem | IO_MEM_ROM);

    /* if kernel is given, boot that directly */
    if (kernel_filename != NULL) {
        bast_binfo.loader_start = CPU_S3C2410X_DRAM;
        //~ bast_binfo.loader_start = 0xc0108000 - 0x00010000;
        arm_load_kernel(stcb->soc->cpu_env, &bast_binfo);
    }

    /* Setup initial (reset) program counter */
    stcb->soc->cpu_env->regs[15] = bast_binfo.loader_start;

    nd = &nd_table[0];
    if (nd->vlan) {
        qemu_check_nic_model(nd, "dm9000");
        dev = qdev_create(NULL, "dm9000");
        qdev_set_nic_properties(dev, nd);
        qdev_init_nofail(dev);
        s = sysbus_from_qdev(dev);
        sysbus_mmio_map(s, 0, DM9000_BASE);
        sysbus_connect_irq(s, 0, s3c24xx_get_eirq(stcb->soc->gpio, DM9000_IRQ));
    }

    nd = &nd_table[1];
    if (nd->vlan) {
        qemu_check_nic_model(nd, "ax88796");
        dev = qdev_create(NULL, "ax88796");
        qdev_set_nic_properties(dev, nd);
        qdev_init_nofail(dev);
        s = sysbus_from_qdev(dev);
        sysbus_mmio_map(s, 0, ASIXNET_BASE);
        logout("ASIXNET_BASE = 0x%08x\n", ASIXNET_BASE);
        logout("AX88796_BASE = 0x%08x\n", AX88796_BASE);
        //~ sysbus_connect_irq(s, 0, s3c24xx_get_eirq(stcb->soc->gpio, AX88796_IRQ));
    }

    /* Initialise the BAST CPLD */
    stcb_cpld_register(stcb);

    /* attach i2c devices */
    stcb_i2c_setup(stcb);

    /* Attach some NAND devices */
    stcb->nand[0] = NULL;
    stcb->nand[1] = NULL;
    dinfo = drive_get(IF_MTD, 0, 0);
    if (!dinfo) {
        stcb->nand[2] = NULL;
    } else {
        stcb->nand[2] = nand_init(NULL, 0xEC, 0x79); /* 128MiB small-page */
    }

    chr = qemu_chr_new("uart0", "vc:80Cx24C", NULL);
    serial_mm_init(address_space_mem, SERIAL_BASE + 0x2f8, 0,
                   s3c24xx_get_eirq(stcb->soc->gpio, 15),
                   SERIAL_CLK, chr, 0);
    chr = qemu_chr_new("uart1", "vc:80Cx24C", NULL);
    serial_mm_init(address_space_mem, SERIAL_BASE + 0x3f8, 0,
                   s3c24xx_get_eirq(stcb->soc->gpio, 14),
                   SERIAL_CLK, chr, 0);
#if 0
    /* Super I/O */
    isa_bus_new(NULL);
    i8259 = i8259_init(s3c24xx_get_eirq(stcb->soc->gpio, 4));
    isa_bus_irqs(i8259);
    /*isa_dev =*/ isa_create_simple("i8042");
    serial_isa_init(0, serial_hds[0]);
    serial_isa_init(1, serial_hds[1]);
#endif
}

static QEMUMachine bast_machine = {
    .name = "bast",
    .desc = "Simtec Electronics BAST (S3C2410A, ARM920T)",
    .init = stcb_init,
    .max_cpus = 1,
};

static void bast_machine_init(void)
{
    qemu_register_machine(&bast_machine);
}

machine_init(bast_machine_init);
