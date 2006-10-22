/*
 * QEMU avalanche support
 * Copyright (c) 2006 Stefan Weil
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
 * - uart0 wrong type (is 16450, should be 16550)
 * - uart1 missing
 * - vlynq0 emulation missing
 * - much more
 *
 * Interrupts:
 *                 CPU0
 *        2:         64            MIPS  AR7 on hw0
 *        7:       1686            MIPS  timer
 *       15:         64             AR7  serial
 *       16:          0             AR7  serial
 *       27:          0             AR7  Cpmac Driver
 *
 *      ERR:          0
 * 
 */

#include <assert.h>
#include <stddef.h>     /* offsetof */

#include "vl.h"
#include "disas.h"      /* lookup_symbol */
#include "exec-all.h"   /* logfile */
#include "hw/ar7.h"     /* ar7_init */

static int bigendian;

#if 0
struct IoState {
    target_ulong base;
    int it_shift;
};
#endif

/* Set flags to >0 to enable debug output. */
#define CLOCK   1
#define CPMAC   1
#define GPIO    1
#define INTC    0
#define MDIO    0       /* polled, so very noisy */
#define RESET   1
#define UART0   0
#define UART1   1
#define WDOG    1
#define OTHER   0

#define DEBUG_AR7

#define TRACE(flag, command) ((flag) ? (command) : (void)0)

#ifdef DEBUG_AR7
#define logout(fmt, args...) fprintf(stderr, "AR7\t%-24s" fmt, __func__, ##args)
#else
#define logout(fmt, args...) ((void)0)
#endif

#define MISSING() logout("%s:%u missing, %s!!!\n", __FILE__, __LINE__, backtrace())
#define UNEXPECTED() logout("%s:%u unexpected, %s!!!\n", __FILE__, __LINE__, backtrace())

#if 0
#define AVALANCHE_ADSL_SUB_SYS_MEM_BASE       (KSEG1ADDR(0x01000000)) /* AVALANCHE ADSL Mem Base */
#define BBIF_SPACE1                           (KSEG1ADDR(0x01800000))
#define AVALANCHE_BROADBAND_INTERFACE__BASE   (KSEG1ADDR(0x02000000)) /* AVALANCHE BBIF */
#define AVALANCHE_ATM_SAR_BASE                (KSEG1ADDR(0x03000000)) /* AVALANCHE ATM SAR */
#define AVALANCHE_USB_SLAVE_BASE              (KSEG1ADDR(0x03400000)) /* AVALANCHE USB SLAVE */
#define AVALANCHE_LOW_VLYNQ_MEM_MAP_BASE      (KSEG1ADDR(0x04000000)) /* AVALANCHE VLYNQ 0 Mem map */

#define AVALANCHE_HIGH_VLYNQ_MEM_MAP_BASE     (KSEG1ADDR(0x0C000000)) /* AVALANCHE VLYNQ 1 Mem map */
#define PHY_BASE                              (KSEG1ADDR(0x1E000000))
#endif

#define OHIO_ADSLSS_BASE0       KERNEL_ADDR(0x01000000)
#define OHIO_ADSLSS_BASE1       KERNEL_ADDR(0x01800000)
#define OHIO_ADSLSS_BASE2       KERNEL_ADDR(0x01C00000)
#define OHIO_ATMSAR_BASE        KERNEL_ADDR(0x03000000)
#define OHIO_USB_BASE           KERNEL_ADDR(0x03400000)
#define OHIO_VLYNQ0_BASE        KERNEL_ADDR(0x04000000)

#define AVALANCHE_CPMAC0_BASE           0x08610000
#define AVALANCHE_EMIF_BASE             0x08610800
#define AVALANCHE_GPIO_BASE             0x08610900
#define AVALANCHE_CLOCK_BASE            0x08610a00 /* Clock Control */
#define AVALANCHE_WATCHDOG_BASE         0x08610b00 /* Watchdog */
#define AVALANCHE_TIMER0_BASE           0x08610c00 /* Timer 1 */
#define AVALANCHE_TIMER1_BASE           0x08610d00 /* Timer 2 */
#define AVALANCHE_UART0_BASE            0x08610e00 /* UART 0 */
#define AVALANCHE_UART1_BASE            0x08610f00 /* UART 1 */
#define OHIO_I2C_BASE                   0x08610f00
#define AVALANCHE_I2C_BASE              0x08611000 /* I2C */
#define DEV_ID_BASE                     0x08611100
#define AVALANCHE_USB_SLAVE_BASE        0x08611200 /* USB DMA */
#define PCI_CONFIG_BASE                 0x08611300
#define AVALANCHE_MCDMA_BASE            0x08611400 /* MC DMA channels 0-3 */
#define TNETD73xx_VDMAVT_BASE           0x08611500 /* VDMAVT Control */
#define AVALANCHE_RESET_BASE            0x08611600
#define AVALANCHE_BIST_CONTROL_BASE     0x08611700 /* BIST Control */
#define AVALANCHE_VLYNQ0_BASE           0x08611800 /* VLYNQ0 */
#define AVALANCHE_DCL_BASE              0x08611a00 /* Device Config Latch */
#define OHIO_MII_SEL_REG                0x08611a08
#define DSL_IF_BASE                     0x08611b00
#define AVALANCHE_VLYNQ1_BASE           0x08611c00 /* VLYNQ1 */
#define AVALANCHE_MDIO_BASE             0x08611e00
#define OHIO_WDT_BASE                   0x08611f00
#define AVALANCHE_FSER_BASE             0x08612000 /* FSER base */
#define AVALANCHE_INTC_BASE             0x08612400
#define AVALANCHE_CPMAC1_BASE           0x08612800
#define AVALANCHE_END                   0x08613000

typedef struct {
    struct BUFF_DESC  *next;
    char              *buff;
    uint32_t           buff_params;
    uint32_t           ctrl_n_len;
} cpmac_buff_t;

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
    uint8_t phys[6]; /* mac address */
    //~ uint8_t curpag;
    //~ uint8_t mult[8]; /* multicast mask array */
    //~ int irq;
    VLANClientState *vc;
    //~ uint8_t macaddr[6];
    //~ uint8_t mem[1];
} NICState;

typedef struct {
    CPUState *cpu_env;
    NICState nic[2];
    uint32_t intmask[2];

    uint32_t cpmac0[0x200];             // 0x08610000
    uint32_t emif[0x40];                // 0x08610800
    uint32_t gpio[8];                   // 0x08610900
            // data in, data out, dir, enable, -, cvr, didr1, didr2
    uint32_t gpio_dummy[0x38];
    uint32_t clock_control[0x40];       // 0x08610a00
                                        // 0x08610a80 struct _ohio_clock_pll
    uint32_t clock_dummy[0x18];
    uint32_t watchdog[0x20];            // 0x08610b00 struct _ohio_clock_pll
    uint32_t timer0[2];                 // 0x08610c00
    uint32_t timer1[2];                 // 0x08610d00
    uint32_t uart0[8];                  // 0x08610e00
    uint32_t uart1[8];                  // 0x08610f00
    uint32_t usb[20];                   // 0x08611200
    uint32_t mc_dma[0x10][4];           // 0x08611400
    uint32_t reset_control[3];          // 0x08611600
    uint32_t reset_dummy[0x80 - 3];
    uint32_t vlynq0[0x40];              // 0x08611800
            // + 0xe0 interrupt enable bits
    uint32_t device_config_latch[5];    // 0x08611a00
    uint32_t vlynq1[0x40];              // 0x08611c00
    uint32_t mdio[0x22];                // 0x08611e00
    uint32_t intc[0xc0];                // 0x08612400
    //~ uint32_t exception_control[7];  //   +0x80
    //~ uint32_t pacing[3];             //   +0xa0
    //~ uint32_t channel_control[40];   //   +0x200
    uint32_t cpmac1[0x200];             // 0x08612800
    //~ uint32_t unknown[0x40]              // 0x08613000
} avalanche_t;

#define UART_MEM_TO_IO(addr)    (((addr) - AVALANCHE_UART0_BASE) / 4)

static avalanche_t av = {
    cpmac0: { 0 },
    emif: { 0 },
    gpio: { 0x800, 0, 0, 0 },
    clock_control: { 0 },
    timer0: { 0 },
    timer1: { 0 },
    uart0: { 0, 0, 0, 0, 0, 0x20, 0 },
    //~ reset_control: { 0x04720043 },
    //~ device_config_latch: 0x025d4297
    // 21-20 phy clk source
    device_config_latch: { 0x025d4291 },
    mdio: { 0x00070101, 0, 0xffffffff }
};

