/*
 * QEMU Soundblaster 16 emulation
 * 
 * Copyright (c) 2003 Vassili Karpov (malc)
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
#include "vl.h"

#define MIN(a, b) ((a)>(b)?(b):(a))
#define LENOFA(a) ((int) (sizeof(a)/sizeof(a[0])))

#define dolog(...) fprintf (stderr, "sb16: " __VA_ARGS__);

/* #define DEBUG_SB16 */

#ifdef DEBUG_SB16
#define lwarn(...) fprintf (stderr, "sb16: " __VA_ARGS__)
#define linfo(...) fprintf (stderr, "sb16: " __VA_ARGS__)
#define ldebug(...) fprintf (stderr, "sb16: " __VA_ARGS__)
#else
#define lwarn(...)
#define linfo(...)
#define ldebug(...)
#endif

#define IO_READ_PROTO(name) \
    uint32_t name (void *opaque, uint32_t nport)
#define IO_WRITE_PROTO(name) \
    void name (void *opaque, uint32_t nport, uint32_t val)

static const char e3[] =
    "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992\0"
    "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1994-1997";
    /* "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1994."; */

static struct {
    int ver_lo;
    int ver_hi;
    int irq;
    int dma;
    int hdma;
    int port;
    int mix_block;
} sb = {5, 4, 5, 1, 5, 0x220, -1};

static int mix_block, noirq;

typedef struct SB16State {
    int in_index;
    int out_data_len;
    int fmt_stereo;
    int fmt_signed;
    int fmt_bits;
    int dma_auto;
    int dma_buffer_size;
    int fifo;
    int freq;
    int time_const;
    int speaker;
    int needed_bytes;
    int cmd;
    int dma_pos;
    int use_hdma;

    int v2x6;

    uint8_t in2_data[10];
    uint8_t out_data[1024];

    int left_till_irq;
    uint64_t nzero;
    uint8_t last_read_byte;
    uint8_t test_reg;

    /* mixer state */
    int mixer_nreg;
    uint8_t mixer_regs[256];
} SB16State;

/* XXX: suppress that and use a context */
static struct SB16State dsp;

static void log_dsp (SB16State *dsp)
{
    ldebug ("%c:%c:%d:%c:dmabuf=%d:pos=%d:freq=%d:timeconst=%d:speaker=%d\n",
           dsp->fmt_stereo ? 'S' : 'M',
           dsp->fmt_signed ? 'S' : 'U',
           dsp->fmt_bits,
           dsp->dma_auto ? 'a' : 's',
           dsp->dma_buffer_size,
           dsp->dma_pos,
           dsp->freq,
           dsp->time_const,
           dsp->speaker);
}

static void control (int hold)
{
    linfo ("%d high %d\n", hold, dsp.use_hdma);
    if (hold) {
        if (dsp.use_hdma)
            DMA_hold_DREQ (sb.hdma);
        else
            DMA_hold_DREQ (sb.dma);
    }
    else {
        if (dsp.use_hdma)
            DMA_release_DREQ (sb.hdma);
        else
            DMA_release_DREQ (sb.dma);
    }
}

static void dma_cmd (uint8_t cmd, uint8_t d0, int dma_len)
{
    int bps;
    audfmt_e fmt;

    dsp.use_hdma = cmd < 0xc0;
    dsp.fifo = (cmd >> 1) & 1;
    dsp.dma_auto = (cmd >> 2) & 1;

    switch (cmd >> 4) {
    case 11:
        dsp.fmt_bits = 16;
        break;

    case 12:
        dsp.fmt_bits = 8;
        break;
    }

    dsp.fmt_signed = (d0 >> 4) & 1;
    dsp.fmt_stereo = (d0 >> 5) & 1;

    if (-1 != dsp.time_const) {
        int tmp;

        tmp = 256 - dsp.time_const;
        dsp.freq = (1000000 + (tmp / 2)) / tmp;
    }
    bps = 1 << (16 == dsp.fmt_bits);

    if (-1 != dma_len)
        dsp.dma_buffer_size = (dma_len + 1) * bps;

    linfo ("frequency %d, stereo %d, signed %d, bits %d, size %d, auto %d\n",
           dsp.freq, dsp.fmt_stereo, dsp.fmt_signed, dsp.fmt_bits,
           dsp.dma_buffer_size, dsp.dma_auto);

    if (16 == dsp.fmt_bits) {
        if (dsp.fmt_signed) {
            fmt = AUD_FMT_S16;
        }
        else {
            fmt = AUD_FMT_U16;
        }
    }
    else {
        if (dsp.fmt_signed) {
            fmt = AUD_FMT_S8;
        }
        else {
            fmt = AUD_FMT_U8;
        }
    }

    dsp.dma_pos = 0;
    dsp.left_till_irq = dsp.dma_buffer_size;

    if (sb.mix_block) {
        mix_block = sb.mix_block;
    }
    else {
        int align;

        align = bps << dsp.fmt_stereo;
        mix_block = ((dsp.freq * align) / 100) & ~(align - 1);
    }

    if (dsp.freq)
    AUD_reset (dsp.freq, 1 << dsp.fmt_stereo, fmt);
    control (1);
    dsp.speaker = 1;
}

