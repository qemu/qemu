/*
 * ARM PrimeCell PL330 DMA Controller
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.crosthwaite@petalogix.com)
 * Copyright (c) 2012 PetaLogix Pty Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 or later.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "sysemu/dma.h"
#include "qemu/log.h"

#ifndef PL330_ERR_DEBUG
#define PL330_ERR_DEBUG 0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do {\
    if (PL330_ERR_DEBUG >= lvl) {\
        fprintf(stderr, "PL330: %s:" fmt, __func__, ## args);\
    } \
} while (0);

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

#define PL330_PERIPH_NUM            32
#define PL330_MAX_BURST_LEN         128
#define PL330_INSN_MAXSIZE          6

#define PL330_FIFO_OK               0
#define PL330_FIFO_STALL            1
#define PL330_FIFO_ERR              (-1)

#define PL330_FAULT_UNDEF_INSTR             (1 <<  0)
#define PL330_FAULT_OPERAND_INVALID         (1 <<  1)
#define PL330_FAULT_DMAGO_ERR               (1 <<  4)
#define PL330_FAULT_EVENT_ERR               (1 <<  5)
#define PL330_FAULT_CH_PERIPH_ERR           (1 <<  6)
#define PL330_FAULT_CH_RDWR_ERR             (1 <<  7)
#define PL330_FAULT_ST_DATA_UNAVAILABLE     (1 << 12)
#define PL330_FAULT_FIFOEMPTY_ERR           (1 << 13)
#define PL330_FAULT_INSTR_FETCH_ERR         (1 << 16)
#define PL330_FAULT_DATA_WRITE_ERR          (1 << 17)
#define PL330_FAULT_DATA_READ_ERR           (1 << 18)
#define PL330_FAULT_DBG_INSTR               (1 << 30)
#define PL330_FAULT_LOCKUP_ERR              (1 << 31)

#define PL330_UNTAGGED              0xff

#define PL330_SINGLE                0x0
#define PL330_BURST                 0x1

#define PL330_WATCHDOG_LIMIT        1024

/* IOMEM mapped registers */
#define PL330_REG_DSR               0x000
#define PL330_REG_DPC               0x004
#define PL330_REG_INTEN             0x020
#define PL330_REG_INT_EVENT_RIS     0x024
#define PL330_REG_INTMIS            0x028
#define PL330_REG_INTCLR            0x02C
#define PL330_REG_FSRD              0x030
#define PL330_REG_FSRC              0x034
#define PL330_REG_FTRD              0x038
#define PL330_REG_FTR_BASE          0x040
#define PL330_REG_CSR_BASE          0x100
#define PL330_REG_CPC_BASE          0x104
#define PL330_REG_CHANCTRL          0x400
#define PL330_REG_DBGSTATUS         0xD00
#define PL330_REG_DBGCMD            0xD04
#define PL330_REG_DBGINST0          0xD08
#define PL330_REG_DBGINST1          0xD0C
#define PL330_REG_CR0_BASE          0xE00
#define PL330_REG_PERIPH_ID         0xFE0

#define PL330_IOMEM_SIZE    0x1000

#define CFG_BOOT_ADDR 2
#define CFG_INS 3
#define CFG_PNS 4
#define CFG_CRD 5

static const uint32_t pl330_id[] = {
    0x30, 0x13, 0x24, 0x00, 0x0D, 0xF0, 0x05, 0xB1
};

/* DMA channel states as they are described in PL330 Technical Reference Manual
 * Most of them will not be used in emulation.
 */
typedef enum  {
    pl330_chan_stopped = 0,
    pl330_chan_executing = 1,
    pl330_chan_cache_miss = 2,
    pl330_chan_updating_pc = 3,
    pl330_chan_waiting_event = 4,
    pl330_chan_at_barrier = 5,
    pl330_chan_queue_busy = 6,
    pl330_chan_waiting_periph = 7,
    pl330_chan_killing = 8,
    pl330_chan_completing = 9,
    pl330_chan_fault_completing = 14,
    pl330_chan_fault = 15,
} PL330ChanState;

typedef struct PL330State PL330State;

typedef struct PL330Chan {
    uint32_t src;
    uint32_t dst;
    uint32_t pc;
    uint32_t control;
    uint32_t status;
    uint32_t lc[2];
    uint32_t fault_type;
    uint32_t watchdog_timer;

    bool ns;
    uint8_t request_flag;
    uint8_t wakeup;
    uint8_t wfp_sbp;

    uint8_t state;
    uint8_t stall;

    bool is_manager;
    PL330State *parent;
    uint8_t tag;
} PL330Chan;

static const VMStateDescription vmstate_pl330_chan = {
    .name = "pl330_chan",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(src, PL330Chan),
        VMSTATE_UINT32(dst, PL330Chan),
        VMSTATE_UINT32(pc, PL330Chan),
        VMSTATE_UINT32(control, PL330Chan),
        VMSTATE_UINT32(status, PL330Chan),
        VMSTATE_UINT32_ARRAY(lc, PL330Chan, 2),
        VMSTATE_UINT32(fault_type, PL330Chan),
        VMSTATE_UINT32(watchdog_timer, PL330Chan),
        VMSTATE_BOOL(ns, PL330Chan),
        VMSTATE_UINT8(request_flag, PL330Chan),
        VMSTATE_UINT8(wakeup, PL330Chan),
        VMSTATE_UINT8(wfp_sbp, PL330Chan),
        VMSTATE_UINT8(state, PL330Chan),
        VMSTATE_UINT8(stall, PL330Chan),
        VMSTATE_END_OF_LIST()
    }
};

typedef struct PL330Fifo {
    uint8_t *buf;
    uint8_t *tag;
    uint32_t head;
    uint32_t num;
    uint32_t buf_size;
} PL330Fifo;

static const VMStateDescription vmstate_pl330_fifo = {
    .name = "pl330_chan",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VBUFFER_UINT32(buf, PL330Fifo, 1, NULL, buf_size),
        VMSTATE_VBUFFER_UINT32(tag, PL330Fifo, 1, NULL, buf_size),
        VMSTATE_UINT32(head, PL330Fifo),
        VMSTATE_UINT32(num, PL330Fifo),
        VMSTATE_UINT32(buf_size, PL330Fifo),
        VMSTATE_END_OF_LIST()
    }
};

typedef struct PL330QueueEntry {
    uint32_t addr;
    uint32_t len;
    uint8_t n;
    bool inc;
    bool z;
    uint8_t tag;
    uint8_t seqn;
} PL330QueueEntry;

static const VMStateDescription vmstate_pl330_queue_entry = {
    .name = "pl330_queue_entry",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(addr, PL330QueueEntry),
        VMSTATE_UINT32(len, PL330QueueEntry),
        VMSTATE_UINT8(n, PL330QueueEntry),
        VMSTATE_BOOL(inc, PL330QueueEntry),
        VMSTATE_BOOL(z, PL330QueueEntry),
        VMSTATE_UINT8(tag, PL330QueueEntry),
        VMSTATE_UINT8(seqn, PL330QueueEntry),
        VMSTATE_END_OF_LIST()
    }
};

