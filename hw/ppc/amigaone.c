/*
 * QEMU Eyetech AmigaOne/Mai Logic Teron emulation
 *
 * Copyright (c) 2023 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/datadir.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/ppc/ppc.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/pci-host/articia.h"
#include "hw/isa/vt82c686.h"
#include "hw/ide/pci.h"
#include "hw/i2c/smbus_eeprom.h"
#include "hw/ppc/ppc.h"
#include "system/block-backend.h"
#include "system/qtest.h"
#include "system/reset.h"
#include "kvm_ppc.h"
#include "elf.h"

#include <zlib.h> /* for crc32 */

#define BUS_FREQ_HZ 100000000

#define INITRD_MIN_ADDR 0x600000
#define INIT_RAM_ADDR 0x40000000

#define PCI_HIGH_ADDR 0x80000000
#define PCI_HIGH_SIZE 0x7d000000
#define PCI_LOW_ADDR  0xfd000000
#define PCI_LOW_SIZE  0xe0000

#define ARTICIA_ADDR 0xfe000000

/*
 * Firmware binary available at
 * https://www.hyperion-entertainment.com/index.php/downloads?view=files&parent=28
 * then "tail -c 524288 updater.image >u-boot-amigaone.bin"
 *
 * BIOS emulator in firmware cannot run QEMU vgabios and hangs on it, use
 * -device VGA,romfile=VGABIOS-lgpl-latest.bin
 * from http://www.nongnu.org/vgabios/ instead.
 */
#define PROM_ADDR 0xfff00000
#define PROM_SIZE (512 * KiB)

/* AmigaOS calls this routine from ROM, use this if no firmware loaded */
static const char dummy_fw[] = {
    0x54, 0x63, 0xc2, 0x3e, /* srwi    r3,r3,8 */
    0x7c, 0x63, 0x18, 0xf8, /* not     r3,r3 */
    0x4e, 0x80, 0x00, 0x20, /* blr */
};

#define NVRAM_ADDR 0xfd0e0000
#define NVRAM_SIZE (4 * KiB)

static const char default_env[] =
    "baudrate=115200\0"
    "stdout=vga\0"
    "stdin=ps2kbd\0"
    "bootcmd=boota; menu; run menuboot_cmd\0"
    "boot1=ide\0"
    "boot2=cdrom\0"
    "boota_timeout=3\0"
    "ide_doreset=on\0"
    "pci_irqa=9\0"
    "pci_irqa_select=level\0"
    "pci_irqb=10\0"
    "pci_irqb_select=level\0"
    "pci_irqc=11\0"
    "pci_irqc_select=level\0"
    "pci_irqd=7\0"
    "pci_irqd_select=level\0"
    "a1ide_irq=1111\0"
    "a1ide_xfer=FFFF\0";
#define CRC32_DEFAULT_ENV 0xb5548481
#define CRC32_ALL_ZEROS   0x603b0489

#define TYPE_A1_NVRAM "a1-nvram"
OBJECT_DECLARE_SIMPLE_TYPE(A1NVRAMState, A1_NVRAM)

struct A1NVRAMState {
    SysBusDevice parent_obj;

    MemoryRegion mr;
    BlockBackend *blk;
};

static uint64_t nvram_read(void *opaque, hwaddr addr, unsigned int size)
{
    /* read callback not used because of romd mode */
    g_assert_not_reached();
}

static void nvram_write(void *opaque, hwaddr addr, uint64_t val,
                        unsigned int size)
{
    A1NVRAMState *s = opaque;
    uint8_t *p = memory_region_get_ram_ptr(&s->mr);

    p[addr] = val;
    if (s->blk && blk_pwrite(s->blk, addr, 1, &val, 0) < 0) {
        error_report("%s: could not write %s", __func__, blk_name(s->blk));
    }
}

