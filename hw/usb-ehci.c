/*
 * QEMU USB EHCI Emulation
 *
 * Copyright(c) 2008  Emutex Ltd. (address@hidden)
 *
 * EHCI project was started by Mark Burkley, with contributions by
 * Niels de Vos.  David S. Ahern continued working on it.  Kevin Wolf,
 * Jan Kiszka and Vincent Palatin contributed bugfixes.
 *
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or(at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * TODO:
 *  o Downstream port handoff
 */

#include "hw.h"
#include "qemu-timer.h"
#include "usb.h"
#include "pci.h"
#include "monitor.h"

#define EHCI_DEBUG   0
#define STATE_DEBUG  0       /* state transitions  */

#if EHCI_DEBUG || STATE_DEBUG
#define DPRINTF printf
#else
#define DPRINTF(...)
#endif

#if STATE_DEBUG
#define DPRINTF_ST DPRINTF
#else
#define DPRINTF_ST(...)
#endif

/* internal processing - reset HC to try and recover */
#define USB_RET_PROCERR   (-99)

#define MMIO_SIZE        0x1000

/* Capability Registers Base Address - section 2.2 */
#define CAPREGBASE       0x0000
#define CAPLENGTH        CAPREGBASE + 0x0000  // 1-byte, 0x0001 reserved
#define HCIVERSION       CAPREGBASE + 0x0002  // 2-bytes, i/f version #
#define HCSPARAMS        CAPREGBASE + 0x0004  // 4-bytes, structural params
#define HCCPARAMS        CAPREGBASE + 0x0008  // 4-bytes, capability params
#define EECP             HCCPARAMS + 1
#define HCSPPORTROUTE1   CAPREGBASE + 0x000c
#define HCSPPORTROUTE2   CAPREGBASE + 0x0010

#define OPREGBASE        0x0020        // Operational Registers Base Address

#define USBCMD           OPREGBASE + 0x0000
#define USBCMD_RUNSTOP   (1 << 0)      // run / Stop
#define USBCMD_HCRESET   (1 << 1)      // HC Reset
#define USBCMD_FLS       (3 << 2)      // Frame List Size
#define USBCMD_FLS_SH    2             // Frame List Size Shift
#define USBCMD_PSE       (1 << 4)      // Periodic Schedule Enable
#define USBCMD_ASE       (1 << 5)      // Asynch Schedule Enable
#define USBCMD_IAAD      (1 << 6)      // Int Asynch Advance Doorbell
#define USBCMD_LHCR      (1 << 7)      // Light Host Controller Reset
#define USBCMD_ASPMC     (3 << 8)      // Async Sched Park Mode Count
#define USBCMD_ASPME     (1 << 11)     // Async Sched Park Mode Enable
#define USBCMD_ITC       (0x7f << 16)  // Int Threshold Control
#define USBCMD_ITC_SH    16            // Int Threshold Control Shift

#define USBSTS           OPREGBASE + 0x0004
#define USBSTS_RO_MASK   0x0000003f
#define USBSTS_INT       (1 << 0)      // USB Interrupt
#define USBSTS_ERRINT    (1 << 1)      // Error Interrupt
#define USBSTS_PCD       (1 << 2)      // Port Change Detect
#define USBSTS_FLR       (1 << 3)      // Frame List Rollover
#define USBSTS_HSE       (1 << 4)      // Host System Error
#define USBSTS_IAA       (1 << 5)      // Interrupt on Async Advance
#define USBSTS_HALT      (1 << 12)     // HC Halted
#define USBSTS_REC       (1 << 13)     // Reclamation
#define USBSTS_PSS       (1 << 14)     // Periodic Schedule Status
#define USBSTS_ASS       (1 << 15)     // Asynchronous Schedule Status

/*
 *  Interrupt enable bits correspond to the interrupt active bits in USBSTS
 *  so no need to redefine here.
 */
#define USBINTR              OPREGBASE + 0x0008
#define USBINTR_MASK         0x0000003f

#define FRINDEX              OPREGBASE + 0x000c
#define CTRLDSSEGMENT        OPREGBASE + 0x0010
#define PERIODICLISTBASE     OPREGBASE + 0x0014
#define ASYNCLISTADDR        OPREGBASE + 0x0018
#define ASYNCLISTADDR_MASK   0xffffffe0

#define CONFIGFLAG           OPREGBASE + 0x0040

#define PORTSC               (OPREGBASE + 0x0044)
#define PORTSC_BEGIN         PORTSC
#define PORTSC_END           (PORTSC + 4 * NB_PORTS)
/*
 * Bits that are reserverd or are read-only are masked out of values
 * written to us by software
 */
#define PORTSC_RO_MASK       0x007021c5
#define PORTSC_RWC_MASK      0x0000002a
#define PORTSC_WKOC_E        (1 << 22)    // Wake on Over Current Enable
#define PORTSC_WKDS_E        (1 << 21)    // Wake on Disconnect Enable
#define PORTSC_WKCN_E        (1 << 20)    // Wake on Connect Enable
#define PORTSC_PTC           (15 << 16)   // Port Test Control
#define PORTSC_PTC_SH        16           // Port Test Control shift
#define PORTSC_PIC           (3 << 14)    // Port Indicator Control
#define PORTSC_PIC_SH        14           // Port Indicator Control Shift
#define PORTSC_POWNER        (1 << 13)    // Port Owner
#define PORTSC_PPOWER        (1 << 12)    // Port Power
#define PORTSC_LINESTAT      (3 << 10)    // Port Line Status
#define PORTSC_LINESTAT_SH   10           // Port Line Status Shift
#define PORTSC_PRESET        (1 << 8)     // Port Reset
#define PORTSC_SUSPEND       (1 << 7)     // Port Suspend
#define PORTSC_FPRES         (1 << 6)     // Force Port Resume
#define PORTSC_OCC           (1 << 5)     // Over Current Change
#define PORTSC_OCA           (1 << 4)     // Over Current Active
#define PORTSC_PEDC          (1 << 3)     // Port Enable/Disable Change
#define PORTSC_PED           (1 << 2)     // Port Enable/Disable
#define PORTSC_CSC           (1 << 1)     // Connect Status Change
#define PORTSC_CONNECT       (1 << 0)     // Current Connect Status

#define FRAME_TIMER_FREQ 1000
#define FRAME_TIMER_USEC (1000000 / FRAME_TIMER_FREQ)

#define NB_MAXINTRATE    8        // Max rate at which controller issues ints
#define NB_PORTS         4        // Number of downstream ports
#define BUFF_SIZE        5*4096   // Max bytes to transfer per transaction
#define MAX_ITERATIONS   20       // Max number of QH before we break the loop
#define MAX_QH           100      // Max allowable queue heads in a chain

/*  Internal periodic / asynchronous schedule state machine states
 */
typedef enum {
    EST_INACTIVE = 1000,
    EST_ACTIVE,
    EST_EXECUTING,
    EST_SLEEPING,
    /*  The following states are internal to the state machine function
    */
    EST_WAITLISTHEAD,
    EST_FETCHENTRY,
    EST_FETCHQH,
    EST_FETCHITD,
    EST_ADVANCEQUEUE,
    EST_FETCHQTD,
    EST_EXECUTE,
    EST_WRITEBACK,
    EST_HORIZONTALQH
} EHCI_STATES;

/* macros for accessing fields within next link pointer entry */
#define NLPTR_GET(x)             ((x) & 0xffffffe0)
#define NLPTR_TYPE_GET(x)        (((x) >> 1) & 3)
#define NLPTR_TBIT(x)            ((x) & 1)  // 1=invalid, 0=valid

/* link pointer types */
#define NLPTR_TYPE_ITD           0     // isoc xfer descriptor
#define NLPTR_TYPE_QH            1     // queue head
#define NLPTR_TYPE_STITD         2     // split xaction, isoc xfer descriptor
#define NLPTR_TYPE_FSTN          3     // frame span traversal node


/*  EHCI spec version 1.0 Section 3.3
 */
typedef struct EHCIitd {
    uint32_t next;

    uint32_t transact[8];
#define ITD_XACT_ACTIVE          (1 << 31)
#define ITD_XACT_DBERROR         (1 << 30)
#define ITD_XACT_BABBLE          (1 << 29)
#define ITD_XACT_XACTERR         (1 << 28)
#define ITD_XACT_LENGTH_MASK     0x0fff0000
#define ITD_XACT_LENGTH_SH       16
#define ITD_XACT_IOC             (1 << 15)
#define ITD_XACT_PGSEL_MASK      0x00007000
#define ITD_XACT_PGSEL_SH        12
#define ITD_XACT_OFFSET_MASK     0x00000fff

    uint32_t bufptr[7];
#define ITD_BUFPTR_MASK          0xfffff000
#define ITD_BUFPTR_SH            12
#define ITD_BUFPTR_EP_MASK       0x00000f00
#define ITD_BUFPTR_EP_SH         8
#define ITD_BUFPTR_DEVADDR_MASK  0x0000007f
#define ITD_BUFPTR_DEVADDR_SH    0
#define ITD_BUFPTR_DIRECTION     (1 << 11)
#define ITD_BUFPTR_MAXPKT_MASK   0x000007ff
#define ITD_BUFPTR_MAXPKT_SH     0
#define ITD_BUFPTR_MULT_MASK     0x00000003
} EHCIitd;

