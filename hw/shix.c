/*
 * SHIX 2.0 board description
 * 
 * Copyright (c) 2005 Samuel Tardieu
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
/* 
   Shix 2.0 board by Alexis Polti, described at
   http://perso.enst.fr/~polti/realisations/shix20/

   More information in target-sh4/README.sh4
*/
#include "vl.h"

#define BIOS_FILENAME "shix_bios.bin"
#define BIOS_ADDRESS 0xA0000000

void DMA_run(void)
{
    /* XXXXX */
}

void irq_info(void)
{
    /* XXXXX */
}

void pic_set_irq(int irq, int level)
{
    /* XXXXX */
}

void pic_info()
{
    /* XXXXX */
}

void vga_update_display()
{
    /* XXXXX */
}

void vga_invalidate_display()
{
    /* XXXXX */
}

void vga_screen_dump(const char *filename)
{
    /* XXXXX */
}

void shix_init(int ram_size, int vga_ram_size, int boot_device,
	       DisplayState * ds, const char **fd_filename, int snapshot,
	       const char *kernel_filename, const char *kernel_cmdline,
	       const char *initrd_filename)
{
    int ret;
    CPUState *env;
    struct SH7750State *s;

    printf("Initializing CPU\n");
    env = cpu_init();

    /* Allocate memory space */
    printf("Allocating ROM\n");
    cpu_register_physical_memory(0x00000000, 0x00004000, IO_MEM_ROM);
    printf("Allocating SDRAM 1\n");
    cpu_register_physical_memory(0x08000000, 0x01000000, 0x00004000);
    printf("Allocating SDRAM 2\n");
    cpu_register_physical_memory(0x0c000000, 0x01000000, 0x01004000);

    /* Load BIOS in 0 (and access it through P2, 0xA0000000) */
    printf("%s: load BIOS '%s'\n", __func__, BIOS_FILENAME);
    ret = load_image(BIOS_FILENAME, phys_ram_base);
    if (ret < 0) {		/* Check bios size */
	fprintf(stderr, "ret=%d\n", ret);
	fprintf(stderr, "qemu: could not load SHIX bios '%s'\n",
		BIOS_FILENAME);
	exit(1);
    }

    /* Register peripherals */
    s = sh7750_init(env);
    /* XXXXX Check success */
    tc58128_init(s, "shix_linux_nand.bin", NULL);
    fprintf(stderr, "initialization terminated\n");
}

QEMUMachine shix_machine = {
    "shix",
    "shix card",
    shix_init
};
