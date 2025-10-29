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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/bitops.h"
#include "qemu/datadir.h"
#include "qemu/cutils.h"
#include "qemu/guest-random.h"
#include "exec/tswap.h"
#include "hw/clock.h"
#include "hw/southbridge/piix.h"
#include "hw/isa/superio.h"
#include "hw/char/serial-mm.h"
#include "net/net.h"
#include "hw/boards.h"
#include "hw/i2c/smbus_eeprom.h"
#include "hw/block/flash.h"
#include "hw/mips/mips.h"
#include "hw/mips/bootloader.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "qemu/log.h"
#include "hw/ide/pci.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "elf.h"
#include "qom/object.h"
#include "hw/sysbus.h"             /* SysBusDevice */
#include "qemu/host-utils.h"
#include "system/qtest.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/system.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "system/kvm.h"
#include "semihosting/semihost.h"
#include "hw/mips/cps.h"
#include "hw/qdev-clock.h"
#include "target/mips/internal.h"
#include "trace.h"
#include "cpu.h"

#define ENVP_PADDR          0x2000
#define ENVP_VADDR          cpu_mips_phys_to_kseg0(NULL, ENVP_PADDR)
#define ENVP_NB_ENTRIES     16
#define ENVP_ENTRY_SIZE     256

/* Hardware addresses */
#define FLASH_ADDRESS       0x1e000000ULL
#define FPGA_ADDRESS        0x1f000000ULL
#define RESET_ADDRESS       0x1fc00000ULL

#define FLASH_SIZE          0x400000
#define BIOS_SIZE           (4 * MiB)

#define PIIX4_PCI_DEVFN     PCI_DEVFN(10, 0)

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
    CharFrontend display;
    char display_text[9];
    SerialMM *uart;
    bool display_inited;
} MaltaFPGAState;

#define TYPE_MIPS_MALTA "mips-malta"
OBJECT_DECLARE_SIMPLE_TYPE(MaltaState, MIPS_MALTA)

struct MaltaState {
    SysBusDevice parent_obj;

    Clock *cpuclk;
    MIPSCPSState cps;
};

static struct _loaderparams {
    int ram_size, ram_low_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
} loaderparams;

/* Malta FPGA */
static void malta_fpga_update_display_leds(MaltaFPGAState *s)
{
    char leds_text[9];
    int i;

    for (i = 7 ; i >= 0 ; i--) {
        if (s->leds & (1 << i)) {
            leds_text[i] = '#';
        } else {
            leds_text[i] = ' ';
        }
    }
    leds_text[8] = '\0';

    trace_malta_fpga_leds(leds_text);
    qemu_chr_fe_printf(&s->display, "\e[H\n\n|\e[32m%-8.8s\e[00m|\r\n",
                       leds_text);
}

