/*
 * PowerMac descriptor-based DMA emulation
 *
 * Copyright (c) 2005-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
 * Copyright (c) 2009 Laurent Vivier
 *
 * some parts from linux-2.6.28, arch/powerpc/include/asm/dbdma.h
 *
 *   Definitions for using the Apple Descriptor-Based DMA controller
 *   in Power Macintosh computers.
 *
 *   Copyright (C) 1996 Paul Mackerras.
 *
 * some parts from mol 0.9.71
 *
 *   Descriptor based DMA emulation
 *
 *   Copyright (C) 1998-2004 Samuel Rydh (samuel@ibrium.se)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "hw.h"
#include "isa.h"
#include "mac_dbdma.h"

/* debug DBDMA */
//#define DEBUG_DBDMA

#ifdef DEBUG_DBDMA
#define DBDMA_DPRINTF(fmt, ...)                                 \
    do { printf("DBDMA: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DBDMA_DPRINTF(fmt, ...)
#endif

/*
 */

/*
 * DBDMA control/status registers.  All little-endian.
 */

#define DBDMA_CONTROL         0x00
#define DBDMA_STATUS          0x01
#define DBDMA_CMDPTR_HI       0x02
#define DBDMA_CMDPTR_LO       0x03
#define DBDMA_INTR_SEL        0x04
#define DBDMA_BRANCH_SEL      0x05
#define DBDMA_WAIT_SEL        0x06
#define DBDMA_XFER_MODE       0x07
#define DBDMA_DATA2PTR_HI     0x08
#define DBDMA_DATA2PTR_LO     0x09
#define DBDMA_RES1            0x0A
#define DBDMA_ADDRESS_HI      0x0B
#define DBDMA_BRANCH_ADDR_HI  0x0C
#define DBDMA_RES2            0x0D
#define DBDMA_RES3            0x0E
#define DBDMA_RES4            0x0F

#define DBDMA_REGS            16
#define DBDMA_SIZE            (DBDMA_REGS * sizeof(uint32_t))

#define DBDMA_CHANNEL_SHIFT   7
#define DBDMA_CHANNEL_SIZE    (1 << DBDMA_CHANNEL_SHIFT)

#define DBDMA_CHANNELS        (0x1000 >> DBDMA_CHANNEL_SHIFT)

/* Bits in control and status registers */

#define RUN	0x8000
#define PAUSE	0x4000
#define FLUSH	0x2000
#define WAKE	0x1000
#define DEAD	0x0800
#define ACTIVE	0x0400
#define BT	0x0100
#define DEVSTAT	0x00ff

/*
 * DBDMA command structure.  These fields are all little-endian!
 */

typedef struct dbdma_cmd {
    uint16_t req_count;	  /* requested byte transfer count */
    uint16_t command;	  /* command word (has bit-fields) */
    uint32_t phy_addr;	  /* physical data address */
    uint32_t cmd_dep;	  /* command-dependent field */
    uint16_t res_count;	  /* residual count after completion */
    uint16_t xfer_status; /* transfer status */
} dbdma_cmd;

/* DBDMA command values in command field */

#define COMMAND_MASK    0xf000
#define OUTPUT_MORE	0x0000	/* transfer memory data to stream */
#define OUTPUT_LAST	0x1000	/* ditto followed by end marker */
#define INPUT_MORE	0x2000	/* transfer stream data to memory */
#define INPUT_LAST	0x3000	/* ditto, expect end marker */
#define STORE_WORD	0x4000	/* write word (4 bytes) to device reg */
#define LOAD_WORD	0x5000	/* read word (4 bytes) from device reg */
#define DBDMA_NOP	0x6000	/* do nothing */
#define DBDMA_STOP	0x7000	/* suspend processing */

/* Key values in command field */

#define KEY_MASK        0x0700
#define KEY_STREAM0	0x0000	/* usual data stream */
#define KEY_STREAM1	0x0100	/* control/status stream */
#define KEY_STREAM2	0x0200	/* device-dependent stream */
#define KEY_STREAM3	0x0300	/* device-dependent stream */
#define KEY_STREAM4	0x0400	/* reserved */
#define KEY_REGS	0x0500	/* device register space */
#define KEY_SYSTEM	0x0600	/* system memory-mapped space */
#define KEY_DEVICE	0x0700	/* device memory-mapped space */

/* Interrupt control values in command field */

#define INTR_MASK       0x0030
#define INTR_NEVER	0x0000	/* don't interrupt */
#define INTR_IFSET	0x0010	/* intr if condition bit is 1 */
#define INTR_IFCLR	0x0020	/* intr if condition bit is 0 */
#define INTR_ALWAYS	0x0030	/* always interrupt */

/* Branch control values in command field */

#define BR_MASK         0x000c
#define BR_NEVER	0x0000	/* don't branch */
#define BR_IFSET	0x0004	/* branch if condition bit is 1 */
#define BR_IFCLR	0x0008	/* branch if condition bit is 0 */
#define BR_ALWAYS	0x000c	/* always branch */

/* Wait control values in command field */

#define WAIT_MASK       0x0003
#define WAIT_NEVER	0x0000	/* don't wait */
#define WAIT_IFSET	0x0001	/* wait if condition bit is 1 */
#define WAIT_IFCLR	0x0002	/* wait if condition bit is 0 */
#define WAIT_ALWAYS	0x0003	/* always wait */

typedef struct DBDMA_channel {
    int channel;
    uint32_t regs[DBDMA_REGS];
    qemu_irq irq;
    DBDMA_io io;
    DBDMA_rw rw;
    DBDMA_flush flush;
    dbdma_cmd current;
    int processing;
} DBDMA_channel;

typedef struct {
    DBDMA_channel channels[DBDMA_CHANNELS];
} DBDMAState;

#ifdef DEBUG_DBDMA
static void dump_dbdma_cmd(dbdma_cmd *cmd)
{
    printf("dbdma_cmd %p\n", cmd);
    printf("    req_count 0x%04x\n", le16_to_cpu(cmd->req_count));
    printf("    command 0x%04x\n", le16_to_cpu(cmd->command));
    printf("    phy_addr 0x%08x\n", le32_to_cpu(cmd->phy_addr));
    printf("    cmd_dep 0x%08x\n", le32_to_cpu(cmd->cmd_dep));
    printf("    res_count 0x%04x\n", le16_to_cpu(cmd->res_count));
    printf("    xfer_status 0x%04x\n", le16_to_cpu(cmd->xfer_status));
}
#else
static void dump_dbdma_cmd(dbdma_cmd *cmd)
{
}
#endif
static void dbdma_cmdptr_load(DBDMA_channel *ch)
{
    DBDMA_DPRINTF("dbdma_cmdptr_load 0x%08x\n",
                  ch->regs[DBDMA_CMDPTR_LO]);
    cpu_physical_memory_read(ch->regs[DBDMA_CMDPTR_LO],
                             (uint8_t*)&ch->current, sizeof(dbdma_cmd));
}

static void dbdma_cmdptr_save(DBDMA_channel *ch)
{
    DBDMA_DPRINTF("dbdma_cmdptr_save 0x%08x\n",
                  ch->regs[DBDMA_CMDPTR_LO]);
    DBDMA_DPRINTF("xfer_status 0x%08x res_count 0x%04x\n",
                  le16_to_cpu(ch->current.xfer_status),
                  le16_to_cpu(ch->current.res_count));
    cpu_physical_memory_write(ch->regs[DBDMA_CMDPTR_LO],
                              (uint8_t*)&ch->current, sizeof(dbdma_cmd));
}

static void kill_channel(DBDMA_channel *ch)
{
    DBDMA_DPRINTF("kill_channel\n");

    ch->regs[DBDMA_STATUS] |= DEAD;
    ch->regs[DBDMA_STATUS] &= ~ACTIVE;

    qemu_irq_raise(ch->irq);
}

static void conditional_interrupt(DBDMA_channel *ch)
{
    dbdma_cmd *current = &ch->current;
    uint16_t intr;
    uint16_t sel_mask, sel_value;
    uint32_t status;
    int cond;

    DBDMA_DPRINTF("conditional_interrupt\n");

    intr = le16_to_cpu(current->command) & INTR_MASK;

    switch(intr) {
    case INTR_NEVER:  /* don't interrupt */
        return;
    case INTR_ALWAYS: /* always interrupt */
        qemu_irq_raise(ch->irq);
        return;
    }

    status = ch->regs[DBDMA_STATUS] & DEVSTAT;

    sel_mask = (ch->regs[DBDMA_INTR_SEL] >> 16) & 0x0f;
    sel_value = ch->regs[DBDMA_INTR_SEL] & 0x0f;

    cond = (status & sel_mask) == (sel_value & sel_mask);

    switch(intr) {
    case INTR_IFSET:  /* intr if condition bit is 1 */
        if (cond)
            qemu_irq_raise(ch->irq);
        return;
    case INTR_IFCLR:  /* intr if condition bit is 0 */
        if (!cond)
            qemu_irq_raise(ch->irq);
        return;
    }
}

static int conditional_wait(DBDMA_channel *ch)
{
    dbdma_cmd *current = &ch->current;
    uint16_t wait;
    uint16_t sel_mask, sel_value;
    uint32_t status;
    int cond;

    DBDMA_DPRINTF("conditional_wait\n");

    wait = le16_to_cpu(current->command) & WAIT_MASK;

    switch(wait) {
    case WAIT_NEVER:  /* don't wait */
        return 0;
    case WAIT_ALWAYS: /* always wait */
        return 1;
    }

    status = ch->regs[DBDMA_STATUS] & DEVSTAT;

    sel_mask = (ch->regs[DBDMA_WAIT_SEL] >> 16) & 0x0f;
    sel_value = ch->regs[DBDMA_WAIT_SEL] & 0x0f;

    cond = (status & sel_mask) == (sel_value & sel_mask);

    switch(wait) {
    case WAIT_IFSET:  /* wait if condition bit is 1 */
        if (cond)
            return 1;
        return 0;
    case WAIT_IFCLR:  /* wait if condition bit is 0 */
        if (!cond)
            return 1;
        return 0;
    }
    return 0;
}

static void next(DBDMA_channel *ch)
{
    uint32_t cp;

    ch->regs[DBDMA_STATUS] &= ~BT;

    cp = ch->regs[DBDMA_CMDPTR_LO];
    ch->regs[DBDMA_CMDPTR_LO] = cp + sizeof(dbdma_cmd);
    dbdma_cmdptr_load(ch);
}

static void branch(DBDMA_channel *ch)
{
    dbdma_cmd *current = &ch->current;

    ch->regs[DBDMA_CMDPTR_LO] = current->cmd_dep;
    ch->regs[DBDMA_STATUS] |= BT;
    dbdma_cmdptr_load(ch);
}

static void conditional_branch(DBDMA_channel *ch)
{
    dbdma_cmd *current = &ch->current;
    uint16_t br;
    uint16_t sel_mask, sel_value;
    uint32_t status;
    int cond;

    DBDMA_DPRINTF("conditional_branch\n");

    /* check if we must branch */

    br = le16_to_cpu(current->command) & BR_MASK;

    switch(br) {
    case BR_NEVER:  /* don't branch */
        next(ch);
        return;
    case BR_ALWAYS: /* always branch */
        branch(ch);
        return;
    }

    status = ch->regs[DBDMA_STATUS] & DEVSTAT;

    sel_mask = (ch->regs[DBDMA_BRANCH_SEL] >> 16) & 0x0f;
    sel_value = ch->regs[DBDMA_BRANCH_SEL] & 0x0f;

    cond = (status & sel_mask) == (sel_value & sel_mask);

    switch(br) {
    case BR_IFSET:  /* branch if condition bit is 1 */
        if (cond)
            branch(ch);
        else
            next(ch);
        return;
    case BR_IFCLR:  /* branch if condition bit is 0 */
        if (!cond)
            branch(ch);
        else
            next(ch);
        return;
    }
}

static QEMUBH *dbdma_bh;
static void channel_run(DBDMA_channel *ch);

static void dbdma_end(DBDMA_io *io)
{
    DBDMA_channel *ch = io->channel;
    dbdma_cmd *current = &ch->current;

    if (conditional_wait(ch))
        goto wait;

    current->xfer_status = cpu_to_le16(ch->regs[DBDMA_STATUS]);
    current->res_count = cpu_to_le16(io->len);
    dbdma_cmdptr_save(ch);
    if (io->is_last)
        ch->regs[DBDMA_STATUS] &= ~FLUSH;

    conditional_interrupt(ch);
    conditional_branch(ch);

wait:
    ch->processing = 0;
    if ((ch->regs[DBDMA_STATUS] & RUN) &&
        (ch->regs[DBDMA_STATUS] & ACTIVE))
        channel_run(ch);
}

static void start_output(DBDMA_channel *ch, int key, uint32_t addr,
                        uint16_t req_count, int is_last)
{
    DBDMA_DPRINTF("start_output\n");

    /* KEY_REGS, KEY_DEVICE and KEY_STREAM
     * are not implemented in the mac-io chip
     */

    DBDMA_DPRINTF("addr 0x%x key 0x%x\n", addr, key);
    if (!addr || key > KEY_STREAM3) {
        kill_channel(ch);
        return;
    }

    ch->io.addr = addr;
    ch->io.len = req_count;
    ch->io.is_last = is_last;
    ch->io.dma_end = dbdma_end;
    ch->io.is_dma_out = 1;
    ch->processing = 1;
    if (ch->rw) {
        ch->rw(&ch->io);
    }
}

static void start_input(DBDMA_channel *ch, int key, uint32_t addr,
                       uint16_t req_count, int is_last)
{
    DBDMA_DPRINTF("start_input\n");

    /* KEY_REGS, KEY_DEVICE and KEY_STREAM
     * are not implemented in the mac-io chip
     */

    if (!addr || key > KEY_STREAM3) {
        kill_channel(ch);
        return;
    }

    ch->io.addr = addr;
    ch->io.len = req_count;
    ch->io.is_last = is_last;
    ch->io.dma_end = dbdma_end;
    ch->io.is_dma_out = 0;
    ch->processing = 1;
    if (ch->rw) {
        ch->rw(&ch->io);
    }
}

static void load_word(DBDMA_channel *ch, int key, uint32_t addr,
                     uint16_t len)
{
    dbdma_cmd *current = &ch->current;
    uint32_t val;

    DBDMA_DPRINTF("load_word\n");

    /* only implements KEY_SYSTEM */

    if (key != KEY_SYSTEM) {
        printf("DBDMA: LOAD_WORD, unimplemented key %x\n", key);
        kill_channel(ch);
        return;
    }

    cpu_physical_memory_read(addr, (uint8_t*)&val, len);

    if (len == 2)
        val = (val << 16) | (current->cmd_dep & 0x0000ffff);
    else if (len == 1)
        val = (val << 24) | (current->cmd_dep & 0x00ffffff);

    current->cmd_dep = val;

    if (conditional_wait(ch))
        goto wait;

    current->xfer_status = cpu_to_le16(ch->regs[DBDMA_STATUS]);
    dbdma_cmdptr_save(ch);
    ch->regs[DBDMA_STATUS] &= ~FLUSH;

    conditional_interrupt(ch);
    next(ch);

wait:
    qemu_bh_schedule(dbdma_bh);
}

static void store_word(DBDMA_channel *ch, int key, uint32_t addr,
                      uint16_t len)
{
    dbdma_cmd *current = &ch->current;
    uint32_t val;

    DBDMA_DPRINTF("store_word\n");

    /* only implements KEY_SYSTEM */

    if (key != KEY_SYSTEM) {
        printf("DBDMA: STORE_WORD, unimplemented key %x\n", key);
        kill_channel(ch);
        return;
    }

    val = current->cmd_dep;
    if (len == 2)
        val >>= 16;
    else if (len == 1)
        val >>= 24;

    cpu_physical_memory_write(addr, (uint8_t*)&val, len);

    if (conditional_wait(ch))
        goto wait;

    current->xfer_status = cpu_to_le16(ch->regs[DBDMA_STATUS]);
    dbdma_cmdptr_save(ch);
    ch->regs[DBDMA_STATUS] &= ~FLUSH;

    conditional_interrupt(ch);
    next(ch);

wait:
    qemu_bh_schedule(dbdma_bh);
}

static void nop(DBDMA_channel *ch)
{
    dbdma_cmd *current = &ch->current;

    if (conditional_wait(ch))
        goto wait;

    current->xfer_status = cpu_to_le16(ch->regs[DBDMA_STATUS]);
    dbdma_cmdptr_save(ch);

    conditional_interrupt(ch);
    conditional_branch(ch);

wait:
    qemu_bh_schedule(dbdma_bh);
}

static void stop(DBDMA_channel *ch)
{
    ch->regs[DBDMA_STATUS] &= ~(ACTIVE|DEAD|FLUSH);

    /* the stop command does not increment command pointer */
}

static void channel_run(DBDMA_channel *ch)
{
    dbdma_cmd *current = &ch->current;
    uint16_t cmd, key;
    uint16_t req_count;
    uint32_t phy_addr;

    DBDMA_DPRINTF("channel_run\n");
    dump_dbdma_cmd(current);

    /* clear WAKE flag at command fetch */

    ch->regs[DBDMA_STATUS] &= ~WAKE;

    cmd = le16_to_cpu(current->command) & COMMAND_MASK;

    switch (cmd) {
    case DBDMA_NOP:
        nop(ch);
	return;

    case DBDMA_STOP:
        stop(ch);
	return;
    }

    key = le16_to_cpu(current->command) & 0x0700;
    req_count = le16_to_cpu(current->req_count);
    phy_addr = le32_to_cpu(current->phy_addr);

    if (key == KEY_STREAM4) {
        printf("command %x, invalid key 4\n", cmd);
        kill_channel(ch);
        return;
    }

    switch (cmd) {
    case OUTPUT_MORE:
        start_output(ch, key, phy_addr, req_count, 0);
	return;

    case OUTPUT_LAST:
        start_output(ch, key, phy_addr, req_count, 1);
	return;

    case INPUT_MORE:
        start_input(ch, key, phy_addr, req_count, 0);
	return;

    case INPUT_LAST:
        start_input(ch, key, phy_addr, req_count, 1);
	return;
    }

    if (key < KEY_REGS) {
        printf("command %x, invalid key %x\n", cmd, key);
        key = KEY_SYSTEM;
    }

    /* for LOAD_WORD and STORE_WORD, req_count is on 3 bits
     * and BRANCH is invalid
     */

    req_count = req_count & 0x0007;
    if (req_count & 0x4) {
        req_count = 4;
        phy_addr &= ~3;
    } else if (req_count & 0x2) {
        req_count = 2;
        phy_addr &= ~1;
    } else
        req_count = 1;

    switch (cmd) {
    case LOAD_WORD:
        load_word(ch, key, phy_addr, req_count);
	return;

    case STORE_WORD:
        store_word(ch, key, phy_addr, req_count);
	return;
    }
}

static void DBDMA_run(DBDMAState *s)
{
    int channel;

    for (channel = 0; channel < DBDMA_CHANNELS; channel++) {
        DBDMA_channel *ch = &s->channels[channel];
        uint32_t status = ch->regs[DBDMA_STATUS];
        if (!ch->processing && (status & RUN) && (status & ACTIVE)) {
            channel_run(ch);
        }
    }
}

static void DBDMA_run_bh(void *opaque)
{
    DBDMAState *s = opaque;

    DBDMA_DPRINTF("DBDMA_run_bh\n");

    DBDMA_run(s);
}

void DBDMA_register_channel(void *dbdma, int nchan, qemu_irq irq,
                            DBDMA_rw rw, DBDMA_flush flush,
                            void *opaque)
{
    DBDMAState *s = dbdma;
    DBDMA_channel *ch = &s->channels[nchan];

    DBDMA_DPRINTF("DBDMA_register_channel 0x%x\n", nchan);

    ch->irq = irq;
    ch->channel = nchan;
    ch->rw = rw;
    ch->flush = flush;
    ch->io.opaque = opaque;
    ch->io.channel = ch;
}

void DBDMA_schedule(void)
{
    qemu_notify_event();
}

static void
dbdma_control_write(DBDMA_channel *ch)
{
    uint16_t mask, value;
    uint32_t status;

    mask = (ch->regs[DBDMA_CONTROL] >> 16) & 0xffff;
    value = ch->regs[DBDMA_CONTROL] & 0xffff;

    value &= (RUN | PAUSE | FLUSH | WAKE | DEVSTAT);

    status = ch->regs[DBDMA_STATUS];

    status = (value & mask) | (status & ~mask);

    if (status & WAKE)
        status |= ACTIVE;
    if (status & RUN) {
        status |= ACTIVE;
        status &= ~DEAD;
    }
    if (status & PAUSE)
        status &= ~ACTIVE;
    if ((ch->regs[DBDMA_STATUS] & RUN) && !(status & RUN)) {
        /* RUN is cleared */
        status &= ~(ACTIVE|DEAD);
    }

    DBDMA_DPRINTF("    status 0x%08x\n", status);

    ch->regs[DBDMA_STATUS] = status;

    if (status & ACTIVE)
        qemu_bh_schedule(dbdma_bh);
    if ((status & FLUSH) && ch->flush)
        ch->flush(&ch->io);
}

static void dbdma_writel (void *opaque,
                          target_phys_addr_t addr, uint32_t value)
{
    int channel = addr >> DBDMA_CHANNEL_SHIFT;
    DBDMAState *s = opaque;
    DBDMA_channel *ch = &s->channels[channel];
    int reg = (addr - (channel << DBDMA_CHANNEL_SHIFT)) >> 2;

    DBDMA_DPRINTF("writel 0x" TARGET_FMT_plx " <= 0x%08x\n", addr, value);
    DBDMA_DPRINTF("channel 0x%x reg 0x%x\n",
                  (uint32_t)addr >> DBDMA_CHANNEL_SHIFT, reg);

    /* cmdptr cannot be modified if channel is RUN or ACTIVE */

    if (reg == DBDMA_CMDPTR_LO &&
        (ch->regs[DBDMA_STATUS] & (RUN | ACTIVE)))
	return;

    ch->regs[reg] = value;

    switch(reg) {
    case DBDMA_CONTROL:
        dbdma_control_write(ch);
        break;
    case DBDMA_CMDPTR_LO:
        /* 16-byte aligned */
        ch->regs[DBDMA_CMDPTR_LO] &= ~0xf;
        dbdma_cmdptr_load(ch);
        break;
    case DBDMA_STATUS:
    case DBDMA_INTR_SEL:
    case DBDMA_BRANCH_SEL:
    case DBDMA_WAIT_SEL:
        /* nothing to do */
        break;
    case DBDMA_XFER_MODE:
    case DBDMA_CMDPTR_HI:
    case DBDMA_DATA2PTR_HI:
    case DBDMA_DATA2PTR_LO:
    case DBDMA_ADDRESS_HI:
    case DBDMA_BRANCH_ADDR_HI:
    case DBDMA_RES1:
    case DBDMA_RES2:
    case DBDMA_RES3:
    case DBDMA_RES4:
        /* unused */
        break;
    }
}

static uint32_t dbdma_readl (void *opaque, target_phys_addr_t addr)
{
    uint32_t value;
    int channel = addr >> DBDMA_CHANNEL_SHIFT;
    DBDMAState *s = opaque;
    DBDMA_channel *ch = &s->channels[channel];
    int reg = (addr - (channel << DBDMA_CHANNEL_SHIFT)) >> 2;

    value = ch->regs[reg];

    DBDMA_DPRINTF("readl 0x" TARGET_FMT_plx " => 0x%08x\n", addr, value);
    DBDMA_DPRINTF("channel 0x%x reg 0x%x\n",
                  (uint32_t)addr >> DBDMA_CHANNEL_SHIFT, reg);

    switch(reg) {
    case DBDMA_CONTROL:
        value = 0;
        break;
    case DBDMA_STATUS:
    case DBDMA_CMDPTR_LO:
    case DBDMA_INTR_SEL:
    case DBDMA_BRANCH_SEL:
    case DBDMA_WAIT_SEL:
        /* nothing to do */
        break;
    case DBDMA_XFER_MODE:
    case DBDMA_CMDPTR_HI:
    case DBDMA_DATA2PTR_HI:
    case DBDMA_DATA2PTR_LO:
    case DBDMA_ADDRESS_HI:
    case DBDMA_BRANCH_ADDR_HI:
        /* unused */
        value = 0;
        break;
    case DBDMA_RES1:
    case DBDMA_RES2:
    case DBDMA_RES3:
    case DBDMA_RES4:
        /* reserved */
        break;
    }

    return value;
}

static CPUWriteMemoryFunc * const dbdma_write[] = {
    NULL,
    NULL,
    dbdma_writel,
};

static CPUReadMemoryFunc * const dbdma_read[] = {
    NULL,
    NULL,
    dbdma_readl,
};

static void dbdma_save(QEMUFile *f, void *opaque)
{
    DBDMAState *s = opaque;
    unsigned int i, j;

    for (i = 0; i < DBDMA_CHANNELS; i++)
        for (j = 0; j < DBDMA_REGS; j++)
            qemu_put_be32s(f, &s->channels[i].regs[j]);
}

static int dbdma_load(QEMUFile *f, void *opaque, int version_id)
{
    DBDMAState *s = opaque;
    unsigned int i, j;

    if (version_id != 2)
        return -EINVAL;

    for (i = 0; i < DBDMA_CHANNELS; i++)
        for (j = 0; j < DBDMA_REGS; j++)
            qemu_get_be32s(f, &s->channels[i].regs[j]);

    return 0;
}

static void dbdma_reset(void *opaque)
{
    DBDMAState *s = opaque;
    int i;

    for (i = 0; i < DBDMA_CHANNELS; i++)
        memset(s->channels[i].regs, 0, DBDMA_SIZE);
}

void* DBDMA_init (int *dbdma_mem_index)
{
    DBDMAState *s;

    s = qemu_mallocz(sizeof(DBDMAState));

    *dbdma_mem_index = cpu_register_io_memory(dbdma_read, dbdma_write, s,
                                              DEVICE_LITTLE_ENDIAN);
    register_savevm(NULL, "dbdma", -1, 1, dbdma_save, dbdma_load, s);
    qemu_register_reset(dbdma_reset, s);

    dbdma_bh = qemu_bh_new(DBDMA_run_bh, s);

    return s;
}