static inline void dsp_out_data(SB16State *dsp, int val)
{
    if (dsp->out_data_len < sizeof(dsp->out_data))
        dsp->out_data[dsp->out_data_len++] = val;
}

static inline uint8_t dsp_get_data(SB16State *dsp)
{
    if (dsp->in_index)
        return dsp->in2_data[--dsp->in_index];
    else
        return 0;
}

static void command (SB16State *dsp, uint8_t cmd)
{
    linfo ("command: %#x\n", cmd);

    if (cmd > 0xaf && cmd < 0xd0) {
        if (cmd & 8)
            goto error;

        switch (cmd >> 4) {
        case 11:
        case 12:
            break;
        default:
            dolog ("command: %#x wrong bits specification\n", cmd);
            goto error;
        }
        dsp->needed_bytes = 3;
    }
    else {
        switch (cmd) {
        case 0x00:
        case 0xe7:
            /* IMS uses those when probing for sound devices */
            return;

        case 0x03:
        case 0x04:
            dsp_out_data (dsp, 0);
            return;

        case 0x05:
            dsp->needed_bytes = 2;
            break;

        case 0x0e:
            dsp->needed_bytes = 2;
            break;

        case 0x0f:
            dsp->needed_bytes = 1;
            break;

        case 0x10:
            dsp->needed_bytes = 1;
            break;

        case 0x14:
            dsp->needed_bytes = 2;
            dsp->dma_buffer_size = 0;
            break;

        case 0x20:
            dsp_out_data(dsp, 0xff);
            break;

        case 0x35:
            lwarn ("MIDI commands not implemented\n");
            break;

        case 0x40:
            dsp->freq = -1;
            dsp->time_const = -1;
            dsp->needed_bytes = 1;
            break;

        case 0x41:
        case 0x42:
            dsp->freq = -1;
            dsp->time_const = -1;
            dsp->needed_bytes = 2;
            break;

        case 0x45:
            dsp_out_data (dsp, 0xaa);
        case 0x47:                /* Continue Auto-Initialize DMA 16bit */
            break;

        case 0x48:
            dsp->needed_bytes = 2;
            break;

        case 0x27:                /* ????????? */
        case 0x4e:
            return;

        case 0x80:
            cmd = -1;
            break;

        case 0x90:
        case 0x91:
            {
                uint8_t d0;

                d0 = 4;
                /* if (dsp->fmt_signed) d0 |= 16; */
                /* if (dsp->fmt_stereo) d0 |= 32; */
                dma_cmd (cmd == 0x90 ? 0xc4 : 0xc0, d0, -1);
                cmd = -1;
                break;
            }

        case 0xd0:                /* XXX */
            control (0);
            return;

        case 0xd1:
            dsp->speaker = 1;
            break;

        case 0xd3:
            dsp->speaker = 0;
            return;

        case 0xd4:
            control (1);
            break;

        case 0xd5:
            control (0);
            break;

        case 0xd6:
            control (1);
            break;

        case 0xd9:
            control (0);
            dsp->dma_auto = 0;
            return;

        case 0xda:
            control (0);
            dsp->dma_auto = 0;
            break;

        case 0xe0:
            dsp->needed_bytes = 1;
            break;

        case 0xe1:
            dsp_out_data(dsp, sb.ver_lo);
            dsp_out_data(dsp, sb.ver_hi);
            return;

        case 0xe3:
            {
                int i;
                for (i = sizeof (e3) - 1; i >= 0; i--)
                    dsp_out_data (dsp, e3[i]);
                return;
            }

        case 0xe4:              /* write test reg */
            dsp->needed_bytes = 1;
            break;

        case 0xe8:              /* read test reg */
            dsp_out_data (dsp, dsp->test_reg);
            break;

        case 0xf2:
            dsp_out_data (dsp, 0xaa);
            dsp->mixer_regs[0x82] |= dsp->mixer_regs[0x80];
            pic_set_irq (sb.irq, 1);
            return;

        case 0xf9:
            dsp->needed_bytes = 1;
            break;

        case 0xfa:
            dsp_out_data (dsp, 0);
            break;

        case 0xfc:              /* FIXME */
            dsp_out_data (dsp, 0);
            break;

        default:
            dolog ("unrecognized command %#x\n", cmd);
            goto error;
        }
    }
    dsp->cmd = cmd;
    return;

 error:
    return;
}

