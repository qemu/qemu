/*
 * QEMU LSI53C895A SCSI Host Bus Adapter emulation
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 */

/* Note:
 * LSI53C810 emulation is incorrect, in the sense that it supports
 * features added in later evolutions. This should not be a problem,
 * as well-behaved operating systems will not try to use them.
 */

#include <assert.h>

#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/scsi/scsi.h"
#include "sysemu/dma.h"
#include "qemu/error-report.h"

//#define DEBUG_LSI
//#define DEBUG_LSI_REG

#ifdef DEBUG_LSI
#define DPRINTF(fmt, ...) \
do { printf("lsi_scsi: " fmt , ## __VA_ARGS__); } while (0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "lsi_scsi: error: " fmt , ## __VA_ARGS__); exit(1);} while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "lsi_scsi: error: " fmt , ## __VA_ARGS__);} while (0)
#endif

#define LSI_MAX_DEVS 7

#define LSI_SCNTL0_TRG    0x01
#define LSI_SCNTL0_AAP    0x02
#define LSI_SCNTL0_EPC    0x08
#define LSI_SCNTL0_WATN   0x10
#define LSI_SCNTL0_START  0x20

#define LSI_SCNTL1_SST    0x01
#define LSI_SCNTL1_IARB   0x02
#define LSI_SCNTL1_AESP   0x04
#define LSI_SCNTL1_RST    0x08
#define LSI_SCNTL1_CON    0x10
#define LSI_SCNTL1_DHP    0x20
#define LSI_SCNTL1_ADB    0x40
#define LSI_SCNTL1_EXC    0x80

#define LSI_SCNTL2_WSR    0x01
#define LSI_SCNTL2_VUE0   0x02
#define LSI_SCNTL2_VUE1   0x04
#define LSI_SCNTL2_WSS    0x08
#define LSI_SCNTL2_SLPHBEN 0x10
#define LSI_SCNTL2_SLPMD  0x20
#define LSI_SCNTL2_CHM    0x40
#define LSI_SCNTL2_SDU    0x80

#define LSI_ISTAT0_DIP    0x01
#define LSI_ISTAT0_SIP    0x02
#define LSI_ISTAT0_INTF   0x04
#define LSI_ISTAT0_CON    0x08
#define LSI_ISTAT0_SEM    0x10
#define LSI_ISTAT0_SIGP   0x20
#define LSI_ISTAT0_SRST   0x40
#define LSI_ISTAT0_ABRT   0x80

#define LSI_ISTAT1_SI     0x01
#define LSI_ISTAT1_SRUN   0x02
#define LSI_ISTAT1_FLSH   0x04

#define LSI_SSTAT0_SDP0   0x01
#define LSI_SSTAT0_RST    0x02
#define LSI_SSTAT0_WOA    0x04
#define LSI_SSTAT0_LOA    0x08
#define LSI_SSTAT0_AIP    0x10
#define LSI_SSTAT0_OLF    0x20
#define LSI_SSTAT0_ORF    0x40
#define LSI_SSTAT0_ILF    0x80

#define LSI_SIST0_PAR     0x01
#define LSI_SIST0_RST     0x02
#define LSI_SIST0_UDC     0x04
#define LSI_SIST0_SGE     0x08
#define LSI_SIST0_RSL     0x10
#define LSI_SIST0_SEL     0x20
#define LSI_SIST0_CMP     0x40
#define LSI_SIST0_MA      0x80

#define LSI_SIST1_HTH     0x01
#define LSI_SIST1_GEN     0x02
#define LSI_SIST1_STO     0x04
#define LSI_SIST1_SBMC    0x10

#define LSI_SOCL_IO       0x01
#define LSI_SOCL_CD       0x02
#define LSI_SOCL_MSG      0x04
#define LSI_SOCL_ATN      0x08
#define LSI_SOCL_SEL      0x10
#define LSI_SOCL_BSY      0x20
#define LSI_SOCL_ACK      0x40
#define LSI_SOCL_REQ      0x80

#define LSI_DSTAT_IID     0x01
#define LSI_DSTAT_SIR     0x04
#define LSI_DSTAT_SSI     0x08
#define LSI_DSTAT_ABRT    0x10
#define LSI_DSTAT_BF      0x20
#define LSI_DSTAT_MDPE    0x40
#define LSI_DSTAT_DFE     0x80

#define LSI_DCNTL_COM     0x01
#define LSI_DCNTL_IRQD    0x02
#define LSI_DCNTL_STD     0x04
#define LSI_DCNTL_IRQM    0x08
#define LSI_DCNTL_SSM     0x10
#define LSI_DCNTL_PFEN    0x20
#define LSI_DCNTL_PFF     0x40
#define LSI_DCNTL_CLSE    0x80

#define LSI_DMODE_MAN     0x01
#define LSI_DMODE_BOF     0x02
#define LSI_DMODE_ERMP    0x04
#define LSI_DMODE_ERL     0x08
#define LSI_DMODE_DIOM    0x10
#define LSI_DMODE_SIOM    0x20

#define LSI_CTEST2_DACK   0x01
#define LSI_CTEST2_DREQ   0x02
#define LSI_CTEST2_TEOP   0x04
#define LSI_CTEST2_PCICIE 0x08
#define LSI_CTEST2_CM     0x10
#define LSI_CTEST2_CIO    0x20
#define LSI_CTEST2_SIGP   0x40
#define LSI_CTEST2_DDIR   0x80

#define LSI_CTEST5_BL2    0x04
#define LSI_CTEST5_DDIR   0x08
#define LSI_CTEST5_MASR   0x10
#define LSI_CTEST5_DFSN   0x20
#define LSI_CTEST5_BBCK   0x40
#define LSI_CTEST5_ADCK   0x80

#define LSI_CCNTL0_DILS   0x01
#define LSI_CCNTL0_DISFC  0x10
#define LSI_CCNTL0_ENNDJ  0x20
#define LSI_CCNTL0_PMJCTL 0x40
#define LSI_CCNTL0_ENPMJ  0x80

#define LSI_CCNTL1_EN64DBMV  0x01
#define LSI_CCNTL1_EN64TIBMV 0x02
#define LSI_CCNTL1_64TIMOD   0x04
#define LSI_CCNTL1_DDAC      0x08
#define LSI_CCNTL1_ZMOD      0x80

/* Enable Response to Reselection */
#define LSI_SCID_RRE      0x60

#define LSI_CCNTL1_40BIT (LSI_CCNTL1_EN64TIBMV|LSI_CCNTL1_64TIMOD)

#define PHASE_DO          0
#define PHASE_DI          1
#define PHASE_CMD         2
#define PHASE_ST          3
#define PHASE_MO          6
#define PHASE_MI          7
#define PHASE_MASK        7

/* Maximum length of MSG IN data.  */
#define LSI_MAX_MSGIN_LEN 8

/* Flag set if this is a tagged command.  */
#define LSI_TAG_VALID     (1 << 16)

typedef struct lsi_request {
    SCSIRequest *req;
    uint32_t tag;
    uint32_t dma_len;
    uint8_t *dma_buf;
    uint32_t pending;
    int out;
    QTAILQ_ENTRY(lsi_request) next;
} lsi_request;

typedef struct {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    MemoryRegion mmio_io;
    MemoryRegion ram_io;
    MemoryRegion io_io;

    int carry; /* ??? Should this be an a visible register somewhere?  */
    int status;
    /* Action to take at the end of a MSG IN phase.
       0 = COMMAND, 1 = disconnect, 2 = DATA OUT, 3 = DATA IN.  */
    int msg_action;
    int msg_len;
    uint8_t msg[LSI_MAX_MSGIN_LEN];
    /* 0 if SCRIPTS are running or stopped.
     * 1 if a Wait Reselect instruction has been issued.
     * 2 if processing DMA from lsi_execute_script.
     * 3 if a DMA operation is in progress.  */
    int waiting;
    SCSIBus bus;
    int current_lun;
    /* The tag is a combination of the device ID and the SCSI tag.  */
    uint32_t select_tag;
    int command_complete;
    QTAILQ_HEAD(, lsi_request) queue;
    lsi_request *current;

    uint32_t dsa;
    uint32_t temp;
    uint32_t dnad;
    uint32_t dbc;
    uint8_t istat0;
    uint8_t istat1;
    uint8_t dcmd;
    uint8_t dstat;
    uint8_t dien;
    uint8_t sist0;
    uint8_t sist1;
    uint8_t sien0;
    uint8_t sien1;
    uint8_t mbox0;
    uint8_t mbox1;
    uint8_t dfifo;
    uint8_t ctest2;
    uint8_t ctest3;
    uint8_t ctest4;
    uint8_t ctest5;
    uint8_t ccntl0;
    uint8_t ccntl1;
    uint32_t dsp;
    uint32_t dsps;
    uint8_t dmode;
    uint8_t dcntl;
    uint8_t scntl0;
    uint8_t scntl1;
    uint8_t scntl2;
    uint8_t scntl3;
    uint8_t sstat0;
    uint8_t sstat1;
    uint8_t scid;
    uint8_t sxfer;
    uint8_t socl;
    uint8_t sdid;
    uint8_t ssid;
    uint8_t sfbr;
    uint8_t stest1;
    uint8_t stest2;
    uint8_t stest3;
    uint8_t sidl;
    uint8_t stime0;
    uint8_t respid0;
    uint8_t respid1;
    uint32_t mmrs;
    uint32_t mmws;
    uint32_t sfs;
    uint32_t drs;
    uint32_t sbms;
    uint32_t dbms;
    uint32_t dnad64;
    uint32_t pmjad1;
    uint32_t pmjad2;
    uint32_t rbc;
    uint32_t ua;
    uint32_t ia;
    uint32_t sbc;
    uint32_t csbc;
    uint32_t scratch[18]; /* SCRATCHA-SCRATCHR */
    uint8_t sbr;

    /* Script ram is stored as 32-bit words in host byteorder.  */
    uint32_t script_ram[2048];
} LSIState;

#define TYPE_LSI53C810  "lsi53c810"
#define TYPE_LSI53C895A "lsi53c895a"

#define LSI53C895A(obj) \
    OBJECT_CHECK(LSIState, (obj), TYPE_LSI53C895A)

static inline int lsi_irq_on_rsl(LSIState *s)
{
    return (s->sien0 & LSI_SIST0_RSL) && (s->scid & LSI_SCID_RRE);
}

static void lsi_soft_reset(LSIState *s)
{
    DPRINTF("Reset\n");
    s->carry = 0;

    s->msg_action = 0;
    s->msg_len = 0;
    s->waiting = 0;
    s->dsa = 0;
    s->dnad = 0;
    s->dbc = 0;
    s->temp = 0;
    memset(s->scratch, 0, sizeof(s->scratch));
    s->istat0 = 0;
    s->istat1 = 0;
    s->dcmd = 0x40;
    s->dstat = LSI_DSTAT_DFE;
    s->dien = 0;
    s->sist0 = 0;
    s->sist1 = 0;
    s->sien0 = 0;
    s->sien1 = 0;
    s->mbox0 = 0;
    s->mbox1 = 0;
    s->dfifo = 0;
    s->ctest2 = LSI_CTEST2_DACK;
    s->ctest3 = 0;
    s->ctest4 = 0;
    s->ctest5 = 0;
    s->ccntl0 = 0;
    s->ccntl1 = 0;
    s->dsp = 0;
    s->dsps = 0;
    s->dmode = 0;
    s->dcntl = 0;
    s->scntl0 = 0xc0;
    s->scntl1 = 0;
    s->scntl2 = 0;
    s->scntl3 = 0;
    s->sstat0 = 0;
    s->sstat1 = 0;
    s->scid = 7;
    s->sxfer = 0;
    s->socl = 0;
    s->sdid = 0;
    s->ssid = 0;
    s->stest1 = 0;
    s->stest2 = 0;
    s->stest3 = 0;
    s->sidl = 0;
    s->stime0 = 0;
    s->respid0 = 0x80;
    s->respid1 = 0;
    s->mmrs = 0;
    s->mmws = 0;
    s->sfs = 0;
    s->drs = 0;
    s->sbms = 0;
    s->dbms = 0;
    s->dnad64 = 0;
    s->pmjad1 = 0;
    s->pmjad2 = 0;
    s->rbc = 0;
    s->ua = 0;
    s->ia = 0;
    s->sbc = 0;
    s->csbc = 0;
    s->sbr = 0;
    assert(QTAILQ_EMPTY(&s->queue));
    assert(!s->current);
}

static int lsi_dma_40bit(LSIState *s)
{
    if ((s->ccntl1 & LSI_CCNTL1_40BIT) == LSI_CCNTL1_40BIT)
        return 1;
    return 0;
}

static int lsi_dma_ti64bit(LSIState *s)
{
    if ((s->ccntl1 & LSI_CCNTL1_EN64TIBMV) == LSI_CCNTL1_EN64TIBMV)
        return 1;
    return 0;
}

static int lsi_dma_64bit(LSIState *s)
{
    if ((s->ccntl1 & LSI_CCNTL1_EN64DBMV) == LSI_CCNTL1_EN64DBMV)
        return 1;
    return 0;
}

static uint8_t lsi_reg_readb(LSIState *s, int offset);
static void lsi_reg_writeb(LSIState *s, int offset, uint8_t val);
static void lsi_execute_script(LSIState *s);
static void lsi_reselect(LSIState *s, lsi_request *p);

static inline uint32_t read_dword(LSIState *s, uint32_t addr)
{
    uint32_t buf;

    pci_dma_read(PCI_DEVICE(s), addr, &buf, 4);
    return cpu_to_le32(buf);
}

static void lsi_stop_script(LSIState *s)
{
    s->istat1 &= ~LSI_ISTAT1_SRUN;
}

static void lsi_update_irq(LSIState *s)
{
    PCIDevice *d = PCI_DEVICE(s);
    int level;
    static int last_level;
    lsi_request *p;

    /* It's unclear whether the DIP/SIP bits should be cleared when the
       Interrupt Status Registers are cleared or when istat0 is read.
       We currently do the formwer, which seems to work.  */
    level = 0;
    if (s->dstat) {
        if (s->dstat & s->dien)
            level = 1;
        s->istat0 |= LSI_ISTAT0_DIP;
    } else {
        s->istat0 &= ~LSI_ISTAT0_DIP;
    }

    if (s->sist0 || s->sist1) {
        if ((s->sist0 & s->sien0) || (s->sist1 & s->sien1))
            level = 1;
        s->istat0 |= LSI_ISTAT0_SIP;
    } else {
        s->istat0 &= ~LSI_ISTAT0_SIP;
    }
    if (s->istat0 & LSI_ISTAT0_INTF)
        level = 1;

    if (level != last_level) {
        DPRINTF("Update IRQ level %d dstat %02x sist %02x%02x\n",
                level, s->dstat, s->sist1, s->sist0);
        last_level = level;
    }
    pci_set_irq(d, level);

    if (!level && lsi_irq_on_rsl(s) && !(s->scntl1 & LSI_SCNTL1_CON)) {
        DPRINTF("Handled IRQs & disconnected, looking for pending "
                "processes\n");
        QTAILQ_FOREACH(p, &s->queue, next) {
            if (p->pending) {
                lsi_reselect(s, p);
                break;
            }
        }
    }
}

/* Stop SCRIPTS execution and raise a SCSI interrupt.  */
static void lsi_script_scsi_interrupt(LSIState *s, int stat0, int stat1)
{
    uint32_t mask0;
    uint32_t mask1;

    DPRINTF("SCSI Interrupt 0x%02x%02x prev 0x%02x%02x\n",
            stat1, stat0, s->sist1, s->sist0);
    s->sist0 |= stat0;
    s->sist1 |= stat1;
    /* Stop processor on fatal or unmasked interrupt.  As a special hack
       we don't stop processing when raising STO.  Instead continue
       execution and stop at the next insn that accesses the SCSI bus.  */
    mask0 = s->sien0 | ~(LSI_SIST0_CMP | LSI_SIST0_SEL | LSI_SIST0_RSL);
    mask1 = s->sien1 | ~(LSI_SIST1_GEN | LSI_SIST1_HTH);
    mask1 &= ~LSI_SIST1_STO;
    if (s->sist0 & mask0 || s->sist1 & mask1) {
        lsi_stop_script(s);
    }
    lsi_update_irq(s);
}

/* Stop SCRIPTS execution and raise a DMA interrupt.  */
static void lsi_script_dma_interrupt(LSIState *s, int stat)
{
    DPRINTF("DMA Interrupt 0x%x prev 0x%x\n", stat, s->dstat);
    s->dstat |= stat;
    lsi_update_irq(s);
    lsi_stop_script(s);
}

static inline void lsi_set_phase(LSIState *s, int phase)
{
    s->sstat1 = (s->sstat1 & ~PHASE_MASK) | phase;
}

static void lsi_bad_phase(LSIState *s, int out, int new_phase)
{
    /* Trigger a phase mismatch.  */
    if (s->ccntl0 & LSI_CCNTL0_ENPMJ) {
        if ((s->ccntl0 & LSI_CCNTL0_PMJCTL)) {
            s->dsp = out ? s->pmjad1 : s->pmjad2;
        } else {
            s->dsp = (s->scntl2 & LSI_SCNTL2_WSR ? s->pmjad2 : s->pmjad1);
        }
        DPRINTF("Data phase mismatch jump to %08x\n", s->dsp);
    } else {
        DPRINTF("Phase mismatch interrupt\n");
        lsi_script_scsi_interrupt(s, LSI_SIST0_MA, 0);
        lsi_stop_script(s);
    }
    lsi_set_phase(s, new_phase);
}


/* Resume SCRIPTS execution after a DMA operation.  */
static void lsi_resume_script(LSIState *s)
{
    if (s->waiting != 2) {
        s->waiting = 0;
        lsi_execute_script(s);
    } else {
        s->waiting = 0;
    }
}

static void lsi_disconnect(LSIState *s)
{
    s->scntl1 &= ~LSI_SCNTL1_CON;
    s->sstat1 &= ~PHASE_MASK;
}

static void lsi_bad_selection(LSIState *s, uint32_t id)
{
    DPRINTF("Selected absent target %d\n", id);
    lsi_script_scsi_interrupt(s, 0, LSI_SIST1_STO);
    lsi_disconnect(s);
}

/* Initiate a SCSI layer data transfer.  */
static void lsi_do_dma(LSIState *s, int out)
{
    PCIDevice *pci_dev;
    uint32_t count;
    dma_addr_t addr;
    SCSIDevice *dev;

    assert(s->current);
    if (!s->current->dma_len) {
        /* Wait until data is available.  */
        DPRINTF("DMA no data available\n");
        return;
    }

    pci_dev = PCI_DEVICE(s);
    dev = s->current->req->dev;
    assert(dev);

    count = s->dbc;
    if (count > s->current->dma_len)
        count = s->current->dma_len;

    addr = s->dnad;
    /* both 40 and Table Indirect 64-bit DMAs store upper bits in dnad64 */
    if (lsi_dma_40bit(s) || lsi_dma_ti64bit(s))
        addr |= ((uint64_t)s->dnad64 << 32);
    else if (s->dbms)
        addr |= ((uint64_t)s->dbms << 32);
    else if (s->sbms)
        addr |= ((uint64_t)s->sbms << 32);

    DPRINTF("DMA addr=0x" DMA_ADDR_FMT " len=%d\n", addr, count);
    s->csbc += count;
    s->dnad += count;
    s->dbc -= count;
     if (s->current->dma_buf == NULL) {
        s->current->dma_buf = scsi_req_get_buf(s->current->req);
    }
    /* ??? Set SFBR to first data byte.  */
    if (out) {
        pci_dma_read(pci_dev, addr, s->current->dma_buf, count);
    } else {
        pci_dma_write(pci_dev, addr, s->current->dma_buf, count);
    }
    s->current->dma_len -= count;
    if (s->current->dma_len == 0) {
        s->current->dma_buf = NULL;
        scsi_req_continue(s->current->req);
    } else {
        s->current->dma_buf += count;
        lsi_resume_script(s);
    }
}


/* Add a command to the queue.  */
static void lsi_queue_command(LSIState *s)
{
    lsi_request *p = s->current;

    DPRINTF("Queueing tag=0x%x\n", p->tag);
    assert(s->current != NULL);
    assert(s->current->dma_len == 0);
    QTAILQ_INSERT_TAIL(&s->queue, s->current, next);
    s->current = NULL;

    p->pending = 0;
    p->out = (s->sstat1 & PHASE_MASK) == PHASE_DO;
}

/* Queue a byte for a MSG IN phase.  */
static void lsi_add_msg_byte(LSIState *s, uint8_t data)
{
    if (s->msg_len >= LSI_MAX_MSGIN_LEN) {
        BADF("MSG IN data too long\n");
    } else {
        DPRINTF("MSG IN 0x%02x\n", data);
        s->msg[s->msg_len++] = data;
    }
}

/* Perform reselection to continue a command.  */
static void lsi_reselect(LSIState *s, lsi_request *p)
{
    int id;

    assert(s->current == NULL);
    QTAILQ_REMOVE(&s->queue, p, next);
    s->current = p;

    id = (p->tag >> 8) & 0xf;
    s->ssid = id | 0x80;
    /* LSI53C700 Family Compatibility, see LSI53C895A 4-73 */
    if (!(s->dcntl & LSI_DCNTL_COM)) {
        s->sfbr = 1 << (id & 0x7);
    }
    DPRINTF("Reselected target %d\n", id);
    s->scntl1 |= LSI_SCNTL1_CON;
    lsi_set_phase(s, PHASE_MI);
    s->msg_action = p->out ? 2 : 3;
    s->current->dma_len = p->pending;
    lsi_add_msg_byte(s, 0x80);
    if (s->current->tag & LSI_TAG_VALID) {
        lsi_add_msg_byte(s, 0x20);
        lsi_add_msg_byte(s, p->tag & 0xff);
    }

    if (lsi_irq_on_rsl(s)) {
        lsi_script_scsi_interrupt(s, LSI_SIST0_RSL, 0);
    }
}

static lsi_request *lsi_find_by_tag(LSIState *s, uint32_t tag)
{
    lsi_request *p;

    QTAILQ_FOREACH(p, &s->queue, next) {
        if (p->tag == tag) {
            return p;
        }
    }

    return NULL;
}

static void lsi_request_free(LSIState *s, lsi_request *p)
{
    if (p == s->current) {
        s->current = NULL;
    } else {
        QTAILQ_REMOVE(&s->queue, p, next);
    }
    g_free(p);
}

static void lsi_request_cancelled(SCSIRequest *req)
{
    LSIState *s = LSI53C895A(req->bus->qbus.parent);
    lsi_request *p = req->hba_private;

    req->hba_private = NULL;
    lsi_request_free(s, p);
    scsi_req_unref(req);
}

/* Record that data is available for a queued command.  Returns zero if
   the device was reselected, nonzero if the IO is deferred.  */
static int lsi_queue_req(LSIState *s, SCSIRequest *req, uint32_t len)
{
    lsi_request *p = req->hba_private;

    if (p->pending) {
        BADF("Multiple IO pending for request %p\n", p);
    }
    p->pending = len;
    /* Reselect if waiting for it, or if reselection triggers an IRQ
       and the bus is free.
       Since no interrupt stacking is implemented in the emulation, it
       is also required that there are no pending interrupts waiting
       for service from the device driver. */
    if (s->waiting == 1 ||
        (lsi_irq_on_rsl(s) && !(s->scntl1 & LSI_SCNTL1_CON) &&
         !(s->istat0 & (LSI_ISTAT0_SIP | LSI_ISTAT0_DIP)))) {
        /* Reselect device.  */
        lsi_reselect(s, p);
        return 0;
    } else {
        DPRINTF("Queueing IO tag=0x%x\n", p->tag);
        p->pending = len;
        return 1;
    }
}

 /* Callback to indicate that the SCSI layer has completed a command.  */
static void lsi_command_complete(SCSIRequest *req, uint32_t status, size_t resid)
{
    LSIState *s = LSI53C895A(req->bus->qbus.parent);
    int out;

    out = (s->sstat1 & PHASE_MASK) == PHASE_DO;
    DPRINTF("Command complete status=%d\n", (int)status);
    s->status = status;
    s->command_complete = 2;
    if (s->waiting && s->dbc != 0) {
        /* Raise phase mismatch for short transfers.  */
        lsi_bad_phase(s, out, PHASE_ST);
    } else {
        lsi_set_phase(s, PHASE_ST);
    }

    if (req->hba_private == s->current) {
        req->hba_private = NULL;
        lsi_request_free(s, s->current);
        scsi_req_unref(req);
    }
    lsi_resume_script(s);
}

 /* Callback to indicate that the SCSI layer has completed a transfer.  */
static void lsi_transfer_data(SCSIRequest *req, uint32_t len)
{
    LSIState *s = LSI53C895A(req->bus->qbus.parent);
    int out;

    assert(req->hba_private);
    if (s->waiting == 1 || req->hba_private != s->current ||
        (lsi_irq_on_rsl(s) && !(s->scntl1 & LSI_SCNTL1_CON))) {
        if (lsi_queue_req(s, req, len)) {
            return;
        }
    }

    out = (s->sstat1 & PHASE_MASK) == PHASE_DO;

    /* host adapter (re)connected */
    DPRINTF("Data ready tag=0x%x len=%d\n", req->tag, len);
    s->current->dma_len = len;
    s->command_complete = 1;
    if (s->waiting) {
        if (s->waiting == 1 || s->dbc == 0) {
            lsi_resume_script(s);
        } else {
            lsi_do_dma(s, out);
        }
    }
}

static void lsi_do_command(LSIState *s)
{
    SCSIDevice *dev;
    uint8_t buf[16];
    uint32_t id;
    int n;

    DPRINTF("Send command len=%d\n", s->dbc);
    if (s->dbc > 16)
        s->dbc = 16;
    pci_dma_read(PCI_DEVICE(s), s->dnad, buf, s->dbc);
    s->sfbr = buf[0];
    s->command_complete = 0;

    id = (s->select_tag >> 8) & 0xf;
    dev = scsi_device_find(&s->bus, 0, id, s->current_lun);
    if (!dev) {
        lsi_bad_selection(s, id);
        return;
    }

    assert(s->current == NULL);
    s->current = g_malloc0(sizeof(lsi_request));
    s->current->tag = s->select_tag;
    s->current->req = scsi_req_new(dev, s->current->tag, s->current_lun, buf,
                                   s->current);

    n = scsi_req_enqueue(s->current->req);
    if (n) {
        if (n > 0) {
            lsi_set_phase(s, PHASE_DI);
        } else if (n < 0) {
            lsi_set_phase(s, PHASE_DO);
        }
        scsi_req_continue(s->current->req);
    }
    if (!s->command_complete) {
        if (n) {
            /* Command did not complete immediately so disconnect.  */
            lsi_add_msg_byte(s, 2); /* SAVE DATA POINTER */
            lsi_add_msg_byte(s, 4); /* DISCONNECT */
            /* wait data */
            lsi_set_phase(s, PHASE_MI);
            s->msg_action = 1;
            lsi_queue_command(s);
        } else {
            /* wait command complete */
            lsi_set_phase(s, PHASE_DI);
        }
    }
}

static void lsi_do_status(LSIState *s)
{
    uint8_t status;
    DPRINTF("Get status len=%d status=%d\n", s->dbc, s->status);
    if (s->dbc != 1)
        BADF("Bad Status move\n");
    s->dbc = 1;
    status = s->status;
    s->sfbr = status;
    pci_dma_write(PCI_DEVICE(s), s->dnad, &status, 1);
    lsi_set_phase(s, PHASE_MI);
    s->msg_action = 1;
    lsi_add_msg_byte(s, 0); /* COMMAND COMPLETE */
}

static void lsi_do_msgin(LSIState *s)
{
    int len;
    DPRINTF("Message in len=%d/%d\n", s->dbc, s->msg_len);
    s->sfbr = s->msg[0];
    len = s->msg_len;
    if (len > s->dbc)
        len = s->dbc;
    pci_dma_write(PCI_DEVICE(s), s->dnad, s->msg, len);
    /* Linux drivers rely on the last byte being in the SIDL.  */
    s->sidl = s->msg[len - 1];
    s->msg_len -= len;
    if (s->msg_len) {
        memmove(s->msg, s->msg + len, s->msg_len);
    } else {
        /* ??? Check if ATN (not yet implemented) is asserted and maybe
           switch to PHASE_MO.  */
        switch (s->msg_action) {
        case 0:
            lsi_set_phase(s, PHASE_CMD);
            break;
        case 1:
            lsi_disconnect(s);
            break;
        case 2:
            lsi_set_phase(s, PHASE_DO);
            break;
        case 3:
            lsi_set_phase(s, PHASE_DI);
            break;
        default:
            abort();
        }
    }
}

/* Read the next byte during a MSGOUT phase.  */
static uint8_t lsi_get_msgbyte(LSIState *s)
{
    uint8_t data;
    pci_dma_read(PCI_DEVICE(s), s->dnad, &data, 1);
    s->dnad++;
    s->dbc--;
    return data;
}

/* Skip the next n bytes during a MSGOUT phase. */
static void lsi_skip_msgbytes(LSIState *s, unsigned int n)
{
    s->dnad += n;
    s->dbc  -= n;
}

static void lsi_do_msgout(LSIState *s)
{
    uint8_t msg;
    int len;
    uint32_t current_tag;
    lsi_request *current_req, *p, *p_next;

    if (s->current) {
        current_tag = s->current->tag;
        current_req = s->current;
    } else {
        current_tag = s->select_tag;
        current_req = lsi_find_by_tag(s, current_tag);
    }

    DPRINTF("MSG out len=%d\n", s->dbc);
    while (s->dbc) {
        msg = lsi_get_msgbyte(s);
        s->sfbr = msg;

        switch (msg) {
        case 0x04:
            DPRINTF("MSG: Disconnect\n");
            lsi_disconnect(s);
            break;
        case 0x08:
            DPRINTF("MSG: No Operation\n");
            lsi_set_phase(s, PHASE_CMD);
            break;
        case 0x01:
            len = lsi_get_msgbyte(s);
            msg = lsi_get_msgbyte(s);
            (void)len; /* avoid a warning about unused variable*/
            DPRINTF("Extended message 0x%x (len %d)\n", msg, len);
            switch (msg) {
            case 1:
                DPRINTF("SDTR (ignored)\n");
                lsi_skip_msgbytes(s, 2);
                break;
            case 3:
                DPRINTF("WDTR (ignored)\n");
                lsi_skip_msgbytes(s, 1);
                break;
            default:
                goto bad;
            }
            break;
        case 0x20: /* SIMPLE queue */
            s->select_tag |= lsi_get_msgbyte(s) | LSI_TAG_VALID;
            DPRINTF("SIMPLE queue tag=0x%x\n", s->select_tag & 0xff);
            break;
        case 0x21: /* HEAD of queue */
            BADF("HEAD queue not implemented\n");
            s->select_tag |= lsi_get_msgbyte(s) | LSI_TAG_VALID;
            break;
        case 0x22: /* ORDERED queue */
            BADF("ORDERED queue not implemented\n");
            s->select_tag |= lsi_get_msgbyte(s) | LSI_TAG_VALID;
            break;
        case 0x0d:
            /* The ABORT TAG message clears the current I/O process only. */
            DPRINTF("MSG: ABORT TAG tag=0x%x\n", current_tag);
            if (current_req) {
                scsi_req_cancel(current_req->req);
            }
            lsi_disconnect(s);
            break;
        case 0x06:
        case 0x0e:
        case 0x0c:
            /* The ABORT message clears all I/O processes for the selecting
               initiator on the specified logical unit of the target. */
            if (msg == 0x06) {
                DPRINTF("MSG: ABORT tag=0x%x\n", current_tag);
            }
            /* The CLEAR QUEUE message clears all I/O processes for all
               initiators on the specified logical unit of the target. */
            if (msg == 0x0e) {
                DPRINTF("MSG: CLEAR QUEUE tag=0x%x\n", current_tag);
            }
            /* The BUS DEVICE RESET message clears all I/O processes for all
               initiators on all logical units of the target. */
            if (msg == 0x0c) {
                DPRINTF("MSG: BUS DEVICE RESET tag=0x%x\n", current_tag);
            }

            /* clear the current I/O process */
            if (s->current) {
                scsi_req_cancel(s->current->req);
            }

            /* As the current implemented devices scsi_disk and scsi_generic
               only support one LUN, we don't need to keep track of LUNs.
               Clearing I/O processes for other initiators could be possible
               for scsi_generic by sending a SG_SCSI_RESET to the /dev/sgX
               device, but this is currently not implemented (and seems not
               to be really necessary). So let's simply clear all queued
               commands for the current device: */
            QTAILQ_FOREACH_SAFE(p, &s->queue, next, p_next) {
                if ((p->tag & 0x0000ff00) == (current_tag & 0x0000ff00)) {
                    scsi_req_cancel(p->req);
                }
            }

            lsi_disconnect(s);
            break;
        default:
            if ((msg & 0x80) == 0) {
                goto bad;
            }
            s->current_lun = msg & 7;
            DPRINTF("Select LUN %d\n", s->current_lun);
            lsi_set_phase(s, PHASE_CMD);
            break;
        }
    }
    return;
bad:
    BADF("Unimplemented message 0x%02x\n", msg);
    lsi_set_phase(s, PHASE_MI);
    lsi_add_msg_byte(s, 7); /* MESSAGE REJECT */
    s->msg_action = 0;
}

#define LSI_BUF_SIZE 4096
static void lsi_memcpy(LSIState *s, uint32_t dest, uint32_t src, int count)
{
    PCIDevice *d = PCI_DEVICE(s);
    int n;
    uint8_t buf[LSI_BUF_SIZE];

    DPRINTF("memcpy dest 0x%08x src 0x%08x count %d\n", dest, src, count);
    while (count) {
        n = (count > LSI_BUF_SIZE) ? LSI_BUF_SIZE : count;
        pci_dma_read(d, src, buf, n);
        pci_dma_write(d, dest, buf, n);
        src += n;
        dest += n;
        count -= n;
    }
}

static void lsi_wait_reselect(LSIState *s)
{
    lsi_request *p;

    DPRINTF("Wait Reselect\n");

    QTAILQ_FOREACH(p, &s->queue, next) {
        if (p->pending) {
            lsi_reselect(s, p);
            break;
        }
    }
    if (s->current == NULL) {
        s->waiting = 1;
    }
}

static void lsi_execute_script(LSIState *s)
{
    PCIDevice *pci_dev = PCI_DEVICE(s);
    uint32_t insn;
    uint32_t addr, addr_high;
    int opcode;
    int insn_processed = 0;

    s->istat1 |= LSI_ISTAT1_SRUN;
again:
    insn_processed++;
    insn = read_dword(s, s->dsp);
    if (!insn) {
        /* If we receive an empty opcode increment the DSP by 4 bytes
           instead of 8 and execute the next opcode at that location */
        s->dsp += 4;
        goto again;
    }
    addr = read_dword(s, s->dsp + 4);
    addr_high = 0;
    DPRINTF("SCRIPTS dsp=%08x opcode %08x arg %08x\n", s->dsp, insn, addr);
    s->dsps = addr;
    s->dcmd = insn >> 24;
    s->dsp += 8;
    switch (insn >> 30) {
    case 0: /* Block move.  */
        if (s->sist1 & LSI_SIST1_STO) {
            DPRINTF("Delayed select timeout\n");
            lsi_stop_script(s);
            break;
        }
        s->dbc = insn & 0xffffff;
        s->rbc = s->dbc;
        /* ??? Set ESA.  */
        s->ia = s->dsp - 8;
        if (insn & (1 << 29)) {
            /* Indirect addressing.  */
            addr = read_dword(s, addr);
        } else if (insn & (1 << 28)) {
            uint32_t buf[2];
            int32_t offset;
            /* Table indirect addressing.  */

            /* 32-bit Table indirect */
            offset = sextract32(addr, 0, 24);
            pci_dma_read(pci_dev, s->dsa + offset, buf, 8);
            /* byte count is stored in bits 0:23 only */
            s->dbc = cpu_to_le32(buf[0]) & 0xffffff;
            s->rbc = s->dbc;
            addr = cpu_to_le32(buf[1]);

            /* 40-bit DMA, upper addr bits [39:32] stored in first DWORD of
             * table, bits [31:24] */
            if (lsi_dma_40bit(s))
                addr_high = cpu_to_le32(buf[0]) >> 24;
            else if (lsi_dma_ti64bit(s)) {
                int selector = (cpu_to_le32(buf[0]) >> 24) & 0x1f;
                switch (selector) {
                case 0 ... 0x0f:
                    /* offset index into scratch registers since
                     * TI64 mode can use registers C to R */
                    addr_high = s->scratch[2 + selector];
                    break;
                case 0x10:
                    addr_high = s->mmrs;
                    break;
                case 0x11:
                    addr_high = s->mmws;
                    break;
                case 0x12:
                    addr_high = s->sfs;
                    break;
                case 0x13:
                    addr_high = s->drs;
                    break;
                case 0x14:
                    addr_high = s->sbms;
                    break;
                case 0x15:
                    addr_high = s->dbms;
                    break;
                default:
                    BADF("Illegal selector specified (0x%x > 0x15)"
                         " for 64-bit DMA block move", selector);
                    break;
                }
            }
        } else if (lsi_dma_64bit(s)) {
            /* fetch a 3rd dword if 64-bit direct move is enabled and
               only if we're not doing table indirect or indirect addressing */
            s->dbms = read_dword(s, s->dsp);
            s->dsp += 4;
            s->ia = s->dsp - 12;
        }
        if ((s->sstat1 & PHASE_MASK) != ((insn >> 24) & 7)) {
            DPRINTF("Wrong phase got %d expected %d\n",
                    s->sstat1 & PHASE_MASK, (insn >> 24) & 7);
            lsi_script_scsi_interrupt(s, LSI_SIST0_MA, 0);
            break;
        }
        s->dnad = addr;
        s->dnad64 = addr_high;
        switch (s->sstat1 & 0x7) {
        case PHASE_DO:
            s->waiting = 2;
            lsi_do_dma(s, 1);
            if (s->waiting)
                s->waiting = 3;
            break;
        case PHASE_DI:
            s->waiting = 2;
            lsi_do_dma(s, 0);
            if (s->waiting)
                s->waiting = 3;
            break;
        case PHASE_CMD:
            lsi_do_command(s);
            break;
        case PHASE_ST:
            lsi_do_status(s);
            break;
        case PHASE_MO:
            lsi_do_msgout(s);
            break;
        case PHASE_MI:
            lsi_do_msgin(s);
            break;
        default:
            BADF("Unimplemented phase %d\n", s->sstat1 & PHASE_MASK);
            exit(1);
        }
        s->dfifo = s->dbc & 0xff;
        s->ctest5 = (s->ctest5 & 0xfc) | ((s->dbc >> 8) & 3);
        s->sbc = s->dbc;
        s->rbc -= s->dbc;
        s->ua = addr + s->dbc;
        break;

    case 1: /* IO or Read/Write instruction.  */
        opcode = (insn >> 27) & 7;
        if (opcode < 5) {
            uint32_t id;

            if (insn & (1 << 25)) {
                id = read_dword(s, s->dsa + sextract32(insn, 0, 24));
            } else {
                id = insn;
            }
            id = (id >> 16) & 0xf;
            if (insn & (1 << 26)) {
                addr = s->dsp + sextract32(addr, 0, 24);
            }
            s->dnad = addr;
            switch (opcode) {
            case 0: /* Select */
                s->sdid = id;
                if (s->scntl1 & LSI_SCNTL1_CON) {
                    DPRINTF("Already reselected, jumping to alternative address\n");
                    s->dsp = s->dnad;
                    break;
                }
                s->sstat0 |= LSI_SSTAT0_WOA;
                s->scntl1 &= ~LSI_SCNTL1_IARB;
                if (!scsi_device_find(&s->bus, 0, id, 0)) {
                    lsi_bad_selection(s, id);
                    break;
                }
                DPRINTF("Selected target %d%s\n",
                        id, insn & (1 << 3) ? " ATN" : "");
                /* ??? Linux drivers compain when this is set.  Maybe
                   it only applies in low-level mode (unimplemented).
                lsi_script_scsi_interrupt(s, LSI_SIST0_CMP, 0); */
                s->select_tag = id << 8;
                s->scntl1 |= LSI_SCNTL1_CON;
                if (insn & (1 << 3)) {
                    s->socl |= LSI_SOCL_ATN;
                }
                lsi_set_phase(s, PHASE_MO);
                break;
            case 1: /* Disconnect */
                DPRINTF("Wait Disconnect\n");
                s->scntl1 &= ~LSI_SCNTL1_CON;
                break;
            case 2: /* Wait Reselect */
                if (!lsi_irq_on_rsl(s)) {
                    lsi_wait_reselect(s);
                }
                break;
            case 3: /* Set */
                DPRINTF("Set%s%s%s%s\n",
                        insn & (1 << 3) ? " ATN" : "",
                        insn & (1 << 6) ? " ACK" : "",
                        insn & (1 << 9) ? " TM" : "",
                        insn & (1 << 10) ? " CC" : "");
                if (insn & (1 << 3)) {
                    s->socl |= LSI_SOCL_ATN;
                    lsi_set_phase(s, PHASE_MO);
                }
                if (insn & (1 << 9)) {
                    BADF("Target mode not implemented\n");
                    exit(1);
                }
                if (insn & (1 << 10))
                    s->carry = 1;
                break;
            case 4: /* Clear */
                DPRINTF("Clear%s%s%s%s\n",
                        insn & (1 << 3) ? " ATN" : "",
                        insn & (1 << 6) ? " ACK" : "",
                        insn & (1 << 9) ? " TM" : "",
                        insn & (1 << 10) ? " CC" : "");
                if (insn & (1 << 3)) {
                    s->socl &= ~LSI_SOCL_ATN;
                }
                if (insn & (1 << 10))
                    s->carry = 0;
                break;
            }
        } else {
            uint8_t op0;
            uint8_t op1;
            uint8_t data8;
            int reg;
            int operator;
#ifdef DEBUG_LSI
            static const char *opcode_names[3] =
                {"Write", "Read", "Read-Modify-Write"};
            static const char *operator_names[8] =
                {"MOV", "SHL", "OR", "XOR", "AND", "SHR", "ADD", "ADC"};
#endif

            reg = ((insn >> 16) & 0x7f) | (insn & 0x80);
            data8 = (insn >> 8) & 0xff;
            opcode = (insn >> 27) & 7;
            operator = (insn >> 24) & 7;
            DPRINTF("%s reg 0x%x %s data8=0x%02x sfbr=0x%02x%s\n",
                    opcode_names[opcode - 5], reg,
                    operator_names[operator], data8, s->sfbr,
                    (insn & (1 << 23)) ? " SFBR" : "");
            op0 = op1 = 0;
            switch (opcode) {
            case 5: /* From SFBR */
                op0 = s->sfbr;
                op1 = data8;
                break;
            case 6: /* To SFBR */
                if (operator)
                    op0 = lsi_reg_readb(s, reg);
                op1 = data8;
                break;
            case 7: /* Read-modify-write */
                if (operator)
                    op0 = lsi_reg_readb(s, reg);
                if (insn & (1 << 23)) {
                    op1 = s->sfbr;
                } else {
                    op1 = data8;
                }
                break;
            }

            switch (operator) {
            case 0: /* move */
                op0 = op1;
                break;
            case 1: /* Shift left */
                op1 = op0 >> 7;
                op0 = (op0 << 1) | s->carry;
                s->carry = op1;
                break;
            case 2: /* OR */
                op0 |= op1;
                break;
            case 3: /* XOR */
                op0 ^= op1;
                break;
            case 4: /* AND */
                op0 &= op1;
                break;
            case 5: /* SHR */
                op1 = op0 & 1;
                op0 = (op0 >> 1) | (s->carry << 7);
                s->carry = op1;
                break;
            case 6: /* ADD */
                op0 += op1;
                s->carry = op0 < op1;
                break;
            case 7: /* ADC */
                op0 += op1 + s->carry;
                if (s->carry)
                    s->carry = op0 <= op1;
                else
                    s->carry = op0 < op1;
                break;
            }

            switch (opcode) {
            case 5: /* From SFBR */
            case 7: /* Read-modify-write */
                lsi_reg_writeb(s, reg, op0);
                break;
            case 6: /* To SFBR */
                s->sfbr = op0;
                break;
            }
        }
        break;

    case 2: /* Transfer Control.  */
        {
            int cond;
            int jmp;

            if ((insn & 0x002e0000) == 0) {
                DPRINTF("NOP\n");
                break;
            }
            if (s->sist1 & LSI_SIST1_STO) {
                DPRINTF("Delayed select timeout\n");
                lsi_stop_script(s);
                break;
            }
            cond = jmp = (insn & (1 << 19)) != 0;
            if (cond == jmp && (insn & (1 << 21))) {
                DPRINTF("Compare carry %d\n", s->carry == jmp);
                cond = s->carry != 0;
            }
            if (cond == jmp && (insn & (1 << 17))) {
                DPRINTF("Compare phase %d %c= %d\n",
                        (s->sstat1 & PHASE_MASK),
                        jmp ? '=' : '!',
                        ((insn >> 24) & 7));
                cond = (s->sstat1 & PHASE_MASK) == ((insn >> 24) & 7);
            }
            if (cond == jmp && (insn & (1 << 18))) {
                uint8_t mask;

                mask = (~insn >> 8) & 0xff;
                DPRINTF("Compare data 0x%x & 0x%x %c= 0x%x\n",
                        s->sfbr, mask, jmp ? '=' : '!', insn & mask);
                cond = (s->sfbr & mask) == (insn & mask);
            }
            if (cond == jmp) {
                if (insn & (1 << 23)) {
                    /* Relative address.  */
                    addr = s->dsp + sextract32(addr, 0, 24);
                }
                switch ((insn >> 27) & 7) {
                case 0: /* Jump */
                    DPRINTF("Jump to 0x%08x\n", addr);
                    s->dsp = addr;
                    break;
                case 1: /* Call */
                    DPRINTF("Call 0x%08x\n", addr);
                    s->temp = s->dsp;
                    s->dsp = addr;
                    break;
                case 2: /* Return */
                    DPRINTF("Return to 0x%08x\n", s->temp);
                    s->dsp = s->temp;
                    break;
                case 3: /* Interrupt */
                    DPRINTF("Interrupt 0x%08x\n", s->dsps);
                    if ((insn & (1 << 20)) != 0) {
                        s->istat0 |= LSI_ISTAT0_INTF;
                        lsi_update_irq(s);
                    } else {
                        lsi_script_dma_interrupt(s, LSI_DSTAT_SIR);
                    }
                    break;
                default:
                    DPRINTF("Illegal transfer control\n");
                    lsi_script_dma_interrupt(s, LSI_DSTAT_IID);
                    break;
                }
            } else {
                DPRINTF("Control condition failed\n");
            }
        }
        break;

    case 3:
        if ((insn & (1 << 29)) == 0) {
            /* Memory move.  */
            uint32_t dest;
            /* ??? The docs imply the destination address is loaded into
               the TEMP register.  However the Linux drivers rely on
               the value being presrved.  */
            dest = read_dword(s, s->dsp);
            s->dsp += 4;
            lsi_memcpy(s, dest, addr, insn & 0xffffff);
        } else {
            uint8_t data[7];
            int reg;
            int n;
            int i;

            if (insn & (1 << 28)) {
                addr = s->dsa + sextract32(addr, 0, 24);
            }
            n = (insn & 7);
            reg = (insn >> 16) & 0xff;
            if (insn & (1 << 24)) {
                pci_dma_read(pci_dev, addr, data, n);
                DPRINTF("Load reg 0x%x size %d addr 0x%08x = %08x\n", reg, n,
                        addr, *(int *)data);
                for (i = 0; i < n; i++) {
                    lsi_reg_writeb(s, reg + i, data[i]);
                }
            } else {
                DPRINTF("Store reg 0x%x size %d addr 0x%08x\n", reg, n, addr);
                for (i = 0; i < n; i++) {
                    data[i] = lsi_reg_readb(s, reg + i);
                }
                pci_dma_write(pci_dev, addr, data, n);
            }
        }
    }
    if (insn_processed > 10000 && !s->waiting) {
        /* Some windows drivers make the device spin waiting for a memory
           location to change.  If we have been executed a lot of code then
           assume this is the case and force an unexpected device disconnect.
           This is apparently sufficient to beat the drivers into submission.
         */
        if (!(s->sien0 & LSI_SIST0_UDC))
            fprintf(stderr, "inf. loop with UDC masked\n");
        lsi_script_scsi_interrupt(s, LSI_SIST0_UDC, 0);
        lsi_disconnect(s);
    } else if (s->istat1 & LSI_ISTAT1_SRUN && !s->waiting) {
        if (s->dcntl & LSI_DCNTL_SSM) {
            lsi_script_dma_interrupt(s, LSI_DSTAT_SSI);
        } else {
            goto again;
        }
    }
    DPRINTF("SCRIPTS execution stopped\n");
}

static uint8_t lsi_reg_readb(LSIState *s, int offset)
{
    uint8_t tmp;
#define CASE_GET_REG24(name, addr) \
    case addr: return s->name & 0xff; \
    case addr + 1: return (s->name >> 8) & 0xff; \
    case addr + 2: return (s->name >> 16) & 0xff;

#define CASE_GET_REG32(name, addr) \
    case addr: return s->name & 0xff; \
    case addr + 1: return (s->name >> 8) & 0xff; \
    case addr + 2: return (s->name >> 16) & 0xff; \
    case addr + 3: return (s->name >> 24) & 0xff;

#ifdef DEBUG_LSI_REG
    DPRINTF("Read reg %x\n", offset);
#endif
    switch (offset) {
    case 0x00: /* SCNTL0 */
        return s->scntl0;
    case 0x01: /* SCNTL1 */
        return s->scntl1;
    case 0x02: /* SCNTL2 */
        return s->scntl2;
    case 0x03: /* SCNTL3 */
        return s->scntl3;
    case 0x04: /* SCID */
        return s->scid;
    case 0x05: /* SXFER */
        return s->sxfer;
    case 0x06: /* SDID */
        return s->sdid;
    case 0x07: /* GPREG0 */
        return 0x7f;
    case 0x08: /* Revision ID */
        return 0x00;
    case 0xa: /* SSID */
        return s->ssid;
    case 0xb: /* SBCL */
        /* ??? This is not correct. However it's (hopefully) only
           used for diagnostics, so should be ok.  */
        return 0;
    case 0xc: /* DSTAT */
        tmp = s->dstat | LSI_DSTAT_DFE;
        if ((s->istat0 & LSI_ISTAT0_INTF) == 0)
            s->dstat = 0;
        lsi_update_irq(s);
        return tmp;
    case 0x0d: /* SSTAT0 */
        return s->sstat0;
    case 0x0e: /* SSTAT1 */
        return s->sstat1;
    case 0x0f: /* SSTAT2 */
        return s->scntl1 & LSI_SCNTL1_CON ? 0 : 2;
    CASE_GET_REG32(dsa, 0x10)
    case 0x14: /* ISTAT0 */
        return s->istat0;
    case 0x15: /* ISTAT1 */
        return s->istat1;
    case 0x16: /* MBOX0 */
        return s->mbox0;
    case 0x17: /* MBOX1 */
        return s->mbox1;
    case 0x18: /* CTEST0 */
        return 0xff;
    case 0x19: /* CTEST1 */
        return 0;
    case 0x1a: /* CTEST2 */
        tmp = s->ctest2 | LSI_CTEST2_DACK | LSI_CTEST2_CM;
        if (s->istat0 & LSI_ISTAT0_SIGP) {
            s->istat0 &= ~LSI_ISTAT0_SIGP;
            tmp |= LSI_CTEST2_SIGP;
        }
        return tmp;
    case 0x1b: /* CTEST3 */
        return s->ctest3;
    CASE_GET_REG32(temp, 0x1c)
    case 0x20: /* DFIFO */
        return 0;
    case 0x21: /* CTEST4 */
        return s->ctest4;
    case 0x22: /* CTEST5 */
        return s->ctest5;
    case 0x23: /* CTEST6 */
         return 0;
    CASE_GET_REG24(dbc, 0x24)
    case 0x27: /* DCMD */
        return s->dcmd;
    CASE_GET_REG32(dnad, 0x28)
    CASE_GET_REG32(dsp, 0x2c)
    CASE_GET_REG32(dsps, 0x30)
    CASE_GET_REG32(scratch[0], 0x34)
    case 0x38: /* DMODE */
        return s->dmode;
    case 0x39: /* DIEN */
        return s->dien;
    case 0x3a: /* SBR */
        return s->sbr;
    case 0x3b: /* DCNTL */
        return s->dcntl;
    case 0x40: /* SIEN0 */
        return s->sien0;
    case 0x41: /* SIEN1 */
        return s->sien1;
    case 0x42: /* SIST0 */
        tmp = s->sist0;
        s->sist0 = 0;
        lsi_update_irq(s);
        return tmp;
    case 0x43: /* SIST1 */
        tmp = s->sist1;
        s->sist1 = 0;
        lsi_update_irq(s);
        return tmp;
    case 0x46: /* MACNTL */
        return 0x0f;
    case 0x47: /* GPCNTL0 */
        return 0x0f;
    case 0x48: /* STIME0 */
        return s->stime0;
    case 0x4a: /* RESPID0 */
        return s->respid0;
    case 0x4b: /* RESPID1 */
        return s->respid1;
    case 0x4d: /* STEST1 */
        return s->stest1;
    case 0x4e: /* STEST2 */
        return s->stest2;
    case 0x4f: /* STEST3 */
        return s->stest3;
    case 0x50: /* SIDL */
        /* This is needed by the linux drivers.  We currently only update it
           during the MSG IN phase.  */
        return s->sidl;
    case 0x52: /* STEST4 */
        return 0xe0;
    case 0x56: /* CCNTL0 */
        return s->ccntl0;
    case 0x57: /* CCNTL1 */
        return s->ccntl1;
    case 0x58: /* SBDL */
        /* Some drivers peek at the data bus during the MSG IN phase.  */
        if ((s->sstat1 & PHASE_MASK) == PHASE_MI)
            return s->msg[0];
        return 0;
    case 0x59: /* SBDL high */
        return 0;
    CASE_GET_REG32(mmrs, 0xa0)
    CASE_GET_REG32(mmws, 0xa4)
    CASE_GET_REG32(sfs, 0xa8)
    CASE_GET_REG32(drs, 0xac)
    CASE_GET_REG32(sbms, 0xb0)
    CASE_GET_REG32(dbms, 0xb4)
    CASE_GET_REG32(dnad64, 0xb8)
    CASE_GET_REG32(pmjad1, 0xc0)
    CASE_GET_REG32(pmjad2, 0xc4)
    CASE_GET_REG32(rbc, 0xc8)
    CASE_GET_REG32(ua, 0xcc)
    CASE_GET_REG32(ia, 0xd4)
    CASE_GET_REG32(sbc, 0xd8)
    CASE_GET_REG32(csbc, 0xdc)
    }
    if (offset >= 0x5c && offset < 0xa0) {
        int n;
        int shift;
        n = (offset - 0x58) >> 2;
        shift = (offset & 3) * 8;
        return (s->scratch[n] >> shift) & 0xff;
    }
    BADF("readb 0x%x\n", offset);
    exit(1);
#undef CASE_GET_REG24
#undef CASE_GET_REG32
}

static void lsi_reg_writeb(LSIState *s, int offset, uint8_t val)
{
#define CASE_SET_REG24(name, addr) \
    case addr    : s->name &= 0xffffff00; s->name |= val;       break; \
    case addr + 1: s->name &= 0xffff00ff; s->name |= val << 8;  break; \
    case addr + 2: s->name &= 0xff00ffff; s->name |= val << 16; break;

#define CASE_SET_REG32(name, addr) \
    case addr    : s->name &= 0xffffff00; s->name |= val;       break; \
    case addr + 1: s->name &= 0xffff00ff; s->name |= val << 8;  break; \
    case addr + 2: s->name &= 0xff00ffff; s->name |= val << 16; break; \
    case addr + 3: s->name &= 0x00ffffff; s->name |= val << 24; break;

#ifdef DEBUG_LSI_REG
    DPRINTF("Write reg %x = %02x\n", offset, val);
#endif
    switch (offset) {
    case 0x00: /* SCNTL0 */
        s->scntl0 = val;
        if (val & LSI_SCNTL0_START) {
            BADF("Start sequence not implemented\n");
        }
        break;
    case 0x01: /* SCNTL1 */
        s->scntl1 = val & ~LSI_SCNTL1_SST;
        if (val & LSI_SCNTL1_IARB) {
            BADF("Immediate Arbritration not implemented\n");
        }
        if (val & LSI_SCNTL1_RST) {
            if (!(s->sstat0 & LSI_SSTAT0_RST)) {
                qbus_reset_all(&s->bus.qbus);
                s->sstat0 |= LSI_SSTAT0_RST;
                lsi_script_scsi_interrupt(s, LSI_SIST0_RST, 0);
            }
        } else {
            s->sstat0 &= ~LSI_SSTAT0_RST;
        }
        break;
    case 0x02: /* SCNTL2 */
        val &= ~(LSI_SCNTL2_WSR | LSI_SCNTL2_WSS);
        s->scntl2 = val;
        break;
    case 0x03: /* SCNTL3 */
        s->scntl3 = val;
        break;
    case 0x04: /* SCID */
        s->scid = val;
        break;
    case 0x05: /* SXFER */
        s->sxfer = val;
        break;
    case 0x06: /* SDID */
        if ((s->ssid & 0x80) && (val & 0xf) != (s->ssid & 0xf)) {
            BADF("Destination ID does not match SSID\n");
        }
        s->sdid = val & 0xf;
        break;
    case 0x07: /* GPREG0 */
        break;
    case 0x08: /* SFBR */
        /* The CPU is not allowed to write to this register.  However the
           SCRIPTS register move instructions are.  */
        s->sfbr = val;
        break;
    case 0x0a: case 0x0b:
        /* Openserver writes to these readonly registers on startup */
	return;
    case 0x0c: case 0x0d: case 0x0e: case 0x0f:
        /* Linux writes to these readonly registers on startup.  */
        return;
    CASE_SET_REG32(dsa, 0x10)
    case 0x14: /* ISTAT0 */
        s->istat0 = (s->istat0 & 0x0f) | (val & 0xf0);
        if (val & LSI_ISTAT0_ABRT) {
            lsi_script_dma_interrupt(s, LSI_DSTAT_ABRT);
        }
        if (val & LSI_ISTAT0_INTF) {
            s->istat0 &= ~LSI_ISTAT0_INTF;
            lsi_update_irq(s);
        }
        if (s->waiting == 1 && val & LSI_ISTAT0_SIGP) {
            DPRINTF("Woken by SIGP\n");
            s->waiting = 0;
            s->dsp = s->dnad;
            lsi_execute_script(s);
        }
        if (val & LSI_ISTAT0_SRST) {
            qdev_reset_all(DEVICE(s));
        }
        break;
    case 0x16: /* MBOX0 */
        s->mbox0 = val;
        break;
    case 0x17: /* MBOX1 */
        s->mbox1 = val;
        break;
    case 0x18: /* CTEST0 */
        /* nothing to do */
        break;
    case 0x1a: /* CTEST2 */
	s->ctest2 = val & LSI_CTEST2_PCICIE;
	break;
    case 0x1b: /* CTEST3 */
        s->ctest3 = val & 0x0f;
        break;
    CASE_SET_REG32(temp, 0x1c)
    case 0x21: /* CTEST4 */
        if (val & 7) {
           BADF("Unimplemented CTEST4-FBL 0x%x\n", val);
        }
        s->ctest4 = val;
        break;
    case 0x22: /* CTEST5 */
        if (val & (LSI_CTEST5_ADCK | LSI_CTEST5_BBCK)) {
            BADF("CTEST5 DMA increment not implemented\n");
        }
        s->ctest5 = val;
        break;
    CASE_SET_REG24(dbc, 0x24)
    CASE_SET_REG32(dnad, 0x28)
    case 0x2c: /* DSP[0:7] */
        s->dsp &= 0xffffff00;
        s->dsp |= val;
        break;
    case 0x2d: /* DSP[8:15] */
        s->dsp &= 0xffff00ff;
        s->dsp |= val << 8;
        break;
    case 0x2e: /* DSP[16:23] */
        s->dsp &= 0xff00ffff;
        s->dsp |= val << 16;
        break;
    case 0x2f: /* DSP[24:31] */
        s->dsp &= 0x00ffffff;
        s->dsp |= val << 24;
        if ((s->dmode & LSI_DMODE_MAN) == 0
            && (s->istat1 & LSI_ISTAT1_SRUN) == 0)
            lsi_execute_script(s);
        break;
    CASE_SET_REG32(dsps, 0x30)
    CASE_SET_REG32(scratch[0], 0x34)
    case 0x38: /* DMODE */
        if (val & (LSI_DMODE_SIOM | LSI_DMODE_DIOM)) {
            BADF("IO mappings not implemented\n");
        }
        s->dmode = val;
        break;
    case 0x39: /* DIEN */
        s->dien = val;
        lsi_update_irq(s);
        break;
    case 0x3a: /* SBR */
        s->sbr = val;
        break;
    case 0x3b: /* DCNTL */
        s->dcntl = val & ~(LSI_DCNTL_PFF | LSI_DCNTL_STD);
        if ((val & LSI_DCNTL_STD) && (s->istat1 & LSI_ISTAT1_SRUN) == 0)
            lsi_execute_script(s);
        break;
    case 0x40: /* SIEN0 */
        s->sien0 = val;
        lsi_update_irq(s);
        break;
    case 0x41: /* SIEN1 */
        s->sien1 = val;
        lsi_update_irq(s);
        break;
    case 0x47: /* GPCNTL0 */
        break;
    case 0x48: /* STIME0 */
        s->stime0 = val;
        break;
    case 0x49: /* STIME1 */
        if (val & 0xf) {
            DPRINTF("General purpose timer not implemented\n");
            /* ??? Raising the interrupt immediately seems to be sufficient
               to keep the FreeBSD driver happy.  */
            lsi_script_scsi_interrupt(s, 0, LSI_SIST1_GEN);
        }
        break;
    case 0x4a: /* RESPID0 */
        s->respid0 = val;
        break;
    case 0x4b: /* RESPID1 */
        s->respid1 = val;
        break;
    case 0x4d: /* STEST1 */
        s->stest1 = val;
        break;
    case 0x4e: /* STEST2 */
        if (val & 1) {
            BADF("Low level mode not implemented\n");
        }
        s->stest2 = val;
        break;
    case 0x4f: /* STEST3 */
        if (val & 0x41) {
            BADF("SCSI FIFO test mode not implemented\n");
        }
        s->stest3 = val;
        break;
    case 0x56: /* CCNTL0 */
        s->ccntl0 = val;
        break;
    case 0x57: /* CCNTL1 */
        s->ccntl1 = val;
        break;
    CASE_SET_REG32(mmrs, 0xa0)
    CASE_SET_REG32(mmws, 0xa4)
    CASE_SET_REG32(sfs, 0xa8)
    CASE_SET_REG32(drs, 0xac)
    CASE_SET_REG32(sbms, 0xb0)
    CASE_SET_REG32(dbms, 0xb4)
    CASE_SET_REG32(dnad64, 0xb8)
    CASE_SET_REG32(pmjad1, 0xc0)
    CASE_SET_REG32(pmjad2, 0xc4)
    CASE_SET_REG32(rbc, 0xc8)
    CASE_SET_REG32(ua, 0xcc)
    CASE_SET_REG32(ia, 0xd4)
    CASE_SET_REG32(sbc, 0xd8)
    CASE_SET_REG32(csbc, 0xdc)
    default:
        if (offset >= 0x5c && offset < 0xa0) {
            int n;
            int shift;
            n = (offset - 0x58) >> 2;
            shift = (offset & 3) * 8;
            s->scratch[n] = deposit32(s->scratch[n], shift, 8, val);
        } else {
            BADF("Unhandled writeb 0x%x = 0x%x\n", offset, val);
        }
    }
#undef CASE_SET_REG24
#undef CASE_SET_REG32
}

static void lsi_mmio_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    LSIState *s = opaque;

    lsi_reg_writeb(s, addr & 0xff, val);
}

static uint64_t lsi_mmio_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    LSIState *s = opaque;

    return lsi_reg_readb(s, addr & 0xff);
}

