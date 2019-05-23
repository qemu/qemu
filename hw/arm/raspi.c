/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Upstreaming code cleanup [including bcm2835_*] (c) 2013 Jan Petrous
 *
 * Rasperry Pi 2 emulation Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * Raspberry Pi 3 emulation Copyright (c) 2018 Zoltán Baldaszti
 * Upstream code cleanup (c) 2018 Pekka Enberg
 *
 * This code is licensed under the GNU GPLv2 and later.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/arm/bcm2836.h"
#include "qemu/error-report.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/arm/boot.h"
#include "sysemu/sysemu.h"

#define SMPBOOT_ADDR    0x300 /* this should leave enough space for ATAGS */
#define MVBAR_ADDR      0x400 /* secure vectors */
#define BOARDSETUP_ADDR (MVBAR_ADDR + 0x20) /* board setup code */
#define FIRMWARE_ADDR_2 0x8000 /* Pi 2 loads kernel.img here by default */
#define FIRMWARE_ADDR_3 0x80000 /* Pi 3 loads kernel.img here by default */
#define SPINTABLE_ADDR  0xd8 /* Pi 3 bootloader spintable */

/* Table of Linux board IDs for different Pi versions */
static const int raspi_boardid[] = {[1] = 0xc42, [2] = 0xc43, [3] = 0xc44};

typedef struct RasPiState {
    BCM283XState soc;
    MemoryRegion ram;
} RasPiState;

static void write_smpboot(ARMCPU *cpu, const struct arm_boot_info *info)
{
    static const uint32_t smpboot[] = {
        0xe1a0e00f, /*    mov     lr, pc */
        0xe3a0fe00 + (BOARDSETUP_ADDR >> 4), /* mov pc, BOARDSETUP_ADDR */
        0xee100fb0, /*    mrc     p15, 0, r0, c0, c0, 5;get core ID */
        0xe7e10050, /*    ubfx    r0, r0, #0, #2       ;extract LSB */
        0xe59f5014, /*    ldr     r5, =0x400000CC      ;load mbox base */
        0xe320f001, /* 1: yield */
        0xe7953200, /*    ldr     r3, [r5, r0, lsl #4] ;read mbox for our core*/
        0xe3530000, /*    cmp     r3, #0               ;spin while zero */
        0x0afffffb, /*    beq     1b */
        0xe7853200, /*    str     r3, [r5, r0, lsl #4] ;clear mbox */
        0xe12fff13, /*    bx      r3                   ;jump to target */
        0x400000cc, /* (constant: mailbox 3 read/clear base) */
    };

    /* check that we don't overrun board setup vectors */
    QEMU_BUILD_BUG_ON(SMPBOOT_ADDR + sizeof(smpboot) > MVBAR_ADDR);
    /* check that board setup address is correctly relocated */
    QEMU_BUILD_BUG_ON((BOARDSETUP_ADDR & 0xf) != 0
                      || (BOARDSETUP_ADDR >> 4) >= 0x100);

    rom_add_blob_fixed("raspi_smpboot", smpboot, sizeof(smpboot),
                       info->smp_loader_start);
}

static void write_smpboot64(ARMCPU *cpu, const struct arm_boot_info *info)
{
    /* Unlike the AArch32 version we don't need to call the board setup hook.
     * The mechanism for doing the spin-table is also entirely different.
     * We must have four 64-bit fields at absolute addresses
     * 0xd8, 0xe0, 0xe8, 0xf0 in RAM, which are the flag variables for
     * our CPUs, and which we must ensure are zero initialized before
     * the primary CPU goes into the kernel. We put these variables inside
     * a rom blob, so that the reset for ROM contents zeroes them for us.
     */
    static const uint32_t smpboot[] = {
        0xd2801b05, /*        mov     x5, 0xd8 */
        0xd53800a6, /*        mrs     x6, mpidr_el1 */
        0x924004c6, /*        and     x6, x6, #0x3 */
        0xd503205f, /* spin:  wfe */
        0xf86678a4, /*        ldr     x4, [x5,x6,lsl #3] */
        0xb4ffffc4, /*        cbz     x4, spin */
        0xd2800000, /*        mov     x0, #0x0 */
        0xd2800001, /*        mov     x1, #0x0 */
        0xd2800002, /*        mov     x2, #0x0 */
        0xd2800003, /*        mov     x3, #0x0 */
        0xd61f0080, /*        br      x4 */
    };

    static const uint64_t spintables[] = {
        0, 0, 0, 0
    };

    rom_add_blob_fixed("raspi_smpboot", smpboot, sizeof(smpboot),
                       info->smp_loader_start);
    rom_add_blob_fixed("raspi_spintables", spintables, sizeof(spintables),
                       SPINTABLE_ADDR);
}

static void write_board_setup(ARMCPU *cpu, const struct arm_boot_info *info)
{
    arm_write_secure_board_setup_dummy_smc(cpu, info, MVBAR_ADDR);
}

static void reset_secondary(ARMCPU *cpu, const struct arm_boot_info *info)
{
    CPUState *cs = CPU(cpu);
    cpu_set_pc(cs, info->smp_loader_start);
}

