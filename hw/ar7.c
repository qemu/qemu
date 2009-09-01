/*
 * QEMU AR7 support
 *
 * Copyright (C) 2006-2008 Stefan Weil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* This code emulates specific parts of Texas Instruments AR7 SoC family.
 * AR7 contains a MIPS 4KEc core and on-chip peripherals (avalanche).
 *
 * These members of the AR7 family are partially supported:
 * - TNETD7100 (not supported)
 * - TNETD7200 (just started, very incomplete)
 * - TNETD7300 (best emulation)
 *
 * TODO:
 * - ldl_phys, stl_phys wrong for big endian AR7
 * - TNETD7100 emulation is missing
 * - TNETD7200 emulation is very incomplete
 * - reboot loops endless reading device config latch (AVALANCHE_DCL_BASE)
 * - uart0, uart1 wrong type (is 16450, should be 16550). Fixed in latest QEMU?
 * - vlynq emulation only very rudimentary
 * - Ethernet not stable. Linux kernel problem? Fixed by latest OpenWrt?
 * - much more
 * - ar7.cpu_env is not needed
 * - Sinus 154 DSL Basic SE raises assertion in pflash_cfi01.c
 *
 * Interrupts:
 *                 CPU0
 *        2:         64            MIPS  AR7 on hw0
 *        7:       1686            MIPS  timer
 *       15:         64             AR7  serial
 *       16:          0             AR7  serial
 *       27:          0             AR7  Cpmac Driver
 *       41:          0             AR7  Cpmac Driver
 *
 *      ERR:          0
 *
 */

#include <assert.h>
#include <stddef.h>             /* offsetof */
#include <stdio.h>              /* fprintf */

#include <zlib.h>               /* crc32 */

#include "hw.h"
#include "boards.h"
#include "mips.h"
#include "net.h"
#include "pci.h"

#include "sysemu.h"             /* serial_hds */
#include "qemu-char.h"          /* qemu_chr_printf */
#include "qemu-timer.h"         /* vm_clock */

#include "block.h"              /* bdrv_getlength */
#include "console.h"            /* console_select */
#include "disas.h"              /* lookup_symbol */
#include "exec-all.h"           /* logfile */

#include "hw/pc.h"              /* serial_16550_init, ... */
#include "hw/pflash.h"          /* pflash_device_register, ... */
#include "hw/tnetw1130.h"       /* vlynq_tnetw1130_init */

#include "target-mips/cpu.h"    /* do_interrupt */

#define MIPS_EXCEPTION_OFFSET   8
#define NUM_PRIMARY_IRQS        40
#define NUM_SECONDARY_IRQS      32

#define AR7_PRIMARY_IRQ(num)    ar7.primary_irq[(num) - MIPS_EXCEPTION_OFFSET]

/* physical address of flash memory */
#define FLASH_ADDR 0x10000000

/* physical address of kernel */
#define KERNEL_LOAD_ADDR 0x14000000

/* physical address of kernel parameters */
#define INITRD_LOAD_ADDR 0x14800000

/* physical address of 4 KiB internal ROM */
#define PROM_ADDR 0x1fc00000

#define K1(physaddr) ((physaddr) + 0x80000000)

#define VIRT_TO_PHYS_ADDEND (-0x80000000LL)

#define MAX_ETH_FRAME_SIZE 1514

#define DEBUG_AR7

#if defined(DEBUG_AR7)
/* Set flags to >0 to enable debug output. */
static struct {
  int CLOCK:1;
  int CPMAC:1;
  int DCL:1;
  int EMIF:1;
  int GPIO:1;
  int INTC:1;
  int MDIO:1;
  int RESET:1;
  int TIMER:1;
  int UART:1;
  int VLYNQ:1;
  int WDOG:1;
  int OTHER:1;
  int RXTX:1;
} traceflags;

#define CLOCK   traceflags.CLOCK
#define DCL     traceflags.DCL
#define CPMAC   traceflags.CPMAC
#define EMIF    traceflags.EMIF
#define GPIO    traceflags.GPIO
#define INTC    traceflags.INTC
#define MDIO    traceflags.MDIO
#define RESET   traceflags.RESET
#define TIMER   traceflags.TIMER
#define UART    traceflags.UART
#define VLYNQ   traceflags.VLYNQ
#define WDOG    traceflags.WDOG
#define OTHER   traceflags.OTHER
#define RXTX    traceflags.RXTX

#define TRACE(flag, command) ((flag) ? (command) : (void)0)

#define logout(fmt, ...) fprintf(stderr, "AR7\t%-24s" fmt, __func__, ##__VA_ARGS__)
//~ #define logout(fmt, ...) fprintf(stderr, "AR7\t%-24s%-40.40s " fmt, __func__, backtrace(), ##__VA_ARGS__)

#else /* DEBUG_AR7 */

#define TRACE(flag, command) ((void)0)
#define logout(fmt, ...) ((void)0)

#endif /* DEBUG_AR7 */

#define MISSING() logout("%s:%u missing, %s!!!\n", __FILE__, __LINE__, backtrace())
#define UNEXPECTED() logout("%s:%u unexpected, %s!!!\n", __FILE__, __LINE__, backtrace())
#define backtrace() mips_backtrace()

#define BIT(n) (1 << (n))
#define BITS(n, m) (((0xffffffffU << (31 - n)) >> (31 - n + m)) << m)
//~ #define MASK(hi, lo) (((~(~0 << (hi))) & (~0 << (lo))) | (1 << (hi)))

static struct _loaderparams {
    ram_addr_t ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
} loaderparams;

#if 0
#define BBIF_SPACE1                           (KSEG1ADDR(0x01800000))
#endif

#define OHIO_ADSLSS_BASE0       KERNEL_ADDR(0x01000000)
#define OHIO_ADSLSS_BASE1       KERNEL_ADDR(0x01800000)
#define OHIO_ADSLSS_BASE2       KERNEL_ADDR(0x01C00000)

/*
Physical memory map
0x00000000      RAM start
0x00000fff      RAM end
0x08610000      I/O start
0x08613000      I/O end
0x10000000      Flash start
0x101fffff      Flash end (2 MiB)
0x103fffff      Flash end (4 MiB)
0x107fffff      Flash end (8 MiB)
0x14000000      RAM start
0x14ffffff      RAM end (16 MiB)
0x15ffffff      RAM end (32 MiB)
0x1e000000      ???
0x1fc00000      internal ROM start
0x1fc00fff      internal ROM end
*/

#define AVALANCHE_ADSLSSYS_MEM_BASE     0x01000000      /* ADSL subsystem mem base */
#define AVALANCHE_BBIF_BASE             0x02000000      /* broadband interface */
#define AVALANCHE_ATM_SAR_BASE          0x03000000      /* ATM SAR */
#define AVALANCHE_USB_MEM_BASE          0x03400000      /* USB slave mem map */
#define AVALANCHE_VLYNQ0_REGION0_BASE   0x04000000      /* VLYNQ 0 memory mapped region 0 */
#define AVALANCHE_VLYNQ0_REGION1_BASE   0x04022000      /* VLYNQ 0 memory mapped region 1 */
#define AVALANCHE_VLYNQ1_REGION0_BASE   0x0c000000      /* VLYNQ 1 memory mapped region 0 */
#define AVALANCHE_VLYNQ1_REGION1_BASE   0x0c022000      /* VLYNQ 1 memory mapped region 1 */
#define AVALANCHE_DES_BASE              0x08600000      /* ??? */
#define AVALANCHE_CPMAC0_BASE           0x08610000
#define AVALANCHE_EMIF_BASE             0x08610800
#define AVALANCHE_GPIO_BASE             0x08610900
#define AVALANCHE_CLOCK_BASE            0x08610a00      /* Clock Control */
//~ #define AVALANCHE_POWER_CTRL_PDCR     (KSEG1ADDR(0x08610A00))
#define AVALANCHE_WAKEUP_CTRL_WKCR    (KSEG1ADDR(0x08610A0C))
#define AVALANCHE_WATCHDOG_BASE         0x08610b00      /* Watchdog */
#define AVALANCHE_TIMER0_BASE           0x08610c00      /* Timer 1 */
#define AVALANCHE_TIMER1_BASE           0x08610d00      /* Timer 2 */
#define AVALANCHE_UART0_BASE            0x08610e00      /* UART 0 */
#define AVALANCHE_UART1_BASE            0x08610f00      /* UART 1 */
#define OHIO_I2C_BASE                   0x08610f00
#define AVALANCHE_I2C_BASE              0x08611000      /* I2C */
#define DEV_ID_BASE                     0x08611100
#define AVALANCHE_USB_SLAVE_BASE        0x08611200      /* USB DMA */
#define PCI_CONFIG_BASE                 0x08611300
#define AVALANCHE_MCDMA_BASE            0x08611400      /* MC DMA channels 0-3 */
#define TNETD73xx_VDMAVT_BASE           0x08611500      /* VDMAVT Control */
#define AVALANCHE_RESET_BASE            0x08611600
#define AVALANCHE_BIST_CONTROL_BASE     0x08611700      /* BIST Control */
#define AVALANCHE_VLYNQ0_BASE           0x08611800      /* VLYNQ0 port controller */
#define AVALANCHE_DCL_BASE              0x08611a00      /* Device Config Latch */
#define OHIO_MII_SEL_REG                0x08611a08
#define DSL_IF_BASE                     0x08611b00
#define AVALANCHE_VLYNQ1_BASE           0x08611c00      /* VLYNQ1 port controller */
#define AVALANCHE_MDIO_BASE             0x08611e00
#define OHIO_WDT_BASE                   0x08611f00
#define AVALANCHE_FSER_BASE             0x08612000      /* FSER base */
#define AVALANCHE_INTC_BASE             0x08612400
#define AVALANCHE_CPMAC1_BASE           0x08612800
#define AVALANCHE_END                   0x08613000
#define AVALANCHE_PHY_BASE              0x1e000000      /* ??? */
#define AVALANCHE_PHY1_BASE             0x1e100000      /* ??? */
#define AVALANCHE_PHY2_BASE             0x1e200000      /* ??? */
#define AVALANCHE_DISPLAY_BASE          0x1f000000      /* ??? */

typedef struct {
    uint32_t next;
    uint32_t buff;
    uint32_t length;
    uint32_t mode;
} cpphy_rcb_t;

typedef enum {
    RCB_SOP = BIT(31),
    RCB_EOP = BIT(30),
    RCB_OWNER = BIT(29),
    RCB_EOQ = BIT(28),
    RCB_TDOWNCMPLT = BIT(27),
    RCB_PASSCRC = BIT(26),
    RCB_JABBER = BIT(25),
    RCB_OVERSIZE = BIT(24),
    RCB_FRAGMENT = BIT(23),
    RCB_UNDERSIZED = BIT(22),
    RCB_CONTROL = BIT(21),
    RCB_OVERRUN = BIT(20),
    RCB_CODEERROR = BIT(19),
    RCB_ALIGNERROR = BIT(18),
    RCB_CRCERROR = BIT(17),
    RCB_NOMATCH = BIT(16),
} rcb_bit_t;

typedef struct {
    uint32_t next;
    uint32_t buff;
    uint32_t length;
    uint32_t mode;
} cpphy_tcb_t;

typedef enum {
    TCB_SOP = BIT(31),
    TCB_EOP = BIT(30),
    TCB_OWNER = BIT(29),
    TCB_EOQ = BIT(28),
    TCB_TDOWNCMPLT = BIT(27),
    TCB_PASSCRC = BIT(26),
} tcb_bit_t;

typedef struct {
    //~ uint8_t cmd;
    //~ uint32_t start;
    //~ uint32_t stop;
    //~ uint8_t boundary;
    //~ uint8_t tsr;
    //~ uint8_t tpsr;
    //~ uint16_t tcnt;
    //~ uint16_t rcnt;
    //~ uint32_t rsar;
    //~ uint8_t rsr;
    //~ uint8_t rxcr;
    //~ uint8_t isr;
    //~ uint8_t dcfg;
    //~ uint8_t imr;
    uint8_t phys[6];            /* mac address */
    //~ uint8_t curpag;
    //~ uint8_t mult[8]; /* multicast mask array */
    //~ int irq;
    VLANClientState *vc;
    //~ uint8_t macaddr[6];
    //~ uint8_t mem[1];
} NICState;

typedef struct {
    uint8_t *base;              /* base address */
    qemu_irq interrupt;         /* interrupt number */
    int cyclic;                 /* 1 = cyclic timer */
    int64_t time;               /* preload value */
    uint16_t prescale;          /* prescale divisor */
    QEMUTimer *qemu_timer;
} ar7_timer_t;

/* Hardware registers of the AR7. Some data is not kept here,
   but in other devices (for example both serial devices). */
typedef struct {
    uint32_t adsl[0x8000];      // 0x01000000
    uint32_t bbif[3];           // 0x02000000
    uint32_t atmsar[0x2400];    // 0x03000000
    uint32_t usbslave[0x800];   // 0x03400000
    /* VLYNQ0 memory regions are emulated in tnetw1130.c. */
    //~ uint32_t vlynq0region0[8 * KiB / 4];        // 0x04000000
    //~ uint32_t vlynq0region1[128 * KiB / 4];      // 0x04022000
    uint32_t vlynq1region0[8 * KiB / 4];        // 0x0c000000
    uint32_t vlynq1region1[128 * KiB / 4];      // 0x0c022000

    uint8_t cpmac0[0x800];      // 0x08610000
    uint8_t emif[0x100];        // 0x08610800
    uint8_t gpio[32];           // 0x08610900
    //~ uint8_t gpio_dummy[4 * 0x38];
    uint8_t clock_control[0x100];       // 0x08610a00
    // 0x08610a80 struct _ohio_clock_pll
    //~ uint32_t clock_dummy[0x18];
    uint32_t watchdog[0x20];    // 0x08610b00 struct _ohio_clock_pll
    uint8_t timer0[16];         // 0x08610c00
    uint8_t timer1[16];         // 0x08610d00
    /* TODO: UART0, UART1 memory is emulated in serial_16450.c, remove here. */
    uint32_t uart0[8];          // 0x08610e00
    uint32_t uart1[8];          // 0x08610f00
    uint32_t usb[20];           // 0x08611200
    uint32_t mc_dma[0x10][4];   // 0x08611400
    uint32_t reset_control[3];  // 0x08611600
    uint32_t reset_dummy[0x80 - 3];
    uint8_t vlynq0[0x100];      // 0x08611800
    // + 0xe0 interrupt enable bits
    uint8_t dcl[20];            // 0x08611a00
    uint8_t vlynq1[0x100];      // 0x08611c00
    uint8_t mdio[0x90];         // 0x08611e00
    uint32_t wdt[8];            // 0x08611f00
    uint8_t intc[0x300];        // 0x08612400
    //~ uint32_t exception_control[7];  //   +0x80
    //~ uint32_t pacing[3];             //   +0xa0
    //~ uint32_t channel_control[40];   //   +0x200
    uint8_t cpmac1[0x800];      // 0x08612800
    //~ uint32_t unknown[0x40]              // 0x08613000
} ar7_register_t;

/* Emulation registers of the AR7. */
typedef struct {
    CPUState *cpu_env;
    QEMUTimer *wd_timer;
    qemu_irq *primary_irq;
    qemu_irq *secondary_irq;
    NICState nic[2];
    /* Address of phy device (0...31). Only one phy device is supported.
       The internal phy has address 31. */
    unsigned phyaddr;
    /* VLYNQ index for TNETW1130. Set to >1 to disable WLAN. */
    unsigned vlynq_tnetw1130;
    CharDriverState *gpio_display;
    SerialState *serial[2];
    uint8_t *cpmac[2];
    ar7_timer_t timer[2];
    uint8_t *vlynq[2];
} ar7_status_t;

static ar7_register_t av;
static ar7_status_t ar7;

int ar7_afe_clock = 35328000;
int ar7_ref_clock = 25000000;
int ar7_xtal_clock = 24000000;

static const unsigned ar7_cpu_clock = 150000000;
static const unsigned ar7_bus_clock = 125000000;
static const unsigned ar7_dsp_clock = 0;
static const unsigned io_frequency = 125000000 / 2;

/* Global variable avalanche can be used in debugger. */
//~ ar7_register_t *avalanche = &av;

static const char *mips_backtrace(void)
{
    static char buffer[256];
    char *p = buffer;
    if (cpu_single_env != 0) {
      assert(ar7.cpu_env == 0 || cpu_single_env == ar7.cpu_env);
      p += sprintf(p, "[%s]", lookup_symbol(cpu_single_env->active_tc.PC));
      p += sprintf(p, "[%s]", lookup_symbol(cpu_single_env->active_tc.gpr[31]));
    } else {
      /* Called from remote gdb. */
      p += sprintf(p, "[gdb?]");
    }
    assert((p - buffer) < sizeof(buffer));
    return buffer;
}

static const char *dump(const uint8_t * buf, unsigned size)
{
    static char buffer[3 * 25 + 1];
    char *p = &buffer[0];
    if (size > 25)
        size = 25;
    while (size-- > 0) {
        p += sprintf(p, " %02x", *buf++);
    }
    return buffer;
}

/*****************************************************************************
 *
 * Helper functions.
 *
 ****************************************************************************/

static inline unsigned ptr2uint(void *ptr)
{
  union {
    void *ptr;
    unsigned n;
  } u;
  u.ptr = ptr;
  return u.n;
}

static inline void *uint2ptr(unsigned n)
{
  union {
    void *ptr;
    unsigned n;
  } u;
  u.n = n;
  return u.ptr;
}

typedef struct {
    unsigned offset;
    const char *name;
} offset_name_t;

static const char *offset2name(const offset_name_t *o2n, unsigned offset)
{
    static char buffer[12];
    const char *name = buffer;
    snprintf(buffer, sizeof(buffer), "0x%08x", offset);
    for (; o2n->name != 0; o2n++) {
        if (offset == o2n->offset) {
            name = o2n->name;
            break;
        }
    }
    return name;
}

#if defined(DEBUG_AR7)

