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

#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/char/serial.h"
#include "hw/block/fdc.h"
#include "net/net.h"
#include "hw/boards.h"
#include "hw/i2c/smbus.h"
#include "block/block.h"
#include "hw/block/flash.h"
#include "hw/mips/mips.h"
#include "hw/mips/cpudevs.h"
#include "hw/pci/pci.h"
#include "sysemu/char.h"
#include "sysemu/sysemu.h"
#include "sysemu/arch_init.h"
#include "qemu/log.h"
#include "hw/mips/bios.h"
#include "hw/ide.h"
#include "hw/loader.h"
#include "elf.h"
#include "hw/timer/mc146818rtc.h"
#include "hw/timer/i8254.h"
#include "sysemu/blockdev.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"             /* SysBusDevice */
#include "qemu/host-utils.h"
#include "sysemu/qtest.h"
#include "qemu/error-report.h"
#include "hw/empty_slot.h"

//#define DEBUG_BOARD_INIT

#define ENVP_ADDR		0x80002000l
#define ENVP_NB_ENTRIES	 	16
#define ENVP_ENTRY_SIZE	 	256

/* Hardware addresses */
#define FLASH_ADDRESS 0x1e000000ULL
#define FPGA_ADDRESS  0x1f000000ULL
#define RESET_ADDRESS 0x1fc00000ULL

#define FLASH_SIZE    0x400000

#define MAX_IDE_BUS 2

