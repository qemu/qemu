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
 * supports version 1.3, 21 March 2013
 * In the developers menu choose the PC Client section then find the TIS
 * specification.
 *
 * TPM TIS for TPM 2 implementation following TCG PC Client Platform
 * TPM Profile (PTP) Specification, Familiy 2.0, Revision 00.43
 */

#include "qemu/osdep.h"
#include "hw/isa/isa.h"
#include "qapi/error.h"

#include "hw/acpi/tpm.h"
#include "hw/pci/pci_ids.h"
#include "sysemu/tpm_backend.h"
#include "tpm_int.h"
#include "tpm_util.h"
#include "trace.h"

#define TPM_TIS_NUM_LOCALITIES      5     /* per spec */
#define TPM_TIS_LOCALITY_SHIFT      12
#define TPM_TIS_NO_LOCALITY         0xff

#define TPM_TIS_IS_VALID_LOCTY(x)   ((x) < TPM_TIS_NUM_LOCALITIES)

#define TPM_TIS_BUFFER_MAX          4096

typedef enum {
    TPM_TIS_STATE_IDLE = 0,
    TPM_TIS_STATE_READY,
    TPM_TIS_STATE_COMPLETION,
    TPM_TIS_STATE_EXECUTION,
    TPM_TIS_STATE_RECEPTION,
} TPMTISState;

/* locality data  -- all fields are persisted */
typedef struct TPMLocality {
    TPMTISState state;
    uint8_t access;
    uint32_t sts;
    uint32_t iface_id;
    uint32_t inte;
    uint32_t ints;
} TPMLocality;

typedef struct TPMState {
    ISADevice busdev;
    MemoryRegion mmio;

    unsigned char buffer[TPM_TIS_BUFFER_MAX];
    uint16_t rw_offset;

    uint8_t active_locty;
    uint8_t aborting_locty;
    uint8_t next_locty;

    TPMLocality loc[TPM_TIS_NUM_LOCALITIES];

    qemu_irq irq;
    uint32_t irq_num;

    TPMBackendCmd cmd;

    TPMBackend *be_driver;
    TPMVersion be_tpm_version;

    size_t be_buffer_size;
} TPMState;

#define TPM(obj) OBJECT_CHECK(TPMState, (obj), TYPE_TPM_TIS)

#define DEBUG_TIS 0

/* local prototypes */

static uint64_t tpm_tis_mmio_read(void *opaque, hwaddr addr,
                                  unsigned size);

/* utility functions */

static uint8_t tpm_tis_locality_from_addr(hwaddr addr)
{
    return (uint8_t)((addr >> TPM_TIS_LOCALITY_SHIFT) & 0x7);
}

static void tpm_tis_show_buffer(const unsigned char *buffer,
                                size_t buffer_size, const char *string)
{
    uint32_t len, i;

    len = MIN(tpm_cmd_get_size(buffer), buffer_size);
    printf("tpm_tis: %s length = %d\n", string, len);
    for (i = 0; i < len; i++) {
        if (i && !(i % 16)) {
            printf("\n");
        }
        printf("%.2X ", buffer[i]);
    }
    printf("\n");
}

/*
 * Set the given flags in the STS register by clearing the register but
 * preserving the SELFTEST_DONE and TPM_FAMILY_MASK flags and then setting
 * the new flags.
 *
 * The SELFTEST_DONE flag is acquired from the backend that determines it by
 * peeking into TPM commands.
 *
 * A VM suspend/resume will preserve the flag by storing it into the VM
 * device state, but the backend will not remember it when QEMU is started
 * again. Therefore, we cache the flag here. Once set, it will not be unset
 * except by a reset.
 */
static void tpm_tis_sts_set(TPMLocality *l, uint32_t flags)
{
    l->sts &= TPM_TIS_STS_SELFTEST_DONE | TPM_TIS_STS_TPM_FAMILY_MASK;
    l->sts |= flags;
}

/*
 * Send a request to the TPM.
 */
