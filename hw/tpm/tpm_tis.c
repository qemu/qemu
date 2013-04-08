/*
 * tpm_tis.c - QEMU's TPM TIS interface emulator
 *
 * Copyright (C) 2006,2010-2013 IBM Corporation
 *
 * Authors:
 *  Stefan Berger <stefanb@us.ibm.com>
 *  David Safford <safford@us.ibm.com>
 *
 * Xen 4 support: Andrease Niederl <andreas.niederl@iaik.tugraz.at>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Implementation of the TIS interface according to specs found at
 * http://www.trustedcomputinggroup.org. This implementation currently
 * supports version 1.21, revision 1.0.
 * In the developers menu choose the PC Client section then find the TIS
 * specification.
 */

#include "sysemu/tpm_backend.h"
#include "tpm_int.h"
#include "block/block.h"
#include "exec/address-spaces.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci_ids.h"
#include "tpm_tis.h"
#include "qemu-common.h"

/*#define DEBUG_TIS */

#ifdef DEBUG_TIS
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

/* whether the STS interrupt is supported */
#define RAISE_STS_IRQ

/* tis registers */
#define TPM_TIS_REG_ACCESS                0x00
#define TPM_TIS_REG_INT_ENABLE            0x08
#define TPM_TIS_REG_INT_VECTOR            0x0c
#define TPM_TIS_REG_INT_STATUS            0x10
#define TPM_TIS_REG_INTF_CAPABILITY       0x14
#define TPM_TIS_REG_STS                   0x18
#define TPM_TIS_REG_DATA_FIFO             0x24
#define TPM_TIS_REG_DID_VID               0xf00
#define TPM_TIS_REG_RID                   0xf04

/* vendor-specific registers */
#define TPM_TIS_REG_DEBUG                 0xf90

#define TPM_TIS_STS_VALID                 (1 << 7)
#define TPM_TIS_STS_COMMAND_READY         (1 << 6)
#define TPM_TIS_STS_TPM_GO                (1 << 5)
#define TPM_TIS_STS_DATA_AVAILABLE        (1 << 4)
#define TPM_TIS_STS_EXPECT                (1 << 3)
#define TPM_TIS_STS_RESPONSE_RETRY        (1 << 1)

#define TPM_TIS_BURST_COUNT_SHIFT         8
#define TPM_TIS_BURST_COUNT(X) \
    ((X) << TPM_TIS_BURST_COUNT_SHIFT)

#define TPM_TIS_ACCESS_TPM_REG_VALID_STS  (1 << 7)
#define TPM_TIS_ACCESS_ACTIVE_LOCALITY    (1 << 5)
#define TPM_TIS_ACCESS_BEEN_SEIZED        (1 << 4)
#define TPM_TIS_ACCESS_SEIZE              (1 << 3)
#define TPM_TIS_ACCESS_PENDING_REQUEST    (1 << 2)
#define TPM_TIS_ACCESS_REQUEST_USE        (1 << 1)
#define TPM_TIS_ACCESS_TPM_ESTABLISHMENT  (1 << 0)

#define TPM_TIS_INT_ENABLED               (1 << 31)
#define TPM_TIS_INT_DATA_AVAILABLE        (1 << 0)
#define TPM_TIS_INT_STS_VALID             (1 << 1)
#define TPM_TIS_INT_LOCALITY_CHANGED      (1 << 2)
#define TPM_TIS_INT_COMMAND_READY         (1 << 7)

#define TPM_TIS_INT_POLARITY_MASK         (3 << 3)
#define TPM_TIS_INT_POLARITY_LOW_LEVEL    (1 << 3)

#ifndef RAISE_STS_IRQ

#define TPM_TIS_INTERRUPTS_SUPPORTED (TPM_TIS_INT_LOCALITY_CHANGED | \
                                      TPM_TIS_INT_DATA_AVAILABLE   | \
                                      TPM_TIS_INT_COMMAND_READY)

#else

#define TPM_TIS_INTERRUPTS_SUPPORTED (TPM_TIS_INT_LOCALITY_CHANGED | \
                                      TPM_TIS_INT_DATA_AVAILABLE   | \
                                      TPM_TIS_INT_STS_VALID | \
                                      TPM_TIS_INT_COMMAND_READY)

#endif