#define SET_TRACEFLAG(name) \
    do { \
        char *substring = strstr(env, #name); \
        if (substring) { \
            name = ((substring > env && substring[-1] == '-') ? 0 : 1); \
        } \
        TRACE(name, logout("Logging enabled for " #name "\n")); \
    } while(0)

static void set_traceflags(void)
{
    const char *env = getenv("DEBUG_AR7");
    if (env != 0) {
        unsigned long ul = strtoul(env, 0, 0);
        if ((ul == 0) && strstr(env, "ALL")) ul = 0xffffffff;
        memcpy(&traceflags, &ul, sizeof(traceflags));
        SET_TRACEFLAG(CLOCK);
        SET_TRACEFLAG(CPMAC);
        SET_TRACEFLAG(DCL);
        SET_TRACEFLAG(EMIF);
        SET_TRACEFLAG(GPIO);
        SET_TRACEFLAG(INTC);
        SET_TRACEFLAG(MDIO);
        SET_TRACEFLAG(RESET);
        SET_TRACEFLAG(TIMER);
        SET_TRACEFLAG(UART);
        SET_TRACEFLAG(VLYNQ);
        SET_TRACEFLAG(WDOG);
        SET_TRACEFLAG(OTHER);
        SET_TRACEFLAG(RXTX);
    }
}
#endif /* DEBUG_AR7 */

static uint32_t reg_read(const uint8_t * reg, uint32_t addr)
{
    if (addr & 3) {
        logout("0x%08x\n", addr);
        UNEXPECTED();
    }
    return le32_to_cpu(*(uint32_t *) (&reg[addr]));
}

static void reg_write(uint8_t * reg, uint32_t addr, uint32_t value)
{
    assert(!(addr & 3));
    *(uint32_t *) (&reg[addr]) = cpu_to_le32(value);
}

#if 0
static void reg_inc(uint8_t * reg, uint32_t addr)
{
    assert(!(addr & 3));
    reg_write(reg, addr, reg_read(reg, addr) + 1);
}
#endif

static void reg_clear(uint8_t * reg, uint32_t addr, uint32_t value)
{
    assert(!(addr & 3));
    *(uint32_t *) (&reg[addr]) &= cpu_to_le32(~value);
}

static void reg_set(uint8_t * reg, uint32_t addr, uint32_t value)
{
    assert(!(addr & 3));
    *(uint32_t *) (&reg[addr]) |= cpu_to_le32(value);
}

/*****************************************************************************
 *
 * Malta display emulation.
 * AR7 based routers don't include an ASCII display, but AVM code
 * thinks there is a Malta like display. So we emulate it here.
 *
 ****************************************************************************/

typedef struct {
    uint32_t leds;
    CharDriverState *display;
    char display_text[9];
} MaltaFPGAState;

static MaltaFPGAState malta_display;

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

    qemu_chr_printf(s->display, "\e[3;2H\e[0;32m%-8.8s", leds_text);
    qemu_chr_printf(s->display, "\e[8;2H\e[0;31m%-8.8s\r\n\n\e[0;37m", s->display_text);
}

/*****************************************************************************
 *
 * Interrupt emulation.
 * Interrupt controller emulation.
 *
 ****************************************************************************/

typedef enum {
    /* primary interrupts 8 ... 47 */
    INTERRUPT_EXT0 =  9,        /* ext0 ??? */
    INTERRUPT_EXT1 = 10,        /* ext1 ??? */
    INTERRUPT_TIMER0 = 13,      /* timer0 ??? */
    INTERRUPT_TIMER1 = 14,      /* timer1 ??? */
    INTERRUPT_SERIAL0 = 15,
    INTERRUPT_SERIAL1 = 16,
    INTERRUPT_DMA0 = 17,        /* ??? */
    INTERRUPT_DMA1 = 18,        /* ??? */
    INTERRUPT_ATMSAR = 23,      /* ??? */
    INTERRUPT_CPMAC0 = 27,
    INTERRUPT_VLYNQ0 = 29,      /* ??? */
    INTERRUPT_CODEC = 30,       /* ??? */
    INTERRUPT_USBSLAVE = 32,    /* ??? */
    INTERRUPT_VLYNQ1 = 33,      /* ??? */
    INTERRUPT_PHY = 36,         /* ??? */
    INTERRUPT_I2C = 37,         /* ??? */
    INTERRUPT_DMA2 = 38,        /* ??? */
    INTERRUPT_DMA3 = 39,        /* ??? */
    INTERRUPT_CPMAC1 = 41,
    INTERRUPT_VDMA_RX = 45,     /* ??? */
    INTERRUPT_VDMA_TX = 46,     /* ??? */
    INTERRUPT_ADSLSS = 47,      /* ??? */
      /* secondary interrupts 40 ... 71 */
    INTERRUPT_EMIF = 55,        /* emif ??? */
} ar7_interrupt;

typedef enum {
    INTC_SR1 = 0x00,    /* Interrupt Status/Set 1 */
    INTC_SR2 = 0x04,    /* Interrupt Status/Set 2 */
    INTC_CR1 = 0x10,    /* Interrupt Clear 1 */
    INTC_CR2 = 0x14,    /* Interrupt Clear 2 */
    INTC_ESR1 = 0x20,   /* Interrupt Enable (Set) 1 */
    INTC_ESR2 = 0x24,   /* Interrupt Enable (Set) 2 */
    INTC_ECR1 = 0x30,   /* Interrupt Enable Clear 1 */
    INTC_ECR2 = 0x34,   /* Interrupt Enable Clear 2 */
    INTC_PIIR = 0x40,   /* Priority Interrupt Index */
    INTC_PIMR = 0x44,   /* Priority Interrupt Mask Index */
    INTC_IPMR1 = 0x50,  /* Interrupt Polarity Mask 1 */
    INTC_IPMR2 = 0x54,  /* Interrupt Polarity Mask 2 */
    INTC_TMR1 = 0x60,   /* Interrupt Type Mask 1 */
    INTC_TMR2 = 0x64,   /* Interrupt Type Mask 2 */
    /* Avalanche Exception control registers */
    INTC_EXSR = 0x80,   /* Exceptions Status/Set */
    INTC_EXCR = 0x88,   /* Exceptions Clear */
    INTC_EXIESR = 0x90, /* Exceptions Interrupt Enable Status/Set */
    INTC_EXIECR = 0x98, /* Exceptions Interrupt Enable Clear */
    /* Interrupt Pacing */
    INTC_IPACEP = 0xa0, /* Interrupt pacing */
    INTC_IPACEMAP = 0xa4,
                        /* Interrupt Pacing Map */
    INTC_IPACEMAX = 0xa8,
                        /* Interrupt Pacing Max Register */
    /* Interrupt Channel Control */
    INTC_CINTNR = 0x200, /* 40 entries */
                        /* Channel Interrupt Number Reg */
} intc_register_t;

static void ar7_update_interrupt(void)
{
    static int intset;

    CPUState *env = first_cpu;
    assert(env == ar7.cpu_env);
    uint32_t masked_int1;
    uint32_t masked_int2;

    masked_int1 = (reg_read(av.intc, INTC_ESR1) & reg_read(av.intc, INTC_SR1));
    masked_int2 = (reg_read(av.intc, INTC_ESR2) & reg_read(av.intc, INTC_SR2));
    if (masked_int1 || masked_int2) {
        if (!intset) {
            intset = 1;
            //~ reg_set(av.intc, INTC_EXSR, BIT(2));
            //~ reg_set(av.intc, INTC_EXCR, BIT(2));
            qemu_irq_raise(env->irq[2]);
            //~ /* use hardware interrupt 0 */
            //~ cpu_env->CP0_Cause |= 0x00000400;
            //~ cpu_interrupt(cpu_env, CPU_INTERRUPT_HARD);
            TRACE(INTC, logout("raise hardware interrupt, mask 0x%08x%08x\n",
                masked_int2, masked_int1));
        } else {
            int channel;
            TRACE(INTC, logout("interrupt still set\n"));
            for (channel = 0; channel < 40; channel++) {
                unsigned cindex = channel / 32;
                unsigned offset = channel % 32;
                uint32_t cr = reg_read(av.intc, INTC_CR1 + 4 * cindex);
                if (cr & BIT(offset)) {
                    reg_write(av.intc, INTC_PIIR, (channel << 16) | channel);
                    break;
                }
            }
            if (channel == 40) {
                reg_write(av.intc, INTC_PIIR, 0);
            }
        }
    } else {
        if (intset) {
            intset = 0;
            qemu_irq_lower(env->irq[2]);
            //~ cpu_env->CP0_Cause &= ~0x00000400;
            //~ cpu_reset_interrupt(cpu_env, CPU_INTERRUPT_HARD);
            TRACE(INTC, logout("clear hardware interrupt\n"));
        } else {
            TRACE(INTC, logout("interrupt still cleared\n"));
        }
    }
}

static void ar7_primary_irq(void *opaque, int channel, int level)
{
    /* AR7 primary interrupt. */
    CPUState *env = (CPUState *)opaque;
    unsigned irq_num = channel + MIPS_EXCEPTION_OFFSET;
    unsigned cindex = channel / 32;
    unsigned offset = channel % 32;
    TRACE(INTC && (irq_num != INTERRUPT_SERIAL0 || UART),
          logout("(%p,%d,%d)\n", opaque, irq_num, level));
    if (level) {
        assert(env == first_cpu);
        assert(env == ar7.cpu_env);
        uint32_t intmask = reg_read(av.intc, INTC_ESR1 + 4 * cindex);
        if (intmask & BIT(offset)) {
            TRACE(INTC && (irq_num != 15 || UART),
                  logout("(%p,%d,%d)\n", opaque, irq_num, level));
            reg_write(av.intc, INTC_PIIR, (channel << 16) | channel);
            /* use hardware interrupt 0 */
            qemu_irq_raise(env->irq[2]);
            //~ cpu_env->CP0_Cause |= 0x00000400;
            //~ cpu_interrupt(cpu_env, CPU_INTERRUPT_HARD);
        } else {
            TRACE(INTC && (irq_num != 15 || UART),
                  logout("(%p,%d,%d) is disabled\n", opaque, irq_num, level));
        }
        reg_set(av.intc, INTC_SR1 + 4 * cindex, BIT(offset));
        reg_set(av.intc, INTC_CR1 + 4 * cindex, BIT(offset));
        /* TODO: write correct value to INTC_PIIR? */
        //~ reg_write(av.intc, INTC_PIIR, (channel << 16) | channel);
    } else {
        /* TODO: write correct value to INTC_PIIR? */
        reg_clear(av.intc, INTC_SR1 + 4 * cindex, BIT(offset));
    }
    ar7_update_interrupt();
}

static void ar7_secondary_irq(void *opaque, int channel, int level)
{
    /* AR7 secondary interrupt. */
    unsigned irq_num = channel + MIPS_EXCEPTION_OFFSET + NUM_PRIMARY_IRQS;
    TRACE(INTC, logout("(%p,%d,%d)\n", opaque, irq_num, level));
    reg_set(av.intc, INTC_EXSR, BIT(channel));
    reg_set(av.intc, INTC_EXCR, BIT(channel));
    MISSING();
    ar7_update_interrupt();
}

static const char *const intc_names[] = {
    "Interrupt Status/Set 1",
    "Interrupt Status/Set 2",
    "0x08",
    "0x0c",
    "Interrupt Clear 1",
    "Interrupt Clear 2",
    "0x18",
    "0x1c",
    "Interrupt Enable Set 1",
    "Interrupt Enable Set 2",
    "0x28",
    "0x2c",
    "Interrupt Enable Clear 1",
    "Interrupt Enable Clear 2",
    "0x38",
    "0x3c",
    "Priority Interrupt Index",
    "Priority Interrupt Mask Index",
    "0x48",
    "0x4c",
    "Interrupt Polarity Mask 1",
    "Interrupt Polarity Mask 2",
    "0x58",
    "0x5c",
    "Interrupt Type Mask 1",
    "Interrupt Type Mask 2",
    "0x68",
    "0x6c",
    "0x70",
    "0x74",
    "0x78",
    "0x7c",
    "Exceptions Status/Set",
    "0x84",
    "Exceptions Clear",
    "0x8c",
    "Exceptions Interrupt Enable (set)",
    "0x94",
    "Exceptions Interrupt Enable (clear)",
    "0x9c",
    "Interrupt Pacing",
    "Interrupt Pacing Map",
    "Interrupt Pacing Max",
};

static const char *i2intc(unsigned name_index)
{
    static char buffer[32];
    const char *text = buffer;
    if (name_index < ARRAY_SIZE(intc_names)) {
        text = intc_names[name_index];
    } else if (name_index >= 128 && name_index < 168) {
        snprintf(buffer, sizeof(buffer),
                 "Channel Interrupt Number 0x%02x", name_index - 128);
    } else {
        snprintf(buffer, sizeof(buffer), "0x%02x", name_index);
    }
    return text;
}

static uint32_t ar7_intc_read(unsigned offset)
{
    unsigned name_index = offset / 4;
    uint32_t val = reg_read(av.intc, offset);
    if (0) {
    } else if (offset == INTC_ECR1 || offset == INTC_ECR2) {
        TRACE(INTC, logout("intc[%s] = 0x%08x\n", i2intc(name_index), val));
        MISSING();
    } else {
        TRACE(INTC, logout("intc[%s] = 0x%08x\n", i2intc(name_index), val));
    }
    return val;
}

static void ar7_intc_write(unsigned offset, uint32_t val)
{
    unsigned name_index = offset / 4;
    if (0) {
        //~ } else if (name_index == 4) {
    } else if (offset == INTC_SR1 || offset == INTC_SR2) {
        /* Interrupt set. */
        TRACE(INTC, logout("intc[%s] val 0x%08x\n", i2intc(name_index), val));
        reg_set(av.intc, offset, val);
        MISSING();
        ar7_update_interrupt();
    } else if (offset == INTC_CR1 || offset == INTC_CR2) {
        /* Interrupt clear. */
        TRACE(INTC, logout("intc[%s] val 0x%08x\n", i2intc(name_index), val));
        offset -= INTC_CR1;
        reg_clear(av.intc, INTC_SR1 + offset, val);
        reg_clear(av.intc, INTC_CR1 + offset, val);
        /* TODO: check old value? */
        //~ reg_write(av.intc, INTC_PIIR, 0);
        //~ logout("??? clear interrupt\a\n");
        ar7_update_interrupt();
    } else if (offset == INTC_ESR1 || offset == INTC_ESR2) {
        /* Interrupt enable. */
        reg_set(av.intc, offset, val);
        TRACE(INTC, logout("intc[%s] val 0x%08x, mask 0x%08x\n",
                           i2intc(name_index), val, reg_read(av.intc, offset)));
        //~ logout("??? check interrupt\a\n");
        ar7_update_interrupt();
    } else if (offset == INTC_ECR1 || offset == INTC_ECR2) {
        offset += INTC_ESR1 - INTC_ECR1;
        reg_clear(av.intc, offset, val);
        TRACE(INTC, logout("intc[%s] val 0x%08x, mask 0x%08x\n",
                           i2intc(name_index), val, reg_read(av.intc, offset)));
        //~ logout("??? check interrupt\a\n");
        ar7_update_interrupt();
    } else if (offset == INTC_EXSR) {
        /* Exceptions Status/Set. */
        TRACE(INTC, logout("intc[%s] val 0x%08x\n", i2intc(name_index), val));
        reg_set(av.intc, INTC_EXSR, val);
        MISSING();
    } else if (offset == INTC_EXCR) {
        /* Exceptions Clear. */
        TRACE(INTC, logout("intc[%s] val 0x%08x\n", i2intc(name_index), val));
        reg_clear(av.intc, INTC_EXSR, val);
        ar7_update_interrupt();
    } else if (offset == INTC_EXIESR) {
        /* Exceptions Interrupt Enable Status/Set. */
        TRACE(INTC, logout("intc[%s] val 0x%08x\n", i2intc(name_index), val));
        reg_set(av.intc, INTC_EXIESR, val);
        ar7_update_interrupt();
    } else if (offset == INTC_EXIECR) {
        /* Exceptions Interrupt Enable Clear. */
        TRACE(INTC, logout("intc[%s] val 0x%08x\n", i2intc(name_index), val));
        reg_clear(av.intc, INTC_EXIESR, val);
        ar7_update_interrupt();
    } else {
        TRACE(INTC, logout("intc[%s] val 0x%08x\n", i2intc(name_index), val));
        reg_write(av.intc, offset, val);
    }
}

/*****************************************************************************
 *
 * Clock / power controller emulation.
 *
 ****************************************************************************/

#if 0
/* Power Control  */
#define TNETD73XX_POWER_CTRL_PDCR           (TNETD73XX_CLOCK_CTRL_BASE + 0x0)
#define TNETD73XX_POWER_CTRL_PCLKCR         (TNETD73XX_CLOCK_CTRL_BASE + 0x4)
#define TNETD73XX_POWER_CTRL_PDUCR          (TNETD73XX_CLOCK_CTRL_BASE + 0x8)
#define TNETD73XX_POWER_CTRL_WKCR           (TNETD73XX_CLOCK_CTRL_BASE + 0xC)

/* Clock Control */
#define TNETD73XX_CLK_CTRL_ACLKCR1          (TNETD73XX_CLOCK_CTRL_BASE + 0xA0)
#define TNETD73XX_CLK_CTRL_ACLKPLLCR1       (TNETD73XX_CLOCK_CTRL_BASE + 0xB0)

#define CLKC_CLKCR(x)          (TNETD73XX_CLOCK_CTRL_BASE + 0x20 + (0x20 * (x)))
#define CLKC_CLKPLLCR(x)       (TNETD73XX_CLOCK_CTRL_BASE + 0x30 + (0x20 * (x)))

static void ar7_machine_power_off(void)
{
    volatile uint32_t *power_reg = (void *)(KSEG1ADDR(0x08610A00));
    uint32_t power_state = *power_reg;

    power_state &= ~(3 << 30);
    power_state |= (3 << 30);   /* power down */
    *power_reg = power_state;

    printk("after power down?\n");
}
#endif

typedef enum {
    CLOCK_PDC = 0x00,
    CLOCK_BUS_CTL = 0x20,
    CLOCK_BUS_PLL = 0x30,
    CLOCK_CPU_CTL = 0x40,
    CLOCK_CPU_PLL = 0x50,
    CLOCK_USB_CTL = 0x60,
    CLOCK_USB_PLL = 0x70,
    CLOCK_DSP_CTL = 0x80,
    CLOCK_DSP_PLL = 0x90,
    //~ CLOCK_DSP1_CTL = 0xa0,
    //~ CLOCK_DSP1_PLL = 0xb0,
} clock_register_t;

#undef ENTRY
#define ENTRY(entry) { CLOCK_##entry, #entry }
static const offset_name_t clock_addr2reg[] = {
    ENTRY(PDC),
    ENTRY(BUS_CTL),
    ENTRY(BUS_PLL),
    ENTRY(CPU_CTL),
    ENTRY(CPU_PLL),
    ENTRY(USB_CTL),
    ENTRY(USB_PLL),
    ENTRY(DSP_CTL),
    ENTRY(DSP_PLL),
    { 0 }
};

static const char *clock_regname(unsigned offset)
{
    return offset2name(clock_addr2reg, offset);
}

#if 0
struct tnetd7300_clock {
	u32 ctrl;
#define PREDIV_MASK 0x001f0000
#define PREDIV_SHIFT 16
#define POSTDIV_MASK 0x0000001f
	u32 unused1[3];
	u32 pll;
#define MUL_MASK 0x0000f000
#define MUL_SHIFT 12
#define PLL_MODE_MASK 0x00000001
#define PLL_NDIV 0x00000800
#define PLL_DIV 0x00000002
#define PLL_STATUS 0x00000001
	u32 unused2[3];
} __attribute__ ((packed));

struct tnetd7300_clocks {
	struct tnetd7300_clock bus;
	struct tnetd7300_clock cpu;
	struct tnetd7300_clock usb;
	struct tnetd7300_clock dsp;
} __attribute__ ((packed));
#endif

static void power_write(uint32_t val)
{
    uint32_t oldpowerstate = reg_read(av.clock_control, 0);
    uint32_t newpowerstate = val;
    if (oldpowerstate != newpowerstate) {
#if defined(DEBUG_AR7)
        static const char *powerbits[] = {
            /* 00 */ "usb", "wdt", "uart0", "uart1",
            /* 04 */ "iic", "vdma", "gpio", "vlynq1",
            /* 08 */ "sar", "adsl", "emif", "reserved11",
            /* 12 */ "adsp", "ram", "rom", "dma",
            /* 16 */ "bist", "reserved17", "timer0", "timer1",
            /* 20 */ "emac0", "reserved21", "emac1", "reserved23",
            /* 24 */ "ephy", "reserved25", "reserved26", "vlynq0",
            /* 28 */ "reserved28", "reserved29", "reserved30", "reserved31" // 30?, 31?
        };
        // bit coded device(s). 0 = disabled (reset), 1 = enabled.
        uint32_t changed = (oldpowerstate ^ newpowerstate);
        uint32_t enabled = (changed & newpowerstate);
        unsigned i;
        for (i = 0; i < 32; i++) {
            if (changed & BIT(i)) {
                TRACE(CLOCK,
                      logout("power %sabled %s (0x%08x)\n",
                             (enabled & BIT(i)) ? "en" : "dis",
                             powerbits[i], val));
            }
        }
#endif
        oldpowerstate >>= 30;
        TRACE(CLOCK, logout("change power state from %u to %u\n",
                            oldpowerstate, newpowerstate));
    }
}

static uint32_t clock_read(unsigned offset)
{
    static uint32_t last;
    static unsigned count;
    uint32_t val = reg_read(av.clock_control, offset);
    unsigned clock_index = offset / 4;
    if (clock_index == 0x0c || clock_index == 0x14 || clock_index == 0x1c || clock_index == 0x24) {
        /* Reset PLL status bit after a short delay. */
        if (val == 0x00000005 || val == 0x00007005 || val == 0x000047fd || val == 0x000057fd) {
          /* Workaround for AVM Linux 2.6.13.1. */
          val &= ~1;
        } else if (val == last) {
          if (count > 0) {
            count--;
          } else {
            val ^= 1;
            reg_write(av.clock_control, offset, val);
          }
        } else {
          count = 2;
          last = val;
          val |= 1;
          reg_write(av.clock_control, offset, val);
        }
    }
    TRACE(CLOCK, logout("clock[%s] = 0x%08x %s\n", clock_regname(offset), val, backtrace()));
    return val;
}

static void clock_write(unsigned offset, uint32_t val)
{
    TRACE(CLOCK, logout("clock[%s] = 0x%08x %s\n", clock_regname(offset), val, backtrace()));
    if (offset == CLOCK_PDC) {
        power_write(val);
    } else if (offset / 4 == 0x0c) {
        uint32_t oldval = reg_read(av.clock_control, offset);
        TRACE(CLOCK, logout("clock[%s] was 0x%08x %s\n", clock_regname(offset), oldval, backtrace()));
        if ((oldval & ~1) == val) {
            val = oldval;
        }
    }
    reg_write(av.clock_control, offset, val);
}

/*****************************************************************************
 *
 * Configuration (device config latch) emulation.
 *
 ****************************************************************************/

static const char *i2dcl[] = {
    "config",
    "test mux1",
    "test mux2",
    "test mux3",
    "adsl pll select",
    "speed control",
    "speed control password",
    "speed control capture",
};

typedef enum {
    DCL_BOOT_CONFIG = 0x00,
    DCL_BOOT_TEST_MUX1 = 0x04,
    DCL_BOOT_TEST_MUX2 = 0x08,
    DCL_BOOT_TEST_MUX3 = 0x0c,
    DCL_BOOT_ADSL_PLL_SELECT = 0x10,
    DCL_BOOT_SPEED_CONTROL = 0x14,
    DCL_BOOT_SPEED_CONTROL_PW = 0x18,
    DCL_BOOT_SPEED_CONTROL_CAPTURE = 0x1c,
} dcl_register_t;

typedef enum {
    CONFIG_BOOTS = BITS(2, 0),          /* 001 */
    CONFIG_WSDP = BIT(3),               /* 0 */
    CONFIG_WDHE = BIT(4),               /* 0 */
    CONFIG_PLL_BYP = BIT(5),            /* 0 */
    CONFIG_ENDIAN = BIT(6),             /* 0 = little endian */
    CONFIG_FLASHW = BITS(8, 7),         /* 01 = 16 bit flash */
    CONFIG_EMIFRATE = BIT(9),           /* 1 */
    CONFIG_EMIFTEST = BIT(10),          /* 0 */
    CONFIG_BOOTS_INT = BITS(13, 11),    /* 000 */
    CONFIG_SYS_PLL_SEL = BITS(15, 14),  /* 01, BUS */
    CONFIG_CPU_PLL_SEL = BITS(17, 16),  /* 01 */
    CONFIG_USB_PLL_SEL = BITS(19, 18),  /* 11 */
    CONFIG_EPHY_PLL_SEL = BITS(21, 20), /* 01 */
    CONFIG_DSP_PLL_SEL = BITS(23, 22),  /* 01, ADSL */
    CONFIG_ADSL_RST = BIT(24),          /* 0 */
    CONFIG_MIPS_ASYNC = BIT(25),        /* 1 */
    CONFIG_DEF = BIT(26),               /* 0 */
    CONFIG_RESERVED = BITS(31, 27),     /* 0 */
} dcl_config_bit_t;

#if 0
/* clock selection */
        case 0: /*--- AFE_CLKl input (DSP, 35328000) ---*/
        case 1: /*--- REFCLCKl input (LAN, 25000000) ---*/
        case 2: /*--- XTAL3IN input ---*/
        case 3: /*--- MIPS-Pll output ---*/
#endif

static uint32_t ar7_dcl_read(unsigned offset)
{
    uint32_t val = reg_read(av.dcl, offset);
    const char *text = i2dcl[offset / 4];
    int logflag = DCL;
    if (0) {
    } else if (offset == DCL_BOOT_CONFIG) {
    } else {
    }
    TRACE(logflag, logout("dcl[%s] (0x%08x) = 0x%08x %s\n",
                        text, (AVALANCHE_DCL_BASE + offset),
                        val, backtrace()));
    return val;
}

static uint32_t ar7_dcl_write(unsigned offset, uint32_t val)
{
    reg_write(av.dcl, offset, val);
    const char *text = i2dcl[offset / 4];
    int logflag = DCL;
    if (0) {
    } else if (offset == DCL_BOOT_CONFIG) {
      assert(0);
    } else {
    }
    TRACE(logflag, logout("dcl[%s] (0x%08x) = 0x%08x %s\n",
                        text, (AVALANCHE_DCL_BASE + offset),
                        val, backtrace()));
    return val;
}

typedef enum {
    TEST_MUX_MBSPL_SEL = BIT(0),
    TEST_MUX_CODEC_CHAR_EN = BIT(1),
} dcl_test_mux1_bit_t;

#if 0
    union _hw_boot_test_mux_2 {
        struct __hw_boot_test_mux_2 {
            unsigned int mii0_sel : 1;
        } Bits;
        volatile unsigned int Register;
    } hw_boot_test_mux_2 ;

    union _hw_boot_test_mux_3 {
        struct __hw_boot_test_mux_3 {
            unsigned int pll_pin_output_enable : 1;
            unsigned int pll_pin_out_id : 2; /* 0: System, 1: MIPS, 2: USB, 3: adsl */
            unsigned int pll_pin_out_div_enable : 1;
        } Bits;
        volatile unsigned int Register;
    } hw_boot_test_mux_3 ;

    union _hw_boot_adsl_pll_select {
        struct __hw_boot_adsl_pll_select {
            unsigned int pll_0_select : 1;
        } Bits;
        volatile unsigned int Register;
    } hw_boot_adsl_pll_select ;

    union _hw_boot_speed_control {
        struct __hw_boot_speed_control {
            unsigned int gated_ring_oscillator_enable : 1;
            unsigned int input_counter_enable : 1;
            unsigned int gated_oscillator_select : 3;
        } Bits;
        volatile unsigned int Register;
    } hw_boot_speed_control ;

    union _hw_boot_speed_control_password {
        struct __hw_boot_speed_control_password {
            unsigned int passwd_enable : 1;
        } Bits;
        volatile unsigned int Register;
    } hw_boot_speed_control_password ;

    union _hw_boot_speed_control_capture {
        struct __hw_boot_speed_control_capture {
            unsigned int out : 16;
        } Bits;
        volatile unsigned int Register;
    } hw_boot_speed_control_capture ;
#endif

/*****************************************************************************
 *
 * Ethernet Media Access Controller (EMAC, CPMAC) emulation.
 *
 ****************************************************************************/

/* Large parts of the emac code can be used for TMS320DM644x emac, too.
   Parts which are specific for AR7 and must be changed for other SoCs
   are marked with CONFIG_AR7_EMAC. */
#define CONFIG_AR7_EMAC         1

#if 0

/*
08611600  43 00 72 04 00 00 00 00  00 00 00 00 00 00 00 00  |C.r.............|
08611610  43 00 72 04 00 00 00 00  00 00 00 00 00 00 00 00  |C.r.............|
08611a00  91 42 5d 02 00 00 00 00  00 00 00 00 00 00 00 00  |.B].............|
08611a10  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
08611b00  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*/

cpmac_reg.h:
#define BOFFTEST(base)                 ((MEM_PTR)(base+0x1E0))
#define PACTEST(base)                  ((MEM_PTR)(base+0x1E4))
#define RXPAUSE(base)                  ((MEM_PTR)(base+0x1E8))
#define TXPAUSE(base)                  ((MEM_PTR)(base+0x1EC))

#endif

typedef enum {
    CPMAC_TXIDVER = 0x0000,
    CPMAC_TXCONTROL = 0x0004,
    CPMAC_TXTEARDOWN = 0x0008,
    CPMAC_RXIDVER = 0x0010,
    CPMAC_RXCONTROL = 0x0014,
    CPMAC_RXTEARDOWN = 0x0018,
    CPMAC_RXMBPENABLE = 0x0100,
    CPMAC_RXUNICASTSET = 0x0104,
    CPMAC_RXUNICASTCLEAR = 0x0108,
    CPMAC_RXMAXLEN = 0x010c,
    CPMAC_RXBUFFEROFFSET = 0x0110,
    CPMAC_RXFILTERLOWTHRESH = 0x0114,
    CPMAC_RX0FLOWTHRESH = 0x0120,
    CPMAC_RX1FLOWTHRESH = 0x0124,
    CPMAC_RX2FLOWTHRESH = 0x0128,
    CPMAC_RX3FLOWTHRESH = 0x012c,
    CPMAC_RX4FLOWTHRESH = 0x0130,
    CPMAC_RX5FLOWTHRESH = 0x0134,
    CPMAC_RX6FLOWTHRESH = 0x0138,
    CPMAC_RX7FLOWTHRESH = 0x013c,
    CPMAC_RX0FREEBUFFER = 0x0140,
    CPMAC_RX1FREEBUFFER = 0x0144,
    CPMAC_RX2FREEBUFFER = 0x0148,
    CPMAC_RX3FREEBUFFER = 0x014c,
    CPMAC_RX4FREEBUFFER = 0x0150,
    CPMAC_RX5FREEBUFFER = 0x0154,
    CPMAC_RX6FREEBUFFER = 0x0158,
    CPMAC_RX7FREEBUFFER = 0x015c,
    CPMAC_MACCONTROL = 0x0160,
    CPMAC_MACSTATUS = 0x0164,
    CPMAC_EMCONTROL = 0x0168,
    CPMAC_TXINTSTATRAW = 0x0170,
    CPMAC_TXINTSTATMASKED = 0x0174,
    CPMAC_TXINTMASKSET = 0x0178,
    CPMAC_TXINTMASKCLEAR = 0x017c,
    CPMAC_MACINVECTOR = 0x0180,
    CPMAC_MACEOIVECTOR = 0x0184,
    CPMAC_RXINTSTATRAW = 0x0190,
    CPMAC_RXINTSTATMASKED = 0x0194,
    CPMAC_RXINTMASKSET = 0x0198,
    CPMAC_RXINTMASKCLEAR = 0x019c,
    CPMAC_MACINTSTATRAW = 0x01a0,
    CPMAC_MACINTSTATMASKED = 0x01a4,
    CPMAC_MACINTMASKSET = 0x01a8,
    CPMAC_MACINTMASKCLEAR = 0x01ac,
    CPMAC_MACADDRLO_0 = 0x01b0,
    CPMAC_MACADDRLO_1 = 0x01b4,
    CPMAC_MACADDRLO_2 = 0x01b8,
    CPMAC_MACADDRLO_3 = 0x01bc,
    CPMAC_MACADDRLO_4 = 0x01c0,
    CPMAC_MACADDRLO_5 = 0x01c4,
    CPMAC_MACADDRLO_6 = 0x01c8,
    CPMAC_MACADDRLO_7 = 0x01cc,
    CPMAC_MACADDRMID = 0x01d0,
    CPMAC_MACADDRHI = 0x01d4,
    CPMAC_MACHASH1 = 0x01d8,
    CPMAC_MACHASH2 = 0x01dc,
    /* Statistics. */
    CPMAC_RXGOODFRAMES = 0x0200,
    CPMAC_RXBROADCASTFRAMES = 0x0204,
    CPMAC_RXMULTICASTFRAMES = 0x0208,
    CPMAC_RXPAUSEFRAMES = 0x020c,
    CPMAC_RXCRCERRORS = 0x0210,
    CPMAC_RXALIGNCODEERRORS = 0x0214,
    CPMAC_RXOVERSIZEDFRAMES = 0x0218,
    CPMAC_RXJABBERFRAMES = 0x021c,
    CPMAC_RXUNDERSIZEDFRAMES = 0x0220,
    CPMAC_RXFRAGMENTS = 0x0224,
    CPMAC_RXFILTEREDFRAMES = 0x0228,
    CPMAC_RXQOSFILTEREDFRAMES = 0x022c,
    CPMAC_RXOCTETS = 0x0230,
    CPMAC_TXGOODFRAMES = 0x234,
    CPMAC_TXBROADCASTFRAMES = 0x238,
    CPMAC_TXMULTICASTFRAMES = 0x23c,
    CPMAC_TXPAUSEFRAMES = 0x0240,
    CPMAC_TXDEFERREDFRAMES = 0x0244,
    CPMAC_TXCOLLISIONFRAMES = 0x0248,
    CPMAC_TXSINGLECOLLFRAMES = 0x024c,
    CPMAC_TXMULTCOLLFRAMES = 0x0250,
    CPMAC_TXEXCESSIVECOLLISIONS = 0x0254,
    CPMAC_TXLATECOLLISIONS = 0x0258,
    CPMAC_TXUNDERRUN = 0x025c,
    CPMAC_TXCARRIERSENSEERRORS = 0x0260,
    CPMAC_TXOCTETS = 0x0264,
    CPMAC_64OCTETFRAMES = 0x0268,
    CPMAC_65T127OCTETFRAMES = 0x026c,
    CPMAC_128T255OCTETFRAMES = 0x0270,
    CPMAC_256T511OCTETFRAMES = 0x0274,
    CPMAC_512T1023OCTETFRAMES = 0x0278,
    CPMAC_1024TUPOCTETFRAMES = 0x027c,
    CPMAC_NETOCTETS = 0x0280,
    CPMAC_RXSOFOVERRUNS = 0x0284,
    CPMAC_RXMOFOVERRUNS = 0x0288,
    CPMAC_RXDMAOVERRUNS = 0x028c,
    CPMAC_TX0HDP = 0x0600,
    CPMAC_TX1HDP = 0x0604,
    CPMAC_TX2HDP = 0x0608,
    CPMAC_TX3HDP = 0x060c,
    CPMAC_TX4HDP = 0x0610,
    CPMAC_TX5HDP = 0x0614,
    CPMAC_TX6HDP = 0x0618,
    CPMAC_TX7HDP = 0x061c,
    CPMAC_RX0HDP = 0x0620,
    CPMAC_RX1HDP = 0x0624,
    CPMAC_RX2HDP = 0x0628,
    CPMAC_RX3HDP = 0x062c,
    CPMAC_RX4HDP = 0x0630,
    CPMAC_RX5HDP = 0x0634,
    CPMAC_RX6HDP = 0x0638,
    CPMAC_RX7HDP = 0x063c,
    CPMAC_TX0CP = 0x0640,
    CPMAC_TX1CP = 0x0644,
    CPMAC_TX2CP = 0x0648,
    CPMAC_TX3CP = 0x064c,
    CPMAC_TX4CP = 0x0650,
    CPMAC_TX5CP = 0x0654,
    CPMAC_TX6CP = 0x0658,
    CPMAC_TX7CP = 0x065c,
    CPMAC_RX0CP = 0x0660,
    CPMAC_RX1CP = 0x0664,
    CPMAC_RX2CP = 0x0668,
    CPMAC_RX3CP = 0x066c,
    CPMAC_RX4CP = 0x0670,
    CPMAC_RX5CP = 0x0674,
    CPMAC_RX6CP = 0x0678,
    CPMAC_RX7CP = 0x067c,
} cpmac_register_t;

typedef enum {
    TXCONTROL_TXEN = BIT(0),
} txcontrol_bit_t;

typedef enum {
    RXCONTROL_RXEN = BIT(0),
} rxcontrol_bit_t;

#if defined(CONFIG_AR7_EMAC)
typedef enum {
    MACINVECTOR_STATUS_INT = BIT(19),
    MACINVECTOR_HOST_INT = BIT(18),
    MACINVECTOR_RX_INT_OR = BIT(17),
    MACINVECTOR_TX_INT_OR = BIT(16),
    MACINVECTOR_RX_INT_VEC = BITS(10, 8),
    MACINVECTOR_TX_INT_VEC = BITS(2, 0),
} mac_in_vec_bit_t;
#else
# error Implementation missing
#endif

typedef enum {
    MACINTSTAT_HOSTPEND = BIT(1),
    MACINTSTAT_STATPEND = BIT(0),
} macintstat_bit_t;

typedef enum {
    RXMBPENABLE_RXPASSCRC = BIT(30),
    RXMBPENABLE_RXQOSEN = BIT(29),
    RXMBPENABLE_RXNOCHAIN = BIT(28),
    RXMBPENABLE_RXCMEMFEN = BIT(24),
    RXMBPENABLE_RXCSFEN = BIT(23),
    RXMBPENABLE_RXCEFEN = BIT(22),
    RXMBPENABLE_RXCAFEN = BIT(21),
    RXMBPENABLE_RXPROMCH = BITS(18, 16),
    RXMBPENABLE_RXBROADEN = BIT(13),
    RXMBPENABLE_RXBROADCH = BITS(10, 8),
    RXMBPENABLE_RXMULTEN = BIT(5),
    RXMBPENABLE_RXMULTCH = BITS(2, 0),
} rxmbpenable_bit_t;

typedef enum {
    MACCONTROL_RXOFFLENBLOCK = BIT(14),
    MACCONTROL_RXOWNERSHIP = BIT(13),
    MACCONTROL_CMDIDLE = BIT(11),
    MACCONTROL_TXPTYPE = BIT(9),
    MACCONTROL_TXPACE = BIT(6),
    MACCONTROL_GMIIEN = BIT(5),
    MACCONTROL_TXFLOWEN = BIT(4),
    MACCONTROL_RXBUFFERFLOWEN = BIT(3),
    MACCONTROL_LOOPBACK = BIT(1),
    MACCONTROL_FULLDUPLEX = BIT(0),
} maccontrol_bit_t;

#undef ENTRY
#define ENTRY(entry) { CPMAC_##entry, #entry }
static const offset_name_t cpmac_addr2reg[] = {
    ENTRY(TXIDVER),
    ENTRY(TXCONTROL),
    ENTRY(TXTEARDOWN),
    ENTRY(RXIDVER),
    ENTRY(RXCONTROL),
    ENTRY(RXTEARDOWN),
    ENTRY(RXMBPENABLE),
    ENTRY(RXUNICASTSET),
    ENTRY(RXUNICASTCLEAR),
    ENTRY(RXMAXLEN),
    ENTRY(RXBUFFEROFFSET),
    ENTRY(RXFILTERLOWTHRESH),
    ENTRY(MACCONTROL),
    ENTRY(MACSTATUS),
    ENTRY(EMCONTROL),
    ENTRY(TXINTSTATRAW),
    ENTRY(TXINTSTATMASKED),
    ENTRY(TXINTMASKSET),
    ENTRY(TXINTMASKCLEAR),
    ENTRY(MACINVECTOR),
    ENTRY(MACEOIVECTOR),
    ENTRY(RXINTSTATRAW),
    ENTRY(RXINTSTATMASKED),
    ENTRY(RXINTMASKSET),
    ENTRY(RXINTMASKCLEAR),
    ENTRY(MACINTSTATRAW),
    ENTRY(MACINTSTATMASKED),
    ENTRY(MACINTMASKSET),
    ENTRY(MACINTMASKCLEAR),
    ENTRY(MACADDRLO_0),
    ENTRY(MACADDRLO_1),
    ENTRY(MACADDRLO_2),
    ENTRY(MACADDRLO_3),
    ENTRY(MACADDRLO_4),
    ENTRY(MACADDRLO_5),
    ENTRY(MACADDRLO_6),
    ENTRY(MACADDRLO_7),
    ENTRY(MACADDRMID),
    ENTRY(MACADDRHI),
    ENTRY(MACHASH1),
    ENTRY(MACHASH2),
    ENTRY(RXGOODFRAMES),
    ENTRY(RXBROADCASTFRAMES),
    ENTRY(RXMULTICASTFRAMES),
    ENTRY(RXPAUSEFRAMES),
    ENTRY(RXCRCERRORS),
    ENTRY(RXALIGNCODEERRORS),
    ENTRY(RXOVERSIZEDFRAMES),
    ENTRY(RXJABBERFRAMES),
    ENTRY(RXUNDERSIZEDFRAMES),
    ENTRY(RXFRAGMENTS),
    ENTRY(RXFILTEREDFRAMES),
    ENTRY(RXQOSFILTEREDFRAMES),
    ENTRY(RXOCTETS),
    ENTRY(TXGOODFRAMES),
    ENTRY(TXBROADCASTFRAMES),
    ENTRY(TXMULTICASTFRAMES),
    ENTRY(TXPAUSEFRAMES),
    ENTRY(TXDEFERREDFRAMES),
    ENTRY(TXCOLLISIONFRAMES),
    ENTRY(TXSINGLECOLLFRAMES),
    ENTRY(TXMULTCOLLFRAMES),
    ENTRY(TXEXCESSIVECOLLISIONS),
    ENTRY(TXLATECOLLISIONS),
    ENTRY(TXUNDERRUN),
    ENTRY(TXCARRIERSENSEERRORS),
    ENTRY(TXOCTETS),
    ENTRY(64OCTETFRAMES),
    ENTRY(65T127OCTETFRAMES),
    ENTRY(128T255OCTETFRAMES),
    ENTRY(256T511OCTETFRAMES),
    ENTRY(512T1023OCTETFRAMES),
    ENTRY(1024TUPOCTETFRAMES),
    ENTRY(NETOCTETS),
    ENTRY(RXSOFOVERRUNS),
    ENTRY(RXMOFOVERRUNS),
    ENTRY(RXDMAOVERRUNS),
    ENTRY(TX0HDP),
    ENTRY(TX1HDP),
    ENTRY(TX2HDP),
    ENTRY(TX3HDP),
    ENTRY(TX4HDP),
    ENTRY(TX5HDP),
    ENTRY(TX6HDP),
    ENTRY(TX7HDP),
    ENTRY(RX0HDP),
    ENTRY(RX1HDP),
    ENTRY(RX2HDP),
    ENTRY(RX3HDP),
    ENTRY(RX4HDP),
    ENTRY(RX5HDP),
    ENTRY(RX6HDP),
    ENTRY(RX7HDP),
    ENTRY(TX0CP),
    ENTRY(TX1CP),
    ENTRY(TX2CP),
    ENTRY(TX3CP),
    ENTRY(TX4CP),
    ENTRY(TX5CP),
    ENTRY(TX6CP),
    ENTRY(TX7CP),
    ENTRY(RX0CP),
    ENTRY(RX1CP),
    ENTRY(RX2CP),
    ENTRY(RX3CP),
    ENTRY(RX4CP),
    ENTRY(RX5CP),
    ENTRY(RX6CP),
    ENTRY(RX7CP),
    { 0 }
};

static const char *cpmac_regname(unsigned offset)
{
    return offset2name(cpmac_addr2reg, offset);
}

static const int cpmac_interrupt[] = { INTERRUPT_CPMAC0, INTERRUPT_CPMAC1 };

static void emac_update_interrupt(unsigned cpmac_index)
{
    uint8_t *cpmac = ar7.cpmac[cpmac_index];
    uint32_t txintmask = reg_read(cpmac, CPMAC_TXINTMASKSET);
    uint32_t txintstat = (reg_read(cpmac, CPMAC_TXINTSTATRAW) & txintmask);
    uint32_t rxintmask = reg_read(cpmac, CPMAC_RXINTMASKSET);
    uint32_t rxintstat = (reg_read(cpmac, CPMAC_RXINTSTATRAW) & rxintmask);
    uint32_t macintmask = reg_read(cpmac, CPMAC_MACINTMASKSET);
    uint32_t macintstat = (reg_read(cpmac, CPMAC_MACINTSTATRAW) & macintmask);
#if defined(CONFIG_AR7_EMAC)
    uint32_t macintvector = (reg_read(cpmac, CPMAC_MACINVECTOR) & 0xffff);
#else
    uint32_t macintvector = (((rxintstat & 0xff) << 8) | (txintstat & 0xff));
#endif
    int enabled;
    reg_write(cpmac, CPMAC_TXINTSTATMASKED, txintstat);
    reg_write(cpmac, CPMAC_RXINTSTATMASKED, rxintstat);
    reg_write(cpmac, CPMAC_MACINTSTATMASKED, macintstat);
    // !!!
    if (txintstat) {
        macintvector |= MACINVECTOR_TX_INT_OR;
#if defined(CONFIG_AR7_EMAC)
    } else {
        macintvector &= ~MACINVECTOR_TX_INT_VEC;
#endif
    }
    if (rxintstat) {
        macintvector |= MACINVECTOR_RX_INT_OR;
#if defined(CONFIG_AR7_EMAC)
    } else {
        macintvector &= ~MACINVECTOR_RX_INT_VEC;
#endif
    }
    if (macintstat & MACINTSTAT_HOSTPEND) {
        macintvector |= MACINVECTOR_HOST_INT;
    }
    if (macintstat & MACINTSTAT_STATPEND) {
        macintvector |= MACINVECTOR_STATUS_INT;
    }
    reg_write(cpmac, CPMAC_MACINVECTOR, macintvector);
    enabled = (txintstat || rxintstat || macintstat);
    qemu_set_irq(AR7_PRIMARY_IRQ(cpmac_interrupt[cpmac_index]), enabled);
}

static void emac_reset(unsigned cpmac_index)
{
    uint8_t *cpmac = ar7.cpmac[cpmac_index];
    memset(cpmac, 0, sizeof(av.cpmac0));
    reg_write(cpmac, CPMAC_TXIDVER, 0x000c0a07);
    reg_write(cpmac, CPMAC_RXIDVER, 0x000c0a07);
    reg_write(cpmac, CPMAC_RXMAXLEN, 1518);
    //~ reg_write(cpmac, CPMAC_MACCONFIG, 0x03030101);
}

#define BD_SOP    MASK(31, 31)
#define BD_EOP    MASK(30, 30)
#define BD_OWNS   MASK(29, 29)

static uint32_t ar7_cpmac_read(unsigned cpmac_index, unsigned offset)
{
    uint8_t *cpmac = ar7.cpmac[cpmac_index];
    uint32_t val = reg_read(cpmac, offset);
    const char *text = cpmac_regname(offset);
    int logflag = CPMAC;
    //~ do_raise_exception(EXCP_DEBUG)
    if (0) {
    } else if (offset == CPMAC_MACINVECTOR) {
        if (val == 0) {
            /* Disable logging of polled default value. */
            /* Linux 2.6.13.1: [scpphy_if_isr_tasklet][tasklet_action] */
            logflag = 0;
        }
    } else {
    }
    TRACE(logflag, logout("cpmac%u[%s] (0x%08x) = 0x%08x %s\n",
                        cpmac_index, text,
                        (AVALANCHE_CPMAC0_BASE + (AVALANCHE_CPMAC1_BASE -
                                         AVALANCHE_CPMAC0_BASE) * cpmac_index + offset),
                        val, backtrace()));
    return val;
}

#if 0
/* Table of CRCs of all 8-bit messages. */
static uint32_t crc_table[256];

/* Flag: has the table been computed? Initially false. */
static int crc_table_computed = 0;

/* Make the table for a fast CRC. */
static void make_crc_table(void)
{
    int n, k;

    for (n = 0; n < 256; n++) {
        uint32_t c = (uint32_t) n;
        for (k = 0; k < 8; k++) {
            if (c & 1)
                c = 0xedb88320L ^ (c >> 1);
            else
                c = c >> 1;
        }
        crc_table[n] = c;
    }
    crc_table_computed = 1;
}

/* Update a running CRC with the bytes buf[0..len-1]--the CRC
  should be initialized to all 1's, and the transmitted value
  is the 1's complement of the final running CRC (see the
  crc() routine below). */

static uint32_t update_crc(uint32_t crc, const uint8_t * buf, int len)
{
    uint32_t c = crc;
    int n;

    if (!crc_table_computed)
        make_crc_table();
    for (n = 0; n < len; n++) {
        c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
    }
    return c;
}

/* Return the CRC of the bytes buf[0..len-1]. */
static uint32_t fcs(const uint8_t * buf, int len)
{
    return update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
}
#endif

static void statusreg_inc(unsigned cpmac_index, unsigned offset)
{
    uint8_t *cpmac = ar7.cpmac[cpmac_index];
    uint32_t value = reg_read(cpmac, offset);
    value++;
    reg_write(cpmac, offset, value);
    if (value >= 0x80000000) {
        reg_set(cpmac, CPMAC_MACINTSTATRAW, MACINTSTAT_STATPEND);
        emac_update_interrupt(cpmac_index);
        MISSING();
    }
}

static void emac_transmit(unsigned cpmac_index, unsigned offset, uint32_t address)
{
    uint8_t *cpmac = ar7.cpmac[cpmac_index];
    uint8_t channel = (offset - CPMAC_TX0HDP) / 4;
    reg_write(cpmac, offset, address);
    if (address == 0) {
    } else if (!(reg_read(cpmac, CPMAC_MACCONTROL) & MACCONTROL_GMIIEN)) {
        TRACE(CPMAC, logout("cpmac%u MII is disabled, frame ignored\n",
          cpmac_index));
    } else if (!(reg_read(cpmac, CPMAC_TXCONTROL) & TXCONTROL_TXEN)) {
        TRACE(CPMAC, logout("cpmac%u transmitter is disabled, frame ignored\n",
          cpmac_index));
    } else {
        uint32_t length = 0;
        uint8_t buffer[MAX_ETH_FRAME_SIZE + 4];
        cpphy_tcb_t tcb;

        loop:

        length = 0;
        cpu_physical_memory_read(address, (uint8_t *) & tcb, sizeof(tcb));
        uint32_t next = le32_to_cpu(tcb.next);
        uint32_t addr = le32_to_cpu(tcb.buff);
        uint32_t bufferlength = le32_to_cpu(tcb.length);
        uint32_t packetlength = le32_to_cpu(tcb.mode);
        uint32_t bufferoffset = bufferlength >> 16;
        uint32_t flags = packetlength & BITS(31, 16);
        bufferlength &= BITS(15, 0);
        packetlength &= BITS(15, 0);

        TRACE(RXTX,
              logout
              ("buffer 0x%08x, next 0x%08x, buff 0x%08x, flags 0x%08x, len 0x%08x, total 0x%08x\n",
               address, next, addr, flags, bufferlength, packetlength));
        assert(length + packetlength <= MAX_ETH_FRAME_SIZE);
        cpu_physical_memory_read(addr, buffer + length, bufferlength);
        length += bufferlength;
        assert(packetlength == bufferlength);
        /* Next assertions normally raise host interrupt. */
        assert(flags & TCB_SOP);
        assert(flags & TCB_EOP);
        //~ assert(flags & TCB_OWNER); // !!!
        if (!(flags & TCB_OWNER)) {
            logout("%s: OWNER flag is not set\n", __func__);
            UNEXPECTED();
        }
        assert(!(flags & TCB_PASSCRC));
        assert(bufferoffset == 0);
        /* Real hardware sets flag when finished, we set it here. */
        flags &= ~(TCB_OWNER);
        flags |= TCB_EOQ;
        stl_phys(address + offsetof(cpphy_tcb_t, mode), flags | packetlength);

        if (ar7.nic[cpmac_index].vc != 0) {
#if 0
            uint32_t crc = fcs(buffer, length);
            TRACE(CPMAC,
                  logout("FCS 0x%04x 0x%04x\n",
                         (uint32_t) crc32(~0, buffer, length - 4), crc));
            crc = htonl(crc);
            memcpy(&buffer[length], &crc, 4);
            length += 4;
#endif
            TRACE(RXTX,
                  logout("cpmac%u sent %u byte: %s\n", cpmac_index, length,
                         dump(buffer, length)));
            qemu_send_packet(ar7.nic[cpmac_index].vc, buffer, length);
        }
        statusreg_inc(cpmac_index, CPMAC_TXGOODFRAMES);
        reg_write(cpmac, offset, next);
        reg_write(cpmac, CPMAC_TX0CP + 4 * channel, address);
        reg_set(cpmac, CPMAC_TXINTSTATRAW, BIT(channel));
#if defined(CONFIG_AR7_EMAC)
        reg_set(cpmac, CPMAC_MACINVECTOR, channel);
#endif
        emac_update_interrupt(cpmac_index);
        //~ break;
        //~ statusreg_inc(cpmac_index, CPMAC_TXBROADCASTFRAMES);
        //~ statusreg_inc(cpmac_index, CPMAC_TXMULTICASTFRAMES);

        if (next != 0) {
            TRACE(RXTX, logout("more data to send...\n"));
            address = next;
            goto loop;
        }
    }
}

static void ar7_cpmac_write(unsigned cpmac_index, unsigned offset,
                            uint32_t val)
{
    uint8_t * cpmac = ar7.cpmac[cpmac_index];
    assert((offset & 3) == 0);
    TRACE(CPMAC, logout("cpmac%u[%s] (0x%08x) = 0x%08lx\n",
                        cpmac_index, cpmac_regname(offset),
                        (AVALANCHE_CPMAC0_BASE +
                                        (AVALANCHE_CPMAC1_BASE -
                                         AVALANCHE_CPMAC0_BASE) * cpmac_index +
                                        offset), (unsigned long)val));
    if (0) {
    } else if (offset == CPMAC_TXTEARDOWN) {
        uint32_t channel = val;
        uint32_t txhdp = reg_read(cpmac, CPMAC_TX0HDP + 4 * channel);
        assert(channel < 8);
        channel &= BITS(2, 0);
        if (txhdp != 0) {
            uint32_t flags = ldl_phys(txhdp + offsetof(cpphy_tcb_t, mode));
            flags |= TCB_TDOWNCMPLT;
            stl_phys(txhdp + offsetof(cpphy_tcb_t, mode), flags);
        }
        reg_write(cpmac, CPMAC_TX0HDP + 4 * channel, 0);
        reg_write(cpmac, CPMAC_TX0CP + 4 * channel, 0xfffffffc);
        reg_set(cpmac, CPMAC_TXINTSTATRAW, BIT(channel));
        emac_update_interrupt(cpmac_index);
    } else if (offset == CPMAC_RXTEARDOWN) {
        uint32_t channel = val;
        uint32_t rxhdp = reg_read(cpmac, CPMAC_RX0HDP + 4 * channel);
        assert(channel < 8);
        channel &= BITS(2, 0);
        if (rxhdp != 0) {
            uint32_t flags = ldl_phys(rxhdp + offsetof(cpphy_rcb_t, mode));
            flags |= RCB_TDOWNCMPLT;
            stl_phys(rxhdp + offsetof(cpphy_rcb_t, mode), flags);
        }
        reg_write(cpmac, CPMAC_RX0HDP + 4 * channel, 0);
        reg_write(cpmac, CPMAC_RX0CP + 4 * channel, 0xfffffffc);
        reg_set(cpmac, CPMAC_RXINTSTATRAW, BIT(channel));
        emac_update_interrupt(cpmac_index);
    } else if (offset == CPMAC_RXMBPENABLE) {
        /* 13 ... 8 = 0x20 enable broadcast */
        reg_write(cpmac, offset, val);
    } else if (offset == CPMAC_RXUNICASTSET) {
        val &= BITS(7, 0);
        val = (reg_read(cpmac, offset) | val);
        //~ assert(val < 2);
        reg_write(cpmac, offset, val);
    } else if (offset == CPMAC_RXUNICASTCLEAR) {
        val = (reg_read(cpmac, CPMAC_RXUNICASTSET) & ~val);
        reg_write(cpmac, CPMAC_RXUNICASTSET, val);
    } else if (offset == CPMAC_RXMAXLEN) {
        TRACE(CPMAC, logout("setting max packet length %u\n", val));
        val &= 0xffff;
        reg_write(cpmac, offset, val);
    } else if (offset == CPMAC_TXINTMASKSET) {
        val &= BITS(7, 0);
        val = (reg_read(cpmac, offset) | val);
        reg_write(cpmac, offset, val);
        emac_update_interrupt(cpmac_index);
    } else if (offset == CPMAC_TXINTMASKCLEAR) {
        val = (reg_read(cpmac, CPMAC_TXINTMASKSET) & ~val);
        reg_write(cpmac, CPMAC_TXINTMASKSET, val);
        emac_update_interrupt(cpmac_index);
    } else if (offset == CPMAC_RXINTMASKSET) {
        val &= BITS(7, 0);
        val = (reg_read(cpmac, offset) | val);
        reg_write(cpmac, offset, val);
        emac_update_interrupt(cpmac_index);
    } else if (offset == CPMAC_RXINTMASKCLEAR) {
        val = (reg_read(cpmac, CPMAC_RXINTMASKSET) & ~val);
        reg_write(cpmac, CPMAC_RXINTMASKSET, val);
        emac_update_interrupt(cpmac_index);
    } else if (offset == CPMAC_MACINTMASKSET) {
        val &= BITS(1, 0);
        val = (reg_read(cpmac, offset) | val);
        reg_write(cpmac, offset, val);
        emac_update_interrupt(cpmac_index);
    } else if (offset == CPMAC_MACINTMASKCLEAR) {
        val = (reg_read(cpmac, CPMAC_MACINTMASKSET) & ~val);
        reg_write(cpmac, CPMAC_MACINTMASKSET, val);
        emac_update_interrupt(cpmac_index);
    } else if (offset == CPMAC_MACADDRHI) {
        /* set MAC address (4 high bytes) */
        uint8_t *phys = ar7.nic[cpmac_index].phys;
        reg_write(cpmac, offset, val);
        phys[5] = cpmac[CPMAC_MACADDRLO_0];
        phys[4] = cpmac[CPMAC_MACADDRMID];
        phys[3] = cpmac[CPMAC_MACADDRHI + 3];
        phys[2] = cpmac[CPMAC_MACADDRHI + 2];
        phys[1] = cpmac[CPMAC_MACADDRHI + 1];
        phys[0] = cpmac[CPMAC_MACADDRHI + 0];
        qemu_format_nic_info_str(ar7.nic[cpmac_index].vc, phys);
        TRACE(CPMAC, logout("setting mac address %s\n",
                            ar7.nic[cpmac_index].vc->info_str));
    } else if (offset >= CPMAC_RXGOODFRAMES && offset <= CPMAC_RXDMAOVERRUNS) {
        /* Write access to statistics register. */
        if (reg_read(cpmac, CPMAC_MACCONTROL) & MACCONTROL_GMIIEN) {
            /* Write-to-decrement mode. */
            uint32_t oldval = reg_read(cpmac, offset);
            if (oldval < val) {
                val = 0;
            } else {
                oldval -= val;
            }
            reg_write(cpmac, offset, val);
        } else {
            /* Normal write direct mode. */
            reg_write(cpmac, offset, val);
        }
    } else if (offset >= CPMAC_TX0HDP && offset <= CPMAC_TX7HDP) {
        /* Transmit buffer. */
        emac_transmit(cpmac_index, offset, val);
    } else if (offset >= CPMAC_RX0HDP && offset <= CPMAC_RX7HDP) {
        reg_write(cpmac, offset, val);
    } else if (offset >= CPMAC_TX0CP && offset <= CPMAC_TX7CP) {
        uint8_t channel = (offset - CPMAC_TX0CP) / 4;
        uint32_t oldval = reg_read(cpmac, offset);
        if (oldval == val) {
            reg_clear(cpmac, CPMAC_TXINTSTATRAW, BIT(channel));
            emac_update_interrupt(cpmac_index);
        }
    } else if (offset >= CPMAC_RX0CP && offset <= CPMAC_RX7CP) {
        uint8_t channel = (offset - CPMAC_RX0CP) / 4;
        uint32_t oldval = reg_read(cpmac, offset);
        if (oldval == val) {
            reg_clear(cpmac, CPMAC_RXINTSTATRAW, BIT(channel));
            emac_update_interrupt(cpmac_index);
        }
    } else {
        //~ logout("???\n");
        reg_write(cpmac, offset, val);
    }
}

/*****************************************************************************
 *
 * EMIF emulation.
 *
 ****************************************************************************/

typedef enum {
    EMIF_REV = 0x00,
    EMIF_GASYNC = 0x04,
    EMIF_DRAMCTL = 0x08,
    EMIF_REFRESH = 0x0c,
    EMIF_ASYNC_CS0 = 0x10,
    EMIF_ASYNC_CS3 = 0x14,
    EMIF_ASYNC_CS4 = 0x18,
    EMIF_ASYNC_CS5 = 0x1c,
} emif_register_t;

static uint32_t ar7_emif_read(unsigned offset)
{
    uint32_t value = reg_read(av.emif, offset);
    TRACE(EMIF, logout("emif[0x%02x] = 0x%08x\n", offset, value));
    return value;
}

static void ar7_emif_write(unsigned offset, uint32_t value)
{
    TRACE(EMIF, logout("emif[0x%02x] = 0x%08x\n", offset, value));
    if (offset == EMIF_REV) {
        /* Revision is readonly. */
        UNEXPECTED();
    } else {
        reg_write(av.emif, offset, value);
    }
}

/*****************************************************************************
 *
 * GPIO emulation.
 *
 ****************************************************************************/

typedef enum {
    GPIO_IN = 0x00,
    GPIO_OUT = 0x04,
    GPIO_DIR = 0x08,
    GPIO_ENABLE = 0x0c,
    GPIO_CVR = 0x14,            /* chip version */
    GPIO_DIDR1 = 0x18,
    GPIO_DIDR2 = 0x1c,
} gpio_t;

static void ar7_led_display(unsigned led_index, int on)
{
  static const uint8_t x[] = { 1, 7, 14, 23, 29 };
  qemu_chr_printf(ar7.gpio_display, "\e[10;%uH\e[%dm \e[m", x[led_index], (on) ? 42 : 40);
}

static void ar7_gpio_display(void)
{
    unsigned bit_index;
    uint32_t in = reg_read(av.gpio, GPIO_IN);
    uint32_t out = reg_read(av.gpio, GPIO_OUT);
    uint32_t dir = reg_read(av.gpio, GPIO_DIR);
    uint32_t enable = reg_read(av.gpio, GPIO_ENABLE);
    char text[32];
    for (bit_index = 0; bit_index < 32; bit_index++) {
        text[bit_index] = (in & BIT(bit_index)) ? '*' : '.';
    }
    qemu_chr_printf(ar7.gpio_display,
                    "\e[5;1H%32.32s (in  0x%08x)",
                    text, in);
    for (bit_index = 0; bit_index < 32; bit_index++) {
        text[bit_index] = (out & BIT(bit_index)) ? '*' : '.';
    }
    qemu_chr_printf(ar7.gpio_display,
                    "\e[6;1H%32.32s (out 0x%08x)",
                    text, out);
    for (bit_index = 0; bit_index < 32; bit_index++) {
        text[bit_index] = (dir & BIT(bit_index)) ? '*' : '.';
    }
    qemu_chr_printf(ar7.gpio_display,
                    "\e[7;1H%32.32s (dir 0x%08x)",
                    text, dir);
    for (bit_index = 0; bit_index < 32; bit_index++) {
        text[bit_index] = (enable & BIT(bit_index)) ? '*' : '.';
    }
    qemu_chr_printf(ar7.gpio_display,
                    "\e[8;1H%32.32s (ena 0x%08x)",
                    text, enable);

    /* LAN LED. */
    ar7_led_display(0, 1);

    /* WLAN LED. */
    ar7_led_display(1, !(out & BIT(6)));

    /* ONLINE LED. */
    ar7_led_display(2, !(out & BIT(13)));

    /* DSL LED. */
    ar7_led_display(3, 0);

    /* POWER LED. */
    ar7_led_display(4, 1);

    /* Hide cursor. */
    qemu_chr_printf(ar7.gpio_display, "\e[20;1H");
}

#undef ENTRY
#define ENTRY(entry) { GPIO_##entry, #entry }
static const offset_name_t gpio_addr2reg[] = {
    ENTRY(IN),
    ENTRY(OUT),
    ENTRY(DIR),
    ENTRY(ENABLE),
    ENTRY(CVR),
    ENTRY(DIDR1),
    ENTRY(DIDR2),
    { 0 }
};

static const char *gpio_regname(unsigned offset)
{
    return offset2name(gpio_addr2reg, offset);
}

static uint32_t ar7_gpio_read(unsigned offset)
{
    uint32_t value = reg_read(av.gpio, offset);
    if (offset == GPIO_IN && value == 0x00000800) {
        /* Do not log polling of reset button. */
        TRACE(GPIO, logout("gpio[%s] = 0x%08x\n", gpio_regname(offset), value));
    } else {
        TRACE(GPIO, logout("gpio[%s] = 0x%08x\n", gpio_regname(offset), value));
    }
    return value;
}

static void ar7_gpio_write(unsigned offset, uint32_t value)
{
    TRACE(GPIO, logout("gpio[%s] = 0x%08x\n", gpio_regname(offset), value));
    reg_write(av.gpio, offset, value);
    if (offset <= GPIO_DIR) {
        ar7_gpio_display();
    }
}

/*****************************************************************************
 *
 * Management Data Input/Output (MDIO) emulation.
 *
 ****************************************************************************/

typedef enum {
    MDIO_VERSION = 0,
    MDIO_CONTROL = 4,
    MDIO_ALIVE = 8,
    MDIO_LINK = 0x0c,
    MDIO_LINKINTRAW = 0x10,
    MDIO_LINKINTMASKED = 0x14,
    MDIO_USERINTRAW = 0x20,
    MDIO_USERINTMASKED = 0x24,
    MDIO_USERINTMASKSET = 0x28,
    MDIO_USERINTMASKCLEAR = 0x2c,
    MDIO_USERACCESS0 = 0x80,
    MDIO_USERPHYSEL0 = 0x84,
    MDIO_USERACCESS1 = 0x88,
    MDIO_USERPHYSEL1 = 0x8c,
} mdio_t;

typedef enum {
    MDIO_VERSION_MODID = BITS(31, 16),
    MDIO_VERSION_REVMAJ = BITS(15, 8),
    MDIO_VERSION_REVMIN = BITS(7, 0),
} mdio_version_bit_t;

typedef enum {
    MDIO_CONTROL_IDLE = BIT(31),
    MDIO_CONTROL_ENABLE = BIT(30),
    MDIO_CONTROL_HIGHEST_USER_CHANNEL = BITS(28, 24),
    MDIO_CONTROL_PREAMBLE = BIT(20),
    MDIO_CONTROL_FAULT = BIT(19),
    MDIO_CONTROL_FAULTENB = BIT(18),
    MDIO_CONTROL_INT_TEST_ENABLE = BIT(17),
    MDIO_CONTROL_CLKDIV = BITS(15, 0),
} mdio_control_bit_t;

typedef enum {
    MDIO_USERACCESS_GO = BIT(31),
    MDIO_USERACCESS_WRITE = BIT(30),
    MDIO_USERACCESS_ACK = BIT(29),
    MDIO_USERACCESS_REGADR = BITS(25, 21),
    MDIO_USERACCESS_PHYADR = BITS(20, 16),
    MDIO_USERACCESS_DATA = BITS(15, 0),
} mdio_useraccess_bit_t;

typedef enum {
    MDIO_USERPHYSEL_LINKSEL = BIT(7),
    MDIO_USERPHYSEL_LINKINTENB = BIT(6),
    MDIO_USERPHYSEL_PHYADRMON = BITS(4, 0),
} mdio_userphysel_bit_t;

#include "phy.c"

static uint32_t mdio_phy_read(unsigned phy_index)
{
    uint32_t val = reg_read(av.mdio, (phy_index == 0) ? MDIO_USERACCESS0 : MDIO_USERACCESS1);
    TRACE(MDIO, logout("mdio[USERACCESS%u] = 0x%08x\n", phy_index, val));
    return val;
}

static void mdio_phy_write(unsigned phy_index, uint32_t val)
{
    unsigned writeflag = (val & MDIO_USERACCESS_WRITE) >> 30;
    unsigned regaddr = (val & MDIO_USERACCESS_REGADR) >> 21;
    unsigned phyaddr = (val & MDIO_USERACCESS_PHYADR) >> 16;
    uint32_t mdio_control = reg_read(av.mdio, MDIO_CONTROL);
    assert(regaddr < 32);
    assert(phyaddr < 32);
    TRACE(MDIO,
          logout
          ("mdio[USERACCESS%u] = 0x%08x, writeflag = %u, reg = %u, phy = %u\n",
           phy_index, val, writeflag, regaddr, phyaddr));
    if (val & MDIO_USERACCESS_GO) {
        val &= (MDIO_USERACCESS_WRITE | MDIO_USERACCESS_REGADR |
                MDIO_USERACCESS_PHYADR | MDIO_USERACCESS_DATA);
        if (!(mdio_control & MDIO_CONTROL_ENABLE)) {
            /* MDIO state machine is not enabled. */
            val = 0;
        } else if (phyaddr == ar7.phyaddr) {
            if (writeflag) {
                phy_write(regaddr, val & MDIO_USERACCESS_DATA);
            } else {
                val = phy_read(regaddr);
                val |= MDIO_USERACCESS_ACK;
                val |= (regaddr << 21);
                val |= (phyaddr << 16);
            }
            reg_set(av.mdio, MDIO_ALIVE, BIT(phyaddr));
        } else {
            val = 0;
            reg_clear(av.mdio, MDIO_ALIVE, BIT(phyaddr));
        }
    }

    reg_write(av.mdio,
              (phy_index == 0) ? MDIO_USERACCESS0 : MDIO_USERACCESS1,
              val);
}

static uint32_t ar7_mdio_read(uint8_t *mdio, unsigned offset)
{
    const char *text = 0;
    uint32_t val = reg_read(mdio, offset);
    if (offset == MDIO_VERSION) {
        text = "VERSION";
//~ cpMacMdioInit(): MDIO_CONTROL = 0x40000138
//~ cpMacMdioInit(): MDIO_CONTROL < 0x40000037
    } else if (offset == MDIO_CONTROL) {
        text = "CONTROL";
    } else if (offset == MDIO_ALIVE) {
        text = "ALIVE";
    } else if (offset == MDIO_LINK) {
        /* Suppress noise logging output from polling of link. */
        //~ if (val != 0x80000000) {
            text = "LINK";
        //~ }
    } else if (offset == MDIO_USERACCESS0) {
        val = mdio_phy_read(0);
    } else if (offset == MDIO_USERACCESS1) {
        val = mdio_phy_read(1);
    } else {
        TRACE(MDIO, logout("mdio[0x%02x] = 0x%08x\n", offset, val));
    }
    if (text) {
        TRACE(MDIO, logout("mdio[%s] = 0x%08x\n", text, val));
    }
    return val;
}

static void ar7_mdio_write(uint8_t *mdio, unsigned offset, uint32_t val)
{
    const char *text = 0;
    if (offset == MDIO_VERSION) {
        text = "VERSION";
        UNEXPECTED();
    } else if (offset == MDIO_CONTROL) {
        uint32_t oldval = reg_read(mdio, offset);
        text = "CONTROL";
        if ((val ^ oldval) & MDIO_CONTROL_ENABLE) {
            if (val & MDIO_CONTROL_ENABLE) {
              TRACE(MDIO, logout("enable MDIO state machine\n"));
              phy_enable();
              reg_write(av.mdio, MDIO_ALIVE, BIT(ar7.phyaddr));
            } else {
              TRACE(MDIO, logout("disable MDIO state machine\n"));
              phy_disable();
            }
        }
        reg_write(mdio, offset, val);
    } else if (offset == MDIO_USERACCESS0) {
        mdio_phy_write(0, val);
    } else if (offset == MDIO_USERACCESS1) {
        mdio_phy_write(1, val);
    } else {
        TRACE(MDIO, logout("mdio[0x%02x] = 0x%08x\n", offset, val));
        reg_write(mdio, offset, val);
    }
    if (text) {
        TRACE(MDIO, logout("mdio[%s] = 0x%08x\n", text, val));
    }
}

/*****************************************************************************
 *
 * Reset emulation.
 *
 ****************************************************************************/

static void ar7_reset_write(uint32_t offset, uint32_t val)
{
    if (offset == 0) {
#if defined(DEBUG_AR7)
        static const char *resetdevice[] = {
            /* 00 */ "uart0", "uart1", "i2c", "timer0",
            /* 04 */ "timer1", "reserved05", "gpio", "adsl",
            /* 08 */ "usb", "atm", "reserved10", "vdma",
            /* 12 */ "fser", "reserved13", "reserved14", "reserved15",
            /* 16 */ "vlynq1", "cpmac0", "mcdma", "bist",
            /* 20 */ "vlynq0", "cpmac1", "mdio", "dsp",
            /* 24 */ "reserved24", "reserved25", "ephy", "reserved27",
            /* 28 */ "reserved28", "reserved29", "reserved30", "reserved31"
        };
        // Reset bit coded device(s). 0 = disabled (reset), 1 = enabled.
        static uint32_t oldval;
        uint32_t changed = (val ^ oldval);
        uint32_t enabled = (changed & val);
        //~ uint32_t disabled = (changed & oldval);
        unsigned i;
        oldval = val;
        for (i = 0; i < 32; i++) {
            if (changed & BIT(i)) {
                TRACE(RESET,
                      logout("reset %sabled %s (0x%08x)\n",
                             (enabled & BIT(i)) ? "en" : "dis",
                             resetdevice[i], val));
            }
        }
#endif
    } else if (offset == 4) {
        TRACE(RESET, logout("reset\n"));
        qemu_system_reset_request();
        //~ CPUState *cpu_env = first_cpu;
        //~ cpu_env->active_tc.PC = 0xbfc00000;
    } else {
        TRACE(RESET, logout("reset[%u]=0x%08x\n", offset, val));
    }
}

/*****************************************************************************
 *
 * Timer emulation.
 *
 ****************************************************************************/

// CTL0             /* control register */
// PRD0             /* period register */
// CNT0             /* counter register */

typedef enum {
    TIMER_CONTROL = 0,
    TIMER_LOAD = 4,
    TIMER_VALUE = 8,
    TIMER_INTERRUPT = 12,
} timer_register_t;

typedef enum {
    TIMER_CONTROL_GO = BIT(0),
    TIMER_CONTROL_MODE = BIT(1),
    TIMER_CONTROL_PRESCALE = BITS(5, 2),
    TIMER_CONTROL_PRESCALE_ENABLE = BIT(15),
} timer_control_bit_t;

static void timer_cb(void *opaque)
{
    ar7_timer_t *timer = (ar7_timer_t *)opaque;

    TRACE(TIMER, logout("timer%d expired\n", timer == &ar7.timer[1]));
    qemu_irq_raise(timer->interrupt);
    if (timer->cyclic) {
        int64_t t = qemu_get_clock(vm_clock);
        qemu_mod_timer(timer->qemu_timer, t + timer->prescale * timer->time);
    }
}

static uint32_t ar7_timer_read(unsigned timer_index, uint32_t addr)
{
    ar7_timer_t *timer = &ar7.timer[timer_index];
    uint32_t val;
    val = reg_read(timer->base, addr);
    TRACE(TIMER, logout("timer%u[%d]=0x%08x\n", timer_index, addr, val));
    return val;
}

static void ar7_timer_write(unsigned timer_index, uint32_t addr, uint32_t val)
{
    ar7_timer_t *timer = &ar7.timer[timer_index];
    TRACE(TIMER, logout("timer%u[%d]=0x%08x\n", timer_index, addr, val));
    reg_write(timer->base, addr, val);
    if (addr == TIMER_CONTROL) {
        timer->cyclic = ((val & TIMER_CONTROL_MODE) != 0);
        if (val & TIMER_CONTROL_PRESCALE_ENABLE) {
            timer->prescale = ((val & TIMER_CONTROL_PRESCALE) >> 2);
            logout("prescale %u\n", timer->prescale);
        } else {
            timer->prescale = 1;
        }
        if (val & TIMER_CONTROL_GO) {
            int64_t t = qemu_get_clock(vm_clock);
            qemu_mod_timer(timer->qemu_timer, t + timer->prescale * timer->time);
        } else {
            qemu_del_timer(timer->qemu_timer);
        }
    } else if (addr == TIMER_LOAD) {
        timer->time = val * (ticks_per_sec / io_frequency);
    }
}

/*****************************************************************************
 *
 * UART emulation.
 *
 ****************************************************************************/

static const char *const uart_read_names[] = {
    "RBR",
    "IER",
    "IIR",
    "LCR",
    "MCR",
    "LSR",
    "MSR",
    "SCR",
    "DLL",
    "DLM",
};

static const char *const uart_write_names[] = {
    "TBR",
    "IER",
    "FCR",
    "LCR",
    "MCR",
    "LSR",
    "MSR",
    "SCR",
    "DLL",
    "DLM",
};

#define UART_MEM_TO_IO(addr)    (((addr) - AVALANCHE_UART0_BASE) / 4)

static const target_phys_addr_t uart_base[] = {
  AVALANCHE_UART0_BASE, AVALANCHE_UART1_BASE
};

static const int uart_interrupt[] = {
  INTERRUPT_SERIAL0, INTERRUPT_SERIAL1
};

/* Status of DLAB bit. */
static uint32_t dlab[2];

static inline unsigned uart_name_index(unsigned uart_index, unsigned reg)
{
    if (reg < 2 && dlab[uart_index]) {
        reg += 8;
    }
    return reg;
}

static uint32_t uart_read(unsigned uart_index, uint32_t addr)
{
    uint32_t val;
    int port = UART_MEM_TO_IO(addr);
    unsigned reg = port;
    if (uart_index == 1) {
        reg -= UART_MEM_TO_IO(AVALANCHE_UART1_BASE);
    }
    assert(reg < 8);
    val = serial_mm_readb(ar7.serial[uart_index], addr);
    //~ if (reg != 5) {
        TRACE(UART, logout("uart%u[%s]=0x%08x\n", uart_index,
            uart_read_names[uart_name_index(uart_index, reg)], val));
    //~ }
    return val;
}

static void uart_write(unsigned uart_index, uint32_t addr, uint32_t val)
{
    int port = UART_MEM_TO_IO(addr);
    unsigned reg = port;
    if (uart_index == 1) {
        reg -= UART_MEM_TO_IO(AVALANCHE_UART1_BASE);
    }
    assert(reg < 8);
    //~ if (reg != 0 || dlab[uart_index]) {
        TRACE(UART, logout("uart%u[%s]=0x%08x\n", uart_index,
            uart_write_names[uart_name_index(uart_index, reg)], val));
    //~ }
    if (reg == 3) {
        dlab[uart_index] = (val & 0x80);
    }
    serial_mm_writeb(ar7.serial[uart_index], addr, val);
}

/*****************************************************************************
 *
 * VLYNQ emulation.
 *
 ****************************************************************************/

static const char *const vlynq_names[] = {
    /* 0x00 */
    "Revision",
    "Control",
    "Status",
    "Interrupt Priority Vector Status/Clear",
    /* 0x10 */
    "Interrupt Status/Clear",
    "Interrupt Pending/Set",
    "Interrupt Pointer",
    "Tx Address Map",
    /* 0x20 */
    "Rx Address Map Size 1",
    "Rx Address Map Offset 1",
    "Rx Address Map Size 2",
    "Rx Address Map Offset 2",
    /* 0x30 */
    "Rx Address Map Size 3",
    "Rx Address Map Offset 3",
    "Rx Address Map Size 4",
    "Rx Address Map Offset 4",
    /* 0x40 */
    "Chip Version",
    "Auto Negotiation",
    "Manual Negotiation",
    "Negotiation Status",
    /* 0x50 */
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    /* 0x60 */
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    /* 0x70 */
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    /* 0x80 */
    "Remote Revision",
    "Remote Control",
    "Remote Status",
    "Remote Interrupt Priority Vector Status/Clear",
    /* 0x90 */
    "Remote Interrupt Status/Clear",
    "Remote Interrupt Pending/Set",
    "Remote Interrupt Pointer",
    "Remote Tx Address Map",
    /* 0xa0 */
    "Remote Rx Address Map Size 1",
    "Remote Rx Address Map Offset 1",
    "Remote Rx Address Map Size 2",
    "Remote Rx Address Map Offset 2",
    /* 0xb0 */
    "Remote Rx Address Map Size 3",
    "Remote Rx Address Map Offset 3",
    "Remote Rx Address Map Size 4",
    "Remote Rx Address Map Offset 4",
    /* 0xc0 */
    "Remote Chip Version",
    "Remote Auto Negotiation",
    "Remote Manual Negotiation",
    "Remote Negotiation Status",
    /* 0xd0 */
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    /* 0xe0 */
    "Remote Interrupt Vector 03-00",
    "Remote Interrupt Vector 07-04",
    "Remote Interrupt Vector 11-08",
    "Remote Interrupt Vector 15-12",
    "Remote Interrupt Vector 19-16",
    "Remote Interrupt Vector 23-20",
    "Remote Interrupt Vector 27-24",
    "Remote Interrupt Vector 31-28",
};

typedef enum {
    VLYNQ_REVID = 0x00,
    VLYNQ_CTRL = 0x04,
    VLYNQ_STAT = 0x08,
    VLYNQ_INTPRI = 0x0c,
    VLYNQ_INTSTATCLR = 0x10,
    VLYNQ_INTPENDSET = 0x14,
    VLYNQ_INTPTR = 0x18,
    VLYNQ_XAM = 0x1c,
    VLYNQ_RAMS1 = 0x20,
    VLYNQ_RAMO1 = 0x24,
    VLYNQ_RAMS2 = 0x28,
    VLYNQ_RAMO2 = 0x2c,
    VLYNQ_RAMS3 = 0x30,
    VLYNQ_RAMO3 = 0x34,
    VLYNQ_RAMS4 = 0x38,
    VLYNQ_RAMO4 = 0x3c,
    VLYNQ_CHIPVER = 0x40,
    VLYNQ_AUTNGO = 0x44,
    VLYNQ_RREVID = 0x80,
    VLYNQ_RCTRL = 0x84,
    VLYNQ_RSTAT = 0x88,
    VLYNQ_RINTPRI = 0x8c,
    VLYNQ_RINTSTATCLR = 0x90,
    VLYNQ_RINTPENDSET = 0x94,
    VLYNQ_RINTPTR = 0x98,
    VLYNQ_RXAM = 0x9c,
    VLYNQ_RRAMS1 = 0xa0,
    VLYNQ_RRAMO1 = 0xa4,
    VLYNQ_RRAMS2 = 0xa8,
    VLYNQ_RRAMO2 = 0xac,
    VLYNQ_RRAMS3 = 0xb0,
    VLYNQ_RRAMO3 = 0xb4,
    VLYNQ_RRAMS4 = 0xb8,
    VLYNQ_RRAMO4 = 0xbc,
    VLYNQ_RCHIPVER = 0xc0,
    VLYNQ_RAUTNGO = 0xc4,
    VLYNQ_RMANNGO = 0xc8,
    VLYNQ_RNGOSTAT = 0xcc,
    VLYNQ_RINTVEC0 = 0xe0,
    VLYNQ_RINTVEC1 = 0xe4,
    VLYNQ_RINTVEC2 = 0xe8,
    VLYNQ_RINTVEC3 = 0xec,
    VLYNQ_RINTVEC4 = 0xf0,
    VLYNQ_RINTVEC5 = 0xf4,
    VLYNQ_RINTVEC6 = 0xf8,
    VLYNQ_RINTVEC7 = 0xfc,
} vlynq_register_t;

#if 0
struct _vlynq_registers_half {

    /*--- 0x00 Revision/ID Register ---*/
    unsigned int Revision_ID;

    /*--- 0x04 Control Register ---*/
    union __vlynq_Control {
        struct _vlynq_Control {
#define VLYNQ_CTL_CTRL_SHIFT            0
            unsigned int reset:1;
#define VLYNQ_CTL_ILOOP_SHIFT           1
            unsigned int iloop:1;
#define VLYNQ_CTL_AOPT_DISABLE_SHIFT    2
            unsigned int aopt_disable:1;
            unsigned int reserved1:4;
#define VLYNQ_CTL_INT2CFG_SHIFT         7
            unsigned int int2cfg:1;
#define VLYNQ_CTL_INTVEC_SHIFT          8
            unsigned int intvec:5;
#define VLYNQ_CTL_INTEN_SHIFT           13
            unsigned int intenable:1;
#define VLYNQ_CTL_INTLOCAL_SHIFT        14
            unsigned int intlocal:1;
#define VLYNQ_CTL_CLKDIR_SHIFT          15
            unsigned int clkdir:1;
#define VLYNQ_CTL_CLKDIV_SHIFT          16
            unsigned int clkdiv:3;
            unsigned int reserved2:2;
#define VLYNQ_CTL_TXFAST_SHIFT          21
            unsigned int txfastpath:1;
#define VLYNQ_CTL_RTMEN_SHIFT           22
            unsigned int rtmenable:1;
#define VLYNQ_CTL_RTMVALID_SHIFT        23
            unsigned int rtmvalidwr:1;
#define VLYNQ_CTL_RTMSAMPLE_SHIFT       24
            unsigned int rxsampleval:3;
            unsigned int reserved3:3;
#define VLYNQ_CTL_SCLKUDIS_SHIFT        30
            unsigned int sclkpudis:1;
#define VLYNQ_CTL_PMEM_SHIFT            31
            unsigned int pmen:1;
        } Bits;
        volatile unsigned int Register;
    } Control;

    /*--- 0x08 Status Register ---*/
    union __vlynq_Status {
        struct _vlynq_Status {
            unsigned int link:1;
            unsigned int mpend:1;
            unsigned int spend:1;
            unsigned int nfempty0:1;
            unsigned int nfempty1:1;
            unsigned int nfempty2:1;
            unsigned int nfempty3:1;
            unsigned int lerror:1;
            unsigned int rerror:1;
            unsigned int oflow:1;
            unsigned int iflow:1;
            unsigned int rtm:1;
            unsigned int rxcurrent_sample:3;
            unsigned int reserved1:5;
            unsigned int swidthout:4;
            unsigned int swidthin:4;
            unsigned int reserved2:4;
        } Bits;
        volatile unsigned int Register;
    } Status;

    /*--- 0x0C Interrupt Priority Vector Status/Clear Register ---*/
    union __vlynq_Interrupt_Priority {
        struct _vlynq_Interrupt_Priority {
            unsigned int intstat:5;
            unsigned int reserved:(32 - 5 - 1);
            unsigned int nointpend:1;
        } Bits;
        volatile unsigned int Register;
    } Interrupt_Priority;

    /*--- 0x10 Interrupt Status/Clear Register ---*/
    volatile unsigned int Interrupt_Status;

    /*--- 0x14 Interrupt Pending/Set Register ---*/
    volatile unsigned int Interrupt_Pending_Set;

    /*--- 0x18 Interrupt Pointer Register ---*/
    volatile unsigned int Interrupt_Pointer;

    /*--- 0x1C Tx Address Map ---*/
    volatile unsigned int Tx_Address;

    /*--- 0x20 Rx Address Map Size 1 ---*/
    /*--- 0x24 Rx Address Map Offset 1 ---*/
    /*--- 0x28 Rx Address Map Size 2 ---*/
    /*--- 0x2c Rx Address Map Offset 2 ---*/
    /*--- 0x30 Rx Address Map Size 3 ---*/
    /*--- 0x34 Rx Address Map Offset 3 ---*/
    /*--- 0x38 Rx Address Map Size 4 ---*/
    /*--- 0x3c Rx Address Map Offset 4 ---*/
    struct ___vlynq_Rx_Address Rx_Address[4];

    /*--- 0x40 Chip Version Register ---*/
    struct ___vlynq_Chip_Version Chip_Version;

    /*--- 0x44 Auto Negotiation Register ---*/
    union __Auto_Negotiation {
        struct _Auto_Negotiation {
            unsigned int reserved1:16;
            unsigned int _2_x:1;
            unsigned int reserved2:15;
        } Bits;
        volatile unsigned int Register;
    } Auto_Negotiation;

    /*--- 0x48 Manual Negotiation Register ---*/
    volatile unsigned int Manual_Negotiation;

    /*--- 0x4C Negotiation Status Register ---*/
    union __Negotiation_Status {
        struct _Negotiation_Status {
            unsigned int status:1;
            unsigned int reserved:31;
        } Bits;
        volatile unsigned int Register;
    } Negotiation_Status;

    /*--- 0x50-0x5C Reserved ---*/
    unsigned char reserved1[0x5C - 0x4C];

    /*--- 0x60 Interrupt Vector 3-0 ---*/
    union __vlynq_Interrupt_Vector Interrupt_Vector_1;

    /*--- 0x64 Interrupt Vector 7-4 ---*/
    union __vlynq_Interrupt_Vector Interrupt_Vector_2;

    /*--- 0x68-0x7C Reserved for Interrupt Vectors 8-31 ---*/
    unsigned char reserved2[0x7C - 0x64];
};
#endif

static uint32_t ar7_vlynq_read(unsigned vlynq_index, unsigned offset)
{
    uint8_t *vlynq = ar7.vlynq[vlynq_index];
    uint32_t val = reg_read(vlynq, offset);
    TRACE(VLYNQ, logout("vlynq%u[0x%02x (%s)] = 0x%08lx\n",
                        vlynq_index, offset,
                        vlynq_names[offset / 4],
                        (unsigned long)val));
    if (offset == VLYNQ_REVID) {
        val = cpu_to_le32(0x00010206);
    } else if (offset == VLYNQ_INTSTATCLR) {
        reg_write(vlynq, offset, 0);
    } else if (offset == VLYNQ_RCHIPVER && vlynq_index == ar7.vlynq_tnetw1130) {
        val = cpu_to_le32(0x00000009);
    } else {
    }
    return val;
}

static void ar7_vlynq_write(unsigned vlynq_index, unsigned offset, uint32_t val)
{
    uint8_t *vlynq = ar7.vlynq[vlynq_index];
    TRACE(VLYNQ, logout("vlynq%u[0x%02x (%s)] = 0x%08lx\n",
                        vlynq_index, offset,
                        vlynq_names[offset / 4],
                        (unsigned long)val));
    if (offset == VLYNQ_REVID) {
    } else if (offset == VLYNQ_CTRL && vlynq_index == ar7.vlynq_tnetw1130) {
        /* Control and first vlynq emulates an established link. */
        if (!(val & BIT(0))) {
            /* Normal operation. Emulation sets link bit in status register. */
            reg_set(vlynq, VLYNQ_STAT, BIT(0));
            reg_set(vlynq, VLYNQ_RSTAT, BIT(0));
        } else {
            /* Reset. */
            reg_clear(vlynq, VLYNQ_STAT, BIT(0));
            reg_clear(vlynq, VLYNQ_RSTAT, BIT(0));
        }
    } else {
    }
    reg_write(vlynq, offset, val);
}

/*****************************************************************************
 *
 * Watchdog timer emulation.
 *
 * This watchdog timer module has prescalar and counter which divide the input
 * reference frequency and upon expiration, the system is reset.
 *
 *                        ref_freq
 * Reset freq = ---------------------
 *                  (prescalar * counter)
 *
 * This watchdog timer supports timer values in mSecs. Thus
 *
 *           prescalar * counter * 1 KHZ
 * mSecs =   --------------------------
 *                  ref_freq
 *
 ****************************************************************************/

#define KHZ                         1000
#define KICK_VALUE                  1

#define KICK_LOCK_1ST_STAGE         0x5555
#define KICK_LOCK_2ND_STAGE         0xAAAA
#define PRESCALE_LOCK_1ST_STAGE     0x5A5A
#define PRESCALE_LOCK_2ND_STAGE     0xA5A5
#define CHANGE_LOCK_1ST_STAGE       0x6666
#define CHANGE_LOCK_2ND_STAGE       0xBBBB
#define DISABLE_LOCK_1ST_STAGE      0x7777
#define DISABLE_LOCK_2ND_STAGE      0xCCCC
#define DISABLE_LOCK_3RD_STAGE      0xDDDD

typedef struct {
    uint32_t kick_lock;         /* 0x00 */
    uint32_t kick;              /* 0x04 */
    uint32_t change_lock;       /* 0x08 */
    uint32_t change;            /* 0x0c */
    uint32_t disable_lock;      /* 0x10 */
    uint32_t disable;           /* 0x14 */
    uint32_t prescale_lock;     /* 0x18 */
    uint32_t prescale;          /* 0x1c */
} wdtimer_t;

static void watchdog_trigger(void)
{
    wdtimer_t *wdt = (wdtimer_t *) & av.watchdog;
    if (wdt->disable == 0) {
        TRACE(WDOG, logout("disabled watchdog\n"));
        qemu_del_timer(ar7.wd_timer);
    } else {
        int64_t t = ((uint64_t)wdt->change * (uint64_t)wdt->prescale) * (ticks_per_sec / io_frequency);
        //~ logout("change   = 0x%x\n", wdt->change);
        //~ logout("prescale = 0x%x\n", wdt->prescale);
        TRACE(WDOG, logout("trigger value = %u ms\n", (unsigned)(t * 1000 / ticks_per_sec)));
        //~ logout("trigger value = %u\n", (unsigned)(ticks_per_sec / 1000000));
        qemu_mod_timer(ar7.wd_timer, qemu_get_clock(vm_clock) + t);
    }
}

static inline uint16_t wd_val(uint16_t val, uint16_t bits)
{
    return ((val & ~0x3) | bits);
}

static void ar7_wdt_write(unsigned offset, uint32_t val)
{
    wdtimer_t *wdt = (wdtimer_t *) & av.watchdog;
    if (offset == offsetof(wdtimer_t, kick_lock)) {
        if (val == KICK_LOCK_1ST_STAGE) {
            TRACE(WDOG, logout("kick lock 1st stage\n"));
            wdt->kick_lock = wd_val(val, 1);
        } else if (val == KICK_LOCK_2ND_STAGE) {
            TRACE(WDOG, logout("kick lock 2nd stage\n"));
            wdt->kick_lock = wd_val(val, 3);
        } else {
            TRACE(WDOG,
                  logout("kick lock unexpected value 0x%08x, %s\n", val,
                         backtrace()));
        }
    } else if (offset == offsetof(wdtimer_t, kick)) {
        if (wdt->kick_lock != wd_val(KICK_LOCK_2ND_STAGE, 3)) {
            TRACE(WDOG, logout("kick still locked!\n"));
            UNEXPECTED();
        } else if (val == KICK_VALUE) {
            TRACE(WDOG, logout("kick (restart) watchdog\n"));
            watchdog_trigger();
        } else {
            UNEXPECTED();
        }
    } else if (offset == offsetof(wdtimer_t, change_lock)) {
        if (val == CHANGE_LOCK_1ST_STAGE) {
            TRACE(WDOG, logout("change lock 1st stage\n"));
            wdt->change_lock = wd_val(val, 1);
        } else if (val == CHANGE_LOCK_2ND_STAGE) {
            TRACE(WDOG, logout("change lock 2nd stage\n"));
            wdt->change_lock = wd_val(val, 3);
        } else {
            TRACE(WDOG,
                  logout("change lock unexpected value 0x%08x, %s\n", val,
                         backtrace()));
        }
    } else if (offset == offsetof(wdtimer_t, change)) {
        if (wdt->change_lock != wd_val(CHANGE_LOCK_2ND_STAGE, 3)) {
            TRACE(WDOG, logout("change still locked!\n"));
            UNEXPECTED();
        } else {
            TRACE(WDOG, logout("change watchdog, val=0x%08x\n", val));
            wdt->change = val;
        }
    } else if (offset == offsetof(wdtimer_t, disable_lock)) {
        if (val == DISABLE_LOCK_1ST_STAGE) {
            TRACE(WDOG, logout("disable lock 1st stage\n"));
            wdt->disable_lock = wd_val(val, 1);
        } else if (val == DISABLE_LOCK_2ND_STAGE) {
            TRACE(WDOG, logout("disable lock 2nd stage\n"));
            wdt->disable_lock = wd_val(val, 2);
        } else if (val == DISABLE_LOCK_3RD_STAGE) {
            TRACE(WDOG, logout("disable lock 3rd stage\n"));
            wdt->disable_lock = wd_val(val, 3);
        } else {
            TRACE(WDOG,
                  logout("disable lock unexpected value 0x%08x, %s\n", val,
                         backtrace()));
        }
    } else if (offset == offsetof(wdtimer_t, disable)) {
        if (wdt->disable_lock != wd_val(DISABLE_LOCK_3RD_STAGE, 3)) {
            TRACE(WDOG, logout("disable still locked, val=0x%08x!\n", val));
            UNEXPECTED();
        } else {
            TRACE(WDOG,
                  logout("%sable watchdog, val=0x%08x\n", val ? "en" : "dis", val));
            wdt->disable = val;
            watchdog_trigger();
        }
    } else if (offset == offsetof(wdtimer_t, prescale_lock)) {
        if (val == PRESCALE_LOCK_1ST_STAGE) {
            TRACE(WDOG, logout("prescale lock 1st stage\n"));
            wdt->prescale_lock = wd_val(val, 1);
        } else if (val == PRESCALE_LOCK_2ND_STAGE) {
            TRACE(WDOG, logout("prescale lock 2nd stage\n"));
            wdt->prescale_lock = wd_val(val, 3);
        } else {
            TRACE(WDOG,
                  logout("prescale lock unexpected value 0x%08x, %s\n", val,
                         backtrace()));
        }
    } else if (offset == offsetof(wdtimer_t, prescale)) {
        if (wdt->prescale_lock != wd_val(PRESCALE_LOCK_2ND_STAGE, 3)) {
            TRACE(WDOG, logout("prescale still locked, val=0x%08x!\n", val));
            UNEXPECTED();
        } else {
            TRACE(WDOG, logout("set watchdog prescale, val=0x%08x\n", val));    // val = 0xffff
            wdt->prescale = val;
        }
    } else {
        TRACE(WDOG,
              logout("??? offset 0x%02x = 0x%08x, %s\n", offset, val,
                     backtrace()));
    }
}

static void watchdog_cb(void *opaque)
{
    CPUState *env = opaque;

    logout("watchdog expired\n");
    env->exception_index = EXCP_NMI;
    env->error_code = 0;
    do_interrupt(env);
}

/*****************************************************************************
 *
 * Generic AR7 hardware emulation.
 *
 ****************************************************************************/

#define INRANGE(base, var) \
        (((addr) >= (base)) && ((addr) < ((base) + (sizeof(var)) - 1)))

#define VALUE(base, var) var[(addr - (base)) / 4]

static uint32_t ar7_io_memread(void *opaque, uint32_t addr)
{
    uint32_t val = 0xffffffff;
    const char *name = 0;
    int logflag = OTHER;

    assert(!(addr & 3));

    if (INRANGE(AVALANCHE_ADSLSSYS_MEM_BASE, av.adsl)) {
        name = "adsl";
        val = VALUE(AVALANCHE_ADSLSSYS_MEM_BASE, av.adsl);
    } else if (INRANGE(AVALANCHE_BBIF_BASE, av.bbif)) {
        name = "bbif";
        val = VALUE(AVALANCHE_BBIF_BASE, av.bbif);
    } else if (INRANGE(AVALANCHE_ATM_SAR_BASE, av.atmsar)) {
        unsigned offset = (addr - AVALANCHE_ATM_SAR_BASE);
        name = "atm sar";
        val = VALUE(AVALANCHE_ATM_SAR_BASE, av.atmsar);
        if (val == 0 && offset == 0x90) {
          val = 0x80000000;
        }
    } else if (INRANGE(AVALANCHE_USB_MEM_BASE, av.usbslave)) {
        name = "usb memory";
        val = VALUE(AVALANCHE_USB_MEM_BASE, av.usbslave);
    } else if (INRANGE(AVALANCHE_VLYNQ1_REGION0_BASE, av.vlynq1region0)) {
        name = "vlynq1 region 0";
        logflag = VLYNQ;
        val = VALUE(AVALANCHE_VLYNQ1_REGION0_BASE, av.vlynq1region0);
    } else if (INRANGE(AVALANCHE_VLYNQ1_REGION1_BASE, av.vlynq1region1)) {
        name = "vlynq1 region 1";
        logflag = VLYNQ;
        val = VALUE(AVALANCHE_VLYNQ1_REGION1_BASE, av.vlynq1region1);
    } else if (INRANGE(AVALANCHE_CPMAC0_BASE, av.cpmac0)) {
        logflag = 0;
        val = ar7_cpmac_read(0, addr - AVALANCHE_CPMAC0_BASE);
    } else if (INRANGE(AVALANCHE_EMIF_BASE, av.emif)) {
        logflag = 0;
        val = ar7_emif_read(addr - AVALANCHE_EMIF_BASE);
    } else if (INRANGE(AVALANCHE_GPIO_BASE, av.gpio)) {
        logflag = 0;
        val = ar7_gpio_read(addr - AVALANCHE_GPIO_BASE);
    } else if (INRANGE(AVALANCHE_CLOCK_BASE, av.clock_control)) {
        logflag = 0;
        val = clock_read(addr - AVALANCHE_CLOCK_BASE);
    } else if (INRANGE(AVALANCHE_WATCHDOG_BASE, av.watchdog)) {
        name = "watchdog";
        logflag = WDOG;
        val = VALUE(AVALANCHE_WATCHDOG_BASE, av.watchdog);
    } else if (INRANGE(AVALANCHE_TIMER0_BASE, av.timer0)) {
        logflag = 0;
        val = ar7_timer_read(0, addr - AVALANCHE_TIMER0_BASE);
    } else if (INRANGE(AVALANCHE_TIMER1_BASE, av.timer1)) {
        logflag = 0;
        val = ar7_timer_read(1, addr - AVALANCHE_TIMER1_BASE);
    } else if (INRANGE(AVALANCHE_UART0_BASE, av.uart0)) {
        logflag = 0;
        val = uart_read(0, addr);
    } else if (INRANGE(AVALANCHE_UART1_BASE, av.uart1)) {
        logflag = 0;
        val = uart_read(1, addr);
    } else if (INRANGE(AVALANCHE_USB_SLAVE_BASE, av.usb)) {
        name = "usb slave";
        val = VALUE(AVALANCHE_USB_SLAVE_BASE, av.usb);
    } else if (INRANGE(AVALANCHE_RESET_BASE, av.reset_control)) {
        name = "reset control";
        logflag = RESET;
        val = VALUE(AVALANCHE_RESET_BASE, av.reset_control);
    } else if (INRANGE(AVALANCHE_DCL_BASE, av.dcl)) {
        logflag = 0;
        val = ar7_dcl_read(addr - AVALANCHE_DCL_BASE);
    } else if (INRANGE(AVALANCHE_VLYNQ0_BASE, av.vlynq0)) {
        logflag = 0;
        val = ar7_vlynq_read(0, addr - AVALANCHE_VLYNQ0_BASE);
    } else if (INRANGE(AVALANCHE_VLYNQ1_BASE, av.vlynq1)) {
        logflag = 0;
        val = ar7_vlynq_read(1, addr - AVALANCHE_VLYNQ1_BASE);
    } else if (INRANGE(AVALANCHE_MDIO_BASE, av.mdio)) {
        logflag = 0;
        val = ar7_mdio_read(av.mdio, addr - AVALANCHE_MDIO_BASE);
    } else if (INRANGE(OHIO_WDT_BASE, av.wdt)) {
        name = "ohio wdt";
        val = VALUE(OHIO_WDT_BASE, av.wdt);
    } else if (INRANGE(AVALANCHE_INTC_BASE, av.intc)) {
        logflag = 0;
        val = ar7_intc_read(addr - AVALANCHE_INTC_BASE);
    } else if (INRANGE(AVALANCHE_CPMAC1_BASE, av.cpmac1)) {
        logflag = 0;
        val = ar7_cpmac_read(1, addr - AVALANCHE_CPMAC1_BASE);
    } else {
        //~ name = "???";
        logflag = 0;
        {
            logout("addr 0x%08x (???" ") = 0x%08x\n", addr, val);
            MISSING();
        }
    }
    if (name != 0) {
        TRACE(logflag, logout("addr 0x%08lx (%s) = 0x%08x\n",
                              (unsigned long)addr, name, val));
    }
    return val;
}

static void ar7_io_memwrite(void *opaque, uint32_t addr, uint32_t val)
{
    const char *name = 0;
    int logflag = OTHER;

    if (addr & 3) {
        logout("??? addr 0x%08x\n", addr);
        assert(!(addr & 3));
    }

    if (INRANGE(AVALANCHE_ADSLSSYS_MEM_BASE, av.adsl)) {
        name = "adsl";
        VALUE(AVALANCHE_ADSLSSYS_MEM_BASE, av.adsl) = val;
    } else if (INRANGE(AVALANCHE_BBIF_BASE, av.bbif)) {
        name = "bbif";
        VALUE(AVALANCHE_BBIF_BASE, av.bbif) = val;
    } else if (INRANGE(AVALANCHE_ATM_SAR_BASE, av.atmsar)) {
        name = "atm sar";
        VALUE(AVALANCHE_ATM_SAR_BASE, av.atmsar) = val;
    } else if (INRANGE(AVALANCHE_USB_MEM_BASE, av.usbslave)) {
        name = "usb memory";
        //~ VALUE(AVALANCHE_USB_MEM_BASE, av.usbslave) = val;
        VALUE(AVALANCHE_USB_MEM_BASE, av.usbslave) = 0xffffffff;
    } else if (INRANGE(AVALANCHE_VLYNQ1_REGION0_BASE, av.vlynq1region0)) {
        name = "vlynq1 region 0";
        logflag = VLYNQ;
        VALUE(AVALANCHE_VLYNQ1_REGION0_BASE, av.vlynq1region0) = val;
    } else if (INRANGE(AVALANCHE_VLYNQ1_REGION1_BASE, av.vlynq1region1)) {
        name = "vlynq1 region 1";
        logflag = VLYNQ;
        VALUE(AVALANCHE_VLYNQ1_REGION1_BASE, av.vlynq1region1) = val;
    } else if (INRANGE(AVALANCHE_CPMAC0_BASE, av.cpmac0)) {
        logflag = 0;
        ar7_cpmac_write(0, addr - AVALANCHE_CPMAC0_BASE, val);
    } else if (INRANGE(AVALANCHE_EMIF_BASE, av.emif)) {
        logflag = 0;
        ar7_emif_write(addr - AVALANCHE_EMIF_BASE, val);
    } else if (INRANGE(AVALANCHE_GPIO_BASE, av.gpio)) {
        logflag = 0;
        ar7_gpio_write(addr - AVALANCHE_GPIO_BASE, val);
    } else if (INRANGE(AVALANCHE_CLOCK_BASE, av.clock_control)) {
        logflag = 0;
        clock_write(addr - AVALANCHE_CLOCK_BASE, val);
    } else if (INRANGE(AVALANCHE_WATCHDOG_BASE, av.watchdog)) {
        logflag = 0;
        ar7_wdt_write(addr - AVALANCHE_WATCHDOG_BASE, val);
    } else if (INRANGE(AVALANCHE_TIMER0_BASE, av.timer0)) {
        logflag = 0;
        ar7_timer_write(0, addr - AVALANCHE_TIMER0_BASE, val);
    } else if (INRANGE(AVALANCHE_TIMER1_BASE, av.timer1)) {
        logflag = 0;
        ar7_timer_write(1, addr - AVALANCHE_TIMER1_BASE, val);
    } else if (INRANGE(AVALANCHE_UART0_BASE, av.uart0)) {
        logflag = 0;
        uart_write(0, addr, val);
    } else if (INRANGE(AVALANCHE_UART1_BASE, av.uart1)) {
        logflag = 0;
        uart_write(1, addr, val);
    } else if (INRANGE(AVALANCHE_USB_SLAVE_BASE, av.usb)) {
        name = "usb slave";
        VALUE(AVALANCHE_USB_SLAVE_BASE, av.usb) = val;
    } else if (INRANGE(AVALANCHE_RESET_BASE, av.reset_control)) {
        logflag = 0;
        VALUE(AVALANCHE_RESET_BASE, av.reset_control) = val;
        ar7_reset_write(addr - AVALANCHE_RESET_BASE, val);
    } else if (INRANGE(AVALANCHE_DCL_BASE, av.dcl)) {
        logflag = 0;
        ar7_dcl_write(addr - AVALANCHE_DCL_BASE, val);
    } else if (INRANGE(AVALANCHE_VLYNQ0_BASE, av.vlynq0)) {
        logflag = 0;
        ar7_vlynq_write(0, addr - AVALANCHE_VLYNQ0_BASE, val);
    } else if (INRANGE(AVALANCHE_VLYNQ1_BASE, av.vlynq1)) {
        logflag = 0;
        ar7_vlynq_write(1, addr - AVALANCHE_VLYNQ1_BASE, val);
    } else if (INRANGE(AVALANCHE_MDIO_BASE, av.mdio)) {
        logflag = 0;
        ar7_mdio_write(av.mdio, addr - AVALANCHE_MDIO_BASE, val);
    } else if (INRANGE(OHIO_WDT_BASE, av.wdt)) {
        name = "ohio wdt";
        VALUE(OHIO_WDT_BASE, av.wdt) = val;
    } else if (INRANGE(AVALANCHE_INTC_BASE, av.intc)) {
        logflag = 0;
        ar7_intc_write(addr - AVALANCHE_INTC_BASE, val);
    } else if (INRANGE(AVALANCHE_CPMAC1_BASE, av.cpmac1)) {
        logflag = 0;
        ar7_cpmac_write(1, addr - AVALANCHE_CPMAC1_BASE, val);
    } else if (addr >= AVALANCHE_DISPLAY_BASE + 0x408 && addr < AVALANCHE_DISPLAY_BASE + 0x453) {
        uint32_t display_address = addr - AVALANCHE_DISPLAY_BASE;
        switch (display_address) {
          /* LEDBAR Register */
          case 0x00408:
              malta_display.leds = val & 0xff;
              break;
          /* ASCIIWORD Register */
          case 0x00410:
              snprintf(malta_display.display_text, 9, "%08X", val);
              malta_fpga_update_display(&malta_display);
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
              malta_display.display_text[(display_address - 0x00418) >> 3] = (char) val;
              malta_fpga_update_display(&malta_display);
              break;
          default:
              MISSING();
        }
    } else {
        //~ name = "???";
        logflag = 0;
        {
            logout("addr 0x%08x (???" ") = 0x%08x\n", addr, val);
            MISSING();
        }
    }
    if (name != 0) {
        TRACE(logflag, logout("addr 0x%08lx (%s) = 0x%08x\n",
                              (unsigned long)addr, name, val));
    }
}

static void io_writeb(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    if (0) {
#if !defined(TARGET_WORDS_BIGENDIAN)
    } else if (INRANGE(AVALANCHE_VLYNQ0_BASE + VLYNQ_CTRL, 4) ||
               INRANGE(AVALANCHE_GPIO_BASE, av.gpio)) {
        uint32_t oldvalue = ar7_io_memread(opaque, addr & ~3);
        //~ logout("??? addr=0x" TARGET_FMT_plx ", val=0x%02x\n", addr, value);
        oldvalue &= ~(0xff << (8 * (addr & 3)));
        value = oldvalue + ((value & 0xff) << (8 * (addr & 3)));
        ar7_io_memwrite(opaque, addr & ~3, value);
    } else if (addr & 3) {
        ar7_io_memwrite(opaque, addr & ~3, value);
        logout("addr=0x" TARGET_FMT_plx ", val=0x%02x\n", addr, value);
        UNEXPECTED();
    } else if (INRANGE(AVALANCHE_UART0_BASE, av.uart0)) {
        ar7_io_memwrite(opaque, addr, value);
    } else if (INRANGE(AVALANCHE_UART1_BASE, av.uart1)) {
        ar7_io_memwrite(opaque, addr, value);
    } else {
        ar7_io_memwrite(opaque, addr, value);
        logout("??? addr=0x" TARGET_FMT_plx ", val=0x%02x\n", addr, value);
        //~ UNEXPECTED();
    }
#else
    } else {
        ar7_io_memwrite(opaque, addr, value);
        logout("??? addr=0x" TARGET_FMT_plx ", val=0x%02x\n", addr, value);
        MISSING();
    }
#endif
    //~ cpu_outb(NULL, addr & 0xffff, value);
}

static uint32_t io_readb(void *opaque, target_phys_addr_t addr)
{
    uint32_t value = ar7_io_memread(opaque, addr & ~3);
    if (0) {
#if !defined(TARGET_WORDS_BIGENDIAN)
    } else if (INRANGE(AVALANCHE_BBIF_BASE, av.bbif)) {
        value >>= (addr & 3) * 8;
        value &= 0xff;
        logout("??? addr=0x" TARGET_FMT_plx ", val=0x%02x\n", addr, value);
    } else if (INRANGE(AVALANCHE_GPIO_BASE, av.gpio)) {
        value >>= ((addr & 3) * 8);
        value &= 0xff;
        //~ logout("??? addr=0x%08x, val=0x%02x\n", addr, value);
    } else if (INRANGE(AVALANCHE_CLOCK_BASE, av.clock_control)) {
        value = clock_read((addr & ~3) - AVALANCHE_CLOCK_BASE);
        value >>= ((addr & 3) * 8);
        value &= 0xff;
    } else if (addr & 3) {
        logout("addr=0x" TARGET_FMT_plx ", val=0x%02x\n", addr, value);
        UNEXPECTED();
    } else if (INRANGE(AVALANCHE_UART0_BASE, av.uart0)) {
    } else if (INRANGE(AVALANCHE_UART1_BASE, av.uart1)) {
    } else if (INRANGE(AVALANCHE_UART1_BASE, av.uart1)) {
    } else {
        logout("addr=0x" TARGET_FMT_plx ", val=0x%02x\n", addr, value & 0xff);
        UNEXPECTED();
    }
#else
    } else {
        logout("addr=0x" TARGET_FMT_plx ", val=0x%02x\n", addr, value & 0xff);
        MISSING();
    }
#endif
    value &= 0xff;
    return value;
}

