/*
 * QEMU PC System Emulator
 * 
 * Copyright (c) 2003-2004 Fabrice Bellard
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <malloc.h>
#include <termios.h>
#include <sys/poll.h>
#include <errno.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include "cpu.h"
#include "vl.h"

#define BIOS_FILENAME "bios.bin"
#define VGABIOS_FILENAME "vgabios.bin"
#define LINUX_BOOT_FILENAME "linux_boot.bin"

#define KERNEL_LOAD_ADDR     0x00100000
#define INITRD_LOAD_ADDR     0x00400000
#define KERNEL_PARAMS_ADDR   0x00090000
#define KERNEL_CMDLINE_ADDR  0x00099000

int speaker_data_on;
int dummy_refresh_clock;

static void ioport80_write(CPUState *env, uint32_t addr, uint32_t data)
{
}

#define REG_EQUIPMENT_BYTE          0x14

static void cmos_init(int ram_size, int boot_device)
{
    RTCState *s = &rtc_state;
    int val;
    
    /* various important CMOS locations needed by PC/Bochs bios */

    s->cmos_data[REG_EQUIPMENT_BYTE] = 0x02; /* FPU is there */
    s->cmos_data[REG_EQUIPMENT_BYTE] |= 0x04; /* PS/2 mouse installed */

    /* memory size */
    val = (ram_size / 1024) - 1024;
    if (val > 65535)
        val = 65535;
    s->cmos_data[0x17] = val;
    s->cmos_data[0x18] = val >> 8;
    s->cmos_data[0x30] = val;
    s->cmos_data[0x31] = val >> 8;

    val = (ram_size / 65536) - ((16 * 1024 * 1024) / 65536);
    if (val > 65535)
        val = 65535;
    s->cmos_data[0x34] = val;
    s->cmos_data[0x35] = val >> 8;
    
    switch(boot_device) {
    case 'a':
    case 'b':
        s->cmos_data[0x3d] = 0x01; /* floppy boot */
        break;
    default:
    case 'c':
        s->cmos_data[0x3d] = 0x02; /* hard drive boot */
        break;
    case 'd':
        s->cmos_data[0x3d] = 0x03; /* CD-ROM boot */
        break;
    }
}

void cmos_register_fd (uint8_t fd0, uint8_t fd1)
{
    RTCState *s = &rtc_state;
    int nb = 0;

    s->cmos_data[0x10] = 0;
    switch (fd0) {
    case 0:
        /* 1.44 Mb 3"5 drive */
        s->cmos_data[0x10] |= 0x40;
        break;
    case 1:
        /* 2.88 Mb 3"5 drive */
        s->cmos_data[0x10] |= 0x60;
        break;
    case 2:
        /* 1.2 Mb 5"5 drive */
        s->cmos_data[0x10] |= 0x20;
        break;
    }
    switch (fd1) {
    case 0:
        /* 1.44 Mb 3"5 drive */
        s->cmos_data[0x10] |= 0x04;
        break;
    case 1:
        /* 2.88 Mb 3"5 drive */
        s->cmos_data[0x10] |= 0x06;
        break;
    case 2:
        /* 1.2 Mb 5"5 drive */
        s->cmos_data[0x10] |= 0x02;
        break;
    }
    if (fd0 < 3)
        nb++;
    if (fd1 < 3)
        nb++;
    switch (nb) {
    case 0:
        break;
    case 1:
        s->cmos_data[REG_EQUIPMENT_BYTE] |= 0x01; /* 1 drive, ready for boot */
        break;
    case 2:
        s->cmos_data[REG_EQUIPMENT_BYTE] |= 0x41; /* 2 drives, ready for boot */
        break;
    }
}

void speaker_ioport_write(CPUState *env, uint32_t addr, uint32_t val)
{
    speaker_data_on = (val >> 1) & 1;
    pit_set_gate(&pit_channels[2], val & 1);
}

uint32_t speaker_ioport_read(CPUState *env, uint32_t addr)
{
    int out;
    out = pit_get_out(&pit_channels[2]);
    dummy_refresh_clock ^= 1;
    return (speaker_data_on << 1) | pit_channels[2].gate | (out << 5) |
      (dummy_refresh_clock << 4);
}

/***********************************************************/
/* PC floppy disk controler emulation glue */
#define PC_FDC_DMA  0x2
#define PC_FDC_IRQ  0x6
#define PC_FDC_BASE 0x3F0

static void fdctrl_register (unsigned char **disknames, int ro,
                             char boot_device)
{
    int i;

    fdctrl_init(PC_FDC_IRQ, PC_FDC_DMA, 0, PC_FDC_BASE, boot_device);
    for (i = 0; i < MAX_FD; i++) {
        if (disknames[i] != NULL)
            fdctrl_disk_change(i, disknames[i], ro);
    }
}

/***********************************************************/
/* Bochs BIOS debug ports */

void bochs_bios_write(CPUX86State *env, uint32_t addr, uint32_t val)
{
    switch(addr) {
        /* Bochs BIOS messages */
    case 0x400:
    case 0x401:
        fprintf(stderr, "BIOS panic at rombios.c, line %d\n", val);
        exit(1);
    case 0x402:
    case 0x403:
#ifdef DEBUG_BIOS
        fprintf(stderr, "%c", val);
#endif
        break;

        /* LGPL'ed VGA BIOS messages */
    case 0x501:
    case 0x502:
        fprintf(stderr, "VGA BIOS panic, line %d\n", val);
        exit(1);
    case 0x500:
    case 0x503:
#ifdef DEBUG_BIOS
        fprintf(stderr, "%c", val);
#endif
        break;
    }
}