#define TPM_TIS_CAP_INTERRUPT_LOW_LEVEL  (1 << 4) /* support is mandatory */
#define TPM_TIS_CAPABILITIES_SUPPORTED   (TPM_TIS_CAP_INTERRUPT_LOW_LEVEL | \
                                          TPM_TIS_INTERRUPTS_SUPPORTED)

#define TPM_TIS_TPM_DID       0x0001
#define TPM_TIS_TPM_VID       PCI_VENDOR_ID_IBM
#define TPM_TIS_TPM_RID       0x0001

#define TPM_TIS_NO_DATA_BYTE  0xff

/* local prototypes */

static uint64_t tpm_tis_mmio_read(void *opaque, hwaddr addr,
                                  unsigned size);

/* utility functions */

static uint8_t tpm_tis_locality_from_addr(hwaddr addr)
{
    return (uint8_t)((addr >> TPM_TIS_LOCALITY_SHIFT) & 0x7);
}

static uint32_t tpm_tis_get_size_from_buffer(const TPMSizedBuffer *sb)
{
    return be32_to_cpu(*(uint32_t *)&sb->buffer[2]);
}

static void tpm_tis_show_buffer(const TPMSizedBuffer *sb, const char *string)
{
#ifdef DEBUG_TIS
    uint32_t len, i;

    len = tpm_tis_get_size_from_buffer(sb);
    DPRINTF("tpm_tis: %s length = %d\n", string, len);
    for (i = 0; i < len; i++) {
        if (i && !(i % 16)) {
            DPRINTF("\n");
        }
        DPRINTF("%.2X ", sb->buffer[i]);
    }
    DPRINTF("\n");
#endif
}

/*
 * Send a request to the TPM.
 */
static void tpm_tis_tpm_send(TPMState *s, uint8_t locty)
{
    TPMTISEmuState *tis = &s->s.tis;

    tpm_tis_show_buffer(&tis->loc[locty].w_buffer, "tpm_tis: To TPM");

    s->locty_number = locty;
    s->locty_data = &tis->loc[locty];

    /*
     * w_offset serves as length indicator for length of data;
     * it's reset when the response comes back
     */
    tis->loc[locty].state = TPM_TIS_STATE_EXECUTION;

    tpm_backend_deliver_request(s->be_driver);
}

/* raise an interrupt if allowed */
static void tpm_tis_raise_irq(TPMState *s, uint8_t locty, uint32_t irqmask)
{
    TPMTISEmuState *tis = &s->s.tis;

    if (!TPM_TIS_IS_VALID_LOCTY(locty)) {
        return;
    }

    if ((tis->loc[locty].inte & TPM_TIS_INT_ENABLED) &&
        (tis->loc[locty].inte & irqmask)) {
        DPRINTF("tpm_tis: Raising IRQ for flag %08x\n", irqmask);
        qemu_irq_raise(s->s.tis.irq);
        tis->loc[locty].ints |= irqmask;
    }
}

static uint32_t tpm_tis_check_request_use_except(TPMState *s, uint8_t locty)
{
    uint8_t l;

    for (l = 0; l < TPM_TIS_NUM_LOCALITIES; l++) {
        if (l == locty) {
            continue;
        }
        if ((s->s.tis.loc[l].access & TPM_TIS_ACCESS_REQUEST_USE)) {
            return 1;
        }
    }

    return 0;
}

static void tpm_tis_new_active_locality(TPMState *s, uint8_t new_active_locty)
{
    TPMTISEmuState *tis = &s->s.tis;
    bool change = (s->s.tis.active_locty != new_active_locty);
    bool is_seize;
    uint8_t mask;

    if (change && TPM_TIS_IS_VALID_LOCTY(s->s.tis.active_locty)) {
        is_seize = TPM_TIS_IS_VALID_LOCTY(new_active_locty) &&
                   tis->loc[new_active_locty].access & TPM_TIS_ACCESS_SEIZE;

        if (is_seize) {
            mask = ~(TPM_TIS_ACCESS_ACTIVE_LOCALITY);
        } else {
            mask = ~(TPM_TIS_ACCESS_ACTIVE_LOCALITY|
                     TPM_TIS_ACCESS_REQUEST_USE);
        }
        /* reset flags on the old active locality */
        tis->loc[s->s.tis.active_locty].access &= mask;

        if (is_seize) {
            tis->loc[tis->active_locty].access |= TPM_TIS_ACCESS_BEEN_SEIZED;
        }
    }

    tis->active_locty = new_active_locty;

    DPRINTF("tpm_tis: Active locality is now %d\n", s->s.tis.active_locty);

    if (TPM_TIS_IS_VALID_LOCTY(new_active_locty)) {
        /* set flags on the new active locality */
        tis->loc[new_active_locty].access |= TPM_TIS_ACCESS_ACTIVE_LOCALITY;
        tis->loc[new_active_locty].access &= ~(TPM_TIS_ACCESS_REQUEST_USE |
                                               TPM_TIS_ACCESS_SEIZE);
    }

    if (change) {
        tpm_tis_raise_irq(s, tis->active_locty, TPM_TIS_INT_LOCALITY_CHANGED);
    }
}

