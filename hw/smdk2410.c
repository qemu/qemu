/* hw/smdk2410.c
 *
 * System emulation for the Samsung SMDK2410
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

#define BIOS_FILENAME "smdk2410.bin"

typedef struct {
    S3CState *soc;
    unsigned char cpld_ctrl2;
    NANDFlashState *nand[4];
} SMDK2410State;

/* Bytes in a Kilobyte */
#define KILO 1024
/* Bytes in a megabyte */
#define MEGA 1024 * KILO
/* Bytes */
#define BYTE 1
/* Bits in a byte */
#define BIT 8

/* Useful defines */
#define SMDK2410_NOR_BASE CPU_S3C2410X_CS0
#define SMDK2410_NOR_SIZE 16 * MEGA / BIT
#define SMDK2410_BOARD_ID 193

static struct arm_boot_info smdk2410_binfo = {
    .board_id = SMDK2410_BOARD_ID,
    .ram_size = 0x10000000, /* 256MB */
};

static void smdk2410_init(ram_addr_t _ram_size,
                      const char *boot_device,
                      const char *kernel_filename, const char *kernel_cmdline,
                      const char *initrd_filename, const char *cpu_model)
{
    ram_addr_t offset;
    SMDK2410State *stcb;
    int ret, index;

    /* ensure memory is limited to 256MB */
    if (_ram_size > (256 * MEGA * BYTE))
        _ram_size = 256 * MEGA * BYTE;
    ram_size = _ram_size;

    /* allocate storage for board state */
    stcb = malloc(sizeof(SMDK2410State));

    /* initialise CPU and memory */
    stcb->soc = s3c2410x_init(ram_size);

    /* Register the NOR flash ROM */
    offset = qemu_ram_alloc(NULL, "smdk2410.flash", SMDK2410_NOR_SIZE);
    cpu_register_physical_memory(SMDK2410_NOR_BASE,
                                 SMDK2410_NOR_SIZE,
                                 offset | IO_MEM_ROM);

    /* initialise board informations */
    smdk2410_binfo.ram_size = ram_size;
    smdk2410_binfo.kernel_filename = kernel_filename;
    smdk2410_binfo.kernel_cmdline = kernel_cmdline;
    smdk2410_binfo.initrd_filename = initrd_filename;
    smdk2410_binfo.nb_cpus = 1;
    smdk2410_binfo.loader_start = SMDK2410_NOR_BASE;

    if (kernel_filename == NULL) {
#if 0 // TODO
        /* No kernel given so try and aquire a bootloader */
        char buf[PATH_MAX];

        snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
        ret = load_image_targphys(buf, smdk2410_binfo.loader_start, SMDK2410_NOR_SIZE);
        if (ret <= 0) {
            perror("qemu");
            fprintf(stderr, "qemu: warning, could not load SMDK2410 BIOS from %s\n", buf);
            exit (1);
        } else {
            fprintf(stdout, "qemu: info, loaded SMDK2410 BIOS %d bytes from %s\n", ret, buf);
        }
#endif
    } else {
        smdk2410_binfo.loader_start = CPU_S3C2410X_DRAM;
        arm_load_kernel(stcb->soc->cpu_env, &smdk2410_binfo);
    }

    /* Setup initial (reset) program counter */
    stcb->soc->cpu_env->regs[15] = smdk2410_binfo.loader_start;

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


static QEMUMachine smdk2410_machine = {
  .name = "smdk2410",
  .desc = "Samsung SMDK2410 (S3C2410A, ARM920T)",
  .init = smdk2410_init,
  .max_cpus = 1,
};

static void smdk2410_machine_init(void)
{
    qemu_register_machine(&smdk2410_machine);
}

machine_init(smdk2410_machine_init);
