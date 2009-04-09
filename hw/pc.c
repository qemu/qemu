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
#include "hw.h"
#include "pc.h"
#include "fdc.h"
#include "pci.h"
#include "block.h"
#include "sysemu.h"
#include "audio/audio.h"
#include "net.h"
#include "smbus.h"
#include "boards.h"
#include "monitor.h"
#include "fw_cfg.h"
#include "virtio-blk.h"
#include "virtio-balloon.h"
#include "virtio-console.h"
#include "hpet_emul.h"

/* output Bochs bios info messages */
//#define DEBUG_BIOS

#define BIOS_FILENAME "bios.bin"
#define VGABIOS_FILENAME "vgabios.bin"
#define VGABIOS_CIRRUS_FILENAME "vgabios-cirrus.bin"

#define PC_MAX_BIOS_SIZE (4 * 1024 * 1024)

/* Leave a chunk of memory at the top of RAM for the BIOS ACPI tables.  */
#define ACPI_DATA_SIZE       0x10000
#define BIOS_CFG_IOPORT 0x510
#define FW_CFG_ACPI_TABLES (FW_CFG_ARCH_LOCAL + 0)

#define MAX_IDE_BUS 2

static fdctrl_t *floppy_controller;
static RTCState *rtc_state;
static PITState *pit;
static IOAPICState *ioapic;
static PCIDevice *i440fx_state;

static void ioport80_write(void *opaque, uint32_t addr, uint32_t data)
{
}

/* MSDOS compatibility mode FPU exception support */
static qemu_irq ferr_irq;
/* XXX: add IGNNE support */
void cpu_set_ferr(CPUX86State *s)
{
    qemu_irq_raise(ferr_irq);
}

static void ioportF0_write(void *opaque, uint32_t addr, uint32_t data)
{
    qemu_irq_lower(ferr_irq);
}

/* TSC handling */
uint64_t cpu_get_tsc(CPUX86State *env)
{
    /* Note: when using kqemu, it is more logical to return the host TSC
       because kqemu does not trap the RDTSC instruction for
       performance reasons */
#ifdef USE_KQEMU
    if (env->kqemu_enabled) {
        return cpu_get_real_ticks();
    } else
#endif
    {
        return cpu_get_ticks();
    }
}

/* SMM support */
void cpu_smm_update(CPUState *env)
{
    if (i440fx_state && env == first_cpu)
        i440fx_set_smm(i440fx_state, (env->hflags >> HF_SMM_SHIFT) & 1);
}


/* IRQ handling */
int cpu_get_pic_interrupt(CPUState *env)
{
    int intno;

    intno = apic_get_interrupt(env);
    if (intno >= 0) {
        /* set irq request if a PIC irq is still pending */
        /* XXX: improve that */
        pic_update_irq(isa_pic);
        return intno;
    }
    /* read the irq from the PIC */
    if (!apic_accept_pic_intr(env))
        return -1;

    intno = pic_read_irq(isa_pic);
    return intno;
}

static void pic_irq_request(void *opaque, int irq, int level)
{
    CPUState *env = first_cpu;

    if (env->apic_state) {
        while (env) {
            if (apic_accept_pic_intr(env))
                apic_deliver_pic_intr(env, level);
            env = env->next_cpu;
        }
    } else {
        if (level)
            cpu_interrupt(env, CPU_INTERRUPT_HARD);
        else
            cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
    }
}

/* PC cmos mappings */

#define REG_EQUIPMENT_BYTE          0x14