/* Global variable avalanche can be used in debugger. */
avalanche_t *avalanche = &av;

static const char *backtrace(void)
{
    static char buffer[256];
    char *p = buffer;
    p += sprintf(p, "[%s]", lookup_symbol(av.cpu_env->PC));
    p += sprintf(p, "[%s]", lookup_symbol(av.cpu_env->gpr[31]));
    assert((p - buffer) < sizeof(buffer));
    return buffer;
}

static void ar7_irq(void *opaque, int irq_num, int level)
{
    CPUState *cpu_env = first_cpu;
    if (cpu_env != av.cpu_env) {
        logout("(%p,%d,%d)\n", opaque, irq_num, level);
    }

    switch (irq_num) {
        case 15:        /* serial0 */
        case 16:        /* serial1 */
        case 27:        /* cpmac0 */
        case 41:        /* cpmac1 */
            if (level) {
                unsigned channel = irq_num - 8;
                if (channel < 32) {
                    if (av.intmask[0] & (1 << channel)) {
                        //~ logout("(%p,%d,%d)\n", opaque, irq_num, level);
                        av.intc[0x10] = (((irq_num - 8) << 16) | channel);
                        /* use hardware interrupt 0 */
                        cpu_env->CP0_Cause |= 0x00000400;
                        cpu_interrupt(cpu_env, CPU_INTERRUPT_HARD);
                    } else {
                        //~ logout("(%p,%d,%d) is disabled\n", opaque, irq_num, level);
                    }
                }
                // int line number
                //~ av.intc[0x10] |= (4 << 16);
                // int channel number
                // 2, 7, 15, 27, 80
                //~ av.intmask[0]
            } else {
                av.intc[0x10] = 0;
                cpu_env->CP0_Cause &= ~0x00000400;
                cpu_reset_interrupt(cpu_env, CPU_INTERRUPT_HARD);
            }
            break;
        default:
            logout("(%p,%d,%d)\n", opaque, irq_num, level);
    }
}

/*****************************************************************************
 *
 * CPMAC emulation.
 *
 ****************************************************************************/

#if 0
08611600  43 00 72 04 00 00 00 00  00 00 00 00 00 00 00 00  |C.r.............|
08611610  43 00 72 04 00 00 00 00  00 00 00 00 00 00 00 00  |C.r.............|
08611a00  91 42 5d 02 00 00 00 00  00 00 00 00 00 00 00 00  |.B].............|
08611a10  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
08611b00  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|



cpmac_reg.h:
#define MACSTATUS(base)                ((MEM_PTR)(base+0x164))
#define MACSTATUS(base)                (*pCPMAC_MACSTATUS(base))
#define EMCONTROL(base)                ((MEM_PTR)(base+0x168))
#define EMCONTROL(base)                (*pCPMAC_EMCONTROL(base))
#define TX_INTSTAT_RAW(base)           ((MEM_PTR)(base+0x170))
#define TX_INTSTAT_RAW(base)           (*pCPMAC_TX_INTSTAT_RAW(base))
#define TX_INTSTAT_MASKED(base)        ((MEM_PTR)(base+0x174))
#define TX_INTSTAT_MASKED(base)        (*pCPMAC_TX_INTSTAT_MASKED(base))

#define RX_INTSTAT_RAW(base)           ((MEM_PTR)(base+0x190))
#define RX_INTSTAT_RAW(base)           (*pCPMAC_RX_INTSTAT_RAW(base))
#define RX_INTSTAT_MASKED(base)        ((MEM_PTR)(base+0x194))
#define RX_INTSTAT_MASKED(base)        (*pCPMAC_RX_INTSTAT_MASKED(base))

#define MAC_INTSTAT_RAW(base)          ((MEM_PTR)(base+0x1A0))
#define MAC_INTSTAT_RAW(base)          (*pCPMAC_MAC_INTSTAT_RAW(base))
#define MAC_INTSTAT_MASKED(base)       ((MEM_PTR)(base+0x1A4))
#define MAC_INTSTAT_MASKED(base)       (*pCPMAC_MAC_INTSTAT_MASKED(base))

#define MAC_INTMASK_CLEAR(base)        ((MEM_PTR)(base+0x1AC))
#define MAC_INTMASK_CLEAR(base)        (*pCPMAC_MAC_INTMASK_CLEAR(base))

#define BOFFTEST(base)                 ((MEM_PTR)(base+0x1E0))
#define BOFFTEST(base)                 (*pCPMAC_BOFFTEST(base))
#define PACTEST(base)                  ((MEM_PTR)(base+0x1E4))
#define PACTEST(base)                  (*pCPMAC_PACTEST(base))
#define RXPAUSE(base)                  ((MEM_PTR)(base+0x1E8))
#define RXPAUSE(base)                  (*pCPMAC_RXPAUSE(base))
#define TXPAUSE(base)                  ((MEM_PTR)(base+0x1EC))
#define TXPAUSE(base)                  (*pCPMAC_TXPAUSE(base))

#define CPMAC_TX_INT_ACK(base,ch)             (*(MEM_PTR)(base+0x640+(4*ch)))
#define pCPMAC_TX0_INT_ACK(base)              ((MEM_PTR)(base+0x640))
...
#define pCPMAC_TX7_INT_ACK(base)              ((MEM_PTR)(base+0x65C))

#define CPMAC_RX_INT_ACK(base,ch)             (*(MEM_PTR)(base+0x660+(4*ch)))
#define pCPMAC_RX0_INT_ACK(base)              ((MEM_PTR)(base+0x660))
...
#define pCPMAC_RX7_INT_ACK(base)              ((MEM_PTR)(base+0x67C))

#endif

#define MAC_IN_VECTOR_STATUS_INT   (1 << 19)
#define MAC_IN_VECTOR_HOST_INT     (1 << 18)
#define MAC_IN_VECTOR_RX_INT_OR    (1 << 17)
#define MAC_IN_VECTOR_TX_INT_OR    (1 << 16)
#define MAC_IN_VECTOR_RX_INT_VEC   (7 << 8)
#define MAC_IN_VECTOR_TX_INT_VEC   (7)

/* STATISTICS */
static const char *cpmac_statistics[] = {
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
        case 0x00: text = "TX_IDVER"; break;
        case 0x01: text = "TX_CONTROL"; break;
        case 0x02: text = "TX_TEARDOWN"; break;
        case 0x04: text = "RX_IDVER"; break;
        case 0x05: text = "RX_CONTROL"; break;
        case 0x06: text = "RX_TEARDOWN"; break;
        case 0x40: text = "RX_MBP_ENABLE"; break;
        case 0x41: text = "RX_UNICAST_SET"; break;
        case 0x42: text = "RX_UNICAST_CLEAR"; break;
        case 0x43: text = "RX_MAXLEN"; break;
        case 0x44: text = "RX_BUFFER_OFFSET"; break;
        case 0x45: text = "RX_FILTERLOWTHRESH"; break;
        case 0x58: text = "MACCONTROL"; break;
        case 0x5e: text = "TX_INTMASK_SET"; break;
        case 0x5f: text = "TX_INTMASK_CLEAR"; break;
        case 0x60: text = "MAC_IN_VECTOR"; break;
        case 0x61: text = "MAC_EOI_VECTOR"; break;
        case 0x66: text = "RX_INTMASK_SET"; break;
        case 0x67: text = "RX_INTMASK_CLEAR"; break;
        case 0x6a: text = "MAC_INTMASK_SET"; break;
        case 0x74: text = "MACADDRMID"; break;
        case 0x75: text = "MACADDRHI"; break;
        case 0x76: text = "MACHASH1"; break;
        case 0x77: text = "MACHASH2"; break;
    }
    if (text != 0) {
    } else if (index >= 0x48 && index < 0x50) {
        text = buffer;
        sprintf(buffer, "RX%u_FLOWTHRESH", (unsigned)(index & 7));
    } else if (index >= 0x50 && index < 0x58) {
        text = buffer;
        sprintf(buffer, "RX%u_FREEBUFFER", (unsigned)(index & 7));
    } else if (index >= 0x6c && index < 0x74) {
        text = buffer;
        sprintf(buffer, "MACADDRLO_%u", (unsigned)(index - 0x6c));
    } else if (index >= 0x80 && index < 0xa4) {
        text = buffer;
        sprintf(buffer, "STAT_%s", cpmac_statistics[index - 0x80]);
    } else if (index >= 0x180 && index < 0x188) {
        text = buffer;
        sprintf(buffer, "CPMAC_TX%u_HDP", (unsigned)(index & 7));
    } else if (index >= 0x188 && index < 0x190) {
        text = buffer;
        sprintf(buffer, "CPMAC_RX%u_HDP", (unsigned)(index & 7));
    } else {
        text = buffer;
        sprintf(buffer, "0x%x", index);
    }
    assert(strlen(buffer) < sizeof(buffer));
    return text;
}