typedef struct PL330Queue {
    PL330State *parent;
    PL330QueueEntry *queue;
    uint32_t queue_size;
} PL330Queue;

static const VMStateDescription vmstate_pl330_queue = {
    .name = "pl330_queue",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_VARRAY_UINT32(queue, PL330Queue, queue_size, 1,
                                 vmstate_pl330_queue_entry, PL330QueueEntry),
        VMSTATE_END_OF_LIST()
    }
};

struct PL330State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq_abort;
    qemu_irq *irq;

    /* Config registers. cfg[5] = CfgDn. */
    uint32_t cfg[6];
#define EVENT_SEC_STATE 3
#define PERIPH_SEC_STATE 4
    /* cfg 0 bits and pieces */
    uint32_t num_chnls;
    uint8_t num_periph_req;
    uint8_t num_events;
    uint8_t mgr_ns_at_rst;
    /* cfg 1 bits and pieces */
    uint8_t i_cache_len;
    uint8_t num_i_cache_lines;
    /* CRD bits and pieces */
    uint8_t data_width;
    uint8_t wr_cap;
    uint8_t wr_q_dep;
    uint8_t rd_cap;
    uint8_t rd_q_dep;
    uint16_t data_buffer_dep;

    PL330Chan manager;
    PL330Chan *chan;
    PL330Fifo fifo;
    PL330Queue read_queue;
    PL330Queue write_queue;
    uint8_t *lo_seqn;
    uint8_t *hi_seqn;
    QEMUTimer *timer; /* is used for restore dma. */

    uint32_t inten;
    uint32_t int_status;
    uint32_t ev_status;
    uint32_t dbg[2];
    uint8_t debug_status;
    uint8_t num_faulting;
    uint8_t periph_busy[PL330_PERIPH_NUM];

};

#define TYPE_PL330 "pl330"
#define PL330(obj) OBJECT_CHECK(PL330State, (obj), TYPE_PL330)

static const VMStateDescription vmstate_pl330 = {
    .name = "pl330",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(manager, PL330State, 0, vmstate_pl330_chan, PL330Chan),
        VMSTATE_STRUCT_VARRAY_UINT32(chan, PL330State, num_chnls, 0,
                                     vmstate_pl330_chan, PL330Chan),
        VMSTATE_VBUFFER_UINT32(lo_seqn, PL330State, 1, NULL, num_chnls),
        VMSTATE_VBUFFER_UINT32(hi_seqn, PL330State, 1, NULL, num_chnls),
        VMSTATE_STRUCT(fifo, PL330State, 0, vmstate_pl330_fifo, PL330Fifo),
        VMSTATE_STRUCT(read_queue, PL330State, 0, vmstate_pl330_queue,
                       PL330Queue),
        VMSTATE_STRUCT(write_queue, PL330State, 0, vmstate_pl330_queue,
                       PL330Queue),
        VMSTATE_TIMER_PTR(timer, PL330State),
        VMSTATE_UINT32(inten, PL330State),
        VMSTATE_UINT32(int_status, PL330State),
        VMSTATE_UINT32(ev_status, PL330State),
        VMSTATE_UINT32_ARRAY(dbg, PL330State, 2),
        VMSTATE_UINT8(debug_status, PL330State),
        VMSTATE_UINT8(num_faulting, PL330State),
        VMSTATE_UINT8_ARRAY(periph_busy, PL330State, PL330_PERIPH_NUM),
        VMSTATE_END_OF_LIST()
    }
};

typedef struct PL330InsnDesc {
    /* OPCODE of the instruction */
    uint8_t opcode;
    /* Mask so we can select several sibling instructions, such as
       DMALD, DMALDS and DMALDB */
    uint8_t opmask;
    /* Size of instruction in bytes */
    uint8_t size;
    /* Interpreter */
    void (*exec)(PL330Chan *, uint8_t opcode, uint8_t *args, int len);
} PL330InsnDesc;


/* MFIFO Implementation
 *
 * MFIFO is implemented as a cyclic buffer of BUF_SIZE size. Tagged bytes are
 * stored in this buffer. Data is stored in BUF field, tags - in the
 * corresponding array elements of TAG field.
 */

/* Initialize queue. */

static void pl330_fifo_init(PL330Fifo *s, uint32_t size)
{
    s->buf = g_malloc0(size);
    s->tag = g_malloc0(size);
    s->buf_size = size;
}

/* Cyclic increment */

static inline int pl330_fifo_inc(PL330Fifo *s, int x)
{
    return (x + 1) % s->buf_size;
}

/* Number of empty bytes in MFIFO */

static inline int pl330_fifo_num_free(PL330Fifo *s)
{
    return s->buf_size - s->num;
}

/* Push LEN bytes of data stored in BUF to MFIFO and tag it with TAG.
 * Zero returned on success, PL330_FIFO_STALL if there is no enough free
 * space in MFIFO to store requested amount of data. If push was unsuccessful
 * no data is stored to MFIFO.
 */

static int pl330_fifo_push(PL330Fifo *s, uint8_t *buf, int len, uint8_t tag)
{
    int i;

    if (s->buf_size - s->num < len) {
        return PL330_FIFO_STALL;
    }
    for (i = 0; i < len; i++) {
        int push_idx = (s->head + s->num + i) % s->buf_size;
        s->buf[push_idx] = buf[i];
        s->tag[push_idx] = tag;
    }
    s->num += len;
    return PL330_FIFO_OK;
}

/* Get LEN bytes of data from MFIFO and store it to BUF. Tag value of each
 * byte is verified. Zero returned on success, PL330_FIFO_ERR on tag mismatch
 * and PL330_FIFO_STALL if there is no enough data in MFIFO. If get was
 * unsuccessful no data is removed from MFIFO.
 */

static int pl330_fifo_get(PL330Fifo *s, uint8_t *buf, int len, uint8_t tag)
{
    int i;

    if (s->num < len) {
        return PL330_FIFO_STALL;
    }
    for (i = 0; i < len; i++) {
        if (s->tag[s->head] == tag) {
            int get_idx = (s->head + i) % s->buf_size;
            buf[i] = s->buf[get_idx];
        } else { /* Tag mismatch - Rollback transaction */
            return PL330_FIFO_ERR;
        }
    }
    s->head = (s->head + len) % s->buf_size;
    s->num -= len;
    return PL330_FIFO_OK;
}

/* Reset MFIFO. This completely erases all data in it. */

static inline void pl330_fifo_reset(PL330Fifo *s)
{
    s->head = 0;
    s->num = 0;
}

/* Return tag of the first byte stored in MFIFO. If MFIFO is empty
 * PL330_UNTAGGED is returned.
 */

static inline uint8_t pl330_fifo_tag(PL330Fifo *s)
{
    return (!s->num) ? PL330_UNTAGGED : s->tag[s->head];
}

/* Returns non-zero if tag TAG is present in fifo or zero otherwise */