static void complete (SB16State *dsp)
{
    int d0, d1, d2;
    linfo ("complete command %#x, in_index %d, needed_bytes %d\n",
           dsp->cmd, dsp->in_index, dsp->needed_bytes);

    if (dsp->cmd > 0xaf && dsp->cmd < 0xd0) {
        d2 = dsp_get_data (dsp);
        d1 = dsp_get_data (dsp);
        d0 = dsp_get_data (dsp);

        ldebug ("d0 = %d, d1 = %d, d2 = %d\n",
                d0, d1, d2);
        dma_cmd (dsp->cmd, d0, d1 + (d2 << 8));
    }
    else {
        switch (dsp->cmd) {
        case 0x04:
        case 0x10:
            dsp_get_data (dsp);
            break;

        case 0x0f:
            d0 = dsp_get_data (dsp);
            dsp_out_data (dsp, 0xf8);
            break;

        case 0x05:
        case 0x0e:
            dsp_get_data (dsp);
            dsp_get_data (dsp);
            break;

        case 0x14:
            {
                int save_left;
                int save_pos;

                d1 = dsp_get_data (dsp);
                d0 = dsp_get_data (dsp);

                save_left = dsp->left_till_irq;
                save_pos = dsp->dma_pos;
                dma_cmd (0xc0, 0, d0 + (d1 << 8));
                dsp->left_till_irq = save_left;
                dsp->dma_pos = save_pos;

                linfo ("set buffer size data[%d, %d] %d pos %d\n",
                       d0, d1, dsp->dma_buffer_size, dsp->dma_pos);
                break;
            }

        case 0x40:
            dsp->time_const = dsp_get_data (dsp);
            linfo ("set time const %d\n", dsp->time_const);
            break;

        case 0x41:
        case 0x42:
            d1 = dsp_get_data (dsp);
            d0 = dsp_get_data (dsp);

            dsp->freq = d1 + (d0 << 8);
            linfo ("set freq %#x, %#x = %d\n", d1, d0, dsp->freq);
            break;

        case 0x48:
            d1 = dsp_get_data (dsp);
            d0 = dsp_get_data (dsp);
            dsp->dma_buffer_size = d1 + (d0 << 8);
            linfo ("set dma len %#x, %#x = %d\n",
                   d1, d0, dsp->dma_buffer_size);
            break;

        case 0xe0:
            d0 = dsp_get_data (dsp);
            dsp->out_data_len = 0;
            linfo ("data = %#x\n", d0);
            dsp_out_data (dsp, d0 ^ 0xff);
            break;

        case 0xe4:
            dsp->test_reg = dsp_get_data (dsp);
            break;


        case 0xf9:
            d0 = dsp_get_data (dsp);
            ldebug ("f9 <- %#x\n", d0);
            switch (d0) {
            case 0x0e: dsp_out_data (dsp, 0xff); break;
            case 0x0f: dsp_out_data (dsp, 0x07); break;
            case 0xf9: dsp_out_data (dsp, 0x00); break;
            case 0x37:
                dsp_out_data (dsp, 0x38); break;
            default:
                dsp_out_data (dsp, 0);
            }
            break;

        default:
            dolog ("complete: unrecognized command %#x\n", dsp->cmd);
            return;
        }
    }

    dsp->needed_bytes = 0;
    dsp->cmd = -1;
    return;
}