static const int cpmac_interrupt[] = { 27, 41 };

static uint32_t ar7_cpmac_read(uint32_t cpmac[], unsigned index)
{
    uint32_t val = cpmac[index];
    const char *text = i2cpmac(index);
    //~ do_raise_exception(EXCP_DEBUG)
    TRACE(CPMAC, logout("cpmac%u[%s] (0x%08lx) = 0x%08lx\n",
      cpmac == av.cpmac1, text,
            (unsigned long)(AVALANCHE_CPMAC0_BASE + 4 * index),
            (unsigned long)val));
    return val;
}

static void ar7_cpmac_write(uint32_t cpmac[], unsigned index, unsigned offset, uint32_t val)
{
    const char *text = i2cpmac(index);
    cpmac[offset] = val;
    TRACE(CPMAC, logout("cpmac%u[%s] (0x%08lx) = 0x%08lx\n",
      cpmac == av.cpmac1, text,
            (unsigned long)(AVALANCHE_CPMAC0_BASE + 4 * index),
            (unsigned long)val));
    if (offset == 0x40) {
        /* 13 ... 8 = 0x20 enable broadcast */
    } else if (offset == 0x43) {
        TRACE(CPMAC, logout("setting max packet length %u\n", (unsigned)val));
    } else if (offset == 0x5e) {
        cpmac[0x60] |= MAC_IN_VECTOR_TX_INT_OR;
        ar7_irq(0, cpmac_interrupt[index], 1);
    } else if (offset == 0x75) {
        /* set MAC address (4 high bytes) */
        uint8_t *phys = av.nic[index].phys;
        phys[5] = cpmac[0x6c];
        phys[4] = cpmac[0x74];
        phys[3] = (cpmac[0x75] >> 24);
        phys[2] = (cpmac[0x75] >> 16);
        phys[1] = (cpmac[0x75] >> 8);
        phys[0] = (cpmac[0x75] >> 0);
        TRACE(CPMAC, logout("setting MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
                phys[0], phys[1], phys[2], phys[3], phys[4], phys[5]));
    } else if (offset >= 0x188 && offset < 0x190) {
        while (val != 0) {
            cpmac_buff_t bd;
            cpu_physical_memory_read(val, (uint8_t *)&bd, sizeof(bd));
            TRACE(CPMAC, logout("buffer 0x%08x, next 0x%08x, buff 0x%08x, params 0x%08x, len 0x%08x\n",
                    val, (unsigned)bd.next, (unsigned)bd.buff,
                    (unsigned)bd.buff_params, (unsigned)bd.ctrl_n_len));
            val = (uint32_t)bd.next;
        }
    }
}

/* Interrupt controller emulation */

typedef struct { /* Avalanche Interrupt control registers */
  uint32_t intsr1;    /* Interrupt Status/Set Register 1   0x00 */
  uint32_t intsr2;    /* Interrupt Status/Set Register 2   0x04 */
  uint32_t unused1;                                     /* 0x08 */
  uint32_t unused2;                                     /* 0x0C */
  uint32_t intcr1;    /* Interrupt Clear Register 1        0x10 */
  uint32_t intcr2;    /* Interrupt Clear Register 2        0x14 */
  uint32_t unused3;                                     /* 0x18 */
  uint32_t unused4;                                     /* 0x1C */
  uint32_t intesr1;   /* Interrupt Enable (Set) Register 1 0x20 */
  uint32_t intesr2;   /* Interrupt Enable (Set) Register 2 0x24 */
  uint32_t unused5;                                     /* 0x28 */
  uint32_t unused6;                                     /* 0x2C */
  uint32_t intecr1;   /* Interrupt Enable Clear Register 1 0x30 */
  uint32_t intecr2;   /* Interrupt Enable Clear Register 2 0x34 */
  uint32_t unused7;                                     /* 0x38 */
  uint32_t unused8;                                     /* 0x3c */
  uint32_t pintir;    /* Priority Interrupt Index Register 0x40 */
  uint32_t intmsr;    /* Priority Interrupt Mask Index Reg 0x44 */
  uint32_t unused9;                                     /* 0x48 */
  uint32_t unused10;                                    /* 0x4C */
  uint32_t intpolr1;  /* Interrupt Polarity Mask Reg 1     0x50 */
  uint32_t intpolr2;  /* Interrupt Polarity Mask Reg 2     0x54 */
  uint32_t unused11;                                    /* 0x58 */
  uint32_t unused12;                                    /* 0x5C */
  uint32_t inttypr1;  /* Interrupt Type Mask Register 1    0x60 */
  uint32_t inttypr2;  /* Interrupt Type Mask Register 2    0x64 */

  /* Avalanche Exception control registers */
  uint32_t exsr;      /* Exceptions Status/Set register    0x80 */
  uint32_t reserved;                                     /*0x84 */
  uint32_t excr;      /* Exceptions Clear Register         0x88 */
  uint32_t reserved1;                                    /*0x8c */
  uint32_t exiesr;    /* Exceptions Interrupt Enable (set) 0x90 */
  uint32_t reserved2;                                    /*0x94 */
  uint32_t exiecr;    /* Exceptions Interrupt Enable(clear)0x98 */
  uint32_t dummy0x9c;

  /* Interrupt Pacing */
  uint32_t ipacep;    /* Interrupt pacing register         0xa0 */
  uint32_t ipacemap;  /* Interrupt Pacing Map Register     0xa4 */
  uint32_t ipacemax;  /* Interrupt Pacing Max Register     0xa8 */
  uint32_t dummy0xac[3 * 4];
  uint32_t dummy0x100[64];

  /* Interrupt Channel Control */
  uint32_t cintnr[40];/* Channel Interrupt Number Reg     0x200 */
} ar7_intc_t;

static uint32_t ar7_intc_read(uint32_t intc[], unsigned index)
{
    uint32_t val = intc[index];
    if (index == 16) {
    } else if (index == 8 || index == 9) {
            TRACE(INTC, logout("intc[0x%02x] = %08x\n", index, val));
    } else if (index >= 128 && index < 168) {
            TRACE(INTC, logout("intc[cintnr%u] = 0x%08x\n", index - 128, val));
    } else {
            TRACE(INTC, logout("intc[0x%02x] = %08x\n", index, val));
    }
    return val;
}

static void ar7_intc_write(uint32_t intc[], unsigned index, uint32_t val)
{
    unsigned subindex = (index & 1);
    intc[index] = val;
    if (index == 4) {
    } else if (index == 8 || index == 9) {
            av.intmask[subindex] |= val;
            TRACE(INTC, logout("intc[intesr%u] val 0x%08x, mask 0x%08x\n",
                    subindex + 1, val, av.intmask[subindex]));
    } else if (index == 12 || index == 13) {
            unsigned subindex = (index & 1);
            av.intmask[subindex] &= ~val;
            TRACE(INTC, logout("intc[intecr%u] val 0x%08x, mask 0x%08x\n",
                    subindex + 1, val, av.intmask[subindex]));
    } else if (index >= 128 && index < 168) {
            TRACE(INTC, logout("intc[cintnr%u] val 0x%08x\n", index - 128, val));
    } else {
            TRACE(INTC, logout("intc[0x%02x] val 0x%08x\n", index, val));
    }
}

/* MDIO emulation */

