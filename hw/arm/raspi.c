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
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/arm/bcm2836.h"
#include "hw/registerfields.h"
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

/* Registered machine type (matches RPi Foundation bootloader and U-Boot) */
#define MACH_TYPE_BCM2708   3138

typedef struct RaspiMachineState {
    /*< private >*/
    MachineState parent_obj;
    /*< public >*/
    BCM283XState soc;
} RaspiMachineState;

typedef struct RaspiMachineClass {
    /*< private >*/
    MachineClass parent_obj;
    /*< public >*/
    uint32_t board_rev;
} RaspiMachineClass;

#define TYPE_RASPI_MACHINE       MACHINE_TYPE_NAME("raspi-common")
#define RASPI_MACHINE(obj) \
    OBJECT_CHECK(RaspiMachineState, (obj), TYPE_RASPI_MACHINE)

#define RASPI_MACHINE_CLASS(klass) \
     OBJECT_CLASS_CHECK(RaspiMachineClass, (klass), TYPE_RASPI_MACHINE)
#define RASPI_MACHINE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(RaspiMachineClass, (obj), TYPE_RASPI_MACHINE)

/*
 * Board revision codes:
 * www.raspberrypi.org/documentation/hardware/raspberrypi/revision-codes/
 */
FIELD(REV_CODE, REVISION,           0, 4);
FIELD(REV_CODE, TYPE,               4, 8);
FIELD(REV_CODE, PROCESSOR,         12, 4);
FIELD(REV_CODE, MANUFACTURER,      16, 4);
FIELD(REV_CODE, MEMORY_SIZE,       20, 3);
FIELD(REV_CODE, STYLE,             23, 1);

static uint64_t board_ram_size(uint32_t board_rev)
{
    assert(FIELD_EX32(board_rev, REV_CODE, STYLE)); /* Only new style */
    return 256 * MiB << FIELD_EX32(board_rev, REV_CODE, MEMORY_SIZE);
}

static int board_processor_id(uint32_t board_rev)
{
    assert(FIELD_EX32(board_rev, REV_CODE, STYLE)); /* Only new style */
    return FIELD_EX32(board_rev, REV_CODE, PROCESSOR);
}

static int board_version(uint32_t board_rev)
{
    return board_processor_id(board_rev) + 1;
}

static const char *board_soc_type(uint32_t board_rev)
{
    static const char *soc_types[] = {
        NULL, TYPE_BCM2836, TYPE_BCM2837,
    };
    int proc_id = board_processor_id(board_rev);

    if (proc_id >= ARRAY_SIZE(soc_types) || !soc_types[proc_id]) {
        error_report("Unsupported processor id '%d' (board revision: 0x%x)",
                     proc_id, board_rev);
        exit(1);
    }
    return soc_types[proc_id];
}

static int cores_count(uint32_t board_rev)
{
    static const int soc_cores_count[] = {
        0, BCM283X_NCPUS, BCM283X_NCPUS,
    };
    int proc_id = board_processor_id(board_rev);

    if (proc_id >= ARRAY_SIZE(soc_cores_count) || !soc_cores_count[proc_id]) {
        error_report("Unsupported processor id '%d' (board revision: 0x%x)",
                     proc_id, board_rev);
        exit(1);
    }
    return soc_cores_count[proc_id];
}

static const char *board_type(uint32_t board_rev)
{
    static const char *types[] = {
        "A", "B", "A+", "B+", "2B", "Alpha", "CM1", NULL, "3B", "Zero",
        "CM3", NULL, "Zero W", "3B+", "3A+", NULL, "CM3+", "4B",
    };
    assert(FIELD_EX32(board_rev, REV_CODE, STYLE)); /* Only new style */
    int bt = FIELD_EX32(board_rev, REV_CODE, TYPE);
    if (bt >= ARRAY_SIZE(types) || !types[bt]) {
        return "Unknown";
    }
    return types[bt];
}

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

    rom_add_blob_fixed_as("raspi_smpboot", smpboot, sizeof(smpboot),
                          info->smp_loader_start,
                          arm_boot_address_space(cpu, info));
}