static void io_writew(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    if (0) {
    } else {
        logout("??? addr=0x" TARGET_FMT_plx ", val=0x%04x\n", addr, value);
        switch (addr & 3) {
#if !defined(TARGET_WORDS_BIGENDIAN)
        case 0:
            ar7_io_memwrite(opaque, addr, value);
            break;
        case 2:
            value <<= 16;
            //~ UNEXPECTED();
            ar7_io_memwrite(opaque, addr - 2, value);
            break;
#else
        case 0:
            MISSING();
            break;
#endif
        default:
            assert(0);
        }
    }
}

static uint32_t io_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t value = ar7_io_memread(opaque, addr & ~3);
    if (0) {
    } else {
      switch (addr & 3) {
#if !defined(TARGET_WORDS_BIGENDIAN)
      case 0:
          value &= 0xffff;
          break;
      case 2:
          value >>= 16;
          break;
#else
      case 0:
          MISSING();
          break;
#endif
      default:
          assert(0);
      }
      TRACE(OTHER, logout("addr=0x" TARGET_FMT_plx ", val=0x%04x\n", addr, value));
    }
    return value;
}

static void io_writel(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    ar7_io_memwrite(opaque, addr, value);
}

static uint32_t io_readl(void *opaque, target_phys_addr_t addr)
{
    return ar7_io_memread(opaque, addr);
}

