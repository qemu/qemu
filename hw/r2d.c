/*
 * Renesas SH7751R R2D-PLUS emulation
 *
 * Copyright (c) 2007 Magnus Damm
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

#include "hw.h"
#include "sh.h"
#include "sysemu.h"
#include "boards.h"

#define SDRAM_BASE 0x0c000000 /* Physical location of SDRAM: Area 3 */
#define SDRAM_SIZE 0x04000000

static void r2d_init(int ram_size, int vga_ram_size,
              const char *boot_device, DisplayState * ds,
	      const char *kernel_filename, const char *kernel_cmdline,
	      const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    struct SH7750State *s;

    if (!cpu_model)
        cpu_model = "any";

    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }

    /* Allocate memory space */
    cpu_register_physical_memory(SDRAM_BASE, SDRAM_SIZE, 0);
    /* Register peripherals */
    s = sh7750_init(env);
    /* Todo: register on board registers */
    {
      int kernel_size;

      kernel_size = load_image(kernel_filename, phys_ram_base);

      if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n", kernel_filename);
        exit(1);
      }

      env->pc = SDRAM_BASE | 0xa0000000; /* Start from P2 area */
    }
}

QEMUMachine r2d_machine = {
    "r2d",
    "r2d-plus board",
    r2d_init
};