static int pl330_fifo_has_tag(PL330Fifo *s, uint8_t tag)
{
    int i, n;

    i = s->head;
    for (n = 0; n < s->num; n++) {
        if (s->tag[i] == tag) {
            return 1;
        }
        i = pl330_fifo_inc(s, i);
    }
    return 0;
}

/* Remove all entry tagged with TAG from MFIFO */

static void pl330_fifo_tagged_remove(PL330Fifo *s, uint8_t tag)
{
    int i, t, n;

    t = i = s->head;
    for (n = 0; n < s->num; n++) {
        if (s->tag[i] != tag) {
            s->buf[t] = s->buf[i];
            s->tag[t] = s->tag[i];
            t = pl330_fifo_inc(s, t);
        } else {
            s->num = s->num - 1;
        }
        i = pl330_fifo_inc(s, i);
    }
}

/* Read-Write Queue implementation
 *
 * A Read-Write Queue stores up to QUEUE_SIZE instructions (loads or stores).
 * Each instruction is described by source (for loads) or destination (for
 * stores) address ADDR, width of data to be loaded/stored LEN, number of
 * stores/loads to be performed N, INC bit, Z bit and TAG to identify channel
 * this instruction belongs to. Queue does not store any information about
 * nature of the instruction: is it load or store. PL330 has different queues
 * for loads and stores so this is already known at the top level where it
 * matters.
 *
 * Queue works as FIFO for instructions with equivalent tags, but can issue
 * instructions with different tags in arbitrary order. SEQN field attached to
 * each instruction helps to achieve this. For each TAG queue contains
 * instructions with consecutive SEQN values ranging from LO_SEQN[TAG] to
 * HI_SEQN[TAG]-1 inclusive. SEQN is 8-bit unsigned integer, so SEQN=255 is
 * followed by SEQN=0.
 *
 * Z bit indicates that zeroes should be stored. No MFIFO fetches are performed
 * in this case.
 */

static void pl330_queue_reset(PL330Queue *s)
{
    int i;

    for (i = 0; i < s->queue_size; i++) {
        s->queue[i].tag = PL330_UNTAGGED;
    }
}

/* Initialize queue */
static void pl330_queue_init(PL330Queue *s, int size, PL330State *parent)
{
    s->parent = parent;
    s->queue = g_new0(PL330QueueEntry, size);
    s->queue_size = size;
}

/* Returns pointer to an empty slot or NULL if queue is full */
static PL330QueueEntry *pl330_queue_find_empty(PL330Queue *s)
{
    int i;

    for (i = 0; i < s->queue_size; i++) {
        if (s->queue[i].tag == PL330_UNTAGGED) {
            return &s->queue[i];
        }
    }
    return NULL;
}

/* Put instruction in queue.
 * Return value:
 * - zero - OK
 * - non-zero - queue is full
 */

static int pl330_queue_put_insn(PL330Queue *s, uint32_t addr,
                                int len, int n, bool inc, bool z, uint8_t tag)
{
    PL330QueueEntry *entry = pl330_queue_find_empty(s);

    if (!entry) {
        return 1;
    }
    entry->tag = tag;
    entry->addr = addr;
    entry->len = len;
    entry->n = n;
    entry->z = z;
    entry->inc = inc;
    entry->seqn = s->parent->hi_seqn[tag];
    s->parent->hi_seqn[tag]++;
    return 0;
}

/* Returns a pointer to queue slot containing instruction which satisfies
 *  following conditions:
 *   - it has valid tag value (not PL330_UNTAGGED)
 *   - if enforce_seq is set it has to be issuable without violating queue
 *     logic (see above)
 *   - if TAG argument is not PL330_UNTAGGED this instruction has tag value
 *     equivalent to the argument TAG value.
 *  If such instruction cannot be found NULL is returned.
 */

static PL330QueueEntry *pl330_queue_find_insn(PL330Queue *s, uint8_t tag,
                                              bool enforce_seq)
{
    int i;

    for (i = 0; i < s->queue_size; i++) {
        if (s->queue[i].tag != PL330_UNTAGGED) {
            if ((!enforce_seq ||
                    s->queue[i].seqn == s->parent->lo_seqn[s->queue[i].tag]) &&
                    (s->queue[i].tag == tag || tag == PL330_UNTAGGED ||
                    s->queue[i].z)) {
                return &s->queue[i];
            }
        }
    }
    return NULL;
}

/* Removes instruction from queue. */

static inline void pl330_queue_remove_insn(PL330Queue *s, PL330QueueEntry *e)
{
    s->parent->lo_seqn[e->tag]++;
    e->tag = PL330_UNTAGGED;
}

/* Removes all instructions tagged with TAG from queue. */

static inline void pl330_queue_remove_tagged(PL330Queue *s, uint8_t tag)
{
    int i;

    for (i = 0; i < s->queue_size; i++) {
        if (s->queue[i].tag == tag) {
            s->queue[i].tag = PL330_UNTAGGED;
        }
    }
}

/* DMA instruction execution engine */

/* Moves DMA channel to the FAULT state and updates it's status. */

static inline void pl330_fault(PL330Chan *ch, uint32_t flags)
{
    DB_PRINT("ch: %p, flags: %" PRIx32 "\n", ch, flags);
    ch->fault_type |= flags;
    if (ch->state == pl330_chan_fault) {
        return;
    }
    ch->state = pl330_chan_fault;
    ch->parent->num_faulting++;
    if (ch->parent->num_faulting == 1) {
        DB_PRINT("abort interrupt raised\n");
        qemu_irq_raise(ch->parent->irq_abort);
    }
}

/*
 * For information about instructions see PL330 Technical Reference Manual.
 *
 * Arguments:
 *   CH - channel executing the instruction
 *   OPCODE - opcode
 *   ARGS - array of 8-bit arguments
 *   LEN - number of elements in ARGS array
 */

static void pl330_dmaadxh(PL330Chan *ch, uint8_t *args, bool ra, bool neg)
{
    uint32_t im = (args[1] << 8) | args[0];
    if (neg) {
        im |= 0xffffu << 16;
    }

    if (ch->is_manager) {
        pl330_fault(ch, PL330_FAULT_UNDEF_INSTR);
        return;
    }
    if (ra) {
        ch->dst += im;
    } else {
        ch->src += im;
    }
}

static void pl330_dmaaddh(PL330Chan *ch, uint8_t opcode, uint8_t *args, int len)
{
    pl330_dmaadxh(ch, args, extract32(opcode, 1, 1), false);
}

static void pl330_dmaadnh(PL330Chan *ch, uint8_t opcode, uint8_t *args, int len)
{
    pl330_dmaadxh(ch, args, extract32(opcode, 1, 1), true);
}

static void pl330_dmaend(PL330Chan *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    PL330State *s = ch->parent;

    if (ch->state == pl330_chan_executing && !ch->is_manager) {
        /* Wait for all transfers to complete */
        if (pl330_fifo_has_tag(&s->fifo, ch->tag) ||
            pl330_queue_find_insn(&s->read_queue, ch->tag, false) != NULL ||
            pl330_queue_find_insn(&s->write_queue, ch->tag, false) != NULL) {

            ch->stall = 1;
            return;
        }
    }
    DB_PRINT("DMA ending!\n");
    pl330_fifo_tagged_remove(&s->fifo, ch->tag);
    pl330_queue_remove_tagged(&s->read_queue, ch->tag);
    pl330_queue_remove_tagged(&s->write_queue, ch->tag);
    ch->state = pl330_chan_stopped;
}