static void malta_fpga_update_display_ascii(MaltaFPGAState *s)
{
    trace_malta_fpga_display(s->display_text);
    qemu_chr_fe_printf(&s->display, "\n\n\n\n|\e[31m%-8.8s\e[00m|",
                       s->display_text);
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

#if defined(DEBUG)
#  define logout(fmt, ...) \
          fprintf(stderr, "MALTA\t%-24s" fmt, __func__, ## __VA_ARGS__)
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
  /* uint16_t size; */
  uint8_t contents[256];
};

typedef struct _eeprom24c0x_t eeprom24c0x_t;

static eeprom24c0x_t spd_eeprom = {
    .contents = {
        /* 00000000: */
        0x80, 0x08, 0xFF, 0x0D, 0x0A, 0xFF, 0x40, 0x00,
        /* 00000008: */
        0x01, 0x75, 0x54, 0x00, 0x82, 0x08, 0x00, 0x01,
        /* 00000010: */
        0x8F, 0x04, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00,
        /* 00000018: */
        0x00, 0x00, 0x00, 0x14, 0x0F, 0x14, 0x2D, 0xFF,
        /* 00000020: */
        0x15, 0x08, 0x15, 0x08, 0x00, 0x00, 0x00, 0x00,
        /* 00000028: */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* 00000030: */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* 00000038: */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0xD0,
        /* 00000040: */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* 00000048: */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* 00000050: */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* 00000058: */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* 00000060: */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* 00000068: */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* 00000070: */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* 00000078: */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0xF4,
    },
};

static void generate_eeprom_spd(uint8_t *eeprom, ram_addr_t ram_size)
{
    enum sdram_type type;
    uint8_t *spd = spd_eeprom.contents;
    uint8_t nbanks = 0;
    uint16_t density = 0;
    int i;

    /* work in terms of MB */
    ram_size /= MiB;

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
        warn_report("SPD cannot represent final " RAM_ADDR_FMT "MB"
                    " of SDRAM", ram_size);
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
        val = 0x00000000;
        break;

    /* STATUS Register */
    case 0x00208:
        val = TARGET_BIG_ENDIAN ? 0x00000012 : 0x00000010;
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
        if (s->i2csel) {
            val = s->i2cout;
        } else {
            val = 0x00;
        }
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
        qemu_log_mask(LOG_GUEST_ERROR,
                      "malta_fpga_read: Bad register addr 0x%"HWADDR_PRIX"\n",
                      addr);
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
        malta_fpga_update_display_leds(s);
        break;

    /* ASCIIWORD Register */
    case 0x00410:
        snprintf(s->display_text, 9, "%08X", (uint32_t)val);
        malta_fpga_update_display_ascii(s);
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
        malta_fpga_update_display_ascii(s);
        break;

    /* SOFTRES Register */
    case 0x00500:
        if (val == 0x42) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
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
        qemu_log_mask(LOG_GUEST_ERROR,
                      "malta_fpga_write: Bad register addr 0x%"HWADDR_PRIX"\n",
                      addr);
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

static void malta_fgpa_display_event(void *opaque, QEMUChrEvent event)
{
    MaltaFPGAState *s = opaque;

    if (event == CHR_EVENT_OPENED && !s->display_inited) {
        qemu_chr_fe_printf(&s->display, "\e[HMalta LEDBAR\r\n");
        qemu_chr_fe_printf(&s->display, "+--------+\r\n");
        qemu_chr_fe_printf(&s->display, "+        +\r\n");
        qemu_chr_fe_printf(&s->display, "+--------+\r\n");
        qemu_chr_fe_printf(&s->display, "\n");
        qemu_chr_fe_printf(&s->display, "Malta ASCII\r\n");
        qemu_chr_fe_printf(&s->display, "+--------+\r\n");
        qemu_chr_fe_printf(&s->display, "+        +\r\n");
        qemu_chr_fe_printf(&s->display, "+--------+\r\n");
        s->display_inited = true;
    }
}

static MaltaFPGAState *malta_fpga_init(MemoryRegion *address_space,
         hwaddr base, qemu_irq uart_irq, Chardev *uart_chr)
{
    MaltaFPGAState *s;
    Chardev *chr;

    s = g_new0(MaltaFPGAState, 1);

    memory_region_init_io(&s->iomem, NULL, &malta_fpga_ops, s,
                          "malta-fpga", 0x100000);
    memory_region_init_alias(&s->iomem_lo, NULL, "malta-fpga",
                             &s->iomem, 0, 0x900);
    memory_region_init_alias(&s->iomem_hi, NULL, "malta-fpga",
                             &s->iomem, 0xa00, 0x100000 - 0xa00);

    memory_region_add_subregion(address_space, base, &s->iomem_lo);
    memory_region_add_subregion(address_space, base + 0xa00, &s->iomem_hi);

    chr = qemu_chr_new("fpga", "vc:320x200", NULL);
    qemu_chr_fe_init(&s->display, chr, NULL);
    qemu_chr_fe_set_handlers(&s->display, NULL, NULL,
                             malta_fgpa_display_event, NULL, s, NULL, true);

    s->uart = serial_mm_init(address_space, base + 0x900, 3, uart_irq,
                             230400, uart_chr, DEVICE_NATIVE_ENDIAN);

    malta_fpga_reset(s);
    qemu_register_reset(malta_fpga_reset, s);

    return s;
}

/* Network support */
static void network_init(PCIBus *pci_bus)
{
    /* The malta board has a PCNet card using PCI SLOT 11 */
    pci_init_nic_in_slot(pci_bus, "pcnet", NULL, "0b");
    pci_init_nic_devices(pci_bus, "pcnet");
}

static void bl_setup_gt64120_jump_kernel(void **p, uint64_t run_addr,
                                         uint64_t kernel_entry)
{
    static const char pci_pins_cfg[PCI_NUM_PINS] = {
        10, 10, 11, 11 /* PIIX IRQRC[A:D] */
    };

    /* Bus endianness is always reversed */
#if TARGET_BIG_ENDIAN
#define cpu_to_gt32(x) (x)
#else
#define cpu_to_gt32(x) bswap32(x)
#endif

    /* setup MEM-to-PCI0 mapping as done by YAMON */

    /* move GT64120 registers from 0x14000000 to 0x1be00000 */
    bl_gen_write_u32(p, /* GT_ISD */
                     cpu_mips_phys_to_kseg1(NULL, 0x14000000 + 0x68),
                     cpu_to_gt32(0x1be00000 << 3));

    /* setup PCI0 io window to 0x18000000-0x181fffff */
    bl_gen_write_u32(p, /* GT_PCI0IOLD */
                     cpu_mips_phys_to_kseg1(NULL, 0x1be00000 + 0x48),
                     cpu_to_gt32(0x18000000 << 3));
    bl_gen_write_u32(p, /* GT_PCI0IOHD */
                     cpu_mips_phys_to_kseg1(NULL, 0x1be00000 + 0x50),
                     cpu_to_gt32(0x08000000 << 3));

    /* setup PCI0 mem windows */
    bl_gen_write_u32(p, /* GT_PCI0M0LD */
                     cpu_mips_phys_to_kseg1(NULL, 0x1be00000 + 0x58),
                     cpu_to_gt32(0x10000000 << 3));
    bl_gen_write_u32(p, /* GT_PCI0M0HD */
                     cpu_mips_phys_to_kseg1(NULL, 0x1be00000 + 0x60),
                     cpu_to_gt32(0x07e00000 << 3));
    bl_gen_write_u32(p, /* GT_PCI0M1LD */
                     cpu_mips_phys_to_kseg1(NULL, 0x1be00000 + 0x80),
                     cpu_to_gt32(0x18200000 << 3));
    bl_gen_write_u32(p, /* GT_PCI0M1HD */
                     cpu_mips_phys_to_kseg1(NULL, 0x1be00000 + 0x88),
                     cpu_to_gt32(0x0bc00000 << 3));

#undef cpu_to_gt32

    /*
     * The PIIX ISA bridge is on PCI bus 0 dev 10 func 0.
     * Load the PIIX IRQC[A:D] routing config address, then
     * write routing configuration to the config data register.
     */
    bl_gen_write_u32(p, /* GT_PCI0_CFGADDR */
                     cpu_mips_phys_to_kseg1(NULL, 0x1be00000 + 0xcf8),
                     tswap32((1 << 31) /* ConfigEn */
                             | PCI_BUILD_BDF(0, PIIX4_PCI_DEVFN) << 8
                             | PIIX_PIRQCA));
    bl_gen_write_u32(p, /* GT_PCI0_CFGDATA */
                     cpu_mips_phys_to_kseg1(NULL, 0x1be00000 + 0xcfc),
                     tswap32(ldl_be_p(pci_pins_cfg)));

    bl_gen_jump_kernel(p,
                       true, ENVP_VADDR - 64,
                       /*
                        * If semihosting is used, arguments have already
                        * been passed, so we preserve $a0.
                        */
                       !semihosting_get_argc(), 2,
                       true, ENVP_VADDR,
                       true, ENVP_VADDR + 8,
                       true, loaderparams.ram_low_size,
                       kernel_entry);
}

static void write_bootloader_nanomips(uint8_t *base, uint64_t run_addr,
                                      uint64_t kernel_entry)
{
    uint16_t *p;

    /* Small bootloader */
    p = (uint16_t *)base;

    stw_p(p++, 0x2800); stw_p(p++, 0x001c);
                                /* bc to_here */
    stw_p(p++, 0x8000); stw_p(p++, 0xc000);
                                /* nop */
    stw_p(p++, 0x8000); stw_p(p++, 0xc000);
                                /* nop */
    stw_p(p++, 0x8000); stw_p(p++, 0xc000);
                                /* nop */
    stw_p(p++, 0x8000); stw_p(p++, 0xc000);
                                /* nop */
    stw_p(p++, 0x8000); stw_p(p++, 0xc000);
                                /* nop */
    stw_p(p++, 0x8000); stw_p(p++, 0xc000);
                                /* nop */
    stw_p(p++, 0x8000); stw_p(p++, 0xc000);
                                /* nop */

    /* to_here: */

    bl_setup_gt64120_jump_kernel((void **)&p, run_addr, kernel_entry);
}

/*
 * ROM and pseudo bootloader
 *
 * The following code implements a very very simple bootloader. It first
 * loads the registers a0 to a3 to the values expected by the OS, and
 * then jump at the kernel address.
 *
 * The bootloader should pass the locations of the kernel arguments and
 * environment variables tables. Those tables contain the 32-bit address
 * of NULL terminated strings. The environment variables table should be
 * terminated by a NULL address.
 *
 * For a simpler implementation, the number of kernel arguments is fixed
 * to two (the name of the kernel and the command line), and the two
 * tables are actually the same one.
 *
 * The registers a0 to a3 should contain the following values:
 *   a0 - number of kernel arguments
 *   a1 - 32-bit address of the kernel arguments table
 *   a2 - 32-bit address of the environment variables table
 *   a3 - RAM size in bytes
 */
static void write_bootloader(uint8_t *base, uint64_t run_addr,
                             uint64_t kernel_entry)
{
    uint32_t *p;

    /* Small bootloader */
    p = (uint32_t *)base;

    stl_p(p++, 0x08000000 |                  /* j 0x1fc00580 */
                 ((run_addr + 0x580) & 0x0fffffff) >> 2);
    stl_p(p++, 0x00000000);                  /* nop */

    /* YAMON service vector */
    stl_p(base + 0x500, run_addr + 0x0580);  /* start: */
    stl_p(base + 0x504, run_addr + 0x083c);  /* print_count: */
    stl_p(base + 0x520, run_addr + 0x0580);  /* start: */
    stl_p(base + 0x52c, run_addr + 0x0800);  /* flush_cache: */
    stl_p(base + 0x534, run_addr + 0x0808);  /* print: */
    stl_p(base + 0x538, run_addr + 0x0800);  /* reg_cpu_isr: */
    stl_p(base + 0x53c, run_addr + 0x0800);  /* unred_cpu_isr: */
    stl_p(base + 0x540, run_addr + 0x0800);  /* reg_ic_isr: */
    stl_p(base + 0x544, run_addr + 0x0800);  /* unred_ic_isr: */
    stl_p(base + 0x548, run_addr + 0x0800);  /* reg_esr: */
    stl_p(base + 0x54c, run_addr + 0x0800);  /* unreg_esr: */
    stl_p(base + 0x550, run_addr + 0x0800);  /* getchar: */
    stl_p(base + 0x554, run_addr + 0x0800);  /* syscon_read: */


    /* Second part of the bootloader */
    p = (uint32_t *) (base + 0x580);

    /*
     * Load BAR registers as done by YAMON:
     *
     *  - set up PCI0 I/O BARs from 0x18000000 to 0x181fffff
     *  - set up PCI0 MEM0 at 0x10000000, size 0x7e00000
     *  - set up PCI0 MEM1 at 0x18200000, size 0xbc00000
     *
     */

    bl_setup_gt64120_jump_kernel((void **)&p, run_addr, kernel_entry);

    /* YAMON subroutines */
    p = (uint32_t *) (base + 0x800);
    stl_p(p++, 0x03e00009);                  /* jalr ra */
    stl_p(p++, 0x24020000);                  /* li v0,0 */
    /* 808 YAMON print */
    stl_p(p++, 0x03e06821);                  /* move t5,ra */
    stl_p(p++, 0x00805821);                  /* move t3,a0 */
    stl_p(p++, 0x00a05021);                  /* move t2,a1 */
    stl_p(p++, 0x91440000);                  /* lbu a0,0(t2) */
    stl_p(p++, 0x254a0001);                  /* addiu t2,t2,1 */
    stl_p(p++, 0x10800005);                  /* beqz a0,834 */
    stl_p(p++, 0x00000000);                  /* nop */
    stl_p(p++, 0x0ff0021c);                  /* jal 870 */
    stl_p(p++, 0x00000000);                  /* nop */
    stl_p(p++, 0x1000fff9);                  /* b 814 */
    stl_p(p++, 0x00000000);                  /* nop */
    stl_p(p++, 0x01a00009);                  /* jalr t5 */
    stl_p(p++, 0x01602021);                  /* move a0,t3 */
    /* 0x83c YAMON print_count */
    stl_p(p++, 0x03e06821);                  /* move t5,ra */
    stl_p(p++, 0x00805821);                  /* move t3,a0 */
    stl_p(p++, 0x00a05021);                  /* move t2,a1 */
    stl_p(p++, 0x00c06021);                  /* move t4,a2 */
    stl_p(p++, 0x91440000);                  /* lbu a0,0(t2) */
    stl_p(p++, 0x0ff0021c);                  /* jal 870 */
    stl_p(p++, 0x00000000);                  /* nop */
    stl_p(p++, 0x254a0001);                  /* addiu t2,t2,1 */
    stl_p(p++, 0x258cffff);                  /* addiu t4,t4,-1 */
    stl_p(p++, 0x1580fffa);                  /* bnez t4,84c */
    stl_p(p++, 0x00000000);                  /* nop */
    stl_p(p++, 0x01a00009);                  /* jalr t5 */
    stl_p(p++, 0x01602021);                  /* move a0,t3 */
    /* 0x870 */
    stl_p(p++, 0x3c08b800);                  /* lui t0,0xb400 */
    stl_p(p++, 0x350803f8);                  /* ori t0,t0,0x3f8 */
    stl_p(p++, 0x91090005);                  /* lbu t1,5(t0) */
    stl_p(p++, 0x00000000);                  /* nop */
    stl_p(p++, 0x31290040);                  /* andi t1,t1,0x40 */
    stl_p(p++, 0x1120fffc);                  /* beqz t1,878 <outch+0x8> */
    stl_p(p++, 0x00000000);                  /* nop */
    stl_p(p++, 0x03e00009);                  /* jalr ra */
    stl_p(p++, 0xa1040000);                  /* sb a0,0(t0) */
}

static void G_GNUC_PRINTF(3, 4) prom_set(uint32_t *prom_buf, int index,
                                        const char *string, ...)
{
    va_list ap;
    uint32_t table_addr;

    if (index >= ENVP_NB_ENTRIES) {
        return;
    }

    if (string == NULL) {
        prom_buf[index] = 0;
        return;
    }

    table_addr = sizeof(uint32_t) * ENVP_NB_ENTRIES + index * ENVP_ENTRY_SIZE;
    prom_buf[index] = tswap32(ENVP_VADDR + table_addr);

    va_start(ap, string);
    vsnprintf((char *)prom_buf + table_addr, ENVP_ENTRY_SIZE, string, ap);
    va_end(ap);
}

static GString *rng_seed_hex_new(void)
{
    uint8_t rng_seed[32];

    qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
    return qemu_hexdump_line(NULL, rng_seed, sizeof(rng_seed), 0, 0);
}

static void reinitialize_rng_seed(void *opaque)
{
    g_autoptr(GString) hex = rng_seed_hex_new();
    memcpy(opaque, hex->str, hex->len);
}

/* Kernel */
static uint64_t load_kernel(void)
{
    uint64_t kernel_entry, kernel_high, initrd_size;
    long kernel_size;
    ram_addr_t initrd_offset;
    uint32_t *prom_buf;
    long prom_size;
    int prom_index = 0;
    size_t rng_seed_prom_offset;

    kernel_size = load_elf(loaderparams.kernel_filename, NULL,
                           cpu_mips_kseg0_to_phys, NULL,
                           &kernel_entry, NULL,
                           &kernel_high, NULL,
                           TARGET_BIG_ENDIAN ? ELFDATA2MSB : ELFDATA2LSB,
                           EM_MIPS, 1, 0);
    if (kernel_size < 0) {
        error_report("could not load kernel '%s': %s",
                     loaderparams.kernel_filename,
                     load_elf_strerror(kernel_size));
        exit(1);
    }

    /* Check where the kernel has been linked */
    if (kernel_entry <= USEG_LIMIT) {
        error_report("Trap-and-Emul kernels (Linux CONFIG_KVM_GUEST)"
                     " are not supported");
        exit(1);
    }

    /* load initrd */
    initrd_size = 0;
    initrd_offset = 0;
    if (loaderparams.initrd_filename) {
        initrd_size = get_image_size(loaderparams.initrd_filename, NULL);
        if (initrd_size > 0) {
            /*
             * The kernel allocates the bootmap memory in the low memory after
             * the initrd.  It takes at most 128kiB for 2GB RAM and 4kiB
             * pages.
             */
            initrd_offset = ROUND_UP(loaderparams.ram_low_size
                                     - (initrd_size + 128 * KiB),
                                     INITRD_PAGE_SIZE);
            if (kernel_high >= initrd_offset) {
                error_report("memory too small for initial ram disk '%s'",
                             loaderparams.initrd_filename);
                exit(1);
            }
            initrd_size = load_image_targphys(loaderparams.initrd_filename,
                                        initrd_offset,
                                        loaderparams.ram_size - initrd_offset,
                                        NULL);
        }
        if (initrd_size == (target_ulong) -1) {
            error_report("could not load initial ram disk '%s'",
                         loaderparams.initrd_filename);
            exit(1);
        }
    }

    /* Setup prom parameters. */
    prom_size = ENVP_NB_ENTRIES * (sizeof(int32_t) + ENVP_ENTRY_SIZE);
    prom_buf = g_malloc(prom_size);

    prom_set(prom_buf, prom_index++, "%s", loaderparams.kernel_filename);
    if (initrd_size > 0) {
        prom_set(prom_buf, prom_index++,
                 "rd_start=0x%" PRIx64 " rd_size=%" PRId64 " %s",
                 cpu_mips_phys_to_kseg0(NULL, initrd_offset),
                 initrd_size, loaderparams.kernel_cmdline);
    } else {
        prom_set(prom_buf, prom_index++, "%s", loaderparams.kernel_cmdline);
    }

    prom_set(prom_buf, prom_index++, "memsize");
    prom_set(prom_buf, prom_index++, "%u", loaderparams.ram_low_size);

    prom_set(prom_buf, prom_index++, "ememsize");
    prom_set(prom_buf, prom_index++, "%u", loaderparams.ram_size);

    prom_set(prom_buf, prom_index++, "modetty0");
    prom_set(prom_buf, prom_index++, "38400n8r");

    prom_set(prom_buf, prom_index++, "rngseed");
    rng_seed_prom_offset = prom_index * ENVP_ENTRY_SIZE +
                           sizeof(uint32_t) * ENVP_NB_ENTRIES;
    {
        g_autoptr(GString) hex = rng_seed_hex_new();
        prom_set(prom_buf, prom_index++, "%s", hex->str);
    }

    prom_set(prom_buf, prom_index++, NULL);

    rom_add_blob_fixed("prom", prom_buf, prom_size, ENVP_PADDR);
    qemu_register_reset_nosnapshotload(reinitialize_rng_seed,
            rom_ptr(ENVP_PADDR, prom_size) + rng_seed_prom_offset);

    g_free(prom_buf);
    return kernel_entry;
}

static void malta_mips_config(MIPSCPU *cpu)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int smp_cpus = ms->smp.cpus;
    CPUMIPSState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    if (ase_mt_available(env)) {
        env->mvp->CP0_MVPConf0 = deposit32(env->mvp->CP0_MVPConf0,
                                           CP0MVPC0_PTC, 8,
                                           smp_cpus * cs->nr_threads - 1);
        env->mvp->CP0_MVPConf0 = deposit32(env->mvp->CP0_MVPConf0,
                                           CP0MVPC0_PVPE, 4, smp_cpus - 1);
    }
}

static int malta_pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num)
{
    int slot;

    slot = PCI_SLOT(pci_dev->devfn);

    switch (slot) {
    /* PIIX4 USB */
    case 10:
        return 3;
    /* AMD 79C973 Ethernet */
    case 11:
        return 1;
    /* Crystal 4281 Sound */
    case 12:
        return 2;
    /* PCI slot 1 to 4 */
    case 18 ... 21:
        return ((slot - 18) + irq_num) & 0x03;
    /* Unknown device, don't do any translation */
    default:
        return irq_num;
    }
}

