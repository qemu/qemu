/* 
 * QEMU LSI53C895A SCSI Host Bus Adapter emulation
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the LGPL.
 */

/* ??? Need to check if the {read,write}[wl] routines work properly on
   big-endian targets.  */

#include "vl.h"

//#define DEBUG_LSI
//#define DEBUG_LSI_REG

#ifdef DEBUG_LSI
#define DPRINTF(fmt, args...) \
do { printf("lsi_scsi: " fmt , ##args); } while (0)
#define BADF(fmt, args...) \
do { fprintf(stderr, "lsi_scsi: " fmt , ##args); exit(1);} while (0)
#else
#define DPRINTF(fmt, args...) do {} while(0)
#define BADF(fmt, args...) \
do { fprintf(stderr, "lsi_scsi: " fmt , ##args); } while (0)
#endif

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

#define PHASE_DO          0
#define PHASE_DI          1
#define PHASE_CMD         2
#define PHASE_ST          3
#define PHASE_MO          6
#define PHASE_MI          7
#define PHASE_MASK        7

/* The HBA is ID 7, so for simplicitly limit to 7 devices.  */
#define LSI_MAX_DEVS      7

typedef struct {
    PCIDevice pci_dev;
    int mmio_io_addr;
    int ram_io_addr;
    uint32_t script_ram_base;
    uint32_t data_len;

    int carry; /* ??? Should this be an a visible register somewhere?  */
    int sense;
    uint8_t msg;
    /* Nonzero if a Wait Reselect instruction has been issued.  */
    int waiting;
    SCSIDevice *scsi_dev[LSI_MAX_DEVS];
    SCSIDevice *current_dev;
    int current_lun;

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
    uint8_t sfbr;
    uint8_t stest1;
    uint8_t stest2;
    uint8_t stest3;
    uint8_t stime0;
    uint8_t respid0;
    uint8_t respid1;
    uint32_t mmrs;
    uint32_t mmws;
    uint32_t sfs;
    uint32_t drs;
    uint32_t sbms;
    uint32_t dmbs;
    uint32_t dnad64;
    uint32_t pmjad1;
    uint32_t pmjad2;
    uint32_t rbc;
    uint32_t ua;
    uint32_t ia;
    uint32_t sbc;
    uint32_t csbc;
    uint32_t scratch[13]; /* SCRATCHA-SCRATCHR */

    /* Script ram is stored as 32-bit words in host byteorder.  */
    uint32_t script_ram[2048];
} LSIState;

static void lsi_soft_reset(LSIState *s)
{
    DPRINTF("Reset\n");
    s->carry = 0;

    s->waiting = 0;
    s->dsa = 0;
    s->dnad = 0;
    s->dbc = 0;
    s->temp = 0;
    memset(s->scratch, 0, sizeof(s->scratch));
    s->istat0 = 0;
    s->istat1 = 0;
    s->dcmd = 0;
    s->dstat = 0;
    s->dien = 0;
    s->sist0 = 0;
    s->sist1 = 0;
    s->sien0 = 0;
    s->sien1 = 0;
    s->mbox0 = 0;
    s->mbox1 = 0;
    s->dfifo = 0;
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
    s->stest1 = 0;
    s->stest2 = 0;
    s->stest3 = 0;
    s->stime0 = 0;
    s->respid0 = 0x80;
    s->respid1 = 0;
    s->mmrs = 0;
    s->mmws = 0;
    s->sfs = 0;
    s->drs = 0;
    s->sbms = 0;
    s->dmbs = 0;
    s->dnad64 = 0;
    s->pmjad1 = 0;
    s->pmjad2 = 0;
    s->rbc = 0;
    s->ua = 0;
    s->ia = 0;
    s->sbc = 0;
    s->csbc = 0;
}

static uint8_t lsi_reg_readb(LSIState *s, int offset);
static void lsi_reg_writeb(LSIState *s, int offset, uint8_t val);

static inline uint32_t read_dword(LSIState *s, uint32_t addr)
{
    uint32_t buf;

    /* Optimize reading from SCRIPTS RAM.  */
    if ((addr & 0xffffe000) == s->script_ram_base) {
        return s->script_ram[(addr & 0x1fff) >> 2];
    }
    cpu_physical_memory_read(addr, (uint8_t *)&buf, 4);
    return cpu_to_le32(buf);
}

static void lsi_stop_script(LSIState *s)
{
    s->istat1 &= ~LSI_ISTAT1_SRUN;
}

static void lsi_update_irq(LSIState *s)
{
    int level;
    static int last_level;

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
    pci_set_irq(&s->pci_dev, 0, level);
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
        if ((s->ccntl0 & LSI_CCNTL0_PMJCTL) || out) {
            s->dsp = s->pmjad1;
        } else {
            s->dsp = s->pmjad2;
        }
        DPRINTF("Data phase mismatch jump to %08x\n", s->dsp);
    } else {
        DPRINTF("Phase mismatch interrupt\n");
        lsi_script_scsi_interrupt(s, LSI_SIST0_MA, 0);
        lsi_stop_script(s);
    }
    lsi_set_phase(s, new_phase);
}

static void lsi_do_dma(LSIState *s, int out)
{
    uint8_t buf[TARGET_PAGE_SIZE];
    uint32_t addr;
    uint32_t count;
    int n;

    count = s->dbc;
    addr = s->dnad;
    DPRINTF("DMA %s addr=0x%08x len=%d avail=%d\n", out ? "out" : "in",
            addr, count, s->data_len);
    /* ??? Too long transfers are truncated. Don't know if this is the
       correct behavior.  */
    if (count > s->data_len) {
        /* If the DMA length is greater then the device data length then
           a phase mismatch will occur.  */
        count = s->data_len;
        s->dbc = count;
        lsi_bad_phase(s, out, PHASE_ST);
    }

    s->csbc += count;

    /* ??? Set SFBR to first data byte.  */
    while (count) {
        n = (count > TARGET_PAGE_SIZE) ? TARGET_PAGE_SIZE : count;
        if (out) {
            cpu_physical_memory_read(addr, buf, n);
            scsi_write_data(s->current_dev, buf, n);
        } else {
            scsi_read_data(s->current_dev, buf, n);
            cpu_physical_memory_write(addr, buf, n);
        }
        addr += n;
        count -= n;
    }
}


static void lsi_do_command(LSIState *s)
{
    uint8_t buf[16];
    int n;

    DPRINTF("Send command len=%d\n", s->dbc);
    if (s->dbc > 16)
        s->dbc = 16;
    cpu_physical_memory_read(s->dnad, buf, s->dbc);
    s->sfbr = buf[0];
    n = scsi_send_command(s->current_dev, 0, buf, s->current_lun);
    if (n > 0) {
        s->data_len = n;
        lsi_set_phase(s, PHASE_DI);
    } else if (n < 0) {
        s->data_len = -n;
        lsi_set_phase(s, PHASE_DO);
    }
}

static void lsi_command_complete(void *opaque, uint32_t tag, int sense)
{
    LSIState *s = (LSIState *)opaque;

    DPRINTF("Command complete sense=%d\n", sense);
    s->sense = sense;
    lsi_set_phase(s, PHASE_ST);
}

static void lsi_do_status(LSIState *s)
{
    DPRINTF("Get status len=%d sense=%d\n", s->dbc, s->sense);
    if (s->dbc != 1)
        BADF("Bad Status move\n");
    s->dbc = 1;
    s->msg = s->sense;
    cpu_physical_memory_write(s->dnad, &s->msg, 1);
    s->sfbr = s->msg;
    lsi_set_phase(s, PHASE_MI);
    s->msg = 0; /* COMMAND COMPLETE */
}

static void lsi_disconnect(LSIState *s)
{
    s->scntl1 &= ~LSI_SCNTL1_CON;
    s->sstat1 &= ~PHASE_MASK;
}

static void lsi_do_msgin(LSIState *s)
{
    DPRINTF("Message in len=%d\n", s->dbc);
    s->dbc = 1;
    s->sfbr = s->msg;
    cpu_physical_memory_write(s->dnad, &s->msg, 1);
    if (s->msg == 0) {
        lsi_disconnect(s);
    } else {
        /* ??? Check if ATN (not yet implemented) is asserted and maybe
           switch to PHASE_MO.  */
        lsi_set_phase(s, PHASE_CMD);
    }
}

static void lsi_do_msgout(LSIState *s)
{
    uint8_t msg;

    DPRINTF("MSG out len=%d\n", s->dbc);
    if (s->dbc != 1) {
        /* Multibyte messages not implemented.  */
        s->msg = 7; /* MESSAGE REJECT */
        //s->dbc = 1;
        //lsi_bad_phase(s, 1, PHASE_MI);
        lsi_set_phase(s, PHASE_MI);
        return;
    }
    cpu_physical_memory_read(s->dnad, &msg, 1);
    s->sfbr = msg;
    s->dnad++;

    switch (msg) {
    case 0x00:
        DPRINTF("Got Disconnect\n");
        lsi_disconnect(s);
        return;
    case 0x08:
        DPRINTF("Got No Operation\n");
        lsi_set_phase(s, PHASE_CMD);
        return;
    }
    if ((msg & 0x80) == 0) {
        DPRINTF("Unimplemented message 0x%d\n", msg);
        s->msg = 7; /* MESSAGE REJECT */
        lsi_bad_phase(s, 1, PHASE_MI);
        return;
    }
    s->current_lun = msg & 7;
    DPRINTF("Select LUN %d\n", s->current_lun);
    lsi_set_phase(s, PHASE_CMD);
}

/* Sign extend a 24-bit value.  */
static inline int32_t sxt24(int32_t n)
{
    return (n << 8) >> 8;
}

static void lsi_memcpy(LSIState *s, uint32_t dest, uint32_t src, int count)
{
    int n;
    uint8_t buf[TARGET_PAGE_SIZE];

    DPRINTF("memcpy dest 0x%08x src 0x%08x count %d\n", dest, src, count);
    while (count) {
        n = (count > TARGET_PAGE_SIZE) ? TARGET_PAGE_SIZE : count;
        cpu_physical_memory_read(src, buf, n);
        cpu_physical_memory_write(dest, buf, n);
        src += n;
        dest += n;
        count -= n;
    }
}

static void lsi_execute_script(LSIState *s)
{
    uint32_t insn;
    uint32_t addr;
    int opcode;

    s->istat1 |= LSI_ISTAT1_SRUN;
again:
    insn = read_dword(s, s->dsp);
    addr = read_dword(s, s->dsp + 4);
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
        if (insn & (1 << 29)) {
            /* Indirect addressing.  */
            addr = read_dword(s, addr);
        } else if (insn & (1 << 28)) {
            uint32_t buf[2];
            int32_t offset;
            /* Table indirect addressing.  */
            offset = sxt24(addr);
            cpu_physical_memory_read(s->dsa + offset, (uint8_t *)buf, 8);
            s->dbc = cpu_to_le32(buf[0]);
            addr = cpu_to_le32(buf[1]);
        }
        if ((s->sstat1 & PHASE_MASK) != ((insn >> 24) & 7)) {
            DPRINTF("Wrong phase got %d expected %d\n",
                    s->sstat1 & PHASE_MASK, (insn >> 24) & 7);
            lsi_script_scsi_interrupt(s, LSI_SIST0_MA, 0);
            break;
        }
        s->dnad = addr;
        switch (s->sstat1 & 0x7) {
        case PHASE_DO:
            lsi_do_dma(s, 1);
            break;
        case PHASE_DI:
            lsi_do_dma(s, 0);
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
        /* ??? Set ESA.  */
        s->ia = s->dsp - 8;
        break;

    case 1: /* IO or Read/Write instruction.  */
        opcode = (insn >> 27) & 7;
        if (opcode < 5) {
            uint32_t id;

            if (insn & (1 << 25)) {
                id = read_dword(s, s->dsa + sxt24(insn));
            } else {
                id = addr;
            }
            id = (id >> 16) & 0xf;
            if (insn & (1 << 26)) {
                addr = s->dsp + sxt24(addr);
            }
            s->dnad = addr;
            switch (opcode) {
            case 0: /* Select */
                s->sstat0 |= LSI_SSTAT0_WOA;
                s->scntl1 &= ~LSI_SCNTL1_IARB;
                s->sdid = id;
                if (id >= LSI_MAX_DEVS || !s->scsi_dev[id]) {
                    DPRINTF("Selected absent target %d\n", id);
                    lsi_script_scsi_interrupt(s, 0, LSI_SIST1_STO);
                    lsi_disconnect(s);
                    break;
                }
                DPRINTF("Selected target %d%s\n",
                        id, insn & (1 << 3) ? " ATN" : "");
                /* ??? Linux drivers compain when this is set.  Maybe
                   it only applies in low-level mode (unimplemented).
                lsi_script_scsi_interrupt(s, LSI_SIST0_CMP, 0); */
                s->current_dev = s->scsi_dev[id];
                s->scntl1 |= LSI_SCNTL1_CON;
                if (insn & (1 << 3)) {
                    s->socl |= LSI_SOCL_ATN;
                }
                lsi_set_phase(s, PHASE_MO);
                break;
            case 1: /* Disconnect */
                DPRINTF("Wait Disconect\n");
                s->scntl1 &= ~LSI_SCNTL1_CON;
                break;
            case 2: /* Wait Reselect */
                DPRINTF("Wait Reselect\n");
                s->waiting = 1;
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
            DPRINTF("%s reg 0x%x %s data8 %d%s\n",
                    opcode_names[opcode - 5], reg,
                    operator_names[operator], data8,
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
                op0 |= op1;
                break;
            case 4: /* AND */
                op0 &= op1;
                break;
            case 5: /* SHR */
                op1 = op0 & 1;
                op0 = (op0 >> 1) | (s->carry << 7);
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
                    addr = s->dsp + sxt24(addr);
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
                addr = s->dsa + sxt24(addr);
            }
            n = (insn & 7);
            reg = (insn >> 16) & 0xff;
            if (insn & (1 << 24)) {
                DPRINTF("Load reg 0x%x size %d addr 0x%08x\n", reg, n, addr);
                cpu_physical_memory_read(addr, data, n);
                for (i = 0; i < n; i++) {
                    lsi_reg_writeb(s, reg + i, data[i]);
                }
            } else {
                DPRINTF("Store reg 0x%x size %d addr 0x%08x\n", reg, n, addr);
                for (i = 0; i < n; i++) {
                    data[i] = lsi_reg_readb(s, reg + i);
                }
                cpu_physical_memory_write(addr, data, n);
            }
        }
    }
    /* ??? Need to avoid infinite loops.  */
    if (s->istat1 & LSI_ISTAT1_SRUN && !s->waiting) {
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
    case 0xb: /* SBCL */
        /* ??? This is not correct. However it's (hopefully) only
           used for diagnostics, so should be ok.  */
        return 0;
    case 0xc: /* DSTAT */
        tmp = s->dstat | 0x80;
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
    case 0x16: /* MBOX0 */
        return s->mbox0;
    case 0x17: /* MBOX1 */
        return s->mbox1;
    case 0x18: /* CTEST0 */
        return 0xff;
    case 0x19: /* CTEST1 */
        return 0;
    case 0x1a: /* CTEST2 */
        tmp = LSI_CTEST2_DACK | LSI_CTEST2_CM;
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
    case 0x24: /* DBC[0:7] */
        return s->dbc & 0xff;
    case 0x25: /* DBC[8:15] */
        return (s->dbc >> 8) & 0xff;
    case 0x26: /* DBC[16->23] */
        return (s->dbc >> 16) & 0xff;
    case 0x27: /* DCMD */
        return s->dcmd;
    CASE_GET_REG32(dsp, 0x2c)
    CASE_GET_REG32(dsps, 0x30)
    CASE_GET_REG32(scratch[0], 0x34)
    case 0x38: /* DMODE */
        return s->dmode;
    case 0x39: /* DIEN */
        return s->dien;
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
    case 0x52: /* STEST4 */
        return 0xe0;
    case 0x56: /* CCNTL0 */
        return s->ccntl0;
    case 0x57: /* CCNTL1 */
        return s->ccntl1;
    case 0x58: case 0x59: /* SBDL */
        return 0;
    CASE_GET_REG32(mmrs, 0xa0)
    CASE_GET_REG32(mmws, 0xa4)
    CASE_GET_REG32(sfs, 0xa8)
    CASE_GET_REG32(drs, 0xac)
    CASE_GET_REG32(sbms, 0xb0)
    CASE_GET_REG32(dmbs, 0xb4)
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
#undef CASE_GET_REG32
}

static void lsi_reg_writeb(LSIState *s, int offset, uint8_t val)
{
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
            s->sstat0 |= LSI_SSTAT0_RST;
            lsi_script_scsi_interrupt(s, LSI_SIST0_RST, 0);
        } else {
            s->sstat0 &= ~LSI_SSTAT0_RST;
        }
        break;
    case 0x02: /* SCNTL2 */
        val &= ~(LSI_SCNTL2_WSR | LSI_SCNTL2_WSS);
        s->scntl3 = val;
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
    case 0x07: /* GPREG0 */
        break;
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
        if (s->waiting && val & LSI_ISTAT0_SIGP) {
            DPRINTF("Woken by SIGP\n");
            s->waiting = 0;
            s->dsp = s->dnad;
            lsi_execute_script(s);
        }
        if (val & LSI_ISTAT0_SRST) {
            lsi_soft_reset(s);
        }
    case 0x16: /* MBOX0 */
        s->mbox0 = val;
    case 0x17: /* MBOX1 */
        s->mbox1 = val;
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
    case 0x2c: /* DSPS[0:7] */
        s->dsp &= 0xffffff00;
        s->dsp |= val;
        break;
    case 0x2d: /* DSPS[8:15] */
        s->dsp &= 0xffff00ff;
        s->dsp |= val << 8;
        break;
    case 0x2e: /* DSPS[16:23] */
        s->dsp &= 0xff00ffff;
        s->dsp |= val << 16;
        break;
    case 0x2f: /* DSPS[14:31] */
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
    CASE_SET_REG32(dmbs, 0xb4)
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
            s->scratch[n] &= ~(0xff << shift);
            s->scratch[n] |= (val & 0xff) << shift;
        } else {
            BADF("Unhandled writeb 0x%x = 0x%x\n", offset, val);
        }
    }
#undef CASE_SET_REG32
}

static void lsi_mmio_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    LSIState *s = (LSIState *)opaque;

    lsi_reg_writeb(s, addr & 0xff, val);
}

static void lsi_mmio_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    LSIState *s = (LSIState *)opaque;

    addr &= 0xff;
    lsi_reg_writeb(s, addr, val & 0xff);
    lsi_reg_writeb(s, addr + 1, (val >> 8) & 0xff);
}

static void lsi_mmio_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    LSIState *s = (LSIState *)opaque;

    addr &= 0xff;
    lsi_reg_writeb(s, addr, val & 0xff);
    lsi_reg_writeb(s, addr + 1, (val >> 8) & 0xff);
    lsi_reg_writeb(s, addr + 2, (val >> 16) & 0xff);
    lsi_reg_writeb(s, addr + 3, (val >> 24) & 0xff);
}

static uint32_t lsi_mmio_readb(void *opaque, target_phys_addr_t addr)
{
    LSIState *s = (LSIState *)opaque;

    return lsi_reg_readb(s, addr & 0xff);
}

static uint32_t lsi_mmio_readw(void *opaque, target_phys_addr_t addr)
{
    LSIState *s = (LSIState *)opaque;
    uint32_t val;

    addr &= 0xff;
    val = lsi_reg_readb(s, addr);
    val |= lsi_reg_readb(s, addr + 1) << 8;
    return val;
}

static uint32_t lsi_mmio_readl(void *opaque, target_phys_addr_t addr)
{
    LSIState *s = (LSIState *)opaque;
    uint32_t val;
    addr &= 0xff;
    val = lsi_reg_readb(s, addr);
    val |= lsi_reg_readb(s, addr + 1) << 8;
    val |= lsi_reg_readb(s, addr + 2) << 16;
    val |= lsi_reg_readb(s, addr + 3) << 24;
    return val;
}

static CPUReadMemoryFunc *lsi_mmio_readfn[3] = {
    lsi_mmio_readb,
    lsi_mmio_readw,
    lsi_mmio_readl,
};

static CPUWriteMemoryFunc *lsi_mmio_writefn[3] = {
    lsi_mmio_writeb,
    lsi_mmio_writew,
    lsi_mmio_writel,
};

static void lsi_ram_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    LSIState *s = (LSIState *)opaque;
    uint32_t newval;
    int shift;

    addr &= 0x1fff;
    newval = s->script_ram[addr >> 2];
    shift = (addr & 3) * 8;
    newval &= ~(0xff << shift);
    newval |= val << shift;
    s->script_ram[addr >> 2] = newval;
}