static void tpm_tis_tpm_send(TPMState *s, uint8_t locty)
{
    if (DEBUG_TIS) {
        tpm_tis_show_buffer(s->buffer, s->be_buffer_size,
                            "tpm_tis: To TPM");
    }

    /*
     * rw_offset serves as length indicator for length of data;
     * it's reset when the response comes back
     */
    s->loc[locty].state = TPM_TIS_STATE_EXECUTION;

    s->cmd = (TPMBackendCmd) {
        .locty = locty,
        .in = s->buffer,
        .in_len = s->rw_offset,
        .out = s->buffer,
        .out_len = s->be_buffer_size,
    };

    tpm_backend_deliver_request(s->be_driver, &s->cmd);
}

/* raise an interrupt if allowed */
static void tpm_tis_raise_irq(TPMState *s, uint8_t locty, uint32_t irqmask)
{
    if (!TPM_TIS_IS_VALID_LOCTY(locty)) {
        return;
    }

    if ((s->loc[locty].inte & TPM_TIS_INT_ENABLED) &&
        (s->loc[locty].inte & irqmask)) {
        trace_tpm_tis_raise_irq(irqmask);
        qemu_irq_raise(s->irq);
        s->loc[locty].ints |= irqmask;
    }
}

static uint32_t tpm_tis_check_request_use_except(TPMState *s, uint8_t locty)
{
    uint8_t l;

    for (l = 0; l < TPM_TIS_NUM_LOCALITIES; l++) {
        if (l == locty) {
            continue;
        }
        if ((s->loc[l].access & TPM_TIS_ACCESS_REQUEST_USE)) {
            return 1;
        }
    }

    return 0;
}

static void tpm_tis_new_active_locality(TPMState *s, uint8_t new_active_locty)
{
    bool change = (s->active_locty != new_active_locty);
    bool is_seize;
    uint8_t mask;

    if (change && TPM_TIS_IS_VALID_LOCTY(s->active_locty)) {
        is_seize = TPM_TIS_IS_VALID_LOCTY(new_active_locty) &&
                   s->loc[new_active_locty].access & TPM_TIS_ACCESS_SEIZE;

        if (is_seize) {
            mask = ~(TPM_TIS_ACCESS_ACTIVE_LOCALITY);
        } else {
            mask = ~(TPM_TIS_ACCESS_ACTIVE_LOCALITY|
                     TPM_TIS_ACCESS_REQUEST_USE);
        }
        /* reset flags on the old active locality */
        s->loc[s->active_locty].access &= mask;

        if (is_seize) {
            s->loc[s->active_locty].access |= TPM_TIS_ACCESS_BEEN_SEIZED;
        }
    }

    s->active_locty = new_active_locty;

    trace_tpm_tis_new_active_locality(s->active_locty);

    if (TPM_TIS_IS_VALID_LOCTY(new_active_locty)) {
        /* set flags on the new active locality */
        s->loc[new_active_locty].access |= TPM_TIS_ACCESS_ACTIVE_LOCALITY;
        s->loc[new_active_locty].access &= ~(TPM_TIS_ACCESS_REQUEST_USE |
                                               TPM_TIS_ACCESS_SEIZE);
    }

    if (change) {
        tpm_tis_raise_irq(s, s->active_locty, TPM_TIS_INT_LOCALITY_CHANGED);
    }
}

/* abort -- this function switches the locality */
static void tpm_tis_abort(TPMState *s, uint8_t locty)
{
    s->rw_offset = 0;

    trace_tpm_tis_abort(s->next_locty);

    /*
     * Need to react differently depending on who's aborting now and
     * which locality will become active afterwards.
     */
    if (s->aborting_locty == s->next_locty) {
        s->loc[s->aborting_locty].state = TPM_TIS_STATE_READY;
        tpm_tis_sts_set(&s->loc[s->aborting_locty],
                        TPM_TIS_STS_COMMAND_READY);
        tpm_tis_raise_irq(s, s->aborting_locty, TPM_TIS_INT_COMMAND_READY);
    }

    /* locality after abort is another one than the current one */
    tpm_tis_new_active_locality(s, s->next_locty);

    s->next_locty = TPM_TIS_NO_LOCALITY;
    /* nobody's aborting a command anymore */
    s->aborting_locty = TPM_TIS_NO_LOCALITY;
}