static void pl330_dmaflushp(PL330Chan *ch, uint8_t opcode,
                                            uint8_t *args, int len)
{
    uint8_t periph_id;

    if (args[0] & 7) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    periph_id = (args[0] >> 3) & 0x1f;
    if (periph_id >= ch->parent->num_periph_req) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (ch->ns && !(ch->parent->cfg[CFG_PNS] & (1 << periph_id))) {
        pl330_fault(ch, PL330_FAULT_CH_PERIPH_ERR);
        return;
    }
    /* Do nothing */
}

static void pl330_dmago(PL330Chan *ch, uint8_t opcode, uint8_t *args, int len)
{
    uint8_t chan_id;
    uint8_t ns;
    uint32_t pc;
    PL330Chan *s;

    DB_PRINT("\n");

    if (!ch->is_manager) {
        pl330_fault(ch, PL330_FAULT_UNDEF_INSTR);
        return;
    }
    ns = !!(opcode & 2);
    chan_id = args[0] & 7;
    if ((args[0] >> 3)) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (chan_id >= ch->parent->num_chnls) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    pc = (((uint32_t)args[4]) << 24) | (((uint32_t)args[3]) << 16) |
         (((uint32_t)args[2]) << 8)  | (((uint32_t)args[1]));
    if (ch->parent->chan[chan_id].state != pl330_chan_stopped) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (ch->ns && !ns) {
        pl330_fault(ch, PL330_FAULT_DMAGO_ERR);
        return;
    }
    s = &ch->parent->chan[chan_id];
    s->ns = ns;
    s->pc = pc;
    s->state = pl330_chan_executing;
}

static void pl330_dmald(PL330Chan *ch, uint8_t opcode, uint8_t *args, int len)
{
    uint8_t bs = opcode & 3;
    uint32_t size, num;
    bool inc;

    if (bs == 2) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if ((bs == 1 && ch->request_flag == PL330_BURST) ||
        (bs == 3 && ch->request_flag == PL330_SINGLE)) {
        /* Perform NOP */
        return;
    }
    if (bs == 1 && ch->request_flag == PL330_SINGLE) {
        num = 1;
    } else {
        num = ((ch->control >> 4) & 0xf) + 1;
    }
    size = (uint32_t)1 << ((ch->control >> 1) & 0x7);
    inc = !!(ch->control & 1);
    ch->stall = pl330_queue_put_insn(&ch->parent->read_queue, ch->src,
                                    size, num, inc, 0, ch->tag);
    if (!ch->stall) {
        DB_PRINT("channel:%" PRId8 " address:%08" PRIx32 " size:%" PRIx32
                 " num:%" PRId32 " %c\n",
                 ch->tag, ch->src, size, num, inc ? 'Y' : 'N');
        ch->src += inc ? size * num - (ch->src & (size - 1)) : 0;
    }
}

static void pl330_dmaldp(PL330Chan *ch, uint8_t opcode, uint8_t *args, int len)
{
    uint8_t periph_id;

    if (args[0] & 7) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    periph_id = (args[0] >> 3) & 0x1f;
    if (periph_id >= ch->parent->num_periph_req) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (ch->ns && !(ch->parent->cfg[CFG_PNS] & (1 << periph_id))) {
        pl330_fault(ch, PL330_FAULT_CH_PERIPH_ERR);
        return;
    }
    pl330_dmald(ch, opcode, args, len);
}

static void pl330_dmalp(PL330Chan *ch, uint8_t opcode, uint8_t *args, int len)
{
    uint8_t lc = (opcode & 2) >> 1;

    ch->lc[lc] = args[0];
}

static void pl330_dmakill(PL330Chan *ch, uint8_t opcode, uint8_t *args, int len)
{
    if (ch->state == pl330_chan_fault ||
        ch->state == pl330_chan_fault_completing) {
        /* This is the only way for a channel to leave the faulting state */
        ch->fault_type = 0;
        ch->parent->num_faulting--;
        if (ch->parent->num_faulting == 0) {
            DB_PRINT("abort interrupt lowered\n");
            qemu_irq_lower(ch->parent->irq_abort);
        }
    }
    ch->state = pl330_chan_killing;
    pl330_fifo_tagged_remove(&ch->parent->fifo, ch->tag);
    pl330_queue_remove_tagged(&ch->parent->read_queue, ch->tag);
    pl330_queue_remove_tagged(&ch->parent->write_queue, ch->tag);
    ch->state = pl330_chan_stopped;
}

static void pl330_dmalpend(PL330Chan *ch, uint8_t opcode,
                                    uint8_t *args, int len)
{
    uint8_t nf = (opcode & 0x10) >> 4;
    uint8_t bs = opcode & 3;
    uint8_t lc = (opcode & 4) >> 2;

    if (bs == 2) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if ((bs == 1 && ch->request_flag == PL330_BURST) ||
        (bs == 3 && ch->request_flag == PL330_SINGLE)) {
        /* Perform NOP */
        return;
    }
    if (!nf || ch->lc[lc]) {
        if (nf) {
            ch->lc[lc]--;
        }
        DB_PRINT("loop reiteration\n");
        ch->pc -= args[0];
        ch->pc -= len + 1;
        /* "ch->pc -= args[0] + len + 1" is incorrect when args[0] == 256 */
    } else {
        DB_PRINT("loop fallthrough\n");
    }
}


