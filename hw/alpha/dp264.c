/*
 * QEMU Alpha DP264/CLIPPER hardware system emulator.
 *
 * Choose CLIPPER IRQ mappings over, say, DP264, MONET, or WEBBRICK
 * variants because CLIPPER doesn't have an SMC669 SuperIO controller
 * that we need to emulate as well.
 */

#include "hw/hw.h"
#include "elf.h"
#include "hw/loader.h"
#include "hw/boards.h"
#include "alpha_sys.h"
#include "sysemu/sysemu.h"
#include "hw/timer/mc146818rtc.h"
#include "hw/ide.h"
#include "hw/timer/i8254.h"
#include "hw/char/serial.h"

#define MAX_IDE_BUS 2

static uint64_t cpu_alpha_superpage_to_phys(void *opaque, uint64_t addr)
{
    if (((addr >> 41) & 3) == 2) {
        addr &= 0xffffffffffull;
    }
    return addr;
}

/* Note that there are at least 3 viewpoints of IRQ numbers on Alpha systems.
    (0) The dev_irq_n lines into the cpu, which we totally ignore,
    (1) The DRIR lines in the typhoon chipset,
    (2) The "vector" aka mangled interrupt number reported by SRM PALcode,
    (3) The interrupt number assigned by the kernel.
   The following function is concerned with (1) only.  */

static int clipper_pci_map_irq(PCIDevice *d, int irq_num)
{
    int slot = d->devfn >> 3;

    assert(irq_num >= 0 && irq_num <= 3);

    return (slot + 1) * 4 + irq_num;
}

static void clipper_init(QEMUMachineInitArgs *args)
{
    ram_addr_t ram_size = args->ram_size;
    const char *cpu_model = args->cpu_model;
    const char *kernel_filename = args->kernel_filename;
    const char *kernel_cmdline = args->kernel_cmdline;
    const char *initrd_filename = args->initrd_filename;
    AlphaCPU *cpus[4];
    PCIBus *pci_bus;
    ISABus *isa_bus;
    qemu_irq rtc_irq;
    long size, i;
    const char *palcode_filename;
    uint64_t palcode_entry, palcode_low, palcode_high;
    uint64_t kernel_entry, kernel_low, kernel_high;

    /* Create up to 4 cpus.  */
    memset(cpus, 0, sizeof(cpus));
    for (i = 0; i < smp_cpus; ++i) {
        cpus[i] = cpu_alpha_init(cpu_model ? cpu_model : "ev67");
    }

    cpus[0]->env.trap_arg0 = ram_size;
    cpus[0]->env.trap_arg1 = 0;
    cpus[0]->env.trap_arg2 = smp_cpus;

    /* Init the chipset.  */
    pci_bus = typhoon_init(ram_size, &isa_bus, &rtc_irq, cpus,
                           clipper_pci_map_irq);

    /* Since we have an SRM-compatible PALcode, use the SRM epoch.  */
    rtc_init(isa_bus, 1900, rtc_irq);

    pit_init(isa_bus, 0x40, 0, NULL);
    isa_create_simple(isa_bus, "i8042");

    /* VGA setup.  Don't bother loading the bios.  */
    pci_vga_init(pci_bus);

    /* Serial code setup.  */
    for (i = 0; i < MAX_SERIAL_PORTS; ++i) {
        if (serial_hds[i]) {
            serial_isa_init(isa_bus, i, serial_hds[i]);
        }
    }

    /* Network setup.  e1000 is good enough, failing Tulip support.  */
    for (i = 0; i < nb_nics; i++) {
        pci_nic_init_nofail(&nd_table[i], pci_bus, "e1000", NULL);
    }

    /* IDE disk setup.  */
    {
        DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
        ide_drive_get(hd, MAX_IDE_BUS);

        pci_cmd646_ide_init(pci_bus, hd, 0);
    }

    /* Load PALcode.  Given that this is not "real" cpu palcode,
       but one explicitly written for the emulation, we might as
       well load it directly from and ELF image.  */
    palcode_filename = (bios_name ? bios_name : "palcode-clipper");
    palcode_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, palcode_filename);
    if (palcode_filename == NULL) {
        hw_error("no palcode provided\n");
        exit(1);
    }
    size = load_elf(palcode_filename, cpu_alpha_superpage_to_phys,
                    NULL, &palcode_entry, &palcode_low, &palcode_high,
                    0, EM_ALPHA, 0);
    if (size < 0) {
        hw_error("could not load palcode '%s'\n", palcode_filename);
        exit(1);
    }

    /* Start all cpus at the PALcode RESET entry point.  */
    for (i = 0; i < smp_cpus; ++i) {
        cpus[i]->env.pal_mode = 1;
        cpus[i]->env.pc = palcode_entry;
        cpus[i]->env.palbr = palcode_entry;
    }

    /* Load a kernel.  */
    if (kernel_filename) {
        uint64_t param_offset;

        size = load_elf(kernel_filename, cpu_alpha_superpage_to_phys,
                        NULL, &kernel_entry, &kernel_low, &kernel_high,
                        0, EM_ALPHA, 0);
        if (size < 0) {
            hw_error("could not load kernel '%s'\n", kernel_filename);
            exit(1);
        }

        cpus[0]->env.trap_arg1 = kernel_entry;

        param_offset = kernel_low - 0x6000;

        if (kernel_cmdline) {
            pstrcpy_targphys("cmdline", param_offset, 0x100, kernel_cmdline);
        }

        if (initrd_filename) {
            long initrd_base, initrd_size;

            initrd_size = get_image_size(initrd_filename);
            if (initrd_size < 0) {
                hw_error("could not load initial ram disk '%s'\n",
                         initrd_filename);
                exit(1);
            }

            /* Put the initrd image as high in memory as possible.  */
            initrd_base = (ram_size - initrd_size) & TARGET_PAGE_MASK;
            load_image_targphys(initrd_filename, initrd_base,
                                ram_size - initrd_base);

            stq_phys(&address_space_memory,
                     param_offset + 0x100, initrd_base + 0xfffffc0000000000ULL);
            stq_phys(&address_space_memory, param_offset + 0x108, initrd_size);
        }
    }
}

static QEMUMachine clipper_machine = {
    .name = "clipper",
    .desc = "Alpha DP264/CLIPPER",
    .init = clipper_init,
    .max_cpus = 4,
    .is_default = 1,
};

static void clipper_machine_init(void)
{
    qemu_register_machine(&clipper_machine);
}

machine_init(clipper_machine_init);
