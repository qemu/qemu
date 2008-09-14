/*
 * QEMU Malta board support
 *
 * Copyright (c) 2006 Aurelien Jarno
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
#include "net.h"
#include "boards.h"
#include "smbus.h"
#include "block.h"
#include "flash.h"
#include "mips.h"
#include "pci.h"
#include "qemu-char.h"
#include "sysemu.h"
#include "audio/audio.h"
#include "boards.h"

//#define DEBUG_BOARD_INIT

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

#define ENVP_ADDR (int32_t)0x80002000
#define VIRT_TO_PHYS_ADDEND (-((int64_t)(int32_t)0x80000000))

#define ENVP_NB_ENTRIES	 	16
#define ENVP_ENTRY_SIZE	 	256

#define MAX_IDE_BUS 2

extern FILE *logfile;

typedef struct {
    uint32_t leds;
    uint32_t brk;
    uint32_t gpout;
    uint32_t i2cin;
    uint32_t i2coe;
    uint32_t i2cout;
    uint32_t i2csel;
    CharDriverState *display;
    char display_text[9];
    SerialState *uart;
} MaltaFPGAState;

static PITState *pit;

static struct _loaderparams {
    int ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
} loaderparams;

/* Malta FPGA */
static void malta_fpga_update_display(void *opaque)
{
    char leds_text[9];
    int i;
    MaltaFPGAState *s = opaque;

    for (i = 7 ; i >= 0 ; i--) {
        if (s->leds & (1 << i))
            leds_text[i] = '#';
        else
            leds_text[i] = ' ';
    }
    leds_text[8] = '\0';

    qemu_chr_printf(s->display, "\e[H\n\n|\e[32m%-8.8s\e[00m|\r\n", leds_text);
    qemu_chr_printf(s->display, "\n\n\n\n|\e[31m%-8.8s\e[00m|", s->display_text);
}

/*
 * EEPROM 24C01 / 24C02 emulation.
 *
 * Emulation for serial EEPROMs:
 * 24C01 - 1024 bit (128 x 8)
 * 24C02 - 2048 bit (256 x 8)
 *
 * Typical device names include Microchip 24C02SC or SGS Thomson ST24C02.
 */

//~ #define DEBUG

#if defined(DEBUG)
#  define logout(fmt, args...) fprintf(stderr, "MALTA\t%-24s" fmt, __func__, ##args)
#else
#  define logout(fmt, args...) ((void)0)
#endif

struct _eeprom24c0x_t {
  uint8_t tick;
  uint8_t address;
  uint8_t command;
  uint8_t ack;
  uint8_t scl;
  uint8_t sda;
  uint8_t data;
  //~ uint16_t size;
  uint8_t contents[256];
};

typedef struct _eeprom24c0x_t eeprom24c0x_t;

static eeprom24c0x_t eeprom = {
    contents: {
        /* 00000000: */ 0x80,0x08,0x04,0x0D,0x0A,0x01,0x40,0x00,
        /* 00000008: */ 0x01,0x75,0x54,0x00,0x82,0x08,0x00,0x01,
        /* 00000010: */ 0x8F,0x04,0x02,0x01,0x01,0x00,0x0E,0x00,
        /* 00000018: */ 0x00,0x00,0x00,0x14,0x0F,0x14,0x2D,0x40,
        /* 00000020: */ 0x15,0x08,0x15,0x08,0x00,0x00,0x00,0x00,
        /* 00000028: */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        /* 00000030: */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        /* 00000038: */ 0x00,0x00,0x00,0x00,0x00,0x00,0x12,0xD0,
        /* 00000040: */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        /* 00000048: */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        /* 00000050: */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        /* 00000058: */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        /* 00000060: */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        /* 00000068: */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        /* 00000070: */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        /* 00000078: */ 0x00,0x00,0x00,0x00,0x00,0x00,0x64,0xF4,
    },
};

static uint8_t eeprom24c0x_read(void)
{
    logout("%u: scl = %u, sda = %u, data = 0x%02x\n",
        eeprom.tick, eeprom.scl, eeprom.sda, eeprom.data);
    return eeprom.sda;
}