static void lsi_ram_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    LSIState *s = (LSIState *)opaque;
    uint32_t newval;

    addr &= 0x1fff;
    newval = s->script_ram[addr >> 2];
    if (addr & 2) {
        newval = (newval & 0xffff) | (val << 16);
    } else {
        newval = (newval & 0xffff0000) | val;
    }
    s->script_ram[addr >> 2] = newval;
}


static void lsi_ram_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    LSIState *s = (LSIState *)opaque;

    addr &= 0x1fff;
    s->script_ram[addr >> 2] = val;
}

static uint32_t lsi_ram_readb(void *opaque, target_phys_addr_t addr)
{
    LSIState *s = (LSIState *)opaque;
    uint32_t val;

    addr &= 0x1fff;
    val = s->script_ram[addr >> 2];
    val >>= (addr & 3) * 8;
    return val & 0xff;
}

static uint32_t lsi_ram_readw(void *opaque, target_phys_addr_t addr)
{
    LSIState *s = (LSIState *)opaque;
    uint32_t val;

    addr &= 0x1fff;
    val = s->script_ram[addr >> 2];
    if (addr & 2)
        val >>= 16;
    return le16_to_cpu(val);
}

static uint32_t lsi_ram_readl(void *opaque, target_phys_addr_t addr)
{
    LSIState *s = (LSIState *)opaque;

    addr &= 0x1fff;
    return le32_to_cpu(s->script_ram[addr >> 2]);
}