void bochs_bios_init(void)
{
    register_ioport_write(0x400, 1, bochs_bios_write, 2);
    register_ioport_write(0x401, 1, bochs_bios_write, 2);
    register_ioport_write(0x402, 1, bochs_bios_write, 1);
    register_ioport_write(0x403, 1, bochs_bios_write, 1);

    register_ioport_write(0x501, 1, bochs_bios_write, 2);
    register_ioport_write(0x502, 1, bochs_bios_write, 2);
    register_ioport_write(0x500, 1, bochs_bios_write, 1);
    register_ioport_write(0x503, 1, bochs_bios_write, 1);
}


int load_kernel(const char *filename, uint8_t *addr, 
                uint8_t *real_addr)
{
    int fd, size;
    int setup_sects;

    fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -1;

    /* load 16 bit code */
    if (read(fd, real_addr, 512) != 512)
        goto fail;
    setup_sects = real_addr[0x1F1];
    if (!setup_sects)
        setup_sects = 4;
    if (read(fd, real_addr + 512, setup_sects * 512) != 
        setup_sects * 512)
        goto fail;
    
    /* load 32 bit code */
    size = read(fd, addr, 16 * 1024 * 1024);
    if (size < 0)
        goto fail;
    close(fd);
    return size;
 fail:
    close(fd);
    return -1;
}

/* PC hardware initialisation */
void pc_init(int ram_size, int vga_ram_size, int boot_device,
             DisplayState *ds, const char **fd_filename, int snapshot,
             const char *kernel_filename, const char *kernel_cmdline,
             const char *initrd_filename)
{
    char buf[1024];
    int ret, linux_boot, initrd_size;

    linux_boot = (kernel_filename != NULL);

    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, 0);

    /* BIOS load */
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
    ret = load_image(buf, phys_ram_base + 0x000f0000);
    if (ret != 0x10000) {
        fprintf(stderr, "qemu: could not load PC bios '%s'\n", buf);
        exit(1);
    }
    
    /* VGA BIOS load */
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, VGABIOS_FILENAME);
    ret = load_image(buf, phys_ram_base + 0x000c0000);
    
    /* setup basic memory access */
    cpu_register_physical_memory(0xc0000, 0x10000, 0xc0000 | IO_MEM_ROM);
    cpu_register_physical_memory(0xf0000, 0x10000, 0xf0000 | IO_MEM_ROM);
    
    bochs_bios_init();

    if (linux_boot) {
        uint8_t bootsect[512];

        if (bs_table[0] == NULL) {
            fprintf(stderr, "A disk image must be given for 'hda' when booting a Linux kernel\n");
            exit(1);
        }
        snprintf(buf, sizeof(buf), "%s/%s", bios_dir, LINUX_BOOT_FILENAME);
        ret = load_image(buf, bootsect);
        if (ret != sizeof(bootsect)) {
            fprintf(stderr, "qemu: could not load linux boot sector '%s'\n",
                    buf);
            exit(1);
        }

        bdrv_set_boot_sector(bs_table[0], bootsect, sizeof(bootsect));

        /* now we can load the kernel */
        ret = load_kernel(kernel_filename, 
                          phys_ram_base + KERNEL_LOAD_ADDR,
                          phys_ram_base + KERNEL_PARAMS_ADDR);
        if (ret < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n", 
                    kernel_filename);
            exit(1);
        }
        
        /* load initrd */
        initrd_size = 0;
        if (initrd_filename) {
            initrd_size = load_image(initrd_filename, phys_ram_base + INITRD_LOAD_ADDR);
            if (initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initial ram disk '%s'\n", 
                        initrd_filename);
                exit(1);
            }
        }
        if (initrd_size > 0) {
            stl_raw(phys_ram_base + KERNEL_PARAMS_ADDR + 0x218, INITRD_LOAD_ADDR);
            stl_raw(phys_ram_base + KERNEL_PARAMS_ADDR + 0x21c, initrd_size);
        }
        pstrcpy(phys_ram_base + KERNEL_CMDLINE_ADDR, 4096,
                kernel_cmdline);
        stw_raw(phys_ram_base + KERNEL_PARAMS_ADDR + 0x20, 0xA33F);
        stw_raw(phys_ram_base + KERNEL_PARAMS_ADDR + 0x22,
                KERNEL_CMDLINE_ADDR - KERNEL_PARAMS_ADDR);
        /* loader type */
        stw_raw(phys_ram_base + KERNEL_PARAMS_ADDR + 0x210, 0x01);
    }

    /* init basic PC hardware */
    register_ioport_write(0x80, 1, ioport80_write, 1);

    vga_initialize(ds, phys_ram_base + ram_size, ram_size, 
                   vga_ram_size);

    rtc_init(0x70, 8);
    cmos_init(ram_size, boot_device);
    register_ioport_read(0x61, 1, speaker_ioport_read, 1);
    register_ioport_write(0x61, 1, speaker_ioport_write, 1);

    pic_init();
    pit_init();
    serial_init(0x3f8, 4);
    ne2000_init(0x300, 9);
    ide_init();
    kbd_init();
    AUD_init();
    DMA_init();
    SB16_init();

    fdctrl_register((unsigned char **)fd_filename, snapshot, boot_device);
}
