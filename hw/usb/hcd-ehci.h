/*
 * QEMU USB EHCI Emulation
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

#ifndef HW_USB_HCD_EHCI_H
#define HW_USB_HCD_EHCI_H

#include "hw/hw.h"
#include "qemu/timer.h"
#include "hw/usb.h"
#include "sysemu/dma.h"
#include "sysemu/sysemu.h"
#include "hw/pci/pci.h"
#include "hw/sysbus.h"

#ifndef EHCI_DEBUG
#define EHCI_DEBUG   0
#endif

#if EHCI_DEBUG
#define DPRINTF printf
#else
#define DPRINTF(...)
#endif

#define MMIO_SIZE        0x1000
#define CAPA_SIZE        0x10

#define NB_PORTS         6        /* Max. Number of downstream ports */

typedef struct EHCIPacket EHCIPacket;
typedef struct EHCIQueue EHCIQueue;
typedef struct EHCIState EHCIState;

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
    uint32_t next;                  /* Standard next link pointer */
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

    uint32_t backptr;                 /* Standard next link pointer */
} EHCIsitd;

/*  EHCI spec version 1.0 Section 3.5
 */
typedef struct EHCIqtd {
    uint32_t next;                    /* Standard next link pointer */
    uint32_t altnext;                 /* Standard next link pointer */
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

    uint32_t bufptr[5];               /* Standard buffer pointer */
#define QTD_BUFPTR_MASK               0xfffff000
#define QTD_BUFPTR_SH                 12
} EHCIqtd;

/*  EHCI spec version 1.0 Section 3.6
 */
typedef struct EHCIqh {
    uint32_t next;                    /* Standard next link pointer */

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

    uint32_t current_qtd;             /* Standard next link pointer */
    uint32_t next_qtd;                /* Standard next link pointer */
    uint32_t altnext_qtd;
#define QH_ALTNEXT_NAKCNT_MASK        0x0000001e
#define QH_ALTNEXT_NAKCNT_SH          1

    uint32_t token;                   /* Same as QTD token */
    uint32_t bufptr[5];               /* Standard buffer pointer */
#define BUFPTR_CPROGMASK_MASK         0x000000ff
#define BUFPTR_FRAMETAG_MASK          0x0000001f
#define BUFPTR_SBYTES_MASK            0x00000fe0
#define BUFPTR_SBYTES_SH              5
} EHCIqh;

/*  EHCI spec version 1.0 Section 3.7
 */
typedef struct EHCIfstn {
    uint32_t next;                    /* Standard next link pointer */
    uint32_t backptr;                 /* Standard next link pointer */
} EHCIfstn;

enum async_state {
    EHCI_ASYNC_NONE = 0,
    EHCI_ASYNC_INITIALIZED,
    EHCI_ASYNC_INFLIGHT,
    EHCI_ASYNC_FINISHED,
};

struct EHCIPacket {
    EHCIQueue *queue;
    QTAILQ_ENTRY(EHCIPacket) next;

    EHCIqtd qtd;           /* copy of current QTD (being worked on) */
    uint32_t qtdaddr;      /* address QTD read from                 */

    USBPacket packet;
    QEMUSGList sgl;
    int pid;
    enum async_state async;
};

struct EHCIQueue {
    EHCIState *ehci;
    QTAILQ_ENTRY(EHCIQueue) next;
    uint32_t seen;
    uint64_t ts;
    int async;
    int transact_ctr;

    /* cached data from guest - needs to be flushed
     * when guest removes an entry (doorbell, handshake sequence)
     */
    EHCIqh qh;             /* copy of current QH (being worked on) */
    uint32_t qhaddr;       /* address QH read from                 */
    uint32_t qtdaddr;      /* address QTD read from                */
    int last_pid;          /* pid of last packet executed          */
    USBDevice *dev;
    QTAILQ_HEAD(pkts_head, EHCIPacket) packets;
};

typedef QTAILQ_HEAD(EHCIQueueHead, EHCIQueue) EHCIQueueHead;

