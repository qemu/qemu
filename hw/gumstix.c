/*
 * Gumstix Platforms
 *
 * Copyright (c) 2007 by Thorsten Zitterell <info@bitmux.org>
 *
 * Code based on spitz platform by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GNU GPL v2.
 */

#include "hw.h"
#include "pxa.h"
#include "net.h"
#include "flash.h"
#include "sysemu.h"
#include "devices.h"
#include "boards.h"

/* Board init. */
enum gumstix_model_e { connex };

static void gumstix_common_init(int ram_size, int vga_ram_size,
                DisplayState *ds, const char *kernel_filename,
                const char *kernel_cmdline, const char *initrd_filename,
                const char *cpu_model, enum gumstix_model_e model)
{
    struct pxa2xx_state_s *cpu;

    uint32_t gumstix_rom = 0x02000000;
    uint32_t gumstix_ram = 0x08000000;

    if (ram_size < (gumstix_ram + gumstix_rom + PXA2XX_INTERNAL_SIZE)) {
        fprintf(stderr, "This platform requires %i bytes of memory\n",
                gumstix_ram + gumstix_rom + PXA2XX_INTERNAL_SIZE);
        exit(1);
    }

    cpu = pxa255_init(gumstix_ram, ds);

    if (pflash_table[0] == NULL) {
        fprintf(stderr, "A flash image must be given with the "
                "'pflash' parameter\n");
        exit(1);
    }

    if (!pflash_register(0x00000000, gumstix_ram + PXA2XX_INTERNAL_SIZE,
            pflash_table[0], 128 * 1024, 128, 2, 0, 0, 0, 0)) {
        fprintf(stderr, "qemu: Error register flash memory.\n");
        exit(1);
    }

    cpu->env->regs[15] = 0x00000000;

    /* Interrupt line of NIC is connected to GPIO line 36 */
    smc91c111_init(&nd_table[0], 0x04000300,
                    pxa2xx_gpio_in_get(cpu->gpio)[36]);
}

static void connex_init(int ram_size, int vga_ram_size,
                const char *boot_device, DisplayState *ds,
                const char **fd_filename, int snapshot,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    gumstix_common_init(ram_size, vga_ram_size, ds, kernel_filename,
                kernel_cmdline, initrd_filename, cpu_model, connex);
}

QEMUMachine connex_machine = {
    "connex",
    "Gumstix Connex (PXA255)",
    connex_init,
};
