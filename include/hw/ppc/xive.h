/*
 * QEMU PowerPC XIVE interrupt controller model
 *
 *
 * The POWER9 processor comes with a new interrupt controller, called
 * XIVE as "eXternal Interrupt Virtualization Engine".
 *
 * = Overall architecture
 *
 *
 *              XIVE Interrupt Controller
 *              +------------------------------------+      IPIs
 *              | +---------+ +---------+ +--------+ |    +-------+
 *              | |VC       | |CQ       | |PC      |----> | CORES |
 *              | |     esb | |         | |        |----> |       |
 *              | |     eas | |  Bridge | |   tctx |----> |       |
 *              | |SC   end | |         | |    nvt | |    |       |
 *  +------+    | +---------+ +----+----+ +--------+ |    +-+-+-+-+
 *  | RAM  |    +------------------|-----------------+      | | |
 *  |      |                       |                        | | |
 *  |      |                       |                        | | |
 *  |      |  +--------------------v------------------------v-v-v--+    other
 *  |      <--+                     Power Bus                      +--> chips
 *  |  esb |  +---------+-----------------------+------------------+
 *  |  eas |            |                       |
 *  |  end |         +--|------+                |
 *  |  nvt |       +----+----+ |           +----+----+
 *  +------+       |SC       | |           |SC       |
 *                 |         | |           |         |
 *                 | PQ-bits | |           | PQ-bits |
 *                 | local   |-+           |  in VC  |
 *                 +---------+             +---------+
 *                    PCIe                 NX,NPU,CAPI
 *
 *                   SC: Source Controller (aka. IVSE)
 *                   VC: Virtualization Controller (aka. IVRE)
 *                   PC: Presentation Controller (aka. IVPE)
 *                   CQ: Common Queue (Bridge)
 *
 *              PQ-bits: 2 bits source state machine (P:pending Q:queued)
 *                  esb: Event State Buffer (Array of PQ bits in an IVSE)
 *                  eas: Event Assignment Structure
 *                  end: Event Notification Descriptor
 *                  nvt: Notification Virtual Target
 *                 tctx: Thread interrupt Context
 *
 *
 * The XIVE IC is composed of three sub-engines :
 *
 * - Interrupt Virtualization Source Engine (IVSE), or Source
 *   Controller (SC). These are found in PCI PHBs, in the PSI host
 *   bridge controller, but also inside the main controller for the
 *   core IPIs and other sub-chips (NX, CAP, NPU) of the
 *   chip/processor. They are configured to feed the IVRE with events.
 *
 * - Interrupt Virtualization Routing Engine (IVRE) or Virtualization
 *   Controller (VC). Its job is to match an event source with an
 *   Event Notification Descriptor (END).
 *
 * - Interrupt Virtualization Presentation Engine (IVPE) or
 *   Presentation Controller (PC). It maintains the interrupt context
 *   state of each thread and handles the delivery of the external
 *   exception to the thread.
 *
 * In XIVE 1.0, the sub-engines used to be referred as:
 *
 *   SC     Source Controller
 *   VC     Virtualization Controller
 *   PC     Presentation Controller
 *   CQ     Common Queue (PowerBUS Bridge)
 *
 *
 * = XIVE internal tables
 *
 * Each of the sub-engines uses a set of tables to redirect exceptions
 * from event sources to CPU threads.
 *
 *                                           +-------+
 *   User or OS                              |  EQ   |
 *       or                          +------>|entries|
 *   Hypervisor                      |       |  ..   |
 *     Memory                        |       +-------+
 *                                   |           ^
 *                                   |           |
 *              +-------------------------------------------------+
 *                                   |           |
 *   Hypervisor      +------+    +---+--+    +---+--+   +------+
 *     Memory        | ESB  |    | EAT  |    | ENDT |   | NVTT |
 *    (skiboot)      +----+-+    +----+-+    +----+-+   +------+
 *                     ^  |        ^  |        ^  |       ^
 *                     |  |        |  |        |  |       |
 *              +-------------------------------------------------+
 *                     |  |        |  |        |  |       |
 *                     |  |        |  |        |  |       |
 *                +----|--|--------|--|--------|--|-+   +-|-----+    +------+
 *                |    |  |        |  |        |  | |   | | tctx|    |Thread|
 *   IPI or   --> |    +  v        +  v        +  v |---| +  .. |----->     |
 *  HW events --> |                                 |   |       |    |      |
 *    IVSE        |             IVRE                |   | IVPE  |    +------+
 *                +---------------------------------+   +-------+
 *
 *
 *
 * The IVSE have a 2-bits state machine, P for pending and Q for queued,
 * for each source that allows events to be triggered. They are stored in
 * an Event State Buffer (ESB) array and can be controlled by MMIOs.
 *
 * If the event is let through, the IVRE looks up in the Event Assignment
 * Structure (EAS) table for an Event Notification Descriptor (END)
 * configured for the source. Each Event Notification Descriptor defines
 * a notification path to a CPU and an in-memory Event Queue, in which
 * will be enqueued an EQ data for the OS to pull.
 *
 * The IVPE determines if a Notification Virtual Target (NVT) can
 * handle the event by scanning the thread contexts of the VCPUs
 * dispatched on the processor HW threads. It maintains the state of
 * the thread interrupt context (TCTX) of each thread in a NVT table.
 *
 * = Acronyms
 *
 *          Description                     In XIVE 1.0, used to be referred as
 *
 *   EAS    Event Assignment Structure      IVE   Interrupt Virt. Entry
 *   EAT    Event Assignment Table          IVT   Interrupt Virt. Table
 *   ENDT   Event Notif. Descriptor Table   EQDT  Event Queue Desc. Table
 *   EQ     Event Queue                     same
 *   ESB    Event State Buffer              SBE   State Bit Entry
 *   NVT    Notif. Virtual Target           VPD   Virtual Processor Desc.
 *   NVTT   Notif. Virtual Target Table     VPDT  Virtual Processor Desc. Table
 *   TCTX   Thread interrupt Context
 *
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 *
 */

