/*
 * QEMU avalanche support
 * Copyright (c) 2006-2007 Stefan Weil
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* This code emulates specific parts of Texas Instruments AR7 processor.
 * AR7 is a chip with a MIPS 4KEc core and on-chip peripherals (avalanche).
 *
 * TODO:
 * - reboot loops endless reading device config latch (AVALANCHE_DCL_BASE)
 * - uart0, uart1 wrong type (is 16450, should be 16550)
 * - vlynq emulation only very rudimentary
 * - ethernet not stable
 * - much more
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

#include <zlib.h>               /* crc32 */
#include <netinet/in.h>         /* htonl */

#include "vl.h"
#include "disas.h"              /* lookup_symbol */
#include "exec-all.h"           /* logfile */

#include "hw/ar7.h"             /* ar7_init */
#include "hw/pflash.h"          /* pflash_amd_register, ... */
#include "hw/tnetw1130.h"       /* vlynq_tnetw1130_init */

//~ #include "target-mips/exec.h"   /* do_int */

#define MIPS_EXCEPTION_OFFSET   8

static int bigendian;

/* physical address of kernel */
#define KERNEL_LOAD_ADDR 0x14000000

/* physical address of kernel parameters */
#define INITRD_LOAD_ADDR 0x14700000

#define K1(physaddr) ((physaddr) + 0x80000000)

#define VIRT_TO_PHYS_ADDEND (-0x80000000LL)

#define GDBRAM (8 * KiB)

#define MAX_ETH_FRAME_SIZE 1514

#if 0
struct IoState {
    target_ulong base;
    int it_shift;
};
#endif

#define DEBUG_AR7

#if defined(DEBUG_AR7)
/* Set flags to >0 to enable debug output. */
static struct {
  int CLOCK:1;
  int CONFIG:1;
  int CPMAC:1;
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
#define CONFIG  traceflags.CONFIG
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

#define logout(fmt, args...) fprintf(stderr, "AR7\t%-24s" fmt, __func__, ##args)
//~ #define logout(fmt, args...) fprintf(stderr, "AR7\t%-24s%-40.40s " fmt, __func__, backtrace(), ##args)

#else /* DEBUG_AR7 */

#define TRACE(flag, command) ((void)0)
#define logout(fmt, args...) ((void)0)

#endif /* DEBUG_AR7 */

#define MISSING() logout("%s:%u missing, %s!!!\n", __FILE__, __LINE__, backtrace())
#define UNEXPECTED() logout("%s:%u unexpected, %s!!!\n", __FILE__, __LINE__, backtrace())
#define backtrace() mips_backtrace()

#define BIT(n) (1 << (n))
#define BITS(n, m) (((0xffffffffU << (31 - n)) >> (31 - n + m)) << m)
//~ #define MASK(hi, lo) (((~(~0 << (hi))) & (~0 << (lo))) | (1 << (hi)))

#if 0
#define BBIF_SPACE1                           (KSEG1ADDR(0x01800000))
#define PHY_BASE                              (KSEG1ADDR(0x1E000000))
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
    int interrupt;              /* interrupt number */
    int cyclic;                 /* 1 = cyclic timer */
    int64_t time;               /* preload value */
    uint16_t prescale;          /* prescale divisor */
    QEMUTimer *qemu_timer;
} ar7_timer_t;

typedef struct {
    CPUState *cpu_env;
    QEMUTimer *wd_timer;
    NICState nic[2];
    uint16_t phy[32];
    CharDriverState *gpio_display;
    SerialState *serial[2];
    uint32_t intmask[2];
    uint8_t *cpmac[2];
    ar7_timer_t timer[2];
    uint8_t *vlynq[2];

    uint32_t adsl[0x8000];      // 0x01000000
    uint32_t bbif[3];           // 0x02000000
    uint32_t atmsar[0x2400];    // 0x03000000
    uint32_t usbslave[0x800];   // 0x03400000
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
    uint32_t intc[0xc0];        // 0x08612400
    //~ uint32_t exception_control[7];  //   +0x80
    //~ uint32_t pacing[3];             //   +0xa0
    //~ uint32_t channel_control[40];   //   +0x200
    uint8_t cpmac1[0x800];      // 0x08612800
    //~ uint32_t unknown[0x40]              // 0x08613000
} avalanche_t;

#define UART_MEM_TO_IO(addr)    (((addr) - AVALANCHE_UART0_BASE) / 4)

static avalanche_t av;

static const unsigned io_frequency = 62500000;

/* Global variable avalanche can be used in debugger. */
//~ avalanche_t *avalanche = &av;