/* abort -- this function switches the locality */
static void tpm_tis_abort(TPMState *s, uint8_t locty)
{
    TPMTISEmuState *tis = &s->s.tis;

    tis->loc[locty].r_offset = 0;
    tis->loc[locty].w_offset = 0;

    DPRINTF("tpm_tis: tis_abort: new active locality is %d\n", tis->next_locty);

    /*
     * Need to react differently depending on who's aborting now and
     * which locality will become active afterwards.
     */
    if (tis->aborting_locty == tis->next_locty) {
        tis->loc[tis->aborting_locty].state = TPM_TIS_STATE_READY;
        tis->loc[tis->aborting_locty].sts = TPM_TIS_STS_COMMAND_READY;
        tpm_tis_raise_irq(s, tis->aborting_locty, TPM_TIS_INT_COMMAND_READY);
    }

    /* locality after abort is another one than the current one */
    tpm_tis_new_active_locality(s, tis->next_locty);

    tis->next_locty = TPM_TIS_NO_LOCALITY;
    /* nobody's aborting a command anymore */
    tis->aborting_locty = TPM_TIS_NO_LOCALITY;
}

/* prepare aborting current command */
static void tpm_tis_prep_abort(TPMState *s, uint8_t locty, uint8_t newlocty)
{
    TPMTISEmuState *tis = &s->s.tis;
    uint8_t busy_locty;

    tis->aborting_locty = locty;
    tis->next_locty = newlocty;  /* locality after successful abort */

    /*
     * only abort a command using an interrupt if currently executing
     * a command AND if there's a valid connection to the vTPM.
     */
    for (busy_locty = 0; busy_locty < TPM_TIS_NUM_LOCALITIES; busy_locty++) {
        if (tis->loc[busy_locty].state == TPM_TIS_STATE_EXECUTION) {
            /*
             * request the backend to cancel. Some backends may not
             * support it
             */
            tpm_backend_cancel_cmd(s->be_driver);
            return;
        }
    }

    tpm_tis_abort(s, locty);
}

static void tpm_tis_receive_bh(void *opaque)
{
    TPMState *s = opaque;
    TPMTISEmuState *tis = &s->s.tis;
    uint8_t locty = s->locty_number;

    tis->loc[locty].sts = TPM_TIS_STS_VALID | TPM_TIS_STS_DATA_AVAILABLE;
    tis->loc[locty].state = TPM_TIS_STATE_COMPLETION;
    tis->loc[locty].r_offset = 0;
    tis->loc[locty].w_offset = 0;

    if (TPM_TIS_IS_VALID_LOCTY(tis->next_locty)) {
        tpm_tis_abort(s, locty);
    }

#ifndef RAISE_STS_IRQ
    tpm_tis_raise_irq(s, locty, TPM_TIS_INT_DATA_AVAILABLE);
#else
    tpm_tis_raise_irq(s, locty,
                      TPM_TIS_INT_DATA_AVAILABLE | TPM_TIS_INT_STS_VALID);
#endif
}

/*
 * Callback from the TPM to indicate that the response was received.
 */
static void tpm_tis_receive_cb(TPMState *s, uint8_t locty)
{
    TPMTISEmuState *tis = &s->s.tis;

    assert(s->locty_number == locty);

    qemu_bh_schedule(tis->bh);
}

/*
 * Read a byte of response data
 */