static void main_cpu_reset(void *opaque)
{
    MIPSCPU *cpu = opaque;
    CPUMIPSState *env = &cpu->env;

    cpu_reset(CPU(cpu));

    /*
     * The bootloader does not need to be rewritten as it is located in a
     * read only location. The kernel location and the arguments table
     * location does not change.
     */
    if (loaderparams.kernel_filename) {
        env->CP0_Status &= ~(1 << CP0St_ERL);
    }

    malta_mips_config(cpu);
}

static void create_cpu_without_cps(MachineState *ms, MaltaState *s,
                                   qemu_irq *cbus_irq, qemu_irq *i8259_irq)
{
    CPUMIPSState *env;
    MIPSCPU *cpu;
    int i;

    for (i = 0; i < ms->smp.cpus; i++) {
        cpu = mips_cpu_create_with_clock(ms->cpu_type, s->cpuclk,
                                         TARGET_BIG_ENDIAN);

        /* Init internal devices */
        cpu_mips_irq_init_cpu(cpu);
        cpu_mips_clock_init(cpu);
        qemu_register_reset(main_cpu_reset, cpu);
    }

    cpu = MIPS_CPU(first_cpu);
    env = &cpu->env;
    *i8259_irq = env->irq[2];
    *cbus_irq = env->irq[4];
}

