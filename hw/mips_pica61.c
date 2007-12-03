/*
 * QEMU Acer Pica Machine support
 *
 * Copyright (c) 2007 Hervé Poussineau
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
#include "mips.h"
#include "isa.h"
#include "pc.h"
#include "fdc.h"
#include "sysemu.h"
#include "boards.h"

#ifdef TARGET_WORDS_BIGENDIAN
#define BIOS_FILENAME "mips_bios.bin"
#else
#define BIOS_FILENAME "mipsel_bios.bin"
#endif

#ifdef TARGET_MIPS64
#define PHYS_TO_VIRT(x) ((x) | ~0x7fffffffULL)
#else
#define PHYS_TO_VIRT(x) ((x) | ~0x7fffffffU)
#endif

#define VIRT_TO_PHYS_ADDEND (-((int64_t)(int32_t)0x80000000))

#define MAX_IDE_BUS 2
#define MAX_FD 2

static const int ide_iobase[2] = { 0x1f0, 0x170 };
static const int ide_iobase2[2] = { 0x3f6, 0x376 };
static const int ide_irq[2] = { 14, 15 };

static uint32_t serial_base[MAX_SERIAL_PORTS] = { 0x80006000, 0x80007000 };
static int serial_irq[MAX_SERIAL_PORTS] = { 8, 9 };

extern FILE *logfile;

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);
}

static
void mips_pica61_init (int ram_size, int vga_ram_size,
                       const char *boot_device, DisplayState *ds,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    char buf[1024];
    unsigned long bios_offset;
    int bios_size;
    CPUState *env;
    int i;
    int available_ram;
    qemu_irq *i8259;
    int index;
    BlockDriverState *fd[MAX_FD];

    /* init CPUs */
    if (cpu_model == NULL) {
#ifdef TARGET_MIPS64
        cpu_model = "R4000";
#else
        /* FIXME: All wrong, this maybe should be R3000 for the older PICAs. */
        cpu_model = "24Kf";
#endif
    }
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    register_savevm("cpu", 0, 3, cpu_save, cpu_load, env);
    qemu_register_reset(main_cpu_reset, env);

    /* allocate RAM (limited to 256 MB) */
    if (ram_size < 256 * 1024 * 1024)
        available_ram = ram_size;
    else
        available_ram = 256 * 1024 * 1024;
    cpu_register_physical_memory(0, available_ram, IO_MEM_RAM);

    /* load a BIOS image */
    bios_offset = ram_size + vga_ram_size;
    if (bios_name == NULL)
        bios_name = BIOS_FILENAME;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
    bios_size = load_image(buf, phys_ram_base + bios_offset);
    if ((bios_size <= 0) || (bios_size > BIOS_SIZE)) {
        /* fatal */
        fprintf(stderr, "qemu: Error, could not load MIPS bios '%s'\n",
                buf);
        exit(1);
    }
    cpu_register_physical_memory(0x1fc00000,
                                     BIOS_SIZE, bios_offset | IO_MEM_ROM);

    /* Device map
     *
     * addr 0xe0004000: mc146818
     * addr 0xe0005000 intr 6: ps2 keyboard
     * addr 0xe0005000 intr 7: ps2 mouse
     * addr 0xe0006000 intr 8: ns16550a,
     * addr 0xe0007000 intr 9: ns16550a
     * isa_io_base 0xe2000000 isa_mem_base 0xe3000000
     */

    /* Init CPU internal devices */
    cpu_mips_irq_init_cpu(env);
    cpu_mips_clock_init(env);
    cpu_mips_irqctrl_init();

    /* Register 64 KB of ISA IO space at 0x10000000 */
    isa_mmio_init(0x10000000, 0x00010000);
    isa_mem_base = 0x11000000;

    /* PC style IRQ (i8259/i8254) and DMA (i8257) */
    /* The PIC is attached to the MIPS CPU INT0 pin */
    i8259 = i8259_init(env->irq[2]);
    rtc_mm_init(0x80004070, 1, i8259[14]);
    pit_init(0x40, 0);

    /* Keyboard (i8042) */
    i8042_mm_init(i8259[6], i8259[7], 0x80005060, 0);

    /* IDE controller */

    if (drive_get_max_bus(IF_IDE) >= MAX_IDE_BUS) {
        fprintf(stderr, "qemu: too many IDE bus\n");
        exit(1);
    }

    for(i = 0; i < MAX_IDE_BUS; i++) {
        int hd0, hd1;
        hd0 = drive_get_index(IF_IDE, i, 0);
        hd1 = drive_get_index(IF_IDE, i, 1);
        isa_ide_init(ide_iobase[i], ide_iobase2[i], i8259[ide_irq[i]],
                     hd0 == -1 ? NULL : drives_table[hd0].bdrv,
                     hd1 == -1 ? NULL : drives_table[hd1].bdrv);
    }

    /* Network controller */
    /* FIXME: missing NS SONIC DP83932 */

    /* SCSI adapter */
    /* FIXME: missing NCR 53C94 */

    /* ISA devices (floppy, serial, parallel) */

    for (i = 0; i < MAX_FD; i++) {
        index = drive_get_index(IF_FLOPPY, 0, i);
        if (index == -1)
            fd[i] = NULL;
        else
            fd[i] = drives_table[index].bdrv;
    }
    fdctrl_init(i8259[1], 1, 1, 0x80003000, fd);
    for(i = 0; i < MAX_SERIAL_PORTS; i++) {
        if (serial_hds[i]) {
            serial_mm_init(serial_base[i], 0, i8259[serial_irq[i]], serial_hds[i], 1);
        }
    }
    /* Parallel port */
    if (parallel_hds[0]) parallel_mm_init(0x80008000, 0, i8259[1], parallel_hds[0]);

    /* Sound card */
    /* FIXME: missing Jazz sound, IRQ 18 */

    /* LED indicator */
    /* FIXME: missing LED indicator */

    /* NVRAM */
    ds1225y_init(0x80009000, "nvram");

    /* Video card */
    /* FIXME: This card is not the real one which was in the original PICA,
     * but let's do with what Qemu currenly emulates... */
    isa_vga_mm_init(ds, phys_ram_base + ram_size, ram_size, vga_ram_size,
                    0x40000000, 0x60000000, 0);

    /* LED indicator */
    jazz_led_init(ds, 0x8000f000);
}

QEMUMachine mips_pica61_machine = {
    "pica61",
    "Acer Pica 61",
    mips_pica61_init,
};