static CPUWriteMemoryFunc *const io_write[] = {
    io_writeb,
    io_writew,
    io_writel,
};

static CPUReadMemoryFunc *const io_read[] = {
    io_readb,
    io_readw,
    io_readl,
};

static void ar7_serial_init(CPUState * env)
{
    /* By default, QEMU only opens one serial console.
     * In this case we open a second console here because
     * we need it for full hardware emulation.
     */
    unsigned uart_index;
    if (serial_hds[1] == 0) {
        serial_hds[1] = qemu_chr_open("serial1", "vc:80Cx24C", NULL);
    }
    for (uart_index = 0; uart_index < 2; uart_index++) {
        ar7.serial[uart_index] = serial_mm_init(uart_base[uart_index], 2,
            AR7_PRIMARY_IRQ(uart_interrupt[uart_index]), io_frequency,
            serial_hds[uart_index], 0);
        serial_frequency(ar7.serial[uart_index], io_frequency / 16);
    }

    /* Set special init values. */
    serial_mm_writeb(ar7.serial[0], AVALANCHE_UART0_BASE + (5 << 2), 0x20);
}

static int ar7_nic_can_receive(VLANClientState *vc)
{
    unsigned cpmac_index = ptr2uint(vc->opaque);
    uint8_t *cpmac = ar7.cpmac[cpmac_index];
    int enabled = (reg_read(cpmac, CPMAC_RXCONTROL) & RXCONTROL_RXEN) != 0;

    TRACE(CPMAC, logout("cpmac%u, enabled %d\n", cpmac_index, enabled));

    return enabled;
}