static uint32_t tpm_tis_data_read(TPMState *s, uint8_t locty)
{
    TPMTISEmuState *tis = &s->s.tis;
    uint32_t ret = TPM_TIS_NO_DATA_BYTE;
    uint16_t len;

    if ((tis->loc[locty].sts & TPM_TIS_STS_DATA_AVAILABLE)) {
        len = tpm_tis_get_size_from_buffer(&tis->loc[locty].r_buffer);

        ret = tis->loc[locty].r_buffer.buffer[tis->loc[locty].r_offset++];
        if (tis->loc[locty].r_offset >= len) {
            /* got last byte */
            tis->loc[locty].sts = TPM_TIS_STS_VALID;
#ifdef RAISE_STS_IRQ
            tpm_tis_raise_irq(s, locty, TPM_TIS_INT_STS_VALID);
#endif
        }
        DPRINTF("tpm_tis: tpm_tis_data_read byte 0x%02x   [%d]\n",
                ret, tis->loc[locty].r_offset-1);
    }

    return ret;
}

#ifdef DEBUG_TIS
static void tpm_tis_dump_state(void *opaque, hwaddr addr)
{
    static const unsigned regs[] = {
        TPM_TIS_REG_ACCESS,
        TPM_TIS_REG_INT_ENABLE,
        TPM_TIS_REG_INT_VECTOR,
        TPM_TIS_REG_INT_STATUS,
        TPM_TIS_REG_INTF_CAPABILITY,
        TPM_TIS_REG_STS,
        TPM_TIS_REG_DID_VID,
        TPM_TIS_REG_RID,
        0xfff};
    int idx;
    uint8_t locty = tpm_tis_locality_from_addr(addr);
    hwaddr base = addr & ~0xfff;
    TPMState *s = opaque;
    TPMTISEmuState *tis = &s->s.tis;

    DPRINTF("tpm_tis: active locality      : %d\n"
            "tpm_tis: state of locality %d : %d\n"
            "tpm_tis: register dump:\n",
            tis->active_locty,
            locty, tis->loc[locty].state);

    for (idx = 0; regs[idx] != 0xfff; idx++) {
        DPRINTF("tpm_tis: 0x%04x : 0x%08x\n", regs[idx],
                (uint32_t)tpm_tis_mmio_read(opaque, base + regs[idx], 4));
    }

    DPRINTF("tpm_tis: read offset   : %d\n"
            "tpm_tis: result buffer : ",
            tis->loc[locty].r_offset);
    for (idx = 0;
         idx < tpm_tis_get_size_from_buffer(&tis->loc[locty].r_buffer);
         idx++) {
        DPRINTF("%c%02x%s",
                tis->loc[locty].r_offset == idx ? '>' : ' ',
                tis->loc[locty].r_buffer.buffer[idx],
                ((idx & 0xf) == 0xf) ? "\ntpm_tis:                 " : "");
    }
    DPRINTF("\n"
            "tpm_tis: write offset  : %d\n"
            "tpm_tis: request buffer: ",
            tis->loc[locty].w_offset);
    for (idx = 0;
         idx < tpm_tis_get_size_from_buffer(&tis->loc[locty].w_buffer);
         idx++) {
        DPRINTF("%c%02x%s",
                tis->loc[locty].w_offset == idx ? '>' : ' ',
                tis->loc[locty].w_buffer.buffer[idx],
                ((idx & 0xf) == 0xf) ? "\ntpm_tis:                 " : "");
    }
    DPRINTF("\n");
}
#endif

/*
 * Read a register of the TIS interface
 * See specs pages 33-63 for description of the registers
 */
