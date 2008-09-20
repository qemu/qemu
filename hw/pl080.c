/*
 * Arm PrimeCell PL080/PL081 DMA controller
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#include "hw.h"
#include "primecell.h"

#define PL080_MAX_CHANNELS 8
#define PL080_CONF_E    0x1
#define PL080_CONF_M1   0x2
#define PL080_CONF_M2   0x4

#define PL080_CCONF_H   0x40000
#define PL080_CCONF_A   0x20000
#define PL080_CCONF_L   0x10000
#define PL080_CCONF_ITC 0x08000
#define PL080_CCONF_IE  0x04000
#define PL080_CCONF_E   0x00001

#define PL080_CCTRL_I   0x80000000
#define PL080_CCTRL_DI  0x08000000
#define PL080_CCTRL_SI  0x04000000
#define PL080_CCTRL_D   0x02000000
#define PL080_CCTRL_S   0x01000000

typedef struct {
    uint32_t src;
    uint32_t dest;
    uint32_t lli;
    uint32_t ctrl;
    uint32_t conf;
} pl080_channel;

typedef struct {
    uint32_t base;
    uint8_t tc_int;
    uint8_t tc_mask;
    uint8_t err_int;
    uint8_t err_mask;
    uint32_t conf;
    uint32_t sync;
    uint32_t req_single;
    uint32_t req_burst;
    pl080_channel chan[PL080_MAX_CHANNELS];
    int nchannels;
    /* Flag to avoid recursive DMA invocations.  */
    int running;
    qemu_irq irq;
} pl080_state;

static const unsigned char pl080_id[] =
{ 0x80, 0x10, 0x04, 0x0a, 0x0d, 0xf0, 0x05, 0xb1 };

static const unsigned char pl081_id[] =
{ 0x81, 0x10, 0x04, 0x0a, 0x0d, 0xf0, 0x05, 0xb1 };

static void pl080_update(pl080_state *s)
{
    if ((s->tc_int & s->tc_mask)
            || (s->err_int & s->err_mask))
        qemu_irq_raise(s->irq);
    else
        qemu_irq_lower(s->irq);
}

static void pl080_run(pl080_state *s)
{
    int c;
    int flow;
    pl080_channel *ch;
    int swidth;
    int dwidth;
    int xsize;
    int n;
    int src_id;
    int dest_id;
    int size;
    uint8_t buff[4];
    uint32_t req;

    s->tc_mask = 0;
    for (c = 0; c < s->nchannels; c++) {
        if (s->chan[c].conf & PL080_CCONF_ITC)
            s->tc_mask |= 1 << c;
        if (s->chan[c].conf & PL080_CCONF_IE)
            s->err_mask |= 1 << c;
    }

    if ((s->conf & PL080_CONF_E) == 0)
        return;

cpu_abort(cpu_single_env, "DMA active\n");
    /* If we are already in the middle of a DMA operation then indicate that
       there may be new DMA requests and return immediately.  */
    if (s->running) {
        s->running++;
        return;
    }
    s->running = 1;
    while (s->running) {
        for (c = 0; c < s->nchannels; c++) {
            ch = &s->chan[c];
again:
            /* Test if thiws channel has any pending DMA requests.  */
            if ((ch->conf & (PL080_CCONF_H | PL080_CCONF_E))
                    != PL080_CCONF_E)
                continue;
            flow = (ch->conf >> 11) & 7;
            if (flow >= 4) {
                cpu_abort(cpu_single_env,
                    "pl080_run: Peripheral flow control not implemented\n");
            }
            src_id = (ch->conf >> 1) & 0x1f;
            dest_id = (ch->conf >> 6) & 0x1f;
            size = ch->ctrl & 0xfff;
            req = s->req_single | s->req_burst;
            switch (flow) {
            case 0:
                break;
            case 1:
                if ((req & (1u << dest_id)) == 0)
                    size = 0;
                break;
            case 2:
                if ((req & (1u << src_id)) == 0)
                    size = 0;
                break;
            case 3:
                if ((req & (1u << src_id)) == 0
                        || (req & (1u << dest_id)) == 0)
                    size = 0;
                break;
            }
            if (!size)
                continue;

            /* Transfer one element.  */
            /* ??? Should transfer multiple elements for a burst request.  */
            /* ??? Unclear what the proper behavior is when source and
               destination widths are different.  */
            swidth = 1 << ((ch->ctrl >> 18) & 7);
            dwidth = 1 << ((ch->ctrl >> 21) & 7);
            for (n = 0; n < dwidth; n+= swidth) {
                cpu_physical_memory_read(ch->src, buff + n, swidth);
                if (ch->ctrl & PL080_CCTRL_SI)
                    ch->src += swidth;
            }
            xsize = (dwidth < swidth) ? swidth : dwidth;
            /* ??? This may pad the value incorrectly for dwidth < 32.  */
            for (n = 0; n < xsize; n += dwidth) {
                cpu_physical_memory_write(ch->dest + n, buff + n, dwidth);
                if (ch->ctrl & PL080_CCTRL_DI)
                    ch->dest += swidth;
            }

            size--;
            ch->ctrl = (ch->ctrl & 0xfffff000) | size;
            if (size == 0) {
                /* Transfer complete.  */
                if (ch->lli) {
                    ch->src = ldl_phys(ch->lli);
                    ch->dest = ldl_phys(ch->lli + 4);
                    ch->ctrl = ldl_phys(ch->lli + 12);
                    ch->lli = ldl_phys(ch->lli + 8);
                } else {
                    ch->conf &= ~PL080_CCONF_E;
                }
                if (ch->ctrl & PL080_CCTRL_I) {
                    s->tc_int |= 1 << c;
                }
            }
            goto again;
        }
        if (--s->running)
            s->running = 1;
    }
}