struct EHCIState {
    USBBus bus;
    DeviceState *device;
    qemu_irq irq;
    MemoryRegion mem;
    AddressSpace *as;
    MemoryRegion mem_caps;
    MemoryRegion mem_opreg;
    MemoryRegion mem_ports;
    int companion_count;
    bool companion_enable;
    uint16_t capsbase;
    uint16_t opregbase;
    uint16_t portscbase;
    uint16_t portnr;

    /* properties */
    uint32_t maxframes;

    /*
     *  EHCI spec version 1.0 Section 2.3
     *  Host Controller Operational Registers
     */
    uint8_t caps[CAPA_SIZE];
    union {
        uint32_t opreg[0x44/sizeof(uint32_t)];
        struct {
            uint32_t usbcmd;
            uint32_t usbsts;
            uint32_t usbintr;
            uint32_t frindex;
            uint32_t ctrldssegment;
            uint32_t periodiclistbase;
            uint32_t asynclistaddr;
            uint32_t notused[9];
            uint32_t configflag;
        };
    };
    uint32_t portsc[NB_PORTS];

    /*
     *  Internal states, shadow registers, etc
     */
    QEMUTimer *frame_timer;
    QEMUBH *async_bh;
    bool working;
    uint32_t astate;         /* Current state in asynchronous schedule */
    uint32_t pstate;         /* Current state in periodic schedule     */
    USBPort ports[NB_PORTS];
    USBPort *companion_ports[NB_PORTS];
    uint32_t usbsts_pending;
    uint32_t usbsts_frindex;
    EHCIQueueHead aqueues;
    EHCIQueueHead pqueues;

    /* which address to look at next */
    uint32_t a_fetch_addr;
    uint32_t p_fetch_addr;

    USBPacket ipacket;
    QEMUSGList isgl;

    uint64_t last_run_ns;
    uint32_t async_stepdown;
    uint32_t periodic_sched_active;
    bool int_req_by_async;
    VMChangeStateEntry *vmstate;
};

extern const VMStateDescription vmstate_ehci;

void usb_ehci_init(EHCIState *s, DeviceState *dev);
void usb_ehci_finalize(EHCIState *s);
void usb_ehci_realize(EHCIState *s, DeviceState *dev, Error **errp);
void usb_ehci_unrealize(EHCIState *s, DeviceState *dev, Error **errp);
void ehci_reset(void *opaque);

#define TYPE_PCI_EHCI "pci-ehci-usb"
#define PCI_EHCI(obj) OBJECT_CHECK(EHCIPCIState, (obj), TYPE_PCI_EHCI)

typedef struct EHCIPCIState {
    /*< private >*/
    PCIDevice pcidev;
    /*< public >*/

    EHCIState ehci;
} EHCIPCIState;


#define TYPE_SYS_BUS_EHCI "sysbus-ehci-usb"
#define TYPE_EXYNOS4210_EHCI "exynos4210-ehci-usb"
#define TYPE_TEGRA2_EHCI "tegra2-ehci-usb"
#define TYPE_FUSBH200_EHCI "fusbh200-ehci-usb"

#define SYS_BUS_EHCI(obj) \
    OBJECT_CHECK(EHCISysBusState, (obj), TYPE_SYS_BUS_EHCI)
#define SYS_BUS_EHCI_CLASS(class) \
    OBJECT_CLASS_CHECK(SysBusEHCIClass, (class), TYPE_SYS_BUS_EHCI)
#define SYS_BUS_EHCI_GET_CLASS(obj) \
    OBJECT_GET_CLASS(SysBusEHCIClass, (obj), TYPE_SYS_BUS_EHCI)

typedef struct EHCISysBusState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    EHCIState ehci;
} EHCISysBusState;

typedef struct SysBusEHCIClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/

    uint16_t capsbase;
    uint16_t opregbase;
    uint16_t portscbase;
    uint16_t portnr;
} SysBusEHCIClass;

#define FUSBH200_EHCI(obj) \
    OBJECT_CHECK(FUSBH200EHCIState, (obj), TYPE_FUSBH200_EHCI)

typedef struct FUSBH200EHCIState {
    /*< private >*/
    EHCISysBusState parent_obj;
    /*< public >*/

    MemoryRegion mem_vendor;
} FUSBH200EHCIState;

#endif