static ssize_t ar7_nic_receive(VLANClientState *vc, const uint8_t * buf, size_t size)
{
    unsigned cpmac_index = ptr2uint(vc->opaque);
    uint8_t *cpmac = ar7.cpmac[cpmac_index];
    uint32_t rxmbpenable = reg_read(cpmac, CPMAC_RXMBPENABLE);
    uint32_t rxmaxlen = reg_read(cpmac, CPMAC_RXMAXLEN);
    unsigned channel = 0xff;
    uint32_t flags = 0;

    if (!(reg_read(cpmac, CPMAC_MACCONTROL) & MACCONTROL_GMIIEN)) {
        TRACE(CPMAC, logout("cpmac%u MII is disabled, frame ignored\n",
              cpmac_index));
        return -1;
    } else if (!(reg_read(cpmac, CPMAC_RXCONTROL) & RXCONTROL_RXEN)) {
        TRACE(CPMAC, logout("cpmac%u receiver is disabled, frame ignored\n",
          cpmac_index));
        return -1;
    }

    TRACE(RXTX,
          logout("cpmac%u received %u byte: %s\n", cpmac_index, (unsigned)size,
                 dump(buf, size)));

    assert(!(rxmbpenable & RXMBPENABLE_RXPASSCRC));
    assert(!(rxmbpenable & RXMBPENABLE_RXQOSEN));
    //~ assert(!(rxmbpenable & RXMBPENABLE_RXNOCHAIN));
    assert(!(rxmbpenable & RXMBPENABLE_RXCMEMFEN));
    assert(!(rxmbpenable & RXMBPENABLE_RXCEFEN));
    assert(reg_read(cpmac, CPMAC_RXBUFFEROFFSET) == 0);

    /* Received a packet. */
    static const uint8_t broadcast_macaddr[6] =
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    if ((rxmbpenable & RXMBPENABLE_RXBROADEN) && !memcmp(buf, broadcast_macaddr, 6)) {
        channel = ((rxmbpenable & RXMBPENABLE_RXBROADCH) >> 8);
        statusreg_inc(cpmac_index, CPMAC_RXBROADCASTFRAMES);
        TRACE(CPMAC, logout("broadcast to channel %d\n", channel));
    } else if ((rxmbpenable & RXMBPENABLE_RXMULTEN) && (buf[0] & 0x01)) {
        // !!! must check MACHASH1, MACHASH2
        channel = ((rxmbpenable & RXMBPENABLE_RXMULTCH) >> 0);
        statusreg_inc(cpmac_index, CPMAC_RXMULTICASTFRAMES);
        TRACE(CPMAC, logout("multicast to channel %d\n", channel));
    } else if (!memcmp(buf, ar7.nic[cpmac_index].phys, 6)) {
        channel = 0;
        TRACE(CPMAC, logout("my address to channel %d\n", channel));
    } else if (rxmbpenable & RXMBPENABLE_RXCAFEN) {
        channel = ((rxmbpenable & RXMBPENABLE_RXPROMCH) >> 16);
        //~ statusreg_inc(cpmac_index, CPMAC_RXMULTICASTFRAMES);
        TRACE(CPMAC, logout("promiscuous to channel %d\n", channel));
        flags |= RCB_NOMATCH;
    } else {
        TRACE(CPMAC, logout("unknown address, frame ignored\n"));
        return -1;
    }

    /* !!! check handling of short and long frames */
    if (size < 64) {
        TRACE(CPMAC, logout("short frame, flag = 0x%x\n",
          rxmbpenable & RXMBPENABLE_RXCSFEN));
        statusreg_inc(cpmac_index, CPMAC_RXUNDERSIZEDFRAMES);
        flags |= RCB_UNDERSIZED;
    } else if (size > rxmaxlen) {
        statusreg_inc(cpmac_index, CPMAC_RXOVERSIZEDFRAMES);
        flags |= RCB_OVERSIZE;
    }

    statusreg_inc(cpmac_index, CPMAC_RXGOODFRAMES);

    assert(channel < 8);

    /* Get descriptor pointer and process the received frame. */
    uint32_t dp = reg_read(cpmac, CPMAC_RX0HDP + 4 * channel);
    if (dp == 0) {
        TRACE(RXTX, logout("no buffer available, frame ignored\n"));
    } else {
        cpphy_rcb_t rcb;
        cpu_physical_memory_read(dp, (uint8_t *) & rcb, sizeof(rcb));
        uint32_t addr = le32_to_cpu(rcb.buff);
        uint32_t length = le32_to_cpu(rcb.length);
        uint32_t mode = le32_to_cpu(rcb.mode);
        TRACE(CPMAC,
              logout
              ("buffer 0x%08x, next 0x%08x, buff 0x%08x, params 0x%08x, len 0x%08x\n",
               dp, (unsigned)rcb.next, addr, mode, length));
        if (mode & RCB_OWNER) {
            assert(length >= size);
            mode &= ~(RCB_OWNER);
            mode &= ~(BITS(15, 0));
            mode |= (size & BITS(15, 0));
            mode |= RCB_SOP | RCB_EOP;
            if (rcb.next == 0) {
                TRACE(CPMAC, logout("last buffer\n"));
                mode |= RCB_EOQ;
            }
            mode |= RCB_PASSCRC;
            mode |= flags;
            rcb.length = cpu_to_le32(size);
            rcb.mode = cpu_to_le32(mode);
            cpu_physical_memory_write(addr, buf, size);
            cpu_physical_memory_write(dp, (uint8_t *) & rcb, sizeof(rcb));
            reg_write(cpmac, CPMAC_RX0HDP + 4 * channel, rcb.next);
            reg_write(cpmac, CPMAC_RX0CP + 4 * channel, dp);
            reg_set(cpmac, CPMAC_RXINTSTATRAW, BIT(channel));
#if defined(CONFIG_AR7_EMAC)
            reg_set(cpmac, CPMAC_MACINVECTOR, channel << 8);
#endif
            emac_update_interrupt(cpmac_index);
        } else {
            logout("buffer not free, frame ignored\n");
        }
    }
    return size;
}