static CPUReadMemoryFunc *lsi_ram_readfn[3] = {
    lsi_ram_readb,
    lsi_ram_readw,
    lsi_ram_readl,
};

static CPUWriteMemoryFunc *lsi_ram_writefn[3] = {
    lsi_ram_writeb,
    lsi_ram_writew,
    lsi_ram_writel,
};

static uint32_t lsi_io_readb(void *opaque, uint32_t addr)
{
    LSIState *s = (LSIState *)opaque;
    return lsi_reg_readb(s, addr & 0xff);
}

static uint32_t lsi_io_readw(void *opaque, uint32_t addr)
{
    LSIState *s = (LSIState *)opaque;
    uint32_t val;
    addr &= 0xff;
    val = lsi_reg_readb(s, addr);
    val |= lsi_reg_readb(s, addr + 1) << 8;
    return val;
}

static uint32_t lsi_io_readl(void *opaque, uint32_t addr)
{
    LSIState *s = (LSIState *)opaque;
    uint32_t val;
    addr &= 0xff;
    val = lsi_reg_readb(s, addr);
    val |= lsi_reg_readb(s, addr + 1) << 8;
    val |= lsi_reg_readb(s, addr + 2) << 16;
    val |= lsi_reg_readb(s, addr + 3) << 24;
    return val;
}