typedef struct {
    uint32_t ver;                   /* 0x00 */
#define         MDIO_VER_MODID         (0xFFFF << 16)
#define         MDIO_VER_REVMAJ        (0xFF   << 8)
#define         MDIO_VER_REVMIN        (0xFF)
    uint32_t control;               /* 0x04 */
#define         MDIO_CONTROL_IDLE                 (1 << 31)
#define         MDIO_CONTROL_ENABLE               (1 << 30)
#define         MDIO_CONTROL_PREAMBLE             (1 << 20)  
#define         MDIO_CONTROL_FAULT                (1 << 19)
#define         MDIO_CONTROL_FAULT_DETECT_ENABLE  (1 << 18)
#define         MDIO_CONTROL_INT_TEST_ENABLE      (1 << 17)
#define         MDIO_CONTROL_HIGHEST_USER_CHANNEL (0x1F << 8)
#define         MDIO_CONTROL_CLKDIV               (0xFF)
    uint32_t alive;                 /* 0x08 */
    uint32_t link;                  /* 0x0c */
    uint32_t linkintraw;            /* 0x10 */
    uint32_t linkintmasked;         /* 0x14 */
    uint32_t dummy18[2];
    uint32_t userintraw;            /* 0x20 */
    uint32_t userintmasked;         /* 0x24 */
    uint32_t userintmaskedset;      /* 0x28 */
    uint32_t userintmaskedclr;      /* 0x2c */
    uint32_t dummy30[20];
    uint32_t useraccess0;           /* 0x80 */
#define         MDIO_USERACCESS_GO     (1 << 31)
#define         MDIO_USERACCESS_WRITE  (1 << 30)
#define         MDIO_USERACCESS_READ   (0 << 30)
#define         MDIO_USERACCESS_ACK    (1 << 29)
#define         MDIO_USERACCESS_REGADR (0x1F << 21)
#define         MDIO_USERACCESS_PHYADR (0x1F << 16)
#define         MDIO_USERACCESS_DATA   (0xFFFF)
    uint32_t userphysel0;           /* 0x84 */
#define         MDIO_USERPHYSEL_LINKSEL         (1 << 7)
#define         MDIO_USERPHYSEL_LINKINT_ENABLE  (1 << 6)
#define         MDIO_USERPHYSEL_PHYADR_MON      (0x1F)
} mdio_t;

#define pMDIO_USERACCESS(base, channel) ((volatile bit32u *)(base+(0x80+(channel*8))))
#define pMDIO_USERPHYSEL(base, channel) ((volatile bit32u *)(base+(0x84+(channel*8))))

typedef struct {
    uint32_t phy_control;
#define PHY_CONTROL_REG       0
  #define PHY_RESET           (1<<15)
  #define PHY_LOOP            (1<<14)
  #define PHY_100             (1<<13)
  #define AUTO_NEGOTIATE_EN   (1<<12)
  #define PHY_PDOWN           (1<<11)
  #define PHY_ISOLATE         (1<<10)
  #define RENEGOTIATE         (1<<9)
  #define PHY_FD              (1<<8)
        uint32_t phy_status;
#define PHY_STATUS_REG        1
  #define NWAY_COMPLETE       (1<<5)
  #define NWAY_CAPABLE        (1<<3)
  #define PHY_LINKED          (1<<2)
        uint32_t dummy2;
        uint32_t dummy3;
        uint32_t nway_advertize;
        uint32_t nway_remadvertize;
#define NWAY_ADVERTIZE_REG    4
#define NWAY_REMADVERTISE_REG 5
  #define NWAY_FD100          (1<<8)
  #define NWAY_HD100          (1<<7)
  #define NWAY_FD10           (1<<6)
  #define NWAY_HD10           (1<<5)
  #define NWAY_SEL            (1<<0)
  #define NWAY_AUTO           (1<<0)
} mdio_user_t;

#if 0
  bit32u  control;

  control =  MDIO_USERACCESS_GO |
             (method) |
             (((regadr) << 21) & MDIO_USERACCESS_REGADR) |
             (((phyadr) << 16) & MDIO_USERACCESS_PHYADR) |
             ((data) & MDIO_USERACCESS_DATA);

  myMDIO_USERACCESS = control;

static bit32u _mdioUserAccessRead(PHY_DEVICE *PhyDev, bit32u regadr, bit32u phyadr)
  {

  _mdioWaitForAccessComplete(PhyDev);  /* Wait until UserAccess ready */
  _mdioUserAccess(PhyDev, MDIO_USERACCESS_READ, regadr, phyadr, 0);
  _mdioWaitForAccessComplete(PhyDev);  /* Wait for Read to complete */

  return(myMDIO_USERACCESS & MDIO_USERACCESS_DATA);
  }

#endif

static uint32_t mdio_regaddr;
static uint32_t mdio_phyaddr;
static uint32_t mdio_data;

static uint16_t mdio_useraccess_data[1][6] = {
    {
        AUTO_NEGOTIATE_EN,
        0x7801 + NWAY_CAPABLE, // + NWAY_COMPLETE + PHY_LINKED,
        0x00000000,
        0x00000000,
        NWAY_FD100 + NWAY_HD100 + NWAY_FD10 + NWAY_HD10 + NWAY_AUTO,
        NWAY_AUTO
    }
};

static uint32_t ar7_mdio_read(uint32_t mdio[], unsigned index)
{
    uint32_t val = av.mdio[index];
    if (index == 0) {
        /* MDIO_VER */
        TRACE(MDIO, logout("mdio[MDIO_VER] = 0x%08lx\n", (unsigned long)val));
//~ cpMacMdioInit(): MDIO_CONTROL = 0x40000138
//~ cpMacMdioInit(): MDIO_CONTROL < 0x40000037
    } else if (index == 1) {
        /* MDIO_CONTROL */
        TRACE(MDIO, logout("mdio[MDIO_CONTROL] = 0x%08lx\n", (unsigned long)val));
    } else if (index == 0x20) {
        //~ mdio_regaddr = (val & MDIO_USERACCESS_REGADR) >> 21;
        //~ mdio_phyaddr = (val & MDIO_USERACCESS_PHYADR) >> 16;
        mdio_data = (val & MDIO_USERACCESS_DATA);
        TRACE(MDIO,
            logout("mdio[0x%02x] = 0x%08lx, reg = %u, phy = %u, data = 0x%04x\n",
                    index, (unsigned long)val,
                    mdio_regaddr, mdio_phyaddr, mdio_data));
    } else {
        TRACE(MDIO, logout("mdio[0x%02x] = 0x%08lx\n", index, (unsigned long)val));
    }
    return val;
}

static void ar7_mdio_write(uint32_t mdio[], unsigned index, unsigned val)
{
    if (index == 0) {
        /* MDIO_VER */
        TRACE(MDIO, logout("unexpected: mdio[0x%02x] = 0x%08lx\n",
                index, (unsigned long)val));
    } else if (index == 1) {
        /* MDIO_CONTROL */
        TRACE(MDIO, logout("mdio[MDIO_CONTROL] = 0x%08lx\n",
                (unsigned long)val));
    } else if (index == 0x20 && (val & MDIO_USERACCESS_GO)) {
        uint32_t write = (val & MDIO_USERACCESS_WRITE) >> 30;
        mdio_regaddr = (val & MDIO_USERACCESS_REGADR) >> 21;
        mdio_phyaddr = (val & MDIO_USERACCESS_PHYADR) >> 16;
        mdio_data = (val & MDIO_USERACCESS_DATA);
        TRACE(MDIO, logout("mdio[0x%02x] = 0x%08lx, write = %u, reg = %u, phy = %u, data = 0x%04x\n",
                index, (unsigned long)val, write,
                mdio_regaddr, mdio_phyaddr, mdio_data));
        val &= MDIO_USERACCESS_DATA;
        if (mdio_phyaddr == 31 && mdio_regaddr < 6) {
            mdio_phyaddr = 0;
            if (write) {
                //~ if ((mdio_regaddr == PHY_CONTROL_REG) && (val & PHY_RESET)) {
                    //~ 1000 7809 0000 0000 01e1 0001
                    //~ mdio_useraccess_data[0][PHY_CONTROL_REG] = 0x1000;
                    //~ mdio_useraccess_data[0][PHY_STATUS_REG] = 0x782d;
                    //~ mdio_useraccess_data[0][NWAY_ADVERTIZE_REG] = 0x01e1;
                    /* 100FD=Yes, 100HD=Yes, 10FD=Yes, 10HD=Yes */
                    //~ mdio_useraccess_data[0][NWAY_REMADVERTISE_REG] = 0x85e1;
                //~ }
                mdio_useraccess_data[mdio_phyaddr][mdio_regaddr] = val;
            } else {
                val = mdio_useraccess_data[mdio_phyaddr][mdio_regaddr];
                if ((mdio_regaddr == PHY_CONTROL_REG) && (val & PHY_RESET)) {
                    mdio_useraccess_data[mdio_phyaddr][mdio_regaddr] =
                            ((val & ~PHY_RESET) | AUTO_NEGOTIATE_EN);
                } else if ((mdio_regaddr == PHY_CONTROL_REG) && (val & RENEGOTIATE)) {
                    val &= ~RENEGOTIATE;
                    mdio_useraccess_data[mdio_phyaddr][mdio_regaddr] = val;
                    //~ 0x0000782d 0x00007809
                    mdio_useraccess_data[mdio_phyaddr][1] = 0x782d;
                    mdio_useraccess_data[mdio_phyaddr][5] =
                            mdio_useraccess_data[mdio_phyaddr][4] | PHY_ISOLATE | PHY_RESET;
                    mdio[3] = 0x80000000;
                }
            }
        }
    } else {
        TRACE(MDIO, logout("mdio[0x%02x] = 0x%08lx\n", index, (unsigned long)val));
    }
    av.mdio[index] = val;
}