static void write_smpboot64(ARMCPU *cpu, const struct arm_boot_info *info)
{
    AddressSpace *as = arm_boot_address_space(cpu, info);
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

    rom_add_blob_fixed_as("raspi_smpboot", smpboot, sizeof(smpboot),
                          info->smp_loader_start, as);
    rom_add_blob_fixed_as("raspi_spintables", spintables, sizeof(spintables),
                          SPINTABLE_ADDR, as);
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

    binfo.board_id = MACH_TYPE_BCM2708;
    binfo.ram_size = ram_size;
    binfo.nb_cpus = machine->smp.cpus;

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
    }

    arm_load_kernel(ARM_CPU(first_cpu), machine, &binfo);
}

static void raspi_machine_init(MachineState *machine)
{
    RaspiMachineClass *mc = RASPI_MACHINE_GET_CLASS(machine);
    RaspiMachineState *s = RASPI_MACHINE(machine);
    uint32_t board_rev = mc->board_rev;
    int version = board_version(board_rev);
    uint64_t ram_size = board_ram_size(board_rev);
    uint32_t vcram_size;
    DriveInfo *di;
    BlockBackend *blk;
    BusState *bus;
    DeviceState *carddev;

    if (machine->ram_size != ram_size) {
        char *size_str = size_to_str(ram_size);
        error_report("Invalid RAM size, should be %s", size_str);
        g_free(size_str);
        exit(1);
    }

    /* FIXME: Remove when we have custom CPU address space support */
    memory_region_add_subregion_overlap(get_system_memory(), 0,
                                        machine->ram, 0);

    /* Setup the SOC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            board_soc_type(board_rev));
    object_property_add_const_link(OBJECT(&s->soc), "ram", OBJECT(machine->ram));
    object_property_set_int(OBJECT(&s->soc), "board-rev", board_rev,
                            &error_abort);
    qdev_realize(DEVICE(&s->soc), NULL, &error_abort);

    /* Create and plug in the SD cards */
    di = drive_get_next(IF_SD);
    blk = di ? blk_by_legacy_dinfo(di) : NULL;
    bus = qdev_get_child_bus(DEVICE(&s->soc), "sd-bus");
    if (bus == NULL) {
        error_report("No SD bus found in SOC object");
        exit(1);
    }
    carddev = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_drive_err(carddev, "drive", blk, &error_fatal);
    qdev_realize_and_unref(carddev, bus, &error_fatal);

    vcram_size = object_property_get_uint(OBJECT(&s->soc), "vcram-size",
                                          &error_abort);
    setup_boot(machine, version, machine->ram_size - vcram_size);
}

static void raspi_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RaspiMachineClass *rmc = RASPI_MACHINE_CLASS(oc);
    uint32_t board_rev = (uint32_t)(uintptr_t)data;

    rmc->board_rev = board_rev;
    mc->desc = g_strdup_printf("Raspberry Pi %s", board_type(board_rev));
    mc->init = raspi_machine_init;
    mc->block_default_type = IF_SD;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->default_cpus = mc->min_cpus = mc->max_cpus = cores_count(board_rev);
    mc->default_ram_size = board_ram_size(board_rev);
    mc->default_ram_id = "ram";
    if (board_version(board_rev) == 2) {
        mc->ignore_memory_transaction_failures = true;
    }
};

static const TypeInfo raspi_machine_types[] = {
    {
        .name           = MACHINE_TYPE_NAME("raspi2"),
        .parent         = TYPE_RASPI_MACHINE,
        .class_init     = raspi_machine_class_init,
        .class_data     = (void *)0xa21041,
#ifdef TARGET_AARCH64
    }, {
        .name           = MACHINE_TYPE_NAME("raspi3"),
        .parent         = TYPE_RASPI_MACHINE,
        .class_init     = raspi_machine_class_init,
        .class_data     = (void *)0xa02082,
#endif
    }, {
        .name           = TYPE_RASPI_MACHINE,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(RaspiMachineState),
        .class_size     = sizeof(RaspiMachineClass),
        .abstract       = true,
    }
};

DEFINE_TYPES(raspi_machine_types)