static int cmos_get_fd_drive_type(int fd0)
{
    int val;

    switch (fd0) {
    case 0:
        /* 1.44 Mb 3"5 drive */
        val = 4;
        break;
    case 1:
        /* 2.88 Mb 3"5 drive */
        val = 5;
        break;
    case 2:
        /* 1.2 Mb 5"5 drive */
        val = 2;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void cmos_init_hd(int type_ofs, int info_ofs, BlockDriverState *hd)
{
    RTCState *s = rtc_state;
    int cylinders, heads, sectors;
    bdrv_get_geometry_hint(hd, &cylinders, &heads, &sectors);
    rtc_set_memory(s, type_ofs, 47);
    rtc_set_memory(s, info_ofs, cylinders);
    rtc_set_memory(s, info_ofs + 1, cylinders >> 8);
    rtc_set_memory(s, info_ofs + 2, heads);
    rtc_set_memory(s, info_ofs + 3, 0xff);
    rtc_set_memory(s, info_ofs + 4, 0xff);
    rtc_set_memory(s, info_ofs + 5, 0xc0 | ((heads > 8) << 3));
    rtc_set_memory(s, info_ofs + 6, cylinders);
    rtc_set_memory(s, info_ofs + 7, cylinders >> 8);
    rtc_set_memory(s, info_ofs + 8, sectors);
}

/* convert boot_device letter to something recognizable by the bios */
static int boot_device2nibble(char boot_device)
{
    switch(boot_device) {
    case 'a':
    case 'b':
        return 0x01; /* floppy boot */
    case 'c':
        return 0x02; /* hard drive boot */
    case 'd':
        return 0x03; /* CD-ROM boot */
    case 'n':
        return 0x04; /* Network boot */
    }
    return 0;
}

/* copy/pasted from cmos_init, should be made a general function
 and used there as well */
static int pc_boot_set(void *opaque, const char *boot_device)
{
    Monitor *mon = cur_mon;
#define PC_MAX_BOOT_DEVICES 3
    RTCState *s = (RTCState *)opaque;
    int nbds, bds[3] = { 0, };
    int i;

    nbds = strlen(boot_device);
    if (nbds > PC_MAX_BOOT_DEVICES) {
        monitor_printf(mon, "Too many boot devices for PC\n");
        return(1);
    }
    for (i = 0; i < nbds; i++) {
        bds[i] = boot_device2nibble(boot_device[i]);
        if (bds[i] == 0) {
            monitor_printf(mon, "Invalid boot device for PC: '%c'\n",
                           boot_device[i]);
            return(1);
        }
    }
    rtc_set_memory(s, 0x3d, (bds[1] << 4) | bds[0]);
    rtc_set_memory(s, 0x38, (bds[2] << 4));
    return(0);
}

/* hd_table must contain 4 block drivers */
static void cmos_init(ram_addr_t ram_size, ram_addr_t above_4g_mem_size,
                      const char *boot_device, BlockDriverState **hd_table)
{
    RTCState *s = rtc_state;
    int nbds, bds[3] = { 0, };
    int val;
    int fd0, fd1, nb;
    int i;

    /* various important CMOS locations needed by PC/Bochs bios */

    /* memory size */
    val = 640; /* base memory in K */
    rtc_set_memory(s, 0x15, val);
    rtc_set_memory(s, 0x16, val >> 8);

    val = (ram_size / 1024) - 1024;
    if (val > 65535)
        val = 65535;
    rtc_set_memory(s, 0x17, val);
    rtc_set_memory(s, 0x18, val >> 8);
    rtc_set_memory(s, 0x30, val);
    rtc_set_memory(s, 0x31, val >> 8);

    if (above_4g_mem_size) {
        rtc_set_memory(s, 0x5b, (unsigned int)above_4g_mem_size >> 16);
        rtc_set_memory(s, 0x5c, (unsigned int)above_4g_mem_size >> 24);
        rtc_set_memory(s, 0x5d, (uint64_t)above_4g_mem_size >> 32);
    }

    if (ram_size > (16 * 1024 * 1024))
        val = (ram_size / 65536) - ((16 * 1024 * 1024) / 65536);
    else
        val = 0;
    if (val > 65535)
        val = 65535;
    rtc_set_memory(s, 0x34, val);
    rtc_set_memory(s, 0x35, val >> 8);

    /* set the number of CPU */
    rtc_set_memory(s, 0x5f, smp_cpus - 1);

    /* set boot devices, and disable floppy signature check if requested */
#define PC_MAX_BOOT_DEVICES 3
    nbds = strlen(boot_device);
    if (nbds > PC_MAX_BOOT_DEVICES) {
        fprintf(stderr, "Too many boot devices for PC\n");
        exit(1);
    }
    for (i = 0; i < nbds; i++) {
        bds[i] = boot_device2nibble(boot_device[i]);
        if (bds[i] == 0) {
            fprintf(stderr, "Invalid boot device for PC: '%c'\n",
                    boot_device[i]);
            exit(1);
        }
    }
    rtc_set_memory(s, 0x3d, (bds[1] << 4) | bds[0]);
    rtc_set_memory(s, 0x38, (bds[2] << 4) | (fd_bootchk ?  0x0 : 0x1));

    /* floppy type */

    fd0 = fdctrl_get_drive_type(floppy_controller, 0);
    fd1 = fdctrl_get_drive_type(floppy_controller, 1);

    val = (cmos_get_fd_drive_type(fd0) << 4) | cmos_get_fd_drive_type(fd1);
    rtc_set_memory(s, 0x10, val);

    val = 0;
    nb = 0;
    if (fd0 < 3)
        nb++;
    if (fd1 < 3)
        nb++;
    switch (nb) {
    case 0:
        break;
    case 1:
        val |= 0x01; /* 1 drive, ready for boot */
        break;
    case 2:
        val |= 0x41; /* 2 drives, ready for boot */
        break;
    }
    val |= 0x02; /* FPU is there */
    val |= 0x04; /* PS/2 mouse installed */
    rtc_set_memory(s, REG_EQUIPMENT_BYTE, val);

    /* hard drives */

    rtc_set_memory(s, 0x12, (hd_table[0] ? 0xf0 : 0) | (hd_table[1] ? 0x0f : 0));
    if (hd_table[0])
        cmos_init_hd(0x19, 0x1b, hd_table[0]);
    if (hd_table[1])
        cmos_init_hd(0x1a, 0x24, hd_table[1]);

    val = 0;
    for (i = 0; i < 4; i++) {
        if (hd_table[i]) {
            int cylinders, heads, sectors, translation;
            /* NOTE: bdrv_get_geometry_hint() returns the physical
                geometry.  It is always such that: 1 <= sects <= 63, 1
                <= heads <= 16, 1 <= cylinders <= 16383. The BIOS
                geometry can be different if a translation is done. */
            translation = bdrv_get_translation_hint(hd_table[i]);
            if (translation == BIOS_ATA_TRANSLATION_AUTO) {
                bdrv_get_geometry_hint(hd_table[i], &cylinders, &heads, &sectors);
                if (cylinders <= 1024 && heads <= 16 && sectors <= 63) {
                    /* No translation. */
                    translation = 0;
                } else {
                    /* LBA translation. */
                    translation = 1;
                }
            } else {
                translation--;
            }
            val |= translation << (i * 2);
        }
    }
    rtc_set_memory(s, 0x39, val);
}

void ioport_set_a20(int enable)
{
    /* XXX: send to all CPUs ? */
    cpu_x86_set_a20(first_cpu, enable);
}

int ioport_get_a20(void)
{
    return ((first_cpu->a20_mask >> 20) & 1);
}

static void ioport92_write(void *opaque, uint32_t addr, uint32_t val)
{
    ioport_set_a20((val >> 1) & 1);
    /* XXX: bit 0 is fast reset */
}

static uint32_t ioport92_read(void *opaque, uint32_t addr)
{
    return ioport_get_a20() << 1;
}

/***********************************************************/
/* Bochs BIOS debug ports */

static void bochs_bios_write(void *opaque, uint32_t addr, uint32_t val)
{
    static const char shutdown_str[8] = "Shutdown";
    static int shutdown_index = 0;

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
    case 0x8900:
        /* same as Bochs power off */
        if (val == shutdown_str[shutdown_index]) {
            shutdown_index++;
            if (shutdown_index == 8) {
                shutdown_index = 0;
                qemu_system_shutdown_request();
            }
        } else {
            shutdown_index = 0;
        }
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

static void bochs_bios_init(void)
{
    void *fw_cfg;

    register_ioport_write(0x400, 1, 2, bochs_bios_write, NULL);
    register_ioport_write(0x401, 1, 2, bochs_bios_write, NULL);
    register_ioport_write(0x402, 1, 1, bochs_bios_write, NULL);
    register_ioport_write(0x403, 1, 1, bochs_bios_write, NULL);
    register_ioport_write(0x8900, 1, 1, bochs_bios_write, NULL);

    register_ioport_write(0x501, 1, 2, bochs_bios_write, NULL);
    register_ioport_write(0x502, 1, 2, bochs_bios_write, NULL);
    register_ioport_write(0x500, 1, 1, bochs_bios_write, NULL);
    register_ioport_write(0x503, 1, 1, bochs_bios_write, NULL);

    fw_cfg = fw_cfg_init(BIOS_CFG_IOPORT, BIOS_CFG_IOPORT + 1, 0, 0);
    fw_cfg_add_i32(fw_cfg, FW_CFG_ID, 1);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
    fw_cfg_add_bytes(fw_cfg, FW_CFG_ACPI_TABLES, (uint8_t *)acpi_tables,
                     acpi_tables_len);
}

/* Generate an initial boot sector which sets state and jump to
   a specified vector */
static void generate_bootsect(uint8_t *option_rom,
                              uint32_t gpr[8], uint16_t segs[6], uint16_t ip)
{
    uint8_t rom[512], *p, *reloc;
    uint8_t sum;
    int i;

    memset(rom, 0, sizeof(rom));

    p = rom;
    /* Make sure we have an option rom signature */
    *p++ = 0x55;
    *p++ = 0xaa;

    /* ROM size in sectors*/
    *p++ = 1;

    /* Hook int19 */

    *p++ = 0x50;		/* push ax */
    *p++ = 0x1e;		/* push ds */
    *p++ = 0x31; *p++ = 0xc0;	/* xor ax, ax */
    *p++ = 0x8e; *p++ = 0xd8;	/* mov ax, ds */

    *p++ = 0xc7; *p++ = 0x06;   /* movvw _start,0x64 */
    *p++ = 0x64; *p++ = 0x00;
    reloc = p;
    *p++ = 0x00; *p++ = 0x00;

    *p++ = 0x8c; *p++ = 0x0e;   /* mov cs,0x66 */
    *p++ = 0x66; *p++ = 0x00;

    *p++ = 0x1f;		/* pop ds */
    *p++ = 0x58;		/* pop ax */
    *p++ = 0xcb;		/* lret */
    
    /* Actual code */
    *reloc = (p - rom);

    *p++ = 0xfa;		/* CLI */
    *p++ = 0xfc;		/* CLD */

    for (i = 0; i < 6; i++) {
	if (i == 1)		/* Skip CS */
	    continue;

	*p++ = 0xb8;		/* MOV AX,imm16 */
	*p++ = segs[i];
	*p++ = segs[i] >> 8;
	*p++ = 0x8e;		/* MOV <seg>,AX */
	*p++ = 0xc0 + (i << 3);
    }

    for (i = 0; i < 8; i++) {
	*p++ = 0x66;		/* 32-bit operand size */
	*p++ = 0xb8 + i;	/* MOV <reg>,imm32 */
	*p++ = gpr[i];
	*p++ = gpr[i] >> 8;
	*p++ = gpr[i] >> 16;
	*p++ = gpr[i] >> 24;
    }

    *p++ = 0xea;		/* JMP FAR */
    *p++ = ip;			/* IP */
    *p++ = ip >> 8;
    *p++ = segs[1];		/* CS */
    *p++ = segs[1] >> 8;

    /* sign rom */
    sum = 0;
    for (i = 0; i < (sizeof(rom) - 1); i++)
        sum += rom[i];
    rom[sizeof(rom) - 1] = -sum;

    memcpy(option_rom, rom, sizeof(rom));
}

static long get_file_size(FILE *f)
{
    long where, size;

    /* XXX: on Unix systems, using fstat() probably makes more sense */

    where = ftell(f);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, where, SEEK_SET);

    return size;
}

static void load_linux(uint8_t *option_rom,
                       const char *kernel_filename,
		       const char *initrd_filename,
		       const char *kernel_cmdline)
{
    uint16_t protocol;
    uint32_t gpr[8];
    uint16_t seg[6];
    uint16_t real_seg;
    int setup_size, kernel_size, initrd_size, cmdline_size;
    uint32_t initrd_max;
    uint8_t header[1024];
    target_phys_addr_t real_addr, prot_addr, cmdline_addr, initrd_addr;
    FILE *f, *fi;

    /* Align to 16 bytes as a paranoia measure */
    cmdline_size = (strlen(kernel_cmdline)+16) & ~15;

    /* load the kernel header */
    f = fopen(kernel_filename, "rb");
    if (!f || !(kernel_size = get_file_size(f)) ||
	fread(header, 1, 1024, f) != 1024) {
	fprintf(stderr, "qemu: could not load kernel '%s'\n",
		kernel_filename);
	exit(1);
    }

    /* kernel protocol version */
#if 0
    fprintf(stderr, "header magic: %#x\n", ldl_p(header+0x202));
#endif
    if (ldl_p(header+0x202) == 0x53726448)
	protocol = lduw_p(header+0x206);
    else
	protocol = 0;

    if (protocol < 0x200 || !(header[0x211] & 0x01)) {
	/* Low kernel */
	real_addr    = 0x90000;
	cmdline_addr = 0x9a000 - cmdline_size;
	prot_addr    = 0x10000;
    } else if (protocol < 0x202) {
	/* High but ancient kernel */
	real_addr    = 0x90000;
	cmdline_addr = 0x9a000 - cmdline_size;
	prot_addr    = 0x100000;
    } else {
	/* High and recent kernel */
	real_addr    = 0x10000;
	cmdline_addr = 0x20000;
	prot_addr    = 0x100000;
    }

#if 0
    fprintf(stderr,
	    "qemu: real_addr     = 0x" TARGET_FMT_plx "\n"
	    "qemu: cmdline_addr  = 0x" TARGET_FMT_plx "\n"
	    "qemu: prot_addr     = 0x" TARGET_FMT_plx "\n",
	    real_addr,
	    cmdline_addr,
	    prot_addr);
#endif

    /* highest address for loading the initrd */
    if (protocol >= 0x203)
	initrd_max = ldl_p(header+0x22c);
    else
	initrd_max = 0x37ffffff;

    if (initrd_max >= ram_size-ACPI_DATA_SIZE)
	initrd_max = ram_size-ACPI_DATA_SIZE-1;

    /* kernel command line */
    pstrcpy_targphys(cmdline_addr, 4096, kernel_cmdline);

    if (protocol >= 0x202) {
	stl_p(header+0x228, cmdline_addr);
    } else {
	stw_p(header+0x20, 0xA33F);
	stw_p(header+0x22, cmdline_addr-real_addr);
    }

    /* loader type */
    /* High nybble = B reserved for Qemu; low nybble is revision number.
       If this code is substantially changed, you may want to consider
       incrementing the revision. */
    if (protocol >= 0x200)
	header[0x210] = 0xB0;

    /* heap */
    if (protocol >= 0x201) {
	header[0x211] |= 0x80;	/* CAN_USE_HEAP */
	stw_p(header+0x224, cmdline_addr-real_addr-0x200);
    }

    /* load initrd */
    if (initrd_filename) {
	if (protocol < 0x200) {
	    fprintf(stderr, "qemu: linux kernel too old to load a ram disk\n");
	    exit(1);
	}

	fi = fopen(initrd_filename, "rb");
	if (!fi) {
	    fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
		    initrd_filename);
	    exit(1);
	}

	initrd_size = get_file_size(fi);
	initrd_addr = (initrd_max-initrd_size) & ~4095;

        fprintf(stderr, "qemu: loading initrd (%#x bytes) at 0x" TARGET_FMT_plx
                "\n", initrd_size, initrd_addr);

	if (!fread_targphys_ok(initrd_addr, initrd_size, fi)) {
	    fprintf(stderr, "qemu: read error on initial ram disk '%s'\n",
		    initrd_filename);
	    exit(1);
	}
	fclose(fi);

	stl_p(header+0x218, initrd_addr);
	stl_p(header+0x21c, initrd_size);
    }

    /* store the finalized header and load the rest of the kernel */
    cpu_physical_memory_write(real_addr, header, 1024);

    setup_size = header[0x1f1];
    if (setup_size == 0)
	setup_size = 4;

    setup_size = (setup_size+1)*512;
    kernel_size -= setup_size;	/* Size of protected-mode code */

    if (!fread_targphys_ok(real_addr+1024, setup_size-1024, f) ||
	!fread_targphys_ok(prot_addr, kernel_size, f)) {
	fprintf(stderr, "qemu: read error on kernel '%s'\n",
		kernel_filename);
	exit(1);
    }
    fclose(f);

    /* generate bootsector to set up the initial register state */
    real_seg = real_addr >> 4;
    seg[0] = seg[2] = seg[3] = seg[4] = seg[4] = real_seg;
    seg[1] = real_seg+0x20;	/* CS */
    memset(gpr, 0, sizeof gpr);
    gpr[4] = cmdline_addr-real_addr-16;	/* SP (-16 is paranoia) */

    generate_bootsect(option_rom, gpr, seg, 0);
}

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);
}