static const MemoryRegionOps nvram_ops = {
    .read = nvram_read,
    .write = nvram_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void nvram_realize(DeviceState *dev, Error **errp)
{
    A1NVRAMState *s = A1_NVRAM(dev);
    void *p;
    uint32_t crc, *c;

    memory_region_init_rom_device(&s->mr, NULL, &nvram_ops, s, "nvram",
                                  NVRAM_SIZE, &error_fatal);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mr);
    c = p = memory_region_get_ram_ptr(&s->mr);
    if (s->blk) {
        if (blk_getlength(s->blk) != NVRAM_SIZE) {
            error_setg(errp, "NVRAM backing file size must be %" PRId64 "bytes",
                       NVRAM_SIZE);
            return;
        }
        blk_set_perm(s->blk, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                     BLK_PERM_ALL, &error_fatal);
        if (blk_pread(s->blk, 0, NVRAM_SIZE, p, 0) < 0) {
            error_setg(errp, "Cannot read NVRAM contents from backing file");
            return;
        }
    }
    crc = crc32(0, p + 4, NVRAM_SIZE - 4);
    if (crc == CRC32_ALL_ZEROS) { /* If env is uninitialized set default */
        *c = cpu_to_be32(CRC32_DEFAULT_ENV);
        /* Also copies terminating \0 as env is terminated by \0\0 */
        memcpy(p + 4, default_env, sizeof(default_env));
        if (s->blk &&
            blk_pwrite(s->blk, 0, sizeof(crc) + sizeof(default_env), p, 0) < 0
           ) {
            error_report("%s: could not write %s", __func__, blk_name(s->blk));
        }
        return;
    }
    if (*c == 0) {
        *c = cpu_to_be32(crc32(0, p + 4, NVRAM_SIZE - 4));
        if (s->blk && blk_pwrite(s->blk, 0, 4, p, 0) < 0) {
            error_report("%s: could not write %s", __func__, blk_name(s->blk));
        }
    }
    if (be32_to_cpu(*c) != crc) {
        warn_report("NVRAM checksum mismatch");
    }
}

static const Property nvram_properties[] = {
    DEFINE_PROP_DRIVE("drive", A1NVRAMState, blk),
};

static void nvram_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = nvram_realize;
    device_class_set_props(dc, nvram_properties);
}

static const TypeInfo nvram_types[] = {
    {
        .name = TYPE_A1_NVRAM,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(A1NVRAMState),
        .class_init = nvram_class_init,
    },
};
DEFINE_TYPES(nvram_types)

struct boot_info {
    hwaddr entry;
    hwaddr stack;
    hwaddr bd_info;
    hwaddr initrd_start;
    hwaddr initrd_end;
    hwaddr cmdline_start;
    hwaddr cmdline_end;
};

/* Board info struct from U-Boot */
struct bd_info {
    uint32_t bi_memstart;
    uint32_t bi_memsize;
    uint32_t bi_flashstart;
    uint32_t bi_flashsize;
    uint32_t bi_flashoffset;
    uint32_t bi_sramstart;
    uint32_t bi_sramsize;
    uint32_t bi_bootflags;
    uint32_t bi_ip_addr;
    uint8_t  bi_enetaddr[6];
    uint16_t bi_ethspeed;
    uint32_t bi_intfreq;
    uint32_t bi_busfreq;
    uint32_t bi_baudrate;
} QEMU_PACKED;

static void create_bd_info(hwaddr addr, ram_addr_t ram_size)
{
    struct bd_info *bd = g_new0(struct bd_info, 1);

    bd->bi_memsize =    cpu_to_be32(ram_size);
    bd->bi_flashstart = cpu_to_be32(PROM_ADDR);
    bd->bi_flashsize =  cpu_to_be32(1); /* match what U-Boot detects */
    bd->bi_bootflags =  cpu_to_be32(1);
    bd->bi_intfreq =    cpu_to_be32(11.5 * BUS_FREQ_HZ);
    bd->bi_busfreq =    cpu_to_be32(BUS_FREQ_HZ);
    bd->bi_baudrate =   cpu_to_be32(115200);

    cpu_physical_memory_write(addr, bd, sizeof(*bd));
}