static void ar7_nic_cleanup(VLANClientState *vc)
{
    /* TODO: check this code. */
    void *d = vc->opaque;
    assert(d == 0);    /* just to trigger always an assertion... */
    unregister_savevm("ar7", d);

#if 0
    qemu_del_timer(d->poll_timer);
    qemu_free_timer(d->poll_timer);
#endif
}

static void ar7_nic_init(void)
{
    unsigned i;
    unsigned n = 0;
    TRACE(CPMAC, logout("\n"));
    for (i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];
        if (nd->vlan) {
            qemu_check_nic_model(nd, "ar7");
            if (n < 2 /*&& (strcmp(nd->model, "ar7") == 0)*/) {
                TRACE(CPMAC, logout("starting AR7 nic CPMAC%u\n", n));
                ar7.nic[n].vc =
                    qemu_new_vlan_client(nd->vlan, nd->model, nd->name,
                                         ar7_nic_can_receive,
                                         ar7_nic_receive,
                                         NULL,
                                         ar7_nic_cleanup, uint2ptr(n));
                //~ qemu_format_nic_info_str(ar7.nic[n].vc, ar7.nic[n].mac);
                n++;
                emac_reset(n);
            }
        }
    }
    phy_init();
}

static int ar7_display_can_receive(void *opaque)
{
    //~ logout("%p\n", opaque);
    return 1;
}