static const int ide_iobase[2] = { 0x1f0, 0x170 };
static const int ide_iobase2[2] = { 0x3f6, 0x376 };
static const int ide_irq[2] = { 14, 15 };

#define NE2000_NB_MAX 6

static int ne2000_io[NE2000_NB_MAX] = { 0x300, 0x320, 0x340, 0x360, 0x280, 0x380 };
static int ne2000_irq[NE2000_NB_MAX] = { 9, 10, 11, 3, 4, 5 };

static int serial_io[MAX_SERIAL_PORTS] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
static int serial_irq[MAX_SERIAL_PORTS] = { 4, 3, 4, 3 };

static int parallel_io[MAX_PARALLEL_PORTS] = { 0x378, 0x278, 0x3bc };
static int parallel_irq[MAX_PARALLEL_PORTS] = { 7, 7, 7 };

#ifdef HAS_AUDIO
static void audio_init (PCIBus *pci_bus, qemu_irq *pic)
{
    struct soundhw *c;
    int audio_enabled = 0;

    for (c = soundhw; !audio_enabled && c->name; ++c) {
        audio_enabled = c->enabled;
    }

    if (audio_enabled) {
        AudioState *s;

        s = AUD_init ();
        if (s) {
            for (c = soundhw; c->name; ++c) {
                if (c->enabled) {
                    if (c->isa) {
                        c->init.init_isa (s, pic);
                    }
                    else {
                        if (pci_bus) {
                            c->init.init_pci (pci_bus, s);
                        }
                    }
                }
            }
        }
    }
}
#endif