static void amigaone_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;

    cpu_reset(CPU(cpu));
    if (env->load_info) {
        struct boot_info *bi = env->load_info;

        env->gpr[1] = bi->stack;
        env->gpr[2] = 1024;
        env->gpr[3] = bi->bd_info;
        env->gpr[4] = bi->initrd_start;
        env->gpr[5] = bi->initrd_end;
        env->gpr[6] = bi->cmdline_start;
        env->gpr[7] = bi->cmdline_end;
        env->nip = bi->entry;
    }
    cpu_ppc_tb_reset(env);
}

static void fix_spd_data(uint8_t *spd)
{
    uint32_t bank_size = 4 * MiB * spd[31];
    uint32_t rows = bank_size / spd[13] / spd[17];
    spd[3] = ctz32(rows) - spd[4];
}

static void amigaone_init(MachineState *machine)
{
    PowerPCCPU *cpu;
    CPUPPCState *env;
    MemoryRegion *rom, *pci_mem, *mr;
    ssize_t sz;
    PCIBus *pci_bus;
    Object *via;
    DeviceState *dev;
    I2CBus *i2c_bus;
    uint8_t *spd_data;
    DriveInfo *di;
    hwaddr loadaddr;
    struct boot_info *bi = NULL;

    /* init CPU */
    cpu = POWERPC_CPU(cpu_create(machine->cpu_type));
    env = &cpu->env;
    if (PPC_INPUT(env) != PPC_FLAGS_INPUT_6xx) {
        error_report("Incompatible CPU, only 6xx bus supported");
        exit(1);
    }
    cpu_ppc_tb_init(env, BUS_FREQ_HZ / 4);
    qemu_register_reset(amigaone_cpu_reset, cpu);

    /* RAM */
    if (machine->ram_size > 2 * GiB) {
        error_report("RAM size more than 2 GiB is not supported");
        exit(1);
    }
    memory_region_add_subregion(get_system_memory(), 0, machine->ram);
    if (machine->ram_size < 1 * GiB + 32 * KiB) {
        /* Firmware uses this area for startup */
        mr = g_new(MemoryRegion, 1);
        memory_region_init_ram(mr, NULL, "init-cache", 32 * KiB, &error_fatal);
        memory_region_add_subregion(get_system_memory(), INIT_RAM_ADDR, mr);
    }

    /* nvram */
    dev = qdev_new(TYPE_A1_NVRAM);
    di = drive_get(IF_MTD, 0, 0);
    if (di) {
        qdev_prop_set_drive(dev, "drive", blk_by_legacy_dinfo(di));
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    memory_region_add_subregion(get_system_memory(), NVRAM_ADDR,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0));

    /* allocate and load firmware */
    rom = g_new(MemoryRegion, 1);
    memory_region_init_rom(rom, NULL, "rom", PROM_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), PROM_ADDR, rom);
    if (!machine->firmware) {
        rom_add_blob_fixed("dummy-fw", dummy_fw, sizeof(dummy_fw),
                           PROM_ADDR + PROM_SIZE - 0x80);
    } else {
        g_autofree char *filename = qemu_find_file(QEMU_FILE_TYPE_BIOS,
                                                   machine->firmware);
        if (!filename) {
            error_report("Could not find firmware '%s'", machine->firmware);
            exit(1);
        }
        sz = load_image_targphys(filename, PROM_ADDR, PROM_SIZE);
        if (sz <= 0 || sz > PROM_SIZE) {
            error_report("Could not load firmware '%s'", filename);
            exit(1);
        }
    }

    /* Articia S */
    dev = sysbus_create_simple(TYPE_ARTICIA, ARTICIA_ADDR, NULL);

    i2c_bus = I2C_BUS(qdev_get_child_bus(dev, "smbus"));
    if (machine->ram_size > 512 * MiB) {
        spd_data = spd_data_generate(SDR, machine->ram_size / 2);
    } else {
        spd_data = spd_data_generate(SDR, machine->ram_size);
    }
    fix_spd_data(spd_data);
    smbus_eeprom_init_one(i2c_bus, 0x51, spd_data);
    if (machine->ram_size > 512 * MiB) {
        smbus_eeprom_init_one(i2c_bus, 0x52, spd_data);
    }

    pci_mem = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    mr = g_new(MemoryRegion, 1);
    memory_region_init_alias(mr, OBJECT(dev), "pci-mem-low", pci_mem,
                             0, PCI_LOW_SIZE);
    memory_region_add_subregion(get_system_memory(), PCI_LOW_ADDR, mr);
    mr = g_new(MemoryRegion, 1);
    memory_region_init_alias(mr, OBJECT(dev), "pci-mem-high", pci_mem,
                             PCI_HIGH_ADDR, PCI_HIGH_SIZE);
    memory_region_add_subregion(get_system_memory(), PCI_HIGH_ADDR, mr);
    pci_bus = PCI_BUS(qdev_get_child_bus(dev, "pci.0"));

    /* VIA VT82c686B South Bridge (multifunction PCI device) */
    via = OBJECT(pci_create_simple_multifunction(pci_bus, PCI_DEVFN(7, 0),
                                                 TYPE_VT82C686B_ISA));
    object_property_add_alias(OBJECT(machine), "rtc-time",
                              object_resolve_path_component(via, "rtc"),
                              "date");
    qdev_connect_gpio_out_named(DEVICE(via), "intr", 0,
                                qdev_get_gpio_in(DEVICE(cpu),
                                PPC6xx_INPUT_INT));
    for (int i = 0; i < PCI_NUM_PINS; i++) {
        qdev_connect_gpio_out(dev, i, qdev_get_gpio_in_named(DEVICE(via),
                                                             "pirq", i));
    }
    pci_ide_create_devs(PCI_DEVICE(object_resolve_path_component(via, "ide")));
    pci_vga_init(pci_bus);

    if (!machine->kernel_filename) {
        return;
    }

    /* handle -kernel, -initrd, -append options and emulate U-Boot */
    bi = g_new0(struct boot_info, 1);
    cpu->env.load_info = bi;

    loadaddr = MIN(machine->ram_size, 256 * MiB);
    bi->bd_info = loadaddr - 8 * MiB;
    create_bd_info(bi->bd_info, machine->ram_size);
    bi->stack = bi->bd_info - 64 * KiB - 8;

    if (machine->kernel_cmdline && machine->kernel_cmdline[0]) {
        size_t len = strlen(machine->kernel_cmdline);

        loadaddr = bi->bd_info + 1 * MiB;
        cpu_physical_memory_write(loadaddr, machine->kernel_cmdline, len + 1);
        bi->cmdline_start = loadaddr;
        bi->cmdline_end = loadaddr + len + 1; /* including terminating '\0' */
    }

    sz = load_elf(machine->kernel_filename, NULL, NULL, NULL,
                  &bi->entry, &loadaddr, NULL, NULL,
                  ELFDATA2MSB, PPC_ELF_MACHINE, 0, 0);
    if (sz <= 0) {
        sz = load_uimage(machine->kernel_filename, &bi->entry, &loadaddr,
                         NULL, NULL, NULL);
    }
    if (sz <= 0) {
        error_report("Could not load kernel '%s'",
                     machine->kernel_filename);
        exit(1);
    }
    loadaddr += sz;

    if (machine->initrd_filename) {
        loadaddr = ROUND_UP(loadaddr + 4 * MiB, 4 * KiB);
        loadaddr = MAX(loadaddr, INITRD_MIN_ADDR);
        sz = load_image_targphys(machine->initrd_filename, loadaddr,
                                 bi->bd_info - loadaddr);
        if (sz <= 0) {
            error_report("Could not load initrd '%s'",
                         machine->initrd_filename);
            exit(1);
        }
        bi->initrd_start = loadaddr;
        bi->initrd_end = loadaddr + sz;
    }
}

static void amigaone_machine_init(MachineClass *mc)
{
    mc->desc = "Eyetech AmigaOne/Mai Logic Teron";
    mc->init = amigaone_init;
    mc->block_default_type = IF_IDE;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("7457_v1.2");
    mc->default_display = "std";
    mc->default_ram_id = "ram";
    mc->default_ram_size = 512 * MiB;
}

DEFINE_MACHINE("amigaone", amigaone_machine_init)
