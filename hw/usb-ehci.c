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
 */

#include "hw.h"
#include "qemu-timer.h"
#include "usb.h"
#include "pci.h"
#include "monitor.h"
#include "trace.h"
#include "dma.h"

#define EHCI_DEBUG   0

#if EHCI_DEBUG
#define DPRINTF printf
#else
#define DPRINTF(...)
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
 * Bits that are reserved or are read-only are masked out of values
 * written to us by software
 */
#define PORTSC_RO_MASK       0x007001c0
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
#define FRAME_TIMER_NS   (1000000000 / FRAME_TIMER_FREQ)

#define NB_MAXINTRATE    8        // Max rate at which controller issues ints
#define NB_PORTS         6        // Number of downstream ports
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
    EST_FETCHSITD,
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
#define ITD_BUFPTR_MULT_SH       0
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
#define QTD_BUFPTR_SH                 12
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

typedef struct EHCIQueue EHCIQueue;
typedef struct EHCIState EHCIState;

enum async_state {
    EHCI_ASYNC_NONE = 0,
    EHCI_ASYNC_INFLIGHT,
    EHCI_ASYNC_FINISHED,
};

struct EHCIQueue {
    EHCIState *ehci;
    QTAILQ_ENTRY(EHCIQueue) next;
    bool async_schedule;
    uint32_t seen;
    uint64_t ts;

    /* cached data from guest - needs to be flushed
     * when guest removes an entry (doorbell, handshake sequence)
     */
    EHCIqh qh;             // copy of current QH (being worked on)
    uint32_t qhaddr;       // address QH read from
    EHCIqtd qtd;           // copy of current QTD (being worked on)
    uint32_t qtdaddr;      // address QTD read from

    USBPacket packet;
    QEMUSGList sgl;
    int pid;
    uint32_t tbytes;
    enum async_state async;
    int usb_status;
};

struct EHCIState {
    PCIDevice dev;
    USBBus bus;
    qemu_irq irq;
    MemoryRegion mem;
    int companion_count;

    /* properties */
    uint32_t freq;
    uint32_t maxframes;

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
    USBPort *companion_ports[NB_PORTS];
    uint32_t usbsts_pending;
    QTAILQ_HEAD(, EHCIQueue) queues;

    uint32_t a_fetch_addr;   // which address to look at next
    uint32_t p_fetch_addr;   // which address to look at next

    USBPacket ipacket;
    QEMUSGList isgl;
    int isoch_pause;

    uint64_t last_run_ns;
};

#define SET_LAST_RUN_CLOCK(s) \
    (s)->last_run_ns = qemu_get_clock_ns(vm_clock);