static IO_WRITE_PROTO (dsp_write)
{
    SB16State *dsp = opaque;
    int iport;

    iport = nport - sb.port;

    ldebug ("dsp_write %#x <- %#x\n", nport, val);
    switch (iport) {
    case 0x6:
        control (0);
        if (0 == val)
            dsp->v2x6 = 0;
        else if ((1 == val) && (0 == dsp->v2x6)) {
            dsp->v2x6 = 1;
            dsp->dma_pos = 0;
            dsp->dma_auto = 0;
            dsp->in_index = 0;
            dsp->out_data_len = 0;
            dsp->left_till_irq = 0;
            dsp->speaker = 0;
            dsp->needed_bytes = 0;
            pic_set_irq (sb.irq, 0);
            dsp_out_data(dsp, 0xaa);
        }
        else
            dsp->v2x6 = ~0;
        break;

    case 0xc:                   /* write data or command | write status */
        if (0 == dsp->needed_bytes) {
            command (dsp, val);
            if (0 == dsp->needed_bytes) {
                log_dsp (dsp);
            }
        }
        else {
            if (dsp->in_index == sizeof (dsp->in2_data)) {
                dolog ("in data overrun\n");
            }
            else {
                dsp->in2_data[dsp->in_index++] = val;
            if (dsp->in_index == dsp->needed_bytes) {
                dsp->needed_bytes = 0;
                complete (dsp);
                log_dsp (dsp);
            }
        }
        }
        break;

    default:
        dolog ("dsp_write (nport=%#x, val=%#x)\n", nport, val);
        break;
    }
}

static IO_READ_PROTO (dsp_read)
{
    SB16State *dsp = opaque;
    int iport, retval;

    iport = nport - sb.port;

    switch (iport) {
    case 0x6:                   /* reset */
        control (0);
        retval = 0;
        dsp->speaker = 0;
        break;

    case 0xa:                   /* read data */
        if (dsp->out_data_len) {
            retval = dsp->out_data[--dsp->out_data_len];
            dsp->last_read_byte = retval;
        } else {
            retval = dsp->last_read_byte;
            dolog ("empty output buffer\n");
            /* goto error; */
        }
        break;

    case 0xc:                   /* 0xxxxxxx can write */
        retval = 0;
        if (dsp->out_data_len == sizeof (dsp->out_data)) retval |= 0x80;
        break;

    case 0xd:                   /* timer interrupt clear */
        dolog ("timer interrupt clear\n");
        goto error;

    case 0xe:                   /* data available status | irq 8 ack */
        /* XXX drop pic irq line here? */
        /* ldebug ("8 ack\n"); */
        retval = dsp->out_data_len ? 0x80 : 0;
        dsp->mixer_regs[0x82] &= ~dsp->mixer_regs[0x80];
        pic_set_irq (sb.irq, 0);
        break;

    case 0xf:                   /* irq 16 ack */
        /* XXX drop pic irq line here? */
        ldebug ("16 ack\n");
        retval = 0xff;
        dsp->mixer_regs[0x82] &= ~dsp->mixer_regs[0x80];
        pic_set_irq (sb.irq, 0);
        break;

    default:
        goto error;
    }

    if (0xe == iport) {
        if (0 == retval) {
            if (!dsp->nzero) {
                ldebug ("dsp_read (nport=%#x iport %#x) = %#x, %lld\n",
                        nport, iport, retval, dsp->nzero);
            }
            dsp->nzero += 1;
        }
        else {
            ldebug ("dsp_read (nport=%#x iport %#x) = %#x, %lld\n",
                    nport, iport, retval, dsp->nzero);
            dsp->nzero = 0;
        }
    }
    else {
        ldebug ("dsp_read (nport=%#x iport %#x) = %#x\n",
                nport, iport, retval);
    }

    return retval;

 error:
    printf ("dsp_read error %#x\n", nport);
    return 0xff;
}

static IO_WRITE_PROTO(mixer_write_indexb)
{
    SB16State *dsp = opaque;
    dsp->mixer_nreg = val;
}