static void eeprom24c0x_write(int scl, int sda)
{
    if (eeprom.scl && scl && (eeprom.sda != sda)) {
        logout("%u: scl = %u->%u, sda = %u->%u i2c %s\n",
                eeprom.tick, eeprom.scl, scl, eeprom.sda, sda, sda ? "stop" : "start");
        if (!sda) {
            eeprom.tick = 1;
            eeprom.command = 0;
        }
    } else if (eeprom.tick == 0 && !eeprom.ack) {
        /* Waiting for start. */
        logout("%u: scl = %u->%u, sda = %u->%u wait for i2c start\n",
                eeprom.tick, eeprom.scl, scl, eeprom.sda, sda);
    } else if (!eeprom.scl && scl) {
        logout("%u: scl = %u->%u, sda = %u->%u trigger bit\n",
                eeprom.tick, eeprom.scl, scl, eeprom.sda, sda);
        if (eeprom.ack) {
            logout("\ti2c ack bit = 0\n");
            sda = 0;
            eeprom.ack = 0;
        } else if (eeprom.sda == sda) {
            uint8_t bit = (sda != 0);
            logout("\ti2c bit = %d\n", bit);
            if (eeprom.tick < 9) {
                eeprom.command <<= 1;
                eeprom.command += bit;
                eeprom.tick++;
                if (eeprom.tick == 9) {
                    logout("\tcommand 0x%04x, %s\n", eeprom.command, bit ? "read" : "write");
                    eeprom.ack = 1;
                }
            } else if (eeprom.tick < 17) {
                if (eeprom.command & 1) {
                    sda = ((eeprom.data & 0x80) != 0);
                }
                eeprom.address <<= 1;
                eeprom.address += bit;
                eeprom.tick++;
                eeprom.data <<= 1;
                if (eeprom.tick == 17) {
                    eeprom.data = eeprom.contents[eeprom.address];
                    logout("\taddress 0x%04x, data 0x%02x\n", eeprom.address, eeprom.data);
                    eeprom.ack = 1;
                    eeprom.tick = 0;
                }
            } else if (eeprom.tick >= 17) {
                sda = 0;
            }
        } else {
            logout("\tsda changed with raising scl\n");
        }
    } else {
        logout("%u: scl = %u->%u, sda = %u->%u\n", eeprom.tick, eeprom.scl, scl, eeprom.sda, sda);
    }
    eeprom.scl = scl;
    eeprom.sda = sda;
}

static uint32_t malta_fpga_readl(void *opaque, target_phys_addr_t addr)
{
    MaltaFPGAState *s = opaque;
    uint32_t val = 0;
    uint32_t saddr;

    saddr = (addr & 0xfffff);

    switch (saddr) {

    /* SWITCH Register */
    case 0x00200:
        val = 0x00000000;		/* All switches closed */
	break;

    /* STATUS Register */
    case 0x00208:
#ifdef TARGET_WORDS_BIGENDIAN
        val = 0x00000012;
#else
        val = 0x00000010;
#endif
        break;

    /* JMPRS Register */
    case 0x00210:
        val = 0x00;
        break;

    /* LEDBAR Register */
    case 0x00408:
        val = s->leds;
        break;

    /* BRKRES Register */
    case 0x00508:
        val = s->brk;
        break;

    /* UART Registers are handled directly by the serial device */

    /* GPOUT Register */
    case 0x00a00:
        val = s->gpout;
        break;

    /* XXX: implement a real I2C controller */

    /* GPINP Register */
    case 0x00a08:
        /* IN = OUT until a real I2C control is implemented */
        if (s->i2csel)
            val = s->i2cout;
        else
            val = 0x00;
        break;

    /* I2CINP Register */
    case 0x00b00:
        val = ((s->i2cin & ~1) | eeprom24c0x_read());
        break;

    /* I2COE Register */
    case 0x00b08:
        val = s->i2coe;
        break;

    /* I2COUT Register */
    case 0x00b10:
        val = s->i2cout;
        break;

    /* I2CSEL Register */
    case 0x00b18:
        val = s->i2csel;
        break;

    default:
#if 0
        printf ("malta_fpga_read: Bad register offset 0x" TARGET_FMT_lx "\n",
		addr);
#endif
        break;
    }
    return val;
}