static void create_cps(MachineState *ms, MaltaState *s,
                       qemu_irq *cbus_irq, qemu_irq *i8259_irq)
{
    object_initialize_child(OBJECT(s), "cps", &s->cps, TYPE_MIPS_CPS);
    object_property_set_str(OBJECT(&s->cps), "cpu-type", ms->cpu_type,
                            &error_fatal);
    object_property_set_bool(OBJECT(&s->cps), "cpu-big-endian",
                             TARGET_BIG_ENDIAN, &error_abort);
    object_property_set_uint(OBJECT(&s->cps), "num-vp", ms->smp.cpus,
                            &error_fatal);
    qdev_connect_clock_in(DEVICE(&s->cps), "clk-in", s->cpuclk);
    sysbus_realize(SYS_BUS_DEVICE(&s->cps), &error_fatal);

    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->cps), 0, 0, 1);

    *i8259_irq = get_cps_irq(&s->cps, 3);
    *cbus_irq = NULL;
}

static void mips_create_cpu(MachineState *ms, MaltaState *s,
                            qemu_irq *cbus_irq, qemu_irq *i8259_irq)
{
    if ((ms->smp.cpus > 1) && cpu_type_supports_cps_smp(ms->cpu_type)) {
        create_cps(ms, s, cbus_irq, i8259_irq);
    } else {
        create_cpu_without_cps(ms, s, cbus_irq, i8259_irq);
    }
}