static void ar7_reset_write(unsigned offset, uint32_t val)
{
    unsigned index = offset / 4;
    if (index == 0) {
#if RESET
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
                TRACE(RESET, logout("reset %s %s\n", (enabled & (1 << i)) ? "enabled" : "disabled", resetdevice[i]));
            }
        }
#endif
    } else if (index == 1) {
        TRACE(RESET, logout("reset\n"));
        qemu_system_reset_request();
        //~ CPUState *cpu_env = first_cpu;
        //~ cpu_env->PC = 0xbfc00000;
    } else {
        TRACE(RESET, logout("reset[%u]=0x%08x\n", index, val));
    }
}

/*****************************************************************************
 *
 * Watchdog timer emulation.
 *
 * This watchdog timer module has prescalar and counter which divide the input
 *  reference frequency and upon expiration, the system is reset.
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

static uint16_t wd_val(uint16_t val, uint16_t bits)
{
    return ((val & ~0x3) | bits);
}

static void ar7_wdt_write(unsigned offset, uint32_t val)
{
    wdtimer_t *wdt = (wdtimer_t *)&av.watchdog;
    if (offset == offsetof(wdtimer_t, kick_lock)) {
        if (val == KICK_LOCK_1ST_STAGE) {
            TRACE(WDOG, logout("kick lock 1st stage\n"));
            wdt->kick_lock = wd_val(val, 1);
        } else if (val == KICK_LOCK_2ND_STAGE) {
            TRACE(WDOG, logout("kick lock 2nd stage\n"));
            wdt->kick_lock = wd_val(val, 3);
        } else {
            TRACE(WDOG, logout("kick lock unexpected value 0x%08x, %s\n", val, backtrace()));
        }
    } else if (offset == offsetof(wdtimer_t, kick)) {
        if (wdt->kick_lock != wd_val(KICK_LOCK_2ND_STAGE, 3)) {
            TRACE(WDOG, logout("kick still locked!\n"));
            UNEXPECTED();
        } else if (val == KICK_VALUE) {
            TRACE(WDOG, logout("kick (restart) watchdog\n"));
        }
        MISSING();
    } else if (offset == offsetof(wdtimer_t, change_lock)) {
        if (val == CHANGE_LOCK_1ST_STAGE) {
            TRACE(WDOG, logout("change lock 1st stage\n"));
            wdt->change_lock = wd_val(val, 1);
        } else if (val == CHANGE_LOCK_2ND_STAGE) {
            TRACE(WDOG, logout("change lock 2nd stage\n"));
            wdt->change_lock = wd_val(val, 3);
        } else {
            TRACE(WDOG, logout("change lock unexpected value 0x%08x, %s\n", val, backtrace()));
        }
    } else if (offset == offsetof(wdtimer_t, change)) {
        if (wdt->change_lock != wd_val(CHANGE_LOCK_2ND_STAGE, 3)) {
            TRACE(WDOG, logout("change still locked!\n"));
            UNEXPECTED();
        } else {
            TRACE(WDOG, logout("change watchdog, val=0x%08x\n", val)); // val = 0xdf5c
        }
        MISSING();
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
            TRACE(WDOG, logout("disable lock unexpected value 0x%08x, %s\n", val, backtrace()));
        }
    } else if (offset == offsetof(wdtimer_t, disable)) {
        if (wdt->disable_lock != wd_val(DISABLE_LOCK_3RD_STAGE, 3)) {
            TRACE(WDOG, logout("disable still locked, val=0x%08x!\n", val));
            UNEXPECTED();
        } else {
            TRACE(WDOG, logout("disable watchdog, val=0x%08x\n", val)); // val = 0
        }
        MISSING();
    } else if (offset == offsetof(wdtimer_t, prescale_lock)) {
        if (val == PRESCALE_LOCK_1ST_STAGE) {
            TRACE(WDOG, logout("prescale lock 1st stage\n"));
            wdt->prescale_lock = wd_val(val, 1);
        } else if (val == PRESCALE_LOCK_2ND_STAGE) {
            TRACE(WDOG, logout("prescale lock 2nd stage\n"));
            wdt->prescale_lock = wd_val(val, 3);
        } else {
            TRACE(WDOG, logout("prescale lock unexpected value 0x%08x, %s\n", val, backtrace()));
        }
    } else if (offset == offsetof(wdtimer_t, prescale)) {
        if (wdt->prescale_lock != wd_val(PRESCALE_LOCK_2ND_STAGE, 3)) {
            TRACE(WDOG, logout("prescale still locked, val=0x%08x!\n", val));
            UNEXPECTED();
        } else {
            TRACE(WDOG, logout("set watchdog prescale, val=0x%08x\n", val)); // val = 0xffff
        }
        MISSING();
    } else {
        TRACE(WDOG, logout("??? offset 0x%02x = 0x%08x, %s\n", offset, val, backtrace()));
    }
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
    addr |= 0x08610000;
    const char *name = 0;
    int logflag = OTHER;
    if (INRANGE(AVALANCHE_CPMAC0_BASE, av.cpmac0)) {
        //~ name = "cpmac0";
        logflag = 0;
        index = (addr - AVALANCHE_CPMAC0_BASE) / 4;
        val = ar7_cpmac_read(av.cpmac0, index);
    } else if (INRANGE(AVALANCHE_EMIF_BASE, av.emif)) {
        name = "emif";
        val = VALUE(AVALANCHE_EMIF_BASE, av.emif);
    } else if (INRANGE(AVALANCHE_GPIO_BASE, av.gpio)) {
        name = "gpio";
        logflag = GPIO;
        val = VALUE(AVALANCHE_GPIO_BASE, av.gpio);
    } else if (INRANGE(AVALANCHE_CLOCK_BASE, av.clock_control)) {
        name = "clock";
        logflag = CLOCK;
        index = (addr - AVALANCHE_CLOCK_BASE) / 4;
        val = av.clock_control[index];
        if (index == 0x0c || index == 0x14 || index == 0x1c || index == 0x24) {
            /* Reset PLL status bit. */
            if (val == 4) {
                val &= ~1;
            } else {
                val |= 1;
            }
        }
    } else if (INRANGE(AVALANCHE_WATCHDOG_BASE, av.watchdog)) {
        name = "watchdog";
        logflag = WDOG;
        val = VALUE(AVALANCHE_WATCHDOG_BASE, av.watchdog);
    } else if (INRANGE(AVALANCHE_TIMER0_BASE, av.timer0)) {
        name = "timer0";
        val = VALUE(AVALANCHE_TIMER0_BASE, av.timer0);
    } else if (INRANGE(AVALANCHE_UART0_BASE, av.uart0)) {
        name = "uart0";
        logflag = UART0;
        val = cpu_inb(av.cpu_env, UART_MEM_TO_IO(addr));
        //~ val = VALUE(AVALANCHE_UART0_BASE, av.uart0);
    } else if (INRANGE(AVALANCHE_UART1_BASE, av.uart1)) {
        name = "uart1";
        logflag = UART1;
        val = cpu_inb(av.cpu_env, UART_MEM_TO_IO(addr));
    } else if (INRANGE(AVALANCHE_USB_SLAVE_BASE, av.usb)) {
        name = "usb slave";
        val = VALUE(AVALANCHE_USB_SLAVE_BASE, av.usb);
    } else if (INRANGE(AVALANCHE_RESET_BASE, av.reset_control)) {
        name = "reset control";
        logflag = RESET;
        val = VALUE(AVALANCHE_RESET_BASE, av.reset_control);
    } else if (INRANGE(AVALANCHE_DCL_BASE, av.device_config_latch)) {
        name = "device config latch";
        val = VALUE(AVALANCHE_DCL_BASE, av.device_config_latch);
    } else if (INRANGE(AVALANCHE_VLYNQ0_BASE, av.vlynq0)) {
        name = "vlynq 0";
        val = VALUE(AVALANCHE_VLYNQ0_BASE, av.vlynq0);
    } else if (INRANGE(AVALANCHE_VLYNQ1_BASE, av.vlynq1)) {
        name = "vlynq 1";
        val = VALUE(AVALANCHE_VLYNQ1_BASE, av.vlynq1);
    } else if (INRANGE(AVALANCHE_MDIO_BASE, av.mdio)) {
        name = "mdio";
        logflag = MDIO;
        index = (addr - AVALANCHE_MDIO_BASE) / 4;
        val = ar7_mdio_read(av.mdio, index);
    } else if (INRANGE(AVALANCHE_INTC_BASE, av.intc)) {
        name = "intc";
        index = (addr - AVALANCHE_INTC_BASE) / 4;
        val = ar7_intc_read(av.intc, index);
    } else if (INRANGE(AVALANCHE_CPMAC1_BASE, av.cpmac1)) {
        //~ name = "cpmac1";
        logflag = 0;
        index = (addr - AVALANCHE_CPMAC1_BASE) / 4;
        val = ar7_cpmac_read(av.cpmac1, index);
    } else {
        name = "???";
        logflag = 1;
{
  TRACE(logflag, logout("addr 0x%08lx (%s) = 0x%08x, caller %s\n",
    (unsigned long)addr, name, val, backtrace()));
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
    unsigned index;
    const char *name = 0;
    int logflag = OTHER;
    addr |= 0x08610000;
    if (INRANGE(AVALANCHE_CPMAC0_BASE, av.cpmac0)) {
        //~ name = "cpmac0";
        logflag = 0;
        index = (addr - AVALANCHE_CPMAC0_BASE) / 4;
        ar7_cpmac_write(av.cpmac0, 0, index, val);
    } else if (INRANGE(AVALANCHE_EMIF_BASE, av.emif)) {
        name = "emif";
        VALUE(AVALANCHE_EMIF_BASE, av.emif) = val;
    } else if (INRANGE(AVALANCHE_GPIO_BASE, av.gpio)) {
        name = "gpio";
        logflag = GPIO;
        VALUE(AVALANCHE_GPIO_BASE, av.gpio) = val;
    } else if (INRANGE(AVALANCHE_CLOCK_BASE, av.clock_control)) {
        name = "clock control";
        logflag = CLOCK;
        index = (addr - AVALANCHE_CLOCK_BASE) / 4;
        TRACE(CLOCK, logout("addr 0x%08lx (clock) = %04x\n",
              (unsigned long)addr, val));
        if (index == 0) {
            uint32_t oldpowerstate = VALUE(AVALANCHE_CLOCK_BASE, av.clock_control) >> 30;
            uint32_t newpowerstate = val;
            if (oldpowerstate != newpowerstate) {
                TRACE(CLOCK, logout("change power state from %u to %u\n",
                      oldpowerstate, newpowerstate));
            }
        }
        VALUE(AVALANCHE_CLOCK_BASE, av.clock_control) = val;
    } else if (INRANGE(AVALANCHE_WATCHDOG_BASE, av.watchdog)) {
        //~ name = "watchdog";
        logflag = 0;
        ar7_wdt_write(addr - AVALANCHE_WATCHDOG_BASE, val);
    } else if (INRANGE(AVALANCHE_TIMER0_BASE, av.timer0)) {
        name = "timer0";
        VALUE(AVALANCHE_TIMER0_BASE, av.timer0) = val;
    } else if (INRANGE(AVALANCHE_UART0_BASE, av.uart0)) {
        name = "uart0";
        logflag = UART0;
        cpu_outb(av.cpu_env, UART_MEM_TO_IO(addr), val);
        //~ VALUE(AVALANCHE_UART0_BASE, av.uart0) = val;
    } else if (INRANGE(AVALANCHE_UART1_BASE, av.uart1)) {
        name = "uart1";
        logflag = UART1;
        cpu_outb(av.cpu_env, UART_MEM_TO_IO(addr), val);
    } else if (INRANGE(AVALANCHE_USB_SLAVE_BASE, av.usb)) {
        name = "usb slave";
        VALUE(AVALANCHE_USB_SLAVE_BASE, av.usb) = val;
    } else if (INRANGE(AVALANCHE_RESET_BASE, av.reset_control)) {
        //~ name = "reset control";
        logflag = 0;
        VALUE(AVALANCHE_RESET_BASE, av.reset_control) = val;
        ar7_reset_write(addr - AVALANCHE_RESET_BASE, val);
    } else if (INRANGE(AVALANCHE_DCL_BASE, av.device_config_latch)) {
        name = "device config latch";
        VALUE(AVALANCHE_DCL_BASE, av.device_config_latch) = val;
    } else if (INRANGE(AVALANCHE_VLYNQ0_BASE, av.vlynq0)) {
        name = "vlynq 0";
        VALUE(AVALANCHE_VLYNQ0_BASE, av.vlynq0) = val;
    } else if (INRANGE(AVALANCHE_VLYNQ1_BASE, av.vlynq1)) {
        name = "vlynq 1";
        VALUE(AVALANCHE_VLYNQ1_BASE, av.vlynq1) = val;
    } else if (INRANGE(AVALANCHE_MDIO_BASE, av.mdio)) {
        name = "mdio";
        logflag = MDIO;
        index = (addr - AVALANCHE_MDIO_BASE) / 4;
        ar7_mdio_write(av.mdio, index, val);
    } else if (INRANGE(AVALANCHE_INTC_BASE, av.intc)) {
        name = "intc";
        index = (addr - AVALANCHE_INTC_BASE) / 4;
        ar7_intc_write(av.intc, index, val);
    } else if (INRANGE(AVALANCHE_CPMAC1_BASE, av.cpmac1)) {
        //~ name = "cpmac1";
        logflag = 0;
        index = (addr - AVALANCHE_CPMAC1_BASE) / 4;
        ar7_cpmac_write(av.cpmac1, 1, index, val);
    } else {
        name = "???";
        logflag = 1;
    }
    if (name != 0) {
      TRACE(logflag, logout("addr 0x%08lx (%s) = 0x%08x\n",
          (unsigned long)addr, name, val));
    }
}

static void io_writeb (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    if (addr & 3) {
        ar7_io_memwrite(opaque, addr, value);
        logout("addr=0x%08x, val=0x%02x\n", addr, value);
        assert(0);
    } else if (INRANGE(AVALANCHE_UART0_BASE, av.uart0)) {
        ar7_io_memwrite(opaque, addr, value);
    } else if (INRANGE(AVALANCHE_UART1_BASE, av.uart1)) {
        ar7_io_memwrite(opaque, addr, value);
    } else {
        ar7_io_memwrite(opaque, addr, value);
        logout("addr=0x%08x, val=0x%02x\n", addr, value);
        assert(0);
    }
    //~ cpu_outb(NULL, addr & 0xffff, value);
}

static uint32_t io_readb (void *opaque, target_phys_addr_t addr)
{
    return ar7_io_memread(opaque, addr);
    //~ uint32_t ret = cpu_inb(NULL, addr & 0xffff);
//~ #if 1
    //~ if (logfile)
        //~ fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, ret);
//~ #endif
    //~ return ret;
}

static void io_writew (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    ar7_io_memwrite(opaque, addr, value);
//~ #if 1
    //~ if (logfile)
        //~ fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, value);
//~ #endif
    //~ assert(bigendian == 0);
    //~ if (bigendian) {
        //~ value = bswap16(value);
    //~ }
    //~ cpu_outw(NULL, addr & 0xffff, value);
}

static uint32_t io_readw (void *opaque, target_phys_addr_t addr)
{
    return ar7_io_memread(opaque, addr);
    //~ uint32_t ret = cpu_inw(NULL, addr & 0xffff);
    //~ assert(bigendian == 0);
    //~ if (bigendian) {
        //~ ret = bswap16(ret);
    //~ }
//~ #if 1
    //~ if (logfile)
        //~ fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, ret);
//~ #endif
    //~ return ret;
}

static void io_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    ar7_io_memwrite(opaque, addr, value);
//~ #if 1
    //~ if (logfile)
        //~ fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, value);
//~ #endif
    //~ assert(bigendian == 0);
    //~ if (bigendian) {
        //~ value = bswap32(value);
    //~ }
    //~ cpu_outl(NULL, addr & 0xffff, value);
}