static void pc_init_ne2k_isa(NICInfo *nd, qemu_irq *pic)
{
    static int nb_ne2k = 0;

    if (nb_ne2k == NE2000_NB_MAX)
        return;
    isa_ne2000_init(ne2000_io[nb_ne2k], pic[ne2000_irq[nb_ne2k]], nd);
    nb_ne2k++;
}

static int load_option_rom(const char *oprom, target_phys_addr_t start,
                           target_phys_addr_t end)
{
        int size;

        size = get_image_size(oprom);
        if (size > 0 && start + size > end) {
            fprintf(stderr, "Not enough space to load option rom '%s'\n",
                    oprom);
            exit(1);
        }
        size = load_image_targphys(oprom, start, end - start);
        if (size < 0) {
            fprintf(stderr, "Could not load option rom '%s'\n", oprom);
            exit(1);
        }
        /* Round up optiom rom size to the next 2k boundary */
        size = (size + 2047) & ~2047;
        return size;
}

/* PC hardware initialisation */
static void pc_init1(ram_addr_t ram_size, int vga_ram_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename,
                     int pci_enabled, const char *cpu_model)
{
    char buf[1024];
    int ret, linux_boot, i;
    ram_addr_t ram_addr, vga_ram_addr, bios_offset, option_rom_offset;
    ram_addr_t below_4g_mem_size, above_4g_mem_size = 0;
    int bios_size, isa_bios_size, oprom_area_size;
    PCIBus *pci_bus;
    int piix3_devfn = -1;
    CPUState *env;
    qemu_irq *cpu_irq;
    qemu_irq *i8259;
    int index;
    BlockDriverState *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    BlockDriverState *fd[MAX_FD];
    int using_vga = cirrus_vga_enabled || std_vga_enabled || vmsvga_enabled;

    if (ram_size >= 0xe0000000 ) {
        above_4g_mem_size = ram_size - 0xe0000000;
        below_4g_mem_size = 0xe0000000;
    } else {
        below_4g_mem_size = ram_size;
    }

    linux_boot = (kernel_filename != NULL);

    /* init CPUs */
    if (cpu_model == NULL) {
#ifdef TARGET_X86_64
        cpu_model = "qemu64";
#else
        cpu_model = "qemu32";
#endif
    }
    
    for(i = 0; i < smp_cpus; i++) {
        env = cpu_init(cpu_model);
        if (!env) {
            fprintf(stderr, "Unable to find x86 CPU definition\n");
            exit(1);
        }
        if (i != 0)
            env->halted = 1;
        if (smp_cpus > 1) {
            /* XXX: enable it in all cases */
            env->cpuid_features |= CPUID_APIC;
        }
        qemu_register_reset(main_cpu_reset, env);
        if (pci_enabled) {
            apic_init(env);
        }
    }

    vmport_init();

    /* allocate RAM */
    ram_addr = qemu_ram_alloc(0xa0000);
    cpu_register_physical_memory(0, 0xa0000, ram_addr);

    /* Allocate, even though we won't register, so we don't break the
     * phys_ram_base + PA assumption. This range includes vga (0xa0000 - 0xc0000),
     * and some bios areas, which will be registered later
     */
    ram_addr = qemu_ram_alloc(0x100000 - 0xa0000);
    ram_addr = qemu_ram_alloc(below_4g_mem_size - 0x100000);
    cpu_register_physical_memory(0x100000,
                 below_4g_mem_size - 0x100000,
                 ram_addr);

    /* above 4giga memory allocation */
    if (above_4g_mem_size > 0) {
        ram_addr = qemu_ram_alloc(above_4g_mem_size);
        cpu_register_physical_memory(0x100000000ULL,
                                     above_4g_mem_size,
                                     ram_addr);
    }


    /* allocate VGA RAM */
    vga_ram_addr = qemu_ram_alloc(vga_ram_size);

    /* BIOS load */
    if (bios_name == NULL)
        bios_name = BIOS_FILENAME;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
    bios_size = get_image_size(buf);
    if (bios_size <= 0 ||
        (bios_size % 65536) != 0) {
        goto bios_error;
    }
    bios_offset = qemu_ram_alloc(bios_size);
    ret = load_image(buf, phys_ram_base + bios_offset);
    if (ret != bios_size) {
    bios_error:
        fprintf(stderr, "qemu: could not load PC BIOS '%s'\n", buf);
        exit(1);
    }
    /* map the last 128KB of the BIOS in ISA space */
    isa_bios_size = bios_size;
    if (isa_bios_size > (128 * 1024))
        isa_bios_size = 128 * 1024;
    cpu_register_physical_memory(0x100000 - isa_bios_size,
                                 isa_bios_size,
                                 (bios_offset + bios_size - isa_bios_size) | IO_MEM_ROM);



    option_rom_offset = qemu_ram_alloc(0x20000);
    oprom_area_size = 0;
    cpu_register_physical_memory(0xc0000, 0x20000,
                                 option_rom_offset | IO_MEM_ROM);

    if (using_vga) {
        /* VGA BIOS load */
        if (cirrus_vga_enabled) {
            snprintf(buf, sizeof(buf), "%s/%s", bios_dir,
                     VGABIOS_CIRRUS_FILENAME);
        } else {
            snprintf(buf, sizeof(buf), "%s/%s", bios_dir, VGABIOS_FILENAME);
        }
        oprom_area_size = load_option_rom(buf, 0xc0000, 0xe0000);
    }
    /* Although video roms can grow larger than 0x8000, the area between
     * 0xc0000 - 0xc8000 is reserved for them. It means we won't be looking
     * for any other kind of option rom inside this area */
    if (oprom_area_size < 0x8000)
        oprom_area_size = 0x8000;

    if (linux_boot) {
        load_linux(phys_ram_base + option_rom_offset + oprom_area_size,
                   kernel_filename, initrd_filename, kernel_cmdline);
        oprom_area_size += 2048;
    }

    for (i = 0; i < nb_option_roms; i++) {
        oprom_area_size += load_option_rom(option_rom[i],
                                           0xc0000 + oprom_area_size, 0xe0000);
    }

    /* map all the bios at the top of memory */
    cpu_register_physical_memory((uint32_t)(-bios_size),
                                 bios_size, bios_offset | IO_MEM_ROM);

    bochs_bios_init();

    cpu_irq = qemu_allocate_irqs(pic_irq_request, NULL, 1);
    i8259 = i8259_init(cpu_irq[0]);
    ferr_irq = i8259[13];

    if (pci_enabled) {
        pci_bus = i440fx_init(&i440fx_state, i8259);
        piix3_devfn = piix3_init(pci_bus, -1);
    } else {
        pci_bus = NULL;
    }

    /* init basic PC hardware */
    register_ioport_write(0x80, 1, 1, ioport80_write, NULL);

    register_ioport_write(0xf0, 1, 1, ioportF0_write, NULL);

    if (cirrus_vga_enabled) {
        if (pci_enabled) {
            pci_cirrus_vga_init(pci_bus,
                                phys_ram_base + vga_ram_addr,
                                vga_ram_addr, vga_ram_size);
        } else {
            isa_cirrus_vga_init(phys_ram_base + vga_ram_addr,
                                vga_ram_addr, vga_ram_size);
        }
    } else if (vmsvga_enabled) {
        if (pci_enabled)
            pci_vmsvga_init(pci_bus, phys_ram_base + vga_ram_addr,
                            vga_ram_addr, vga_ram_size);
        else
            fprintf(stderr, "%s: vmware_vga: no PCI bus\n", __FUNCTION__);
    } else if (std_vga_enabled) {
        if (pci_enabled) {
            pci_vga_init(pci_bus, phys_ram_base + vga_ram_addr,
                         vga_ram_addr, vga_ram_size, 0, 0);
        } else {
            isa_vga_init(phys_ram_base + vga_ram_addr,
                         vga_ram_addr, vga_ram_size);
        }
    }

    rtc_state = rtc_init(0x70, i8259[8], 2000);

    qemu_register_boot_set(pc_boot_set, rtc_state);

    register_ioport_read(0x92, 1, 1, ioport92_read, NULL);
    register_ioport_write(0x92, 1, 1, ioport92_write, NULL);

    if (pci_enabled) {
        ioapic = ioapic_init();
    }
    pit = pit_init(0x40, i8259[0]);
    pcspk_init(pit);
    if (!no_hpet) {
        hpet_init(i8259);
    }
    if (pci_enabled) {
        pic_set_alt_irq_func(isa_pic, ioapic_set_irq, ioapic);
    }

    for(i = 0; i < MAX_SERIAL_PORTS; i++) {
        if (serial_hds[i]) {
            serial_init(serial_io[i], i8259[serial_irq[i]], 115200,
                        serial_hds[i]);
        }
    }

    for(i = 0; i < MAX_PARALLEL_PORTS; i++) {
        if (parallel_hds[i]) {
            parallel_init(parallel_io[i], i8259[parallel_irq[i]],
                          parallel_hds[i]);
        }
    }

    for(i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];

        if (!pci_enabled || (nd->model && strcmp(nd->model, "ne2k_isa") == 0))
            pc_init_ne2k_isa(nd, i8259);
        else
            pci_nic_init(pci_bus, nd, -1, "ne2k_pci");
    }

    qemu_system_hot_add_init();

    if (drive_get_max_bus(IF_IDE) >= MAX_IDE_BUS) {
        fprintf(stderr, "qemu: too many IDE bus\n");
        exit(1);
    }

    for(i = 0; i < MAX_IDE_BUS * MAX_IDE_DEVS; i++) {
        index = drive_get_index(IF_IDE, i / MAX_IDE_DEVS, i % MAX_IDE_DEVS);
	if (index != -1)
	    hd[i] = drives_table[index].bdrv;
	else
	    hd[i] = NULL;
    }

    if (pci_enabled) {
        pci_piix3_ide_init(pci_bus, hd, piix3_devfn + 1, i8259);
    } else {
        for(i = 0; i < MAX_IDE_BUS; i++) {
            isa_ide_init(ide_iobase[i], ide_iobase2[i], i8259[ide_irq[i]],
	                 hd[MAX_IDE_DEVS * i], hd[MAX_IDE_DEVS * i + 1]);
        }
    }

    i8042_init(i8259[1], i8259[12], 0x60);
    DMA_init(0);