static void lsi_io_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    LSIState *s = (LSIState *)opaque;
    lsi_reg_writeb(s, addr & 0xff, val);
}

static void lsi_io_writew(void *opaque, uint32_t addr, uint32_t val)
{
    LSIState *s = (LSIState *)opaque;
    addr &= 0xff;
    lsi_reg_writeb(s, addr, val & 0xff);
    lsi_reg_writeb(s, addr + 1, (val >> 8) & 0xff);
}

static void lsi_io_writel(void *opaque, uint32_t addr, uint32_t val)
{
    LSIState *s = (LSIState *)opaque;
    addr &= 0xff;
    lsi_reg_writeb(s, addr, val & 0xff);
    lsi_reg_writeb(s, addr + 1, (val >> 8) & 0xff);
    lsi_reg_writeb(s, addr + 2, (val >> 16) & 0xff);
    lsi_reg_writeb(s, addr + 2, (val >> 24) & 0xff);
}

static void lsi_io_mapfunc(PCIDevice *pci_dev, int region_num, 
                           uint32_t addr, uint32_t size, int type)
{
    LSIState *s = (LSIState *)pci_dev;

    DPRINTF("Mapping IO at %08x\n", addr);

    register_ioport_write(addr, 256, 1, lsi_io_writeb, s);
    register_ioport_read(addr, 256, 1, lsi_io_readb, s);
    register_ioport_write(addr, 256, 2, lsi_io_writew, s);
    register_ioport_read(addr, 256, 2, lsi_io_readw, s);
    register_ioport_write(addr, 256, 4, lsi_io_writel, s);
    register_ioport_read(addr, 256, 4, lsi_io_readl, s);
}