/*  EHCI spec version 1.0 Section 3.4
 */
typedef struct EHCIsitd {
    uint32_t next;                  // Standard next link pointer
    uint32_t epchar;
#define SITD_EPCHAR_IO              (1 << 31)
#define SITD_EPCHAR_PORTNUM_MASK    0x7f000000
#define SITD_EPCHAR_PORTNUM_SH      24
#define SITD_EPCHAR_HUBADD_MASK     0x007f0000
#define SITD_EPCHAR_HUBADDR_SH      16
#define SITD_EPCHAR_EPNUM_MASK      0x00000f00
#define SITD_EPCHAR_EPNUM_SH        8
#define SITD_EPCHAR_DEVADDR_MASK    0x0000007f

    uint32_t uframe;
#define SITD_UFRAME_CMASK_MASK      0x0000ff00
#define SITD_UFRAME_CMASK_SH        8
#define SITD_UFRAME_SMASK_MASK      0x000000ff

    uint32_t results;
#define SITD_RESULTS_IOC              (1 << 31)
#define SITD_RESULTS_PGSEL            (1 << 30)
#define SITD_RESULTS_TBYTES_MASK      0x03ff0000
#define SITD_RESULTS_TYBYTES_SH       16
#define SITD_RESULTS_CPROGMASK_MASK   0x0000ff00
#define SITD_RESULTS_CPROGMASK_SH     8
#define SITD_RESULTS_ACTIVE           (1 << 7)
#define SITD_RESULTS_ERR              (1 << 6)
#define SITD_RESULTS_DBERR            (1 << 5)
#define SITD_RESULTS_BABBLE           (1 << 4)
#define SITD_RESULTS_XACTERR          (1 << 3)
#define SITD_RESULTS_MISSEDUF         (1 << 2)
#define SITD_RESULTS_SPLITXSTATE      (1 << 1)

    uint32_t bufptr[2];
#define SITD_BUFPTR_MASK              0xfffff000
#define SITD_BUFPTR_CURROFF_MASK      0x00000fff
#define SITD_BUFPTR_TPOS_MASK         0x00000018
#define SITD_BUFPTR_TPOS_SH           3
#define SITD_BUFPTR_TCNT_MASK         0x00000007

    uint32_t backptr;                 // Standard next link pointer
} EHCIsitd;

/*  EHCI spec version 1.0 Section 3.5
 */
typedef struct EHCIqtd {
    uint32_t next;                    // Standard next link pointer
    uint32_t altnext;                 // Standard next link pointer
    uint32_t token;
#define QTD_TOKEN_DTOGGLE             (1 << 31)
#define QTD_TOKEN_TBYTES_MASK         0x7fff0000
#define QTD_TOKEN_TBYTES_SH           16
#define QTD_TOKEN_IOC                 (1 << 15)
#define QTD_TOKEN_CPAGE_MASK          0x00007000
#define QTD_TOKEN_CPAGE_SH            12
#define QTD_TOKEN_CERR_MASK           0x00000c00
#define QTD_TOKEN_CERR_SH             10
#define QTD_TOKEN_PID_MASK            0x00000300
#define QTD_TOKEN_PID_SH              8
#define QTD_TOKEN_ACTIVE              (1 << 7)
#define QTD_TOKEN_HALT                (1 << 6)
#define QTD_TOKEN_DBERR               (1 << 5)
#define QTD_TOKEN_BABBLE              (1 << 4)
#define QTD_TOKEN_XACTERR             (1 << 3)
#define QTD_TOKEN_MISSEDUF            (1 << 2)
#define QTD_TOKEN_SPLITXSTATE         (1 << 1)
#define QTD_TOKEN_PING                (1 << 0)

    uint32_t bufptr[5];               // Standard buffer pointer
#define QTD_BUFPTR_MASK               0xfffff000
} EHCIqtd;

/*  EHCI spec version 1.0 Section 3.6
 */
typedef struct EHCIqh {
    uint32_t next;                    // Standard next link pointer

    /* endpoint characteristics */
    uint32_t epchar;
#define QH_EPCHAR_RL_MASK             0xf0000000
#define QH_EPCHAR_RL_SH               28
#define QH_EPCHAR_C                   (1 << 27)
#define QH_EPCHAR_MPLEN_MASK          0x07FF0000
#define QH_EPCHAR_MPLEN_SH            16
#define QH_EPCHAR_H                   (1 << 15)
#define QH_EPCHAR_DTC                 (1 << 14)
#define QH_EPCHAR_EPS_MASK            0x00003000
#define QH_EPCHAR_EPS_SH              12
#define EHCI_QH_EPS_FULL              0
#define EHCI_QH_EPS_LOW               1
#define EHCI_QH_EPS_HIGH              2
#define EHCI_QH_EPS_RESERVED          3

#define QH_EPCHAR_EP_MASK             0x00000f00
#define QH_EPCHAR_EP_SH               8
#define QH_EPCHAR_I                   (1 << 7)
#define QH_EPCHAR_DEVADDR_MASK        0x0000007f
#define QH_EPCHAR_DEVADDR_SH          0

    /* endpoint capabilities */
    uint32_t epcap;
#define QH_EPCAP_MULT_MASK            0xc0000000
#define QH_EPCAP_MULT_SH              30
#define QH_EPCAP_PORTNUM_MASK         0x3f800000
#define QH_EPCAP_PORTNUM_SH           23
#define QH_EPCAP_HUBADDR_MASK         0x007f0000
#define QH_EPCAP_HUBADDR_SH           16
#define QH_EPCAP_CMASK_MASK           0x0000ff00
#define QH_EPCAP_CMASK_SH             8
#define QH_EPCAP_SMASK_MASK           0x000000ff
#define QH_EPCAP_SMASK_SH             0

    uint32_t current_qtd;             // Standard next link pointer
    uint32_t next_qtd;                // Standard next link pointer
    uint32_t altnext_qtd;
#define QH_ALTNEXT_NAKCNT_MASK        0x0000001e
#define QH_ALTNEXT_NAKCNT_SH          1

    uint32_t token;                   // Same as QTD token
    uint32_t bufptr[5];               // Standard buffer pointer
#define BUFPTR_CPROGMASK_MASK         0x000000ff
#define BUFPTR_FRAMETAG_MASK          0x0000001f
#define BUFPTR_SBYTES_MASK            0x00000fe0
#define BUFPTR_SBYTES_SH              5
} EHCIqh;

/*  EHCI spec version 1.0 Section 3.7
 */
typedef struct EHCIfstn {
    uint32_t next;                    // Standard next link pointer
    uint32_t backptr;                 // Standard next link pointer
} EHCIfstn;

typedef struct {
    PCIDevice dev;
    qemu_irq irq;
    target_phys_addr_t mem_base;
    int mem;
    int num_ports;
    /*
     *  EHCI spec version 1.0 Section 2.3
     *  Host Controller Operational Registers
     */
    union {
        uint8_t mmio[MMIO_SIZE];
        struct {
            uint8_t cap[OPREGBASE];
            uint32_t usbcmd;
            uint32_t usbsts;
            uint32_t usbintr;
            uint32_t frindex;
            uint32_t ctrldssegment;
            uint32_t periodiclistbase;
            uint32_t asynclistaddr;
            uint32_t notused[9];
            uint32_t configflag;
            uint32_t portsc[NB_PORTS];
        };
    };
    /*
     *  Internal states, shadow registers, etc
     */
    uint32_t sofv;
    QEMUTimer *frame_timer;
    int attach_poll_counter;
    int astate;                        // Current state in asynchronous schedule
    int pstate;                        // Current state in periodic schedule
    USBPort ports[NB_PORTS];
    uint8_t buffer[BUFF_SIZE];
    uint32_t usbsts_pending;

    /* cached data from guest - needs to be flushed
     * when guest removes an entry (doorbell, handshake sequence)
     */
    EHCIqh qh;             // copy of current QH (being worked on)
    uint32_t qhaddr;       // address QH read from

    EHCIqtd qtd;           // copy of current QTD (being worked on)
    uint32_t qtdaddr;      // address QTD read from

    uint32_t itdaddr;      // current ITD

    uint32_t fetch_addr;   // which address to look at next

    USBBus bus;
    USBPacket usb_packet;
    int async_complete;
    uint32_t tbytes;
    int pid;
    int exec_status;
    int isoch_pause;
    uint32_t last_run_usec;
    uint32_t frame_end_usec;
} EHCIState;

#define SET_LAST_RUN_CLOCK(s) \
    (s)->last_run_usec = qemu_get_clock_ns(vm_clock) / 1000;