typedef struct {
    MemoryRegion iomem;
    MemoryRegion iomem_lo; /* 0 - 0x900 */
    MemoryRegion iomem_hi; /* 0xa00 - 0x100000 */
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

#define TYPE_MIPS_MALTA "mips-malta"
#define MIPS_MALTA(obj) OBJECT_CHECK(MaltaState, (obj), TYPE_MIPS_MALTA)

typedef struct {
    SysBusDevice parent_obj;

    qemu_irq *i8259;
} MaltaState;

static ISADevice *pit;

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

    qemu_chr_fe_printf(s->display, "\e[H\n\n|\e[32m%-8.8s\e[00m|\r\n", leds_text);
    qemu_chr_fe_printf(s->display, "\n\n\n\n|\e[31m%-8.8s\e[00m|", s->display_text);
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
#  define logout(fmt, ...) fprintf(stderr, "MALTA\t%-24s" fmt, __func__, ## __VA_ARGS__)
#else
#  define logout(fmt, ...) ((void)0)
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

static eeprom24c0x_t spd_eeprom = {
    .contents = {
        /* 00000000: */ 0x80,0x08,0xFF,0x0D,0x0A,0xFF,0x40,0x00,
        /* 00000008: */ 0x01,0x75,0x54,0x00,0x82,0x08,0x00,0x01,
        /* 00000010: */ 0x8F,0x04,0x02,0x01,0x01,0x00,0x00,0x00,
        /* 00000018: */ 0x00,0x00,0x00,0x14,0x0F,0x14,0x2D,0xFF,
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

static void generate_eeprom_spd(uint8_t *eeprom, ram_addr_t ram_size)
{
    enum { SDR = 0x4, DDR2 = 0x8 } type;
    uint8_t *spd = spd_eeprom.contents;
    uint8_t nbanks = 0;
    uint16_t density = 0;
    int i;

    /* work in terms of MB */
    ram_size >>= 20;

    while ((ram_size >= 4) && (nbanks <= 2)) {
        int sz_log2 = MIN(31 - clz32(ram_size), 14);
        nbanks++;
        density |= 1 << (sz_log2 - 2);
        ram_size -= 1 << sz_log2;
    }

    /* split to 2 banks if possible */
    if ((nbanks == 1) && (density > 1)) {
        nbanks++;
        density >>= 1;
    }

    if (density & 0xff00) {
        density = (density & 0xe0) | ((density >> 8) & 0x1f);
        type = DDR2;
    } else if (!(density & 0x1f)) {
        type = DDR2;
    } else {
        type = SDR;
    }

    if (ram_size) {
        fprintf(stderr, "Warning: SPD cannot represent final %dMB"
                " of SDRAM\n", (int)ram_size);
    }

    /* fill in SPD memory information */
    spd[2] = type;
    spd[5] = nbanks;
    spd[31] = density;

    /* checksum */
    spd[63] = 0;
    for (i = 0; i < 63; i++) {
        spd[63] += spd[i];
    }

    /* copy for SMBUS */
    memcpy(eeprom, spd, sizeof(spd_eeprom.contents));
}

static void generate_eeprom_serial(uint8_t *eeprom)
{
    int i, pos = 0;
    uint8_t mac[6] = { 0x00 };
    uint8_t sn[5] = { 0x01, 0x23, 0x45, 0x67, 0x89 };

    /* version */
    eeprom[pos++] = 0x01;

    /* count */
    eeprom[pos++] = 0x02;

    /* MAC address */
    eeprom[pos++] = 0x01; /* MAC */
    eeprom[pos++] = 0x06; /* length */
    memcpy(&eeprom[pos], mac, sizeof(mac));
    pos += sizeof(mac);

    /* serial number */
    eeprom[pos++] = 0x02; /* serial */
    eeprom[pos++] = 0x05; /* length */
    memcpy(&eeprom[pos], sn, sizeof(sn));
    pos += sizeof(sn);

    /* checksum */
    eeprom[pos] = 0;
    for (i = 0; i < pos; i++) {
        eeprom[pos] += eeprom[i];
    }
}

static uint8_t eeprom24c0x_read(eeprom24c0x_t *eeprom)
{
    logout("%u: scl = %u, sda = %u, data = 0x%02x\n",
        eeprom->tick, eeprom->scl, eeprom->sda, eeprom->data);
    return eeprom->sda;
}

static void eeprom24c0x_write(eeprom24c0x_t *eeprom, int scl, int sda)
{
    if (eeprom->scl && scl && (eeprom->sda != sda)) {
        logout("%u: scl = %u->%u, sda = %u->%u i2c %s\n",
                eeprom->tick, eeprom->scl, scl, eeprom->sda, sda,
                sda ? "stop" : "start");
        if (!sda) {
            eeprom->tick = 1;
            eeprom->command = 0;
        }
    } else if (eeprom->tick == 0 && !eeprom->ack) {
        /* Waiting for start. */
        logout("%u: scl = %u->%u, sda = %u->%u wait for i2c start\n",
                eeprom->tick, eeprom->scl, scl, eeprom->sda, sda);
    } else if (!eeprom->scl && scl) {
        logout("%u: scl = %u->%u, sda = %u->%u trigger bit\n",
                eeprom->tick, eeprom->scl, scl, eeprom->sda, sda);
        if (eeprom->ack) {
            logout("\ti2c ack bit = 0\n");
            sda = 0;
            eeprom->ack = 0;
        } else if (eeprom->sda == sda) {
            uint8_t bit = (sda != 0);
            logout("\ti2c bit = %d\n", bit);
            if (eeprom->tick < 9) {
                eeprom->command <<= 1;
                eeprom->command += bit;
                eeprom->tick++;
                if (eeprom->tick == 9) {
                    logout("\tcommand 0x%04x, %s\n", eeprom->command,
                           bit ? "read" : "write");
                    eeprom->ack = 1;
                }
            } else if (eeprom->tick < 17) {
                if (eeprom->command & 1) {
                    sda = ((eeprom->data & 0x80) != 0);
                }
                eeprom->address <<= 1;
                eeprom->address += bit;
                eeprom->tick++;
                eeprom->data <<= 1;
                if (eeprom->tick == 17) {
                    eeprom->data = eeprom->contents[eeprom->address];
                    logout("\taddress 0x%04x, data 0x%02x\n",
                           eeprom->address, eeprom->data);
                    eeprom->ack = 1;
                    eeprom->tick = 0;
                }
            } else if (eeprom->tick >= 17) {
                sda = 0;
            }
        } else {
            logout("\tsda changed with raising scl\n");
        }
    } else {
        logout("%u: scl = %u->%u, sda = %u->%u\n", eeprom->tick, eeprom->scl,
               scl, eeprom->sda, sda);
    }
    eeprom->scl = scl;
    eeprom->sda = sda;
}

static uint64_t malta_fpga_read(void *opaque, hwaddr addr,
                                unsigned size)
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
        val = ((s->i2cin & ~1) | eeprom24c0x_read(&spd_eeprom));
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

static void malta_fpga_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
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
    case 0x00408:
        s->leds = val & 0xff;
        malta_fpga_update_display(s);
        break;

    /* ASCIIWORD Register */
    case 0x00410:
        snprintf(s->display_text, 9, "%08X", (uint32_t)val);
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
        eeprom24c0x_write(&spd_eeprom, val & 0x02, val & 0x01);
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

static const MemoryRegionOps malta_fpga_ops = {
    .read = malta_fpga_read,
    .write = malta_fpga_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
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
}

static void malta_fpga_led_init(CharDriverState *chr)
{
    qemu_chr_fe_printf(chr, "\e[HMalta LEDBAR\r\n");
    qemu_chr_fe_printf(chr, "+--------+\r\n");
    qemu_chr_fe_printf(chr, "+        +\r\n");
    qemu_chr_fe_printf(chr, "+--------+\r\n");
    qemu_chr_fe_printf(chr, "\n");
    qemu_chr_fe_printf(chr, "Malta ASCII\r\n");
    qemu_chr_fe_printf(chr, "+--------+\r\n");
    qemu_chr_fe_printf(chr, "+        +\r\n");
    qemu_chr_fe_printf(chr, "+--------+\r\n");
}

static MaltaFPGAState *malta_fpga_init(MemoryRegion *address_space,
         hwaddr base, qemu_irq uart_irq, CharDriverState *uart_chr)
{
    MaltaFPGAState *s;

    s = (MaltaFPGAState *)g_malloc0(sizeof(MaltaFPGAState));

    memory_region_init_io(&s->iomem, NULL, &malta_fpga_ops, s,
                          "malta-fpga", 0x100000);
    memory_region_init_alias(&s->iomem_lo, NULL, "malta-fpga",
                             &s->iomem, 0, 0x900);
    memory_region_init_alias(&s->iomem_hi, NULL, "malta-fpga",
                             &s->iomem, 0xa00, 0x10000-0xa00);

    memory_region_add_subregion(address_space, base, &s->iomem_lo);
    memory_region_add_subregion(address_space, base + 0xa00, &s->iomem_hi);

    s->display = qemu_chr_new("fpga", "vc:320x200", malta_fpga_led_init);

    s->uart = serial_mm_init(address_space, base + 0x900, 3, uart_irq,
                             230400, uart_chr, DEVICE_NATIVE_ENDIAN);

    malta_fpga_reset(s);
    qemu_register_reset(malta_fpga_reset, s);

    return s;
}

/* Network support */
static void network_init(PCIBus *pci_bus)
{
    int i;

    for(i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];
        const char *default_devaddr = NULL;

        if (i == 0 && (!nd->model || strcmp(nd->model, "pcnet") == 0))
            /* The malta board has a PCNet card using PCI SLOT 11 */
            default_devaddr = "0b";

        pci_nic_init_nofail(nd, pci_bus, "pcnet", default_devaddr);
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

static void write_bootloader (CPUMIPSState *env, uint8_t *base,
                              int64_t kernel_entry)
{
    uint32_t *p;

    /* Small bootloader */
    p = (uint32_t *)base;
    stl_raw(p++, 0x0bf00160);                                      /* j 0x1fc00580 */
    stl_raw(p++, 0x00000000);                                      /* nop */

    /* YAMON service vector */
    stl_raw(base + 0x500, 0xbfc00580);      /* start: */
    stl_raw(base + 0x504, 0xbfc0083c);      /* print_count: */
    stl_raw(base + 0x520, 0xbfc00580);      /* start: */
    stl_raw(base + 0x52c, 0xbfc00800);      /* flush_cache: */
    stl_raw(base + 0x534, 0xbfc00808);      /* print: */
    stl_raw(base + 0x538, 0xbfc00800);      /* reg_cpu_isr: */
    stl_raw(base + 0x53c, 0xbfc00800);      /* unred_cpu_isr: */
    stl_raw(base + 0x540, 0xbfc00800);      /* reg_ic_isr: */
    stl_raw(base + 0x544, 0xbfc00800);      /* unred_ic_isr: */
    stl_raw(base + 0x548, 0xbfc00800);      /* reg_esr: */
    stl_raw(base + 0x54c, 0xbfc00800);      /* unreg_esr: */
    stl_raw(base + 0x550, 0xbfc00800);      /* getchar: */
    stl_raw(base + 0x554, 0xbfc00800);      /* syscon_read: */


    /* Second part of the bootloader */
    p = (uint32_t *) (base + 0x580);
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
    p = (uint32_t *) (base + 0x800);
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

static void GCC_FMT_ATTR(3, 4) prom_set(uint32_t* prom_buf, int index,
                                        const char *string, ...)
{
    va_list ap;
    int32_t table_addr;

    if (index >= ENVP_NB_ENTRIES)
        return;

    if (string == NULL) {
        prom_buf[index] = 0;
        return;
    }

    table_addr = sizeof(int32_t) * ENVP_NB_ENTRIES + index * ENVP_ENTRY_SIZE;
    prom_buf[index] = tswap32(ENVP_ADDR + table_addr);

    va_start(ap, string);
    vsnprintf((char *)prom_buf + table_addr, ENVP_ENTRY_SIZE, string, ap);
    va_end(ap);
}

/* Kernel */
static int64_t load_kernel (void)
{
    int64_t kernel_entry, kernel_high;
    long initrd_size;
    ram_addr_t initrd_offset;
    int big_endian;
    uint32_t *prom_buf;
    long prom_size;
    int prom_index = 0;

#ifdef TARGET_WORDS_BIGENDIAN
    big_endian = 1;
#else
    big_endian = 0;
#endif

    if (load_elf(loaderparams.kernel_filename, cpu_mips_kseg0_to_phys, NULL,
                 (uint64_t *)&kernel_entry, NULL, (uint64_t *)&kernel_high,
                 big_endian, ELF_MACHINE, 1) < 0) {
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
            initrd_offset = (kernel_high + ~INITRD_PAGE_MASK) & INITRD_PAGE_MASK;
            if (initrd_offset + initrd_size > ram_size) {
                fprintf(stderr,
                        "qemu: memory too small for initial ram disk '%s'\n",
                        loaderparams.initrd_filename);
                exit(1);
            }
            initrd_size = load_image_targphys(loaderparams.initrd_filename,
                                              initrd_offset,
                                              ram_size - initrd_offset);
        }
        if (initrd_size == (target_ulong) -1) {
            fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                    loaderparams.initrd_filename);
            exit(1);
        }
    }

    /* Setup prom parameters. */
    prom_size = ENVP_NB_ENTRIES * (sizeof(int32_t) + ENVP_ENTRY_SIZE);
    prom_buf = g_malloc(prom_size);

    prom_set(prom_buf, prom_index++, "%s", loaderparams.kernel_filename);
    if (initrd_size > 0) {
        prom_set(prom_buf, prom_index++, "rd_start=0x%" PRIx64 " rd_size=%li %s",
                 cpu_mips_phys_to_kseg0(NULL, initrd_offset), initrd_size,
                 loaderparams.kernel_cmdline);
    } else {
        prom_set(prom_buf, prom_index++, "%s", loaderparams.kernel_cmdline);
    }

    prom_set(prom_buf, prom_index++, "memsize");
    prom_set(prom_buf, prom_index++, "%i",
             MIN(loaderparams.ram_size, 256 << 20));
    prom_set(prom_buf, prom_index++, "modetty0");
    prom_set(prom_buf, prom_index++, "38400n8r");
    prom_set(prom_buf, prom_index++, NULL);

    rom_add_blob_fixed("prom", prom_buf, prom_size,
                       cpu_mips_kseg0_to_phys(NULL, ENVP_ADDR));

    return kernel_entry;
}

static void malta_mips_config(MIPSCPU *cpu)
{
    CPUMIPSState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    env->mvp->CP0_MVPConf0 |= ((smp_cpus - 1) << CP0MVPC0_PVPE) |
                         ((smp_cpus * cs->nr_threads - 1) << CP0MVPC0_PTC);
}

static void main_cpu_reset(void *opaque)
{
    MIPSCPU *cpu = opaque;
    CPUMIPSState *env = &cpu->env;

    cpu_reset(CPU(cpu));

    /* The bootloader does not need to be rewritten as it is located in a
       read only location. The kernel location and the arguments table
       location does not change. */
    if (loaderparams.kernel_filename) {
        env->CP0_Status &= ~((1 << CP0St_BEV) | (1 << CP0St_ERL));
    }

    malta_mips_config(cpu);
}

static void cpu_request_exit(void *opaque, int irq, int level)
{
    CPUState *cpu = current_cpu;

    if (cpu && level) {
        cpu_exit(cpu);
    }
}

static
void mips_malta_init(QEMUMachineInitArgs *args)
{
    ram_addr_t ram_size = args->ram_size;
    const char *cpu_model = args->cpu_model;
    const char *kernel_filename = args->kernel_filename;
    const char *kernel_cmdline = args->kernel_cmdline;
    const char *initrd_filename = args->initrd_filename;
    char *filename;
    pflash_t *fl;
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *ram_high = g_new(MemoryRegion, 1);
    MemoryRegion *ram_low_preio = g_new(MemoryRegion, 1);
    MemoryRegion *ram_low_postio;
    MemoryRegion *bios, *bios_copy = g_new(MemoryRegion, 1);
    target_long bios_size = FLASH_SIZE;
    const size_t smbus_eeprom_size = 8 * 256;
    uint8_t *smbus_eeprom_buf = g_malloc0(smbus_eeprom_size);
    int64_t kernel_entry;
    PCIBus *pci_bus;
    ISABus *isa_bus;
    MIPSCPU *cpu;
    CPUMIPSState *env;
    qemu_irq *isa_irq;
    qemu_irq *cpu_exit_irq;
    int piix4_devfn;
    I2CBus *smbus;
    int i;
    DriveInfo *dinfo;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    DriveInfo *fd[MAX_FD];
    int fl_idx = 0;
    int fl_sectors = bios_size >> 16;
    int be;

    DeviceState *dev = qdev_create(NULL, TYPE_MIPS_MALTA);
    MaltaState *s = MIPS_MALTA(dev);

    /* The whole address space decoded by the GT-64120A doesn't generate
       exception when accessing invalid memory. Create an empty slot to
       emulate this feature. */
    empty_slot_init(0, 0x20000000);

    qdev_init_nofail(dev);

    /* Make sure the first 3 serial ports are associated with a device. */
    for(i = 0; i < 3; i++) {
        if (!serial_hds[i]) {
            char label[32];
            snprintf(label, sizeof(label), "serial%d", i);
            serial_hds[i] = qemu_chr_new(label, "null", NULL);
        }
    }

    /* init CPUs */
    if (cpu_model == NULL) {
#ifdef TARGET_MIPS64
        cpu_model = "20Kc";
#else
        cpu_model = "24Kf";
#endif
    }

    for (i = 0; i < smp_cpus; i++) {
        cpu = cpu_mips_init(cpu_model);
        if (cpu == NULL) {
            fprintf(stderr, "Unable to find CPU definition\n");
            exit(1);
        }
        env = &cpu->env;

        /* Init internal devices */
        cpu_mips_irq_init_cpu(env);
        cpu_mips_clock_init(env);
        qemu_register_reset(main_cpu_reset, cpu);
    }
    cpu = MIPS_CPU(first_cpu);
    env = &cpu->env;

    /* allocate RAM */
    if (ram_size > (2048u << 20)) {
        fprintf(stderr,
                "qemu: Too much memory for this machine: %d MB, maximum 2048 MB\n",
                ((unsigned int)ram_size / (1 << 20)));
        exit(1);
    }

    /* register RAM at high address where it is undisturbed by IO */
    memory_region_init_ram(ram_high, NULL, "mips_malta.ram", ram_size);
    vmstate_register_ram_global(ram_high);
    memory_region_add_subregion(system_memory, 0x80000000, ram_high);

    /* alias for pre IO hole access */
    memory_region_init_alias(ram_low_preio, NULL, "mips_malta_low_preio.ram",
                             ram_high, 0, MIN(ram_size, (256 << 20)));
    memory_region_add_subregion(system_memory, 0, ram_low_preio);

    /* alias for post IO hole access, if there is enough RAM */
    if (ram_size > (512 << 20)) {
        ram_low_postio = g_new(MemoryRegion, 1);
        memory_region_init_alias(ram_low_postio, NULL,
                                 "mips_malta_low_postio.ram",
                                 ram_high, 512 << 20,
                                 ram_size - (512 << 20));
        memory_region_add_subregion(system_memory, 512 << 20, ram_low_postio);
    }

    /* generate SPD EEPROM data */
    generate_eeprom_spd(&smbus_eeprom_buf[0 * 256], ram_size);
    generate_eeprom_serial(&smbus_eeprom_buf[6 * 256]);

#ifdef TARGET_WORDS_BIGENDIAN
    be = 1;
#else
    be = 0;
#endif
    /* FPGA */
    /* The CBUS UART is attached to the MIPS CPU INT2 pin, ie interrupt 4 */
    malta_fpga_init(system_memory, FPGA_ADDRESS, env->irq[4], serial_hds[2]);

    /* Load firmware in flash / BIOS. */
    dinfo = drive_get(IF_PFLASH, 0, fl_idx);
#ifdef DEBUG_BOARD_INIT
    if (dinfo) {
        printf("Register parallel flash %d size " TARGET_FMT_lx " at "
               "addr %08llx '%s' %x\n",
               fl_idx, bios_size, FLASH_ADDRESS,
               bdrv_get_device_name(dinfo->bdrv), fl_sectors);
    }
#endif
    fl = pflash_cfi01_register(FLASH_ADDRESS, NULL, "mips_malta.bios",
                               BIOS_SIZE, dinfo ? dinfo->bdrv : NULL,
                               65536, fl_sectors,
                               4, 0x0000, 0x0000, 0x0000, 0x0000, be);
    bios = pflash_cfi01_get_memory(fl);
    fl_idx++;
    if (kernel_filename) {
        /* Write a small bootloader to the flash location. */
        loaderparams.ram_size = MIN(ram_size, 256 << 20);
        loaderparams.kernel_filename = kernel_filename;
        loaderparams.kernel_cmdline = kernel_cmdline;
        loaderparams.initrd_filename = initrd_filename;
        kernel_entry = load_kernel();
        write_bootloader(env, memory_region_get_ram_ptr(bios), kernel_entry);
    } else {
        /* Load firmware from flash. */
        if (!dinfo) {
            /* Load a BIOS image. */
            if (bios_name == NULL) {
                bios_name = BIOS_FILENAME;
            }
            filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
            if (filename) {
                bios_size = load_image_targphys(filename, FLASH_ADDRESS,
                                                BIOS_SIZE);
                g_free(filename);
            } else {
                bios_size = -1;
            }
            if ((bios_size < 0 || bios_size > BIOS_SIZE) &&
                !kernel_filename && !qtest_enabled()) {
                error_report("Could not load MIPS bios '%s', and no "
                             "-kernel argument was specified", bios_name);
                exit(1);
            }
        }
        /* In little endian mode the 32bit words in the bios are swapped,
           a neat trick which allows bi-endian firmware. */
#ifndef TARGET_WORDS_BIGENDIAN
        {
            uint32_t *end, *addr = rom_ptr(FLASH_ADDRESS);
            if (!addr) {
                addr = memory_region_get_ram_ptr(bios);
            }
            end = (void *)addr + MIN(bios_size, 0x3e0000);
            while (addr < end) {
                bswap32s(addr);
                addr++;
            }
        }
#endif
    }

    /*
     * Map the BIOS at a 2nd physical location, as on the real board.
     * Copy it so that we can patch in the MIPS revision, which cannot be
     * handled by an overlapping region as the resulting ROM code subpage
     * regions are not executable.
     */
    memory_region_init_ram(bios_copy, NULL, "bios.1fc", BIOS_SIZE);
    if (!rom_copy(memory_region_get_ram_ptr(bios_copy),
                  FLASH_ADDRESS, BIOS_SIZE)) {
        memcpy(memory_region_get_ram_ptr(bios_copy),
               memory_region_get_ram_ptr(bios), BIOS_SIZE);
    }
    memory_region_set_readonly(bios_copy, true);
    memory_region_add_subregion(system_memory, RESET_ADDRESS, bios_copy);

    /* Board ID = 0x420 (Malta Board with CoreLV) */
    stl_p(memory_region_get_ram_ptr(bios_copy) + 0x10, 0x00000420);

    /* Init internal devices */
    cpu_mips_irq_init_cpu(env);
    cpu_mips_clock_init(env);

    /*
     * We have a circular dependency problem: pci_bus depends on isa_irq,
     * isa_irq is provided by i8259, i8259 depends on ISA, ISA depends
     * on piix4, and piix4 depends on pci_bus.  To stop the cycle we have
     * qemu_irq_proxy() adds an extra bit of indirection, allowing us
     * to resolve the isa_irq -> i8259 dependency after i8259 is initialized.
     */
    isa_irq = qemu_irq_proxy(&s->i8259, 16);

    /* Northbridge */
    pci_bus = gt64120_register(isa_irq);

    /* Southbridge */
    ide_drive_get(hd, MAX_IDE_BUS);

    piix4_devfn = piix4_init(pci_bus, &isa_bus, 80);

    /* Interrupt controller */
    /* The 8259 is attached to the MIPS CPU INT0 pin, ie interrupt 2 */
    s->i8259 = i8259_init(isa_bus, env->irq[2]);

    isa_bus_irqs(isa_bus, s->i8259);
    pci_piix4_ide_init(pci_bus, hd, piix4_devfn + 1);
    pci_create_simple(pci_bus, piix4_devfn + 2, "piix4-usb-uhci");
    smbus = piix4_pm_init(pci_bus, piix4_devfn + 3, 0x1100,
                          isa_get_irq(NULL, 9), NULL, 0, NULL);
    smbus_eeprom_init(smbus, 8, smbus_eeprom_buf, smbus_eeprom_size);
    g_free(smbus_eeprom_buf);
    pit = pit_init(isa_bus, 0x40, 0, NULL);
    cpu_exit_irq = qemu_allocate_irqs(cpu_request_exit, NULL, 1);
    DMA_init(0, cpu_exit_irq);

    /* Super I/O */
    isa_create_simple(isa_bus, "i8042");

    rtc_init(isa_bus, 2000, NULL);
    serial_isa_init(isa_bus, 0, serial_hds[0]);
    serial_isa_init(isa_bus, 1, serial_hds[1]);
    if (parallel_hds[0])
        parallel_init(isa_bus, 0, parallel_hds[0]);
    for(i = 0; i < MAX_FD; i++) {
        fd[i] = drive_get(IF_FLOPPY, 0, i);
    }
    fdctrl_init_isa(isa_bus, fd);

    /* Network card */
    network_init(pci_bus);

    /* Optional PCI video card */
    pci_vga_init(pci_bus);
}

static int mips_malta_sysbus_device_init(SysBusDevice *sysbusdev)
{
    return 0;
}

static void mips_malta_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = mips_malta_sysbus_device_init;
}

static const TypeInfo mips_malta_device = {
    .name          = TYPE_MIPS_MALTA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MaltaState),
    .class_init    = mips_malta_class_init,
};

static QEMUMachine mips_malta_machine = {
    .name = "malta",
    .desc = "MIPS Malta Core LV",
    .init = mips_malta_init,
    .max_cpus = 16,
    .is_default = 1,
};

static void mips_malta_register_types(void)
{
    type_register_static(&mips_malta_device);
}

static void mips_malta_machine_init(void)
{
    qemu_register_machine(&mips_malta_machine);
}

type_init(mips_malta_register_types)
machine_init(mips_malta_machine_init);