static void lsi_ram_mapfunc(PCIDevice *pci_dev, int region_num, 
                            uint32_t addr, uint32_t size, int type)
{
    LSIState *s = (LSIState *)pci_dev;

    DPRINTF("Mapping ram at %08x\n", addr);
    s->script_ram_base = addr;
    cpu_register_physical_memory(addr + 0, 0x2000, s->ram_io_addr);
}

static void lsi_mmio_mapfunc(PCIDevice *pci_dev, int region_num, 
                             uint32_t addr, uint32_t size, int type)
{
    LSIState *s = (LSIState *)pci_dev;

    DPRINTF("Mapping registers at %08x\n", addr);
    cpu_register_physical_memory(addr + 0, 0x400, s->mmio_io_addr);
}

void lsi_scsi_attach(void *opaque, BlockDriverState *bd, int id)
{
    LSIState *s = (LSIState *)opaque;

    if (id < 0) {
        for (id = 0; id < LSI_MAX_DEVS; id++) {
            if (s->scsi_dev[id] == NULL)
                break;
        }
    }
    if (id >= LSI_MAX_DEVS) {
        BADF("Bad Device ID %d\n", id);
        return;
    }
    if (s->scsi_dev[id]) {
        DPRINTF("Destroying device %d\n", id);
        scsi_disk_destroy(s->scsi_dev[id]);
    }
    DPRINTF("Attaching block device %d\n", id);
    s->scsi_dev[id] = scsi_disk_init(bd, lsi_command_complete, s);
}