static IO_WRITE_PROTO(mixer_write_datab)
{
    SB16State *dsp = opaque;
    int i;

    linfo ("mixer [%#x] <- %#x\n", dsp->mixer_nreg, val);
    switch (dsp->mixer_nreg) {
    case 0x00:
        /* Bochs */
        dsp->mixer_regs[0x04] = 0xcc;
        dsp->mixer_regs[0x0a] = 0x00;
        dsp->mixer_regs[0x22] = 0xcc;
        dsp->mixer_regs[0x26] = 0xcc;
        dsp->mixer_regs[0x28] = 0x00;
        dsp->mixer_regs[0x2e] = 0x00;
        dsp->mixer_regs[0x3c] = 0x1f;
        dsp->mixer_regs[0x3d] = 0x15;
        dsp->mixer_regs[0x3e] = 0x0b;

        for (i = 0x30; i <= 0x35; i++)
            dsp->mixer_regs[i] = 0xc0;

        for (i = 0x36; i <= 0x3b; i++)
            dsp->mixer_regs[i] = 0x00;

        for (i = 0x3f; i <= 0x43; i++)
            dsp->mixer_regs[i] = 0x00;

        for (i = 0x44; i <= 0x47; i++)
            dsp->mixer_regs[i] = 0x80;

        for (i = 0x30; i < 0x48; i++) {
            dsp->mixer_regs[i] = 0x20;
        }
        break;

    case 0x04:
    case 0x0a:
    case 0x22:
    case 0x26:
    case 0x28:
    case 0x2e:
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
    case 0x3a:
    case 0x3b:
    case 0x3c:
    case 0x3d:
    case 0x3e:
    case 0x3f:
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x45:
    case 0x46:
    case 0x47:
    case 0x80:
    case 0x81:
        break;
    default:
        return;
    }
    dsp->mixer_regs[dsp->mixer_nreg] = val;
}

static IO_WRITE_PROTO(mpu_write)
{
    linfo ("mpu: %#x\n", val);
}

static IO_WRITE_PROTO(adlib_write)
{
    linfo ("adlib: %#x\n", val);
}

static IO_READ_PROTO(mpu_read)
{
    linfo ("mpu read: %#x\n", nport);
    return 0x80;
}

static IO_READ_PROTO(adlib_read)
{
    linfo ("adlib read: %#x\n", nport);
    return 0;
}

static IO_WRITE_PROTO(mixer_write_indexw)
{
    mixer_write_indexb (opaque, nport, val & 0xff);
    mixer_write_datab (opaque, nport, (val >> 8) & 0xff);
}

static IO_READ_PROTO(mixer_read)
{
    SB16State *dsp = opaque;
    linfo ("mixer [%#x] -> %#x\n", dsp->mixer_nreg, dsp->mixer_regs[dsp->mixer_nreg]);
    return dsp->mixer_regs[dsp->mixer_nreg];
}

void SB16_run (void)
{
    if (0 == dsp.speaker)
        return;

    AUD_run ();
}

static int write_audio (uint32_t addr, int len, int size)
{
    int temp, net;
    uint8_t tmpbuf[4096];

    temp = size;

    net = 0;

    while (temp) {
        int left_till_end;
        int to_copy;
        int copied;

        left_till_end = len - dsp.dma_pos;

        to_copy = MIN (temp, left_till_end);
        if (to_copy > sizeof(tmpbuf))
            to_copy = sizeof(tmpbuf);
        cpu_physical_memory_read(addr + dsp.dma_pos, tmpbuf, to_copy);
        copied = AUD_write (tmpbuf, to_copy);

        temp -= copied;
        dsp.dma_pos += copied;

        if (dsp.dma_pos == len) {
            dsp.dma_pos = 0;
        }

        net += copied;

        if (copied != to_copy)
            return net;
    }

    return net;
}

static int SB_read_DMA (void *opaque, target_ulong addr, int size)
{
    SB16State *dsp = opaque;
    int free, till, copy, written;

    if (0 == dsp->speaker)
        return 0;

    if (dsp->left_till_irq < 0) {
	ldebug ("left_till_irq < 0, %d, pos %d \n",
                dsp->left_till_irq, dsp->dma_buffer_size);
        dsp->left_till_irq += dsp->dma_buffer_size;
        return dsp->dma_pos;
    }

    free = AUD_get_free ();

    if ((free <= 0) || (0 == size)) {
        ldebug ("returning, since free = %d and size = %d\n", free, size);
        return dsp->dma_pos;
    }

    if (mix_block > 0) {
        copy = MIN (free, mix_block);
    }
    else {
        copy = free;
    }

    till = dsp->left_till_irq;

#ifdef DEBUG_SB16_MOST
    ldebug ("addr:%#010x free:%d till:%d size:%d\n",
            addr, free, till, size);
#endif

    if (till <= copy) {
        if (0 == dsp->dma_auto) {
            copy = till;
        }
    }

    written = write_audio (addr, size, copy);
    dsp->left_till_irq -= written;
    AUD_adjust_estimate (free - written);

    if (dsp->left_till_irq <= 0) {
        dsp->mixer_regs[0x82] |= dsp->mixer_regs[0x80];
        if (0 == noirq) {
            ldebug ("request irq pos %d, left %d\n",
                    dsp->dma_pos, dsp->left_till_irq);
            pic_set_irq(sb.irq, 1);
        }

        if (0 == dsp->dma_auto) {
            control (0);
        }
    }

#ifdef DEBUG_SB16_MOST
    ldebug ("pos %5d free %5d size %5d till % 5d copy %5d dma size %5d\n",
            dsp->dma_pos, free, size, dsp->left_till_irq, copy,
            dsp->dma_buffer_size);
#endif

    if (dsp->left_till_irq <= 0) {
        dsp->left_till_irq += dsp->dma_buffer_size;
    }

    return dsp->dma_pos;
}