static void malta_fpga_writel(void *opaque, target_phys_addr_t addr,
                              uint32_t val)
{
    MaltaFPGAState *s = opaque;
    uint32_t saddr;

    saddr = (addr & 0xfffff);

    switch (saddr) {

    /* SWITCH Register */
    case 0x00200:
        break;

    /* JMPRS Register */
    case 0x00210:
        break;

    /* LEDBAR Register */
    /* XXX: implement a 8-LED array */
    case 0x00408:
        s->leds = val & 0xff;
        break;

    /* ASCIIWORD Register */
    case 0x00410:
        snprintf(s->display_text, 9, "%08X", val);
        malta_fpga_update_display(s);
        break;

    /* ASCIIPOS0 to ASCIIPOS7 Registers */
    case 0x00418:
    case 0x00420:
    case 0x00428:
    case 0x00430:
    case 0x00438:
    case 0x00440:
    case 0x00448:
    case 0x00450:
        s->display_text[(saddr - 0x00418) >> 3] = (char) val;
        malta_fpga_update_display(s);
        break;

    /* SOFTRES Register */
    case 0x00500:
        if (val == 0x42)
            qemu_system_reset_request ();
        break;

    /* BRKRES Register */
    case 0x00508:
        s->brk = val & 0xff;
        break;

    /* UART Registers are handled directly by the serial device */

    /* GPOUT Register */
    case 0x00a00:
        s->gpout = val & 0xff;
        break;

    /* I2COE Register */
    case 0x00b08:
        s->i2coe = val & 0x03;
        break;

    /* I2COUT Register */
    case 0x00b10:
        eeprom24c0x_write(val & 0x02, val & 0x01);
        s->i2cout = val;
        break;

    /* I2CSEL Register */
    case 0x00b18:
        s->i2csel = val & 0x01;
        break;

    default:
#if 0
        printf ("malta_fpga_write: Bad register offset 0x" TARGET_FMT_lx "\n",
		addr);
#endif
        break;
    }
}

static CPUReadMemoryFunc *malta_fpga_read[] = {
   malta_fpga_readl,
   malta_fpga_readl,
   malta_fpga_readl
};

static CPUWriteMemoryFunc *malta_fpga_write[] = {
   malta_fpga_writel,
   malta_fpga_writel,
   malta_fpga_writel
};

static void malta_fpga_reset(void *opaque)
{
    MaltaFPGAState *s = opaque;

    s->leds   = 0x00;
    s->brk    = 0x0a;
    s->gpout  = 0x00;
    s->i2cin  = 0x3;
    s->i2coe  = 0x0;
    s->i2cout = 0x3;
    s->i2csel = 0x1;

    s->display_text[8] = '\0';
    snprintf(s->display_text, 9, "        ");
    malta_fpga_update_display(s);
}

static MaltaFPGAState *malta_fpga_init(target_phys_addr_t base, CPUState *env)
{
    MaltaFPGAState *s;
    CharDriverState *uart_chr;
    int malta;

    s = (MaltaFPGAState *)qemu_mallocz(sizeof(MaltaFPGAState));

    malta = cpu_register_io_memory(0, malta_fpga_read,
                                   malta_fpga_write, s);

    cpu_register_physical_memory(base, 0x900, malta);
    cpu_register_physical_memory(base + 0xa00, 0x100000 - 0xa00, malta);

    s->display = qemu_chr_open("vc:320x200");
    qemu_chr_printf(s->display, "\e[HMalta LEDBAR\r\n");
    qemu_chr_printf(s->display, "+--------+\r\n");
    qemu_chr_printf(s->display, "+        +\r\n");
    qemu_chr_printf(s->display, "+--------+\r\n");
    qemu_chr_printf(s->display, "\n");
    qemu_chr_printf(s->display, "Malta ASCII\r\n");
    qemu_chr_printf(s->display, "+--------+\r\n");
    qemu_chr_printf(s->display, "+        +\r\n");
    qemu_chr_printf(s->display, "+--------+\r\n");

    uart_chr = qemu_chr_open("vc:80Cx24C");
    qemu_chr_printf(uart_chr, "CBUS UART\r\n");
    s->uart =
        serial_mm_init(base + 0x900, 3, env->irq[2], 230400, uart_chr, 1);

    malta_fpga_reset(s);
    qemu_register_reset(malta_fpga_reset, s);

    return s;
}