static uint32_t io_readl (void *opaque, target_phys_addr_t addr)
{
    return ar7_io_memread(opaque, addr);
    //~ target_phys_addr_t addr = (0x08610000 & 0xffff);

    //~ uint32_t ret = cpu_inl(NULL, addr & 0xffff);

    //~ assert(bigendian == 0);
    //~ if (bigendian) {
        //~ ret = bswap32(ret);
    //~ }
//~ #if 1
    //~ if (logfile)
        //~ fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, ret);
//~ #endif
    //~ return ret;
}

static CPUWriteMemoryFunc * const io_write[] = {
    io_writeb,
    io_writew,
    io_writel,
};

static CPUReadMemoryFunc * const io_read[] = {
    io_readb,
    io_readw,
    io_readl,
};

static void ar7_serial_init(CPUState *env)
{
  /* By default, QEMU only opens one serial console.
   * In this case we open a second console here because
   * we need it for full hardware emulation.
   */
    av.cpu_env = env;
    if (serial_hds[1] == 0) {
        serial_hds[1] = qemu_chr_open("null");
    }
#define IRQ_OPAQUE 0
    serial_16450_init(ar7_irq, IRQ_OPAQUE,
      UART_MEM_TO_IO(AVALANCHE_UART0_BASE), 15, serial_hds[0]);
    serial_16450_init(ar7_irq, IRQ_OPAQUE,
      UART_MEM_TO_IO(AVALANCHE_UART1_BASE), 16, serial_hds[1]);
}