static const MemoryRegionOps lsi_mmio_ops = {
    .read = lsi_mmio_read,
    .write = lsi_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void lsi_ram_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size)
{
    LSIState *s = opaque;
    uint32_t newval;
    uint32_t mask;
    int shift;

    newval = s->script_ram[addr >> 2];
    shift = (addr & 3) * 8;
    mask = ((uint64_t)1 << (size * 8)) - 1;
    newval &= ~(mask << shift);
    newval |= val << shift;
    s->script_ram[addr >> 2] = newval;
}

static uint64_t lsi_ram_read(void *opaque, hwaddr addr,
                             unsigned size)
{
    LSIState *s = opaque;
    uint32_t val;
    uint32_t mask;

    val = s->script_ram[addr >> 2];
    mask = ((uint64_t)1 << (size * 8)) - 1;
    val >>= (addr & 3) * 8;
    return val & mask;
}

static const MemoryRegionOps lsi_ram_ops = {
    .read = lsi_ram_read,
    .write = lsi_ram_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t lsi_io_read(void *opaque, hwaddr addr,
                            unsigned size)
{
    LSIState *s = opaque;
    return lsi_reg_readb(s, addr & 0xff);
}

static void lsi_io_write(void *opaque, hwaddr addr,
                         uint64_t val, unsigned size)
{
    LSIState *s = opaque;
    lsi_reg_writeb(s, addr & 0xff, val);
}

static const MemoryRegionOps lsi_io_ops = {
    .read = lsi_io_read,
    .write = lsi_io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void lsi_scsi_reset(DeviceState *dev)
{
    LSIState *s = LSI53C895A(dev);

    lsi_soft_reset(s);
}

static void lsi_pre_save(void *opaque)
{
    LSIState *s = opaque;

    if (s->current) {
        assert(s->current->dma_buf == NULL);
        assert(s->current->dma_len == 0);
    }
    assert(QTAILQ_EMPTY(&s->queue));
}

static const VMStateDescription vmstate_lsi_scsi = {
    .name = "lsiscsi",
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_save = lsi_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, LSIState),

        VMSTATE_INT32(carry, LSIState),
        VMSTATE_INT32(status, LSIState),
        VMSTATE_INT32(msg_action, LSIState),
        VMSTATE_INT32(msg_len, LSIState),
        VMSTATE_BUFFER(msg, LSIState),
        VMSTATE_INT32(waiting, LSIState),

        VMSTATE_UINT32(dsa, LSIState),
        VMSTATE_UINT32(temp, LSIState),
        VMSTATE_UINT32(dnad, LSIState),
        VMSTATE_UINT32(dbc, LSIState),
        VMSTATE_UINT8(istat0, LSIState),
        VMSTATE_UINT8(istat1, LSIState),
        VMSTATE_UINT8(dcmd, LSIState),
        VMSTATE_UINT8(dstat, LSIState),
        VMSTATE_UINT8(dien, LSIState),
        VMSTATE_UINT8(sist0, LSIState),
        VMSTATE_UINT8(sist1, LSIState),
        VMSTATE_UINT8(sien0, LSIState),
        VMSTATE_UINT8(sien1, LSIState),
        VMSTATE_UINT8(mbox0, LSIState),
        VMSTATE_UINT8(mbox1, LSIState),
        VMSTATE_UINT8(dfifo, LSIState),
        VMSTATE_UINT8(ctest2, LSIState),
        VMSTATE_UINT8(ctest3, LSIState),
        VMSTATE_UINT8(ctest4, LSIState),
        VMSTATE_UINT8(ctest5, LSIState),
        VMSTATE_UINT8(ccntl0, LSIState),
        VMSTATE_UINT8(ccntl1, LSIState),
        VMSTATE_UINT32(dsp, LSIState),
        VMSTATE_UINT32(dsps, LSIState),
        VMSTATE_UINT8(dmode, LSIState),
        VMSTATE_UINT8(dcntl, LSIState),
        VMSTATE_UINT8(scntl0, LSIState),
        VMSTATE_UINT8(scntl1, LSIState),
        VMSTATE_UINT8(scntl2, LSIState),
        VMSTATE_UINT8(scntl3, LSIState),
        VMSTATE_UINT8(sstat0, LSIState),
        VMSTATE_UINT8(sstat1, LSIState),
        VMSTATE_UINT8(scid, LSIState),
        VMSTATE_UINT8(sxfer, LSIState),
        VMSTATE_UINT8(socl, LSIState),
        VMSTATE_UINT8(sdid, LSIState),
        VMSTATE_UINT8(ssid, LSIState),
        VMSTATE_UINT8(sfbr, LSIState),
        VMSTATE_UINT8(stest1, LSIState),
        VMSTATE_UINT8(stest2, LSIState),
        VMSTATE_UINT8(stest3, LSIState),
        VMSTATE_UINT8(sidl, LSIState),
        VMSTATE_UINT8(stime0, LSIState),
        VMSTATE_UINT8(respid0, LSIState),
        VMSTATE_UINT8(respid1, LSIState),
        VMSTATE_UINT32(mmrs, LSIState),
        VMSTATE_UINT32(mmws, LSIState),
        VMSTATE_UINT32(sfs, LSIState),
        VMSTATE_UINT32(drs, LSIState),
        VMSTATE_UINT32(sbms, LSIState),
        VMSTATE_UINT32(dbms, LSIState),
        VMSTATE_UINT32(dnad64, LSIState),
        VMSTATE_UINT32(pmjad1, LSIState),
        VMSTATE_UINT32(pmjad2, LSIState),
        VMSTATE_UINT32(rbc, LSIState),
        VMSTATE_UINT32(ua, LSIState),
        VMSTATE_UINT32(ia, LSIState),
        VMSTATE_UINT32(sbc, LSIState),
        VMSTATE_UINT32(csbc, LSIState),
        VMSTATE_BUFFER_UNSAFE(scratch, LSIState, 0, 18 * sizeof(uint32_t)),
        VMSTATE_UINT8(sbr, LSIState),

        VMSTATE_BUFFER_UNSAFE(script_ram, LSIState, 0, 2048 * sizeof(uint32_t)),
        VMSTATE_END_OF_LIST()
    }
};