#ifndef PPC_XIVE_H
#define PPC_XIVE_H

#include "hw/qdev-core.h"

/*
 * XIVE Interrupt Source
 */

#define TYPE_XIVE_SOURCE "xive-source"
#define XIVE_SOURCE(obj) OBJECT_CHECK(XiveSource, (obj), TYPE_XIVE_SOURCE)

/*
 * XIVE Interrupt Source characteristics, which define how the ESB are
 * controlled.
 */
#define XIVE_SRC_H_INT_ESB     0x1 /* ESB managed with hcall H_INT_ESB */
#define XIVE_SRC_STORE_EOI     0x2 /* Store EOI supported */

typedef struct XiveSource {
    DeviceState parent;

    /* IRQs */
    uint32_t        nr_irqs;
    qemu_irq        *qirqs;

    /* PQ bits */
    uint8_t         *status;

    /* ESB memory region */
    uint64_t        esb_flags;
    uint32_t        esb_shift;
    MemoryRegion    esb_mmio;
} XiveSource;

/*
 * ESB MMIO setting. Can be one page, for both source triggering and
 * source management, or two different pages. See below for magic
 * values.
 */
#define XIVE_ESB_4K          12 /* PSI HB only */
#define XIVE_ESB_4K_2PAGE    13
#define XIVE_ESB_64K         16
#define XIVE_ESB_64K_2PAGE   17

static inline bool xive_source_esb_has_2page(XiveSource *xsrc)
{
    return xsrc->esb_shift == XIVE_ESB_64K_2PAGE ||
        xsrc->esb_shift == XIVE_ESB_4K_2PAGE;
}

/* The trigger page is always the first/even page */
static inline hwaddr xive_source_esb_page(XiveSource *xsrc, uint32_t srcno)
{
    assert(srcno < xsrc->nr_irqs);
    return (1ull << xsrc->esb_shift) * srcno;
}

/* In a two pages ESB MMIO setting, the odd page is for management */
static inline hwaddr xive_source_esb_mgmt(XiveSource *xsrc, int srcno)
{
    hwaddr addr = xive_source_esb_page(xsrc, srcno);

    if (xive_source_esb_has_2page(xsrc)) {
        addr += (1 << (xsrc->esb_shift - 1));
    }

    return addr;
}

/*
 * Each interrupt source has a 2-bit state machine which can be
 * controlled by MMIO. P indicates that an interrupt is pending (has
 * been sent to a queue and is waiting for an EOI). Q indicates that
 * the interrupt has been triggered while pending.
 *
 * This acts as a coalescing mechanism in order to guarantee that a
 * given interrupt only occurs at most once in a queue.
 *
 * When doing an EOI, the Q bit will indicate if the interrupt
 * needs to be re-triggered.
 */
#define XIVE_ESB_VAL_P        0x2
#define XIVE_ESB_VAL_Q        0x1

#define XIVE_ESB_RESET        0x0
#define XIVE_ESB_PENDING      XIVE_ESB_VAL_P
#define XIVE_ESB_QUEUED       (XIVE_ESB_VAL_P | XIVE_ESB_VAL_Q)
#define XIVE_ESB_OFF          XIVE_ESB_VAL_Q

/*
 * "magic" Event State Buffer (ESB) MMIO offsets.
 *
 * The following offsets into the ESB MMIO allow to read or manipulate
 * the PQ bits. They must be used with an 8-byte load instruction.
 * They all return the previous state of the interrupt (atomically).
 *
 * Additionally, some ESB pages support doing an EOI via a store and
 * some ESBs support doing a trigger via a separate trigger page.
 */
#define XIVE_ESB_STORE_EOI      0x400 /* Store */
#define XIVE_ESB_LOAD_EOI       0x000 /* Load */
#define XIVE_ESB_GET            0x800 /* Load */
#define XIVE_ESB_SET_PQ_00      0xc00 /* Load */
#define XIVE_ESB_SET_PQ_01      0xd00 /* Load */
#define XIVE_ESB_SET_PQ_10      0xe00 /* Load */
#define XIVE_ESB_SET_PQ_11      0xf00 /* Load */

uint8_t xive_source_esb_get(XiveSource *xsrc, uint32_t srcno);
uint8_t xive_source_esb_set(XiveSource *xsrc, uint32_t srcno, uint8_t pq);

void xive_source_pic_print_info(XiveSource *xsrc, uint32_t offset,
                                Monitor *mon);

static inline qemu_irq xive_source_qirq(XiveSource *xsrc, uint32_t srcno)
{
    assert(srcno < xsrc->nr_irqs);
    return xsrc->qirqs[srcno];
}

#endif /* PPC_XIVE_H */