#ifdef HAS_AUDIO
    audio_init(pci_enabled ? pci_bus : NULL, i8259);
#endif

    for(i = 0; i < MAX_FD; i++) {
        index = drive_get_index(IF_FLOPPY, 0, i);
	if (index != -1)
	    fd[i] = drives_table[index].bdrv;
	else
	    fd[i] = NULL;
    }
    floppy_controller = fdctrl_init(i8259[6], 2, 0, 0x3f0, fd);

    cmos_init(below_4g_mem_size, above_4g_mem_size, boot_device, hd);

    if (pci_enabled && usb_enabled) {
        usb_uhci_piix3_init(pci_bus, piix3_devfn + 2);
    }

    if (pci_enabled && acpi_enabled) {
        uint8_t *eeprom_buf = qemu_mallocz(8 * 256); /* XXX: make this persistent */
        i2c_bus *smbus;

        /* TODO: Populate SPD eeprom data.  */
        smbus = piix4_pm_init(pci_bus, piix3_devfn + 3, 0xb100, i8259[9]);
        for (i = 0; i < 8; i++) {
            smbus_eeprom_device_init(smbus, 0x50 + i, eeprom_buf + (i * 256));
        }
    }

    if (i440fx_state) {
        i440fx_init_memory_mappings(i440fx_state);
    }

    if (pci_enabled) {
	int max_bus;
        int bus, unit;
        void *scsi;

        max_bus = drive_get_max_bus(IF_SCSI);

	for (bus = 0; bus <= max_bus; bus++) {
            scsi = lsi_scsi_init(pci_bus, -1);
            for (unit = 0; unit < LSI_MAX_DEVS; unit++) {
	        index = drive_get_index(IF_SCSI, bus, unit);
		if (index == -1)
		    continue;
		lsi_scsi_attach(scsi, drives_table[index].bdrv, unit);
	    }
        }
    }

    /* Add virtio block devices */
    if (pci_enabled) {
        int index;
        int unit_id = 0;

        while ((index = drive_get_index(IF_VIRTIO, 0, unit_id)) != -1) {
            virtio_blk_init(pci_bus, drives_table[index].bdrv);
            unit_id++;
        }
    }

    /* Add virtio balloon device */
    if (pci_enabled)
        virtio_balloon_init(pci_bus);

    /* Add virtio console devices */
    if (pci_enabled) {
        for(i = 0; i < MAX_VIRTIO_CONSOLES; i++) {
            if (virtcon_hds[i])
                virtio_console_init(pci_bus, virtcon_hds[i]);
        }
    }
}