/* Audio support */
#ifdef HAS_AUDIO
static void audio_init (PCIBus *pci_bus)
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
                if (c->enabled)
                    c->init.init_pci (pci_bus, s);
            }
        }
    }
}
#endif

/* Network support */
static void network_init (PCIBus *pci_bus)
{
    int i;
    NICInfo *nd;

    for(i = 0; i < nb_nics; i++) {
        nd = &nd_table[i];
        if (!nd->model) {
            nd->model = "pcnet";
        }
        if (i == 0  && strcmp(nd->model, "pcnet") == 0) {
            /* The malta board has a PCNet card using PCI SLOT 11 */
            pci_nic_init(pci_bus, nd, 88);
        } else {
            pci_nic_init(pci_bus, nd, -1);
        }
    }
}

/* ROM and pseudo bootloader

   The following code implements a very very simple bootloader. It first
   loads the registers a0 to a3 to the values expected by the OS, and
   then jump at the kernel address.

   The bootloader should pass the locations of the kernel arguments and
   environment variables tables. Those tables contain the 32-bit address
   of NULL terminated strings. The environment variables table should be
   terminated by a NULL address.

   For a simpler implementation, the number of kernel arguments is fixed
   to two (the name of the kernel and the command line), and the two
   tables are actually the same one.

   The registers a0 to a3 should contain the following values:
     a0 - number of kernel arguments
     a1 - 32-bit address of the kernel arguments table
     a2 - 32-bit address of the environment variables table
     a3 - RAM size in bytes
*/