static uint32_t pl080_read(void *opaque, target_phys_addr_t offset)
{
    pl080_state *s = (pl080_state *)opaque;
    uint32_t i;
    uint32_t mask;

    offset -= s->base;
    if (offset >= 0xfe0 && offset < 0x1000) {
        if (s->nchannels == 8) {
            return pl080_id[(offset - 0xfe0) >> 2];
        } else {
            return pl081_id[(offset - 0xfe0) >> 2];
        }
    }
    if (offset >= 0x100 && offset < 0x200) {
        i = (offset & 0xe0) >> 5;
        if (i >= s->nchannels)
            goto bad_offset;
        switch (offset >> 2) {
        case 0: /* SrcAddr */
            return s->chan[i].src;
        case 1: /* DestAddr */
            return s->chan[i].dest;
        case 2: /* LLI */
            return s->chan[i].lli;
        case 3: /* Control */
            return s->chan[i].ctrl;
        case 4: /* Configuration */
            return s->chan[i].conf;
        default:
            goto bad_offset;
        }
    }
    switch (offset >> 2) {
    case 0: /* IntStatus */
        return (s->tc_int & s->tc_mask) | (s->err_int & s->err_mask);
    case 1: /* IntTCStatus */
        return (s->tc_int & s->tc_mask);
    case 3: /* IntErrorStatus */
        return (s->err_int & s->err_mask);
    case 5: /* RawIntTCStatus */
        return s->tc_int;
    case 6: /* RawIntErrorStatus */
        return s->err_int;
    case 7: /* EnbldChns */
        mask = 0;
        for (i = 0; i < s->nchannels; i++) {
            if (s->chan[i].conf & PL080_CCONF_E)
                mask |= 1 << i;
        }
        return mask;
    case 8: /* SoftBReq */
    case 9: /* SoftSReq */
    case 10: /* SoftLBReq */
    case 11: /* SoftLSReq */
        /* ??? Implement these. */
        return 0;
    case 12: /* Configuration */
        return s->conf;
    case 13: /* Sync */
        return s->sync;
    default:
    bad_offset:
        cpu_abort(cpu_single_env, "pl080_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void pl080_write(void *opaque, target_phys_addr_t offset,
                          uint32_t value)
{
    pl080_state *s = (pl080_state *)opaque;
    int i;

    offset -= s->base;
    if (offset >= 0x100 && offset < 0x200) {
        i = (offset & 0xe0) >> 5;
        if (i >= s->nchannels)
            goto bad_offset;
        switch (offset >> 2) {
        case 0: /* SrcAddr */
            s->chan[i].src = value;
            break;
        case 1: /* DestAddr */
            s->chan[i].dest = value;
            break;
        case 2: /* LLI */
            s->chan[i].lli = value;
            break;
        case 3: /* Control */
            s->chan[i].ctrl = value;
            break;
        case 4: /* Configuration */
            s->chan[i].conf = value;
            pl080_run(s);
            break;
        }
    }
    switch (offset >> 2) {
    case 2: /* IntTCClear */
        s->tc_int &= ~value;
        break;
    case 4: /* IntErrorClear */
        s->err_int &= ~value;
        break;
    case 8: /* SoftBReq */
    case 9: /* SoftSReq */
    case 10: /* SoftLBReq */
    case 11: /* SoftLSReq */
        /* ??? Implement these.  */
        cpu_abort(cpu_single_env, "pl080_write: Soft DMA not implemented\n");
        break;
    case 12: /* Configuration */
        s->conf = value;
        if (s->conf & (PL080_CONF_M1 | PL080_CONF_M1)) {
            cpu_abort(cpu_single_env,
                      "pl080_write: Big-endian DMA not implemented\n");
        }
        pl080_run(s);
        break;
    case 13: /* Sync */
        s->sync = value;
        break;
    default:
    bad_offset:
        cpu_abort(cpu_single_env, "pl080_write: Bad offset %x\n", (int)offset);
    }
    pl080_update(s);
}

static CPUReadMemoryFunc *pl080_readfn[] = {
   pl080_read,
   pl080_read,
   pl080_read
};

static CPUWriteMemoryFunc *pl080_writefn[] = {
   pl080_write,
   pl080_write,
   pl080_write
};

/* The PL080 and PL081 are the same except for the number of channels
   they implement (8 and 2 respectively).  */
void *pl080_init(uint32_t base, qemu_irq irq, int nchannels)
{
    int iomemtype;
    pl080_state *s;

    s = (pl080_state *)qemu_mallocz(sizeof(pl080_state));
    iomemtype = cpu_register_io_memory(0, pl080_readfn,
                                       pl080_writefn, s);
    cpu_register_physical_memory(base, 0x00001000, iomemtype);
    s->base = base;
    s->irq = irq;
    s->nchannels = nchannels;
    /* ??? Save/restore.  */
    return s;
}

