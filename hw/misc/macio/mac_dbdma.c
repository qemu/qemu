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
#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/isa/isa.h"
#include "hw/ppc/mac_dbdma.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "sysemu/dma.h"

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

static DBDMAState *dbdma_from_ch(DBDMA_channel *ch)
{
    return container_of(ch, DBDMAState, channels[ch->channel]);
}

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
    dma_memory_read(&address_space_memory, ch->regs[DBDMA_CMDPTR_LO],
                    &ch->current, sizeof(dbdma_cmd));
}

static void dbdma_cmdptr_save(DBDMA_channel *ch)
{
    DBDMA_DPRINTF("dbdma_cmdptr_save 0x%08x\n",
                  ch->regs[DBDMA_CMDPTR_LO]);
    DBDMA_DPRINTF("xfer_status 0x%08x res_count 0x%04x\n",
                  le16_to_cpu(ch->current.xfer_status),
                  le16_to_cpu(ch->current.res_count));
    dma_memory_write(&address_space_memory, ch->regs[DBDMA_CMDPTR_LO],
                     &ch->current, sizeof(dbdma_cmd));
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

    DBDMA_DPRINTF("%s\n", __func__);

    intr = le16_to_cpu(current->command) & INTR_MASK;

    switch(intr) {
    case INTR_NEVER:  /* don't interrupt */
        return;
    case INTR_ALWAYS: /* always interrupt */
        qemu_irq_raise(ch->irq);
        DBDMA_DPRINTF("%s: raise\n", __func__);
        return;
    }

    status = ch->regs[DBDMA_STATUS] & DEVSTAT;

    sel_mask = (ch->regs[DBDMA_INTR_SEL] >> 16) & 0x0f;
    sel_value = ch->regs[DBDMA_INTR_SEL] & 0x0f;

    cond = (status & sel_mask) == (sel_value & sel_mask);

    switch(intr) {
    case INTR_IFSET:  /* intr if condition bit is 1 */
        if (cond) {
            qemu_irq_raise(ch->irq);
            DBDMA_DPRINTF("%s: raise\n", __func__);
        }
        return;
    case INTR_IFCLR:  /* intr if condition bit is 0 */
        if (!cond) {
            qemu_irq_raise(ch->irq);
            DBDMA_DPRINTF("%s: raise\n", __func__);
        }
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

static void channel_run(DBDMA_channel *ch);

static void dbdma_end(DBDMA_io *io)
{
    DBDMA_channel *ch = io->channel;
    dbdma_cmd *current = &ch->current;

    DBDMA_DPRINTF("%s\n", __func__);

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
    /* Indicate that we're ready for a new DMA round */
    ch->io.processing = false;

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
    ch->io.processing = true;
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

    DBDMA_DPRINTF("addr 0x%x key 0x%x\n", addr, key);
    if (!addr || key > KEY_STREAM3) {
        kill_channel(ch);
        return;
    }

    ch->io.addr = addr;
    ch->io.len = req_count;
    ch->io.is_last = is_last;
    ch->io.dma_end = dbdma_end;
    ch->io.is_dma_out = 0;
    ch->io.processing = true;
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

    dma_memory_read(&address_space_memory, addr, &val, len);

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
    DBDMA_kick(dbdma_from_ch(ch));
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

    dma_memory_write(&address_space_memory, addr, &val, len);

    if (conditional_wait(ch))
        goto wait;

    current->xfer_status = cpu_to_le16(ch->regs[DBDMA_STATUS]);
    dbdma_cmdptr_save(ch);
    ch->regs[DBDMA_STATUS] &= ~FLUSH;

    conditional_interrupt(ch);
    next(ch);

wait:
    DBDMA_kick(dbdma_from_ch(ch));
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
    DBDMA_kick(dbdma_from_ch(ch));
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
        if (!ch->io.processing && (status & RUN) && (status & ACTIVE)) {
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

void DBDMA_kick(DBDMAState *dbdma)
{
    qemu_bh_schedule(dbdma->bh);
}

void DBDMA_register_channel(void *dbdma, int nchan, qemu_irq irq,
                            DBDMA_rw rw, DBDMA_flush flush,
                            void *opaque)
{
    DBDMAState *s = dbdma;
    DBDMA_channel *ch = &s->channels[nchan];

    DBDMA_DPRINTF("DBDMA_register_channel 0x%x\n", nchan);

    assert(rw);
    assert(flush);

    ch->irq = irq;
    ch->rw = rw;
    ch->flush = flush;
    ch->io.opaque = opaque;
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

    if ((status & FLUSH) && ch->flush) {
        ch->flush(&ch->io);
        status &= ~FLUSH;
    }

    DBDMA_DPRINTF("    status 0x%08x\n", status);

    ch->regs[DBDMA_STATUS] = status;

    if (status & ACTIVE) {
        DBDMA_kick(dbdma_from_ch(ch));
    }
}

static void dbdma_write(void *opaque, hwaddr addr,
                        uint64_t value, unsigned size)
{
    int channel = addr >> DBDMA_CHANNEL_SHIFT;
    DBDMAState *s = opaque;
    DBDMA_channel *ch = &s->channels[channel];
    int reg = (addr - (channel << DBDMA_CHANNEL_SHIFT)) >> 2;

    DBDMA_DPRINTF("writel 0x" TARGET_FMT_plx " <= 0x%08"PRIx64"\n",
                  addr, value);
    DBDMA_DPRINTF("channel 0x%x reg 0x%x\n",
                  (uint32_t)addr >> DBDMA_CHANNEL_SHIFT, reg);

    /* cmdptr cannot be modified if channel is ACTIVE */

    if (reg == DBDMA_CMDPTR_LO && (ch->regs[DBDMA_STATUS] & ACTIVE)) {
        return;
    }

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

static uint64_t dbdma_read(void *opaque, hwaddr addr,
                           unsigned size)
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

static const MemoryRegionOps dbdma_ops = {
    .read = dbdma_read,
    .write = dbdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const VMStateDescription vmstate_dbdma_io = {
    .name = "dbdma_io",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(addr, struct DBDMA_io),
        VMSTATE_INT32(len, struct DBDMA_io),
        VMSTATE_INT32(is_last, struct DBDMA_io),
        VMSTATE_INT32(is_dma_out, struct DBDMA_io),
        VMSTATE_BOOL(processing, struct DBDMA_io),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_dbdma_cmd = {
    .name = "dbdma_cmd",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(req_count, dbdma_cmd),
        VMSTATE_UINT16(command, dbdma_cmd),
        VMSTATE_UINT32(phy_addr, dbdma_cmd),
        VMSTATE_UINT32(cmd_dep, dbdma_cmd),
        VMSTATE_UINT16(res_count, dbdma_cmd),
        VMSTATE_UINT16(xfer_status, dbdma_cmd),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_dbdma_channel = {
    .name = "dbdma_channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, struct DBDMA_channel, DBDMA_REGS),
        VMSTATE_STRUCT(io, struct DBDMA_channel, 0, vmstate_dbdma_io, DBDMA_io),
        VMSTATE_STRUCT(current, struct DBDMA_channel, 0, vmstate_dbdma_cmd,
                       dbdma_cmd),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_dbdma = {
    .name = "dbdma",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(channels, DBDMAState, DBDMA_CHANNELS, 1,
                             vmstate_dbdma_channel, DBDMA_channel),
        VMSTATE_END_OF_LIST()
    }
};

static void dbdma_reset(void *opaque)
{
    DBDMAState *s = opaque;
    int i;

    for (i = 0; i < DBDMA_CHANNELS; i++)
        memset(s->channels[i].regs, 0, DBDMA_SIZE);
}

static void dbdma_unassigned_rw(DBDMA_io *io)
{
    DBDMA_channel *ch = io->channel;
    qemu_log_mask(LOG_GUEST_ERROR, "%s: use of unassigned channel %d\n",
                  __func__, ch->channel);
}

static void dbdma_unassigned_flush(DBDMA_io *io)
{
    DBDMA_channel *ch = io->channel;
    qemu_log_mask(LOG_GUEST_ERROR, "%s: use of unassigned channel %d\n",
                  __func__, ch->channel);
}

void* DBDMA_init (MemoryRegion **dbdma_mem)
{
    DBDMAState *s;
    int i;

    s = g_malloc0(sizeof(DBDMAState));

    for (i = 0; i < DBDMA_CHANNELS; i++) {
        DBDMA_io *io = &s->channels[i].io;
        DBDMA_channel *ch = &s->channels[i];
        qemu_iovec_init(&io->iov, 1);

        ch->rw = dbdma_unassigned_rw;
        ch->flush = dbdma_unassigned_flush;
        ch->channel = i;
        ch->io.channel = ch;
    }

    memory_region_init_io(&s->mem, NULL, &dbdma_ops, s, "dbdma", 0x1000);
    *dbdma_mem = &s->mem;
    vmstate_register(NULL, -1, &vmstate_dbdma, s);
    qemu_register_reset(dbdma_reset, s);

    s->bh = qemu_bh_new(DBDMA_run_bh, s);

    return s;
}