static void pl330_dmamov(PL330Chan *ch, uint8_t opcode, uint8_t *args, int len)
{
    uint8_t rd = args[0] & 7;
    uint32_t im;

    if ((args[0] >> 3)) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    im = (((uint32_t)args[4]) << 24) | (((uint32_t)args[3]) << 16) |
         (((uint32_t)args[2]) << 8)  | (((uint32_t)args[1]));
    switch (rd) {
    case 0:
        ch->src = im;
        break;
    case 1:
        ch->control = im;
        break;
    case 2:
        ch->dst = im;
        break;
    default:
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
}

static void pl330_dmanop(PL330Chan *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    /* NOP is NOP. */
}

static void pl330_dmarmb(PL330Chan *ch, uint8_t opcode, uint8_t *args, int len)
{
   if (pl330_queue_find_insn(&ch->parent->read_queue, ch->tag, false)) {
        ch->state = pl330_chan_at_barrier;
        ch->stall = 1;
        return;
    } else {
        ch->state = pl330_chan_executing;
    }
}

static void pl330_dmasev(PL330Chan *ch, uint8_t opcode, uint8_t *args, int len)
{
    uint8_t ev_id;

    if (args[0] & 7) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    ev_id = (args[0] >> 3) & 0x1f;
    if (ev_id >= ch->parent->num_events) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (ch->ns && !(ch->parent->cfg[CFG_INS] & (1 << ev_id))) {
        pl330_fault(ch, PL330_FAULT_EVENT_ERR);
        return;
    }
    if (ch->parent->inten & (1 << ev_id)) {
        ch->parent->int_status |= (1 << ev_id);
        DB_PRINT("event interrupt raised %" PRId8 "\n", ev_id);
        qemu_irq_raise(ch->parent->irq[ev_id]);
    }
    DB_PRINT("event raised %" PRId8 "\n", ev_id);
    ch->parent->ev_status |= (1 << ev_id);
}

static void pl330_dmast(PL330Chan *ch, uint8_t opcode, uint8_t *args, int len)
{
    uint8_t bs = opcode & 3;
    uint32_t size, num;
    bool inc;

    if (bs == 2) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if ((bs == 1 && ch->request_flag == PL330_BURST) ||
        (bs == 3 && ch->request_flag == PL330_SINGLE)) {
        /* Perform NOP */
        return;
    }
    num = ((ch->control >> 18) & 0xf) + 1;
    size = (uint32_t)1 << ((ch->control >> 15) & 0x7);
    inc = !!((ch->control >> 14) & 1);
    ch->stall = pl330_queue_put_insn(&ch->parent->write_queue, ch->dst,
                                    size, num, inc, 0, ch->tag);
    if (!ch->stall) {
        DB_PRINT("channel:%" PRId8 " address:%08" PRIx32 " size:%" PRIx32
                 " num:%" PRId32 " %c\n",
                 ch->tag, ch->dst, size, num, inc ? 'Y' : 'N');
        ch->dst += inc ? size * num - (ch->dst & (size - 1)) : 0;
    }
}

static void pl330_dmastp(PL330Chan *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    uint8_t periph_id;

    if (args[0] & 7) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    periph_id = (args[0] >> 3) & 0x1f;
    if (periph_id >= ch->parent->num_periph_req) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (ch->ns && !(ch->parent->cfg[CFG_PNS] & (1 << periph_id))) {
        pl330_fault(ch, PL330_FAULT_CH_PERIPH_ERR);
        return;
    }
    pl330_dmast(ch, opcode, args, len);
}

static void pl330_dmastz(PL330Chan *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    uint32_t size, num;
    bool inc;

    num = ((ch->control >> 18) & 0xf) + 1;
    size = (uint32_t)1 << ((ch->control >> 15) & 0x7);
    inc = !!((ch->control >> 14) & 1);
    ch->stall = pl330_queue_put_insn(&ch->parent->write_queue, ch->dst,
                                    size, num, inc, 1, ch->tag);
    if (inc) {
        ch->dst += size * num;
    }
}

static void pl330_dmawfe(PL330Chan *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    uint8_t ev_id;
    int i;

    if (args[0] & 5) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    ev_id = (args[0] >> 3) & 0x1f;
    if (ev_id >= ch->parent->num_events) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (ch->ns && !(ch->parent->cfg[CFG_INS] & (1 << ev_id))) {
        pl330_fault(ch, PL330_FAULT_EVENT_ERR);
        return;
    }
    ch->wakeup = ev_id;
    ch->state = pl330_chan_waiting_event;
    if (~ch->parent->inten & ch->parent->ev_status & 1 << ev_id) {
        ch->state = pl330_chan_executing;
        /* If anyone else is currently waiting on the same event, let them
         * clear the ev_status so they pick up event as well
         */
        for (i = 0; i < ch->parent->num_chnls; ++i) {
            PL330Chan *peer = &ch->parent->chan[i];
            if (peer->state == pl330_chan_waiting_event &&
                    peer->wakeup == ev_id) {
                return;
            }
        }
        ch->parent->ev_status &= ~(1 << ev_id);
        DB_PRINT("event lowered %" PRIx8 "\n", ev_id);
    } else {
        ch->stall = 1;
    }
}

static void pl330_dmawfp(PL330Chan *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    uint8_t bs = opcode & 3;
    uint8_t periph_id;

    if (args[0] & 7) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    periph_id = (args[0] >> 3) & 0x1f;
    if (periph_id >= ch->parent->num_periph_req) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (ch->ns && !(ch->parent->cfg[CFG_PNS] & (1 << periph_id))) {
        pl330_fault(ch, PL330_FAULT_CH_PERIPH_ERR);
        return;
    }
    switch (bs) {
    case 0: /* S */
        ch->request_flag = PL330_SINGLE;
        ch->wfp_sbp = 0;
        break;
    case 1: /* P */
        ch->request_flag = PL330_BURST;
        ch->wfp_sbp = 2;
        break;
    case 2: /* B */
        ch->request_flag = PL330_BURST;
        ch->wfp_sbp = 1;
        break;
    default:
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }

    if (ch->parent->periph_busy[periph_id]) {
        ch->state = pl330_chan_waiting_periph;
        ch->stall = 1;
    } else if (ch->state == pl330_chan_waiting_periph) {
        ch->state = pl330_chan_executing;
    }
}

static void pl330_dmawmb(PL330Chan *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    if (pl330_queue_find_insn(&ch->parent->write_queue, ch->tag, false)) {
        ch->state = pl330_chan_at_barrier;
        ch->stall = 1;
        return;
    } else {
        ch->state = pl330_chan_executing;
    }
}

/* NULL terminated array of the instruction descriptions. */
static const PL330InsnDesc insn_desc[] = {
    { .opcode = 0x54, .opmask = 0xFD, .size = 3, .exec = pl330_dmaaddh, },
    { .opcode = 0x5c, .opmask = 0xFD, .size = 3, .exec = pl330_dmaadnh, },
    { .opcode = 0x00, .opmask = 0xFF, .size = 1, .exec = pl330_dmaend, },
    { .opcode = 0x35, .opmask = 0xFF, .size = 2, .exec = pl330_dmaflushp, },
    { .opcode = 0xA0, .opmask = 0xFD, .size = 6, .exec = pl330_dmago, },
    { .opcode = 0x04, .opmask = 0xFC, .size = 1, .exec = pl330_dmald, },
    { .opcode = 0x25, .opmask = 0xFD, .size = 2, .exec = pl330_dmaldp, },
    { .opcode = 0x20, .opmask = 0xFD, .size = 2, .exec = pl330_dmalp, },
    /* dmastp  must be before dmalpend in this list, because their maps
     * are overlapping
     */
    { .opcode = 0x29, .opmask = 0xFD, .size = 2, .exec = pl330_dmastp, },
    { .opcode = 0x28, .opmask = 0xE8, .size = 2, .exec = pl330_dmalpend, },
    { .opcode = 0x01, .opmask = 0xFF, .size = 1, .exec = pl330_dmakill, },
    { .opcode = 0xBC, .opmask = 0xFF, .size = 6, .exec = pl330_dmamov, },
    { .opcode = 0x18, .opmask = 0xFF, .size = 1, .exec = pl330_dmanop, },
    { .opcode = 0x12, .opmask = 0xFF, .size = 1, .exec = pl330_dmarmb, },
    { .opcode = 0x34, .opmask = 0xFF, .size = 2, .exec = pl330_dmasev, },
    { .opcode = 0x08, .opmask = 0xFC, .size = 1, .exec = pl330_dmast, },
    { .opcode = 0x0C, .opmask = 0xFF, .size = 1, .exec = pl330_dmastz, },
    { .opcode = 0x36, .opmask = 0xFF, .size = 2, .exec = pl330_dmawfe, },
    { .opcode = 0x30, .opmask = 0xFC, .size = 2, .exec = pl330_dmawfp, },
    { .opcode = 0x13, .opmask = 0xFF, .size = 1, .exec = pl330_dmawmb, },
    { .opcode = 0x00, .opmask = 0x00, .size = 0, .exec = NULL, }
};

/* Instructions which can be issued via debug registers. */
static const PL330InsnDesc debug_insn_desc[] = {
    { .opcode = 0xA0, .opmask = 0xFD, .size = 6, .exec = pl330_dmago, },
    { .opcode = 0x01, .opmask = 0xFF, .size = 1, .exec = pl330_dmakill, },
    { .opcode = 0x34, .opmask = 0xFF, .size = 2, .exec = pl330_dmasev, },
    { .opcode = 0x00, .opmask = 0x00, .size = 0, .exec = NULL, }
};

static inline const PL330InsnDesc *pl330_fetch_insn(PL330Chan *ch)
{
    uint8_t opcode;
    int i;

    dma_memory_read(&address_space_memory, ch->pc, &opcode, 1);
    for (i = 0; insn_desc[i].size; i++) {
        if ((opcode & insn_desc[i].opmask) == insn_desc[i].opcode) {
            return &insn_desc[i];
        }
    }
    return NULL;
}

static inline void pl330_exec_insn(PL330Chan *ch, const PL330InsnDesc *insn)
{
    uint8_t buf[PL330_INSN_MAXSIZE];

    assert(insn->size <= PL330_INSN_MAXSIZE);
    dma_memory_read(&address_space_memory, ch->pc, buf, insn->size);
    insn->exec(ch, buf[0], &buf[1], insn->size - 1);
}

static inline void pl330_update_pc(PL330Chan *ch,
                                   const PL330InsnDesc *insn)
{
    ch->pc += insn->size;
}

/* Try to execute current instruction in channel CH. Number of executed
   instructions is returned (0 or 1). */
static int pl330_chan_exec(PL330Chan *ch)
{
    const PL330InsnDesc *insn;

    if (ch->state != pl330_chan_executing &&
            ch->state != pl330_chan_waiting_periph &&
            ch->state != pl330_chan_at_barrier &&
            ch->state != pl330_chan_waiting_event) {
        return 0;
    }
    ch->stall = 0;
    insn = pl330_fetch_insn(ch);
    if (!insn) {
        DB_PRINT("pl330 undefined instruction\n");
        pl330_fault(ch, PL330_FAULT_UNDEF_INSTR);
        return 0;
    }
    pl330_exec_insn(ch, insn);
    if (!ch->stall) {
        pl330_update_pc(ch, insn);
        ch->watchdog_timer = 0;
        return 1;
    /* WDT only active in exec state */
    } else if (ch->state == pl330_chan_executing) {
        ch->watchdog_timer++;
        if (ch->watchdog_timer >= PL330_WATCHDOG_LIMIT) {
            pl330_fault(ch, PL330_FAULT_LOCKUP_ERR);
        }
    }
    return 0;
}

/* Try to execute 1 instruction in each channel, one instruction from read
   queue and one instruction from write queue. Number of successfully executed
   instructions is returned. */
static int pl330_exec_cycle(PL330Chan *channel)
{
    PL330State *s = channel->parent;
    PL330QueueEntry *q;
    int i;
    int num_exec = 0;
    int fifo_res = 0;
    uint8_t buf[PL330_MAX_BURST_LEN];

    /* Execute one instruction in each channel */
    num_exec += pl330_chan_exec(channel);

    /* Execute one instruction from read queue */
    q = pl330_queue_find_insn(&s->read_queue, PL330_UNTAGGED, true);
    if (q != NULL && q->len <= pl330_fifo_num_free(&s->fifo)) {
        int len = q->len - (q->addr & (q->len - 1));

        dma_memory_read(&address_space_memory, q->addr, buf, len);
        if (PL330_ERR_DEBUG > 1) {
            DB_PRINT("PL330 read from memory @%08" PRIx32 " (size = %08x):\n",
                      q->addr, len);
            qemu_hexdump((char *)buf, stderr, "", len);
        }
        fifo_res = pl330_fifo_push(&s->fifo, buf, len, q->tag);
        if (fifo_res == PL330_FIFO_OK) {
            if (q->inc) {
                q->addr += len;
            }
            q->n--;
            if (!q->n) {
                pl330_queue_remove_insn(&s->read_queue, q);
            }
            num_exec++;
        }
    }

    /* Execute one instruction from write queue. */
    q = pl330_queue_find_insn(&s->write_queue, pl330_fifo_tag(&s->fifo), true);
    if (q != NULL) {
        int len = q->len - (q->addr & (q->len - 1));

        if (q->z) {
            for (i = 0; i < len; i++) {
                buf[i] = 0;
            }
        } else {
            fifo_res = pl330_fifo_get(&s->fifo, buf, len, q->tag);
        }
        if (fifo_res == PL330_FIFO_OK || q->z) {
            dma_memory_write(&address_space_memory, q->addr, buf, len);
            if (PL330_ERR_DEBUG > 1) {
                DB_PRINT("PL330 read from memory @%08" PRIx32
                         " (size = %08x):\n", q->addr, len);
                qemu_hexdump((char *)buf, stderr, "", len);
            }
            if (q->inc) {
                q->addr += len;
            }
            num_exec++;
        } else if (fifo_res == PL330_FIFO_STALL) {
            pl330_fault(&channel->parent->chan[q->tag],
                                PL330_FAULT_FIFOEMPTY_ERR);
        }
        q->n--;
        if (!q->n) {
            pl330_queue_remove_insn(&s->write_queue, q);
        }
    }

    return num_exec;
}

static int pl330_exec_channel(PL330Chan *channel)
{
    int insr_exec = 0;

    /* TODO: Is it all right to execute everything or should we do per-cycle
       simulation? */
    while (pl330_exec_cycle(channel)) {
        insr_exec++;
    }

    /* Detect deadlock */
    if (channel->state == pl330_chan_executing) {
        pl330_fault(channel, PL330_FAULT_LOCKUP_ERR);
    }
    /* Situation when one of the queues has deadlocked but all channels
     * have finished their programs should be impossible.
     */

    return insr_exec;
}

static inline void pl330_exec(PL330State *s)
{
    DB_PRINT("\n");
    int i, insr_exec;
    do {
        insr_exec = pl330_exec_channel(&s->manager);

        for (i = 0; i < s->num_chnls; i++) {
            insr_exec += pl330_exec_channel(&s->chan[i]);
        }
    } while (insr_exec);
}

static void pl330_exec_cycle_timer(void *opaque)
{
    PL330State *s = (PL330State *)opaque;
    pl330_exec(s);
}

/* Stop or restore dma operations */

static void pl330_dma_stop_irq(void *opaque, int irq, int level)
{
    PL330State *s = (PL330State *)opaque;

    if (s->periph_busy[irq] != level) {
        s->periph_busy[irq] = level;
        timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    }
}

static void pl330_debug_exec(PL330State *s)
{
    uint8_t args[5];
    uint8_t opcode;
    uint8_t chan_id;
    int i;
    PL330Chan *ch;
    const PL330InsnDesc *insn;

    s->debug_status = 1;
    chan_id = (s->dbg[0] >>  8) & 0x07;
    opcode  = (s->dbg[0] >> 16) & 0xff;
    args[0] = (s->dbg[0] >> 24) & 0xff;
    args[1] = (s->dbg[1] >>  0) & 0xff;
    args[2] = (s->dbg[1] >>  8) & 0xff;
    args[3] = (s->dbg[1] >> 16) & 0xff;
    args[4] = (s->dbg[1] >> 24) & 0xff;
    DB_PRINT("chan id: %" PRIx8 "\n", chan_id);
    if (s->dbg[0] & 1) {
        ch = &s->chan[chan_id];
    } else {
        ch = &s->manager;
    }
    insn = NULL;
    for (i = 0; debug_insn_desc[i].size; i++) {
        if ((opcode & debug_insn_desc[i].opmask) == debug_insn_desc[i].opcode) {
            insn = &debug_insn_desc[i];
        }
    }
    if (!insn) {
        pl330_fault(ch, PL330_FAULT_UNDEF_INSTR | PL330_FAULT_DBG_INSTR);
        return ;
    }
    ch->stall = 0;
    insn->exec(ch, opcode, args, insn->size - 1);
    if (ch->fault_type) {
        ch->fault_type |= PL330_FAULT_DBG_INSTR;
    }
    if (ch->stall) {
        qemu_log_mask(LOG_UNIMP, "pl330: stall of debug instruction not "
                      "implemented\n");
    }
    s->debug_status = 0;
}

/* IOMEM mapped registers */

static void pl330_iomem_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    PL330State *s = (PL330State *) opaque;
    int i;

    DB_PRINT("addr: %08x data: %08x\n", (unsigned)offset, (unsigned)value);

    switch (offset) {
    case PL330_REG_INTEN:
        s->inten = value;
        break;
    case PL330_REG_INTCLR:
        for (i = 0; i < s->num_events; i++) {
            if (s->int_status & s->inten & value & (1 << i)) {
                DB_PRINT("event interrupt lowered %d\n", i);
                qemu_irq_lower(s->irq[i]);
            }
        }
        s->ev_status &= ~(value & s->inten);
        s->int_status &= ~(value & s->inten);
        break;
    case PL330_REG_DBGCMD:
        if ((value & 3) == 0) {
            pl330_debug_exec(s);
            pl330_exec(s);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "pl330: write of illegal value %u "
                          "for offset " TARGET_FMT_plx "\n", (unsigned)value,
                          offset);
        }
        break;
    case PL330_REG_DBGINST0:
        DB_PRINT("s->dbg[0] = %08x\n", (unsigned)value);
        s->dbg[0] = value;
        break;
    case PL330_REG_DBGINST1:
        DB_PRINT("s->dbg[1] = %08x\n", (unsigned)value);
        s->dbg[1] = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "pl330: bad write offset " TARGET_FMT_plx
                      "\n", offset);
        break;
    }
}