static int ar7_nic_can_receive(void *opaque)
{
    NICState *s = (NICState *)opaque;
    logout("opaque=%p\n", s);

    //~ if (s->cmd & E8390_STOP)
        return 1;
    //~ return !ne2000_buffer_full(s);
}

static void ar7_nic_receive(void *opaque, const uint8_t *buf, int size)
{
    /* Received a packet. */
    static const uint8_t broadcast_macaddr[6] = 
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    NICState *s = (NICState *)opaque;

    if (!memcmp(buf, broadcast_macaddr, sizeof(broadcast_macaddr))) {
        logout("s=%p, buf=%p, size=%d broadcast\n", s, buf, size);
    } else if (buf[0] & 0x01) {
        logout("s=%p, buf=%p, size=%d multicast\n", s, buf, size);
    } else {
        logout("s=%p, buf=%p, size=%d\n", s, buf, size);
    }

/* Rcb/Tcb Constants */

#define CB_SOF_BIT         (1<<31)
#define CB_EOF_BIT         (1<<30)
#define CB_SOF_AND_EOF_BIT (CB_SOF_BIT|CB_EOF_BIT)
#define CB_OWNERSHIP_BIT   (1<<29)
#define CB_EOQ_BIT         (1<<28)
#define CB_SIZE_MASK       0x0000ffff
#define RCB_ERRORS_MASK    0x03fe0000

    uint32_t val = av.cpmac0[0x188];
    if (val != 0) {
        cpmac_buff_t bd;
        cpu_physical_memory_read(val, (uint8_t *)&bd, sizeof(bd));
        logout("buffer 0x%08x, next 0x%08x, buff 0x%08x, params 0x%08x, len 0x%08x\n",
                val, (unsigned)bd.next, (unsigned)bd.buff,
                (unsigned)bd.buff_params, (unsigned)bd.ctrl_n_len);
        bd.ctrl_n_len &= ~(CB_OWNERSHIP_BIT);
        bd.ctrl_n_len |= (size & CB_SIZE_MASK);
        bd.ctrl_n_len |= CB_SOF_BIT | CB_EOF_BIT /*| CB_OWNERSHIP_BIT */;
        cpu_physical_memory_write(val, (uint8_t *)&bd, sizeof(bd));
        val = (uint32_t)bd.next;
        cpu_physical_memory_write((target_phys_addr_t)bd.buff, buf, size);
    }

    av.cpmac0[0x60] |= MAC_IN_VECTOR_RX_INT_OR;
    ar7_irq(0, 27, 1);  // !!! fix
}

static void ar7_nic_init(void)
{
    unsigned i;
    unsigned n = 0;
    logout("\n");
    for (i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];
        if (nd->vlan) {
          if (n < 2 && (nd->model == NULL
            || strcmp(nd->model, "ar7") == 0)) {
            logout("starting AR7 nic CPMAC%u\n", n);
            av.nic[n++].vc = qemu_new_vlan_client(nd->vlan, ar7_nic_receive,
                                 ar7_nic_can_receive, &av.nic);
          } else {
            fprintf(stderr, "qemu: Unsupported NIC: %s\n", nd_table[0].model);
            exit (1);
          }
        }
    }
}

static int ar7_load(QEMUFile *f, void *opaque, int version_id)
{
        int result = 0;
        if (version_id == 0) {
                qemu_get_buffer(f, (uint8_t *)&av, sizeof(av));
        } else {
                result = -EINVAL;
        }
        return result;
}

static void ar7_save(QEMUFile *f, void* opaque)
{
        /* TODO: fix */
        qemu_put_buffer(f, (uint8_t *)&av, sizeof(av));
}

static void ar7_reset(void *opaque)
{
    CPUState *env = opaque;
    logout("%s:%u\n", __FILE__, __LINE__);
    env->exception_index = EXCP_RESET;
    do_interrupt(env);
    //~ env->CP0_Cause |= 0x00000400;
    //~ cpu_interrupt(env, CPU_INTERRUPT_RESET);
}

void ar7_init(CPUState *env)
{
    //~ target_phys_addr_t addr = (0x08610000 & 0xffff);
    //~ unsigned offset;
    int io_memory = cpu_register_io_memory(0, io_read, io_write, env);
    cpu_register_physical_memory(0x08610000, 0x0002800, io_memory);
    assert(bigendian == 0);
    bigendian = env->bigendian;
    assert(bigendian == 0);
    logout("setting endianness %d\n", bigendian);
    ar7_serial_init(env);
    ar7_nic_init();
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
            //~ struct SerialState state = {
                    //~ base: 0,
                    //~ it_shift: 0
            //~ };
    //~ s_io_memory = cpu_register_io_memory(&state, mips_mm_read, mips_mm_write, 0);
    //~ cpu_register_physical_memory(0x08610000, 0x2000, s_io_memory);
    //~ }
#define ar7_instance 0
#define ar7_version 0
    qemu_register_reset(ar7_reset, env);
    register_savevm("ar7", ar7_instance, ar7_version, ar7_save, ar7_load, 0);
}

//~ __dev_mc_upload
//~ notifier_call_chain
//~ halControl
//~ cpmac_handle_tasklet
//~ halIsr
//~ _mdioUserAccessRead