static void ar7_display_receive(void *opaque, const uint8_t *buf, int size)
{
    //~ logout("%p, %d bytes (0x%02x)\n", opaque, size, buf[0]);
    if (buf[0] == 'r') {
        uint32_t in = reg_read(av.gpio, GPIO_IN);
        reg_write(av.gpio, GPIO_IN, in ^ 0x00000800);
        ar7_gpio_display();
    } else if (buf[0] == 'R') {
        uint32_t in = reg_read(av.gpio, GPIO_IN);
        reg_write(av.gpio, GPIO_IN, in & ~0x00000800);
        ar7_gpio_display();
    }
}

static void ar7_display_event(void *opaque, int event)
{
    /* TODO: what should we do here? */
    /* Wird gleich am Anfang mit (NULL, 2) aufgerufen. */
    TRACE(OTHER, logout("%p, %d\n", opaque, event));
    //~ if (event == CHR_EVENT_BREAK)
}

static void malta_fpga_led_init(CharDriverState *chr)
{
    qemu_chr_printf(chr, "\e[HMalta LEDBAR\r\n");
    qemu_chr_printf(chr, "+--------+\r\n");
    qemu_chr_printf(chr, "+        +\r\n");
    qemu_chr_printf(chr, "+--------+\r\n");
    qemu_chr_printf(chr, "\n");
    qemu_chr_printf(chr, "Malta ASCII\r\n");
    qemu_chr_printf(chr, "+--------+\r\n");
    qemu_chr_printf(chr, "+        +\r\n");
    qemu_chr_printf(chr, "+--------+\r\n");

    /* Select 1st serial console as default (because we don't have VGA). */
    console_select(1);
}

static void ar7_gpio_display_init(CharDriverState *chr)
{
    qemu_chr_printf(chr,
                    "\e[1;1HGPIO Status"
                    "\e[2;1H0         1         2         3"
                    "\e[3;1H01234567890123456789012345678901"
                    "\e[10;1H* lan * wlan * online * dsl * power"
                    "\e[12;1HPress 'r' to toggle the reset button");
    ar7_gpio_display();
}

static void ar7_display_init(CPUState *env)
{
    ar7.gpio_display = qemu_chr_open("gpio", "vc:400x300", ar7_gpio_display_init);
    qemu_chr_add_handlers(ar7.gpio_display, ar7_display_can_receive,
                          ar7_display_receive, ar7_display_event, 0);

    malta_display.display = qemu_chr_open("led display", "vc:320x200", malta_fpga_led_init);
}

static int ar7_load(QEMUFile * f, void *opaque, int version_id)
{
    int result = 0;
    if (version_id == 0) {
        qemu_get_buffer(f, (uint8_t *) & av, sizeof(av));
    } else {
        result = -EINVAL;
    }
    return result;
}

static void ar7_save(QEMUFile * f, void *opaque)
{
    /* TODO: fix */
    qemu_put_buffer(f, (uint8_t *) & av, sizeof(av));
}

static void ar7_reset(void *opaque)
{
    //~ CPUState *env = opaque;
    logout("%s:%u\n", __FILE__, __LINE__);
    //~ env->exception_index = EXCP_RESET;
    //~ env->exception_index = EXCP_SRESET;
    //~ do_interrupt(env);
    //~ env->CP0_Cause |= 0x00000400;
    //~ cpu_interrupt(env, CPU_INTERRUPT_RESET);
}

static void ar7_init(CPUState * env)
{
    //~ target_phys_addr_t addr = (0x08610000 & 0xffff);
    //~ unsigned offset;
    int io_memory = cpu_register_io_memory(io_read, io_write, env);
    //~ cpu_register_physical_memory(0x08610000, 0x00002800, io_memory);
    //~ cpu_register_physical_memory(0x00001000, 0x0860f000, io_memory);
    cpu_register_physical_memory_offset(0x00001000, 0x0ffff000, io_memory, 0x00001000);
    //~ cpu_register_physical_memory(0x00001000, 0x10000000, io_memory);
    cpu_register_physical_memory_offset(0x1e000000, 0x01c00000, io_memory, 0x1e000000);

    //~ reg_write(av.gpio, GPIO_IN, 0x0cbea075);
    reg_write(av.gpio, GPIO_IN, 0x0cbea875);
    //~ reg_write(av.gpio, GPIO_OUT, 0x00000000);
    reg_write(av.gpio, GPIO_DIR, 0xffffffff);
    reg_write(av.gpio, GPIO_ENABLE, 0xffffffff);
#define AR7_CHIP_7100 0x18
#define AR7_CHIP_7200 0x2b
#define AR7_CHIP_7300 0x05
    reg_write(av.gpio, GPIO_CVR, 0x00020005);
    reg_write(av.gpio, GPIO_DIDR1, 0x7106150d);
    reg_write(av.gpio, GPIO_DIDR2, 0xf52ccccf);

  //~ .mdio = {0x00, 0x07, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff}
#if defined(CONFIG_AR7_EMAC)
    reg_write(av.mdio, MDIO_VERSION, 0x00070101);
#else
    reg_write(av.mdio, MDIO_VERSION, 0x00070103);
#endif
    reg_write(av.mdio, MDIO_CONTROL, MDIO_CONTROL_IDLE | BIT(24) | BITS(7, 0));

    //~ .reset_control = { 0x04720043 },

    //~ .dcl = 0x025d4297
    reg_write(av.dcl, DCL_BOOT_CONFIG, 0x025d4291);
#if defined(TARGET_WORDS_BIGENDIAN)
    reg_set(av.dcl, DCL_BOOT_CONFIG, CONFIG_ENDIAN);
#endif

    ar7.cpmac[0] = av.cpmac0;
    ar7.cpmac[1] = av.cpmac1;
    ar7.vlynq[0] = av.vlynq0;
    ar7.vlynq[1] = av.vlynq1;
    ar7.cpu_env = env;

    ar7_serial_init(env);
    ar7_display_init(env);
    ar7_nic_init();
    vlynq_tnetw1130_init();

    //~ for (offset = 0; offset < 0x2800; offset += 0x100) {
    //~ if (offset == 0xe00) continue;
    //~ if (offset == 0xf00) continue;
    //~ register_ioport_read(addr + offset, 0x100, 1, ar7_io_memread, 0);
    //~ register_ioport_read(addr + offset, 0x100, 2, ar7_io_memread, 0);
    //~ register_ioport_read(addr + offset, 0x100, 4, ar7_io_memread, 0);
    //~ register_ioport_write(addr + offset, 0x100, 1, ar7_io_memwrite, 0);
    //~ register_ioport_write(addr + offset, 0x100, 2, ar7_io_memwrite, 0);
    //~ register_ioport_write(addr + offset, 0x100, 4, ar7_io_memwrite, 0);
    //~ }
    //~ {
    //~ s_io_memory = cpu_register_io_memory(mips_mm_read, mips_mm_write, 0);
    //~ cpu_register_physical_memory(0x08610000, 0x2000, s_io_memory);
    //~ }
#define ar7_instance 0
#define ar7_version 0
    qemu_register_reset(ar7_reset, env);
    register_savevm("ar7", ar7_instance, ar7_version, ar7_save, ar7_load, 0);
}