static const struct SCSIBusInfo lsi_scsi_info = {
    .tcq = true,
    .max_target = LSI_MAX_DEVS,
    .max_lun = 0,  /* LUN support is buggy */

    .transfer_data = lsi_transfer_data,
    .complete = lsi_command_complete,
    .cancel = lsi_request_cancelled
};

static int lsi_scsi_init(PCIDevice *dev)
{
    LSIState *s = LSI53C895A(dev);
    DeviceState *d = DEVICE(dev);
    uint8_t *pci_conf;
    Error *err = NULL;

    pci_conf = dev->config;

    /* PCI latency timer = 255 */
    pci_conf[PCI_LATENCY_TIMER] = 0xff;
    /* Interrupt pin A */
    pci_conf[PCI_INTERRUPT_PIN] = 0x01;

    memory_region_init_io(&s->mmio_io, OBJECT(s), &lsi_mmio_ops, s,
                          "lsi-mmio", 0x400);
    memory_region_init_io(&s->ram_io, OBJECT(s), &lsi_ram_ops, s,
                          "lsi-ram", 0x2000);
    memory_region_init_io(&s->io_io, OBJECT(s), &lsi_io_ops, s,
                          "lsi-io", 256);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io_io);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio_io);
    pci_register_bar(dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->ram_io);
    QTAILQ_INIT(&s->queue);

    scsi_bus_new(&s->bus, sizeof(s->bus), d, &lsi_scsi_info, NULL);
    if (!d->hotplugged) {
        scsi_bus_legacy_handle_cmdline(&s->bus, &err);
        if (err != NULL) {
            error_free(err);
            return -1;
        }
    }
    return 0;
}

static void lsi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = lsi_scsi_init;
    k->vendor_id = PCI_VENDOR_ID_LSI_LOGIC;
    k->device_id = PCI_DEVICE_ID_LSI_53C895A;
    k->class_id = PCI_CLASS_STORAGE_SCSI;
    k->subsystem_id = 0x1000;
    dc->reset = lsi_scsi_reset;
    dc->vmsd = &vmstate_lsi_scsi;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo lsi_info = {
    .name          = TYPE_LSI53C895A,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(LSIState),
    .class_init    = lsi_class_init,
};

static void lsi53c810_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = PCI_DEVICE_ID_LSI_53C810;
}

static TypeInfo lsi53c810_info = {
    .name          = TYPE_LSI53C810,
    .parent        = TYPE_LSI53C895A,
    .class_init    = lsi53c810_class_init,
};

static void lsi53c895a_register_types(void)
{
    type_register_static(&lsi_info);
    type_register_static(&lsi53c810_info);
}

type_init(lsi53c895a_register_types)