/* nifty macros from Arnon's EHCI version  */
#define get_field(data, field) \
    (((data) & field##_MASK) >> field##_SH)

#define set_field(data, newval, field) do { \
    uint32_t val = *data; \
    val &= ~ field##_MASK; \
    val |= ((newval) << field##_SH) & field##_MASK; \
    *data = val; \
    } while(0)


#if EHCI_DEBUG
static const char *addr2str(unsigned addr)
{
    const char *r            = "   unknown";
    const char *n[] = {
        [ CAPLENGTH ]        = " CAPLENGTH",
        [ HCIVERSION ]       = "HCIVERSION",
        [ HCSPARAMS ]        = " HCSPARAMS",
        [ HCCPARAMS ]        = " HCCPARAMS",
        [ USBCMD ]           = "   COMMAND",
        [ USBSTS ]           = "    STATUS",
        [ USBINTR ]          = " INTERRUPT",
        [ FRINDEX ]          = " FRAME IDX",
        [ PERIODICLISTBASE ] = "P-LIST BASE",
        [ ASYNCLISTADDR ]    = "A-LIST ADDR",
        [ PORTSC_BEGIN ...
          PORTSC_END ]       = "PORT STATUS",
        [ CONFIGFLAG ]       = "CONFIG FLAG",
    };

    if (addr < ARRAY_SIZE(n) && n[addr] != NULL) {
        return n[addr];
    } else {
        return r;
    }
}
#endif


static inline void ehci_set_interrupt(EHCIState *s, int intr)
{
    int level = 0;

    // TODO honour interrupt threshold requests

    s->usbsts |= intr;

    if ((s->usbsts & USBINTR_MASK) & s->usbintr) {
        level = 1;
    }

    qemu_set_irq(s->irq, level);
}

static inline void ehci_record_interrupt(EHCIState *s, int intr)
{
    s->usbsts_pending |= intr;
}

static inline void ehci_commit_interrupt(EHCIState *s)
{
    if (!s->usbsts_pending) {
        return;
    }
    ehci_set_interrupt(s, s->usbsts_pending);
    s->usbsts_pending = 0;
}

/* Attach or detach a device on root hub */

static void ehci_attach(USBPort *port)
{
    EHCIState *s = port->opaque;
    uint32_t *portsc = &s->portsc[port->index];

    DPRINTF("ehci_attach invoked for index %d, portsc 0x%x, desc %s\n",
           port->index, *portsc, port->dev->product_desc);

    *portsc |= PORTSC_CONNECT;
    *portsc |= PORTSC_CSC;

    /*
     *  If a high speed device is attached then we own this port(indicated
     *  by zero in the PORTSC_POWNER bit field) so set the status bit
     *  and set an interrupt if enabled.
     */
    if ( !(*portsc & PORTSC_POWNER)) {
        ehci_set_interrupt(s, USBSTS_PCD);
    }
}

static void ehci_detach(USBPort *port)
{
    EHCIState *s = port->opaque;
    uint32_t *portsc = &s->portsc[port->index];

    DPRINTF("ehci_attach invoked for index %d, portsc 0x%x\n",
           port->index, *portsc);

    *portsc &= ~PORTSC_CONNECT;
    *portsc |= PORTSC_CSC;

    /*
     *  If a high speed device is attached then we own this port(indicated
     *  by zero in the PORTSC_POWNER bit field) so set the status bit
     *  and set an interrupt if enabled.
     */
    if ( !(*portsc & PORTSC_POWNER)) {
        ehci_set_interrupt(s, USBSTS_PCD);
    }
}

/* 4.1 host controller initialization */
static void ehci_reset(void *opaque)
{
    EHCIState *s = opaque;
    uint8_t *pci_conf;
    int i;

    pci_conf = s->dev.config;

    memset(&s->mmio[OPREGBASE], 0x00, MMIO_SIZE - OPREGBASE);

    s->usbcmd = NB_MAXINTRATE << USBCMD_ITC_SH;
    s->usbsts = USBSTS_HALT;

    s->astate = EST_INACTIVE;
    s->pstate = EST_INACTIVE;
    s->async_complete = 0;
    s->isoch_pause = -1;
    s->attach_poll_counter = 0;

    for(i = 0; i < NB_PORTS; i++) {
        s->portsc[i] = PORTSC_POWNER | PORTSC_PPOWER;

        if (s->ports[i].dev) {
            usb_attach(&s->ports[i], s->ports[i].dev);
        }
    }
}

static uint32_t ehci_mem_readb(void *ptr, target_phys_addr_t addr)
{
    EHCIState *s = ptr;
    uint32_t val;

    val = s->mmio[addr];

    return val;
}

static uint32_t ehci_mem_readw(void *ptr, target_phys_addr_t addr)
{
    EHCIState *s = ptr;
    uint32_t val;

    val = s->mmio[addr] | (s->mmio[addr+1] << 8);

    return val;
}

static uint32_t ehci_mem_readl(void *ptr, target_phys_addr_t addr)
{
    EHCIState *s = ptr;
    uint32_t val;

    val = s->mmio[addr] | (s->mmio[addr+1] << 8) |
          (s->mmio[addr+2] << 16) | (s->mmio[addr+3] << 24);

    return val;
}

static void ehci_mem_writeb(void *ptr, target_phys_addr_t addr, uint32_t val)
{
    fprintf(stderr, "EHCI doesn't handle byte writes to MMIO\n");
    exit(1);
}

static void ehci_mem_writew(void *ptr, target_phys_addr_t addr, uint32_t val)
{
    fprintf(stderr, "EHCI doesn't handle 16-bit writes to MMIO\n");
    exit(1);
}

static void handle_port_status_write(EHCIState *s, int port, uint32_t val)
{
    uint32_t *portsc = &s->portsc[port];
    int rwc;
    USBDevice *dev = s->ports[port].dev;

    DPRINTF("port_status_write: "
            "PORTSC (port %d) curr %08X new %08X rw-clear %08X rw %08X\n",
            port, *portsc, val, (val & PORTSC_RWC_MASK), val & PORTSC_RO_MASK);

    rwc = val & PORTSC_RWC_MASK;
    val &= PORTSC_RO_MASK;

    // handle_read_write_clear(&val, portsc, PORTSC_PEDC | PORTSC_CSC);

    *portsc &= ~rwc;

    if ((val & PORTSC_PRESET) && !(*portsc & PORTSC_PRESET)) {
        DPRINTF("port_status_write: USBTRAN Port %d reset begin\n", port);
    }

    if (!(val & PORTSC_PRESET) &&(*portsc & PORTSC_PRESET)) {
        DPRINTF("port_status_write: USBTRAN Port %d reset done\n", port);
        usb_attach(&s->ports[port], dev);

        // TODO how to handle reset of ports with no device
        if (dev) {
            usb_send_msg(dev, USB_MSG_RESET);
        }

        if (s->ports[port].dev) {
            DPRINTF("port_status_write: "
                    "Device was connected before reset, clearing CSC bit\n");
            *portsc &= ~PORTSC_CSC;
        }

        /*  Table 2.16 Set the enable bit(and enable bit change) to indicate
         *  to SW that this port has a high speed device attached
         *
         *  TODO - when to disable?
         */
        val |= PORTSC_PED;
        val |= PORTSC_PEDC;
    }

    *portsc &= ~PORTSC_RO_MASK;
    *portsc |= val;
    DPRINTF("port_status_write: Port %d status set to 0x%08x\n", port, *portsc);
}

static void ehci_mem_writel(void *ptr, target_phys_addr_t addr, uint32_t val)
{
    EHCIState *s = ptr;
    int i;
#if EHCI_DEBUG
    const char *str;
#endif

    /* Only aligned reads are allowed on OHCI */
    if (addr & 3) {
        fprintf(stderr, "usb-ehci: Mis-aligned write to addr 0x"
                TARGET_FMT_plx "\n", addr);
        return;
    }

    if (addr >= PORTSC && addr < PORTSC + 4 * NB_PORTS) {
        handle_port_status_write(s, (addr-PORTSC)/4, val);
        return;
    }

    if (addr < OPREGBASE) {
        fprintf(stderr, "usb-ehci: write attempt to read-only register"
                TARGET_FMT_plx "\n", addr);
        return;
    }


    /* Do any register specific pre-write processing here.  */
#if EHCI_DEBUG
    str = addr2str((unsigned) addr);
#endif
    switch(addr) {
    case USBCMD:
        DPRINTF("ehci_mem_writel: USBCMD val=0x%08X, current cmd=0x%08X\n",
                val, s->usbcmd);

        if ((val & USBCMD_RUNSTOP) && !(s->usbcmd & USBCMD_RUNSTOP)) {
            DPRINTF("ehci_mem_writel: %s run, clear halt\n", str);
            qemu_mod_timer(s->frame_timer, qemu_get_clock_ns(vm_clock));
            SET_LAST_RUN_CLOCK(s);
            s->usbsts &= ~USBSTS_HALT;
        }

        if (!(val & USBCMD_RUNSTOP) && (s->usbcmd & USBCMD_RUNSTOP)) {
            DPRINTF("                         ** STOP **\n");
            qemu_del_timer(s->frame_timer);
            // TODO - should finish out some stuff before setting halt
            s->usbsts |= USBSTS_HALT;
        }

        if (val & USBCMD_HCRESET) {
            DPRINTF("ehci_mem_writel: %s run, resetting\n", str);
            ehci_reset(s);
            val &= ~USBCMD_HCRESET;
        }

        /* not supporting dynamic frame list size at the moment */
        if ((val & USBCMD_FLS) && !(s->usbcmd & USBCMD_FLS)) {
            fprintf(stderr, "attempt to set frame list size -- value %d\n",
                    val & USBCMD_FLS);
            val &= ~USBCMD_FLS;
        }
#if EHCI_DEBUG
        if ((val & USBCMD_PSE) && !(s->usbcmd & USBCMD_PSE)) {
            DPRINTF("periodic scheduling enabled\n");
        }
        if (!(val & USBCMD_PSE) && (s->usbcmd & USBCMD_PSE)) {
            DPRINTF("periodic scheduling disabled\n");
        }
        if ((val & USBCMD_ASE) && !(s->usbcmd & USBCMD_ASE)) {
            DPRINTF("asynchronous scheduling enabled\n");
        }
        if (!(val & USBCMD_ASE) && (s->usbcmd & USBCMD_ASE)) {
            DPRINTF("asynchronous scheduling disabled\n");
        }
        if ((val & USBCMD_IAAD) && !(s->usbcmd & USBCMD_IAAD)) {
            DPRINTF("doorbell request received\n");
        }
        if ((val & USBCMD_LHCR) && !(s->usbcmd & USBCMD_LHCR)) {
            DPRINTF("light host controller reset received\n");
        }
        if ((val & USBCMD_ITC) != (s->usbcmd & USBCMD_ITC)) {
            DPRINTF("interrupt threshold control set to %x\n",
                    (val & USBCMD_ITC)>>USBCMD_ITC_SH);
        }
#endif
        break;


    case USBSTS:
        val &= USBSTS_RO_MASK;              // bits 6 thru 31 are RO
        DPRINTF("ehci_mem_writel: %s RWC set to 0x%08X\n", str, val);

        val = (s->usbsts &= ~val);         // bits 0 thru 5 are R/WC

        DPRINTF("ehci_mem_writel: %s updating interrupt condition\n", str);
        ehci_set_interrupt(s, 0);
        break;


    case USBINTR:
        val &= USBINTR_MASK;
        DPRINTF("ehci_mem_writel: %s set to 0x%08X\n", str, val);
        break;

    case FRINDEX:
        s->sofv = val >> 3;
        DPRINTF("ehci_mem_writel: %s set to 0x%08X\n", str, val);
        break;

    case CONFIGFLAG:
        DPRINTF("ehci_mem_writel: %s set to 0x%08X\n", str, val);
        val &= 0x1;
        if (val) {
            for(i = 0; i < NB_PORTS; i++)
                s->portsc[i] &= ~PORTSC_POWNER;
        }
        break;

    case PERIODICLISTBASE:
        if ((s->usbcmd & USBCMD_PSE) && (s->usbcmd & USBCMD_RUNSTOP)) {
            fprintf(stderr,
              "ehci: PERIODIC list base register set while periodic schedule\n"
              "      is enabled and HC is enabled\n");
        }
        DPRINTF("ehci_mem_writel: P-LIST BASE set to 0x%08X\n", val);
        break;

    case ASYNCLISTADDR:
        if ((s->usbcmd & USBCMD_ASE) && (s->usbcmd & USBCMD_RUNSTOP)) {
            fprintf(stderr,
              "ehci: ASYNC list address register set while async schedule\n"
              "      is enabled and HC is enabled\n");
        }
        DPRINTF("ehci_mem_writel: A-LIST ADDR set to 0x%08X\n", val);
        break;
    }

    *(uint32_t *)(&s->mmio[addr]) = val;
}


// TODO : Put in common header file, duplication from usb-ohci.c

/* Get an array of dwords from main memory */
static inline int get_dwords(uint32_t addr, uint32_t *buf, int num)
{
    int i;

    for(i = 0; i < num; i++, buf++, addr += sizeof(*buf)) {
        cpu_physical_memory_rw(addr,(uint8_t *)buf, sizeof(*buf), 0);
        *buf = le32_to_cpu(*buf);
    }

    return 1;
}

/* Put an array of dwords in to main memory */
static inline int put_dwords(uint32_t addr, uint32_t *buf, int num)
{
    int i;

    for(i = 0; i < num; i++, buf++, addr += sizeof(*buf)) {
        uint32_t tmp = cpu_to_le32(*buf);
        cpu_physical_memory_rw(addr,(uint8_t *)&tmp, sizeof(tmp), 1);
    }

    return 1;
}

// 4.10.2

static int ehci_qh_do_overlay(EHCIState *ehci, EHCIqh *qh, EHCIqtd *qtd)
{
    int i;
    int dtoggle;
    int ping;
    int eps;
    int reload;

    // remember values in fields to preserve in qh after overlay

    dtoggle = qh->token & QTD_TOKEN_DTOGGLE;
    ping    = qh->token & QTD_TOKEN_PING;

    DPRINTF("setting qh.current from %08X to 0x%08X\n", qh->current_qtd,
            ehci->qtdaddr);
    qh->current_qtd = ehci->qtdaddr;
    qh->next_qtd    = qtd->next;
    qh->altnext_qtd = qtd->altnext;
    qh->token       = qtd->token;


    eps = get_field(qh->epchar, QH_EPCHAR_EPS);
    if (eps == EHCI_QH_EPS_HIGH) {
        qh->token &= ~QTD_TOKEN_PING;
        qh->token |= ping;
    }

    reload = get_field(qh->epchar, QH_EPCHAR_RL);
    set_field(&qh->altnext_qtd, reload, QH_ALTNEXT_NAKCNT);

    for (i = 0; i < 5; i++) {
        qh->bufptr[i] = qtd->bufptr[i];
    }

    if (!(qh->epchar & QH_EPCHAR_DTC)) {
        // preserve QH DT bit
        qh->token &= ~QTD_TOKEN_DTOGGLE;
        qh->token |= dtoggle;
    }

    qh->bufptr[1] &= ~BUFPTR_CPROGMASK_MASK;
    qh->bufptr[2] &= ~BUFPTR_FRAMETAG_MASK;

    put_dwords(NLPTR_GET(ehci->qhaddr), (uint32_t *) qh, sizeof(EHCIqh) >> 2);

    return 0;
}

static int ehci_buffer_rw(uint8_t *buffer, EHCIqh *qh, int bytes, int rw)
{
    int bufpos = 0;
    int cpage, offset;
    uint32_t head;
    uint32_t tail;


    if (!bytes) {
        return 0;
    }

    cpage = get_field(qh->token, QTD_TOKEN_CPAGE);
    if (cpage > 4) {
        fprintf(stderr, "cpage out of range (%d)\n", cpage);
        return USB_RET_PROCERR;
    }

    offset = qh->bufptr[0] & ~QTD_BUFPTR_MASK;
    DPRINTF("ehci_buffer_rw: %sing %d bytes %08x cpage %d offset %d\n",
           rw ? "writ" : "read", bytes, qh->bufptr[0], cpage, offset);

    do {
        /* start and end of this page */
        head = qh->bufptr[cpage] & QTD_BUFPTR_MASK;
        tail = head + ~QTD_BUFPTR_MASK + 1;
        /* add offset into page */
        head |= offset;

        if (bytes <= (tail - head)) {
            tail = head + bytes;
        }

        DPRINTF("DATA %s cpage:%d head:%08X tail:%08X target:%08X\n",
                rw ? "WRITE" : "READ ", cpage, head, tail, bufpos);

        cpu_physical_memory_rw(head, &buffer[bufpos], tail - head, rw);

        bufpos += (tail - head);
        bytes -= (tail - head);

        if (bytes > 0) {
            cpage++;
            offset = 0;
        }
    } while (bytes > 0);

    /* save cpage */
    set_field(&qh->token, cpage, QTD_TOKEN_CPAGE);

    /* save offset into cpage */
    offset = tail - head;
    qh->bufptr[0] &= ~QTD_BUFPTR_MASK;
    qh->bufptr[0] |= offset;

    return 0;
}

static void ehci_async_complete_packet(USBDevice *dev, USBPacket *packet)
{
    EHCIState *ehci = container_of(packet, EHCIState, usb_packet);

    DPRINTF("Async packet complete\n");
    ehci->async_complete = 1;
    ehci->exec_status = packet->len;
}

static int ehci_execute_complete(EHCIState *ehci, EHCIqh *qh, int ret)
{
    int c_err, reload;

    if (ret == USB_RET_ASYNC && !ehci->async_complete) {
        DPRINTF("not done yet\n");
        return ret;
    }

    ehci->async_complete = 0;

    DPRINTF("execute_complete: qhaddr 0x%x, next %x, qtdaddr 0x%x, status %d\n",
            ehci->qhaddr, qh->next, ehci->qtdaddr, ret);

    if (ret < 0) {
err:
        /* TO-DO: put this is in a function that can be invoked below as well */
        c_err = get_field(qh->token, QTD_TOKEN_CERR);
        c_err--;
        set_field(&qh->token, c_err, QTD_TOKEN_CERR);

        switch(ret) {
        case USB_RET_NODEV:
            fprintf(stderr, "USB no device\n");
            break;
        case USB_RET_STALL:
            fprintf(stderr, "USB stall\n");
            qh->token |= QTD_TOKEN_HALT;
            ehci_record_interrupt(ehci, USBSTS_ERRINT);
            break;
        case USB_RET_NAK:
            /* 4.10.3 */
            reload = get_field(qh->epchar, QH_EPCHAR_RL);
            if ((ehci->pid == USB_TOKEN_IN) && reload) {
                int nakcnt = get_field(qh->altnext_qtd, QH_ALTNEXT_NAKCNT);
                nakcnt--;
                set_field(&qh->altnext_qtd, nakcnt, QH_ALTNEXT_NAKCNT);
            } else if (!reload) {
                return USB_RET_NAK;
            }
            break;
        case USB_RET_BABBLE:
            fprintf(stderr, "USB babble TODO\n");
            qh->token |= QTD_TOKEN_BABBLE;
            ehci_record_interrupt(ehci, USBSTS_ERRINT);
            break;
        default:
            fprintf(stderr, "USB invalid response %d to handle\n", ret);
            /* TO-DO: transaction error */
            ret = USB_RET_PROCERR;
            break;
        }
    } else {
        // DPRINTF("Short packet condition\n");
        // TODO check 4.12 for splits

        if ((ret > ehci->tbytes) && (ehci->pid == USB_TOKEN_IN)) {
            ret = USB_RET_BABBLE;
            goto err;
        }

        if (ehci->tbytes && ehci->pid == USB_TOKEN_IN) {
            if (ehci_buffer_rw(ehci->buffer, qh, ret, 1) != 0) {
                return USB_RET_PROCERR;
            }
            ehci->tbytes -= ret;
        } else {
            ehci->tbytes = 0;
        }

        DPRINTF("updating tbytes to %d\n", ehci->tbytes);
        set_field(&qh->token, ehci->tbytes, QTD_TOKEN_TBYTES);
    }

    qh->token ^= QTD_TOKEN_DTOGGLE;
    qh->token &= ~QTD_TOKEN_ACTIVE;

    if ((ret >= 0) && (qh->token & QTD_TOKEN_IOC)) {
        ehci_record_interrupt(ehci, USBSTS_INT);
    }

    return ret;
}

// 4.10.3

static int ehci_execute(EHCIState *ehci, EHCIqh *qh)
{
    USBPort *port;
    USBDevice *dev;
    int ret;
    int i;
    int endp;
    int devadr;

    if ( !(qh->token & QTD_TOKEN_ACTIVE)) {
        fprintf(stderr, "Attempting to execute inactive QH\n");
        return USB_RET_PROCERR;
    }

    ehci->tbytes = (qh->token & QTD_TOKEN_TBYTES_MASK) >> QTD_TOKEN_TBYTES_SH;
    if (ehci->tbytes > BUFF_SIZE) {
        fprintf(stderr, "Request for more bytes than allowed\n");
        return USB_RET_PROCERR;
    }

    ehci->pid = (qh->token & QTD_TOKEN_PID_MASK) >> QTD_TOKEN_PID_SH;
    switch(ehci->pid) {
        case 0: ehci->pid = USB_TOKEN_OUT; break;
        case 1: ehci->pid = USB_TOKEN_IN; break;
        case 2: ehci->pid = USB_TOKEN_SETUP; break;
        default: fprintf(stderr, "bad token\n"); break;
    }

    if ((ehci->tbytes && ehci->pid != USB_TOKEN_IN) &&
        (ehci_buffer_rw(ehci->buffer, qh, ehci->tbytes, 0) != 0)) {
        return USB_RET_PROCERR;
    }

    endp = get_field(qh->epchar, QH_EPCHAR_EP);
    devadr = get_field(qh->epchar, QH_EPCHAR_DEVADDR);

    ret = USB_RET_NODEV;

    // TO-DO: associating device with ehci port
    for(i = 0; i < NB_PORTS; i++) {
        port = &ehci->ports[i];
        dev = port->dev;

        // TODO sometime we will also need to check if we are the port owner

        if (!(ehci->portsc[i] &(PORTSC_CONNECT))) {
            DPRINTF("Port %d, no exec, not connected(%08X)\n",
                    i, ehci->portsc[i]);
            continue;
        }

        ehci->usb_packet.pid = ehci->pid;
        ehci->usb_packet.devaddr = devadr;
        ehci->usb_packet.devep = endp;
        ehci->usb_packet.data = ehci->buffer;
        ehci->usb_packet.len = ehci->tbytes;

        ret = usb_handle_packet(dev, &ehci->usb_packet);

        DPRINTF("submit: qh %x next %x qtd %x pid %x len %d (total %d) endp %x ret %d\n",
                ehci->qhaddr, qh->next, ehci->qtdaddr, ehci->pid,
                ehci->usb_packet.len, ehci->tbytes, endp, ret);

        if (ret != USB_RET_NODEV) {
            break;
        }
    }

    if (ret > BUFF_SIZE) {
        fprintf(stderr, "ret from usb_handle_packet > BUFF_SIZE\n");
        return USB_RET_PROCERR;
    }

    if (ret == USB_RET_ASYNC) {
        ehci->async_complete = 0;
    }

    return ret;
}

/*  4.7.2
 */

static int ehci_process_itd(EHCIState *ehci,
                            EHCIitd *itd)
{
    USBPort *port;
    USBDevice *dev;
    int ret;
    int i, j;
    int ptr;
    int pid;
    int pg;
    int len;
    int dir;
    int devadr;
    int endp;
    int maxpkt;

    dir =(itd->bufptr[1] & ITD_BUFPTR_DIRECTION);
    devadr = get_field(itd->bufptr[0], ITD_BUFPTR_DEVADDR);
    endp = get_field(itd->bufptr[0], ITD_BUFPTR_EP);
    maxpkt = get_field(itd->bufptr[1], ITD_BUFPTR_MAXPKT);

    for(i = 0; i < 8; i++) {
        if (itd->transact[i] & ITD_XACT_ACTIVE) {
            DPRINTF("ISOCHRONOUS active for frame %d, interval %d\n",
                    ehci->frindex >> 3, i);

            pg = get_field(itd->transact[i], ITD_XACT_PGSEL);
            ptr = (itd->bufptr[pg] & ITD_BUFPTR_MASK) |
                (itd->transact[i] & ITD_XACT_OFFSET_MASK);
            len = get_field(itd->transact[i], ITD_XACT_LENGTH);

            if (len > BUFF_SIZE) {
                return USB_RET_PROCERR;
            }

            DPRINTF("ISOCH: buffer %08X len %d\n", ptr, len);

            if (!dir) {
                cpu_physical_memory_rw(ptr, &ehci->buffer[0], len, 0);
                pid = USB_TOKEN_OUT;
            } else
                pid = USB_TOKEN_IN;

            ret = USB_RET_NODEV;

            for (j = 0; j < NB_PORTS; j++) {
                port = &ehci->ports[j];
                dev = port->dev;

                // TODO sometime we will also need to check if we are the port owner

                if (!(ehci->portsc[j] &(PORTSC_CONNECT))) {
                    DPRINTF("Port %d, no exec, not connected(%08X)\n",
                            j, ehci->portsc[j]);
                    continue;
                }

                ehci->usb_packet.pid = ehci->pid;
                ehci->usb_packet.devaddr = devadr;
                ehci->usb_packet.devep = endp;
                ehci->usb_packet.data = ehci->buffer;
                ehci->usb_packet.len = len;

                DPRINTF("calling usb_handle_packet\n");
                ret = usb_handle_packet(dev, &ehci->usb_packet);

                if (ret != USB_RET_NODEV) {
                    break;
                }
            }

            /*  In isoch, there is no facility to indicate a NAK so let's
             *  instead just complete a zero-byte transaction.  Setting
             *  DBERR seems too draconian.
             */

            if (ret == USB_RET_NAK) {
                if (ehci->isoch_pause > 0) {
                    DPRINTF("ISOCH: received a NAK but paused so returning\n");
                    ehci->isoch_pause--;
                    return 0;
                } else if (ehci->isoch_pause == -1) {
                    DPRINTF("ISOCH: recv NAK & isoch pause inactive, setting\n");
                    // Pause frindex for up to 50 msec waiting for data from
                    // remote
                    ehci->isoch_pause = 50;
                    return 0;
                } else {
                    DPRINTF("ISOCH: isoch pause timeout! return 0\n");
                    ret = 0;
                }
            } else {
                DPRINTF("ISOCH: received ACK, clearing pause\n");
                ehci->isoch_pause = -1;
            }

            if (ret >= 0) {
                itd->transact[i] &= ~ITD_XACT_ACTIVE;

                if (itd->transact[i] & ITD_XACT_IOC) {
                    ehci_record_interrupt(ehci, USBSTS_INT);
                }
            }

            if (ret >= 0 && dir) {
                cpu_physical_memory_rw(ptr, &ehci->buffer[0], len, 1);

                if (ret != len) {
                    DPRINTF("ISOCH IN expected %d, got %d\n",
                            len, ret);
                    set_field(&itd->transact[i], ret, ITD_XACT_LENGTH);
                }
            }
        }
    }
    return 0;
}

/*  This state is the entry point for asynchronous schedule
 *  processing.  Entry here consitutes a EHCI start event state (4.8.5)
 */
static int ehci_state_waitlisthead(EHCIState *ehci,  int async, int *state)
{
    EHCIqh *qh = &ehci->qh;
    int i = 0;
    int again = 0;
    uint32_t entry = ehci->asynclistaddr;

    /* set reclamation flag at start event (4.8.6) */
    if (async) {
        ehci->usbsts |= USBSTS_REC;
    }

    /*  Find the head of the list (4.9.1.1) */
    for(i = 0; i < MAX_QH; i++) {
        get_dwords(NLPTR_GET(entry), (uint32_t *) qh, sizeof(EHCIqh) >> 2);

        if (qh->epchar & QH_EPCHAR_H) {
            DPRINTF_ST("WAITLISTHEAD: QH %08X is the HEAD of the list\n",
                       entry);
            if (async) {
                entry |= (NLPTR_TYPE_QH << 1);
            }

            ehci->fetch_addr = entry;
            *state = EST_FETCHENTRY;
            again = 1;
            goto out;
        }

        DPRINTF_ST("WAITLISTHEAD: QH %08X is NOT the HEAD of the list\n",
                   entry);
        entry = qh->next;
        if (entry == ehci->asynclistaddr) {
            DPRINTF("WAITLISTHEAD: reached beginning of QH list\n");
            break;
        }
    }

    /* no head found for list. */

    *state = EST_ACTIVE;

out:
    return again;
}


/*  This state is the entry point for periodic schedule processing as
 *  well as being a continuation state for async processing.
 */
static int ehci_state_fetchentry(EHCIState *ehci, int async, int *state)
{
    int again = 0;
    uint32_t entry = ehci->fetch_addr;

#if EHCI_DEBUG == 0
    if (qemu_get_clock_ns(vm_clock) / 1000 >= ehci->frame_end_usec) {
        if (async) {
            DPRINTF("FETCHENTRY: FRAME timer elapsed, exit state machine\n");
            goto out;
        } else {
            DPRINTF("FETCHENTRY: WARNING "
                    "- frame timer elapsed during periodic\n");
        }
    }
#endif
    if (entry < 0x1000) {
        DPRINTF("fetchentry: entry invalid (0x%08x)\n", entry);
        *state = EST_ACTIVE;
        goto out;
    }

    /* section 4.8, only QH in async schedule */
    if (async && (NLPTR_TYPE_GET(entry) != NLPTR_TYPE_QH)) {
        fprintf(stderr, "non queue head request in async schedule\n");
        return -1;
    }

    switch (NLPTR_TYPE_GET(entry)) {
    case NLPTR_TYPE_QH:
        DPRINTF_ST("FETCHENTRY: entry %X is a Queue Head\n", entry);
        *state = EST_FETCHQH;
        ehci->qhaddr = entry;
        again = 1;
        break;

    case NLPTR_TYPE_ITD:
        DPRINTF_ST("FETCHENTRY: entry %X is an ITD\n", entry);
        *state = EST_FETCHITD;
        ehci->itdaddr = entry;
        again = 1;
        break;

    default:
        // TODO: handle siTD and FSTN types
        fprintf(stderr, "FETCHENTRY: entry at %X is of type %d "
                "which is not supported yet\n", entry, NLPTR_TYPE_GET(entry));
        return -1;
    }

out:
    return again;
}

static int ehci_state_fetchqh(EHCIState *ehci, int async, int *state)
{
    EHCIqh *qh = &ehci->qh;
    int reload;
    int again = 0;

    get_dwords(NLPTR_GET(ehci->qhaddr), (uint32_t *) qh, sizeof(EHCIqh) >> 2);

    if (async && (qh->epchar & QH_EPCHAR_H)) {

        /*  EHCI spec version 1.0 Section 4.8.3 & 4.10.1 */
        if (ehci->usbsts & USBSTS_REC) {
            ehci->usbsts &= ~USBSTS_REC;
        } else {
            DPRINTF("FETCHQH:  QH 0x%08x. H-bit set, reclamation status reset"
                       " - done processing\n", ehci->qhaddr);
            *state = EST_ACTIVE;
            goto out;
        }
    }

#if EHCI_DEBUG
    if (ehci->qhaddr != qh->next) {
    DPRINTF("FETCHQH:  QH 0x%08x (h %x halt %x active %x) next 0x%08x\n",
               ehci->qhaddr,
               qh->epchar & QH_EPCHAR_H,
               qh->token & QTD_TOKEN_HALT,
               qh->token & QTD_TOKEN_ACTIVE,
               qh->next);
    }
#endif

    reload = get_field(qh->epchar, QH_EPCHAR_RL);
    if (reload) {
        DPRINTF_ST("FETCHQH: reloading nakcnt to %d\n", reload);
        set_field(&qh->altnext_qtd, reload, QH_ALTNEXT_NAKCNT);
    }

    if (qh->token & QTD_TOKEN_HALT) {
        DPRINTF_ST("FETCHQH: QH Halted, go horizontal\n");
        *state = EST_HORIZONTALQH;
        again = 1;

    } else if ((qh->token & QTD_TOKEN_ACTIVE) && (qh->current_qtd > 0x1000)) {
        DPRINTF_ST("FETCHQH: Active, !Halt, execute - fetch qTD\n");
        ehci->qtdaddr = qh->current_qtd;
        *state = EST_FETCHQTD;
        again = 1;

    } else {
        /*  EHCI spec version 1.0 Section 4.10.2 */
        DPRINTF_ST("FETCHQH: !Active, !Halt, advance queue\n");
        *state = EST_ADVANCEQUEUE;
        again = 1;
    }

out:
    return again;
}

static int ehci_state_fetchitd(EHCIState *ehci, int async, int *state)
{
    EHCIitd itd;

    get_dwords(NLPTR_GET(ehci->itdaddr),(uint32_t *) &itd,
               sizeof(EHCIitd) >> 2);
    DPRINTF_ST("FETCHITD: Fetched ITD at address %08X " "(next is %08X)\n",
               ehci->itdaddr, itd.next);

    if (ehci_process_itd(ehci, &itd) != 0) {
        return -1;
    }

    put_dwords(NLPTR_GET(ehci->itdaddr), (uint32_t *) &itd,
                sizeof(EHCIitd) >> 2);
    ehci->fetch_addr = itd.next;
    *state = EST_FETCHENTRY;

    return 1;
}

/* Section 4.10.2 - paragraph 3 */
static int ehci_state_advqueue(EHCIState *ehci, int async, int *state)
{
#if 0
    /* TO-DO: 4.10.2 - paragraph 2
     * if I-bit is set to 1 and QH is not active
     * go to horizontal QH
     */
    if (I-bit set) {
        *state = EST_HORIZONTALQH;
        goto out;
    }
#endif

    /*
     * want data and alt-next qTD is valid
     */
    if (((ehci->qh.token & QTD_TOKEN_TBYTES_MASK) != 0) &&
        (ehci->qh.altnext_qtd > 0x1000) &&
        (NLPTR_TBIT(ehci->qh.altnext_qtd) == 0)) {
        DPRINTF_ST("ADVQUEUE: goto alt next qTD. "
                   "curr 0x%08x next 0x%08x alt 0x%08x (next qh %x)\n",
                   ehci->qh.current_qtd, ehci->qh.altnext_qtd,
                   ehci->qh.next_qtd, ehci->qh.next);
        ehci->qtdaddr = ehci->qh.altnext_qtd;
        *state = EST_FETCHQTD;

    /*
     *  next qTD is valid
     */
    } else if ((ehci->qh.next_qtd > 0x1000) &&
               (NLPTR_TBIT(ehci->qh.next_qtd) == 0)) {
        DPRINTF_ST("ADVQUEUE: next qTD. "
                   "curr 0x%08x next 0x%08x alt 0x%08x (next qh %x)\n",
                   ehci->qh.current_qtd, ehci->qh.altnext_qtd,
                   ehci->qh.next_qtd, ehci->qh.next);
        ehci->qtdaddr = ehci->qh.next_qtd;
        *state = EST_FETCHQTD;

    /*
     *  no valid qTD, try next QH
     */
    } else {
        DPRINTF_ST("ADVQUEUE: go to horizontal QH\n");
        *state = EST_HORIZONTALQH;
    }

    return 1;
}

/* Section 4.10.2 - paragraph 4 */
static int ehci_state_fetchqtd(EHCIState *ehci, int async, int *state)
{
    EHCIqtd *qtd = &ehci->qtd;
    int again = 0;

    get_dwords(NLPTR_GET(ehci->qtdaddr),(uint32_t *) qtd, sizeof(EHCIqtd) >> 2);

    if (qtd->token & QTD_TOKEN_ACTIVE) {
        *state = EST_EXECUTE;
        again = 1;
    } else {
        *state = EST_HORIZONTALQH;
        again = 1;
    }

    return again;
}

static int ehci_state_horizqh(EHCIState *ehci, int async, int *state)
{
    int again = 0;

    if (ehci->fetch_addr != ehci->qh.next) {
        ehci->fetch_addr = ehci->qh.next;
        *state = EST_FETCHENTRY;
        again = 1;
    } else {
        *state = EST_ACTIVE;
    }

    return again;
}

static int ehci_state_execute(EHCIState *ehci, int async, int *state)
{
    EHCIqh *qh = &ehci->qh;
    EHCIqtd *qtd = &ehci->qtd;
    int again = 0;
    int reload, nakcnt;
    int smask;

    if (async) {
        DPRINTF_ST(">>>>> ASYNC STATE MACHINE execute QH 0x%08x, QTD 0x%08x\n",
                  ehci->qhaddr, ehci->qtdaddr);
    } else {
        DPRINTF_ST(">>>>> PERIODIC STATE MACHINE execute\n");
    }

    if (ehci_qh_do_overlay(ehci, qh, qtd) != 0) {
        return -1;
    }

    smask = get_field(qh->epcap, QH_EPCAP_SMASK);

    if (!smask) {
        reload = get_field(qh->epchar, QH_EPCHAR_RL);
        nakcnt = get_field(qh->altnext_qtd, QH_ALTNEXT_NAKCNT);
        if (reload && !nakcnt) {
            DPRINTF_ST("EXECUTE: RL != 0 but NakCnt == 0 -- no execute\n");
            *state = EST_HORIZONTALQH;
            again = 1;
            goto out;
        }
    }

    // TODO verify enough time remains in the uframe as in 4.4.1.1
    // TODO write back ptr to async list when done or out of time
    // TODO Windows does not seem to ever set the MULT field

    if (!async) {
        int transactCtr = get_field(qh->epcap, QH_EPCAP_MULT);
        if (!transactCtr) {
            DPRINTF("ZERO transactctr for int qh, go HORIZ\n");
            *state = EST_HORIZONTALQH;
            again = 1;
            goto out;
        }
    }

    if (async) {
        ehci->usbsts |= USBSTS_REC;
    }

    ehci->exec_status = ehci_execute(ehci, qh);
    if (ehci->exec_status == USB_RET_PROCERR) {
        again = -1;
        goto out;
    }
    *state = EST_EXECUTING;

    if (ehci->exec_status != USB_RET_ASYNC) {
        again = 1;
    }

out:
    return again;
}

static int ehci_state_executing(EHCIState *ehci, int async, int *state)
{
    EHCIqh *qh = &ehci->qh;
    int again = 0;
    int reload, nakcnt;

    ehci->exec_status = ehci_execute_complete(ehci, qh, ehci->exec_status);
    if (ehci->exec_status == USB_RET_ASYNC) {
        goto out;
    }
    if (ehci->exec_status == USB_RET_PROCERR) {
        again = -1;
        goto out;
    }

    // 4.10.3
    if (!async) {
        int transactCtr = get_field(qh->epcap, QH_EPCAP_MULT);
        transactCtr--;
        set_field(&qh->epcap, transactCtr, QH_EPCAP_MULT);
        // 4.10.3, bottom of page 82, should exit this state when transaction
        // counter decrements to 0
    }


    reload = get_field(qh->epchar, QH_EPCHAR_RL);
    if (reload) {
        nakcnt = get_field(qh->altnext_qtd, QH_ALTNEXT_NAKCNT);
        if (ehci->exec_status == USB_RET_NAK) {
            if (nakcnt) {
                nakcnt--;
            }
            DPRINTF_ST("EXECUTING: Nak occured and RL != 0, dec NakCnt to %d\n",
                    nakcnt);
        } else {
            nakcnt = reload;
            DPRINTF_ST("EXECUTING: Nak didn't occur, reloading to %d\n",
                       nakcnt);
        }
        set_field(&qh->altnext_qtd, nakcnt, QH_ALTNEXT_NAKCNT);
    }

    /*
     *  Write the qh back to guest physical memory.  This step isn't
     *  in the EHCI spec but we need to do it since we don't share
     *  physical memory with our guest VM.
     */

    DPRINTF("EXECUTING: write QH to VM memory: qhaddr 0x%x, next 0x%x\n",
              ehci->qhaddr, qh->next);
    put_dwords(NLPTR_GET(ehci->qhaddr), (uint32_t *) qh, sizeof(EHCIqh) >> 2);

    /* 4.10.5 */
    if ((ehci->exec_status == USB_RET_NAK) || (qh->token & QTD_TOKEN_ACTIVE)) {
        *state = EST_HORIZONTALQH;
    } else {
        *state = EST_WRITEBACK;
    }

    again = 1;

out:
    return again;
}


static int ehci_state_writeback(EHCIState *ehci, int async, int *state)
{
    EHCIqh *qh = &ehci->qh;
    int again = 0;

    /*  Write back the QTD from the QH area */
    DPRINTF_ST("WRITEBACK: write QTD to VM memory\n");
    put_dwords(NLPTR_GET(ehci->qtdaddr),(uint32_t *) &qh->next_qtd,
                sizeof(EHCIqtd) >> 2);

    /* TODO confirm next state.  For now, keep going if async
     * but stop after one qtd if periodic
     */
    //if (async) {
        *state = EST_ADVANCEQUEUE;
        again = 1;
    //} else {
    //    *state = EST_ACTIVE;
    //}
    return again;
}

/*
 * This is the state machine that is common to both async and periodic
 */

static int ehci_advance_state(EHCIState *ehci,
                              int async,
                              int state)
{
    int again;
    int iter = 0;

    do {
        if (state == EST_FETCHQH) {
            iter++;
            /* if we are roaming a lot of QH without executing a qTD
             * something is wrong with the linked list. TO-DO: why is
             * this hack needed?
             */
            if (iter > MAX_ITERATIONS) {
                DPRINTF("\n*** advance_state: bailing on MAX ITERATIONS***\n");
                state = EST_ACTIVE;
                break;
            }
        }
        switch(state) {
        case EST_WAITLISTHEAD:
            again = ehci_state_waitlisthead(ehci, async, &state);
            break;

        case EST_FETCHENTRY:
            again = ehci_state_fetchentry(ehci, async, &state);
            break;

        case EST_FETCHQH:
            again = ehci_state_fetchqh(ehci, async, &state);
            break;

        case EST_FETCHITD:
            again = ehci_state_fetchitd(ehci, async, &state);
            break;

        case EST_ADVANCEQUEUE:
            again = ehci_state_advqueue(ehci, async, &state);
            break;

        case EST_FETCHQTD:
            again = ehci_state_fetchqtd(ehci, async, &state);
            break;

        case EST_HORIZONTALQH:
            again = ehci_state_horizqh(ehci, async, &state);
            break;

        case EST_EXECUTE:
            iter = 0;
            again = ehci_state_execute(ehci, async, &state);
            break;

        case EST_EXECUTING:
            again = ehci_state_executing(ehci, async, &state);
            break;

        case EST_WRITEBACK:
            again = ehci_state_writeback(ehci, async, &state);
            break;

        default:
            fprintf(stderr, "Bad state!\n");
            again = -1;
            break;
        }

        if (again < 0) {
            fprintf(stderr, "processing error - resetting ehci HC\n");
            ehci_reset(ehci);
            again = 0;
        }
    }
    while (again);

    ehci_commit_interrupt(ehci);
    return state;
}

static void ehci_advance_async_state(EHCIState *ehci)
{
    EHCIqh qh;
    int state = ehci->astate;

    switch(state) {
    case EST_INACTIVE:
        if (!(ehci->usbcmd & USBCMD_ASE)) {
            break;
        }
        ehci->usbsts |= USBSTS_ASS;
        ehci->astate = EST_ACTIVE;
        // No break, fall through to ACTIVE

    case EST_ACTIVE:
        if ( !(ehci->usbcmd & USBCMD_ASE)) {
            ehci->usbsts &= ~USBSTS_ASS;
            ehci->astate = EST_INACTIVE;
            break;
        }

        /* If the doorbell is set, the guest wants to make a change to the
         * schedule. The host controller needs to release cached data.
         * (section 4.8.2)
         */
        if (ehci->usbcmd & USBCMD_IAAD) {
            DPRINTF("ASYNC: doorbell request acknowledged\n");
            ehci->usbcmd &= ~USBCMD_IAAD;
            ehci_set_interrupt(ehci, USBSTS_IAA);
            break;
        }

        /* make sure guest has acknowledged */
        /* TO-DO: is this really needed? */
        if (ehci->usbsts & USBSTS_IAA) {
            DPRINTF("IAA status bit still set.\n");
            break;
        }

        DPRINTF_ST("ASYNC: waiting for listhead, starting at %08x\n",
                ehci->asynclistaddr);
        /* check that address register has been set */
        if (ehci->asynclistaddr == 0) {
            break;
        }

        state = EST_WAITLISTHEAD;
        /* fall through */

    case EST_FETCHENTRY:
        /* fall through */

    case EST_EXECUTING:
        get_dwords(NLPTR_GET(ehci->qhaddr), (uint32_t *) &qh,
                   sizeof(EHCIqh) >> 2);
        ehci->astate = ehci_advance_state(ehci, 1, state);
        break;

    default:
        /* this should only be due to a developer mistake */
        fprintf(stderr, "ehci: Bad asynchronous state %d. "
                "Resetting to active\n", ehci->astate);
        ehci->astate = EST_ACTIVE;
    }
}

static void ehci_advance_periodic_state(EHCIState *ehci)
{
    uint32_t entry;
    uint32_t list;

    // 4.6

    switch(ehci->pstate) {
    case EST_INACTIVE:
        if ( !(ehci->frindex & 7) && (ehci->usbcmd & USBCMD_PSE)) {
            DPRINTF("PERIODIC going active\n");
            ehci->usbsts |= USBSTS_PSS;
            ehci->pstate = EST_ACTIVE;
            // No break, fall through to ACTIVE
        } else
            break;

    case EST_ACTIVE:
        if ( !(ehci->frindex & 7) && !(ehci->usbcmd & USBCMD_PSE)) {
            DPRINTF("PERIODIC going inactive\n");
            ehci->usbsts &= ~USBSTS_PSS;
            ehci->pstate = EST_INACTIVE;
            break;
        }

        list = ehci->periodiclistbase & 0xfffff000;
        /* check that register has been set */
        if (list == 0) {
            break;
        }
        list |= ((ehci->frindex & 0x1ff8) >> 1);

        cpu_physical_memory_rw(list, (uint8_t *) &entry, sizeof entry, 0);
        entry = le32_to_cpu(entry);

        DPRINTF("PERIODIC state adv fr=%d.  [%08X] -> %08X\n",
                ehci->frindex / 8, list, entry);
        ehci->fetch_addr = entry;
        ehci->pstate = ehci_advance_state(ehci, 0, EST_FETCHENTRY);
        break;

    case EST_EXECUTING:
        DPRINTF("PERIODIC state adv for executing\n");
        ehci->pstate = ehci_advance_state(ehci, 0, EST_EXECUTING);
        break;

    default:
        /* this should only be due to a developer mistake */
        fprintf(stderr, "ehci: Bad periodic state %d. "
                "Resetting to active\n", ehci->pstate);
        ehci->pstate = EST_ACTIVE;
    }
}

static void ehci_frame_timer(void *opaque)
{
    EHCIState *ehci = opaque;
    int64_t expire_time, t_now;
    int usec_elapsed;
    int frames;
    int usec_now;
    int i;
    int skipped_frames = 0;


    t_now = qemu_get_clock_ns(vm_clock);
    expire_time = t_now + (get_ticks_per_sec() / FRAME_TIMER_FREQ);
    if (expire_time == t_now) {
        expire_time++;
    }

    usec_now = t_now / 1000;
    usec_elapsed = usec_now - ehci->last_run_usec;
    frames = usec_elapsed / FRAME_TIMER_USEC;
    ehci->frame_end_usec = usec_now + FRAME_TIMER_USEC - 10;

    for (i = 0; i < frames; i++) {
        if ( !(ehci->usbsts & USBSTS_HALT)) {
            if (ehci->isoch_pause <= 0) {
                ehci->frindex += 8;
            }

            if (ehci->frindex > 0x00001fff) {
                ehci->frindex = 0;
                ehci_set_interrupt(ehci, USBSTS_FLR);
            }

            ehci->sofv = (ehci->frindex - 1) >> 3;
            ehci->sofv &= 0x000003ff;
        }

        if (frames - i > 10) {
            skipped_frames++;
        } else {
            // TODO could this cause periodic frames to get skipped if async
            // active?
            if (ehci->astate != EST_EXECUTING) {
                ehci_advance_periodic_state(ehci);
            }
        }

        ehci->last_run_usec += FRAME_TIMER_USEC;
    }

#if 0
    if (skipped_frames) {
        DPRINTF("WARNING - EHCI skipped %d frames\n", skipped_frames);
    }
#endif

    /*  Async is not inside loop since it executes everything it can once
     *  called
     */
    if (ehci->pstate != EST_EXECUTING) {
        ehci_advance_async_state(ehci);
    }

    qemu_mod_timer(ehci->frame_timer, expire_time);
}

static CPUReadMemoryFunc *ehci_readfn[3]={
    ehci_mem_readb,
    ehci_mem_readw,
    ehci_mem_readl
};

static CPUWriteMemoryFunc *ehci_writefn[3]={
    ehci_mem_writeb,
    ehci_mem_writew,
    ehci_mem_writel
};

static void ehci_map(PCIDevice *pci_dev, int region_num,
                     pcibus_t addr, pcibus_t size, int type)
{
    EHCIState *s =(EHCIState *)pci_dev;

    DPRINTF("ehci_map: region %d, addr %08" PRIx64 ", size %" PRId64 ", s->mem %08X\n",
            region_num, addr, size, s->mem);
    s->mem_base = addr;
    cpu_register_physical_memory(addr, size, s->mem);
}

static int usb_ehci_initfn(PCIDevice *dev);

static USBPortOps ehci_port_ops = {
    .attach = ehci_attach,
    .detach = ehci_detach,
    .complete = ehci_async_complete_packet,
};

static PCIDeviceInfo ehci_info = {
    .qdev.name    = "usb-ehci",
    .qdev.size    = sizeof(EHCIState),
    .init         = usb_ehci_initfn,
};

static int usb_ehci_initfn(PCIDevice *dev)
{
    EHCIState *s = DO_UPCAST(EHCIState, dev, dev);
    uint8_t *pci_conf = s->dev.config;
    int i;

    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82801D);
    pci_set_byte(&pci_conf[PCI_REVISION_ID], 0x10);
    pci_set_byte(&pci_conf[PCI_CLASS_PROG], 0x20);
    pci_config_set_class(pci_conf, PCI_CLASS_SERIAL_USB);
    pci_set_byte(&pci_conf[PCI_HEADER_TYPE], PCI_HEADER_TYPE_NORMAL);

    /* capabilities pointer */
    pci_set_byte(&pci_conf[PCI_CAPABILITY_LIST], 0x00);
    //pci_set_byte(&pci_conf[PCI_CAPABILITY_LIST], 0x50);

    pci_set_byte(&pci_conf[PCI_INTERRUPT_PIN], 4); // interrupt pin 3
    pci_set_byte(&pci_conf[PCI_MIN_GNT], 0);
    pci_set_byte(&pci_conf[PCI_MAX_LAT], 0);

    // pci_conf[0x50] = 0x01; // power management caps

    pci_set_byte(&pci_conf[0x60], 0x20);  // spec release number (2.1.4)
    pci_set_byte(&pci_conf[0x61], 0x20);  // frame length adjustment (2.1.5)
    pci_set_word(&pci_conf[0x62], 0x00);  // port wake up capability (2.1.6)

    pci_conf[0x64] = 0x00;
    pci_conf[0x65] = 0x00;
    pci_conf[0x66] = 0x00;
    pci_conf[0x67] = 0x00;
    pci_conf[0x68] = 0x01;
    pci_conf[0x69] = 0x00;
    pci_conf[0x6a] = 0x00;
    pci_conf[0x6b] = 0x00;  // USBLEGSUP
    pci_conf[0x6c] = 0x00;
    pci_conf[0x6d] = 0x00;
    pci_conf[0x6e] = 0x00;
    pci_conf[0x6f] = 0xc0;  // USBLEFCTLSTS

    // 2.2 host controller interface version
    s->mmio[0x00] = (uint8_t) OPREGBASE;
    s->mmio[0x01] = 0x00;
    s->mmio[0x02] = 0x00;
    s->mmio[0x03] = 0x01;        // HC version
    s->mmio[0x04] = NB_PORTS;    // Number of downstream ports
    s->mmio[0x05] = 0x00;        // No companion ports at present
    s->mmio[0x06] = 0x00;
    s->mmio[0x07] = 0x00;
    s->mmio[0x08] = 0x80;        // We can cache whole frame, not 64-bit capable
    s->mmio[0x09] = 0x68;        // EECP
    s->mmio[0x0a] = 0x00;
    s->mmio[0x0b] = 0x00;

    s->irq = s->dev.irq[3];

    usb_bus_new(&s->bus, &s->dev.qdev);
    for(i = 0; i < NB_PORTS; i++) {
        usb_register_port(&s->bus, &s->ports[i], s, i, &ehci_port_ops,
                          USB_SPEED_MASK_HIGH);
        usb_port_location(&s->ports[i], NULL, i+1);
        s->ports[i].dev = 0;
    }

    s->frame_timer = qemu_new_timer_ns(vm_clock, ehci_frame_timer, s);

    qemu_register_reset(ehci_reset, s);

    s->mem = cpu_register_io_memory(ehci_readfn, ehci_writefn, s,
                                    DEVICE_LITTLE_ENDIAN);

    pci_register_bar(&s->dev, 0, MMIO_SIZE, PCI_BASE_ADDRESS_SPACE_MEMORY,
                                                            ehci_map);

    fprintf(stderr, "*** EHCI support is under development ***\n");

    return 0;
}

static void ehci_register(void)
{
    pci_qdev_register(&ehci_info);
}
device_init(ehci_register);

/*
 * vim: expandtab ts=4
 */