/*
1   breakpoint     keep y   0xffffffff900010b4 proc_fs.h:153
2   breakpoint     keep y   0xffffffff940b887c in halSend at cppi_cpmac.c:600
        breakpoint already hit 3 times
3   breakpoint     keep y   0xffffffff940b9bd4 in DuplexUpdate at hcpmac.c:269
4   breakpoint     keep y   0xffffffff940b9974 in cpMacMdioGetLinked at cpmdio.c:869
5   breakpoint     keep y   0xffffffff940bd074 in cpMacMdioTic at cpmdio.c:789
        breakpoint already hit 1 time
(gdb)                 
* TODO:
* ar7 w: unknown address 0x08610b1c
* Serial emulation:
  serial device is 16550A, not 16450:
  ttyS01 at 0xa8610f00 (irq = 16) is a 16550A
* VLYNQ emulation:
  VLYNQ INIT FAILED: Please try cold reboot. 
  VLYNQ 0 : init failed
* PHY emulation:
  PhyControl: 1000, Lookback=Off, Speed=10, Duplex=Half
  PhyStatus: 782D, AutoNeg=Complete, Link=Up
  PhyMyCapability: 01E1, 100FD=Yes, 100HD=Yes, 10FD=Yes, 10HD=Yes
  PhyPartnerCapability: 85E1, 100FD=Yes, 100HD=Yes, 10FD=Yes, 10HD=Yes
  0: Phy= 31, Speed=100, Duplex=Full
  DuplexUpdate[0]: MACCONTROL 0x00000021, Linked
  [halStatus] Link Status is 7 for 0xA8610000
(gdb) p *HalDev->OsFunc
$10 = {Control = 0x940b5858 <cpmac_hal_control>, CriticalOn = 0x940b75b8 <os_critical_on>, CriticalOff = 0x940b75e0 <os_critical_off>,
  DataCacheHitInvalidate = 0x940b7614 <os_cache_invalidate>, DataCacheHitWriteback = 0x940b7624 <os_cache_writeback>,
  DeviceFindInfo = 0x940b742c <os_find_device>, DeviceFindParmUint = 0x940b7374 <os_find_parm_u_int>,
  DeviceFindParmValue = 0x940b73d4 <os_find_parm_val>, Free = 0x940b748c <os_free>, FreeRxBuffer = 0x940b7494 <os_free_buffer>,
  FreeDev = 0x940b7590 <os_free_dev>, FreeDmaXfer = 0x940b7598 <os_free_dma_xfer>, IsrRegister = 0x940b76b0 <hal_drv_register_isr>,
  IsrUnRegister = 0x940b7634 <hal_drv_unregister_isr>, Malloc = 0x940b75a0 <os_malloc>, MallocDev = 0x940b75b0 <os_malloc_dev>,
  MallocDmaXfer = 0x940b75a8 <os_malloc_dma_xfer>, MallocRxBuffer = 0x940b55e4 <cpmac_hal_malloc_buffer>, Memset = 0x941313e0 <memset>,
  Printf = 0x94013d54 <printk>, Receive = 0x940b6abc <cpmac_hal_receive>, SendComplete = 0x940b6f38 <cpmac_hal_send_complete>,
  Sprintf = 0x94133070 <sprintf>, Strcmpi = 0x940b72b0 <cpmac_ci_strcmp>, Strlen = 0x94131fa4 <strlen>, Strstr = 0x9413223c <strstr>,
  Strtoul = 0x941323f0 <simple_strtol>, TeardownComplete = 0x940b6f14 <cpmac_hal_tear_down_complete>}
*/
#if 0
static void ar7_machine_power_off(void)
{
        volatile uint32_t *power_reg = (void *)(KSEG1ADDR(0x08610A00));
        uint32_t power_state = *power_reg;

        /* add something to turn LEDs off? */

        power_state &= ~(3 << 30);
        power_state |=  (3 << 30); /* power down */
        *power_reg = power_state;

        printk("after power down?\n");
}
#define AVALANCHE_POWER_CTRL_PDCR     (KSEG1ADDR(0x08610A00))
#define AVALANCHE_WAKEUP_CTRL_WKCR    (KSEG1ADDR(0x08610A0C))
./arch/mips/ar7/tnetd73xx_misc.c:#define CLKC_CLKCR(x)          (TNETD73XX_CLOCK_CTRL_BASE + 0x20 + (0x20 * (x)))
./arch/mips/ar7/tnetd73xx_misc.c:#define CLKC_CLKPLLCR(x)       (TNETD73XX_CLOCK_CTRL_BASE + 0x30 + (0x20 * (x)))
./include/asm-mips/ar7/tnetd73xx.h:#define TNETD73XX_CLOCK_CTRL_BASE           PHYS_TO_K1(0x08610A00)      /* Clock Control */
./include/asm-mips/ar7/tnetd73xx.h:#define TNETD73XX_POWER_CTRL_PDCR           (TNETD73XX_CLOCK_CTRL_BASE + 0x0)
./include/asm-mips/ar7/tnetd73xx.h:#define TNETD73XX_POWER_CTRL_PCLKCR         (TNETD73XX_CLOCK_CTRL_BASE + 0x4)
./include/asm-mips/ar7/tnetd73xx.h:#define TNETD73XX_POWER_CTRL_PDUCR          (TNETD73XX_CLOCK_CTRL_BASE + 0x8)
./include/asm-mips/ar7/tnetd73xx.h:#define TNETD73XX_POWER_CTRL_WKCR           (TNETD73XX_CLOCK_CTRL_BASE + 0xC)
./include/asm-mips/ar7/tnetd73xx.h:#define TNETD73XX_CLK_CTRL_SCLKCR           (TNETD73XX_CLOCK_CTRL_BASE + 0x20)
./include/asm-mips/ar7/tnetd73xx.h:#define TNETD73XX_CLK_CTRL_SCLKPLLCR        (TNETD73XX_CLOCK_CTRL_BASE + 0x30)
./include/asm-mips/ar7/tnetd73xx.h:#define TNETD73XX_CLK_CTRL_MCLKCR           (TNETD73XX_CLOCK_CTRL_BASE + 0x40)
./include/asm-mips/ar7/tnetd73xx.h:#define TNETD73XX_CLK_CTRL_MCLKPLLCR        (TNETD73XX_CLOCK_CTRL_BASE + 0x50)
./include/asm-mips/ar7/tnetd73xx.h:#define TNETD73XX_CLK_CTRL_UCLKCR           (TNETD73XX_CLOCK_CTRL_BASE + 0x60)
./include/asm-mips/ar7/tnetd73xx.h:#define TNETD73XX_CLK_CTRL_UCLKPLLCR        (TNETD73XX_CLOCK_CTRL_BASE + 0x70)
./include/asm-mips/ar7/tnetd73xx.h:#define TNETD73XX_CLK_CTRL_ACLKCR0          (TNETD73XX_CLOCK_CTRL_BASE + 0x80)
./include/asm-mips/ar7/tnetd73xx.h:#define TNETD73XX_CLK_CTRL_ACLKPLLCR0       (TNETD73XX_CLOCK_CTRL_BASE + 0x90)
./include/asm-mips/ar7/tnetd73xx.h:#define TNETD73XX_CLK_CTRL_ACLKCR1          (TNETD73XX_CLOCK_CTRL_BASE + 0xA0)
./include/asm-mips/ar7/tnetd73xx.h:#define TNETD73XX_CLK_CTRL_ACLKPLLCR1       (TNETD73XX_CLOCK_CTRL_BASE + 0xB0)


Num Type           Disp Enb Address    What
2   breakpoint     keep y   0x08074592 in pflash_register
                                       at /home/stefan/public_html/ar7-firmware.berlios.de/qemu/trunk/qemu/hw/pflash_cfi02.c:533
        breakpoint already hit 2 times
3   breakpoint     keep y   0x0808e86f in cpu_register_io_memory
                                       at /home/stefan/public_html/ar7-firmware.berlios.de/qemu/trunk/qemu/exec.c:2026
        breakpoint already hit 12 times
4   breakpoint     keep y   0x0808e584 in unassigned_mem_readb
                                       at /home/stefan/public_html/ar7-firmware.berlios.de/qemu/trunk/qemu/exec.c:1892
5   breakpoint     keep y   0x0808e58e in unassigned_mem_writeb
                                       at /home/stefan/public_html/ar7-firmware.berlios.de/qemu/trunk/qemu/exec.c:1896
6   breakpoint     keep y   0x08074481 in pflash_writeb
                                       at /home/stefan/public_html/ar7-firmware.berlios.de/qemu/trunk/qemu/hw/pflash_cfi02.c:459
7   breakpoint     keep y   0x080744aa in pflash_writew
                                       at /home/stefan/public_html/ar7-firmware.berlios.de/qemu/trunk/qemu/hw/pflash_cfi02.c:465
8   breakpoint     keep y   0x080744d9 in pflash_writel
                                       at /home/stefan/public_html/ar7-firmware.berlios.de/qemu/trunk/qemu/hw/pflash_cfi02.c:473
10  breakpoint     keep y   0x0808e9b8 in cpu_physical_memory_rw
                                       at /home/stefan/public_html/ar7-firmware.berlios.de/qemu/trunk/qemu/exec.c:2113
11  breakpoint     keep y   0x0808e385 in tlb_set_page_exec at /home/stefan/public_html/ar7-firmware.berlios.de/qemu/trunk/qemu/exec.c:1592
        breakpoint already hit 2 times
 
(gdb) p/x *pfl
$4 = {bs = 0x0, base = 0x10000000, sector_len = 0x10000, total_len = 0x200000, width = 0x2, wcycle = 0x0, bypass = 0x0, ro = 0x0,
  cmd = 0x0, status = 0x0, ident = {0x1111, 0x2222, 0x3333, 0x4444}, cfi_len = 0x52, cfi_table = {0x0 <repeats 16 times>, 0x51, 0x52, 0x59,
    0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x27, 0x36, 0x0, 0x0, 0x7, 0x4, 0x9, 0xc, 0x1, 0x4, 0xa, 0xd, 0x16, 0x2, 0x0, 0x5, 0x0, 0x1,
    0x1f, 0x0, 0x0, 0x1, 0x0 <repeats 33 times>}, timer = 0x9ad7008, off = 0x1000000, fl_mem = 0x50, storage = 0x9f3af000}

#endif