void *lsi_scsi_init(PCIBus *bus, int devfn)
{
    LSIState *s;

    s = (LSIState *)pci_register_device(bus, "LSI53C895A SCSI HBA",
                                        sizeof(*s), devfn, NULL, NULL);
    if (s == NULL) {
        fprintf(stderr, "lsi-scsi: Failed to register PCI device\n");
        return NULL;
    }

    s->pci_dev.config[0x00] = 0x00;
    s->pci_dev.config[0x01] = 0x10;
    s->pci_dev.config[0x02] = 0x12;
    s->pci_dev.config[0x03] = 0x00;
    s->pci_dev.config[0x0b] = 0x01;
    s->pci_dev.config[0x3d] = 0x01; /* interrupt pin 1 */

    s->mmio_io_addr = cpu_register_io_memory(0, lsi_mmio_readfn,
                                             lsi_mmio_writefn, s);
    s->ram_io_addr = cpu_register_io_memory(0, lsi_ram_readfn,
                                            lsi_ram_writefn, s);

    pci_register_io_region((struct PCIDevice *)s, 0, 256,
                           PCI_ADDRESS_SPACE_IO, lsi_io_mapfunc);
    pci_register_io_region((struct PCIDevice *)s, 1, 0x400,
                           PCI_ADDRESS_SPACE_MEM, lsi_mmio_mapfunc);
    pci_register_io_region((struct PCIDevice *)s, 2, 0x2000,
                           PCI_ADDRESS_SPACE_MEM, lsi_ram_mapfunc);

    lsi_soft_reset(s);

    return s;
}