static inline uint32_t pl330_iomem_read_imp(void *opaque,
        hwaddr offset)
{
    PL330State *s = (PL330State *)opaque;
    int chan_id;
    int i;
    uint32_t res;

    if (offset >= PL330_REG_PERIPH_ID && offset < PL330_REG_PERIPH_ID + 32) {
        return pl330_id[(offset - PL330_REG_PERIPH_ID) >> 2];
    }
    if (offset >= PL330_REG_CR0_BASE && offset < PL330_REG_CR0_BASE + 24) {
        return s->cfg[(offset - PL330_REG_CR0_BASE) >> 2];
    }
    if (offset >= PL330_REG_CHANCTRL && offset < PL330_REG_DBGSTATUS) {
        offset -= PL330_REG_CHANCTRL;
        chan_id = offset >> 5;
        if (chan_id >= s->num_chnls) {
            qemu_log_mask(LOG_GUEST_ERROR, "pl330: bad read offset "
                          TARGET_FMT_plx "\n", offset);
            return 0;
        }
        switch (offset & 0x1f) {
        case 0x00:
            return s->chan[chan_id].src;
        case 0x04:
            return s->chan[chan_id].dst;
        case 0x08:
            return s->chan[chan_id].control;
        case 0x0C:
            return s->chan[chan_id].lc[0];
        case 0x10:
            return s->chan[chan_id].lc[1];
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "pl330: bad read offset "
                          TARGET_FMT_plx "\n", offset);
            return 0;
        }
    }
    if (offset >= PL330_REG_CSR_BASE && offset < 0x400) {
        offset -= PL330_REG_CSR_BASE;
        chan_id = offset >> 3;
        if (chan_id >= s->num_chnls) {
            qemu_log_mask(LOG_GUEST_ERROR, "pl330: bad read offset "
                          TARGET_FMT_plx "\n", offset);
            return 0;
        }
        switch ((offset >> 2) & 1) {
        case 0x0:
            res = (s->chan[chan_id].ns << 21) |
                    (s->chan[chan_id].wakeup << 4) |
                    (s->chan[chan_id].state) |
                    (s->chan[chan_id].wfp_sbp << 14);
            return res;
        case 0x1:
            return s->chan[chan_id].pc;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "pl330: read error\n");
            return 0;
        }
    }
    if (offset >= PL330_REG_FTR_BASE && offset < 0x100) {
        offset -= PL330_REG_FTR_BASE;
        chan_id = offset >> 2;
        if (chan_id >= s->num_chnls) {
            qemu_log_mask(LOG_GUEST_ERROR, "pl330: bad read offset "
                          TARGET_FMT_plx "\n", offset);
            return 0;
        }
        return s->chan[chan_id].fault_type;
    }
    switch (offset) {
    case PL330_REG_DSR:
        return (s->manager.ns << 9) | (s->manager.wakeup << 4) |
            (s->manager.state & 0xf);
    case PL330_REG_DPC:
        return s->manager.pc;
    case PL330_REG_INTEN:
        return s->inten;
    case PL330_REG_INT_EVENT_RIS:
        return s->ev_status;
    case PL330_REG_INTMIS:
        return s->int_status;
    case PL330_REG_INTCLR:
        /* Documentation says that we can't read this register
         * but linux kernel does it
         */
        return 0;
    case PL330_REG_FSRD:
        return s->manager.state ? 1 : 0;
    case PL330_REG_FSRC:
        res = 0;
        for (i = 0; i < s->num_chnls; i++) {
            if (s->chan[i].state == pl330_chan_fault ||
                s->chan[i].state == pl330_chan_fault_completing) {
                res |= 1 << i;
            }
        }
        return res;
    case PL330_REG_FTRD:
        return s->manager.fault_type;
    case PL330_REG_DBGSTATUS:
        return s->debug_status;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "pl330: bad read offset "
                      TARGET_FMT_plx "\n", offset);
    }
    return 0;
}