static void write_bootloader (CPUState *env, unsigned long bios_offset, int64_t kernel_entry)
{
    uint32_t *p;

    /* Small bootloader */
    p = (uint32_t *) (phys_ram_base + bios_offset);
    stl_raw(p++, 0x0bf00160);                                      /* j 0x1fc00580 */
    stl_raw(p++, 0x00000000);                                      /* nop */

    /* YAMON service vector */
    stl_raw(phys_ram_base + bios_offset + 0x500, 0xbfc00580);      /* start: */
    stl_raw(phys_ram_base + bios_offset + 0x504, 0xbfc0083c);      /* print_count: */
    stl_raw(phys_ram_base + bios_offset + 0x520, 0xbfc00580);      /* start: */
    stl_raw(phys_ram_base + bios_offset + 0x52c, 0xbfc00800);      /* flush_cache: */
    stl_raw(phys_ram_base + bios_offset + 0x534, 0xbfc00808);      /* print: */
    stl_raw(phys_ram_base + bios_offset + 0x538, 0xbfc00800);      /* reg_cpu_isr: */
    stl_raw(phys_ram_base + bios_offset + 0x53c, 0xbfc00800);      /* unred_cpu_isr: */
    stl_raw(phys_ram_base + bios_offset + 0x540, 0xbfc00800);      /* reg_ic_isr: */
    stl_raw(phys_ram_base + bios_offset + 0x544, 0xbfc00800);      /* unred_ic_isr: */
    stl_raw(phys_ram_base + bios_offset + 0x548, 0xbfc00800);      /* reg_esr: */
    stl_raw(phys_ram_base + bios_offset + 0x54c, 0xbfc00800);      /* unreg_esr: */
    stl_raw(phys_ram_base + bios_offset + 0x550, 0xbfc00800);      /* getchar: */
    stl_raw(phys_ram_base + bios_offset + 0x554, 0xbfc00800);      /* syscon_read: */


    /* Second part of the bootloader */
    p = (uint32_t *) (phys_ram_base + bios_offset + 0x580);
    stl_raw(p++, 0x24040002);                                      /* addiu a0, zero, 2 */
    stl_raw(p++, 0x3c1d0000 | (((ENVP_ADDR - 64) >> 16) & 0xffff)); /* lui sp, high(ENVP_ADDR) */
    stl_raw(p++, 0x37bd0000 | ((ENVP_ADDR - 64) & 0xffff));        /* ori sp, sp, low(ENVP_ADDR) */
    stl_raw(p++, 0x3c050000 | ((ENVP_ADDR >> 16) & 0xffff));       /* lui a1, high(ENVP_ADDR) */
    stl_raw(p++, 0x34a50000 | (ENVP_ADDR & 0xffff));               /* ori a1, a1, low(ENVP_ADDR) */
    stl_raw(p++, 0x3c060000 | (((ENVP_ADDR + 8) >> 16) & 0xffff)); /* lui a2, high(ENVP_ADDR + 8) */
    stl_raw(p++, 0x34c60000 | ((ENVP_ADDR + 8) & 0xffff));         /* ori a2, a2, low(ENVP_ADDR + 8) */
    stl_raw(p++, 0x3c070000 | (loaderparams.ram_size >> 16));     /* lui a3, high(ram_size) */
    stl_raw(p++, 0x34e70000 | (loaderparams.ram_size & 0xffff));  /* ori a3, a3, low(ram_size) */

    /* Load BAR registers as done by YAMON */
    stl_raw(p++, 0x3c09b400);                                      /* lui t1, 0xb400 */

#ifdef TARGET_WORDS_BIGENDIAN
    stl_raw(p++, 0x3c08df00);                                      /* lui t0, 0xdf00 */
#else
    stl_raw(p++, 0x340800df);                                      /* ori t0, r0, 0x00df */
#endif
    stl_raw(p++, 0xad280068);                                      /* sw t0, 0x0068(t1) */

    stl_raw(p++, 0x3c09bbe0);                                      /* lui t1, 0xbbe0 */

#ifdef TARGET_WORDS_BIGENDIAN
    stl_raw(p++, 0x3c08c000);                                      /* lui t0, 0xc000 */
#else
    stl_raw(p++, 0x340800c0);                                      /* ori t0, r0, 0x00c0 */
#endif
    stl_raw(p++, 0xad280048);                                      /* sw t0, 0x0048(t1) */
#ifdef TARGET_WORDS_BIGENDIAN
    stl_raw(p++, 0x3c084000);                                      /* lui t0, 0x4000 */
#else
    stl_raw(p++, 0x34080040);                                      /* ori t0, r0, 0x0040 */
#endif
    stl_raw(p++, 0xad280050);                                      /* sw t0, 0x0050(t1) */

#ifdef TARGET_WORDS_BIGENDIAN
    stl_raw(p++, 0x3c088000);                                      /* lui t0, 0x8000 */
#else
    stl_raw(p++, 0x34080080);                                      /* ori t0, r0, 0x0080 */
#endif
    stl_raw(p++, 0xad280058);                                      /* sw t0, 0x0058(t1) */
#ifdef TARGET_WORDS_BIGENDIAN
    stl_raw(p++, 0x3c083f00);                                      /* lui t0, 0x3f00 */
#else
    stl_raw(p++, 0x3408003f);                                      /* ori t0, r0, 0x003f */
#endif
    stl_raw(p++, 0xad280060);                                      /* sw t0, 0x0060(t1) */

#ifdef TARGET_WORDS_BIGENDIAN
    stl_raw(p++, 0x3c08c100);                                      /* lui t0, 0xc100 */
#else
    stl_raw(p++, 0x340800c1);                                      /* ori t0, r0, 0x00c1 */
#endif
    stl_raw(p++, 0xad280080);                                      /* sw t0, 0x0080(t1) */
#ifdef TARGET_WORDS_BIGENDIAN
    stl_raw(p++, 0x3c085e00);                                      /* lui t0, 0x5e00 */
#else
    stl_raw(p++, 0x3408005e);                                      /* ori t0, r0, 0x005e */
#endif
    stl_raw(p++, 0xad280088);                                      /* sw t0, 0x0088(t1) */

    /* Jump to kernel code */
    stl_raw(p++, 0x3c1f0000 | ((kernel_entry >> 16) & 0xffff));    /* lui ra, high(kernel_entry) */
    stl_raw(p++, 0x37ff0000 | (kernel_entry & 0xffff));            /* ori ra, ra, low(kernel_entry) */
    stl_raw(p++, 0x03e00008);                                      /* jr ra */
    stl_raw(p++, 0x00000000);                                      /* nop */

    /* YAMON subroutines */
    p = (uint32_t *) (phys_ram_base + bios_offset + 0x800);
    stl_raw(p++, 0x03e00008);                                     /* jr ra */
    stl_raw(p++, 0x24020000);                                     /* li v0,0 */
   /* 808 YAMON print */
    stl_raw(p++, 0x03e06821);                                     /* move t5,ra */
    stl_raw(p++, 0x00805821);                                     /* move t3,a0 */
    stl_raw(p++, 0x00a05021);                                     /* move t2,a1 */
    stl_raw(p++, 0x91440000);                                     /* lbu a0,0(t2) */
    stl_raw(p++, 0x254a0001);                                     /* addiu t2,t2,1 */
    stl_raw(p++, 0x10800005);                                     /* beqz a0,834 */
    stl_raw(p++, 0x00000000);                                     /* nop */
    stl_raw(p++, 0x0ff0021c);                                     /* jal 870 */
    stl_raw(p++, 0x00000000);                                     /* nop */
    stl_raw(p++, 0x08000205);                                     /* j 814 */
    stl_raw(p++, 0x00000000);                                     /* nop */
    stl_raw(p++, 0x01a00008);                                     /* jr t5 */
    stl_raw(p++, 0x01602021);                                     /* move a0,t3 */
    /* 0x83c YAMON print_count */
    stl_raw(p++, 0x03e06821);                                     /* move t5,ra */
    stl_raw(p++, 0x00805821);                                     /* move t3,a0 */
    stl_raw(p++, 0x00a05021);                                     /* move t2,a1 */
    stl_raw(p++, 0x00c06021);                                     /* move t4,a2 */
    stl_raw(p++, 0x91440000);                                     /* lbu a0,0(t2) */
    stl_raw(p++, 0x0ff0021c);                                     /* jal 870 */
    stl_raw(p++, 0x00000000);                                     /* nop */
    stl_raw(p++, 0x254a0001);                                     /* addiu t2,t2,1 */
    stl_raw(p++, 0x258cffff);                                     /* addiu t4,t4,-1 */
    stl_raw(p++, 0x1580fffa);                                     /* bnez t4,84c */
    stl_raw(p++, 0x00000000);                                     /* nop */
    stl_raw(p++, 0x01a00008);                                     /* jr t5 */
    stl_raw(p++, 0x01602021);                                     /* move a0,t3 */
    /* 0x870 */
    stl_raw(p++, 0x3c08b800);                                     /* lui t0,0xb400 */
    stl_raw(p++, 0x350803f8);                                     /* ori t0,t0,0x3f8 */
    stl_raw(p++, 0x91090005);                                     /* lbu t1,5(t0) */
    stl_raw(p++, 0x00000000);                                     /* nop */
    stl_raw(p++, 0x31290040);                                     /* andi t1,t1,0x40 */
    stl_raw(p++, 0x1120fffc);                                     /* beqz t1,878 <outch+0x8> */
    stl_raw(p++, 0x00000000);                                     /* nop */
    stl_raw(p++, 0x03e00008);                                     /* jr ra */
    stl_raw(p++, 0xa1040000);                                     /* sb a0,0(t0) */

}