static
void mips_malta_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    ram_addr_t ram_low_size;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    char *filename;
    PFlashCFI01 *fl;
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *ram_low_preio = g_new(MemoryRegion, 1);
    MemoryRegion *ram_low_postio;
    MemoryRegion *bios, *bios_copy = g_new(MemoryRegion, 1);
    const size_t smbus_eeprom_size = 8 * 256;
    uint8_t *smbus_eeprom_buf = g_malloc0(smbus_eeprom_size);
    uint64_t kernel_entry, bootloader_run_addr;
    PCIBus *pci_bus;
    ISABus *isa_bus;
    qemu_irq cbus_irq, i8259_irq;
    I2CBus *smbus;
    DriveInfo *dinfo;
    int fl_idx = 0;
    MaltaState *s;
    PCIDevice *piix4;
    DeviceState *dev;

    s = MIPS_MALTA(qdev_new(TYPE_MIPS_MALTA));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);

    /* create CPU */
    mips_create_cpu(machine, s, &cbus_irq, &i8259_irq);

    /* allocate RAM */
    if (ram_size > 2 * GiB) {
        error_report("Too much memory for this machine: %" PRId64 "MB,"
                     " maximum 2048MB", ram_size / MiB);
        exit(1);
    }

    /* register RAM at high address where it is undisturbed by IO */
    memory_region_add_subregion(system_memory, 0x80000000, machine->ram);

    /* alias for pre IO hole access */
    memory_region_init_alias(ram_low_preio, NULL, "mips_malta_low_preio.ram",
                             machine->ram, 0, MIN(ram_size, 256 * MiB));
    memory_region_add_subregion(system_memory, 0, ram_low_preio);

    /* alias for post IO hole access, if there is enough RAM */
    if (ram_size > 512 * MiB) {
        ram_low_postio = g_new(MemoryRegion, 1);
        memory_region_init_alias(ram_low_postio, NULL,
                                 "mips_malta_low_postio.ram",
                                 machine->ram, 512 * MiB,
                                 ram_size - 512 * MiB);
        memory_region_add_subregion(system_memory, 512 * MiB,
                                    ram_low_postio);
    }

    /* FPGA */

    /* The CBUS UART is attached to the MIPS CPU INT2 pin, ie interrupt 4 */
    malta_fpga_init(system_memory, FPGA_ADDRESS, cbus_irq, serial_hd(2));

    /* Load firmware in flash / BIOS. */
    dinfo = drive_get(IF_PFLASH, 0, fl_idx);
    fl = pflash_cfi01_register(FLASH_ADDRESS, "mips_malta.bios",
                               FLASH_SIZE,
                               dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                               65536,
                               4, 0x0000, 0x0000, 0x0000, 0x0000,
                               TARGET_BIG_ENDIAN);
    bios = pflash_cfi01_get_memory(fl);
    fl_idx++;
    if (kernel_filename) {
        ram_low_size = MIN(ram_size, 256 * MiB);
        bootloader_run_addr = cpu_mips_phys_to_kseg0(NULL, RESET_ADDRESS);

        /* Write a small bootloader to the flash location. */
        loaderparams.ram_size = ram_size;
        loaderparams.ram_low_size = ram_low_size;
        loaderparams.kernel_filename = kernel_filename;
        loaderparams.kernel_cmdline = kernel_cmdline;
        loaderparams.initrd_filename = initrd_filename;
        kernel_entry = load_kernel();

        if (!cpu_type_supports_isa(machine->cpu_type, ISA_NANOMIPS32)) {
            write_bootloader(memory_region_get_ram_ptr(bios),
                             bootloader_run_addr, kernel_entry);
        } else {
            write_bootloader_nanomips(memory_region_get_ram_ptr(bios),
                                      bootloader_run_addr, kernel_entry);
        }
    } else {
        target_long bios_size = FLASH_SIZE;
        /* Load firmware from flash. */
        if (!dinfo) {
            const char *bios_name = TARGET_BIG_ENDIAN ? "mips_bios.bin"
                                                        : "mipsel_bios.bin";

            /* Load a BIOS image. */
            filename = qemu_find_file(QEMU_FILE_TYPE_BIOS,
                                      machine->firmware ?: bios_name);
            if (filename) {
                bios_size = load_image_targphys(filename, FLASH_ADDRESS,
                                                BIOS_SIZE, NULL);
                g_free(filename);
            } else {
                bios_size = -1;
            }
            if ((bios_size < 0 || bios_size > BIOS_SIZE) &&
                machine->firmware && !qtest_enabled()) {
                error_report("Could not load MIPS bios '%s'", machine->firmware);
                exit(1);
            }
        }
        /*
         * In little endian mode the 32bit words in the bios are swapped,
         * a neat trick which allows bi-endian firmware.
         */
        if (!TARGET_BIG_ENDIAN && bios_size > 0) {
            uint32_t *end, *addr;
            const size_t swapsize = MIN(bios_size, 0x3e0000);
            addr = rom_ptr(FLASH_ADDRESS, swapsize);
            if (!addr) {
                addr = memory_region_get_ram_ptr(bios);
            }
            end = (void *)addr + swapsize;
            while (addr < end) {
                bswap32s(addr);
                addr++;
            }
        }
    }

    /*
     * Map the BIOS at a 2nd physical location, as on the real board.
     * Copy it so that we can patch in the MIPS revision, which cannot be
     * handled by an overlapping region as the resulting ROM code subpage
     * regions are not executable.
     */
    memory_region_init_ram(bios_copy, NULL, "bios.1fc", BIOS_SIZE,
                           &error_fatal);
    if (!rom_copy(memory_region_get_ram_ptr(bios_copy),
                  FLASH_ADDRESS, BIOS_SIZE)) {
        memcpy(memory_region_get_ram_ptr(bios_copy),
               memory_region_get_ram_ptr(bios), BIOS_SIZE);
    }
    memory_region_set_readonly(bios_copy, true);
    memory_region_add_subregion(system_memory, RESET_ADDRESS, bios_copy);

    /* Board ID = 0x420 (Malta Board with CoreLV) */
    stl_p(memory_region_get_ram_ptr(bios_copy) + 0x10, 0x00000420);

    /* Northbridge */
    dev = qdev_new("gt64120");
    qdev_prop_set_bit(dev, "cpu-little-endian", !TARGET_BIG_ENDIAN);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    pci_bus = PCI_BUS(qdev_get_child_bus(dev, "pci"));
    pci_bus_map_irqs(pci_bus, malta_pci_slot_get_pirq);

    /* Southbridge */
    piix4 = pci_new_multifunction(PIIX4_PCI_DEVFN, TYPE_PIIX4_PCI_DEVICE);
    qdev_prop_set_uint32(DEVICE(piix4), "smb_io_base", 0x1100);
    pci_realize_and_unref(piix4, pci_bus, &error_fatal);
    isa_bus = ISA_BUS(qdev_get_child_bus(DEVICE(piix4), "isa.0"));

    dev = DEVICE(object_resolve_path_component(OBJECT(piix4), "ide"));
    pci_ide_create_devs(PCI_DEVICE(dev));

    /* Interrupt controller */
    qdev_connect_gpio_out_named(DEVICE(piix4), "intr", 0, i8259_irq);

    /* generate SPD EEPROM data */
    dev = DEVICE(object_resolve_path_component(OBJECT(piix4), "pm"));
    smbus = I2C_BUS(qdev_get_child_bus(dev, "i2c"));
    generate_eeprom_spd(&smbus_eeprom_buf[0 * 256], ram_size);
    generate_eeprom_serial(&smbus_eeprom_buf[6 * 256]);
    smbus_eeprom_init(smbus, 8, smbus_eeprom_buf, smbus_eeprom_size);
    g_free(smbus_eeprom_buf);

    /* Super I/O: SMS FDC37M817 */
    isa_create_simple(isa_bus, TYPE_FDC37M81X_SUPERIO);

    /* Network card */
    network_init(pci_bus);

    /* Optional PCI video card */
    pci_vga_init(pci_bus);
}