const char *mips_backtrace(void)
{
    static char buffer[256];
    char *p = buffer;
    p += sprintf(p, "[%s]", lookup_symbol(av.cpu_env->PC));
    p += sprintf(p, "[%s]", lookup_symbol(av.cpu_env->gpr[31]));
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

#if defined(DEBUG_AR7)
static void set_traceflags(void)
{
    const char *env = getenv("DEBUG_AR7");
    if (env != 0) {
        unsigned long ul = strtoul(env, 0, 0);
        memcpy(&traceflags, &ul, sizeof(traceflags));
        if (strstr(env, "CLOCK")) CLOCK = 1;
        if (strstr(env, "CONFIG")) CONFIG = 1;
        if (strstr(env, "CPMAC")) CPMAC = 1;
        if (strstr(env, "EMIF")) EMIF = 1;
        if (strstr(env, "GPIO")) GPIO = 1;
        if (strstr(env, "INTC")) INTC = 1;
        if (strstr(env, "MDIO")) MDIO = 1;
        if (strstr(env, "RESET")) RESET = 1;
        if (strstr(env, "TIMER")) TIMER = 1;
        if (strstr(env, "UART")) UART = 1;
        if (strstr(env, "VLYNQ")) VLYNQ = 1;
        if (strstr(env, "WDOG")) WDOG = 1;
        if (strstr(env, "OTHER")) OTHER = 1;
        if (strstr(env, "RXTX")) RXTX = 1;
    }
    TRACE(CLOCK, logout("Logging enabled for CLOCK\n"));
    TRACE(CONFIG, logout("Logging enabled for CONFIG\n"));
    TRACE(CPMAC, logout("Logging enabled for CPMAC\n"));
    TRACE(EMIF, logout("Logging enabled for EMIF\n"));
    TRACE(GPIO, logout("Logging enabled for GPIO\n"));
    TRACE(INTC, logout("Logging enabled for INTC\n"));
    TRACE(MDIO, logout("Logging enabled for MDIO\n"));
    TRACE(RESET, logout("Logging enabled for RESET\n"));
    TRACE(TIMER, logout("Logging enabled for TIMER\n"));
    TRACE(UART, logout("Logging enabled for UART\n"));
    TRACE(VLYNQ, logout("Logging enabled for VLYNQ\n"));
    TRACE(WDOG, logout("Logging enabled for WDOG\n"));
    TRACE(OTHER, logout("Logging enabled for OTHER\n"));
    TRACE(RXTX, logout("Logging enabled for RXTX\n"));
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
 * Interrupt emulation.
 * Interrupt controller emulation.
 *
 ****************************************************************************/

typedef enum {
    INTC_SR1 = 0x00,    /* Interrupt Status/Set Register 1 */
    INTC_SR2 = 0x04,    /* Interrupt Status/Set Register 2 */
    INTC_CR1 = 0x10,    /* Interrupt Clear Register 1 */
    INTC_CR2 = 0x14,    /* Interrupt Clear Register 2 */
    INTC_ESR1 = 0x20,   /* Interrupt Enable (Set) Register 1 */
    INTC_ESR2 = 0x24,   /* Interrupt Enable (Set) Register 2 */
    INTC_ECR1 = 0x30,   /* Interrupt Enable Clear Register 1 */
    INTC_ECR2 = 0x34,   /* Interrupt Enable Clear Register 2 */
    INTC_PIIR = 0x40,   /* Priority Interrupt Index Register */
    INTC_PIMR = 0x44,   /* Priority Interrupt Mask Index Reg */
    INTC_IPMR1 = 0x50,  /* Interrupt Polarity Mask Reg 1 */
    INTC_IPMR2 = 0x54,  /* Interrupt Polarity Mask Reg 2 */
    INTC_TMR1 = 0x60,   /* Interrupt Type Mask Register 1 */
    INTC_TMR2 = 0x64,   /* Interrupt Type Mask Register 2 */
    /* Avalanche Exception control registers */
    INTC_EXSR = 0x80,   /* Exceptions Status/Set register */
#if 0
typedef struct {                /* Avalanche Interrupt control registers */
    /* Avalanche Exception control registers */
    uint32_t excr;              /* Exceptions Clear Register         0x88 */
    uint32_t exiesr;            /* Exceptions Interrupt Enable (set) 0x90 */
    uint32_t exiecr;            /* Exceptions Interrupt Enable(clear)0x98 */
    /* Interrupt Pacing */
    uint32_t ipacep;            /* Interrupt pacing register         0xa0 */
    uint32_t ipacemap;          /* Interrupt Pacing Map Register     0xa4 */
    uint32_t ipacemax;          /* Interrupt Pacing Max Register     0xa8 */
    /* Interrupt Channel Control */
    uint32_t cintnr[40];        /* Channel Interrupt Number Reg     0x200 */
} ar7_intc_t;
#endif
} intc_register_t;

/* ar7_irq does not use the opaque parameter, so we set it to 0. */
#define IRQ_OPAQUE 0

static void ar7_irq(void *opaque, int irq_num, int level)
{
    CPUState *cpu_env = first_cpu;
    unsigned channel = irq_num - MIPS_EXCEPTION_OFFSET;
    assert(cpu_env == av.cpu_env);

    switch (channel) {
      /* primary interrupts 1 ... 39 */
    case  1:    /* ext0 ??? */
    case  2:    /* ext1 ??? */
    case  5:    /* timer0 ??? */
    case  6:    /* timer1 ??? */
    case  7:    /* serial0 */
    case  8:    /* serial1 */
    case  9:    /* dma0 ??? */
    case 10:    /* dma1 ??? */
    case 15:    /* atmsar ??? */
    case 19:    /* cpmac0 */
    case 21:    /* vlynq0 ??? */
    case 22:    /* codec ??? */
    case 24:    /* usbslave ??? */
    case 25:    /* vlynq1 ??? */
    case 28:    /* phy ??? */
    case 29:    /* i2c ??? */
    case 30:    /* dma2 ??? */
    case 31:    /* dma3 ??? */
    case 33:    /* cpmac1 */
    case 37:    /* vdma rx ??? */
    case 38:    /* vdma tx ??? */
    case 39:    /* adslss ??? */
      /* secondary interrupts 40 ... 71 */
    case 47:    /* emif ??? */
        if (level) {
            if (channel < 48) {
                unsigned index = channel / 32;
                unsigned offset = channel % 32;
                if (av.intmask[index] & (1 << offset)) {
                    TRACE(INTC, logout("(%p,%d,%d)\n", opaque, irq_num, level));
                    av.intc[INTC_CR1 / 4 + index] |= (1 << offset);
                    av.intc[INTC_SR1 / 4 + index] |= (1 << offset);
                    av.intc[INTC_PIIR / 4] = ((channel << 16) | channel);
                    /* use hardware interrupt 0 */
                    cpu_env->CP0_Cause |= 0x00000400;
                    cpu_interrupt(cpu_env, CPU_INTERRUPT_HARD);
                } else {
                    TRACE(INTC, logout("(%p,%d,%d) is disabled\n", opaque, irq_num, level));
                }
            }
            // int line number
            //~ av.intc[0x10] |= (4 << 16);
            // int channel number
            // 2, 7, 15, 27, 80
            //~ av.intmask[0]
        } else {
            TRACE(INTC, logout("(%p,%d,%d)\n", opaque, irq_num, level));
            av.intc[INTC_PIIR / 4] = 0;
            cpu_env->CP0_Cause &= ~0x00000400;
            cpu_reset_interrupt(cpu_env, CPU_INTERRUPT_HARD);
        }
        break;
    default:
        logout("(%p,%d,%d)\n", opaque, irq_num, level);
    }
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

static const char *i2intc(unsigned index)
{
    static char buffer[32];
    const char *text = buffer;
    if (index < sizeof(intc_names) / sizeof(*intc_names)) {
        text = intc_names[index];
    } else if (index >= 128 && index < 168) {
        snprintf(buffer, sizeof(buffer), "Channel Interrupt Number 0x%02x", index - 128);
    } else {
        snprintf(buffer, sizeof(buffer), "0x%02x", index);
    }
    return text;
}

static uint32_t ar7_intc_read(unsigned offset)
{
    unsigned index = offset / 4;
    uint32_t val = av.intc[index];
    assert((offset & 3) == 0);
    if (0) {
        //~ } else if (index == 16) {
    } else {
        TRACE(INTC, logout("intc[%s] = 0x%08x\n", i2intc(index), val));
    }
    return val;
}

static void ar7_intc_write(unsigned offset, uint32_t val)
{
    unsigned index = offset / 4;
    unsigned subindex = (index & 1);
    assert((offset & 3) == 0);
    if (0) {
        //~ } else if (index == 4) {
    } else if (offset == INTC_SR1 || offset == INTC_SR2) {
        /* Interrupt set. */
        av.intc[index] |= val;
    } else if (offset == INTC_CR1 || offset == INTC_CR2) {
        /* Interrupt clear. */
        TRACE(INTC, logout("intc[%s] val 0x%08x\n", i2intc(index), val));
        av.intc[INTC_SR1 / 4 + subindex] &= ~val;
        av.intc[index] &= ~val;
    } else if (offset == INTC_ESR1 || offset == INTC_ESR2) {
        /* Interrupt enable. */
        av.intmask[subindex] |= val;
        TRACE(INTC, logout("intc[%s] val 0x%08x, mask 0x%08x\n",
                           i2intc(index), val, av.intmask[subindex]));
        av.intc[index] = val;
    } else if (offset == INTC_ECR1 || offset == INTC_ECR2) {
        av.intmask[subindex] &= ~val;
        TRACE(INTC, logout("intc[%s] val 0x%08x, mask 0x%08x\n",
                           i2intc(index), val, av.intmask[subindex]));
        av.intc[index] = val;
    } else {
        TRACE(INTC, logout("intc[%s] val 0x%08x\n", i2intc(index), val));
        av.intc[index] = val;
    }
}

/*****************************************************************************
 *
 * Clock emulation.
 *
 ****************************************************************************/

static uint32_t clock_read(unsigned offset)
{
    static uint32_t last;
    static unsigned count;
    uint32_t val = reg_read(av.clock_control, offset);
    unsigned index = offset / 4;
    if (index == 0x0c || index == 0x14 || index == 0x1c || index == 0x24) {
        /* Reset PLL status bit after a short delay. */
        if (val == last) {
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
    TRACE(CLOCK, logout("clock[0x%04x] = 0x%08x %s\n", index, val, backtrace()));
    return val;
}

static void clock_write(unsigned offset, uint32_t val)
{
    TRACE(CLOCK, logout("clock[0x%04x] = 0x%08x %s\n", offset / 4, val, backtrace()));
    if (offset == 0) {
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
                if (changed & (1 << i)) {
                    TRACE(CLOCK,
                          logout("power %sabled %s (0x%08x)\n",
                                 (enabled & (1 << i)) ? "en" : "dis",
                                 powerbits[i], val));
                }
            }
#endif
            oldpowerstate >>= 30;
            TRACE(CLOCK, logout("change power state from %u to %u\n",
                                oldpowerstate, newpowerstate));
        }
    } else if (offset / 4 == 0x0c) {
        uint32_t oldval = reg_read(av.clock_control, offset);
    TRACE(CLOCK, logout("clock[0x%04x] was 0x%08x %s\n", offset / 4, oldval, backtrace()));
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
    CONFIG_SYS_PLL_SEL = BITS(15, 14),  /* 01 */
    CONFIG_MIPS_PLL_SEL = BITS(17, 16), /* 01 */
    CONFIG_USB_PLL_SEL = BITS(19, 18),  /* 11 */
    CONFIG_EPHY_PLL_SEL = BITS(21, 20), /* 01 */
    CONFIG_ADSL_PLLSEL = BITS(23, 22),  /* 01 */
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
    int logflag = CONFIG;
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
    int logflag = CONFIG;
    if (0) {
    } else if (offset == DCL_BOOT_CONFIG) {
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
    CPMAC_RXGOODFRAMES = 0x0200,
    CPMAC_RXBROADCASTFRAMES = 0x0204,
    CPMAC_RXMULTICASTFRAMES = 0x0208,
    CPMAC_RXDMAOVERRUNS = 0x028c,
    CPMAC_RXOVERSIZEDFRAMES = 0x0218,
    CPMAC_RXJABBERFRAMES = 0x021c,
    CPMAC_RXUNDERSIZEDFRAMES = 0x0220,
    CPMAC_TXGOODFRAMES = 0x234,
    CPMAC_TXBROADCASTFRAMES = 0x238,
    CPMAC_TXMULTICASTFRAMES = 0x23c,
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

typedef enum {
    MACINVECTOR_STATUS_INT = BIT(19),
    MACINVECTOR_HOST_INT = BIT(18),
    MACINVECTOR_RX_INT_OR = BIT(17),
    MACINVECTOR_TX_INT_OR = BIT(16),
    MACINVECTOR_RX_INT_VEC = BITS(10, 8),
    MACINVECTOR_TX_INT_VEC = BITS(2, 0),
} mac_in_vec_bit_t;

typedef enum {
    MACINTSTAT_HOSTPEND = BIT(1),
    MACINTSTAT_STATPEND = BIT(0),
} macinstat_bit_t;

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

/* STATISTICS */
static const char *const cpmac_statistics[] = {
    "RXGOODFRAMES",
    "RXBROADCASTFRAMES",
    "RXMULTICASTFRAMES",
    "RXPAUSEFRAMES",
    "RXCRCERRORS",
    "RXALIGNCODEERRORS",
    "RXOVERSIZEDFRAMES",
    "RXJABBERFRAMES",
    "RXUNDERSIZEDFRAMES",
    "RXFRAGMENTS",
    "RXFILTEREDFRAMES",
    "RXQOSFILTEREDFRAMES",
    "RXOCTETS",
    "TXGOODFRAMES",
    "TXBROADCASTFRAMES",
    "TXMULTICASTFRAMES",
    "TXPAUSEFRAMES",
    "TXDEFERREDFRAMES",
    "TXCOLLISIONFRAMES",
    "TXSINGLECOLLFRAMES",
    "TXMULTCOLLFRAMES",
    "TXEXCESSIVECOLLISIONS",
    "TXLATECOLLISIONS",
    "TXUNDERRUN",
    "TXCARRIERSENSEERRORS",
    "TXOCTETS",
    "64OCTETFRAMES",
    "65T127OCTETFRAMES",
    "128T255OCTETFRAMES",
    "256T511OCTETFRAMES",
    "512T1023OCTETFRAMES",
    "1024TUPOCTETFRAMES",
    "NETOCTETS",
    "RXSOFOVERRUNS",
    "RXMOFOVERRUNS",
    "RXDMAOVERRUNS"
};

static const char *i2cpmac(unsigned index)
{
    static char buffer[32];
    const char *text = 0;
    switch (index) {
    case 0x00:
        text = "TXIDVER";
        break;
    case 0x01:
        text = "TXCONTROL";
        break;
    case 0x02:
        text = "TXTEARDOWN";
        break;
    case 0x04:
        text = "RXIDVER";
        break;
    case 0x05:
        text = "RXCONTROL";
        break;
    case 0x06:
        text = "RXTEARDOWN";
        break;
    case 0x40:
        text = "RXMBPENABLE";
        break;
    case 0x41:
        text = "RXUNICASTSET";
        break;
    case 0x42:
        text = "RXUNICASTCLEAR";
        break;
    case 0x43:
        text = "RXMAXLEN";
        break;
    case 0x44:
        text = "RXBUFFEROFFSET";
        break;
    case 0x45:
        text = "RXFILTERLOWTHRESH";
        break;
    case 0x58:
        text = "MACCONTROL";
        break;
    case 0x5c:
        text = "TXINTSTATRAW";
        break;
    case 0x5d:
        text = "TXINTSTATMASKED";
        break;
    case 0x5e:
        text = "TXINTMASKSET";
        break;
    case 0x5f:
        text = "TXINTMASKCLEAR";
        break;
    case 0x60:
        text = "MACINVECTOR";
        break;
    case 0x61:
        text = "MACEOIVECTOR";
        break;
    case 0x66:
        text = "RXINTMASKSET";
        break;
    case 0x67:
        text = "RXINTMASKCLEAR";
        break;
    case 0x6a:
        text = "MACINTMASKSET";
        break;
    case 0x74:
        text = "MACADDRMID";
        break;
    case 0x75:
        text = "MACADDRHI";
        break;
    case 0x76:
        text = "MACHASH1";
        break;
    case 0x77:
        text = "MACHASH2";
        break;
    }
    if (text != 0) {
    } else if (index >= 0x48 && index < 0x50) {
        text = buffer;
        snprintf(buffer, sizeof(buffer), "RX%uFLOWTHRESH", index & 7);
    } else if (index >= 0x50 && index < 0x58) {
        text = buffer;
        sprintf(buffer, "RX%uFREEBUFFER", index & 7);
    } else if (index >= 0x6c && index < 0x74) {
        text = buffer;
        sprintf(buffer, "MACADDRLO_%u", (unsigned)(index - 0x6c));
    } else if (index >= 0x80 && index < 0xa4) {
        text = buffer;
        sprintf(buffer, "STAT %s", cpmac_statistics[index - 0x80]);
    } else if (index >= 0x180 && index < 0x188) {
        text = buffer;
        sprintf(buffer, "TX%uHDP", index & 7);
    } else if (index >= 0x188 && index < 0x190) {
        text = buffer;
        sprintf(buffer, "RX%uHDP", index & 7);
    } else if (index >= 0x190 && index < 0x198) {
        text = buffer;
        sprintf(buffer, "TX%uCP", index & 7);
    } else if (index >= 0x198 && index < 0x1a0) {
        text = buffer;
        sprintf(buffer, "RX%uCP", index & 7);
    } else {
        text = buffer;
        sprintf(buffer, "0x%x", index);
    }
    assert(strlen(buffer) < sizeof(buffer));
    return text;
}

static const int cpmac_interrupt[] = { 27, 41 };

static void emac_update_interrupt(unsigned index)
{
    uint8_t *cpmac = av.cpmac[index];
    uint32_t txintstat = reg_read(cpmac, CPMAC_TXINTSTATRAW);
    uint32_t txintmask = reg_read(cpmac, CPMAC_TXINTMASKSET);
    uint32_t rxintstat = reg_read(cpmac, CPMAC_RXINTSTATRAW);
    uint32_t rxintmask = reg_read(cpmac, CPMAC_RXINTMASKSET);
    uint32_t macintstat = reg_read(cpmac, CPMAC_MACINTSTATRAW);
    uint32_t macintmask = reg_read(cpmac, CPMAC_MACINTMASKSET);
    uint32_t macinvector = 0;
    int enabled;
    txintstat &= txintmask;
    reg_write(cpmac, CPMAC_TXINTSTATMASKED, txintstat);
    rxintstat &= rxintmask;
    reg_write(cpmac, CPMAC_RXINTSTATMASKED, rxintstat);
    macintstat &= macintmask;
    reg_write(cpmac, CPMAC_MACINTSTATMASKED, macintstat);
    // !!!
    if (txintstat) {
        macinvector |= MACINVECTOR_TX_INT_OR;
    }
    if (rxintstat) {
        macinvector |= MACINVECTOR_RX_INT_OR;
    }
    if (macintstat & MACINTSTAT_HOSTPEND) {
        macinvector |= MACINVECTOR_HOST_INT;
    }
    if (macintstat & MACINTSTAT_STATPEND) {
        macinvector |= MACINVECTOR_STATUS_INT;
    }
    reg_write(cpmac, CPMAC_MACINVECTOR, macinvector);
    //~ reg_write(cpmac, CPMAC_MACINVECTOR, (macintstat << 16) + (rxintstat << 8) + txintstat);
    enabled = (txintstat || rxintstat || macintstat);
    ar7_irq(IRQ_OPAQUE, cpmac_interrupt[index], enabled);
}

static void emac_reset(unsigned index)
{
    uint8_t *cpmac = av.cpmac[index];
    memset(cpmac, 0, sizeof(av.cpmac0));
    reg_write(cpmac, CPMAC_TXIDVER, 0x000c0a07);
    reg_write(cpmac, CPMAC_RXIDVER, 0x000c0a07);
    reg_write(cpmac, CPMAC_RXMAXLEN, 1518);
    //~ reg_write(cpmac, CPMAC_MACCONFIG, 0x03030101);
}

#define BD_SOP    MASK(31, 31)
#define BD_EOP    MASK(30, 30)
#define BD_OWNS   MASK(29, 29)

static uint32_t ar7_cpmac_read(unsigned index, unsigned offset)
{
    uint8_t *cpmac = av.cpmac[index];
    uint32_t val = reg_read(cpmac, offset);
    const char *text = i2cpmac(offset / 4);
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
                        index, text,
                        (AVALANCHE_CPMAC0_BASE + (AVALANCHE_CPMAC1_BASE -
                                         AVALANCHE_CPMAC0_BASE) * index + offset),
                        val, backtrace()));
    return val;
}

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
uint32_t fcs(const uint8_t * buf, int len)
{
    return update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
}

static void statusreg_inc(unsigned index, unsigned offset)
{
    uint8_t *cpmac = av.cpmac[index];
    uint32_t value = reg_read(cpmac, offset);
    value++;
    reg_write(cpmac, offset, value);
    if (value >= 0x80000000) {
        reg_set(cpmac, CPMAC_MACINTSTATRAW, MACINTSTAT_STATPEND);
        emac_update_interrupt(index);
        MISSING();
    }
}

static void emac_transmit(unsigned index, unsigned offset, uint32_t address)
{
    uint8_t *cpmac = av.cpmac[index];
    uint8_t channel = (offset - CPMAC_TX0HDP) / 4;
    reg_write(cpmac, offset, address);
    if (address == 0) {
    } else if (!(reg_read(cpmac, CPMAC_MACCONTROL) & MACCONTROL_GMIIEN)) {
        TRACE(CPMAC, logout("cpmac%u MII is disabled, frame ignored\n",
          index));
    } else if (!(reg_read(cpmac, CPMAC_TXCONTROL) & TXCONTROL_TXEN)) {
        TRACE(CPMAC, logout("cpmac%u transmitter is disabled, frame ignored\n",
          index));
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
        assert(flags & TCB_OWNER);
        assert(!(flags & TCB_PASSCRC));
        assert(bufferoffset == 0);
        /* Real hardware sets flag when finished, we set it here. */
        flags &= ~(TCB_OWNER);
        flags |= TCB_EOQ;
        stl_phys(address + offsetof(cpphy_tcb_t, mode), flags | packetlength);

        if (av.nic[index].vc != 0) {
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
                  logout("cpmac%u sent %u byte: %s\n", index, length,
                         dump(buffer, length)));
            qemu_send_packet(av.nic[index].vc, buffer, length);
        }
        statusreg_inc(index, CPMAC_TXGOODFRAMES);
        reg_write(cpmac, offset, next);
        reg_write(cpmac, CPMAC_TX0CP + 4 * channel, address);
        reg_set(cpmac, CPMAC_TXINTSTATRAW, 1 << channel);
        emac_update_interrupt(index);
        //~ break;
        //~ statusreg_inc(index, CPMAC_TXBROADCASTFRAMES);
        //~ statusreg_inc(index, CPMAC_TXMULTICASTFRAMES);

        if (next != 0) {
            TRACE(RXTX, logout("more data to send...\n"));
            address = next;
            goto loop;
        }
    }
}

static void ar7_cpmac_write(unsigned index, unsigned offset,
                            uint32_t val)
{
    uint8_t * cpmac = av.cpmac[index];
    assert((offset & 3) == 0);
    TRACE(CPMAC, logout("cpmac%u[%s] (0x%08x) = 0x%08lx\n",
                        index, i2cpmac(offset / 4),
                        (AVALANCHE_CPMAC0_BASE +
                                        (AVALANCHE_CPMAC1_BASE -
                                         AVALANCHE_CPMAC0_BASE) * index +
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
        reg_set(cpmac, CPMAC_TXINTSTATRAW, 1 << channel);
        emac_update_interrupt(index);
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
        reg_set(cpmac, CPMAC_RXINTSTATRAW, 1 << channel);
        emac_update_interrupt(index);
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
        emac_update_interrupt(index);
    } else if (offset == CPMAC_TXINTMASKCLEAR) {
        val = (reg_read(cpmac, CPMAC_TXINTMASKSET) & ~val);
        reg_write(cpmac, CPMAC_TXINTMASKSET, val);
        emac_update_interrupt(index);
    } else if (offset == CPMAC_RXINTMASKSET) {
        val &= BITS(7, 0);
        val = (reg_read(cpmac, offset) | val);
        reg_write(cpmac, offset, val);
        emac_update_interrupt(index);
    } else if (offset == CPMAC_RXINTMASKCLEAR) {
        val = (reg_read(cpmac, CPMAC_RXINTMASKSET) & ~val);
        reg_write(cpmac, CPMAC_RXINTMASKSET, val);
        emac_update_interrupt(index);
    } else if (offset == CPMAC_MACINTMASKSET) {
        val &= BITS(1, 0);
        val = (reg_read(cpmac, offset) | val);
        reg_write(cpmac, offset, val);
        emac_update_interrupt(index);
    } else if (offset == CPMAC_MACINTMASKCLEAR) {
        val = (reg_read(cpmac, CPMAC_MACINTMASKSET) & ~val);
        reg_write(cpmac, CPMAC_MACINTMASKSET, val);
        emac_update_interrupt(index);
    } else if (offset == CPMAC_MACADDRHI) {
        /* set MAC address (4 high bytes) */
        uint8_t *phys = av.nic[index].phys;
        reg_write(cpmac, offset, val);
        phys[5] = cpmac[CPMAC_MACADDRLO_0];
        phys[4] = cpmac[CPMAC_MACADDRMID];
        phys[3] = cpmac[CPMAC_MACADDRHI + 3];
        phys[2] = cpmac[CPMAC_MACADDRHI + 2];
        phys[1] = cpmac[CPMAC_MACADDRHI + 1];
        phys[0] = cpmac[CPMAC_MACADDRHI + 0];
        TRACE(CPMAC, logout("setting mac address %02x:%02x:%02x:%02x:%02x:%02x\n",
                            phys[0], phys[1], phys[2], phys[3], phys[4],
                            phys[5]));
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
        emac_transmit(index, offset, val);
    } else if (offset >= CPMAC_RX0HDP && offset <= CPMAC_RX7HDP) {
        reg_write(cpmac, offset, val);
    } else if (offset >= CPMAC_TX0CP && offset <= CPMAC_TX7CP) {
        uint8_t channel = (offset - CPMAC_TX0CP) / 4;
        uint32_t oldval = reg_read(cpmac, offset);
        if (oldval == val) {
            reg_clear(cpmac, CPMAC_TXINTSTATRAW, 1 << channel);
            emac_update_interrupt(index);
        }
    } else if (offset >= CPMAC_RX0CP && offset <= CPMAC_RX7CP) {
        uint8_t channel = (offset - CPMAC_RX0CP) / 4;
        uint32_t oldval = reg_read(cpmac, offset);
        if (oldval == val) {
            reg_clear(cpmac, CPMAC_RXINTSTATRAW, 1 << channel);
            emac_update_interrupt(index);
        }
    } else {
        //~ logout("???\n");
        reg_write(cpmac, offset, val);
    }
}

/*****************************************************************************
 *
 * Clock / power controller emulation.
 *
 ****************************************************************************/

/* Power Control  */
#define TNETD73XX_POWER_CTRL_PDCR           (TNETD73XX_CLOCK_CTRL_BASE + 0x0)
#define TNETD73XX_POWER_CTRL_PCLKCR         (TNETD73XX_CLOCK_CTRL_BASE + 0x4)
#define TNETD73XX_POWER_CTRL_PDUCR          (TNETD73XX_CLOCK_CTRL_BASE + 0x8)
#define TNETD73XX_POWER_CTRL_WKCR           (TNETD73XX_CLOCK_CTRL_BASE + 0xC)

/* Clock Control */
#define TNETD73XX_CLK_CTRL_SCLKCR           (TNETD73XX_CLOCK_CTRL_BASE + 0x20)
#define TNETD73XX_CLK_CTRL_SCLKPLLCR        (TNETD73XX_CLOCK_CTRL_BASE + 0x30)
#define TNETD73XX_CLK_CTRL_MCLKCR           (TNETD73XX_CLOCK_CTRL_BASE + 0x40)
#define TNETD73XX_CLK_CTRL_MCLKPLLCR        (TNETD73XX_CLOCK_CTRL_BASE + 0x50)
#define TNETD73XX_CLK_CTRL_UCLKCR           (TNETD73XX_CLOCK_CTRL_BASE + 0x60)
#define TNETD73XX_CLK_CTRL_UCLKPLLCR        (TNETD73XX_CLOCK_CTRL_BASE + 0x70)
#define TNETD73XX_CLK_CTRL_ACLKCR0          (TNETD73XX_CLOCK_CTRL_BASE + 0x80)
#define TNETD73XX_CLK_CTRL_ACLKPLLCR0       (TNETD73XX_CLOCK_CTRL_BASE + 0x90)
#define TNETD73XX_CLK_CTRL_ACLKCR1          (TNETD73XX_CLOCK_CTRL_BASE + 0xA0)
#define TNETD73XX_CLK_CTRL_ACLKPLLCR1       (TNETD73XX_CLOCK_CTRL_BASE + 0xB0)

#if 0
#define CLKC_CLKCR(x)          (TNETD73XX_CLOCK_CTRL_BASE + 0x20 + (0x20 * (x)))
#define CLKC_CLKPLLCR(x)       (TNETD73XX_CLOCK_CTRL_BASE + 0x30 + (0x20 * (x)))

static void ar7_machine_power_off(void)
{
    volatile uint32_t *power_reg = (void *)(KSEG1ADDR(0x08610A00));
    uint32_t power_state = *power_reg;

    /* add something to turn LEDs off? */

    power_state &= ~(3 << 30);
    power_state |= (3 << 30);   /* power down */
    *power_reg = power_state;

    printk("after power down?\n");
}
#endif

#define AVALANCHE_POWER_MODULE_USBSP               0
#define AVALANCHE_POWER_MODULE_WDTP                1
#define AVALANCHE_POWER_MODULE_UT0P                2
#define AVALANCHE_POWER_MODULE_UT1P                3
#define AVALANCHE_POWER_MODULE_IICP                4
#define AVALANCHE_POWER_MODULE_VDMAP               5
#define AVALANCHE_POWER_MODULE_GPIOP               6
#define AVALANCHE_POWER_MODULE_VLYNQ1P             7
#define AVALANCHE_POWER_MODULE_SARP                8
#define AVALANCHE_POWER_MODULE_ADSLP               9
#define AVALANCHE_POWER_MODULE_EMIFP              10
#define AVALANCHE_POWER_MODULE_ADSPP              12
#define AVALANCHE_POWER_MODULE_RAMP               13
#define AVALANCHE_POWER_MODULE_ROMP               14
#define AVALANCHE_POWER_MODULE_DMAP               15
#define AVALANCHE_POWER_MODULE_BISTP              16
#define AVALANCHE_POWER_MODULE_TIMER0P            18
#define AVALANCHE_POWER_MODULE_TIMER1P            19
#define AVALANCHE_POWER_MODULE_EMAC0P             20
#define AVALANCHE_POWER_MODULE_EMAC1P             22
#define AVALANCHE_POWER_MODULE_EPHYP              24
#define AVALANCHE_POWER_MODULE_VLYNQ0P            27

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

static void ar7_gpio_display(void)
{
    unsigned index;
    uint32_t in = reg_read(av.gpio, GPIO_IN);
    uint32_t out = reg_read(av.gpio, GPIO_OUT);
    uint32_t dir = reg_read(av.gpio, GPIO_DIR);
    uint32_t enable = reg_read(av.gpio, GPIO_ENABLE);
    char text[32];
    for (index = 0; index < 32; index++) {
        text[index] = (in & BIT(index)) ? '*' : '.';
    }
    qemu_chr_printf(av.gpio_display,
                    "\e[5;1H%32.32s (in  0x%08x)",
                    text, in);
    for (index = 0; index < 32; index++) {
        text[index] = (out & BIT(index)) ? '*' : '.';
    }
    qemu_chr_printf(av.gpio_display,
                    "\e[6;1H%32.32s (out 0x%08x)",
                    text, out);
    for (index = 0; index < 32; index++) {
        text[index] = (dir & BIT(index)) ? '*' : '.';
    }
    qemu_chr_printf(av.gpio_display,
                    "\e[7;1H%32.32s (dir 0x%08x)",
                    text, dir);
    for (index = 0; index < 32; index++) {
        text[index] = (enable & BIT(index)) ? '*' : '.';
    }
    qemu_chr_printf(av.gpio_display,
                    "\e[8;1H%32.32s (ena 0x%08x)",
                    text, enable);
}

static const char *i2gpio(unsigned offset)
{
    static char buffer[10];
    const char *text = buffer;
    switch (offset) {
        case GPIO_IN:
            text = "in";
            break;
        case GPIO_OUT:
            text = "out";
            break;
        case GPIO_DIR:
            text = "dir";
            break;
        case GPIO_ENABLE:
            text = "ena";
            break;
        case GPIO_CVR:
            text = "cvr";
            break;
        default:
            sprintf(buffer, "??? 0x%02x", offset);
    }
    return text;
}

static uint32_t ar7_gpio_read(unsigned offset)
{
    uint32_t value = reg_read(av.gpio, offset);
    if (offset == GPIO_IN && value == 0x00000800) {
        /* Do not log polling of reset button. */
        TRACE(GPIO, logout("gpio[%s] = 0x%08x\n", i2gpio(offset), value));
    } else {
        TRACE(GPIO, logout("gpio[%s] = 0x%08x\n", i2gpio(offset), value));
    }
    return value;
}

static void ar7_gpio_write(unsigned offset, uint32_t value)
{
    TRACE(GPIO, logout("gpio[%s] = 0x%08x\n", i2gpio(offset), value));
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

typedef struct {
    uint32_t phy_control;
#define PHY_CONTROL_REG       0
#define PHY_RESET           BIT(15)
#define PHY_LOOP            BIT(14)
#define PHY_100             BIT(13)
#define AUTO_NEGOTIATE_EN   BIT(12)
#define PHY_PDOWN           BIT(11)
#define PHY_ISOLATE         BIT(10)
#define RENEGOTIATE         BIT(9)
#define PHY_FD              BIT(8)
    uint32_t phy_status;
#define PHY_STATUS_REG        1
#define NWAY_COMPLETE       BIT(5)
#define NWAY_CAPABLE        BIT(3)
#define PHY_LINKED          BIT(2)
    uint32_t dummy2;
    uint32_t dummy3;
    uint32_t nway_advertize;
    uint32_t nway_remadvertize;
#define NWAY_ADVERTIZE_REG    4
#define NWAY_REMADVERTISE_REG 5
#define NWAY_FD100          BIT(8)
#define NWAY_HD100          BIT(7)
#define NWAY_FD10           BIT(6)
#define NWAY_HD10           BIT(5)
#define NWAY_SEL            BIT(0)
#define NWAY_AUTO           BIT(0)
} mdio_user_t;

static uint32_t phy_read(unsigned index)
{
    uint32_t val = reg_read(av.mdio, (index == 0) ? MDIO_USERACCESS0 : MDIO_USERACCESS1);
    TRACE(MDIO, logout("mdio[USERACCESS%u] = 0x%08x\n", index, val));
    return val;
}

static void phy_write(unsigned index, uint32_t val)
{
    unsigned write = (val & MDIO_USERACCESS_WRITE) >> 30;
    unsigned regaddr = (val & MDIO_USERACCESS_REGADR) >> 21;
    unsigned phyaddr = (val & MDIO_USERACCESS_PHYADR) >> 16;
    assert(regaddr < 32);
    TRACE(MDIO,
          logout
          ("mdio[USERACCESS%u] = 0x%08x, write = %u, reg = %u, phy = %u\n",
           index, val, write, regaddr, phyaddr));
    if (val & MDIO_USERACCESS_GO) {
        val &= MDIO_USERACCESS_DATA;
        if (((index == 0 && phyaddr == 31) || (index == 1 && phyaddr == 0)) && regaddr < 32) {
            phyaddr = 0;
            if (write) {
                //~ if ((regaddr == PHY_CONTROL_REG) && (val & PHY_RESET)) {
                //~ 1000 7809 0000 0000 01e1 0001
                //~ mdio_useraccess_data[0][PHY_CONTROL_REG] = 0x1000;
                //~ mdio_useraccess_data[0][PHY_STATUS_REG] = 0x782d;
                //~ mdio_useraccess_data[0][NWAY_ADVERTIZE_REG] = 0x01e1;
                /* 100FD=Yes, 100HD=Yes, 10FD=Yes, 10HD=Yes */
                //~ mdio_useraccess_data[0][NWAY_REMADVERTISE_REG] = 0x85e1;
                //~ }
                av.phy[regaddr] = val;
            } else {
                val = av.phy[regaddr];
                if ((regaddr == PHY_CONTROL_REG) && (val & PHY_RESET)) {
                    av.phy[regaddr] =
                        ((val & ~PHY_RESET) | AUTO_NEGOTIATE_EN);
                } else if ((regaddr == PHY_CONTROL_REG)
                           && (val & RENEGOTIATE)) {
                    val &= ~RENEGOTIATE;
                    av.phy[regaddr] = val;
                    //~ 0x0000782d 0x00007809
                    av.phy[1] = 0x782d;
                    av.phy[5] = av.phy[4] | PHY_ISOLATE | PHY_RESET;
                    reg_write(av.mdio, MDIO_LINK, 0x80000000);
                } else if (regaddr == PHY_STATUS_REG) {
                    val |= PHY_LINKED | NWAY_CAPABLE | NWAY_COMPLETE;
                }
            }
        }
    }

    reg_write(av.mdio,
              (index == 0) ? MDIO_USERACCESS0 : MDIO_USERACCESS1,
              val);

#if 0
    uint8_t raiseint = (val & 0x20000000) >> 29;
    uint8_t opcode = (val & 0x0c000000) >> 26;
    uint8_t phy = (val & 0x03e00000) >> 21;
    uint8_t reg = (val & 0x001f0000) >> 16;
    uint16_t data = (val & 0x0000ffff);
    if (phy != 1) {
        /* Unsupported PHY address. */
        //~ logout("phy must be 1 but is %u\n", phy);
        data = 0;
    } else if (opcode != 1 && opcode != 2) {
        /* Unsupported opcode. */
        logout("opcode must be 1 or 2 but is %u\n", opcode);
        data = 0;
    } else if (reg > 6) {
        /* Unsupported register. */
        logout("register must be 0...6 but is %u\n", reg);
        data = 0;
    } else {
        TRACE(MDI, logout("val=0x%08x (int=%u, %s, phy=%u, %s, data=0x%04x\n",
                          val, raiseint, mdi_op_name[opcode], phy,
                          mdi_reg_name[reg], data));
        if (opcode == 1) {
            /* MDI write */
            switch (reg) {
            case 0:            /* Control Register */
                if (data & 0x8000) {
                    /* Reset status and control registers to default. */
                    s->mdimem[0] = eepro100_mdi_default[0];
                    s->mdimem[1] = eepro100_mdi_default[1];
                    data = s->mdimem[reg];
                } else {
                    /* Restart Auto Configuration = Normal Operation */
                    data &= ~0x0200;
                }
                break;
            case 1:            /* Status Register */
                missing("not writable");
                data = s->mdimem[reg];
                break;
            case 2:            /* PHY Identification Register (Word 1) */
            case 3:            /* PHY Identification Register (Word 2) */
                missing("not implemented");
                break;
            case 4:            /* Auto-Negotiation Advertisement Register */
            case 5:            /* Auto-Negotiation Link Partner Ability Register */
                break;
            case 6:            /* Auto-Negotiation Expansion Register */
            default:
                missing("not implemented");
            }
            s->mdimem[reg] = data;
        } else if (opcode == 2) {
            /* MDI read */
            switch (reg) {
            case 0:            /* Control Register */
                if (data & 0x8000) {
                    /* Reset status and control registers to default. */
                    s->mdimem[0] = eepro100_mdi_default[0];
                    s->mdimem[1] = eepro100_mdi_default[1];
                }
                break;
            case 1:            /* Status Register */
                s->mdimem[reg] |= 0x0020;
                break;
            case 2:            /* PHY Identification Register (Word 1) */
            case 3:            /* PHY Identification Register (Word 2) */
            case 4:            /* Auto-Negotiation Advertisement Register */
                break;
            case 5:            /* Auto-Negotiation Link Partner Ability Register */
                s->mdimem[reg] = 0x41fe;
                break;
            case 6:            /* Auto-Negotiation Expansion Register */
                s->mdimem[reg] = 0x0001;
                break;
            }
            data = s->mdimem[reg];
        }
        /* Emulation takes no time to finish MDI transaction.
         * Set MDI bit in SCB status register. */
        s->mem[SCBAck] |= 0x08;
        val |= BIT(28);
        if (raiseint) {
            eepro100_mdi_interrupt(s);
        }
    }
    val = (val & 0xffff0000) + data;
    memcpy(&s->mem[0x10], &val, sizeof(val));
#endif
}

static void phy_enable(void)
{
    av.phy[0] = AUTO_NEGOTIATE_EN;
    av.phy[1] = 0x7801 + NWAY_CAPABLE;     // + NWAY_COMPLETE + PHY_LINKED,
    av.phy[2] = 0x00000000;
    av.phy[3] = 0x00000000;
    av.phy[4] = NWAY_FD100 + NWAY_HD100 + NWAY_FD10 + NWAY_HD10 + NWAY_AUTO;
    av.phy[5] = NWAY_AUTO;
    reg_write(av.mdio, MDIO_ALIVE, BIT(31));
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
        if (val != 0x80000000) {
            text = "LINK";
        }
    } else if (offset == MDIO_USERACCESS0) {
        val = phy_read(0);
    } else if (offset == MDIO_USERACCESS1) {
        val = phy_read(1);
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
            } else {
              TRACE(MDIO, logout("disable MDIO state machine\n"));
            }
        }
        reg_write(mdio, offset, val);
    } else if (offset == MDIO_USERACCESS0) {
        phy_write(0, val);
    } else if (offset == MDIO_USERACCESS1) {
        phy_write(1, val);
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
            if (changed & (1 << i)) {
                TRACE(RESET,
                      logout("reset %sabled %s (0x%08x)\n",
                             (enabled & (1 << i)) ? "en" : "dis",
                             resetdevice[i], val));
            }
        }
#endif
    } else if (offset == 4) {
        TRACE(RESET, logout("reset\n"));
        qemu_system_reset_request();
        //~ CPUState *cpu_env = first_cpu;
        //~ cpu_env->PC = 0xbfc00000;
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

    TRACE(TIMER, logout("timer expired\n"));
    ar7_irq(IRQ_OPAQUE, timer->interrupt, 1);
    if (timer->cyclic) {
        int64_t t = qemu_get_clock(vm_clock);
        qemu_mod_timer(timer->qemu_timer, t + timer->prescale * timer->time);
    }
}

static uint32_t ar7_timer_read(unsigned index, uint32_t addr)
{
    ar7_timer_t *timer = &av.timer[index];
    uint32_t val;
    val = reg_read(timer->base, addr);
    TRACE(TIMER, logout("timer%u[%d]=0x%08x\n", index, addr, val));
    return val;
}

static void ar7_timer_write(unsigned index, uint32_t addr, uint32_t val)
{
    ar7_timer_t *timer = &av.timer[index];
    TRACE(TIMER, logout("timer%u[%d]=0x%08x\n", index, addr, val));
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

static const int uart_interrupt[] = { 15, 16 };

/* Status of DLAB bit. */
static uint32_t dlab[2];

static inline unsigned uart_name_index(unsigned index, unsigned reg)
{
    if (reg < 2 && dlab[index]) {
        reg += 8;
    }
    return reg;
}

static uint32_t uart_read(unsigned index, uint32_t addr)
{
    uint32_t val;
    int port = UART_MEM_TO_IO(addr);
    unsigned reg = port;
    if (index == 1) {
        reg -= UART_MEM_TO_IO(AVALANCHE_UART1_BASE);
    }
    assert(reg < 8);
    val = serial_read(av.serial[index], reg);
    //~ if (reg != 5) {
        TRACE(UART, logout("uart%u[%s]=0x%08x\n",
            index, uart_read_names[uart_name_index(index, reg)], val));
    //~ }
    return val;
}

static void uart_write(unsigned index, uint32_t addr, uint32_t val)
{
    int port = UART_MEM_TO_IO(addr);
    unsigned reg = port;
    if (index == 1) {
        reg -= UART_MEM_TO_IO(AVALANCHE_UART1_BASE);
    }
    assert(reg < 8);
    //~ if (reg != 0 || dlab[index]) {
        TRACE(UART, logout("uart%u[%s]=0x%08x\n",
            index, uart_write_names[uart_name_index(index, reg)], val));
    //~ }
    if (reg == 3) {
        dlab[index] = (val & 0x80);
    }
    serial_write(av.serial[index], reg, val);
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

static uint32_t ar7_vlynq_read(unsigned index, unsigned offset)
{
    uint8_t *vlynq = av.vlynq[index];
    uint32_t val = reg_read(vlynq, offset);
    TRACE(VLYNQ, logout("vlynq%u[0x%02x (%s)] = 0x%08lx\n",
                        index, offset,
                        vlynq_names[offset / 4],
                        (unsigned long)val));
    if (offset == VLYNQ_REVID) {
        val = cpu_to_le32(0x00010206);
    } else if (offset == VLYNQ_INTSTATCLR) {
        reg_write(vlynq, offset, 0);
    } else if (index == 0 && offset == VLYNQ_RCHIPVER) {
        val = cpu_to_le32(0x00000009);
    } else {
    }
    return val;
}

static void ar7_vlynq_write(unsigned index, unsigned offset, uint32_t val)
{
    uint8_t *vlynq = av.vlynq[index];
    TRACE(VLYNQ, logout("vlynq%u[0x%02x (%s)] = 0x%08lx\n",
                        index, offset,
                        vlynq_names[offset / 4],
                        (unsigned long)val));
    if (offset == VLYNQ_REVID) {
    } else if (offset == VLYNQ_CTRL && index == 0) {
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

void watchdog_trigger(void)
{
    wdtimer_t *wdt = (wdtimer_t *) & av.watchdog;
    if (wdt->disable == 0) {
        TRACE(WDOG, logout("disabled watchdog\n"));
        qemu_del_timer(av.wd_timer);
    } else {
        int64_t t = ((uint64_t)wdt->change * (uint64_t)wdt->prescale) * (ticks_per_sec / io_frequency);
        //~ logout("change   = 0x%x\n", wdt->change);
        //~ logout("prescale = 0x%x\n", wdt->prescale);
        TRACE(WDOG, logout("trigger value = %u ms\n", (unsigned)(t * 1000 / ticks_per_sec)));
        //~ logout("trigger value = %u\n", (unsigned)(ticks_per_sec / 1000000));
        qemu_mod_timer(av.wd_timer, qemu_get_clock(vm_clock) + t);
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
    unsigned index;
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
        name = "atm sar";
        val = VALUE(AVALANCHE_ATM_SAR_BASE, av.atmsar);
        index = (addr - AVALANCHE_ATM_SAR_BASE);
        if (val == 0 && index == 0x90) {
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
    } else if (INRANGE(AVALANCHE_VLYNQ0_BASE + VLYNQ_CTRL, 4) ||
               INRANGE(AVALANCHE_GPIO_BASE, av.gpio)) {
        uint32_t oldvalue = ar7_io_memread(opaque, addr & ~3);
        logout("??? addr=0x%08x, val=0x%02x\n", addr, value);
        oldvalue &= ~(0xff << 8 * (addr & 3));
        value = oldvalue + ((value & 0xff) << 8 * (addr & 3));
        ar7_io_memwrite(0, addr & ~3, value);
    } else if (addr & 3) {
        ar7_io_memwrite(opaque, addr & ~3, value);
        logout("addr=0x%08x, val=0x%02x\n", addr, value);
        UNEXPECTED();
    } else if (INRANGE(AVALANCHE_UART0_BASE, av.uart0)) {
        ar7_io_memwrite(opaque, addr, value);
    } else if (INRANGE(AVALANCHE_UART1_BASE, av.uart1)) {
        ar7_io_memwrite(opaque, addr, value);
    } else {
        ar7_io_memwrite(opaque, addr, value);
        logout("??? addr=0x%08x, val=0x%02x\n", addr, value);
        //~ UNEXPECTED();
    }
    //~ cpu_outb(NULL, addr & 0xffff, value);
}

static uint32_t io_readb(void *opaque, target_phys_addr_t addr)
{
    uint32_t value = ar7_io_memread(opaque, addr & ~3);
    if (0) {
    } else if (INRANGE(AVALANCHE_BBIF_BASE, av.bbif)) {
        value >>= (addr & 3) * 8;
        value &= 0xff;
        logout("??? addr=0x%08x, val=0x%02x\n", addr, value);
    } else if (INRANGE(AVALANCHE_GPIO_BASE, av.gpio)) {
        value >>= (addr & 3) * 8;
        value &= 0xff;
        logout("??? addr=0x%08x, val=0x%02x\n", addr, value);
    } else if (addr & 3) {
        logout("addr=0x%08x, val=0x%02x\n", addr, value);
        UNEXPECTED();
    } else if (INRANGE(AVALANCHE_CLOCK_BASE, av.clock_control)) {
        //~ value = clock_read(addr - AVALANCHE_CLOCK_BASE);
    } else if (INRANGE(AVALANCHE_UART0_BASE, av.uart0)) {
    } else if (INRANGE(AVALANCHE_UART1_BASE, av.uart1)) {
    } else if (INRANGE(AVALANCHE_UART1_BASE, av.uart1)) {
    } else {
        logout("addr=0x%08x, val=0x%02x\n", addr, value & 0xff);
        UNEXPECTED();
    }
    value &= 0xff;
    return value;
}

static void io_writew(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    if (0) {
    } else {
        logout("??? addr=0x%08x, val=0x%04x\n", addr, value);
        switch (addr & 3) {
        case 0:
            ar7_io_memwrite(opaque, addr, value);
            break;
        case 2:
            value <<= 16;
            //~ UNEXPECTED();
            ar7_io_memwrite(opaque, addr - 2, value);
            break;
        default:
            assert(0);
        }
    }
}

static uint32_t io_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t value = 0;
    if (0) {
    } else {
      value = ar7_io_memread(opaque, addr & ~3);
      switch (addr & 3) {
      case 0:
          value &= 0xffff;
          break;
      case 2:
          value >>= 16;
          break;
      default:
          assert(0);
      }
      TRACE(OTHER, logout("addr=0x%08x, val=0x%04x\n", addr, value));
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
    unsigned index;
    if (serial_hds[1] == 0) {
        serial_hds[1] = qemu_chr_open("vc");
        qemu_chr_printf(serial_hds[1], "serial1 console\r\n");
    }
    for (index = 0; index < 2; index++) {
        av.serial[index] = serial_16550_init(ar7_irq, IRQ_OPAQUE,
                                             0, uart_interrupt[index],
                                             serial_hds[index]);
        serial_frequency(av.serial[index], io_frequency / 16);
    }
    /* Select 1st serial console as default (because we don't have VGA). */
    console_select(1);
}

static int ar7_nic_can_receive(void *opaque)
{
    unsigned index = (unsigned)opaque;
    uint8_t *cpmac = av.cpmac[index];
    int enabled = (reg_read(cpmac, CPMAC_RXCONTROL) & RXCONTROL_RXEN) != 0;

    TRACE(CPMAC, logout("cpmac%u, enabled %d\n", index, enabled));

    return enabled;
}

static void ar7_nic_receive(void *opaque, const uint8_t * buf, int size)
{
    unsigned index = (unsigned)opaque;
    uint8_t *cpmac = av.cpmac[index];
    uint32_t rxmbpenable = reg_read(cpmac, CPMAC_RXMBPENABLE);
    uint32_t rxmaxlen = reg_read(cpmac, CPMAC_RXMAXLEN);
    unsigned channel = 0xff;
    uint32_t flags = 0;

    if (!(reg_read(cpmac, CPMAC_MACCONTROL) & MACCONTROL_GMIIEN)) {
        TRACE(CPMAC, logout("cpmac%u MII is disabled, frame ignored\n",
              index));
        return;
    } else if (!(reg_read(cpmac, CPMAC_RXCONTROL) & RXCONTROL_RXEN)) {
        TRACE(CPMAC, logout("cpmac%u receiver is disabled, frame ignored\n",
          index));
        return;
    }

    TRACE(RXTX,
          logout("cpmac%u received %u byte: %s\n", index, size,
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
        statusreg_inc(index, CPMAC_RXBROADCASTFRAMES);
        TRACE(CPMAC, logout("broadcast to channel %d\n", channel));
    } else if ((rxmbpenable & RXMBPENABLE_RXMULTEN) && (buf[0] & 0x01)) {
        // !!! must check MACHASH1, MACHASH2
        channel = ((rxmbpenable & RXMBPENABLE_RXMULTCH) >> 0);
        statusreg_inc(index, CPMAC_RXMULTICASTFRAMES);
        TRACE(CPMAC, logout("multicast to channel %d\n", channel));
    } else if (!memcmp(buf, av.nic[index].phys, 6)) {
        channel = 0;
        TRACE(CPMAC, logout("my address to channel %d\n", channel));
    } else if (rxmbpenable & RXMBPENABLE_RXCAFEN) {
        channel = ((rxmbpenable & RXMBPENABLE_RXPROMCH) >> 16);
        //~ statusreg_inc(index, CPMAC_RXMULTICASTFRAMES);
        TRACE(CPMAC, logout("promicuous to channel %d\n", channel));
        flags |= RCB_NOMATCH;
    } else {
        TRACE(CPMAC, logout("unknown address, frame ignored\n"));
        return;
    }

    /* !!! check handling of short and long frames */
    if (size < 64) {
        TRACE(CPMAC, logout("short frame, flag = 0x%x\n",
          rxmbpenable & RXMBPENABLE_RXCSFEN));
        statusreg_inc(index, CPMAC_RXUNDERSIZEDFRAMES);
        flags |= RCB_UNDERSIZED;
    } else if (size > rxmaxlen) {
        statusreg_inc(index, CPMAC_RXOVERSIZEDFRAMES);
        flags |= RCB_OVERSIZE;
    }

    statusreg_inc(index, CPMAC_RXGOODFRAMES);

    assert(channel < 8);
    uint32_t val = reg_read(cpmac, CPMAC_RX0HDP + 4 * channel);
    if (val == 0) {
        TRACE(RXTX, logout("no buffer available, frame ignored\n"));
    } else {
        cpphy_rcb_t rcb;
        cpu_physical_memory_read(val, (uint8_t *) & rcb, sizeof(rcb));
        uint32_t addr = le32_to_cpu(rcb.buff);
        uint32_t length = le32_to_cpu(rcb.length);
        uint32_t mode = le32_to_cpu(rcb.mode);
        TRACE(CPMAC,
              logout
              ("buffer 0x%08x, next 0x%08x, buff 0x%08x, params 0x%08x, len 0x%08x\n",
               val, (unsigned)rcb.next, addr, mode, length));
        if (mode & RCB_OWNER) {
            assert(length >= size);
            mode &= ~(RCB_OWNER);
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
            cpu_physical_memory_write(val, (uint8_t *) & rcb, sizeof(rcb));
            reg_write(cpmac, CPMAC_RX0HDP + 4 * channel, rcb.next);
            reg_write(cpmac, CPMAC_RX0CP + 4 * channel, val);
            reg_set(cpmac, CPMAC_RXINTSTATRAW, 1 << channel);
            emac_update_interrupt(index);
        } else {
            logout("buffer not free, frame ignored\n");
        }
    }
}

static void ar7_nic_init(void)
{
    unsigned i;
    unsigned n = 0;
    TRACE(CPMAC, logout("\n"));
    for (i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];
        if (nd->vlan) {
            if (n < 2 && (nd->model == NULL || strcmp(nd->model, "ar7") == 0)) {
                TRACE(CPMAC, logout("starting AR7 nic CPMAC%u\n", n));
                av.nic[n].vc = qemu_new_vlan_client(nd->vlan, ar7_nic_receive,
                                                      ar7_nic_can_receive,
                                                      (void *)n);
                n++;
                emac_reset(n);
            } else {
                fprintf(stderr, "qemu: Unsupported NIC: %s\n",
                        nd_table[n].model);
                exit(1);
            }
        }
    }
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
    logout("%p, %d\n", opaque, event);
    //~ if (event == CHR_EVENT_BREAK)
}

static void ar7_display_init(CPUState *env, const char *devname)
{
    av.gpio_display = qemu_chr_open(devname);
    qemu_chr_add_handlers(av.gpio_display, ar7_display_can_receive,
                          ar7_display_receive, ar7_display_event, 0);
    if (!strcmp(devname, "vc")) {
        qemu_chr_printf(av.gpio_display,
                        "\e[1;1HGPIO Status"
                        "\e[2;1H0         1         2         3"
                        "\e[3;1H01234567890123456789012345678901"
                        "\e[10;1H* lan * wlan * online * dsl * power"
                        "\e[12;1HPress 'r' to toggle the reset button");
        ar7_gpio_display();
    }
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

void ar7_init(CPUState * env)
{
    //~ target_phys_addr_t addr = (0x08610000 & 0xffff);
    //~ unsigned offset;
    int io_memory = cpu_register_io_memory(0, io_read, io_write, env);
    //~ cpu_register_physical_memory(0x08610000, 0x00002800, io_memory);
    //~ cpu_register_physical_memory(0x00001000, 0x0860f000, io_memory);
    cpu_register_physical_memory(0x00001000, 0x0ffff000 - GDBRAM, io_memory);
    //~ cpu_register_physical_memory(0x00001000, 0x10000000, io_memory);
    cpu_register_physical_memory(0x1e000000, 0x01c00000, io_memory);

    //~ reg_write(av.gpio, GPIO_IN, 0x0cbea075);
    reg_write(av.gpio, GPIO_IN, 0x0cbea875);
    //~ reg_write(av.gpio, GPIO_OUT, 0x00000000);
    reg_write(av.gpio, GPIO_DIR, 0xffffffff);
    reg_write(av.gpio, GPIO_ENABLE, 0xffffffff);
    reg_write(av.gpio, GPIO_CVR, 0x00020005);
    //~ reg_write(av.gpio, GPIO_DIDR1, 0x7106150d);
    //~ reg_write(av.gpio, GPIO_DIDR2, 0xf52ccccf);

  //~ .mdio = {0x00, 0x07, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff}
    reg_write(av.mdio, MDIO_VERSION, 0x00070101);
    reg_write(av.mdio, MDIO_CONTROL, MDIO_CONTROL_IDLE | BIT(24) | BITS(7, 0));
    //~ reg_write(av.mdio, MDIO_ALIVE, BIT(31));

    //~ .uart0 = {0, 0, 0, 0, 0, 0x20, 0},
    reg_write(av.uart0, 5 * 4, 0x20);
    //~ .reset_control = { 0x04720043 },

    //~ .dcl = 0x025d4297
    reg_write(av.dcl, DCL_BOOT_CONFIG, 0x025d4291);
#if defined(TARGET_WORDS_BIGENDIAN)
    reg_set(av.dcl, DCL_BOOT_CONFIG, CONFIG_ENDIAN);
#endif

    av.cpmac[0] = av.cpmac0;
    av.cpmac[1] = av.cpmac1;
    av.vlynq[0] = av.vlynq0;
    av.vlynq[1] = av.vlynq1;
    av.cpu_env = env;

    ar7_serial_init(env);
    ar7_display_init(env, "vc");
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
    //~ s_io_memory = cpu_register_io_memory(&state, mips_mm_read, mips_mm_write, 0);
    //~ cpu_register_physical_memory(0x08610000, 0x2000, s_io_memory);
    //~ }
#define ar7_instance 0
#define ar7_version 0
    qemu_register_reset(ar7_reset, env);
    register_savevm("ar7", ar7_instance, ar7_version, ar7_save, ar7_load, 0);
}

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);
    /* AR7 is MIPS32 release 1. */
    env->CP0_Config0 &= ~(7 << CP0C0_AR);
    /* AR7 has no FPU. */
    env->CP0_Config1 &= ~(1 << CP0C1_FP);

    if (env->kernel_filename) {
        mips_load_kernel (env, env->ram_size, env->kernel_filename,
                          env->kernel_cmdline, env->initrd_filename);
    }
}

static void mips_ar7_common_init (int ram_size,
                    uint16_t flash_manufacturer, uint16_t flash_type,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    char buf[1024];
    int64_t entry = 0;
    CPUState *env;
    mips_def_t *def;
    int flash_size;
    int kernel_size;
    ram_addr_t flash_offset;
    ram_addr_t ram_offset;

    /* Typical AR7 systems run in little endian mode.
       Zyxel uses big endian, so this mode must be supported, too. */
#if defined(TARGET_WORDS_BIGENDIAN)
    bigendian = 1;
#else
    bigendian = 0;
#endif
    //~ bigendian = env->bigendian;
    fprintf(stderr, "%s: setting endianness %d\n", __func__, bigendian);

    /* Initialize CPU. */
    if (cpu_model == NULL) {
#ifdef MIPS_HAS_MIPS64
# error AR7 has a 32 bit CPU
#endif
        cpu_model = "4KEcR1";
    }
    if (mips_find_by_name(cpu_model, &def) != 0) {
        def = NULL;
    }
    env = cpu_init();
    cpu_mips_register(env, def);
    register_savevm("cpu", 0, 3, cpu_save, cpu_load, env);

    /* Have config1, is MIPS32R1, uses TLB, no virtual icache,
       uncached coherency */
    env->CP0_Config0 =
        ((1 << CP0C0_M) | (0x0 << CP0C0_K23) | (0x0 << CP0C0_KU) |
         (1 << 21) | (0x2 << CP0C0_MM) |
         (0x0 << CP0C0_AT) | (0x0 << CP0C0_AR) | (0x1 << CP0C0_MT) |
         (0x2 << CP0C0_K0));
    if (bigendian) {
        env->CP0_Config0 |= (1 << CP0C0_BE);
    }
    /* Have config2, 16 TLB entries, 256 sets Icache, 16 bytes Icache line,
       4-way Icache, 256 sets Dcache, 16 bytes Dcache line, 4-way Dcache,
       no coprocessor2 attached, no MDMX support attached,
       no performance counters, watch registers present,
       no code compression, EJTAG present, FPU enable bit depending on
       MIPS_USES_FPU */
    env->CP0_Config1 =
        ((1 << CP0C1_M) | ((MIPS_TLB_NB - 1) << CP0C1_MMU) |
         (0x2 << CP0C1_IS) | (0x3 << CP0C1_IL) | (0x3 << CP0C1_IA) |
         (0x2 << CP0C1_DS) | (0x3 << CP0C1_DL) | (0x3 << CP0C1_DA) |
         (0 << CP0C1_C2) | (0 << CP0C1_MD) | (0 << CP0C1_PC) |
         (1 << CP0C1_WR) | (0 << CP0C1_CA) | (1 << CP0C1_EP));
    /* Have config3, no tertiary/secondary caches implemented */
    env->CP0_Config2 = (1 << CP0C2_M);
    /* No config4, no DSP ASE, no large physaddr,
       no external interrupt controller, no vectored interupts,
       no 1kb pages, no MT ASE, no SmartMIPS ASE, no trace logic */
    env->CP0_Config3 =
        ((0 << CP0C3_M) | (0 << CP0C3_DSPP) | (0 << CP0C3_LPA) |
         (0 << CP0C3_VEIC) | (0 << CP0C3_VInt) | (0 << CP0C3_SP) |
         (0 << CP0C3_MT) | (0 << CP0C3_SM) | (0 << CP0C3_TL));

    if (env->CP0_Config0 != 0x80240082) printf("CP0_Config0 = 0x%08x\n", env->CP0_Config0);
    if (env->CP0_Config1 != 0x9e9b4d8a) printf("CP0_Config1 = 0x%08x\n", env->CP0_Config1);
    if (env->CP0_Config2 != 0x80000000) printf("CP0_Config2 = 0x%08x\n", env->CP0_Config2);
#if defined(TARGET_WORDS_BIGENDIAN)
#else
    assert(env->CP0_Config0 == 0x80240082);
#endif
    assert(env->CP0_Config1 == 0x9e9b4d8a);
    assert(env->CP0_Config2 == 0x80000000);
    assert(env->CP0_Config3 == 0x00000000);

    register_savevm("cpu", 0, 3, cpu_save, cpu_load, env);
    qemu_register_reset(main_cpu_reset, env);

    /* The AR7 processor has 4 KiB internal RAM at physical address 0x00000000. */
    ram_offset = qemu_ram_alloc(4 * KiB);
    cpu_register_physical_memory(0, 4 * KiB, ram_offset | IO_MEM_RAM);

    /* Allocate 0x1000 bytes before start of flash (needed for gdb). */
    ram_offset = qemu_ram_alloc(GDBRAM);
    cpu_register_physical_memory(0x10000000 - GDBRAM, GDBRAM, ram_offset | IO_MEM_RAM);

    /* Allocate 0x1000 bytes before start of ram (needed for gdb). */
    ram_offset = qemu_ram_alloc(GDBRAM);
    cpu_register_physical_memory(KERNEL_LOAD_ADDR - GDBRAM, GDBRAM, ram_offset | IO_MEM_RAM);

    /* 16 MiB external RAM at physical address KERNEL_LOAD_ADDR.
       More memory can be selected with command line option -m. */
    if (ram_size > 100 * MiB) {
            ram_size = 16 * MiB;
    }
    ram_offset = qemu_ram_alloc(ram_size);
    cpu_register_physical_memory(KERNEL_LOAD_ADDR, ram_size, ram_offset | IO_MEM_RAM);
    fprintf(stderr, "%s: ram_base = %p, ram_size = 0x%08x\n",
        __func__, phys_ram_base, ram_size);

    /* Try to load a BIOS image. If this fails, we continue regardless,
       but initialize the hardware ourselves. When a kernel gets
       preloaded we also initialize the hardware, since the BIOS wasn't
       run. */
    flash_offset = qemu_ram_alloc(0);

    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, "flashimage.bin");
    flash_size = load_image(buf, phys_ram_base + flash_offset);
    fprintf(stderr, "%s: load BIOS '%s', size %d\n", __func__, buf, flash_size);
    if (flash_size > 0) {
        const uint32_t address = 0x10000000;
        pflash_t *pf;
        flash_offset = qemu_ram_alloc(flash_size);
        pf = pflash_register(address, flash_offset, 0, flash_size, 2,
                             flash_manufacturer, flash_type);
    }

    /* The AR7 processor has 4 KiB internal ROM at physical address 0x1fc00000. */
    flash_offset = qemu_ram_alloc(4 * KiB);
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, "mips_bios.bin");
    flash_size = load_image(buf, phys_ram_base + flash_offset);
    if ((flash_size > 0) && (flash_size <= 4 * KiB)) {
        fprintf(stderr, "%s: load BIOS '%s', size %d\n", __func__, buf, flash_size);
    } else {
        /* Not fatal, write a jump to address 0xb0000000 into memory. */
        static const uint8_t jump[] = {
            /* lui t9,0xb000; jr t9 */
            0x00, 0xb0, 0x19, 0x3c, 0x08, 0x00, 0x20, 0x03
        };
        fprintf(stderr, "QEMU: Warning, could not load MIPS bios '%s'.\n"
                "QEMU added a jump instruction to flash start.\n", buf);
        memcpy (phys_ram_base + flash_offset, jump, sizeof(jump));
        flash_size = 4 * KiB;
    }
    cpu_register_physical_memory((uint32_t)(0x1fc00000),
                                 flash_size, flash_offset | IO_MEM_ROM);

    kernel_size = 0;
    if (kernel_filename) {
        kernel_size = load_elf(kernel_filename, VIRT_TO_PHYS_ADDEND, &entry);
        if (kernel_size >= 0) {
            fprintf(stderr, "qemu: elf kernel '%s' with start address 0x%08lx\n",
                        kernel_filename, (unsigned long)entry);
            env->PC = entry;
        } else {
            kernel_size = load_image(kernel_filename,
                                phys_ram_base + ram_offset);
            if (kernel_size > 0 && kernel_size < ram_size) {
                fprintf(stderr, "qemu: elf kernel '%s' with size 0x%08x\n",
                            kernel_filename, kernel_size);
            } else {
                fprintf(stderr, "qemu: could not load kernel '%s'\n",
                        kernel_filename);
                exit(1);
            }
            env->PC = K1(KERNEL_LOAD_ADDR);
        }

        /* a0 = argc, a1 = argv, a2 = envp */
        env->gpr[4] = 0;
        env->gpr[5] = K1(INITRD_LOAD_ADDR);
        env->gpr[6] = K1(INITRD_LOAD_ADDR);

        /* Set SP (needed for some kernels) - normally set by bootloader. */
        env->gpr[29] = env->PC + ram_size - 0x1000;

        if (initrd_filename) {
            /* Load kernel parameters (argv, envp) from file. */
            uint8_t *address = phys_ram_base + ram_offset + INITRD_LOAD_ADDR - KERNEL_LOAD_ADDR;
            int argc;
            uint8_t **argv;
            uint8_t **arg0;
            target_ulong size = load_image(initrd_filename, address);
            target_ulong i;
            if (size == (target_ulong) -1) {
                fprintf(stderr, "qemu: could not load kernel parameters '%s'\n",
                        initrd_filename);
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
            argv = (uint8_t **)(address + i);
            env->gpr[5] = K1(INITRD_LOAD_ADDR + i);
            arg0 = argv;
            *argv = (uint8_t *)K1(INITRD_LOAD_ADDR);
            for (i = 0; i < size;) {
                uint8_t c = address[i++];
                if (c == '\0') {
                    *++argv = (uint8_t *)K1(INITRD_LOAD_ADDR + i);
                    if (address[i] == '\0' && argc == 0) {
                      argc = argv - arg0;
                      *argv = (uint8_t *)0;
                      env->gpr[4] = argc;
                      env->gpr[6] = env->gpr[5] + 4 * (argc + 1);
                    }
                }
            }
        }
    }

    /* Init internal devices */
    cpu_mips_clock_init(env);
    //~ cpu_mips_irqctrl_init();

    av.wd_timer = qemu_new_timer(vm_clock, &watchdog_cb, env);
    av.timer[0].qemu_timer = qemu_new_timer(vm_clock, &timer_cb, &av.timer[0]);
    av.timer[0].base = av.timer0;
    av.timer[0].interrupt = 13;
    av.timer[1].qemu_timer = qemu_new_timer(vm_clock, &timer_cb, &av.timer[1]);
    av.timer[1].base = av.timer1;
    av.timer[1].interrupt = 14;

    ar7_init(env);

#if defined(DEBUG_AR7)
    set_traceflags();
#endif
}

static void mips_ar7_init(int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    mips_ar7_common_init (ram_size, MANUFACTURER_ST, 0x2249,
                          kernel_filename, kernel_cmdline, initrd_filename,
                          cpu_model);
}

static void ar7_amd_init(int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    mips_ar7_common_init (ram_size, MANUFACTURER_AMD, AM29LV160DB,
                          kernel_filename, kernel_cmdline, initrd_filename,
                          cpu_model);
}

#if defined(TARGET_WORDS_BIGENDIAN)

static void zyxel_init(int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    mips_ar7_common_init (8 * MiB, MANUFACTURER_INTEL, I28F160C3B,
                          kernel_filename, kernel_cmdline, initrd_filename,
                          cpu_model);
}

#else

static void fbox4_init(int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    mips_ar7_common_init (32 * MiB, MANUFACTURER_MACRONIX, MX29LV320CT,
                          kernel_filename, kernel_cmdline, initrd_filename,
                          cpu_model);
}

static void fbox8_init(int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    mips_ar7_common_init (32 * MiB, MANUFACTURER_MACRONIX, MX29LV640BT,
                          kernel_filename, kernel_cmdline, initrd_filename,
                          cpu_model);
}

static void sinus_3_init(int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    mips_ar7_common_init (16 * MiB, MANUFACTURER_004A, ES29LV160DB,
                          kernel_filename, kernel_cmdline, initrd_filename,
                          cpu_model);
}

static void sinus_se_init(int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    mips_ar7_common_init (16 * MiB, MANUFACTURER_INTEL, I28F160C3B,
                          kernel_filename, kernel_cmdline, initrd_filename,
                          cpu_model);
}

#endif

static QEMUMachine mips_machines[] = {
  {
    "ar7",
    "MIPS 4KEc / AR7 platform",
    mips_ar7_init,
  },
  {
    "ar7-amd",
    "MIPS AR7 with AMD flash",
    ar7_amd_init,
  },
#if defined(TARGET_WORDS_BIGENDIAN)
  {
    "zyxel",
    "Zyxel 2 MiB flash (AR7 platform)",
    zyxel_init,
  },
#else
  {
    "fbox-4mb",
    "FBox 4 MiB flash (AR7 platform)",
    fbox4_init,
  },
  {
    "fbox-8mb",
    "FBox 8 MiB flash (AR7 platform)",
    fbox8_init,
  },
  {
    "sinus-se",
    "Sinus DSL SE, Sinus DSL Basic SE (AR7 platform)",
    sinus_se_init,
  },
  {
    "sinus-3",
    "Sinus DSL Basic 3 (AR7 platform)",
    sinus_3_init,
  },
#endif
};

int qemu_register_ar7_machines(void)
{
    size_t i;
    for (i = 0; i < sizeof(mips_machines) / sizeof(*mips_machines); i++) {
        qemu_register_machine(&mips_machines[i]);
    }
    return 0;
}

/* eof */

/*
AR7     phy_read                mdio[USERACCESS0] = 0x000001e1, reg = 0, phy = 0
AR7     phy_write               mdio[USERACCESS0] = 0x803f0000, write = 0, reg = 1, phy = 31
AR7     phy_read                mdio[USERACCESS0] = 0x00007809, reg = 0, phy = 0
AR7     phy_read                mdio[USERACCESS0] = 0x00007809, reg = 0, phy = 0
AR7     phy_write               mdio[USERACCESS0] = 0x803f0000, write = 0, reg = 1, phy = 31
AR7     phy_read                mdio[USERACCESS0] = 0x00007809, reg = 0, phy = 0
AR7     phy_read                mdio[USERACCESS0] = 0x00007809, reg = 0, phy = 0
AR7     phy_write               mdio[USERACCESS0] = 0x80bf0000, write = 0, reg = 5, phy = 31
AR7     phy_read                mdio[USERACCESS0] = 0x00000001, reg = 0, phy = 0
AR7     phy_read                mdio[USERACCESS0] = 0x00000001, reg = 0, phy = 0
AR7     phy_write               mdio[USERACCESS0] = 0x809f0000, write = 0, reg = 4, phy = 31
AR7     phy_read                mdio[USERACCESS0] = 0x000001e1, reg = 0, phy = 0
*/