static void prom_set(int index, const char *string, ...)
{
    va_list ap;
    int32_t *p;
    int32_t table_addr;
    char *s;

    if (index >= ENVP_NB_ENTRIES)
        return;

    p = (int32_t *) (phys_ram_base + ENVP_ADDR + VIRT_TO_PHYS_ADDEND);
    p += index;

    if (string == NULL) {
        stl_raw(p, 0);
        return;
    }

    table_addr = ENVP_ADDR + sizeof(int32_t) * ENVP_NB_ENTRIES + index * ENVP_ENTRY_SIZE;
    s = (char *) (phys_ram_base + VIRT_TO_PHYS_ADDEND + table_addr);

    stl_raw(p, table_addr);

    va_start(ap, string);
    vsnprintf (s, ENVP_ENTRY_SIZE, string, ap);
    va_end(ap);
}

/* Kernel */
static int64_t load_kernel (CPUState *env)
{
    int64_t kernel_entry, kernel_low, kernel_high;
    int index = 0;
    long initrd_size;
    ram_addr_t initrd_offset;

    if (load_elf(loaderparams.kernel_filename, VIRT_TO_PHYS_ADDEND,
                 &kernel_entry, &kernel_low, &kernel_high) < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n",
                loaderparams.kernel_filename);
        exit(1);
    }

    /* load initrd */
    initrd_size = 0;
    initrd_offset = 0;
    if (loaderparams.initrd_filename) {
        initrd_size = get_image_size (loaderparams.initrd_filename);
        if (initrd_size > 0) {
            initrd_offset = (kernel_high + ~TARGET_PAGE_MASK) & TARGET_PAGE_MASK;
            if (initrd_offset + initrd_size > ram_size) {
                fprintf(stderr,
                        "qemu: memory too small for initial ram disk '%s'\n",
                        loaderparams.initrd_filename);
                exit(1);
            }
            initrd_size = load_image(loaderparams.initrd_filename,
                                     phys_ram_base + initrd_offset);
        }
        if (initrd_size == (target_ulong) -1) {
            fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                    loaderparams.initrd_filename);
            exit(1);
        }
    }

    /* Store command line.  */
    prom_set(index++, loaderparams.kernel_filename);
    if (initrd_size > 0)
        prom_set(index++, "rd_start=0x" TARGET_FMT_lx " rd_size=%li %s",
                 PHYS_TO_VIRT(initrd_offset), initrd_size,
                 loaderparams.kernel_cmdline);
    else
        prom_set(index++, loaderparams.kernel_cmdline);

    /* Setup minimum environment variables */
    prom_set(index++, "memsize");
    prom_set(index++, "%i", loaderparams.ram_size);
    prom_set(index++, "modetty0");
    prom_set(index++, "38400n8r");
    prom_set(index++, NULL);

    return kernel_entry;
}

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);

    /* The bootload does not need to be rewritten as it is located in a
       read only location. The kernel location and the arguments table
       location does not change. */
    if (loaderparams.kernel_filename) {
        env->CP0_Status &= ~((1 << CP0St_BEV) | (1 << CP0St_ERL));
        load_kernel (env);
    }
}

