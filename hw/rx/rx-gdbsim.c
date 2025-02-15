/*
 * RX QEMU GDB simulator
 *
 * Copyright (c) 2019 Yoshinori Sato
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/loader.h"
#include "hw/rx/rx62n.h"
#include "system/qtest.h"
#include "system/device_tree.h"
#include "system/reset.h"
#include "hw/boards.h"
#include "qom/object.h"

/* Same address of GDB integrated simulator */
#define SDRAM_BASE  EXT_CS_BASE

struct RxGdbSimMachineClass {
    /*< private >*/
    MachineClass parent_class;
    /*< public >*/
    const char *mcu_name;
    uint32_t xtal_freq_hz;
};
typedef struct RxGdbSimMachineClass RxGdbSimMachineClass;

struct RxGdbSimMachineState {
    /*< private >*/
    MachineState parent_obj;
    /*< public >*/
    RX62NState mcu;
};
typedef struct RxGdbSimMachineState RxGdbSimMachineState;

#define TYPE_RX_GDBSIM_MACHINE MACHINE_TYPE_NAME("rx62n-common")

DECLARE_OBJ_CHECKERS(RxGdbSimMachineState, RxGdbSimMachineClass,
                     RX_GDBSIM_MACHINE, TYPE_RX_GDBSIM_MACHINE)


static void rx_load_image(RXCPU *cpu, const char *filename,
                          uint32_t start, uint32_t size)
{
    static uint32_t extable[32];
    long kernel_size;
    int i;

    kernel_size = load_image_targphys(filename, start, size);
    if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n", filename);
        exit(1);
    }
    cpu->env.pc = start;

    /* setup exception trap trampoline */
    /* linux kernel only works little-endian mode */
    for (i = 0; i < ARRAY_SIZE(extable); i++) {
        extable[i] = cpu_to_le32(0x10 + i * 4);
    }
    rom_add_blob_fixed("extable", extable, sizeof(extable), VECTOR_TABLE_BASE);
}

static void rx_gdbsim_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    RxGdbSimMachineState *s = RX_GDBSIM_MACHINE(machine);
    RxGdbSimMachineClass *rxc = RX_GDBSIM_MACHINE_GET_CLASS(machine);
    MemoryRegion *sysmem = get_system_memory();
    const char *kernel_filename = machine->kernel_filename;
    const char *dtb_filename = machine->dtb;
    uint8_t rng_seed[32];

    if (machine->ram_size < mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be more than %s", sz);
        g_free(sz);
        exit(1);
    }

    /* Allocate memory space */
    memory_region_add_subregion(sysmem, SDRAM_BASE, machine->ram);

    /* Initialize MCU */
    object_initialize_child(OBJECT(machine), "mcu", &s->mcu, rxc->mcu_name);
    object_property_set_link(OBJECT(&s->mcu), "main-bus", OBJECT(sysmem),
                             &error_abort);
    object_property_set_uint(OBJECT(&s->mcu), "xtal-frequency-hz",
                             rxc->xtal_freq_hz, &error_abort);
    object_property_set_bool(OBJECT(&s->mcu), "load-kernel",
                             kernel_filename != NULL, &error_abort);

    if (!kernel_filename) {
        if (machine->firmware) {
            rom_add_file_fixed(machine->firmware, RX62N_CFLASH_BASE, 0);
        }
    }

    qdev_realize(DEVICE(&s->mcu), NULL, &error_abort);

    /* Load kernel and dtb */
    if (kernel_filename) {
        ram_addr_t kernel_offset;

        /*
         * The kernel image is loaded into
         * the latter half of the SDRAM space.
         */
        kernel_offset = machine->ram_size / 2;
        rx_load_image(&s->mcu.cpu, kernel_filename,
                      SDRAM_BASE + kernel_offset, kernel_offset);
        if (dtb_filename) {
            ram_addr_t dtb_offset;
            int dtb_size;
            g_autofree void *dtb = load_device_tree(dtb_filename, &dtb_size);

            if (dtb == NULL) {
                error_report("Couldn't open dtb file %s", dtb_filename);
                exit(1);
            }
            if (machine->kernel_cmdline &&
                qemu_fdt_setprop_string(dtb, "/chosen", "bootargs",
                                        machine->kernel_cmdline) < 0) {
                error_report("Couldn't set /chosen/bootargs");
                exit(1);
            }
            qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
            qemu_fdt_setprop(dtb, "/chosen", "rng-seed", rng_seed, sizeof(rng_seed));
            /* DTB is located at the end of SDRAM space. */
            dtb_offset = ROUND_DOWN(machine->ram_size - dtb_size, 16);
            rom_add_blob_fixed("dtb", dtb, dtb_size,
                               SDRAM_BASE + dtb_offset);
            qemu_register_reset_nosnapshotload(qemu_fdt_randomize_seeds,
                                rom_ptr(SDRAM_BASE + dtb_offset, dtb_size));
            /* Set dtb address to R1 */
            s->mcu.cpu.env.regs[1] = SDRAM_BASE + dtb_offset;
        }
    }
}

static void rx_gdbsim_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = rx_gdbsim_init;
    mc->default_cpu_type = TYPE_RX62N_CPU;
    mc->default_ram_size = 16 * MiB;
    mc->default_ram_id = "ext-sdram";
}

static void rx62n7_class_init(ObjectClass *oc, void *data)
{
    RxGdbSimMachineClass *rxc = RX_GDBSIM_MACHINE_CLASS(oc);
    MachineClass *mc = MACHINE_CLASS(oc);

    rxc->mcu_name = TYPE_R5F562N7_MCU;
    rxc->xtal_freq_hz = 12 * 1000 * 1000;
    mc->desc = "gdb simulator (R5F562N7 MCU and external RAM)";
};

static void rx62n8_class_init(ObjectClass *oc, void *data)
{
    RxGdbSimMachineClass *rxc = RX_GDBSIM_MACHINE_CLASS(oc);
    MachineClass *mc = MACHINE_CLASS(oc);

    rxc->mcu_name = TYPE_R5F562N8_MCU;
    rxc->xtal_freq_hz = 12 * 1000 * 1000;
    mc->desc = "gdb simulator (R5F562N8 MCU and external RAM)";
};

static const TypeInfo rx_gdbsim_types[] = {
    {
        .name           = MACHINE_TYPE_NAME("gdbsim-r5f562n7"),
        .parent         = TYPE_RX_GDBSIM_MACHINE,
        .class_init     = rx62n7_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("gdbsim-r5f562n8"),
        .parent         = TYPE_RX_GDBSIM_MACHINE,
        .class_init     = rx62n8_class_init,
    }, {
        .name           = TYPE_RX_GDBSIM_MACHINE,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(RxGdbSimMachineState),
        .class_size     = sizeof(RxGdbSimMachineClass),
        .class_init     = rx_gdbsim_class_init,
        .abstract       = true,
     }
};

DEFINE_TYPES(rx_gdbsim_types)