static void mips_malta_instance_init(Object *obj)
{
    MaltaState *s = MIPS_MALTA(obj);

    s->cpuclk = qdev_init_clock_out(DEVICE(obj), "cpu-refclk");
    clock_set_hz(s->cpuclk, 320000000); /* 320 MHz */
}

static const TypeInfo mips_malta_device = {
    .name          = TYPE_MIPS_MALTA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MaltaState),
    .instance_init = mips_malta_instance_init,
};

GlobalProperty malta_compat[] = {
    { "PIIX4_PM", "memory-hotplug-support", "off" },
    { "PIIX4_PM", "acpi-pci-hotplug-with-bridge-support", "off" },
    { "PIIX4_PM", "acpi-root-pci-hotplug", "off" },
    { "PIIX4_PM", "x-not-migrate-acpi-index", "true" },
};
const size_t malta_compat_len = G_N_ELEMENTS(malta_compat);

static void mips_malta_machine_init(MachineClass *mc)
{
    mc->desc = "MIPS Malta Core LV";
    mc->init = mips_malta_init;
    mc->block_default_type = IF_IDE;
    mc->max_cpus = 16;
    mc->is_default = true;
#ifdef TARGET_MIPS64
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("20Kc");
#else
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("24Kf");
#endif
    mc->default_ram_id = "mips_malta.ram";
    compat_props_add(mc->compat_props, malta_compat, malta_compat_len);
}

DEFINE_MACHINE("malta", mips_malta_machine_init)

static void mips_malta_register_types(void)
{
    type_register_static(&mips_malta_device);
}

type_init(mips_malta_register_types)