static uint64_t tpm_tis_mmio_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    TPMState *s = opaque;
    TPMTISEmuState *tis = &s->s.tis;
    uint16_t offset = addr & 0xffc;
    uint8_t shift = (addr & 0x3) * 8;
    uint32_t val = 0xffffffff;
    uint8_t locty = tpm_tis_locality_from_addr(addr);
    uint32_t avail;

    if (tpm_backend_had_startup_error(s->be_driver)) {
        return val;
    }

    switch (offset) {
    case TPM_TIS_REG_ACCESS:
        /* never show the SEIZE flag even though we use it internally */
        val = tis->loc[locty].access & ~TPM_TIS_ACCESS_SEIZE;
        /* the pending flag is always calculated */
        if (tpm_tis_check_request_use_except(s, locty)) {
            val |= TPM_TIS_ACCESS_PENDING_REQUEST;
        }
        val |= !tpm_backend_get_tpm_established_flag(s->be_driver);
        break;
    case TPM_TIS_REG_INT_ENABLE:
        val = tis->loc[locty].inte;
        break;
    case TPM_TIS_REG_INT_VECTOR:
        val = tis->irq_num;
        break;
    case TPM_TIS_REG_INT_STATUS:
        val = tis->loc[locty].ints;
        break;
    case TPM_TIS_REG_INTF_CAPABILITY:
        val = TPM_TIS_CAPABILITIES_SUPPORTED;
        break;
    case TPM_TIS_REG_STS:
        if (tis->active_locty == locty) {
            if ((tis->loc[locty].sts & TPM_TIS_STS_DATA_AVAILABLE)) {
                val = TPM_TIS_BURST_COUNT(
                       tpm_tis_get_size_from_buffer(&tis->loc[locty].r_buffer)
                       - tis->loc[locty].r_offset) | tis->loc[locty].sts;
            } else {
                avail = tis->loc[locty].w_buffer.size
                        - tis->loc[locty].w_offset;
                /*
                 * byte-sized reads should not return 0x00 for 0x100
                 * available bytes.
                 */
                if (size == 1 && avail > 0xff) {
                    avail = 0xff;
                }
                val = TPM_TIS_BURST_COUNT(avail) | tis->loc[locty].sts;
            }
        }
        break;
    case TPM_TIS_REG_DATA_FIFO:
        if (tis->active_locty == locty) {
            switch (tis->loc[locty].state) {
            case TPM_TIS_STATE_COMPLETION:
                val = tpm_tis_data_read(s, locty);
                break;
            default:
                val = TPM_TIS_NO_DATA_BYTE;
                break;
            }
        }
        break;
    case TPM_TIS_REG_DID_VID:
        val = (TPM_TIS_TPM_DID << 16) | TPM_TIS_TPM_VID;
        break;
    case TPM_TIS_REG_RID:
        val = TPM_TIS_TPM_RID;
        break;
#ifdef DEBUG_TIS
    case TPM_TIS_REG_DEBUG:
        tpm_tis_dump_state(opaque, addr);
        break;
#endif
    }

    if (shift) {
        val >>= shift;
    }

    DPRINTF("tpm_tis:  read.%u(%08x) = %08x\n", size, (int)addr, (uint32_t)val);

    return val;
}

/*
 * Write a value to a register of the TIS interface
 * See specs pages 33-63 for description of the registers
 */