static int magic_of_irq (int irq)
{
    switch (irq) {
    case 2:
        return 1;
    case 5:
        return 2;
    case 7:
        return 4;
    case 10:
        return 8;
    default:
        dolog ("bad irq %d\n", irq);
        return 2;
    }
}

#if 0
static int irq_of_magic (int magic)
{
    switch (magic) {
    case 1:
        return 2;
    case 2:
        return 5;
    case 4:
        return 7;
    case 8:
        return 10;
    default:
        dolog ("bad irq magic %d\n", magic);
        return 2;
    }
}
#endif

#ifdef SB16_TRAP_ALL
static IO_READ_PROTO (trap_read)
{
    switch (nport) {
    case 0x220:
        return 0;
    case 0x226:
    case 0x22a:
    case 0x22c:
    case 0x22d:
    case 0x22e:
    case 0x22f:
        return dsp_read (opaque, nport);
    }
    linfo ("trap_read: %#x\n", nport);
    return 0xff;
}

static IO_WRITE_PROTO (trap_write)
{
    switch (nport) {
    case 0x226:
    case 0x22c:
        dsp_write (opaque, nport, val);
        return;
    }
    linfo ("trap_write: %#x = %#x\n", nport, val);
}
#endif

void SB16_init (void)
{
    SB16State *s = &dsp;
    int i;
    static const uint8_t dsp_write_ports[] = {0x6, 0xc};
    static const uint8_t dsp_read_ports[] = {0x6, 0xa, 0xc, 0xd, 0xe, 0xf};

    memset(s->mixer_regs, 0xff, sizeof(s->mixer_regs));

    s->mixer_regs[0x00] = 0;
    s->mixer_regs[0x0e] = ~0;
    s->mixer_regs[0x80] = magic_of_irq (sb.irq);
    s->mixer_regs[0x81] = 0x80 | 0x10 | (sb.dma << 1);
    s->mixer_regs[0x82] = 0;
    s->mixer_regs[0xfd] = 16;   /* bochs */
    s->mixer_regs[0xfe] = 6;    /* bochs */
    mixer_write_indexw (s, 0x224, 0);

#ifdef SB16_TRAP_ALL
    for (i = 0; i < 0x100; i++) {
        if (i != 4 && i != 5) {
            register_ioport_write (sb.port + i, 1, 1, trap_write, s);
            register_ioport_read (sb.port + i, 1, 1, trap_read, s);
        }
    }
#else

    for (i = 0; i < LENOFA (dsp_write_ports); i++) {
        register_ioport_write (sb.port + dsp_write_ports[i], 1, 1, dsp_write, s);
    }

    for (i = 0; i < LENOFA (dsp_read_ports); i++) {
        register_ioport_read (sb.port + dsp_read_ports[i], 1, 1, dsp_read, s);
    }
#endif

    register_ioport_write (sb.port + 0x4, 1, 1, mixer_write_indexb, s);
    register_ioport_write (sb.port + 0x4, 1, 2, mixer_write_indexw, s);
    register_ioport_read (sb.port + 0x5, 1, 1, mixer_read, s);
    register_ioport_write (sb.port + 0x5, 1, 1, mixer_write_datab, s);

    for (i = 0; 4 < 4; i++) {
        register_ioport_read (0x330 + i, 1, 1, mpu_read, s);
        register_ioport_write (0x330 + i, 1, 1, mpu_write, s);
        register_ioport_read (0x388 + i, 1, 1, adlib_read, s);
        register_ioport_write (0x388 + i, 1, 1, adlib_write, s);
    }

    DMA_register_channel (sb.hdma, SB_read_DMA, s);
    DMA_register_channel (sb.dma, SB_read_DMA, s);
}
