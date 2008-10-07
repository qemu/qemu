/*
 * Renesas SH7751R R2D-PLUS emulation
 *
 * Copyright (c) 2007 Magnus Damm
 * Copyright (c) 2008 Paul Mundt
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

#define PA_POWOFF	0x30
#define PA_VERREG	0x32
#define PA_OUTPORT	0x36

typedef struct {
    target_phys_addr_t base;

    uint16_t bcr;
    uint16_t irlmon;
    uint16_t cfctl;
    uint16_t cfpow;
    uint16_t dispctl;
    uint16_t sdmpow;
    uint16_t rtcce;
    uint16_t pcicd;
    uint16_t voyagerrts;
    uint16_t cfrst;
    uint16_t admrts;
    uint16_t extrst;
    uint16_t cfcdintclr;
    uint16_t keyctlclr;
    uint16_t pad0;
    uint16_t pad1;
    uint16_t powoff;
    uint16_t verreg;
    uint16_t inport;
    uint16_t outport;
    uint16_t bverreg;
} r2d_fpga_t;

static uint32_t r2d_fpga_read(void *opaque, target_phys_addr_t addr)
{
    r2d_fpga_t *s = opaque;

    addr -= s->base;

    switch (addr) {
    case PA_OUTPORT:
	return s->outport;
    case PA_POWOFF:
	return s->powoff;
    case PA_VERREG:
	return 0x10;
    }

    return 0;
}

static void
r2d_fpga_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    r2d_fpga_t *s = opaque;

    addr -= s->base;

    switch (addr) {
    case PA_OUTPORT:
	s->outport = value;
	break;
    case PA_POWOFF:
	s->powoff = value;
	break;
    case PA_VERREG:
	/* Discard writes */
	break;
    }
}

static CPUReadMemoryFunc *r2d_fpga_readfn[] = {
    r2d_fpga_read,
    r2d_fpga_read,
    NULL,
};

static CPUWriteMemoryFunc *r2d_fpga_writefn[] = {
    r2d_fpga_write,
    r2d_fpga_write,
    NULL,
};

static void r2d_fpga_init(target_phys_addr_t base)
{
    int iomemtype;
    r2d_fpga_t *s;

    s = qemu_mallocz(sizeof(r2d_fpga_t));
    if (!s)
	return;

    s->base = base;
    iomemtype = cpu_register_io_memory(0, r2d_fpga_readfn,
				       r2d_fpga_writefn, s);
    cpu_register_physical_memory(base, 0x40, iomemtype);
}

static void r2d_init(ram_addr_t ram_size, int vga_ram_size,
              const char *boot_device, DisplayState * ds,
	      const char *kernel_filename, const char *kernel_cmdline,
	      const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    struct SH7750State *s;

    if (!cpu_model)
        cpu_model = "SH7751R";

    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }

    /* Allocate memory space */
    cpu_register_physical_memory(SDRAM_BASE, SDRAM_SIZE, 0);
    /* Register peripherals */
    r2d_fpga_init(0x04000000);
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
    .name = "r2d",
    .desc = "r2d-plus board",
    .init = r2d_init,
    .ram_require = SDRAM_SIZE | RAMSIZE_FIXED,
};