/* prepare aborting current command */
static void tpm_tis_prep_abort(TPMState *s, uint8_t locty, uint8_t newlocty)
{
    uint8_t busy_locty;

    s->aborting_locty = locty;
    s->next_locty = newlocty;  /* locality after successful abort */

    /*
     * only abort a command using an interrupt if currently executing
     * a command AND if there's a valid connection to the vTPM.
     */
    for (busy_locty = 0; busy_locty < TPM_TIS_NUM_LOCALITIES; busy_locty++) {
        if (s->loc[busy_locty].state == TPM_TIS_STATE_EXECUTION) {
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

/*
 * Callback from the TPM to indicate that the response was received.
 */
static void tpm_tis_request_completed(TPMIf *ti, int ret)
{
    TPMState *s = TPM(ti);
    uint8_t locty = s->cmd.locty;
    uint8_t l;

    if (s->cmd.selftest_done) {
        for (l = 0; l < TPM_TIS_NUM_LOCALITIES; l++) {
            s->loc[l].sts |= TPM_TIS_STS_SELFTEST_DONE;
        }
    }

    /* FIXME: report error if ret != 0 */
    tpm_tis_sts_set(&s->loc[locty],
                    TPM_TIS_STS_VALID | TPM_TIS_STS_DATA_AVAILABLE);
    s->loc[locty].state = TPM_TIS_STATE_COMPLETION;
    s->rw_offset = 0;

    if (DEBUG_TIS) {
        tpm_tis_show_buffer(s->buffer, s->be_buffer_size,
                            "tpm_tis: From TPM");
    }

    if (TPM_TIS_IS_VALID_LOCTY(s->next_locty)) {
        tpm_tis_abort(s, locty);
    }

    tpm_tis_raise_irq(s, locty,
                      TPM_TIS_INT_DATA_AVAILABLE | TPM_TIS_INT_STS_VALID);
}

/*
 * Read a byte of response data
 */
static uint32_t tpm_tis_data_read(TPMState *s, uint8_t locty)
{
    uint32_t ret = TPM_TIS_NO_DATA_BYTE;
    uint16_t len;

    if ((s->loc[locty].sts & TPM_TIS_STS_DATA_AVAILABLE)) {
        len = MIN(tpm_cmd_get_size(&s->buffer),
                  s->be_buffer_size);

        ret = s->buffer[s->rw_offset++];
        if (s->rw_offset >= len) {
            /* got last byte */
            tpm_tis_sts_set(&s->loc[locty], TPM_TIS_STS_VALID);
            tpm_tis_raise_irq(s, locty, TPM_TIS_INT_STS_VALID);
        }
        trace_tpm_tis_data_read(ret, s->rw_offset - 1);
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

    printf("tpm_tis: active locality      : %d\n"
           "tpm_tis: state of locality %d : %d\n"
           "tpm_tis: register dump:\n",
           s->active_locty,
           locty, s->loc[locty].state);

    for (idx = 0; regs[idx] != 0xfff; idx++) {
        printf("tpm_tis: 0x%04x : 0x%08x\n", regs[idx],
               (int)tpm_tis_mmio_read(opaque, base + regs[idx], 4));
    }

    printf("tpm_tis: r/w offset    : %d\n"
           "tpm_tis: result buffer : ",
           s->rw_offset);
    for (idx = 0;
         idx < MIN(tpm_cmd_get_size(&s->buffer), s->be_buffer_size);
         idx++) {
        printf("%c%02x%s",
               s->rw_offset == idx ? '>' : ' ',
               s->buffer[idx],
               ((idx & 0xf) == 0xf) ? "\ntpm_tis:                 " : "");
    }
    printf("\n");
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
    uint16_t offset = addr & 0xffc;
    uint8_t shift = (addr & 0x3) * 8;
    uint32_t val = 0xffffffff;
    uint8_t locty = tpm_tis_locality_from_addr(addr);
    uint32_t avail;
    uint8_t v;

    if (tpm_backend_had_startup_error(s->be_driver)) {
        return 0;
    }

    switch (offset) {
    case TPM_TIS_REG_ACCESS:
        /* never show the SEIZE flag even though we use it internally */
        val = s->loc[locty].access & ~TPM_TIS_ACCESS_SEIZE;
        /* the pending flag is always calculated */
        if (tpm_tis_check_request_use_except(s, locty)) {
            val |= TPM_TIS_ACCESS_PENDING_REQUEST;
        }
        val |= !tpm_backend_get_tpm_established_flag(s->be_driver);
        break;
    case TPM_TIS_REG_INT_ENABLE:
        val = s->loc[locty].inte;
        break;
    case TPM_TIS_REG_INT_VECTOR:
        val = s->irq_num;
        break;
    case TPM_TIS_REG_INT_STATUS:
        val = s->loc[locty].ints;
        break;
    case TPM_TIS_REG_INTF_CAPABILITY:
        switch (s->be_tpm_version) {
        case TPM_VERSION_UNSPEC:
            val = 0;
            break;
        case TPM_VERSION_1_2:
            val = TPM_TIS_CAPABILITIES_SUPPORTED1_3;
            break;
        case TPM_VERSION_2_0:
            val = TPM_TIS_CAPABILITIES_SUPPORTED2_0;
            break;
        }
        break;
    case TPM_TIS_REG_STS:
        if (s->active_locty == locty) {
            if ((s->loc[locty].sts & TPM_TIS_STS_DATA_AVAILABLE)) {
                val = TPM_TIS_BURST_COUNT(
                       MIN(tpm_cmd_get_size(&s->buffer),
                           s->be_buffer_size)
                       - s->rw_offset) | s->loc[locty].sts;
            } else {
                avail = s->be_buffer_size - s->rw_offset;
                /*
                 * byte-sized reads should not return 0x00 for 0x100
                 * available bytes.
                 */
                if (size == 1 && avail > 0xff) {
                    avail = 0xff;
                }
                val = TPM_TIS_BURST_COUNT(avail) | s->loc[locty].sts;
            }
        }
        break;
    case TPM_TIS_REG_DATA_FIFO:
    case TPM_TIS_REG_DATA_XFIFO ... TPM_TIS_REG_DATA_XFIFO_END:
        if (s->active_locty == locty) {
            if (size > 4 - (addr & 0x3)) {
                /* prevent access beyond FIFO */
                size = 4 - (addr & 0x3);
            }
            val = 0;
            shift = 0;
            while (size > 0) {
                switch (s->loc[locty].state) {
                case TPM_TIS_STATE_COMPLETION:
                    v = tpm_tis_data_read(s, locty);
                    break;
                default:
                    v = TPM_TIS_NO_DATA_BYTE;
                    break;
                }
                val |= (v << shift);
                shift += 8;
                size--;
            }
            shift = 0; /* no more adjustments */
        }
        break;
    case TPM_TIS_REG_INTERFACE_ID:
        val = s->loc[locty].iface_id;
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

    trace_tpm_tis_mmio_read(size, addr, val);

    return val;
}

/*
 * Write a value to a register of the TIS interface
 * See specs pages 33-63 for description of the registers
 */
static void tpm_tis_mmio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    TPMState *s = opaque;
    uint16_t off = addr & 0xffc;
    uint8_t shift = (addr & 0x3) * 8;
    uint8_t locty = tpm_tis_locality_from_addr(addr);
    uint8_t active_locty, l;
    int c, set_new_locty = 1;
    uint16_t len;
    uint32_t mask = (size == 1) ? 0xff : ((size == 2) ? 0xffff : ~0);

    trace_tpm_tis_mmio_write(size, addr, val);

    if (locty == 4) {
        trace_tpm_tis_mmio_write_locty4();
        return;
    }

    if (tpm_backend_had_startup_error(s->be_driver)) {
        return;
    }

    val &= mask;

    if (shift) {
        val <<= shift;
        mask <<= shift;
    }

    mask ^= 0xffffffff;

    switch (off) {
    case TPM_TIS_REG_ACCESS:

        if ((val & TPM_TIS_ACCESS_SEIZE)) {
            val &= ~(TPM_TIS_ACCESS_REQUEST_USE |
                     TPM_TIS_ACCESS_ACTIVE_LOCALITY);
        }

        active_locty = s->active_locty;

        if ((val & TPM_TIS_ACCESS_ACTIVE_LOCALITY)) {
            /* give up locality if currently owned */
            if (s->active_locty == locty) {
                trace_tpm_tis_mmio_write_release_locty(locty);

                uint8_t newlocty = TPM_TIS_NO_LOCALITY;
                /* anybody wants the locality ? */
                for (c = TPM_TIS_NUM_LOCALITIES - 1; c >= 0; c--) {
                    if ((s->loc[c].access & TPM_TIS_ACCESS_REQUEST_USE)) {
                        trace_tpm_tis_mmio_write_locty_req_use(c);
                        newlocty = c;
                        break;
                    }
                }
                trace_tpm_tis_mmio_write_next_locty(newlocty);

                if (TPM_TIS_IS_VALID_LOCTY(newlocty)) {
                    set_new_locty = 0;
                    tpm_tis_prep_abort(s, locty, newlocty);
                } else {
                    active_locty = TPM_TIS_NO_LOCALITY;
                }
            } else {
                /* not currently the owner; clear a pending request */
                s->loc[locty].access &= ~TPM_TIS_ACCESS_REQUEST_USE;
            }
        }

        if ((val & TPM_TIS_ACCESS_BEEN_SEIZED)) {
            s->loc[locty].access &= ~TPM_TIS_ACCESS_BEEN_SEIZED;
        }

        if ((val & TPM_TIS_ACCESS_SEIZE)) {
            /*
             * allow seize if a locality is active and the requesting
             * locality is higher than the one that's active
             * OR
             * allow seize for requesting locality if no locality is
             * active
             */
            while ((TPM_TIS_IS_VALID_LOCTY(s->active_locty) &&
                    locty > s->active_locty) ||
                    !TPM_TIS_IS_VALID_LOCTY(s->active_locty)) {
                bool higher_seize = FALSE;

                /* already a pending SEIZE ? */
                if ((s->loc[locty].access & TPM_TIS_ACCESS_SEIZE)) {
                    break;
                }

                /* check for ongoing seize by a higher locality */
                for (l = locty + 1; l < TPM_TIS_NUM_LOCALITIES; l++) {
                    if ((s->loc[l].access & TPM_TIS_ACCESS_SEIZE)) {
                        higher_seize = TRUE;
                        break;
                    }
                }

                if (higher_seize) {
                    break;
                }

                /* cancel any seize by a lower locality */
                for (l = 0; l < locty - 1; l++) {
                    s->loc[l].access &= ~TPM_TIS_ACCESS_SEIZE;
                }

                s->loc[locty].access |= TPM_TIS_ACCESS_SEIZE;

                trace_tpm_tis_mmio_write_locty_seized(locty, s->active_locty);
                trace_tpm_tis_mmio_write_init_abort();

                set_new_locty = 0;
                tpm_tis_prep_abort(s, s->active_locty, locty);
                break;
            }
        }

        if ((val & TPM_TIS_ACCESS_REQUEST_USE)) {
            if (s->active_locty != locty) {
                if (TPM_TIS_IS_VALID_LOCTY(s->active_locty)) {
                    s->loc[locty].access |= TPM_TIS_ACCESS_REQUEST_USE;
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
        if (s->active_locty != locty) {
            break;
        }

        s->loc[locty].inte &= mask;
        s->loc[locty].inte |= (val & (TPM_TIS_INT_ENABLED |
                                        TPM_TIS_INT_POLARITY_MASK |
                                        TPM_TIS_INTERRUPTS_SUPPORTED));
        break;
    case TPM_TIS_REG_INT_VECTOR:
        /* hard wired -- ignore */
        break;
    case TPM_TIS_REG_INT_STATUS:
        if (s->active_locty != locty) {
            break;
        }

        /* clearing of interrupt flags */
        if (((val & TPM_TIS_INTERRUPTS_SUPPORTED)) &&
            (s->loc[locty].ints & TPM_TIS_INTERRUPTS_SUPPORTED)) {
            s->loc[locty].ints &= ~val;
            if (s->loc[locty].ints == 0) {
                qemu_irq_lower(s->irq);
                trace_tpm_tis_mmio_write_lowering_irq();
            }
        }
        s->loc[locty].ints &= ~(val & TPM_TIS_INTERRUPTS_SUPPORTED);
        break;
    case TPM_TIS_REG_STS:
        if (s->active_locty != locty) {
            break;
        }

        if (s->be_tpm_version == TPM_VERSION_2_0) {
            /* some flags that are only supported for TPM 2 */
            if (val & TPM_TIS_STS_COMMAND_CANCEL) {
                if (s->loc[locty].state == TPM_TIS_STATE_EXECUTION) {
                    /*
                     * request the backend to cancel. Some backends may not
                     * support it
                     */
                    tpm_backend_cancel_cmd(s->be_driver);
                }
            }

            if (val & TPM_TIS_STS_RESET_ESTABLISHMENT_BIT) {
                if (locty == 3 || locty == 4) {
                    tpm_backend_reset_tpm_established_flag(s->be_driver, locty);
                }
            }
        }

        val &= (TPM_TIS_STS_COMMAND_READY | TPM_TIS_STS_TPM_GO |
                TPM_TIS_STS_RESPONSE_RETRY);

        if (val == TPM_TIS_STS_COMMAND_READY) {
            switch (s->loc[locty].state) {

            case TPM_TIS_STATE_READY:
                s->rw_offset = 0;
            break;

            case TPM_TIS_STATE_IDLE:
                tpm_tis_sts_set(&s->loc[locty], TPM_TIS_STS_COMMAND_READY);
                s->loc[locty].state = TPM_TIS_STATE_READY;
                tpm_tis_raise_irq(s, locty, TPM_TIS_INT_COMMAND_READY);
            break;

            case TPM_TIS_STATE_EXECUTION:
            case TPM_TIS_STATE_RECEPTION:
                /* abort currently running command */
                trace_tpm_tis_mmio_write_init_abort();
                tpm_tis_prep_abort(s, locty, locty);
            break;

            case TPM_TIS_STATE_COMPLETION:
                s->rw_offset = 0;
                /* shortcut to ready state with C/R set */
                s->loc[locty].state = TPM_TIS_STATE_READY;
                if (!(s->loc[locty].sts & TPM_TIS_STS_COMMAND_READY)) {
                    tpm_tis_sts_set(&s->loc[locty],
                                    TPM_TIS_STS_COMMAND_READY);
                    tpm_tis_raise_irq(s, locty, TPM_TIS_INT_COMMAND_READY);
                }
                s->loc[locty].sts &= ~(TPM_TIS_STS_DATA_AVAILABLE);
            break;

            }
        } else if (val == TPM_TIS_STS_TPM_GO) {
            switch (s->loc[locty].state) {
            case TPM_TIS_STATE_RECEPTION:
                if ((s->loc[locty].sts & TPM_TIS_STS_EXPECT) == 0) {
                    tpm_tis_tpm_send(s, locty);
                }
                break;
            default:
                /* ignore */
                break;
            }
        } else if (val == TPM_TIS_STS_RESPONSE_RETRY) {
            switch (s->loc[locty].state) {
            case TPM_TIS_STATE_COMPLETION:
                s->rw_offset = 0;
                tpm_tis_sts_set(&s->loc[locty],
                                TPM_TIS_STS_VALID|
                                TPM_TIS_STS_DATA_AVAILABLE);
                break;
            default:
                /* ignore */
                break;
            }
        }
        break;
    case TPM_TIS_REG_DATA_FIFO:
    case TPM_TIS_REG_DATA_XFIFO ... TPM_TIS_REG_DATA_XFIFO_END:
        /* data fifo */
        if (s->active_locty != locty) {
            break;
        }

        if (s->loc[locty].state == TPM_TIS_STATE_IDLE ||
            s->loc[locty].state == TPM_TIS_STATE_EXECUTION ||
            s->loc[locty].state == TPM_TIS_STATE_COMPLETION) {
            /* drop the byte */
        } else {
            trace_tpm_tis_mmio_write_data2send(val, size);
            if (s->loc[locty].state == TPM_TIS_STATE_READY) {
                s->loc[locty].state = TPM_TIS_STATE_RECEPTION;
                tpm_tis_sts_set(&s->loc[locty],
                                TPM_TIS_STS_EXPECT | TPM_TIS_STS_VALID);
            }

            val >>= shift;
            if (size > 4 - (addr & 0x3)) {
                /* prevent access beyond FIFO */
                size = 4 - (addr & 0x3);
            }

            while ((s->loc[locty].sts & TPM_TIS_STS_EXPECT) && size > 0) {
                if (s->rw_offset < s->be_buffer_size) {
                    s->buffer[s->rw_offset++] =
                        (uint8_t)val;
                    val >>= 8;
                    size--;
                } else {
                    tpm_tis_sts_set(&s->loc[locty], TPM_TIS_STS_VALID);
                }
            }

            /* check for complete packet */
            if (s->rw_offset > 5 &&
                (s->loc[locty].sts & TPM_TIS_STS_EXPECT)) {
                /* we have a packet length - see if we have all of it */
                bool need_irq = !(s->loc[locty].sts & TPM_TIS_STS_VALID);

                len = tpm_cmd_get_size(&s->buffer);
                if (len > s->rw_offset) {
                    tpm_tis_sts_set(&s->loc[locty],
                                    TPM_TIS_STS_EXPECT | TPM_TIS_STS_VALID);
                } else {
                    /* packet complete */
                    tpm_tis_sts_set(&s->loc[locty], TPM_TIS_STS_VALID);
                }
                if (need_irq) {
                    tpm_tis_raise_irq(s, locty, TPM_TIS_INT_STS_VALID);
                }
            }
        }
        break;
    case TPM_TIS_REG_INTERFACE_ID:
        if (val & TPM_TIS_IFACE_ID_INT_SEL_LOCK) {
            for (l = 0; l < TPM_TIS_NUM_LOCALITIES; l++) {
                s->loc[l].iface_id |= TPM_TIS_IFACE_ID_INT_SEL_LOCK;
            }
        }
        break;
    }
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

/*
 * Get the TPMVersion of the backend device being used
 */
static enum TPMVersion tpm_tis_get_tpm_version(TPMIf *ti)
{
    TPMState *s = TPM(ti);

    if (tpm_backend_had_startup_error(s->be_driver)) {
        return TPM_VERSION_UNSPEC;
    }

    return tpm_backend_get_tpm_version(s->be_driver);
}

/*
 * This function is called when the machine starts, resets or due to
 * S3 resume.
 */
static void tpm_tis_reset(DeviceState *dev)
{
    TPMState *s = TPM(dev);
    int c;

    s->be_tpm_version = tpm_backend_get_tpm_version(s->be_driver);
    s->be_buffer_size = MIN(tpm_backend_get_buffer_size(s->be_driver),
                            TPM_TIS_BUFFER_MAX);

    tpm_backend_reset(s->be_driver);

    s->active_locty = TPM_TIS_NO_LOCALITY;
    s->next_locty = TPM_TIS_NO_LOCALITY;
    s->aborting_locty = TPM_TIS_NO_LOCALITY;

    for (c = 0; c < TPM_TIS_NUM_LOCALITIES; c++) {
        s->loc[c].access = TPM_TIS_ACCESS_TPM_REG_VALID_STS;
        switch (s->be_tpm_version) {
        case TPM_VERSION_UNSPEC:
            break;
        case TPM_VERSION_1_2:
            s->loc[c].sts = TPM_TIS_STS_TPM_FAMILY1_2;
            s->loc[c].iface_id = TPM_TIS_IFACE_ID_SUPPORTED_FLAGS1_3;
            break;
        case TPM_VERSION_2_0:
            s->loc[c].sts = TPM_TIS_STS_TPM_FAMILY2_0;
            s->loc[c].iface_id = TPM_TIS_IFACE_ID_SUPPORTED_FLAGS2_0;
            break;
        }
        s->loc[c].inte = TPM_TIS_INT_POLARITY_LOW_LEVEL;
        s->loc[c].ints = 0;
        s->loc[c].state = TPM_TIS_STATE_IDLE;

        s->rw_offset = 0;
    }

    tpm_backend_startup_tpm(s->be_driver, s->be_buffer_size);
}

/* persistent state handling */

static int tpm_tis_pre_save(void *opaque)
{
    TPMState *s = opaque;
    uint8_t locty = s->active_locty;

    trace_tpm_tis_pre_save(locty, s->rw_offset);

    if (DEBUG_TIS) {
        tpm_tis_dump_state(opaque, 0);
    }

    /*
     * Synchronize with backend completion.
     */
    tpm_backend_finish_sync(s->be_driver);

    return 0;
}

static const VMStateDescription vmstate_locty = {
    .name = "tpm-tis/locty",
    .version_id = 0,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(state, TPMLocality),
        VMSTATE_UINT32(inte, TPMLocality),
        VMSTATE_UINT32(ints, TPMLocality),
        VMSTATE_UINT8(access, TPMLocality),
        VMSTATE_UINT32(sts, TPMLocality),
        VMSTATE_UINT32(iface_id, TPMLocality),
        VMSTATE_END_OF_LIST(),
    }
};

static const VMStateDescription vmstate_tpm_tis = {
    .name = "tpm-tis",
    .version_id = 0,
    .pre_save  = tpm_tis_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_BUFFER(buffer, TPMState),
        VMSTATE_UINT16(rw_offset, TPMState),
        VMSTATE_UINT8(active_locty, TPMState),
        VMSTATE_UINT8(aborting_locty, TPMState),
        VMSTATE_UINT8(next_locty, TPMState),

        VMSTATE_STRUCT_ARRAY(loc, TPMState, TPM_TIS_NUM_LOCALITIES, 0,
                             vmstate_locty, TPMLocality),

        VMSTATE_END_OF_LIST()
    }
};

static Property tpm_tis_properties[] = {
    DEFINE_PROP_UINT32("irq", TPMState, irq_num, TPM_TIS_IRQ),
    DEFINE_PROP_TPMBE("tpmdev", TPMState, be_driver),
    DEFINE_PROP_END_OF_LIST(),
};

static void tpm_tis_realizefn(DeviceState *dev, Error **errp)
{
    TPMState *s = TPM(dev);

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }

    if (!s->be_driver) {
        error_setg(errp, "'tpmdev' property is required");
        return;
    }
    if (s->irq_num > 15) {
        error_setg(errp, "IRQ %d is outside valid range of 0 to 15",
                   s->irq_num);
        return;
    }

    isa_init_irq(&s->busdev, &s->irq, s->irq_num);

    memory_region_add_subregion(isa_address_space(ISA_DEVICE(dev)),
                                TPM_TIS_ADDR_BASE, &s->mmio);
}

static void tpm_tis_initfn(Object *obj)
{
    TPMState *s = TPM(obj);

    memory_region_init_io(&s->mmio, OBJECT(s), &tpm_tis_memory_ops,
                          s, "tpm-tis-mmio",
                          TPM_TIS_NUM_LOCALITIES << TPM_TIS_LOCALITY_SHIFT);
}

static void tpm_tis_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);

    dc->realize = tpm_tis_realizefn;
    dc->props = tpm_tis_properties;
    dc->reset = tpm_tis_reset;
    dc->vmsd  = &vmstate_tpm_tis;
    tc->model = TPM_MODEL_TPM_TIS;
    tc->get_version = tpm_tis_get_tpm_version;
    tc->request_completed = tpm_tis_request_completed;
}

static const TypeInfo tpm_tis_info = {
    .name = TYPE_TPM_TIS,
    .parent = TYPE_ISA_DEVICE,
    .instance_size = sizeof(TPMState),
    .instance_init = tpm_tis_initfn,
    .class_init  = tpm_tis_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { }
    }
};

static void tpm_tis_register(void)
{
    type_register_static(&tpm_tis_info);
}

type_init(tpm_tis_register)