static void pc_init_pci(ram_addr_t ram_size, int vga_ram_size,
                        const char *boot_device,
                        const char *kernel_filename,
                        const char *kernel_cmdline,
                        const char *initrd_filename,
                        const char *cpu_model)
{
    pc_init1(ram_size, vga_ram_size, boot_device,
             kernel_filename, kernel_cmdline,
             initrd_filename, 1, cpu_model);
}

static void pc_init_isa(ram_addr_t ram_size, int vga_ram_size,
                        const char *boot_device,
                        const char *kernel_filename,
                        const char *kernel_cmdline,
                        const char *initrd_filename,
                        const char *cpu_model)
{
    pc_init1(ram_size, vga_ram_size, boot_device,
             kernel_filename, kernel_cmdline,
             initrd_filename, 0, cpu_model);
}

/* set CMOS shutdown status register (index 0xF) as S3_resume(0xFE)
   BIOS will read it and start S3 resume at POST Entry */
void cmos_set_s3_resume(void)
{
    if (rtc_state)
        rtc_set_memory(rtc_state, 0xF, 0xFE);
}

QEMUMachine pc_machine = {
    .name = "pc",
    .desc = "Standard PC",
    .init = pc_init_pci,
    .ram_require = VGA_RAM_SIZE + PC_MAX_BIOS_SIZE,
    .max_cpus = 255,
};

QEMUMachine isapc_machine = {
    .name = "isapc",
    .desc = "ISA-only PC",
    .init = pc_init_isa,
    .ram_require = VGA_RAM_SIZE + PC_MAX_BIOS_SIZE,
    .max_cpus = 1,
};