/* nifty macros from Arnon's EHCI version  */
#define get_field(data, field) \
    (((data) & field##_MASK) >> field##_SH)

#define set_field(data, newval, field) do { \
    uint32_t val = *data; \
    val &= ~ field##_MASK; \
    val |= ((newval) << field##_SH) & field##_MASK; \
    *data = val; \
    } while(0)

static const char *ehci_state_names[] = {
    [ EST_INACTIVE ]     = "INACTIVE",
    [ EST_ACTIVE ]       = "ACTIVE",
    [ EST_EXECUTING ]    = "EXECUTING",
    [ EST_SLEEPING ]     = "SLEEPING",
    [ EST_WAITLISTHEAD ] = "WAITLISTHEAD",
    [ EST_FETCHENTRY ]   = "FETCH ENTRY",
    [ EST_FETCHQH ]      = "FETCH QH",
    [ EST_FETCHITD ]     = "FETCH ITD",
    [ EST_ADVANCEQUEUE ] = "ADVANCEQUEUE",
    [ EST_FETCHQTD ]     = "FETCH QTD",
    [ EST_EXECUTE ]      = "EXECUTE",
    [ EST_WRITEBACK ]    = "WRITEBACK",
    [ EST_HORIZONTALQH ] = "HORIZONTALQH",
};

static const char *ehci_mmio_names[] = {
    [ CAPLENGTH ]        = "CAPLENGTH",
    [ HCIVERSION ]       = "HCIVERSION",
    [ HCSPARAMS ]        = "HCSPARAMS",
    [ HCCPARAMS ]        = "HCCPARAMS",
    [ USBCMD ]           = "USBCMD",
    [ USBSTS ]           = "USBSTS",
    [ USBINTR ]          = "USBINTR",
    [ FRINDEX ]          = "FRINDEX",
    [ PERIODICLISTBASE ] = "P-LIST BASE",
    [ ASYNCLISTADDR ]    = "A-LIST ADDR",
    [ PORTSC_BEGIN ]     = "PORTSC #0",
    [ PORTSC_BEGIN + 4]  = "PORTSC #1",
    [ PORTSC_BEGIN + 8]  = "PORTSC #2",
    [ PORTSC_BEGIN + 12] = "PORTSC #3",
    [ CONFIGFLAG ]       = "CONFIGFLAG",
};

static const char *nr2str(const char **n, size_t len, uint32_t nr)
{
    if (nr < len && n[nr] != NULL) {
        return n[nr];
    } else {
        return "unknown";
    }
}

static const char *state2str(uint32_t state)
{
    return nr2str(ehci_state_names, ARRAY_SIZE(ehci_state_names), state);
}

static const char *addr2str(target_phys_addr_t addr)
{
    return nr2str(ehci_mmio_names, ARRAY_SIZE(ehci_mmio_names), addr);
}

static void ehci_trace_usbsts(uint32_t mask, int state)
{
    /* interrupts */
    if (mask & USBSTS_INT) {
        trace_usb_ehci_usbsts("INT", state);
    }
    if (mask & USBSTS_ERRINT) {
        trace_usb_ehci_usbsts("ERRINT", state);
    }
    if (mask & USBSTS_PCD) {
        trace_usb_ehci_usbsts("PCD", state);
    }
    if (mask & USBSTS_FLR) {
        trace_usb_ehci_usbsts("FLR", state);
    }
    if (mask & USBSTS_HSE) {
        trace_usb_ehci_usbsts("HSE", state);
    }
    if (mask & USBSTS_IAA) {
        trace_usb_ehci_usbsts("IAA", state);
    }

    /* status */
    if (mask & USBSTS_HALT) {
        trace_usb_ehci_usbsts("HALT", state);
    }
    if (mask & USBSTS_REC) {
        trace_usb_ehci_usbsts("REC", state);
    }
    if (mask & USBSTS_PSS) {
        trace_usb_ehci_usbsts("PSS", state);
    }
    if (mask & USBSTS_ASS) {
        trace_usb_ehci_usbsts("ASS", state);
    }
}

static inline void ehci_set_usbsts(EHCIState *s, int mask)
{
    if ((s->usbsts & mask) == mask) {
        return;
    }
    ehci_trace_usbsts(mask, 1);
    s->usbsts |= mask;
}

static inline void ehci_clear_usbsts(EHCIState *s, int mask)
{
    if ((s->usbsts & mask) == 0) {
        return;
    }
    ehci_trace_usbsts(mask, 0);
    s->usbsts &= ~mask;
}

static inline void ehci_set_interrupt(EHCIState *s, int intr)
{
    int level = 0;

    // TODO honour interrupt threshold requests

    ehci_set_usbsts(s, intr);

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

static void ehci_set_state(EHCIState *s, int async, int state)
{
    if (async) {
        trace_usb_ehci_state("async", state2str(state));
        s->astate = state;
    } else {
        trace_usb_ehci_state("periodic", state2str(state));
        s->pstate = state;
    }
}

static int ehci_get_state(EHCIState *s, int async)
{
    return async ? s->astate : s->pstate;
}

static void ehci_set_fetch_addr(EHCIState *s, int async, uint32_t addr)
{
    if (async) {
        s->a_fetch_addr = addr;
    } else {
        s->p_fetch_addr = addr;
    }
}

static int ehci_get_fetch_addr(EHCIState *s, int async)
{
    return async ? s->a_fetch_addr : s->p_fetch_addr;
}

static void ehci_trace_qh(EHCIQueue *q, target_phys_addr_t addr, EHCIqh *qh)
{
    /* need three here due to argument count limits */
    trace_usb_ehci_qh_ptrs(q, addr, qh->next,
                           qh->current_qtd, qh->next_qtd, qh->altnext_qtd);
    trace_usb_ehci_qh_fields(addr,
                             get_field(qh->epchar, QH_EPCHAR_RL),
                             get_field(qh->epchar, QH_EPCHAR_MPLEN),
                             get_field(qh->epchar, QH_EPCHAR_EPS),
                             get_field(qh->epchar, QH_EPCHAR_EP),
                             get_field(qh->epchar, QH_EPCHAR_DEVADDR));
    trace_usb_ehci_qh_bits(addr,
                           (bool)(qh->epchar & QH_EPCHAR_C),
                           (bool)(qh->epchar & QH_EPCHAR_H),
                           (bool)(qh->epchar & QH_EPCHAR_DTC),
                           (bool)(qh->epchar & QH_EPCHAR_I));
}

static void ehci_trace_qtd(EHCIQueue *q, target_phys_addr_t addr, EHCIqtd *qtd)
{
    /* need three here due to argument count limits */
    trace_usb_ehci_qtd_ptrs(q, addr, qtd->next, qtd->altnext);
    trace_usb_ehci_qtd_fields(addr,
                              get_field(qtd->token, QTD_TOKEN_TBYTES),
                              get_field(qtd->token, QTD_TOKEN_CPAGE),
                              get_field(qtd->token, QTD_TOKEN_CERR),
                              get_field(qtd->token, QTD_TOKEN_PID));
    trace_usb_ehci_qtd_bits(addr,
                            (bool)(qtd->token & QTD_TOKEN_IOC),
                            (bool)(qtd->token & QTD_TOKEN_ACTIVE),
                            (bool)(qtd->token & QTD_TOKEN_HALT),
                            (bool)(qtd->token & QTD_TOKEN_BABBLE),
                            (bool)(qtd->token & QTD_TOKEN_XACTERR));
}

static void ehci_trace_itd(EHCIState *s, target_phys_addr_t addr, EHCIitd *itd)
{
    trace_usb_ehci_itd(addr, itd->next,
                       get_field(itd->bufptr[1], ITD_BUFPTR_MAXPKT),
                       get_field(itd->bufptr[2], ITD_BUFPTR_MULT),
                       get_field(itd->bufptr[0], ITD_BUFPTR_EP),
                       get_field(itd->bufptr[0], ITD_BUFPTR_DEVADDR));
}

static void ehci_trace_sitd(EHCIState *s, target_phys_addr_t addr,
                            EHCIsitd *sitd)
{
    trace_usb_ehci_sitd(addr, sitd->next,
                        (bool)(sitd->results & SITD_RESULTS_ACTIVE));
}

/* queue management */

static EHCIQueue *ehci_alloc_queue(EHCIState *ehci, int async)
{
    EHCIQueue *q;

    q = g_malloc0(sizeof(*q));
    q->ehci = ehci;
    q->async_schedule = async;
    QTAILQ_INSERT_HEAD(&ehci->queues, q, next);
    trace_usb_ehci_queue_action(q, "alloc");
    return q;
}

static void ehci_free_queue(EHCIQueue *q)
{
    trace_usb_ehci_queue_action(q, "free");
    if (q->async == EHCI_ASYNC_INFLIGHT) {
        usb_cancel_packet(&q->packet);
    }
    QTAILQ_REMOVE(&q->ehci->queues, q, next);
    g_free(q);
}

static EHCIQueue *ehci_find_queue_by_qh(EHCIState *ehci, uint32_t addr)
{
    EHCIQueue *q;

    QTAILQ_FOREACH(q, &ehci->queues, next) {
        if (addr == q->qhaddr) {
            return q;
        }
    }
    return NULL;
}

static void ehci_queues_rip_unused(EHCIState *ehci)
{
    EHCIQueue *q, *tmp;

    QTAILQ_FOREACH_SAFE(q, &ehci->queues, next, tmp) {
        if (q->seen) {
            q->seen = 0;
            q->ts = ehci->last_run_ns;
            continue;
        }
        if (ehci->last_run_ns < q->ts + 250000000) {
            /* allow 0.25 sec idle */
            continue;
        }
        ehci_free_queue(q);
    }
}

static void ehci_queues_rip_device(EHCIState *ehci, USBDevice *dev)
{
    EHCIQueue *q, *tmp;

    QTAILQ_FOREACH_SAFE(q, &ehci->queues, next, tmp) {
        if (q->packet.owner != dev) {
            continue;
        }
        ehci_free_queue(q);
    }
}

static void ehci_queues_rip_all(EHCIState *ehci)
{
    EHCIQueue *q, *tmp;

    QTAILQ_FOREACH_SAFE(q, &ehci->queues, next, tmp) {
        ehci_free_queue(q);
    }
}

/* Attach or detach a device on root hub */

static void ehci_attach(USBPort *port)
{
    EHCIState *s = port->opaque;
    uint32_t *portsc = &s->portsc[port->index];

    trace_usb_ehci_port_attach(port->index, port->dev->product_desc);

    if (*portsc & PORTSC_POWNER) {
        USBPort *companion = s->companion_ports[port->index];
        companion->dev = port->dev;
        companion->ops->attach(companion);
        return;
    }

    *portsc |= PORTSC_CONNECT;
    *portsc |= PORTSC_CSC;

    ehci_set_interrupt(s, USBSTS_PCD);
}

static void ehci_detach(USBPort *port)
{
    EHCIState *s = port->opaque;
    uint32_t *portsc = &s->portsc[port->index];

    trace_usb_ehci_port_detach(port->index);

    if (*portsc & PORTSC_POWNER) {
        USBPort *companion = s->companion_ports[port->index];
        companion->ops->detach(companion);
        companion->dev = NULL;
        return;
    }

    ehci_queues_rip_device(s, port->dev);

    *portsc &= ~(PORTSC_CONNECT|PORTSC_PED);
    *portsc |= PORTSC_CSC;

    ehci_set_interrupt(s, USBSTS_PCD);
}

static void ehci_child_detach(USBPort *port, USBDevice *child)
{
    EHCIState *s = port->opaque;
    uint32_t portsc = s->portsc[port->index];

    if (portsc & PORTSC_POWNER) {
        USBPort *companion = s->companion_ports[port->index];
        companion->ops->child_detach(companion, child);
        companion->dev = NULL;
        return;
    }

    ehci_queues_rip_device(s, child);
}

static void ehci_wakeup(USBPort *port)
{
    EHCIState *s = port->opaque;
    uint32_t portsc = s->portsc[port->index];

    if (portsc & PORTSC_POWNER) {
        USBPort *companion = s->companion_ports[port->index];
        if (companion->ops->wakeup) {
            companion->ops->wakeup(companion);
        }
    }
}

static int ehci_register_companion(USBBus *bus, USBPort *ports[],
                                   uint32_t portcount, uint32_t firstport)
{
    EHCIState *s = container_of(bus, EHCIState, bus);
    uint32_t i;

    if (firstport + portcount > NB_PORTS) {
        qerror_report(QERR_INVALID_PARAMETER_VALUE, "firstport",
                      "firstport on masterbus");
        error_printf_unless_qmp(
            "firstport value of %u makes companion take ports %u - %u, which "
            "is outside of the valid range of 0 - %u\n", firstport, firstport,
            firstport + portcount - 1, NB_PORTS - 1);
        return -1;
    }

    for (i = 0; i < portcount; i++) {
        if (s->companion_ports[firstport + i]) {
            qerror_report(QERR_INVALID_PARAMETER_VALUE, "masterbus",
                          "an USB masterbus");
            error_printf_unless_qmp(
                "port %u on masterbus %s already has a companion assigned\n",
                firstport + i, bus->qbus.name);
            return -1;
        }
    }

    for (i = 0; i < portcount; i++) {
        s->companion_ports[firstport + i] = ports[i];
        s->ports[firstport + i].speedmask |=
            USB_SPEED_MASK_LOW | USB_SPEED_MASK_FULL;
        /* Ensure devs attached before the initial reset go to the companion */
        s->portsc[firstport + i] = PORTSC_POWNER;
    }

    s->companion_count++;
    s->mmio[0x05] = (s->companion_count << 4) | portcount;

    return 0;
}

/* 4.1 host controller initialization */
static void ehci_reset(void *opaque)
{
    EHCIState *s = opaque;
    int i;
    USBDevice *devs[NB_PORTS];

    trace_usb_ehci_reset();

    /*
     * Do the detach before touching portsc, so that it correctly gets send to
     * us or to our companion based on PORTSC_POWNER before the reset.
     */
    for(i = 0; i < NB_PORTS; i++) {
        devs[i] = s->ports[i].dev;
        if (devs[i] && devs[i]->attached) {
            usb_detach(&s->ports[i]);
        }
    }

    memset(&s->mmio[OPREGBASE], 0x00, MMIO_SIZE - OPREGBASE);

    s->usbcmd = NB_MAXINTRATE << USBCMD_ITC_SH;
    s->usbsts = USBSTS_HALT;

    s->astate = EST_INACTIVE;
    s->pstate = EST_INACTIVE;
    s->isoch_pause = -1;
    s->attach_poll_counter = 0;

    for(i = 0; i < NB_PORTS; i++) {
        if (s->companion_ports[i]) {
            s->portsc[i] = PORTSC_POWNER | PORTSC_PPOWER;
        } else {
            s->portsc[i] = PORTSC_PPOWER;
        }
        if (devs[i] && devs[i]->attached) {
            usb_attach(&s->ports[i]);
        }
    }
    ehci_queues_rip_all(s);
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

    trace_usb_ehci_mmio_readl(addr, addr2str(addr), val);
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

static void handle_port_owner_write(EHCIState *s, int port, uint32_t owner)
{
    USBDevice *dev = s->ports[port].dev;
    uint32_t *portsc = &s->portsc[port];
    uint32_t orig;

    if (s->companion_ports[port] == NULL)
        return;

    owner = owner & PORTSC_POWNER;
    orig  = *portsc & PORTSC_POWNER;

    if (!(owner ^ orig)) {
        return;
    }

    if (dev && dev->attached) {
        usb_detach(&s->ports[port]);
    }

    *portsc &= ~PORTSC_POWNER;
    *portsc |= owner;

    if (dev && dev->attached) {
        usb_attach(&s->ports[port]);
    }
}

static void handle_port_status_write(EHCIState *s, int port, uint32_t val)
{
    uint32_t *portsc = &s->portsc[port];
    USBDevice *dev = s->ports[port].dev;

    /* Clear rwc bits */
    *portsc &= ~(val & PORTSC_RWC_MASK);
    /* The guest may clear, but not set the PED bit */
    *portsc &= val | ~PORTSC_PED;
    /* POWNER is masked out by RO_MASK as it is RO when we've no companion */
    handle_port_owner_write(s, port, val);
    /* And finally apply RO_MASK */
    val &= PORTSC_RO_MASK;

    if ((val & PORTSC_PRESET) && !(*portsc & PORTSC_PRESET)) {
        trace_usb_ehci_port_reset(port, 1);
    }

    if (!(val & PORTSC_PRESET) &&(*portsc & PORTSC_PRESET)) {
        trace_usb_ehci_port_reset(port, 0);
        if (dev && dev->attached) {
            usb_attach(&s->ports[port]);
            usb_send_msg(dev, USB_MSG_RESET);
            *portsc &= ~PORTSC_CSC;
        }

        /*
         *  Table 2.16 Set the enable bit(and enable bit change) to indicate
         *  to SW that this port has a high speed device attached
         */
        if (dev && dev->attached && (dev->speedmask & USB_SPEED_MASK_HIGH)) {
            val |= PORTSC_PED;
        }
    }

    *portsc &= ~PORTSC_RO_MASK;
    *portsc |= val;
}

static void ehci_mem_writel(void *ptr, target_phys_addr_t addr, uint32_t val)
{
    EHCIState *s = ptr;
    uint32_t *mmio = (uint32_t *)(&s->mmio[addr]);
    uint32_t old = *mmio;
    int i;

    trace_usb_ehci_mmio_writel(addr, addr2str(addr), val);

    /* Only aligned reads are allowed on OHCI */
    if (addr & 3) {
        fprintf(stderr, "usb-ehci: Mis-aligned write to addr 0x"
                TARGET_FMT_plx "\n", addr);
        return;
    }

    if (addr >= PORTSC && addr < PORTSC + 4 * NB_PORTS) {
        handle_port_status_write(s, (addr-PORTSC)/4, val);
        trace_usb_ehci_mmio_change(addr, addr2str(addr), *mmio, old);
        return;
    }

    if (addr < OPREGBASE) {
        fprintf(stderr, "usb-ehci: write attempt to read-only register"
                TARGET_FMT_plx "\n", addr);
        return;
    }


    /* Do any register specific pre-write processing here.  */
    switch(addr) {
    case USBCMD:
        if ((val & USBCMD_RUNSTOP) && !(s->usbcmd & USBCMD_RUNSTOP)) {
            qemu_mod_timer(s->frame_timer, qemu_get_clock_ns(vm_clock));
            SET_LAST_RUN_CLOCK(s);
            ehci_clear_usbsts(s, USBSTS_HALT);
        }

        if (!(val & USBCMD_RUNSTOP) && (s->usbcmd & USBCMD_RUNSTOP)) {
            qemu_del_timer(s->frame_timer);
            // TODO - should finish out some stuff before setting halt
            ehci_set_usbsts(s, USBSTS_HALT);
        }

        if (val & USBCMD_HCRESET) {
            ehci_reset(s);
            val &= ~USBCMD_HCRESET;
        }

        /* not supporting dynamic frame list size at the moment */
        if ((val & USBCMD_FLS) && !(s->usbcmd & USBCMD_FLS)) {
            fprintf(stderr, "attempt to set frame list size -- value %d\n",
                    val & USBCMD_FLS);
            val &= ~USBCMD_FLS;
        }
        break;

    case USBSTS:
        val &= USBSTS_RO_MASK;              // bits 6 thru 31 are RO
        ehci_clear_usbsts(s, val);          // bits 0 thru 5 are R/WC
        val = s->usbsts;
        ehci_set_interrupt(s, 0);
        break;

    case USBINTR:
        val &= USBINTR_MASK;
        break;

    case FRINDEX:
        s->sofv = val >> 3;
        break;

    case CONFIGFLAG:
        val &= 0x1;
        if (val) {
            for(i = 0; i < NB_PORTS; i++)
                handle_port_owner_write(s, i, 0);
        }
        break;

    case PERIODICLISTBASE:
        if ((s->usbcmd & USBCMD_PSE) && (s->usbcmd & USBCMD_RUNSTOP)) {
            fprintf(stderr,
              "ehci: PERIODIC list base register set while periodic schedule\n"
              "      is enabled and HC is enabled\n");
        }
        break;

    case ASYNCLISTADDR:
        if ((s->usbcmd & USBCMD_ASE) && (s->usbcmd & USBCMD_RUNSTOP)) {
            fprintf(stderr,
              "ehci: ASYNC list address register set while async schedule\n"
              "      is enabled and HC is enabled\n");
        }
        break;
    }

    *mmio = val;
    trace_usb_ehci_mmio_change(addr, addr2str(addr), *mmio, old);
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

static int ehci_qh_do_overlay(EHCIQueue *q)
{
    int i;
    int dtoggle;
    int ping;
    int eps;
    int reload;

    // remember values in fields to preserve in qh after overlay

    dtoggle = q->qh.token & QTD_TOKEN_DTOGGLE;
    ping    = q->qh.token & QTD_TOKEN_PING;

    q->qh.current_qtd = q->qtdaddr;
    q->qh.next_qtd    = q->qtd.next;
    q->qh.altnext_qtd = q->qtd.altnext;
    q->qh.token       = q->qtd.token;


    eps = get_field(q->qh.epchar, QH_EPCHAR_EPS);
    if (eps == EHCI_QH_EPS_HIGH) {
        q->qh.token &= ~QTD_TOKEN_PING;
        q->qh.token |= ping;
    }

    reload = get_field(q->qh.epchar, QH_EPCHAR_RL);
    set_field(&q->qh.altnext_qtd, reload, QH_ALTNEXT_NAKCNT);

    for (i = 0; i < 5; i++) {
        q->qh.bufptr[i] = q->qtd.bufptr[i];
    }

    if (!(q->qh.epchar & QH_EPCHAR_DTC)) {
        // preserve QH DT bit
        q->qh.token &= ~QTD_TOKEN_DTOGGLE;
        q->qh.token |= dtoggle;
    }

    q->qh.bufptr[1] &= ~BUFPTR_CPROGMASK_MASK;
    q->qh.bufptr[2] &= ~BUFPTR_FRAMETAG_MASK;

    put_dwords(NLPTR_GET(q->qhaddr), (uint32_t *) &q->qh, sizeof(EHCIqh) >> 2);

    return 0;
}

static int ehci_init_transfer(EHCIQueue *q)
{
    uint32_t cpage, offset, bytes, plen;
    target_phys_addr_t page;

    cpage  = get_field(q->qh.token, QTD_TOKEN_CPAGE);
    bytes  = get_field(q->qh.token, QTD_TOKEN_TBYTES);
    offset = q->qh.bufptr[0] & ~QTD_BUFPTR_MASK;
    qemu_sglist_init(&q->sgl, 5);

    while (bytes > 0) {
        if (cpage > 4) {
            fprintf(stderr, "cpage out of range (%d)\n", cpage);
            return USB_RET_PROCERR;
        }

        page  = q->qh.bufptr[cpage] & QTD_BUFPTR_MASK;
        page += offset;
        plen  = bytes;
        if (plen > 4096 - offset) {
            plen = 4096 - offset;
            offset = 0;
            cpage++;
        }

        qemu_sglist_add(&q->sgl, page, plen);
        bytes -= plen;
    }
    return 0;
}

static void ehci_finish_transfer(EHCIQueue *q, int status)
{
    uint32_t cpage, offset;

    qemu_sglist_destroy(&q->sgl);

    if (status > 0) {
        /* update cpage & offset */
        cpage  = get_field(q->qh.token, QTD_TOKEN_CPAGE);
        offset = q->qh.bufptr[0] & ~QTD_BUFPTR_MASK;

        offset += status;
        cpage  += offset >> QTD_BUFPTR_SH;
        offset &= ~QTD_BUFPTR_MASK;

        set_field(&q->qh.token, cpage, QTD_TOKEN_CPAGE);
        q->qh.bufptr[0] &= QTD_BUFPTR_MASK;
        q->qh.bufptr[0] |= offset;
    }
}

static void ehci_async_complete_packet(USBPort *port, USBPacket *packet)
{
    EHCIQueue *q;
    EHCIState *s = port->opaque;
    uint32_t portsc = s->portsc[port->index];

    if (portsc & PORTSC_POWNER) {
        USBPort *companion = s->companion_ports[port->index];
        companion->ops->complete(companion, packet);
        return;
    }

    q = container_of(packet, EHCIQueue, packet);
    trace_usb_ehci_queue_action(q, "wakeup");
    assert(q->async == EHCI_ASYNC_INFLIGHT);
    q->async = EHCI_ASYNC_FINISHED;
    q->usb_status = packet->result;
}

static void ehci_execute_complete(EHCIQueue *q)
{
    int c_err, reload;

    assert(q->async != EHCI_ASYNC_INFLIGHT);
    q->async = EHCI_ASYNC_NONE;

    DPRINTF("execute_complete: qhaddr 0x%x, next %x, qtdaddr 0x%x, status %d\n",
            q->qhaddr, q->qh.next, q->qtdaddr, q->usb_status);

    if (q->usb_status < 0) {
err:
        /* TO-DO: put this is in a function that can be invoked below as well */
        c_err = get_field(q->qh.token, QTD_TOKEN_CERR);
        c_err--;
        set_field(&q->qh.token, c_err, QTD_TOKEN_CERR);

        switch(q->usb_status) {
        case USB_RET_NODEV:
            q->qh.token |= (QTD_TOKEN_HALT | QTD_TOKEN_XACTERR);
            ehci_record_interrupt(q->ehci, USBSTS_ERRINT);
            break;
        case USB_RET_STALL:
            q->qh.token |= QTD_TOKEN_HALT;
            ehci_record_interrupt(q->ehci, USBSTS_ERRINT);
            break;
        case USB_RET_NAK:
            /* 4.10.3 */
            reload = get_field(q->qh.epchar, QH_EPCHAR_RL);
            if ((q->pid == USB_TOKEN_IN) && reload) {
                int nakcnt = get_field(q->qh.altnext_qtd, QH_ALTNEXT_NAKCNT);
                nakcnt--;
                set_field(&q->qh.altnext_qtd, nakcnt, QH_ALTNEXT_NAKCNT);
            } else if (!reload) {
                return;
            }
            break;
        case USB_RET_BABBLE:
            q->qh.token |= (QTD_TOKEN_HALT | QTD_TOKEN_BABBLE);
            ehci_record_interrupt(q->ehci, USBSTS_ERRINT);
            break;
        default:
            /* should not be triggerable */
            fprintf(stderr, "USB invalid response %d to handle\n", q->usb_status);
            assert(0);
            break;
        }
    } else {
        // DPRINTF("Short packet condition\n");
        // TODO check 4.12 for splits

        if ((q->usb_status > q->tbytes) && (q->pid == USB_TOKEN_IN)) {
            q->usb_status = USB_RET_BABBLE;
            goto err;
        }

        if (q->tbytes && q->pid == USB_TOKEN_IN) {
            q->tbytes -= q->usb_status;
        } else {
            q->tbytes = 0;
        }

        DPRINTF("updating tbytes to %d\n", q->tbytes);
        set_field(&q->qh.token, q->tbytes, QTD_TOKEN_TBYTES);
    }
    ehci_finish_transfer(q, q->usb_status);
    usb_packet_unmap(&q->packet);

    q->qh.token ^= QTD_TOKEN_DTOGGLE;
    q->qh.token &= ~QTD_TOKEN_ACTIVE;

    if ((q->usb_status >= 0) && (q->qh.token & QTD_TOKEN_IOC)) {
        ehci_record_interrupt(q->ehci, USBSTS_INT);
    }
}

// 4.10.3

static int ehci_execute(EHCIQueue *q)
{
    USBPort *port;
    USBDevice *dev;
    int ret;
    int i;
    int endp;
    int devadr;

    if ( !(q->qh.token & QTD_TOKEN_ACTIVE)) {
        fprintf(stderr, "Attempting to execute inactive QH\n");
        return USB_RET_PROCERR;
    }

    q->tbytes = (q->qh.token & QTD_TOKEN_TBYTES_MASK) >> QTD_TOKEN_TBYTES_SH;
    if (q->tbytes > BUFF_SIZE) {
        fprintf(stderr, "Request for more bytes than allowed\n");
        return USB_RET_PROCERR;
    }

    q->pid = (q->qh.token & QTD_TOKEN_PID_MASK) >> QTD_TOKEN_PID_SH;
    switch(q->pid) {
        case 0: q->pid = USB_TOKEN_OUT; break;
        case 1: q->pid = USB_TOKEN_IN; break;
        case 2: q->pid = USB_TOKEN_SETUP; break;
        default: fprintf(stderr, "bad token\n"); break;
    }

    if (ehci_init_transfer(q) != 0) {
        return USB_RET_PROCERR;
    }

    endp = get_field(q->qh.epchar, QH_EPCHAR_EP);
    devadr = get_field(q->qh.epchar, QH_EPCHAR_DEVADDR);

    ret = USB_RET_NODEV;

    usb_packet_setup(&q->packet, q->pid, devadr, endp);
    usb_packet_map(&q->packet, &q->sgl);

    // TO-DO: associating device with ehci port
    for(i = 0; i < NB_PORTS; i++) {
        port = &q->ehci->ports[i];
        dev = port->dev;

        if (!(q->ehci->portsc[i] &(PORTSC_CONNECT))) {
            DPRINTF("Port %d, no exec, not connected(%08X)\n",
                    i, q->ehci->portsc[i]);
            continue;
        }

        ret = usb_handle_packet(dev, &q->packet);

        DPRINTF("submit: qh %x next %x qtd %x pid %x len %zd "
                "(total %d) endp %x ret %d\n",
                q->qhaddr, q->qh.next, q->qtdaddr, q->pid,
                q->packet.iov.size, q->tbytes, endp, ret);

        if (ret != USB_RET_NODEV) {
            break;
        }
    }

    if (ret > BUFF_SIZE) {
        fprintf(stderr, "ret from usb_handle_packet > BUFF_SIZE\n");
        return USB_RET_PROCERR;
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
    uint32_t i, j, len, pid, dir, devaddr, endp;
    uint32_t pg, off, ptr1, ptr2, max, mult;

    dir =(itd->bufptr[1] & ITD_BUFPTR_DIRECTION);
    devaddr = get_field(itd->bufptr[0], ITD_BUFPTR_DEVADDR);
    endp = get_field(itd->bufptr[0], ITD_BUFPTR_EP);
    max = get_field(itd->bufptr[1], ITD_BUFPTR_MAXPKT);
    mult = get_field(itd->bufptr[2], ITD_BUFPTR_MULT);

    for(i = 0; i < 8; i++) {
        if (itd->transact[i] & ITD_XACT_ACTIVE) {
            pg   = get_field(itd->transact[i], ITD_XACT_PGSEL);
            off  = itd->transact[i] & ITD_XACT_OFFSET_MASK;
            ptr1 = (itd->bufptr[pg] & ITD_BUFPTR_MASK);
            ptr2 = (itd->bufptr[pg+1] & ITD_BUFPTR_MASK);
            len  = get_field(itd->transact[i], ITD_XACT_LENGTH);

            if (len > max * mult) {
                len = max * mult;
            }

            if (len > BUFF_SIZE) {
                return USB_RET_PROCERR;
            }

            qemu_sglist_init(&ehci->isgl, 2);
            if (off + len > 4096) {
                /* transfer crosses page border */
                uint32_t len2 = off + len - 4096;
                uint32_t len1 = len - len2;
                qemu_sglist_add(&ehci->isgl, ptr1 + off, len1);
                qemu_sglist_add(&ehci->isgl, ptr2, len2);
            } else {
                qemu_sglist_add(&ehci->isgl, ptr1 + off, len);
            }

            pid = dir ? USB_TOKEN_IN : USB_TOKEN_OUT;

            usb_packet_setup(&ehci->ipacket, pid, devaddr, endp);
            usb_packet_map(&ehci->ipacket, &ehci->isgl);

            ret = USB_RET_NODEV;
            for (j = 0; j < NB_PORTS; j++) {
                port = &ehci->ports[j];
                dev = port->dev;

                if (!(ehci->portsc[j] &(PORTSC_CONNECT))) {
                    continue;
                }

                ret = usb_handle_packet(dev, &ehci->ipacket);

                if (ret != USB_RET_NODEV) {
                    break;
                }
            }

            usb_packet_unmap(&ehci->ipacket);
            qemu_sglist_destroy(&ehci->isgl);

#if 0
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
#else
            if (ret == USB_RET_NAK) {
                ret = 0;
            }
#endif

            if (ret >= 0) {
                if (!dir) {
                    /* OUT */
                    set_field(&itd->transact[i], len - ret, ITD_XACT_LENGTH);
                } else {
                    /* IN */
                    set_field(&itd->transact[i], ret, ITD_XACT_LENGTH);
                }

                if (itd->transact[i] & ITD_XACT_IOC) {
                    ehci_record_interrupt(ehci, USBSTS_INT);
                }
            }
            itd->transact[i] &= ~ITD_XACT_ACTIVE;
        }
    }
    return 0;
}

/*  This state is the entry point for asynchronous schedule
 *  processing.  Entry here consitutes a EHCI start event state (4.8.5)
 */
static int ehci_state_waitlisthead(EHCIState *ehci,  int async)
{
    EHCIqh qh;
    int i = 0;
    int again = 0;
    uint32_t entry = ehci->asynclistaddr;

    /* set reclamation flag at start event (4.8.6) */
    if (async) {
        ehci_set_usbsts(ehci, USBSTS_REC);
    }

    ehci_queues_rip_unused(ehci);

    /*  Find the head of the list (4.9.1.1) */
    for(i = 0; i < MAX_QH; i++) {
        get_dwords(NLPTR_GET(entry), (uint32_t *) &qh, sizeof(EHCIqh) >> 2);
        ehci_trace_qh(NULL, NLPTR_GET(entry), &qh);

        if (qh.epchar & QH_EPCHAR_H) {
            if (async) {
                entry |= (NLPTR_TYPE_QH << 1);
            }

            ehci_set_fetch_addr(ehci, async, entry);
            ehci_set_state(ehci, async, EST_FETCHENTRY);
            again = 1;
            goto out;
        }

        entry = qh.next;
        if (entry == ehci->asynclistaddr) {
            break;
        }
    }

    /* no head found for list. */

    ehci_set_state(ehci, async, EST_ACTIVE);

out:
    return again;
}


/*  This state is the entry point for periodic schedule processing as
 *  well as being a continuation state for async processing.
 */
static int ehci_state_fetchentry(EHCIState *ehci, int async)
{
    int again = 0;
    uint32_t entry = ehci_get_fetch_addr(ehci, async);

    if (entry < 0x1000) {
        DPRINTF("fetchentry: entry invalid (0x%08x)\n", entry);
        ehci_set_state(ehci, async, EST_ACTIVE);
        goto out;
    }

    /* section 4.8, only QH in async schedule */
    if (async && (NLPTR_TYPE_GET(entry) != NLPTR_TYPE_QH)) {
        fprintf(stderr, "non queue head request in async schedule\n");
        return -1;
    }

    switch (NLPTR_TYPE_GET(entry)) {
    case NLPTR_TYPE_QH:
        ehci_set_state(ehci, async, EST_FETCHQH);
        again = 1;
        break;

    case NLPTR_TYPE_ITD:
        ehci_set_state(ehci, async, EST_FETCHITD);
        again = 1;
        break;

    case NLPTR_TYPE_STITD:
        ehci_set_state(ehci, async, EST_FETCHSITD);
        again = 1;
        break;

    default:
        /* TODO: handle FSTN type */
        fprintf(stderr, "FETCHENTRY: entry at %X is of type %d "
                "which is not supported yet\n", entry, NLPTR_TYPE_GET(entry));
        return -1;
    }

out:
    return again;
}

static EHCIQueue *ehci_state_fetchqh(EHCIState *ehci, int async)
{
    uint32_t entry;
    EHCIQueue *q;
    int reload;

    entry = ehci_get_fetch_addr(ehci, async);
    q = ehci_find_queue_by_qh(ehci, entry);
    if (NULL == q) {
        q = ehci_alloc_queue(ehci, async);
    }
    q->qhaddr = entry;
    q->seen++;

    if (q->seen > 1) {
        /* we are going in circles -- stop processing */
        ehci_set_state(ehci, async, EST_ACTIVE);
        q = NULL;
        goto out;
    }

    get_dwords(NLPTR_GET(q->qhaddr), (uint32_t *) &q->qh, sizeof(EHCIqh) >> 2);
    ehci_trace_qh(q, NLPTR_GET(q->qhaddr), &q->qh);

    if (q->async == EHCI_ASYNC_INFLIGHT) {
        /* I/O still in progress -- skip queue */
        ehci_set_state(ehci, async, EST_HORIZONTALQH);
        goto out;
    }
    if (q->async == EHCI_ASYNC_FINISHED) {
        /* I/O finished -- continue processing queue */
        trace_usb_ehci_queue_action(q, "resume");
        ehci_set_state(ehci, async, EST_EXECUTING);
        goto out;
    }

    if (async && (q->qh.epchar & QH_EPCHAR_H)) {

        /*  EHCI spec version 1.0 Section 4.8.3 & 4.10.1 */
        if (ehci->usbsts & USBSTS_REC) {
            ehci_clear_usbsts(ehci, USBSTS_REC);
        } else {
            DPRINTF("FETCHQH:  QH 0x%08x. H-bit set, reclamation status reset"
                       " - done processing\n", q->qhaddr);
            ehci_set_state(ehci, async, EST_ACTIVE);
            q = NULL;
            goto out;
        }
    }

#if EHCI_DEBUG
    if (q->qhaddr != q->qh.next) {
    DPRINTF("FETCHQH:  QH 0x%08x (h %x halt %x active %x) next 0x%08x\n",
               q->qhaddr,
               q->qh.epchar & QH_EPCHAR_H,
               q->qh.token & QTD_TOKEN_HALT,
               q->qh.token & QTD_TOKEN_ACTIVE,
               q->qh.next);
    }
#endif

    reload = get_field(q->qh.epchar, QH_EPCHAR_RL);
    if (reload) {
        set_field(&q->qh.altnext_qtd, reload, QH_ALTNEXT_NAKCNT);
    }

    if (q->qh.token & QTD_TOKEN_HALT) {
        ehci_set_state(ehci, async, EST_HORIZONTALQH);

    } else if ((q->qh.token & QTD_TOKEN_ACTIVE) && (q->qh.current_qtd > 0x1000)) {
        q->qtdaddr = q->qh.current_qtd;
        ehci_set_state(ehci, async, EST_FETCHQTD);

    } else {
        /*  EHCI spec version 1.0 Section 4.10.2 */
        ehci_set_state(ehci, async, EST_ADVANCEQUEUE);
    }

out:
    return q;
}

static int ehci_state_fetchitd(EHCIState *ehci, int async)
{
    uint32_t entry;
    EHCIitd itd;

    assert(!async);
    entry = ehci_get_fetch_addr(ehci, async);

    get_dwords(NLPTR_GET(entry),(uint32_t *) &itd,
               sizeof(EHCIitd) >> 2);
    ehci_trace_itd(ehci, entry, &itd);

    if (ehci_process_itd(ehci, &itd) != 0) {
        return -1;
    }

    put_dwords(NLPTR_GET(entry), (uint32_t *) &itd,
                sizeof(EHCIitd) >> 2);
    ehci_set_fetch_addr(ehci, async, itd.next);
    ehci_set_state(ehci, async, EST_FETCHENTRY);

    return 1;
}

static int ehci_state_fetchsitd(EHCIState *ehci, int async)
{
    uint32_t entry;
    EHCIsitd sitd;

    assert(!async);
    entry = ehci_get_fetch_addr(ehci, async);

    get_dwords(NLPTR_GET(entry), (uint32_t *)&sitd,
               sizeof(EHCIsitd) >> 2);
    ehci_trace_sitd(ehci, entry, &sitd);

    if (!(sitd.results & SITD_RESULTS_ACTIVE)) {
        /* siTD is not active, nothing to do */;
    } else {
        /* TODO: split transfers are not implemented */
        fprintf(stderr, "WARNING: Skipping active siTD\n");
    }

    ehci_set_fetch_addr(ehci, async, sitd.next);
    ehci_set_state(ehci, async, EST_FETCHENTRY);
    return 1;
}

/* Section 4.10.2 - paragraph 3 */
static int ehci_state_advqueue(EHCIQueue *q, int async)
{
#if 0
    /* TO-DO: 4.10.2 - paragraph 2
     * if I-bit is set to 1 and QH is not active
     * go to horizontal QH
     */
    if (I-bit set) {
        ehci_set_state(ehci, async, EST_HORIZONTALQH);
        goto out;
    }
#endif

    /*
     * want data and alt-next qTD is valid
     */
    if (((q->qh.token & QTD_TOKEN_TBYTES_MASK) != 0) &&
        (q->qh.altnext_qtd > 0x1000) &&
        (NLPTR_TBIT(q->qh.altnext_qtd) == 0)) {
        q->qtdaddr = q->qh.altnext_qtd;
        ehci_set_state(q->ehci, async, EST_FETCHQTD);

    /*
     *  next qTD is valid
     */
    } else if ((q->qh.next_qtd > 0x1000) &&
               (NLPTR_TBIT(q->qh.next_qtd) == 0)) {
        q->qtdaddr = q->qh.next_qtd;
        ehci_set_state(q->ehci, async, EST_FETCHQTD);

    /*
     *  no valid qTD, try next QH
     */
    } else {
        ehci_set_state(q->ehci, async, EST_HORIZONTALQH);
    }

    return 1;
}

/* Section 4.10.2 - paragraph 4 */
static int ehci_state_fetchqtd(EHCIQueue *q, int async)
{
    int again = 0;

    get_dwords(NLPTR_GET(q->qtdaddr),(uint32_t *) &q->qtd, sizeof(EHCIqtd) >> 2);
    ehci_trace_qtd(q, NLPTR_GET(q->qtdaddr), &q->qtd);

    if (q->qtd.token & QTD_TOKEN_ACTIVE) {
        ehci_set_state(q->ehci, async, EST_EXECUTE);
        again = 1;
    } else {
        ehci_set_state(q->ehci, async, EST_HORIZONTALQH);
        again = 1;
    }

    return again;
}

static int ehci_state_horizqh(EHCIQueue *q, int async)
{
    int again = 0;

    if (ehci_get_fetch_addr(q->ehci, async) != q->qh.next) {
        ehci_set_fetch_addr(q->ehci, async, q->qh.next);
        ehci_set_state(q->ehci, async, EST_FETCHENTRY);
        again = 1;
    } else {
        ehci_set_state(q->ehci, async, EST_ACTIVE);
    }

    return again;
}

/*
 *  Write the qh back to guest physical memory.  This step isn't
 *  in the EHCI spec but we need to do it since we don't share
 *  physical memory with our guest VM.
 *
 *  The first three dwords are read-only for the EHCI, so skip them
 *  when writing back the qh.
 */
static void ehci_flush_qh(EHCIQueue *q)
{
    uint32_t *qh = (uint32_t *) &q->qh;
    uint32_t dwords = sizeof(EHCIqh) >> 2;
    uint32_t addr = NLPTR_GET(q->qhaddr);

    put_dwords(addr + 3 * sizeof(uint32_t), qh + 3, dwords - 3);
}

static int ehci_state_execute(EHCIQueue *q, int async)
{
    int again = 0;
    int reload, nakcnt;
    int smask;

    if (ehci_qh_do_overlay(q) != 0) {
        return -1;
    }

    smask = get_field(q->qh.epcap, QH_EPCAP_SMASK);

    if (!smask) {
        reload = get_field(q->qh.epchar, QH_EPCHAR_RL);
        nakcnt = get_field(q->qh.altnext_qtd, QH_ALTNEXT_NAKCNT);
        if (reload && !nakcnt) {
            ehci_set_state(q->ehci, async, EST_HORIZONTALQH);
            again = 1;
            goto out;
        }
    }

    // TODO verify enough time remains in the uframe as in 4.4.1.1
    // TODO write back ptr to async list when done or out of time
    // TODO Windows does not seem to ever set the MULT field

    if (!async) {
        int transactCtr = get_field(q->qh.epcap, QH_EPCAP_MULT);
        if (!transactCtr) {
            ehci_set_state(q->ehci, async, EST_HORIZONTALQH);
            again = 1;
            goto out;
        }
    }

    if (async) {
        ehci_set_usbsts(q->ehci, USBSTS_REC);
    }

    q->usb_status = ehci_execute(q);
    if (q->usb_status == USB_RET_PROCERR) {
        again = -1;
        goto out;
    }
    if (q->usb_status == USB_RET_ASYNC) {
        ehci_flush_qh(q);
        trace_usb_ehci_queue_action(q, "suspend");
        q->async = EHCI_ASYNC_INFLIGHT;
        ehci_set_state(q->ehci, async, EST_HORIZONTALQH);
        again = 1;
        goto out;
    }

    ehci_set_state(q->ehci, async, EST_EXECUTING);
    again = 1;

out:
    return again;
}

static int ehci_state_executing(EHCIQueue *q, int async)
{
    int again = 0;
    int reload, nakcnt;

    ehci_execute_complete(q);
    if (q->usb_status == USB_RET_ASYNC) {
        goto out;
    }
    if (q->usb_status == USB_RET_PROCERR) {
        again = -1;
        goto out;
    }

    // 4.10.3
    if (!async) {
        int transactCtr = get_field(q->qh.epcap, QH_EPCAP_MULT);
        transactCtr--;
        set_field(&q->qh.epcap, transactCtr, QH_EPCAP_MULT);
        // 4.10.3, bottom of page 82, should exit this state when transaction
        // counter decrements to 0
    }

    reload = get_field(q->qh.epchar, QH_EPCHAR_RL);
    if (reload) {
        nakcnt = get_field(q->qh.altnext_qtd, QH_ALTNEXT_NAKCNT);
        if (q->usb_status == USB_RET_NAK) {
            if (nakcnt) {
                nakcnt--;
            }
        } else {
            nakcnt = reload;
        }
        set_field(&q->qh.altnext_qtd, nakcnt, QH_ALTNEXT_NAKCNT);
    }

    /* 4.10.5 */
    if ((q->usb_status == USB_RET_NAK) || (q->qh.token & QTD_TOKEN_ACTIVE)) {
        ehci_set_state(q->ehci, async, EST_HORIZONTALQH);
    } else {
        ehci_set_state(q->ehci, async, EST_WRITEBACK);
    }

    again = 1;

out:
    ehci_flush_qh(q);
    return again;
}


static int ehci_state_writeback(EHCIQueue *q, int async)
{
    int again = 0;

    /*  Write back the QTD from the QH area */
    ehci_trace_qtd(q, NLPTR_GET(q->qtdaddr), (EHCIqtd*) &q->qh.next_qtd);
    put_dwords(NLPTR_GET(q->qtdaddr),(uint32_t *) &q->qh.next_qtd,
                sizeof(EHCIqtd) >> 2);

    /*
     * EHCI specs say go horizontal here.
     *
     * We can also advance the queue here for performance reasons.  We
     * need to take care to only take that shortcut in case we've
     * processed the qtd just written back without errors, i.e. halt
     * bit is clear.
     */
    if (q->qh.token & QTD_TOKEN_HALT) {
        ehci_set_state(q->ehci, async, EST_HORIZONTALQH);
        again = 1;
    } else {
        ehci_set_state(q->ehci, async, EST_ADVANCEQUEUE);
        again = 1;
    }
    return again;
}

/*
 * This is the state machine that is common to both async and periodic
 */

static void ehci_advance_state(EHCIState *ehci,
                               int async)
{
    EHCIQueue *q = NULL;
    int again;
    int iter = 0;

    do {
        if (ehci_get_state(ehci, async) == EST_FETCHQH) {
            iter++;
            /* if we are roaming a lot of QH without executing a qTD
             * something is wrong with the linked list. TO-DO: why is
             * this hack needed?
             */
            assert(iter < MAX_ITERATIONS);
#if 0
            if (iter > MAX_ITERATIONS) {
                DPRINTF("\n*** advance_state: bailing on MAX ITERATIONS***\n");
                ehci_set_state(ehci, async, EST_ACTIVE);
                break;
            }
#endif
        }
        switch(ehci_get_state(ehci, async)) {
        case EST_WAITLISTHEAD:
            again = ehci_state_waitlisthead(ehci, async);
            break;

        case EST_FETCHENTRY:
            again = ehci_state_fetchentry(ehci, async);
            break;

        case EST_FETCHQH:
            q = ehci_state_fetchqh(ehci, async);
            again = q ? 1 : 0;
            break;

        case EST_FETCHITD:
            again = ehci_state_fetchitd(ehci, async);
            break;

        case EST_FETCHSITD:
            again = ehci_state_fetchsitd(ehci, async);
            break;

        case EST_ADVANCEQUEUE:
            again = ehci_state_advqueue(q, async);
            break;

        case EST_FETCHQTD:
            again = ehci_state_fetchqtd(q, async);
            break;

        case EST_HORIZONTALQH:
            again = ehci_state_horizqh(q, async);
            break;

        case EST_EXECUTE:
            iter = 0;
            again = ehci_state_execute(q, async);
            break;

        case EST_EXECUTING:
            assert(q != NULL);
            again = ehci_state_executing(q, async);
            break;

        case EST_WRITEBACK:
            again = ehci_state_writeback(q, async);
            break;

        default:
            fprintf(stderr, "Bad state!\n");
            again = -1;
            assert(0);
            break;
        }

        if (again < 0) {
            fprintf(stderr, "processing error - resetting ehci HC\n");
            ehci_reset(ehci);
            again = 0;
            assert(0);
        }
    }
    while (again);

    ehci_commit_interrupt(ehci);
}

static void ehci_advance_async_state(EHCIState *ehci)
{
    int async = 1;

    switch(ehci_get_state(ehci, async)) {
    case EST_INACTIVE:
        if (!(ehci->usbcmd & USBCMD_ASE)) {
            break;
        }
        ehci_set_usbsts(ehci, USBSTS_ASS);
        ehci_set_state(ehci, async, EST_ACTIVE);
        // No break, fall through to ACTIVE

    case EST_ACTIVE:
        if ( !(ehci->usbcmd & USBCMD_ASE)) {
            ehci_clear_usbsts(ehci, USBSTS_ASS);
            ehci_set_state(ehci, async, EST_INACTIVE);
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

        /* check that address register has been set */
        if (ehci->asynclistaddr == 0) {
            break;
        }

        ehci_set_state(ehci, async, EST_WAITLISTHEAD);
        ehci_advance_state(ehci, async);
        break;

    default:
        /* this should only be due to a developer mistake */
        fprintf(stderr, "ehci: Bad asynchronous state %d. "
                "Resetting to active\n", ehci->astate);
        assert(0);
    }
}

static void ehci_advance_periodic_state(EHCIState *ehci)
{
    uint32_t entry;
    uint32_t list;
    int async = 0;

    // 4.6

    switch(ehci_get_state(ehci, async)) {
    case EST_INACTIVE:
        if ( !(ehci->frindex & 7) && (ehci->usbcmd & USBCMD_PSE)) {
            ehci_set_usbsts(ehci, USBSTS_PSS);
            ehci_set_state(ehci, async, EST_ACTIVE);
            // No break, fall through to ACTIVE
        } else
            break;

    case EST_ACTIVE:
        if ( !(ehci->frindex & 7) && !(ehci->usbcmd & USBCMD_PSE)) {
            ehci_clear_usbsts(ehci, USBSTS_PSS);
            ehci_set_state(ehci, async, EST_INACTIVE);
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
        ehci_set_fetch_addr(ehci, async,entry);
        ehci_set_state(ehci, async, EST_FETCHENTRY);
        ehci_advance_state(ehci, async);
        break;

    default:
        /* this should only be due to a developer mistake */
        fprintf(stderr, "ehci: Bad periodic state %d. "
                "Resetting to active\n", ehci->pstate);
        assert(0);
    }
}

static void ehci_frame_timer(void *opaque)
{
    EHCIState *ehci = opaque;
    int64_t expire_time, t_now;
    uint64_t ns_elapsed;
    int frames;
    int i;
    int skipped_frames = 0;

    t_now = qemu_get_clock_ns(vm_clock);
    expire_time = t_now + (get_ticks_per_sec() / ehci->freq);

    ns_elapsed = t_now - ehci->last_run_ns;
    frames = ns_elapsed / FRAME_TIMER_NS;

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

        if (frames - i > ehci->maxframes) {
            skipped_frames++;
        } else {
            ehci_advance_periodic_state(ehci);
        }

        ehci->last_run_ns += FRAME_TIMER_NS;
    }

#if 0
    if (skipped_frames) {
        DPRINTF("WARNING - EHCI skipped %d frames\n", skipped_frames);
    }
#endif

    /*  Async is not inside loop since it executes everything it can once
     *  called
     */
    ehci_advance_async_state(ehci);

    qemu_mod_timer(ehci->frame_timer, expire_time);
}


static const MemoryRegionOps ehci_mem_ops = {
    .old_mmio = {
        .read = { ehci_mem_readb, ehci_mem_readw, ehci_mem_readl },
        .write = { ehci_mem_writeb, ehci_mem_writew, ehci_mem_writel },
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static int usb_ehci_initfn(PCIDevice *dev);

static USBPortOps ehci_port_ops = {
    .attach = ehci_attach,
    .detach = ehci_detach,
    .child_detach = ehci_child_detach,
    .wakeup = ehci_wakeup,
    .complete = ehci_async_complete_packet,
};

static USBBusOps ehci_bus_ops = {
    .register_companion = ehci_register_companion,
};

static const VMStateDescription vmstate_ehci = {
    .name = "ehci",
    .unmigratable = 1,
};

static Property ehci_properties[] = {
    DEFINE_PROP_UINT32("freq",      EHCIState, freq, FRAME_TIMER_FREQ),
    DEFINE_PROP_UINT32("maxframes", EHCIState, maxframes, 128),
    DEFINE_PROP_END_OF_LIST(),
};

static PCIDeviceInfo ehci_info[] = {
    {
        .qdev.name    = "usb-ehci",
        .qdev.size    = sizeof(EHCIState),
        .qdev.vmsd    = &vmstate_ehci,
        .init         = usb_ehci_initfn,
        .vendor_id    = PCI_VENDOR_ID_INTEL,
        .device_id    = PCI_DEVICE_ID_INTEL_82801D, /* ich4 */
        .revision     = 0x10,
        .class_id     = PCI_CLASS_SERIAL_USB,
        .qdev.props   = ehci_properties,
    },{
        .qdev.name    = "ich9-usb-ehci1",
        .qdev.size    = sizeof(EHCIState),
        .qdev.vmsd    = &vmstate_ehci,
        .init         = usb_ehci_initfn,
        .vendor_id    = PCI_VENDOR_ID_INTEL,
        .device_id    = PCI_DEVICE_ID_INTEL_82801I_EHCI1,
        .revision     = 0x03,
        .class_id     = PCI_CLASS_SERIAL_USB,
        .qdev.props   = ehci_properties,
    },{
        /* end of list */
    }
};

static int usb_ehci_initfn(PCIDevice *dev)
{
    EHCIState *s = DO_UPCAST(EHCIState, dev, dev);
    uint8_t *pci_conf = s->dev.config;
    int i;

    pci_set_byte(&pci_conf[PCI_CLASS_PROG], 0x20);

    /* capabilities pointer */
    pci_set_byte(&pci_conf[PCI_CAPABILITY_LIST], 0x00);
    //pci_set_byte(&pci_conf[PCI_CAPABILITY_LIST], 0x50);

    pci_set_byte(&pci_conf[PCI_INTERRUPT_PIN], 4); /* interrupt pin D */
    pci_set_byte(&pci_conf[PCI_MIN_GNT], 0);
    pci_set_byte(&pci_conf[PCI_MAX_LAT], 0);

    // pci_conf[0x50] = 0x01; // power management caps

    pci_set_byte(&pci_conf[USB_SBRN], USB_RELEASE_2); // release number (2.1.4)
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

    usb_bus_new(&s->bus, &ehci_bus_ops, &s->dev.qdev);
    for(i = 0; i < NB_PORTS; i++) {
        usb_register_port(&s->bus, &s->ports[i], s, i, &ehci_port_ops,
                          USB_SPEED_MASK_HIGH);
        s->ports[i].dev = 0;
    }

    s->frame_timer = qemu_new_timer_ns(vm_clock, ehci_frame_timer, s);
    QTAILQ_INIT(&s->queues);

    qemu_register_reset(ehci_reset, s);

    memory_region_init_io(&s->mem, &ehci_mem_ops, s, "ehci", MMIO_SIZE);
    pci_register_bar(&s->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mem);

    fprintf(stderr, "*** EHCI support is under development ***\n");

    return 0;
}

static void ehci_register(void)
{
    pci_qdev_register_many(ehci_info);
}
device_init(ehci_register);

/*
 * vim: expandtab ts=4
 */