/* Kernel */
static int64_t load_kernel (CPUState *env)
{
    uint64_t kernel_addr = 0;
    uint64_t kernel_low, kernel_high;
    int kernel_size;
    kernel_size = load_elf(loaderparams.kernel_filename, VIRT_TO_PHYS_ADDEND, &kernel_addr, &kernel_low, &kernel_high);
    if (kernel_size < 0) {
        kernel_size = load_image_targphys(loaderparams.kernel_filename,
                                          KERNEL_LOAD_ADDR,
                                          loaderparams.ram_size);
        kernel_addr = K1(KERNEL_LOAD_ADDR);
    }
    if (kernel_size > 0 && kernel_size < loaderparams.ram_size) {
        fprintf(stderr, "qemu: elf kernel '%s' with start address 0x%08lx"
                " and size %d bytes\n",
                loaderparams.kernel_filename, (unsigned long)kernel_addr, kernel_size);
        fprintf(stderr, "qemu: kernel low 0x%08lx, high 0x%08lx\n",
                (unsigned long)kernel_low, (unsigned long)kernel_high);
        env->active_tc.PC = kernel_addr;
    } else {
        fprintf(stderr, "qemu: could not load kernel '%s'\n",
                loaderparams.kernel_filename);
        exit(1);
    }

    /* a0 = argc, a1 = argv, a2 = envp */
    env->active_tc.gpr[4] = 0;
    env->active_tc.gpr[5] = K1(INITRD_LOAD_ADDR);
    env->active_tc.gpr[6] = K1(INITRD_LOAD_ADDR);

    /* Set SP (needed for some kernels) - normally set by bootloader. */
    env->active_tc.gpr[29] = env->active_tc.PC + loaderparams.ram_size - 0x1000;

    /* TODO: use code from Malta for command line setup. */
    if (loaderparams.kernel_cmdline && *loaderparams.kernel_cmdline) {
        /* Load kernel parameters (argv, envp) from file. */
        // TODO: use cpu_physical_memory_write(bdloc, (void *)kernel_cmdline, len + 1)
        uint8_t *address = qemu_get_ram_ptr(INITRD_LOAD_ADDR - KERNEL_LOAD_ADDR);
        int argc;
        uint32_t *argv;
        uint32_t *arg0;
        target_ulong size = load_image_targphys(loaderparams.kernel_cmdline,
                                                INITRD_LOAD_ADDR, 1 * KiB);
        target_ulong i;
        if (size == (target_ulong) -1) {
            fprintf(stderr, "qemu: could not load kernel parameters '%s'\n",
                    loaderparams.kernel_cmdline);
            exit(1);
        }
        /* Replace all linefeeds by null bytes. */
        for (i = 0; i < size; i++) {
            uint8_t c = address[i];
            if (c == '\n') {
                address[i] = '\0';
            }
        }
        /* Build argv and envp vectors (behind data). */
        argc = 0;
        i = ((i + 3) & ~3);
        argv = (uint32_t *)(address + i);
        env->active_tc.gpr[5] = K1(INITRD_LOAD_ADDR + i);
        arg0 = argv;
        *argv = (uint32_t)K1(INITRD_LOAD_ADDR);
        for (i = 0; i < size;) {
            uint8_t c = address[i++];
            if (c == '\0') {
                *++argv = (uint32_t)K1(INITRD_LOAD_ADDR + i);
                if (address[i] == '\0' && argc == 0) {
                  argc = argv - arg0;
                  *argv = 0;
                  env->active_tc.gpr[4] = argc;
                  env->active_tc.gpr[6] = env->active_tc.gpr[5] + 4 * (argc + 1);
                }
            }
        }
    }

    return kernel_addr;
}

static void ar7_mips_init(CPUState *env)
{
#if !defined(UR8)
    /* AR7 cpu revision is 2.2. */
    env->CP0_PRid |= 0x48;
#else
    /* UR8 cpu revision is 6.8. */
    env->CP0_PRid |= 0x68;
#endif

    /* Special configuration bits set by external hw inputs. */
    env->CP0_Config0 |= (0x2 << CP0C0_MM);
    env->CP0_Config0 |= (1 << CP0C0_SB);
    /* 256 instruction cache sets. */
    env->CP0_Config1 |= (0x2 << CP0C1_IS);
    /* 4-way instruction cache associativity. */
    env->CP0_Config1 |= (0x3 << CP0C1_IA);
    /* 256 data cache sets. */
    env->CP0_Config1 |= (0x2 << CP0C1_DS);
    /* 4-way data cache associativity. */
    env->CP0_Config1 |= (0x3 << CP0C1_DA);

    /* Compare selected emulation values to original hardware registers. */
    if (env->CP0_PRid != 0x00018448)    printf("CP0_PRid    = 0x%08x\n", env->CP0_PRid);
    if (env->CP0_Config0 != 0x80240082) printf("CP0_Config0 = 0x%08x\n", env->CP0_Config0);
    if (env->CP0_Config1 != 0x9e9b4d8a) printf("CP0_Config1 = 0x%08x\n", env->CP0_Config1);
    if (env->CP0_Config2 != 0x80000000) printf("CP0_Config2 = 0x%08x\n", env->CP0_Config2);
#if !defined(UR8)
#if defined(TARGET_WORDS_BIGENDIAN)
    assert(env->CP0_Config0 == 0x80240082 + (1 << CP0C0_BE));
#else
    assert(env->CP0_Config0 == 0x80240082);
#endif
#endif
    assert(env->CP0_Config1 == 0x9e9b4d8a);
    assert(env->CP0_Config2 == 0x80000000);
    assert(env->CP0_Config3 == 0x00000000);
}

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);
    ar7_mips_init(env);
    /* AR7 is MIPS32 release 1. */
    env->CP0_Config0 &= ~(7 << CP0C0_AR);
    /* AR7 has no FPU. */
    env->CP0_Config1 &= ~(1 << CP0C1_FP);

    if (loaderparams.kernel_filename) {
        load_kernel(env);
    }
}

static void mips_ar7_common_init (ram_addr_t machine_ram_size,
                    uint16_t flash_manufacturer, uint16_t flash_type,
                    int flash_size,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    char *filename;
    CPUState *env;
    DriveInfo *dinfo;
    ram_addr_t flash_offset;
    ram_addr_t ram_offset;
    ram_addr_t rom_offset;
    int rom_size;

#if defined(DEBUG_AR7)
    set_traceflags();
#endif

    if (machine_ram_size > 192 * MiB) {
        /* The external RAM start at 0x14000000 and ends before 0x20000000. */
        machine_ram_size = 192 * MiB;
    }

    /* Initialize CPU. */
    if (cpu_model == NULL) {
#ifdef MIPS_HAS_MIPS64
# error AR7 has a 32 bit CPU
#endif
        cpu_model = "4KEcR1";
    }
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition %s\n", cpu_model);
        exit(1);
    }

    qemu_register_reset(main_cpu_reset, env);
    ar7_mips_init(env);

    loaderparams.ram_size = machine_ram_size;
    loaderparams.kernel_filename = kernel_filename;
    loaderparams.kernel_cmdline = kernel_cmdline;
    loaderparams.initrd_filename = initrd_filename;

    ram_offset = qemu_ram_alloc(machine_ram_size);
    cpu_register_physical_memory_offset(KERNEL_LOAD_ADDR, machine_ram_size, ram_offset | IO_MEM_RAM, KERNEL_LOAD_ADDR);
    fprintf(stderr, "%s: ram_size = 0x%08x\n",
        __func__, (unsigned)machine_ram_size);

    /* load_kernel would fail when ram_offset != 0. */
    assert(ram_offset == 0);

    /* The AR7 processor has 4 KiB internal RAM at physical address 0x00000000. */
    ram_offset = qemu_ram_alloc(4 * KiB);
    logout("ram_offset (internal RAM) = %x\n", (unsigned)ram_offset);
    cpu_register_physical_memory_offset(0, 4 * KiB, ram_offset | IO_MEM_RAM, 0);

    /* Try to load a BIOS image. If this fails, we continue regardless,
       but initialize the hardware ourselves. When a kernel gets
       preloaded we also initialize the hardware, since the BIOS wasn't
       run. */
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, "flashimage.bin");
    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (dinfo) {
        pflash_t *pf;
        int64_t image_size = bdrv_getlength(dinfo->bdrv);
        if (image_size > 0) {
            flash_size = image_size;
            flash_offset = qemu_ram_alloc(flash_size);
            pf = pflash_device_register(FLASH_ADDR, flash_offset,
                                        dinfo->bdrv,
                                        flash_size, 2,
                                        flash_manufacturer, flash_type);
        } else {
            flash_offset = qemu_ram_alloc(flash_size);
            pf = pflash_device_register(FLASH_ADDR, flash_offset,
                                        0,
                                        flash_size, 2,
                                        flash_manufacturer, flash_type);
        }
    } else if (filename) {
        pflash_t *pf;
        flash_offset = qemu_ram_alloc(flash_size);
        pf = pflash_device_register(FLASH_ADDR, flash_offset,
                                    0,
                                    flash_size, 2,
                                    flash_manufacturer, flash_type);
        flash_size = load_image_targphys(filename, FLASH_ADDR, flash_size);
        qemu_free(filename);
    }
    fprintf(stderr, "%s: load BIOS '%s', size %d\n", __func__, "flashimage.bin", flash_size);

    /* The AR7 processor has 4 KiB internal ROM at physical address 0x1fc00000. */
    rom_offset = qemu_ram_alloc(4 * KiB);
    cpu_register_physical_memory(PROM_ADDR,
                                 4 * KiB, rom_offset | IO_MEM_ROM);
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, "mips_bios.bin");
    rom_size = load_image_targphys(filename, PROM_ADDR, 4 * KiB);
    qemu_free(filename);
    if ((rom_size > 0) && (rom_size <= 4 * KiB)) {
        fprintf(stderr, "%s: load BIOS '%s', size %d\n", __func__, "mips_bios.bin", rom_size);
    } else {
        /* Not fatal, write a jump to address 0xb0000000 into memory. */
        static const uint8_t jump[] = {
            /* lui t9,0xb000; jr t9 */
            0x00, 0xb0, 0x19, 0x3c, 0x08, 0x00, 0x20, 0x03
        };
        fprintf(stderr, "QEMU: Warning, could not load MIPS bios '%s'.\n"
                "QEMU added a jump instruction to flash start.\n", "mips_bios.bin");
        cpu_physical_memory_write_rom(PROM_ADDR, jump, sizeof(jump));
        rom_size = 4 * KiB;
    }

    if (kernel_filename) {
        load_kernel(env);
    }

    /* Init internal devices */
    cpu_mips_irq_init_cpu(env);
    cpu_mips_clock_init(env);

    /* Interrupt controller */
    /* The 8259 is attached to the MIPS CPU INT0 pin, ie interrupt 2 */
    //~ i8259 = i8259_init(env->irq[2]);

    ar7.primary_irq = qemu_allocate_irqs(ar7_primary_irq, env, NUM_PRIMARY_IRQS);
    ar7.secondary_irq = qemu_allocate_irqs(ar7_secondary_irq, env, NUM_SECONDARY_IRQS);

    ar7.wd_timer = qemu_new_timer(vm_clock, &watchdog_cb, env);
    ar7.timer[0].qemu_timer = qemu_new_timer(vm_clock, &timer_cb, &ar7.timer[0]);
    ar7.timer[0].base = av.timer0;
    ar7.timer[0].interrupt = AR7_PRIMARY_IRQ(INTERRUPT_TIMER0);
    ar7.timer[1].qemu_timer = qemu_new_timer(vm_clock, &timer_cb, &ar7.timer[1]);
    ar7.timer[1].base = av.timer1;
    ar7.timer[1].interrupt = AR7_PRIMARY_IRQ(INTERRUPT_TIMER1);

    /* Address 31 is the AR7 internal phy. */
    ar7.phyaddr = 31;

    /* TNETW1130 is connected to VLYNQ0. */
    ar7.vlynq_tnetw1130 = 0;
    //~ ar7.vlynq_tnetw1130 = 99;

    ar7_init(env);
}

static void mips_ar7_init(ram_addr_t machine_ram_size,
                    const char *boot_device,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    mips_ar7_common_init(machine_ram_size, MANUFACTURER_ST, 0x2249, 2 * MiB,
                         kernel_filename, kernel_cmdline, initrd_filename,
                         cpu_model);
}

static void ar7_amd_init(ram_addr_t machine_ram_size,
                    const char *boot_device,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    mips_ar7_common_init(machine_ram_size,
                         MANUFACTURER_AMD, AM29LV160DB, 2 * MiB,
                         kernel_filename, kernel_cmdline, initrd_filename,
                         cpu_model);
}

static void mips_tnetd7200_init(ram_addr_t machine_ram_size,
                    const char *boot_device,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    mips_ar7_common_init(machine_ram_size, MANUFACTURER_ST, 0x2249, 2 * MiB,
                         kernel_filename, kernel_cmdline, initrd_filename,
                         cpu_model);
    reg_write(av.gpio, GPIO_CVR, 0x0002002b);
}

static void mips_tnetd7300_init(ram_addr_t machine_ram_size,
                    const char *boot_device,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    mips_ar7_common_init(machine_ram_size, MANUFACTURER_ST, 0x2249, 2 * MiB,
                         kernel_filename, kernel_cmdline, initrd_filename,
                         cpu_model);
}

#if defined(TARGET_WORDS_BIGENDIAN)

static void zyxel_init(ram_addr_t machine_ram_size,
                    const char *boot_device,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    /* Change the default RAM size from 128 MiB to 8 MiB.
       This is the external RAM at physical address KERNEL_LOAD_ADDR.
       Any other size can be selected with command line option -m. */
    if (machine_ram_size == 128 * MiB) {
        machine_ram_size = 8 * MiB;
    }
    mips_ar7_common_init(machine_ram_size,
                         MANUFACTURER_INTEL, I28F160C3B, 2 * MiB,
                         kernel_filename, kernel_cmdline, initrd_filename,
                         cpu_model);
}

#else

static void fbox4_init(ram_addr_t machine_ram_size,
                    const char *boot_device,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    /* Change the default RAM size from 128 MiB to 32 MiB.
       This is the external RAM at physical address KERNEL_LOAD_ADDR.
       Any other size can be selected with command line option -m. */
    if (machine_ram_size == 128 * MiB) {
        machine_ram_size = 32 * MiB;
    }
    mips_ar7_common_init(machine_ram_size,
                         MANUFACTURER_MACRONIX, MX29LV320CT, 4 * MiB,
                         kernel_filename, kernel_cmdline, initrd_filename,
                         cpu_model);
}

static void fbox8_init(ram_addr_t machine_ram_size,
                    const char *boot_device,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    /* Change the default RAM size from 128 MiB to 32 MiB.
       This is the external RAM at physical address KERNEL_LOAD_ADDR.
       Any other size can be selected with command line option -m. */
    if (machine_ram_size == 128 * MiB) {
        machine_ram_size = 32 * MiB;
    }
    mips_ar7_common_init(machine_ram_size,
                         MANUFACTURER_MACRONIX, MX29LV640BT, 8 * MiB,
                         kernel_filename, kernel_cmdline, initrd_filename,
                         cpu_model);
}

static void sinus_basic_3_init(ram_addr_t machine_ram_size,
                    const char *boot_device,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    /* Change the default RAM size from 128 MiB to 16 MiB.
       This is the external RAM at physical address KERNEL_LOAD_ADDR.
       Any other size can be selected with command line option -m. */
    if (machine_ram_size == 128 * MiB) {
        machine_ram_size = 16 * MiB;
    }
    mips_ar7_common_init(machine_ram_size,
                         MANUFACTURER_004A, ES29LV160DB, 2 * MiB,
                         kernel_filename, kernel_cmdline, initrd_filename,
                         cpu_model);
}

static void sinus_basic_se_init(ram_addr_t machine_ram_size,
                    const char *boot_device,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    /* Change the default RAM size from 128 MiB to 16 MiB.
       This is the external RAM at physical address KERNEL_LOAD_ADDR.
       Any other size can be selected with command line option -m. */
    if (machine_ram_size == 128 * MiB) {
        machine_ram_size = 16 * MiB;
    }
    mips_ar7_common_init(machine_ram_size,
                         MANUFACTURER_INTEL, I28F160C3B, 2 * MiB,
                         kernel_filename, kernel_cmdline, initrd_filename,
                         cpu_model);
}

static void sinus_se_init(ram_addr_t machine_ram_size,
                    const char *boot_device,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    /* Change the default RAM size from 128 MiB to 16 MiB.
       This is the external RAM at physical address KERNEL_LOAD_ADDR.
       Any other size can be selected with command line option -m. */
    if (machine_ram_size == 128 * MiB) {
        machine_ram_size = 16 * MiB;
    }
    mips_ar7_common_init(machine_ram_size,
                         MANUFACTURER_INTEL, I28F160C3B, 2 * MiB,
                         kernel_filename, kernel_cmdline, initrd_filename,
                         cpu_model);
    /* Emulate external phy 0. */
    ar7.phyaddr = 0;
}

static void speedport_init(ram_addr_t machine_ram_size,
                    const char *boot_device,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    /* Change the default RAM size from 128 MiB to 32 MiB.
       This is the external RAM at physical address KERNEL_LOAD_ADDR.
       Any other size can be selected with command line option -m. */
    if (machine_ram_size == 128 * MiB) {
        machine_ram_size = 32 * MiB;
    }
    mips_ar7_common_init(machine_ram_size,
                         MANUFACTURER_MACRONIX, MX29LV320CT, 4 * MiB,
                         kernel_filename, kernel_cmdline, initrd_filename,
                         cpu_model);
    reg_write(av.gpio, GPIO_CVR, 0x0002002b);
}

#endif

#define RAMSIZE (0 * MiB)

static QEMUMachine ar7_machines[] = {
  {
    .name = "ar7",
    .desc = "MIPS 4KEc / AR7 platform",
    .init = mips_ar7_init,
    .max_cpus = 1,
  },
  {
    .name = "ar7-amd",
    .desc = "MIPS AR7 with AMD flash",
    .init = ar7_amd_init,
    .max_cpus = 1,
  },
  {
    .name = "tnetd7200",
    .desc = "MIPS 4KEc / TNETD7200 platform",
    .init = mips_tnetd7200_init,
    .max_cpus = 1,
  },
  {
    .name = "tnetd7300",
    .desc = "MIPS 4KEc / TNETD7300 platform",
    .init = mips_tnetd7300_init,
    .max_cpus = 1,
  },
#if defined(TARGET_WORDS_BIGENDIAN)
  {
    .name = "zyxel",
    .desc = "Zyxel 2 MiB flash (AR7 platform)",
    .init = zyxel_init,
    .max_cpus = 1,
  },
#else
  {
    .name = "fbox-4mb",
    .desc = "FBox 4 MiB flash (AR7 platform)",
    .init = fbox4_init,
    .max_cpus = 1,
  },
  {
    .name = "fbox-8mb",
    .desc = "FBox 8 MiB flash (AR7 platform)",
    .init = fbox8_init,
    .max_cpus = 1,
  },
  {
    .name = "sinus-basic-se",
    .desc = "Sinus DSL Basic SE (AR7 platform)",
    .init = sinus_basic_se_init,
    .max_cpus = 1,
  },
  {
    .name = "sinus-se",
    .desc = "Sinus DSL SE (AR7 platform)",
    .init = sinus_se_init,
    .max_cpus = 1,
  },
  {
    .name = "sinus-basic-3",
    .desc = "Sinus DSL Basic 3 (AR7 platform)",
    .init = sinus_basic_3_init,
    .max_cpus = 1,
  },
  {
    .name = "speedport",
    .desc = "Speedport (AR7 platform)",
    .init = speedport_init,
    .max_cpus = 1,
  },
#endif
};

static void ar7_machine_init(void)
{
    size_t i;
    for (i = 0; i < ARRAY_SIZE(ar7_machines); i++) {
        qemu_register_machine(&ar7_machines[i]);
    }
}

machine_init(ar7_machine_init);

/* eof */