static uint64_t pl330_iomem_read(void *opaque, hwaddr offset,
        unsigned size)
{
    uint32_t ret = pl330_iomem_read_imp(opaque, offset);
    DB_PRINT("addr: %08" HWADDR_PRIx " data: %08" PRIx32 "\n", offset, ret);
    return ret;
}

static const MemoryRegionOps pl330_ops = {
    .read = pl330_iomem_read,
    .write = pl330_iomem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

/* Controller logic and initialization */

static void pl330_chan_reset(PL330Chan *ch)
{
    ch->src = 0;
    ch->dst = 0;
    ch->pc = 0;
    ch->state = pl330_chan_stopped;
    ch->watchdog_timer = 0;
    ch->stall = 0;
    ch->control = 0;
    ch->status = 0;
    ch->fault_type = 0;
}

static void pl330_reset(DeviceState *d)
{
    int i;
    PL330State *s = PL330(d);

    s->inten = 0;
    s->int_status = 0;
    s->ev_status = 0;
    s->debug_status = 0;
    s->num_faulting = 0;
    s->manager.ns = s->mgr_ns_at_rst;
    pl330_fifo_reset(&s->fifo);
    pl330_queue_reset(&s->read_queue);
    pl330_queue_reset(&s->write_queue);

    for (i = 0; i < s->num_chnls; i++) {
        pl330_chan_reset(&s->chan[i]);
    }
    for (i = 0; i < s->num_periph_req; i++) {
        s->periph_busy[i] = 0;
    }

    timer_del(s->timer);
}

static void pl330_realize(DeviceState *dev, Error **errp)
{
    int i;
    PL330State *s = PL330(dev);

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq_abort);
    memory_region_init_io(&s->iomem, OBJECT(s), &pl330_ops, s,
                          "dma", PL330_IOMEM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, pl330_exec_cycle_timer, s);

    s->cfg[0] = (s->mgr_ns_at_rst ? 0x4 : 0) |
                (s->num_periph_req > 0 ? 1 : 0) |
                ((s->num_chnls - 1) & 0x7) << 4 |
                ((s->num_periph_req - 1) & 0x1f) << 12 |
                ((s->num_events - 1) & 0x1f) << 17;

    switch (s->i_cache_len) {
    case (4):
        s->cfg[1] |= 2;
        break;
    case (8):
        s->cfg[1] |= 3;
        break;
    case (16):
        s->cfg[1] |= 4;
        break;
    case (32):
        s->cfg[1] |= 5;
        break;
    default:
        error_setg(errp, "Bad value for i-cache_len property: %" PRIx8,
                   s->i_cache_len);
        return;
    }
    s->cfg[1] |= ((s->num_i_cache_lines - 1) & 0xf) << 4;

    s->chan = g_new0(PL330Chan, s->num_chnls);
    s->hi_seqn = g_new0(uint8_t, s->num_chnls);
    s->lo_seqn = g_new0(uint8_t, s->num_chnls);
    for (i = 0; i < s->num_chnls; i++) {
        s->chan[i].parent = s;
        s->chan[i].tag = (uint8_t)i;
    }
    s->manager.parent = s;
    s->manager.tag = s->num_chnls;
    s->manager.is_manager = true;

    s->irq = g_new0(qemu_irq, s->num_events);
    for (i = 0; i < s->num_events; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq[i]);
    }

    qdev_init_gpio_in(dev, pl330_dma_stop_irq, PL330_PERIPH_NUM);

    switch (s->data_width) {
    case (32):
        s->cfg[CFG_CRD] |= 0x2;
        break;
    case (64):
        s->cfg[CFG_CRD] |= 0x3;
        break;
    case (128):
        s->cfg[CFG_CRD] |= 0x4;
        break;
    default:
        error_setg(errp, "Bad value for data_width property: %" PRIx8,
                   s->data_width);
        return;
    }

    s->cfg[CFG_CRD] |= ((s->wr_cap - 1) & 0x7) << 4 |
                    ((s->wr_q_dep - 1) & 0xf) << 8 |
                    ((s->rd_cap - 1) & 0x7) << 12 |
                    ((s->rd_q_dep - 1) & 0xf) << 16 |
                    ((s->data_buffer_dep - 1) & 0x1ff) << 20;

    pl330_queue_init(&s->read_queue, s->rd_q_dep, s);
    pl330_queue_init(&s->write_queue, s->wr_q_dep, s);
    pl330_fifo_init(&s->fifo, s->data_width / 4 * s->data_buffer_dep);
}