static void tpm_tis_mmio_write_intern(void *opaque, hwaddr addr,
                                      uint64_t val, unsigned size,
                                      bool hw_access)
{
    TPMState *s = opaque;
    TPMTISEmuState *tis = &s->s.tis;
    uint16_t off = addr & 0xfff;
    uint8_t locty = tpm_tis_locality_from_addr(addr);
    uint8_t active_locty, l;
    int c, set_new_locty = 1;
    uint16_t len;

    DPRINTF("tpm_tis: write.%u(%08x) = %08x\n", size, (int)addr, (uint32_t)val);

    if (locty == 4 && !hw_access) {
        DPRINTF("tpm_tis: Access to locality 4 only allowed from hardware\n");
        return;
    }

    if (tpm_backend_had_startup_error(s->be_driver)) {
        return;
    }

    switch (off) {
    case TPM_TIS_REG_ACCESS:

        if ((val & TPM_TIS_ACCESS_SEIZE)) {
            val &= ~(TPM_TIS_ACCESS_REQUEST_USE |
                     TPM_TIS_ACCESS_ACTIVE_LOCALITY);
        }

        active_locty = tis->active_locty;

        if ((val & TPM_TIS_ACCESS_ACTIVE_LOCALITY)) {
            /* give up locality if currently owned */
            if (tis->active_locty == locty) {
                DPRINTF("tpm_tis: Releasing locality %d\n", locty);

                uint8_t newlocty = TPM_TIS_NO_LOCALITY;
                /* anybody wants the locality ? */
                for (c = TPM_TIS_NUM_LOCALITIES - 1; c >= 0; c--) {
                    if ((tis->loc[c].access & TPM_TIS_ACCESS_REQUEST_USE)) {
                        DPRINTF("tpm_tis: Locality %d requests use.\n", c);
                        newlocty = c;
                        break;
                    }
                }
                DPRINTF("tpm_tis: TPM_TIS_ACCESS_ACTIVE_LOCALITY: "
                        "Next active locality: %d\n",
                        newlocty);

                if (TPM_TIS_IS_VALID_LOCTY(newlocty)) {
                    set_new_locty = 0;
                    tpm_tis_prep_abort(s, locty, newlocty);
                } else {
                    active_locty = TPM_TIS_NO_LOCALITY;
                }
            } else {
                /* not currently the owner; clear a pending request */
                tis->loc[locty].access &= ~TPM_TIS_ACCESS_REQUEST_USE;
            }
        }

        if ((val & TPM_TIS_ACCESS_BEEN_SEIZED)) {
            tis->loc[locty].access &= ~TPM_TIS_ACCESS_BEEN_SEIZED;
        }

        if ((val & TPM_TIS_ACCESS_SEIZE)) {
            /*
             * allow seize if a locality is active and the requesting
             * locality is higher than the one that's active
             * OR
             * allow seize for requesting locality if no locality is
             * active
             */
            while ((TPM_TIS_IS_VALID_LOCTY(tis->active_locty) &&
                    locty > tis->active_locty) ||
                    !TPM_TIS_IS_VALID_LOCTY(tis->active_locty)) {
                bool higher_seize = FALSE;

                /* already a pending SEIZE ? */
                if ((tis->loc[locty].access & TPM_TIS_ACCESS_SEIZE)) {
                    break;
                }

                /* check for ongoing seize by a higher locality */
                for (l = locty + 1; l < TPM_TIS_NUM_LOCALITIES; l++) {
                    if ((tis->loc[l].access & TPM_TIS_ACCESS_SEIZE)) {
                        higher_seize = TRUE;
                        break;
                    }
                }

                if (higher_seize) {
                    break;
                }

                /* cancel any seize by a lower locality */
                for (l = 0; l < locty - 1; l++) {
                    tis->loc[l].access &= ~TPM_TIS_ACCESS_SEIZE;
                }

                tis->loc[locty].access |= TPM_TIS_ACCESS_SEIZE;
                DPRINTF("tpm_tis: TPM_TIS_ACCESS_SEIZE: "
                        "Locality %d seized from locality %d\n",
                        locty, tis->active_locty);
                DPRINTF("tpm_tis: TPM_TIS_ACCESS_SEIZE: Initiating abort.\n");
                set_new_locty = 0;
                tpm_tis_prep_abort(s, tis->active_locty, locty);
                break;
            }
        }

        if ((val & TPM_TIS_ACCESS_REQUEST_USE)) {
            if (tis->active_locty != locty) {
                if (TPM_TIS_IS_VALID_LOCTY(tis->active_locty)) {
                    tis->loc[locty].access |= TPM_TIS_ACCESS_REQUEST_USE;
                } else {
                    /* no locality active -> make this one active now */
                    active_locty = locty;
                }
            }
        }

        if (set_new_locty) {
            tpm_tis_new_active_locality(s, active_locty);
        }

        break;
    case TPM_TIS_REG_INT_ENABLE:
        if (tis->active_locty != locty) {
            break;
        }

        tis->loc[locty].inte = (val & (TPM_TIS_INT_ENABLED |
                                       TPM_TIS_INT_POLARITY_MASK |
                                       TPM_TIS_INTERRUPTS_SUPPORTED));
        break;
    case TPM_TIS_REG_INT_VECTOR:
        /* hard wired -- ignore */
        break;
    case TPM_TIS_REG_INT_STATUS:
        if (tis->active_locty != locty) {
            break;
        }

        /* clearing of interrupt flags */
        if (((val & TPM_TIS_INTERRUPTS_SUPPORTED)) &&
            (tis->loc[locty].ints & TPM_TIS_INTERRUPTS_SUPPORTED)) {
            tis->loc[locty].ints &= ~val;
            if (tis->loc[locty].ints == 0) {
                qemu_irq_lower(tis->irq);
                DPRINTF("tpm_tis: Lowering IRQ\n");
            }
        }
        tis->loc[locty].ints &= ~(val & TPM_TIS_INTERRUPTS_SUPPORTED);
        break;
    case TPM_TIS_REG_STS:
        if (tis->active_locty != locty) {
            break;
        }

        val &= (TPM_TIS_STS_COMMAND_READY | TPM_TIS_STS_TPM_GO |
                TPM_TIS_STS_RESPONSE_RETRY);

        if (val == TPM_TIS_STS_COMMAND_READY) {
            switch (tis->loc[locty].state) {

            case TPM_TIS_STATE_READY:
                tis->loc[locty].w_offset = 0;
                tis->loc[locty].r_offset = 0;
            break;

            case TPM_TIS_STATE_IDLE:
                tis->loc[locty].sts = TPM_TIS_STS_COMMAND_READY;
                tis->loc[locty].state = TPM_TIS_STATE_READY;
                tpm_tis_raise_irq(s, locty, TPM_TIS_INT_COMMAND_READY);
            break;

            case TPM_TIS_STATE_EXECUTION:
            case TPM_TIS_STATE_RECEPTION:
                /* abort currently running command */
                DPRINTF("tpm_tis: %s: Initiating abort.\n",
                        __func__);
                tpm_tis_prep_abort(s, locty, locty);
            break;

            case TPM_TIS_STATE_COMPLETION:
                tis->loc[locty].w_offset = 0;
                tis->loc[locty].r_offset = 0;
                /* shortcut to ready state with C/R set */
                tis->loc[locty].state = TPM_TIS_STATE_READY;
                if (!(tis->loc[locty].sts & TPM_TIS_STS_COMMAND_READY)) {
                    tis->loc[locty].sts   = TPM_TIS_STS_COMMAND_READY;
                    tpm_tis_raise_irq(s, locty, TPM_TIS_INT_COMMAND_READY);
                }
                tis->loc[locty].sts &= ~(TPM_TIS_STS_DATA_AVAILABLE);
            break;

            }
        } else if (val == TPM_TIS_STS_TPM_GO) {
            switch (tis->loc[locty].state) {
            case TPM_TIS_STATE_RECEPTION:
                if ((tis->loc[locty].sts & TPM_TIS_STS_EXPECT) == 0) {
                    tpm_tis_tpm_send(s, locty);
                }
                break;
            default:
                /* ignore */
                break;
            }
        } else if (val == TPM_TIS_STS_RESPONSE_RETRY) {
            switch (tis->loc[locty].state) {
            case TPM_TIS_STATE_COMPLETION:
                tis->loc[locty].r_offset = 0;
                tis->loc[locty].sts = TPM_TIS_STS_VALID |
                                      TPM_TIS_STS_DATA_AVAILABLE;
                break;
            default:
                /* ignore */
                break;
            }
        }
        break;
    case TPM_TIS_REG_DATA_FIFO:
        /* data fifo */
        if (tis->active_locty != locty) {
            break;
        }

        if (tis->loc[locty].state == TPM_TIS_STATE_IDLE ||
            tis->loc[locty].state == TPM_TIS_STATE_EXECUTION ||
            tis->loc[locty].state == TPM_TIS_STATE_COMPLETION) {
            /* drop the byte */
        } else {
            DPRINTF("tpm_tis: Byte to send to TPM: %02x\n", (uint8_t)val);
            if (tis->loc[locty].state == TPM_TIS_STATE_READY) {
                tis->loc[locty].state = TPM_TIS_STATE_RECEPTION;
                tis->loc[locty].sts = TPM_TIS_STS_EXPECT | TPM_TIS_STS_VALID;
            }

            if ((tis->loc[locty].sts & TPM_TIS_STS_EXPECT)) {
                if (tis->loc[locty].w_offset < tis->loc[locty].w_buffer.size) {
                    tis->loc[locty].w_buffer.
                        buffer[tis->loc[locty].w_offset++] = (uint8_t)val;
                } else {
                    tis->loc[locty].sts = TPM_TIS_STS_VALID;
                }
            }

            /* check for complete packet */
            if (tis->loc[locty].w_offset > 5 &&
                (tis->loc[locty].sts & TPM_TIS_STS_EXPECT)) {
                /* we have a packet length - see if we have all of it */
#ifdef RAISE_STS_IRQ
                bool needIrq = !(tis->loc[locty].sts & TPM_TIS_STS_VALID);
#endif
                len = tpm_tis_get_size_from_buffer(&tis->loc[locty].w_buffer);
                if (len > tis->loc[locty].w_offset) {
                    tis->loc[locty].sts = TPM_TIS_STS_EXPECT |
                                          TPM_TIS_STS_VALID;
                } else {
                    /* packet complete */
                    tis->loc[locty].sts = TPM_TIS_STS_VALID;
                }
#ifdef RAISE_STS_IRQ
                if (needIrq) {
                    tpm_tis_raise_irq(s, locty, TPM_TIS_INT_STS_VALID);
                }
#endif
            }
        }
        break;
    }
}