static void setup_boot(MachineState *machine, int version, size_t ram_size)
{
    static struct arm_boot_info binfo;
    int r;

    binfo.board_id = raspi_boardid[version];
    binfo.ram_size = ram_size;
    binfo.nb_cpus = smp_cpus;

    if (version <= 2) {
        /* The rpi1 and 2 require some custom setup code to run in Secure
         * mode before booting a kernel (to set up the SMC vectors so
         * that we get a no-op SMC; this is used by Linux to call the
         * firmware for some cache maintenance operations.
         * The rpi3 doesn't need this.
         */
        binfo.board_setup_addr = BOARDSETUP_ADDR;
        binfo.write_board_setup = write_board_setup;
        binfo.secure_board_setup = true;
        binfo.secure_boot = true;
    }

    /* Pi2 and Pi3 requires SMP setup */
    if (version >= 2) {
        binfo.smp_loader_start = SMPBOOT_ADDR;
        if (version == 2) {
            binfo.write_secondary_boot = write_smpboot;
        } else {
            binfo.write_secondary_boot = write_smpboot64;
        }
        binfo.secondary_cpu_reset_hook = reset_secondary;
    }

    /* If the user specified a "firmware" image (e.g. UEFI), we bypass
     * the normal Linux boot process
     */
    if (machine->firmware) {
        hwaddr firmware_addr = version == 3 ? FIRMWARE_ADDR_3 : FIRMWARE_ADDR_2;
        /* load the firmware image (typically kernel.img) */
        r = load_image_targphys(machine->firmware, firmware_addr,
                                ram_size - firmware_addr);
        if (r < 0) {
            error_report("Failed to load firmware from %s", machine->firmware);
            exit(1);
        }

        binfo.entry = firmware_addr;
        binfo.firmware_loaded = true;
    } else {
        binfo.kernel_filename = machine->kernel_filename;
        binfo.kernel_cmdline = machine->kernel_cmdline;
        binfo.initrd_filename = machine->initrd_filename;
    }

    arm_load_kernel(ARM_CPU(first_cpu), &binfo);
}

static void raspi_init(MachineState *machine, int version)
{
    RasPiState *s = g_new0(RasPiState, 1);
    uint32_t vcram_size;
    DriveInfo *di;
    BlockBackend *blk;
    BusState *bus;
    DeviceState *carddev;

    if (machine->ram_size > 1 * GiB) {
        error_report("Requested ram size is too large for this machine: "
                     "maximum is 1GB");
        exit(1);
    }

    object_initialize_child(OBJECT(machine), "soc", &s->soc, sizeof(s->soc),
                            version == 3 ? TYPE_BCM2837 : TYPE_BCM2836,
                            &error_abort, NULL);

    /* Allocate and map RAM */
    memory_region_allocate_system_memory(&s->ram, OBJECT(machine), "ram",
                                         machine->ram_size);
    /* FIXME: Remove when we have custom CPU address space support */
    memory_region_add_subregion_overlap(get_system_memory(), 0, &s->ram, 0);

    /* Setup the SOC */
    object_property_add_const_link(OBJECT(&s->soc), "ram", OBJECT(&s->ram),
                                   &error_abort);
    object_property_set_int(OBJECT(&s->soc), smp_cpus, "enabled-cpus",
                            &error_abort);
    int board_rev = version == 3 ? 0xa02082 : 0xa21041;
    object_property_set_int(OBJECT(&s->soc), board_rev, "board-rev",
                            &error_abort);
    object_property_set_bool(OBJECT(&s->soc), true, "realized", &error_abort);

    /* Create and plug in the SD cards */
    di = drive_get_next(IF_SD);
    blk = di ? blk_by_legacy_dinfo(di) : NULL;
    bus = qdev_get_child_bus(DEVICE(&s->soc), "sd-bus");
    if (bus == NULL) {
        error_report("No SD bus found in SOC object");
        exit(1);
    }
    carddev = qdev_create(bus, TYPE_SD_CARD);
    qdev_prop_set_drive(carddev, "drive", blk, &error_fatal);
    object_property_set_bool(OBJECT(carddev), true, "realized", &error_fatal);

    vcram_size = object_property_get_uint(OBJECT(&s->soc), "vcram-size",
                                          &error_abort);
    setup_boot(machine, version, machine->ram_size - vcram_size);
}

static void raspi2_init(MachineState *machine)
{
    raspi_init(machine, 2);
}

static void raspi2_machine_init(MachineClass *mc)
{
    mc->desc = "Raspberry Pi 2";
    mc->init = raspi2_init;
    mc->block_default_type = IF_SD;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->max_cpus = BCM283X_NCPUS;
    mc->min_cpus = BCM283X_NCPUS;
    mc->default_cpus = BCM283X_NCPUS;
    mc->default_ram_size = 1024 * 1024 * 1024;
    mc->ignore_memory_transaction_failures = true;
};
DEFINE_MACHINE("raspi2", raspi2_machine_init)

#ifdef TARGET_AARCH64
static void raspi3_init(MachineState *machine)
{
    raspi_init(machine, 3);
}

static void raspi3_machine_init(MachineClass *mc)
{
    mc->desc = "Raspberry Pi 3";
    mc->init = raspi3_init;
    mc->block_default_type = IF_SD;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->max_cpus = BCM283X_NCPUS;
    mc->min_cpus = BCM283X_NCPUS;
    mc->default_cpus = BCM283X_NCPUS;
    mc->default_ram_size = 1024 * 1024 * 1024;
}
DEFINE_MACHINE("raspi3", raspi3_machine_init)
#endif
