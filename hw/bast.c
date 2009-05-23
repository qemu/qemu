/* hw/bast.c
 *
 * System emulation for the Simtec Electronics BAST
 *
 * Copyright 2006, 2008 Daniel Silverstone and Vincent Sanders
 *
 * This file is under the terms of the GNU General Public
 * License Version 2.
 */

#include "hw.h"
#include "sysemu.h"
#include "arm-misc.h"
#include "net.h"
#include "smbus.h"
#include "devices.h"
#include "boards.h"

#include "s3c2410x.h"

#define BIOS_FILENAME "able.bin"

typedef struct {
    S3CState *soc;
    unsigned char cpld_ctrl2;
    NANDFlashState *nand[4];
} STCBState;

/* Bytes in a Kilobyte */
#define KILO 1024
/* Bytes in a megabyte */
#define MEGA 1024 * KILO
/* Bytes */
#define BYTE 1
/* Bits in a byte */
#define BIT 8

/* Useful defines */
#define BAST_NOR_RO_BASE CPU_S3C2410X_CS0
#define BAST_NOR_RW_BASE (CPU_S3C2410X_CS1 + 0x4000000)
#define BAST_NOR_SIZE 16 * MEGA / BIT
#define BAST_BOARD_ID 331

#define BAST_CS1_CPLD_BASE ((target_phys_addr_t)(CPU_S3C2410X_CS1 | (0xc << 23)))
#define BAST_CS5_CPLD_BASE ((target_phys_addr_t)(CPU_S3C2410X_CS5 | (0xc << 23)))
#define BAST_CPLD_SIZE (4<<23)

static uint32_t cpld_read(void *opaque, target_phys_addr_t address)
{
    STCBState *stcb = (STCBState *)opaque;
    int reg = (address >> 23) & 0xf;
    if (reg == 0xc)
        return stcb->cpld_ctrl2;
    return 0;
}

static void cpld_write(void *opaque, target_phys_addr_t address,
                       uint32_t value)
{
    STCBState *stcb = (STCBState *)opaque;
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
    int tag = cpu_register_io_memory(cpld_readfn, cpld_writefn, stcb);
    cpu_register_physical_memory(BAST_CS1_CPLD_BASE, BAST_CPLD_SIZE, tag);
    cpu_register_physical_memory(BAST_CS5_CPLD_BASE, BAST_CPLD_SIZE, tag);
    stcb->cpld_ctrl2 = 0;
}

static void stcb_i2c_setup(STCBState *stcb)
{
    i2c_bus *bus = s3c24xx_i2c_bus(stcb->soc->iic);
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
    STCBState *stcb;
    int ret, index;
    ram_addr_t flash_mem;
    BlockDriverState *flash_bds = NULL;

    /* ensure memory is limited to 256MB */
    if (_ram_size > (256 * MEGA * BYTE))
        _ram_size = 256 * MEGA * BYTE;
    ram_size = _ram_size;

    /* initialise board informations */
    bast_binfo.ram_size = ram_size;
    bast_binfo.kernel_filename = kernel_filename;
    bast_binfo.kernel_cmdline = kernel_cmdline;
    bast_binfo.initrd_filename = initrd_filename;
    bast_binfo.nb_cpus = 1;
    bast_binfo.loader_start = BAST_NOR_RO_BASE;

    /* allocate storage for board state */
    stcb = malloc(sizeof(STCBState));

    /* initialise SOC */
    stcb->soc = s3c2410x_init(ram_size);

    /* Register the NOR flash ROM */
    flash_mem = qemu_ram_alloc(NULL, "bast.flash", BAST_NOR_SIZE);

    /* Read only ROM type mapping */
    cpu_register_physical_memory(BAST_NOR_RO_BASE,
                                 BAST_NOR_SIZE,
                                 flash_mem | IO_MEM_ROM);

#if 0 // TODO
    /* Aquire flash contents and register pflash device */
    index = drive_get_index(IF_PFLASH, 0, 0);
    if (index != -1) {
        /* load from specified flash device */
        flash_bds = drives_table[index].bdrv;
    } else {
        /* Try and load default bootloader image */
        char buf[PATH_MAX];

        snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
        ret = load_image_targphys(buf, BAST_NOR_RO_BASE, BAST_NOR_SIZE);
    }
    pflash_cfi02_register(BAST_NOR_RW_BASE, flash_mem, flash_bds,
                          65536, 32, 1, 2,
                          0x00BF, 0x234B, 0x0000, 0x0000, 0x5555, 0x2AAA);
#endif

    /* if kernel is given, boot that directly */
    if (kernel_filename != NULL) {
        bast_binfo.loader_start = CPU_S3C2410X_DRAM;
        arm_load_kernel(stcb->soc->cpu_env, &bast_binfo);
    }

    /* Setup initial (reset) program counter */
    stcb->soc->cpu_env->regs[15] = bast_binfo.loader_start;

    /* Initialise the BAST CPLD */
    stcb_cpld_register(stcb);

    /* attach i2c devices */
    stcb_i2c_setup(stcb);

    /* Attach some NAND devices */
    stcb->nand[0] = NULL;
    stcb->nand[1] = NULL;
#if 0 // TODO
    index = drive_get_index(IF_MTD, 0, 0);
    if (index == -1)
        stcb->nand[2] = NULL;
    else
        stcb->nand[2] = nand_init(0xEC, 0x79); /* 128MiB small-page */
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