static void tpm_tis_mmio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    return tpm_tis_mmio_write_intern(opaque, addr, val, size, false);
}

static const MemoryRegionOps tpm_tis_memory_ops = {
    .read = tpm_tis_mmio_read,
    .write = tpm_tis_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static int tpm_tis_do_startup_tpm(TPMState *s)
{
    return tpm_backend_startup_tpm(s->be_driver);
}

/*
 * This function is called when the machine starts, resets or due to
 * S3 resume.
 */
static void tpm_tis_reset(DeviceState *dev)
{
    TPMState *s = TPM(dev);
    TPMTISEmuState *tis = &s->s.tis;
    int c;

    tpm_backend_reset(s->be_driver);

    tis->active_locty = TPM_TIS_NO_LOCALITY;
    tis->next_locty = TPM_TIS_NO_LOCALITY;
    tis->aborting_locty = TPM_TIS_NO_LOCALITY;

    for (c = 0; c < TPM_TIS_NUM_LOCALITIES; c++) {
        tis->loc[c].access = TPM_TIS_ACCESS_TPM_REG_VALID_STS;
        tis->loc[c].sts = 0;
        tis->loc[c].inte = TPM_TIS_INT_POLARITY_LOW_LEVEL;
        tis->loc[c].ints = 0;
        tis->loc[c].state = TPM_TIS_STATE_IDLE;

        tis->loc[c].w_offset = 0;
        tpm_backend_realloc_buffer(s->be_driver, &tis->loc[c].w_buffer);
        tis->loc[c].r_offset = 0;
        tpm_backend_realloc_buffer(s->be_driver, &tis->loc[c].r_buffer);
    }

    tpm_tis_do_startup_tpm(s);
}

static const VMStateDescription vmstate_tpm_tis = {
    .name = "tpm",
    .unmigratable = 1,
};

static Property tpm_tis_properties[] = {
    DEFINE_PROP_UINT32("irq", TPMState,
                       s.tis.irq_num, TPM_TIS_IRQ),
    DEFINE_PROP_STRING("tpmdev", TPMState, backend),
    DEFINE_PROP_END_OF_LIST(),
};

static void tpm_tis_realizefn(DeviceState *dev, Error **errp)
{
    TPMState *s = TPM(dev);
    TPMTISEmuState *tis = &s->s.tis;

    s->be_driver = qemu_find_tpm(s->backend);
    if (!s->be_driver) {
        error_setg(errp, "tpm_tis: backend driver with id %s could not be "
                   "found", s->backend);
        return;
    }

    s->be_driver->fe_model = TPM_MODEL_TPM_TIS;

    if (tpm_backend_init(s->be_driver, s, tpm_tis_receive_cb)) {
        error_setg(errp, "tpm_tis: backend driver with id %s could not be "
                   "initialized", s->backend);
        return;
    }

    if (tis->irq_num > 15) {
        error_setg(errp, "tpm_tis: IRQ %d for TPM TIS is outside valid range "
                   "of 0 to 15.\n", tis->irq_num);
        return;
    }

    tis->bh = qemu_bh_new(tpm_tis_receive_bh, s);

    isa_init_irq(&s->busdev, &tis->irq, tis->irq_num);
}

static void tpm_tis_initfn(Object *obj)
{
    ISADevice *dev = ISA_DEVICE(obj);
    TPMState *s = TPM(obj);

    memory_region_init_io(&s->mmio, &tpm_tis_memory_ops, s, "tpm-tis-mmio",
                          TPM_TIS_NUM_LOCALITIES << TPM_TIS_LOCALITY_SHIFT);
    memory_region_add_subregion(isa_address_space(dev), TPM_TIS_ADDR_BASE,
                                &s->mmio);
}

static void tpm_tis_uninitfn(Object *obj)
{
    TPMState *s = TPM(obj);

    memory_region_del_subregion(get_system_memory(), &s->mmio);
    memory_region_destroy(&s->mmio);
}

static void tpm_tis_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = tpm_tis_realizefn;
    dc->props = tpm_tis_properties;
    dc->reset = tpm_tis_reset;
    dc->vmsd  = &vmstate_tpm_tis;
}

static const TypeInfo tpm_tis_info = {
    .name = TYPE_TPM_TIS,
    .parent = TYPE_ISA_DEVICE,
    .instance_size = sizeof(TPMState),
    .instance_init = tpm_tis_initfn,
    .instance_finalize = tpm_tis_uninitfn,
    .class_init  = tpm_tis_class_init,
};

static void tpm_tis_register(void)
{
    type_register_static(&tpm_tis_info);
    tpm_register_model(TPM_MODEL_TPM_TIS);
}

type_init(tpm_tis_register)