static
void mips_malta_init (ram_addr_t ram_size, int vga_ram_size,
                      const char *boot_device, DisplayState *ds,
                      const char *kernel_filename, const char *kernel_cmdline,
                      const char *initrd_filename, const char *cpu_model)
{
    char buf[1024];
    unsigned long bios_offset;
    target_long bios_size;
    int64_t kernel_entry;
    PCIBus *pci_bus;
    CPUState *env;
    RTCState *rtc_state;
    fdctrl_t *floppy_controller;
    MaltaFPGAState *malta_fpga;
    qemu_irq *i8259;
    int piix4_devfn;
    uint8_t *eeprom_buf;
    i2c_bus *smbus;
    int i;
    int index;
    BlockDriverState *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    BlockDriverState *fd[MAX_FD];
    int fl_idx = 0;
    int fl_sectors = 0;

    /* init CPUs */
    if (cpu_model == NULL) {
#ifdef TARGET_MIPS64
        cpu_model = "20Kc";
#else
        cpu_model = "24Kf";
#endif
    }
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    qemu_register_reset(main_cpu_reset, env);

    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, IO_MEM_RAM);

    /* Map the bios at two physical locations, as on the real board. */
    bios_offset = ram_size + vga_ram_size;
    cpu_register_physical_memory(0x1e000000LL,
                                 BIOS_SIZE, bios_offset | IO_MEM_ROM);
    cpu_register_physical_memory(0x1fc00000LL,
                                 BIOS_SIZE, bios_offset | IO_MEM_ROM);

    /* FPGA */
    malta_fpga = malta_fpga_init(0x1f000000LL, env);

    /* Load firmware in flash / BIOS unless we boot directly into a kernel. */
    if (kernel_filename) {
        /* Write a small bootloader to the flash location. */
        loaderparams.ram_size = ram_size;
        loaderparams.kernel_filename = kernel_filename;
        loaderparams.kernel_cmdline = kernel_cmdline;
        loaderparams.initrd_filename = initrd_filename;
        kernel_entry = load_kernel(env);
        env->CP0_Status &= ~((1 << CP0St_BEV) | (1 << CP0St_ERL));
        write_bootloader(env, bios_offset, kernel_entry);
    } else {
        index = drive_get_index(IF_PFLASH, 0, fl_idx);
        if (index != -1) {
            /* Load firmware from flash. */
            bios_size = 0x400000;
            fl_sectors = bios_size >> 16;
#ifdef DEBUG_BOARD_INIT
            printf("Register parallel flash %d size " TARGET_FMT_lx " at "
                   "offset %08lx addr %08llx '%s' %x\n",
                   fl_idx, bios_size, bios_offset, 0x1e000000LL,
                   bdrv_get_device_name(drives_table[index].bdrv), fl_sectors);
#endif
            pflash_cfi01_register(0x1e000000LL, bios_offset,
                                  drives_table[index].bdrv, 65536, fl_sectors,
                                  4, 0x0000, 0x0000, 0x0000, 0x0000);
            fl_idx++;
        } else {
            /* Load a BIOS image. */
            if (bios_name == NULL)
                bios_name = BIOS_FILENAME;
            snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
            bios_size = load_image(buf, phys_ram_base + bios_offset);
            if ((bios_size < 0 || bios_size > BIOS_SIZE) && !kernel_filename) {
                fprintf(stderr,
                        "qemu: Could not load MIPS bios '%s', and no -kernel argument was specified\n",
                        buf);
                exit(1);
            }
        }
        /* In little endian mode the 32bit words in the bios are swapped,
           a neat trick which allows bi-endian firmware. */
#ifndef TARGET_WORDS_BIGENDIAN
        {
            uint32_t *addr;
            for (addr = (uint32_t *)(phys_ram_base + bios_offset);
                 addr < (uint32_t *)(phys_ram_base + bios_offset + bios_size);
                 addr++) {
                *addr = bswap32(*addr);
            }
        }
#endif
    }

    /* Board ID = 0x420 (Malta Board with CoreLV)
       XXX: theoretically 0x1e000010 should map to flash and 0x1fc00010 should
       map to the board ID. */
    stl_raw(phys_ram_base + bios_offset + 0x10, 0x00000420);

    /* Init internal devices */
    cpu_mips_irq_init_cpu(env);
    cpu_mips_clock_init(env);

    /* Interrupt controller */
    /* The 8259 is attached to the MIPS CPU INT0 pin, ie interrupt 2 */
    i8259 = i8259_init(env->irq[2]);

    /* Northbridge */
    pci_bus = pci_gt64120_init(i8259);

    /* Southbridge */

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

    piix4_devfn = piix4_init(pci_bus, 80);
    pci_piix4_ide_init(pci_bus, hd, piix4_devfn + 1, i8259);
    usb_uhci_piix4_init(pci_bus, piix4_devfn + 2);
    smbus = piix4_pm_init(pci_bus, piix4_devfn + 3, 0x1100, i8259[9]);
    eeprom_buf = qemu_mallocz(8 * 256); /* XXX: make this persistent */
    for (i = 0; i < 8; i++) {
        /* TODO: Populate SPD eeprom data.  */
        smbus_eeprom_device_init(smbus, 0x50 + i, eeprom_buf + (i * 256));
    }
    pit = pit_init(0x40, i8259[0]);
    DMA_init(0);

    /* Super I/O */
    i8042_init(i8259[1], i8259[12], 0x60);
    rtc_state = rtc_init(0x70, i8259[8]);
    if (serial_hds[0])
        serial_init(0x3f8, i8259[4], 115200, serial_hds[0]);
    if (serial_hds[1])
        serial_init(0x2f8, i8259[3], 115200, serial_hds[1]);
    if (parallel_hds[0])
        parallel_init(0x378, i8259[7], parallel_hds[0]);
    for(i = 0; i < MAX_FD; i++) {
        index = drive_get_index(IF_FLOPPY, 0, i);
       if (index != -1)
           fd[i] = drives_table[index].bdrv;
       else
           fd[i] = NULL;
    }
    floppy_controller = fdctrl_init(i8259[6], 2, 0, 0x3f0, fd);

    /* Sound card */
#ifdef HAS_AUDIO
    audio_init(pci_bus);
#endif

    /* Network card */
    network_init(pci_bus);

    /* Optional PCI video card */
    pci_cirrus_vga_init(pci_bus, ds, phys_ram_base + ram_size,
                        ram_size, vga_ram_size);
}

QEMUMachine mips_malta_machine = {
    .name = "malta",
    .desc = "MIPS Malta Core LV",
    .init = mips_malta_init,
    .ram_require = VGA_RAM_SIZE + BIOS_SIZE,
    .nodisk_ok = 1,
};