static Property pl330_properties[] = {
    /* CR0 */
    DEFINE_PROP_UINT32("num_chnls", PL330State, num_chnls, 8),
    DEFINE_PROP_UINT8("num_periph_req", PL330State, num_periph_req, 4),
    DEFINE_PROP_UINT8("num_events", PL330State, num_events, 16),
    DEFINE_PROP_UINT8("mgr_ns_at_rst", PL330State, mgr_ns_at_rst, 0),
    /* CR1 */
    DEFINE_PROP_UINT8("i-cache_len", PL330State, i_cache_len, 4),
    DEFINE_PROP_UINT8("num_i-cache_lines", PL330State, num_i_cache_lines, 8),
    /* CR2-4 */
    DEFINE_PROP_UINT32("boot_addr", PL330State, cfg[CFG_BOOT_ADDR], 0),
    DEFINE_PROP_UINT32("INS", PL330State, cfg[CFG_INS], 0),
    DEFINE_PROP_UINT32("PNS", PL330State, cfg[CFG_PNS], 0),
    /* CRD */
    DEFINE_PROP_UINT8("data_width", PL330State, data_width, 64),
    DEFINE_PROP_UINT8("wr_cap", PL330State, wr_cap, 8),
    DEFINE_PROP_UINT8("wr_q_dep", PL330State, wr_q_dep, 16),
    DEFINE_PROP_UINT8("rd_cap", PL330State, rd_cap, 8),
    DEFINE_PROP_UINT8("rd_q_dep", PL330State, rd_q_dep, 16),
    DEFINE_PROP_UINT16("data_buffer_dep", PL330State, data_buffer_dep, 256),

    DEFINE_PROP_END_OF_LIST(),
};

static void pl330_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pl330_realize;
    dc->reset = pl330_reset;
    dc->props = pl330_properties;
    dc->vmsd = &vmstate_pl330;
}

static const TypeInfo pl330_type_info = {
    .name           = TYPE_PL330,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(PL330State),
    .class_init      = pl330_class_init,
};

static void pl330_register_types(void)
{
    type_register_static(&pl330_type_info);
}

type_init(pl330_register_types)
