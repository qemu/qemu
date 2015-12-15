/*
 * QEMU PowerMac CUDA device support
 *
 * Copyright (c) 2004-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
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
#include "hw/ppc/mac.h"
#include "hw/input/adb.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "qemu/log.h"

/* XXX: implement all timer modes */

/* debug CUDA */
//#define DEBUG_CUDA

/* debug CUDA packets */
//#define DEBUG_CUDA_PACKET

#ifdef DEBUG_CUDA
#define CUDA_DPRINTF(fmt, ...)                                  \
    do { printf("CUDA: " fmt , ## __VA_ARGS__); } while (0)
#else
#define CUDA_DPRINTF(fmt, ...)
#endif

/* Bits in B data register: all active low */
#define TREQ		0x08		/* Transfer request (input) */
#define TACK		0x10		/* Transfer acknowledge (output) */
#define TIP		0x20		/* Transfer in progress (output) */

/* Bits in ACR */
#define SR_CTRL		0x1c		/* Shift register control bits */
#define SR_EXT		0x0c		/* Shift on external clock */
#define SR_OUT		0x10		/* Shift out if 1 */

/* Bits in IFR and IER */
#define IER_SET		0x80		/* set bits in IER */
#define IER_CLR		0		/* clear bits in IER */
#define SR_INT		0x04		/* Shift register full/empty */
#define SR_DATA_INT	0x08
#define SR_CLOCK_INT	0x10
#define T1_INT          0x40            /* Timer 1 interrupt */
#define T2_INT          0x20            /* Timer 2 interrupt */

/* Bits in ACR */
#define T1MODE          0xc0            /* Timer 1 mode */
#define T1MODE_CONT     0x40            /*  continuous interrupts */

/* commands (1st byte) */
#define ADB_PACKET	0
#define CUDA_PACKET	1
#define ERROR_PACKET	2
#define TIMER_PACKET	3
#define POWER_PACKET	4
#define MACIIC_PACKET	5
#define PMU_PACKET	6


/* CUDA commands (2nd byte) */
#define CUDA_WARM_START			0x0
#define CUDA_AUTOPOLL			0x1
#define CUDA_GET_6805_ADDR		0x2
#define CUDA_GET_TIME			0x3
#define CUDA_GET_PRAM			0x7
#define CUDA_SET_6805_ADDR		0x8
#define CUDA_SET_TIME			0x9
#define CUDA_POWERDOWN			0xa
#define CUDA_POWERUP_TIME		0xb
#define CUDA_SET_PRAM			0xc
#define CUDA_MS_RESET			0xd
#define CUDA_SEND_DFAC			0xe
#define CUDA_BATTERY_SWAP_SENSE		0x10
#define CUDA_RESET_SYSTEM		0x11
#define CUDA_SET_IPL			0x12
#define CUDA_FILE_SERVER_FLAG		0x13
#define CUDA_SET_AUTO_RATE		0x14
#define CUDA_GET_AUTO_RATE		0x16
#define CUDA_SET_DEVICE_LIST		0x19
#define CUDA_GET_DEVICE_LIST		0x1a
#define CUDA_SET_ONE_SECOND_MODE	0x1b
#define CUDA_SET_POWER_MESSAGES		0x21
#define CUDA_GET_SET_IIC		0x22
#define CUDA_WAKEUP			0x23
#define CUDA_TIMER_TICKLE		0x24
#define CUDA_COMBINED_FORMAT_IIC	0x25

#define CUDA_TIMER_FREQ (4700000 / 6)

/* CUDA returns time_t's offset from Jan 1, 1904, not 1970 */
#define RTC_OFFSET                      2082844800

/* CUDA registers */
#define CUDA_REG_B       0x00
#define CUDA_REG_A       0x01
#define CUDA_REG_DIRB    0x02
#define CUDA_REG_DIRA    0x03
#define CUDA_REG_T1CL    0x04
#define CUDA_REG_T1CH    0x05
#define CUDA_REG_T1LL    0x06
#define CUDA_REG_T1LH    0x07
#define CUDA_REG_T2CL    0x08
#define CUDA_REG_T2CH    0x09
#define CUDA_REG_SR      0x0a
#define CUDA_REG_ACR     0x0b
#define CUDA_REG_PCR     0x0c
#define CUDA_REG_IFR     0x0d
#define CUDA_REG_IER     0x0e
#define CUDA_REG_ANH     0x0f

static void cuda_update(CUDAState *s);
static void cuda_receive_packet_from_host(CUDAState *s,
                                          const uint8_t *data, int len);
static void cuda_timer_update(CUDAState *s, CUDATimer *ti,
                              int64_t current_time);

static void cuda_update_irq(CUDAState *s)
{
    if (s->ifr & s->ier & (SR_INT | T1_INT | T2_INT)) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static uint64_t get_tb(uint64_t time, uint64_t freq)
{
    return muldiv64(time, freq, NANOSECONDS_PER_SECOND);
}

static unsigned int get_counter(CUDATimer *ti)
{
    int64_t d;
    unsigned int counter;
    uint64_t tb_diff;
    uint64_t current_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    /* Reverse of the tb calculation algorithm that Mac OS X uses on bootup. */
    tb_diff = get_tb(current_time, ti->frequency) - ti->load_time;
    d = (tb_diff * 0xBF401675E5DULL) / (ti->frequency << 24);

    if (ti->index == 0) {
        /* the timer goes down from latch to -1 (period of latch + 2) */
        if (d <= (ti->counter_value + 1)) {
            counter = (ti->counter_value - d) & 0xffff;
        } else {
            counter = (d - (ti->counter_value + 1)) % (ti->latch + 2);
            counter = (ti->latch - counter) & 0xffff;
        }
    } else {
        counter = (ti->counter_value - d) & 0xffff;
    }
    return counter;
}

static void set_counter(CUDAState *s, CUDATimer *ti, unsigned int val)
{
    CUDA_DPRINTF("T%d.counter=%d\n", 1 + ti->index, val);
    ti->load_time = get_tb(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                           s->frequency);
    ti->counter_value = val;
    cuda_timer_update(s, ti, ti->load_time);
}

static int64_t get_next_irq_time(CUDATimer *s, int64_t current_time)
{
    int64_t d, next_time;
    unsigned int counter;

    /* current counter value */
    d = muldiv64(current_time - s->load_time,
                 CUDA_TIMER_FREQ, NANOSECONDS_PER_SECOND);
    /* the timer goes down from latch to -1 (period of latch + 2) */
    if (d <= (s->counter_value + 1)) {
        counter = (s->counter_value - d) & 0xffff;
    } else {
        counter = (d - (s->counter_value + 1)) % (s->latch + 2);
        counter = (s->latch - counter) & 0xffff;
    }

    /* Note: we consider the irq is raised on 0 */
    if (counter == 0xffff) {
        next_time = d + s->latch + 1;
    } else if (counter == 0) {
        next_time = d + s->latch + 2;
    } else {
        next_time = d + counter;
    }
    CUDA_DPRINTF("latch=%d counter=%" PRId64 " delta_next=%" PRId64 "\n",
                 s->latch, d, next_time - d);
    next_time = muldiv64(next_time, NANOSECONDS_PER_SECOND, CUDA_TIMER_FREQ) +
        s->load_time;
    if (next_time <= current_time)
        next_time = current_time + 1;
    return next_time;
}

static void cuda_timer_update(CUDAState *s, CUDATimer *ti,
                              int64_t current_time)
{
    if (!ti->timer)
        return;
    if (ti->index == 0 && (s->acr & T1MODE) != T1MODE_CONT) {
        timer_del(ti->timer);
    } else {
        ti->next_irq_time = get_next_irq_time(ti, current_time);
        timer_mod(ti->timer, ti->next_irq_time);
    }
}

static void cuda_timer1(void *opaque)
{
    CUDAState *s = opaque;
    CUDATimer *ti = &s->timers[0];

    cuda_timer_update(s, ti, ti->next_irq_time);
    s->ifr |= T1_INT;
    cuda_update_irq(s);
}

static void cuda_timer2(void *opaque)
{
    CUDAState *s = opaque;
    CUDATimer *ti = &s->timers[1];

    cuda_timer_update(s, ti, ti->next_irq_time);
    s->ifr |= T2_INT;
    cuda_update_irq(s);
}

static void cuda_set_sr_int(void *opaque)
{
    CUDAState *s = opaque;

    CUDA_DPRINTF("CUDA: %s:%d\n", __func__, __LINE__);
    s->ifr |= SR_INT;
    cuda_update_irq(s);
}

static void cuda_delay_set_sr_int(CUDAState *s)
{
    int64_t expire;

    if (s->dirb == 0xff) {
        /* Not in Mac OS, fire the IRQ directly */
        cuda_set_sr_int(s);
        return;
    }

    CUDA_DPRINTF("CUDA: %s:%d\n", __func__, __LINE__);

    expire = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 300 * SCALE_US;
    timer_mod(s->sr_delay_timer, expire);
}

static uint32_t cuda_readb(void *opaque, hwaddr addr)
{
    CUDAState *s = opaque;
    uint32_t val;

    addr = (addr >> 9) & 0xf;
    switch(addr) {
    case CUDA_REG_B:
        val = s->b;
        break;
    case CUDA_REG_A:
        val = s->a;
        break;
    case CUDA_REG_DIRB:
        val = s->dirb;
        break;
    case CUDA_REG_DIRA:
        val = s->dira;
        break;
    case CUDA_REG_T1CL:
        val = get_counter(&s->timers[0]) & 0xff;
        s->ifr &= ~T1_INT;
        cuda_update_irq(s);
        break;
    case CUDA_REG_T1CH:
        val = get_counter(&s->timers[0]) >> 8;
        cuda_update_irq(s);
        break;
    case CUDA_REG_T1LL:
        val = s->timers[0].latch & 0xff;
        break;
    case CUDA_REG_T1LH:
        /* XXX: check this */
        val = (s->timers[0].latch >> 8) & 0xff;
        break;
    case CUDA_REG_T2CL:
        val = get_counter(&s->timers[1]) & 0xff;
        s->ifr &= ~T2_INT;
        cuda_update_irq(s);
        break;
    case CUDA_REG_T2CH:
        val = get_counter(&s->timers[1]) >> 8;
        break;
    case CUDA_REG_SR:
        val = s->sr;
        s->ifr &= ~(SR_INT | SR_CLOCK_INT | SR_DATA_INT);
        cuda_update_irq(s);
        break;
    case CUDA_REG_ACR:
        val = s->acr;
        break;
    case CUDA_REG_PCR:
        val = s->pcr;
        break;
    case CUDA_REG_IFR:
        val = s->ifr;
        if (s->ifr & s->ier) {
            val |= 0x80;
        }
        break;
    case CUDA_REG_IER:
        val = s->ier | 0x80;
        break;
    default:
    case CUDA_REG_ANH:
        val = s->anh;
        break;
    }
    if (addr != CUDA_REG_IFR || val != 0) {
        CUDA_DPRINTF("read: reg=0x%x val=%02x\n", (int)addr, val);
    }

    return val;
}

static void cuda_writeb(void *opaque, hwaddr addr, uint32_t val)
{
    CUDAState *s = opaque;

    addr = (addr >> 9) & 0xf;
    CUDA_DPRINTF("write: reg=0x%x val=%02x\n", (int)addr, val);

    switch(addr) {
    case CUDA_REG_B:
        s->b = val;
        cuda_update(s);
        break;
    case CUDA_REG_A:
        s->a = val;
        break;
    case CUDA_REG_DIRB:
        s->dirb = val;
        break;
    case CUDA_REG_DIRA:
        s->dira = val;
        break;
    case CUDA_REG_T1CL:
        s->timers[0].latch = (s->timers[0].latch & 0xff00) | val;
        cuda_timer_update(s, &s->timers[0], qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        break;
    case CUDA_REG_T1CH:
        s->timers[0].latch = (s->timers[0].latch & 0xff) | (val << 8);
        s->ifr &= ~T1_INT;
        set_counter(s, &s->timers[0], s->timers[0].latch);
        break;
    case CUDA_REG_T1LL:
        s->timers[0].latch = (s->timers[0].latch & 0xff00) | val;
        cuda_timer_update(s, &s->timers[0], qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        break;
    case CUDA_REG_T1LH:
        s->timers[0].latch = (s->timers[0].latch & 0xff) | (val << 8);
        s->ifr &= ~T1_INT;
        cuda_timer_update(s, &s->timers[0], qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        break;
    case CUDA_REG_T2CL:
        s->timers[1].latch = (s->timers[1].latch & 0xff00) | val;
        break;
    case CUDA_REG_T2CH:
        /* To ensure T2 generates an interrupt on zero crossing with the
           common timer code, write the value directly from the latch to
           the counter */
        s->timers[1].latch = (s->timers[1].latch & 0xff) | (val << 8);
        s->ifr &= ~T2_INT;
        set_counter(s, &s->timers[1], s->timers[1].latch);
        break;
    case CUDA_REG_SR:
        s->sr = val;
        break;
    case CUDA_REG_ACR:
        s->acr = val;
        cuda_timer_update(s, &s->timers[0], qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        cuda_update(s);
        break;
    case CUDA_REG_PCR:
        s->pcr = val;
        break;
    case CUDA_REG_IFR:
        /* reset bits */
        s->ifr &= ~val;
        cuda_update_irq(s);
        break;
    case CUDA_REG_IER:
        if (val & IER_SET) {
            /* set bits */
            s->ier |= val & 0x7f;
        } else {
            /* reset bits */
            s->ier &= ~val;
        }
        cuda_update_irq(s);
        break;
    default:
    case CUDA_REG_ANH:
        s->anh = val;
        break;
    }
}

/* NOTE: TIP and TREQ are negated */
static void cuda_update(CUDAState *s)
{
    int packet_received, len;

    packet_received = 0;
    if (!(s->b & TIP)) {
        /* transfer requested from host */

        if (s->acr & SR_OUT) {
            /* data output */
            if ((s->b & (TACK | TIP)) != (s->last_b & (TACK | TIP))) {
                if (s->data_out_index < sizeof(s->data_out)) {
                    CUDA_DPRINTF("send: %02x\n", s->sr);
                    s->data_out[s->data_out_index++] = s->sr;
                    cuda_delay_set_sr_int(s);
                }
            }
        } else {
            if (s->data_in_index < s->data_in_size) {
                /* data input */
                if ((s->b & (TACK | TIP)) != (s->last_b & (TACK | TIP))) {
                    s->sr = s->data_in[s->data_in_index++];
                    CUDA_DPRINTF("recv: %02x\n", s->sr);
                    /* indicate end of transfer */
                    if (s->data_in_index >= s->data_in_size) {
                        s->b = (s->b | TREQ);
                    }
                    cuda_delay_set_sr_int(s);
                }
            }
        }
    } else {
        /* no transfer requested: handle sync case */
        if ((s->last_b & TIP) && (s->b & TACK) != (s->last_b & TACK)) {
            /* update TREQ state each time TACK change state */
            if (s->b & TACK)
                s->b = (s->b | TREQ);
            else
                s->b = (s->b & ~TREQ);
            cuda_delay_set_sr_int(s);
        } else {
            if (!(s->last_b & TIP)) {
                /* handle end of host to cuda transfer */
                packet_received = (s->data_out_index > 0);
                /* always an IRQ at the end of transfer */
                cuda_delay_set_sr_int(s);
            }
            /* signal if there is data to read */
            if (s->data_in_index < s->data_in_size) {
                s->b = (s->b & ~TREQ);
            }
        }
    }

    s->last_acr = s->acr;
    s->last_b = s->b;

    /* NOTE: cuda_receive_packet_from_host() can call cuda_update()
       recursively */
    if (packet_received) {
        len = s->data_out_index;
        s->data_out_index = 0;
        cuda_receive_packet_from_host(s, s->data_out, len);
    }
}

static void cuda_send_packet_to_host(CUDAState *s,
                                     const uint8_t *data, int len)
{
#ifdef DEBUG_CUDA_PACKET
    {
        int i;
        printf("cuda_send_packet_to_host:\n");
        for(i = 0; i < len; i++)
            printf(" %02x", data[i]);
        printf("\n");
    }
#endif
    memcpy(s->data_in, data, len);
    s->data_in_size = len;
    s->data_in_index = 0;
    cuda_update(s);
    cuda_delay_set_sr_int(s);
}

static void cuda_adb_poll(void *opaque)
{
    CUDAState *s = opaque;
    uint8_t obuf[ADB_MAX_OUT_LEN + 2];
    int olen;

    olen = adb_poll(&s->adb_bus, obuf + 2, s->adb_poll_mask);
    if (olen > 0) {
        obuf[0] = ADB_PACKET;
        obuf[1] = 0x40; /* polled data */
        cuda_send_packet_to_host(s, obuf, olen + 2);
    }
    timer_mod(s->adb_poll_timer,
                   qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                   (NANOSECONDS_PER_SECOND / (1000 / s->autopoll_rate_ms)));
}

/* description of commands */
typedef struct CudaCommand {
    uint8_t command;
    const char *name;
    bool (*handler)(CUDAState *s,
                    const uint8_t *in_args, int in_len,
                    uint8_t *out_args, int *out_len);
} CudaCommand;

static bool cuda_cmd_autopoll(CUDAState *s,
                              const uint8_t *in_data, int in_len,
                              uint8_t *out_data, int *out_len)
{
    int autopoll;

    if (in_len != 1) {
        return false;
    }

    autopoll = (in_data[0] != 0);
    if (autopoll != s->autopoll) {
        s->autopoll = autopoll;
        if (autopoll) {
            timer_mod(s->adb_poll_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                      (NANOSECONDS_PER_SECOND / (1000 / s->autopoll_rate_ms)));
        } else {
            timer_del(s->adb_poll_timer);
        }
    }
    return true;
}

static bool cuda_cmd_set_autorate(CUDAState *s,
                                  const uint8_t *in_data, int in_len,
                                  uint8_t *out_data, int *out_len)
{
    if (in_len != 1) {
        return false;
    }

    /* we don't want a period of 0 ms */
    /* FIXME: check what real hardware does */
    if (in_data[0] == 0) {
        return false;
    }

    s->autopoll_rate_ms = in_data[0];
    if (s->autopoll) {
        timer_mod(s->adb_poll_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  (NANOSECONDS_PER_SECOND / (1000 / s->autopoll_rate_ms)));
    }
    return true;
}

static bool cuda_cmd_set_device_list(CUDAState *s,
                                     const uint8_t *in_data, int in_len,
                                     uint8_t *out_data, int *out_len)
{
    if (in_len != 2) {
        return false;
    }

    s->adb_poll_mask = (((uint16_t)in_data[0]) << 8) | in_data[1];
    return true;
}

static bool cuda_cmd_powerdown(CUDAState *s,
                               const uint8_t *in_data, int in_len,
                               uint8_t *out_data, int *out_len)
{
    if (in_len != 0) {
        return false;
    }

    qemu_system_shutdown_request();
    return true;
}

static bool cuda_cmd_reset_system(CUDAState *s,
                                  const uint8_t *in_data, int in_len,
                                  uint8_t *out_data, int *out_len)
{
    if (in_len != 0) {
        return false;
    }

    qemu_system_reset_request();
    return true;
}

static bool cuda_cmd_set_file_server_flag(CUDAState *s,
                                          const uint8_t *in_data, int in_len,
                                          uint8_t *out_data, int *out_len)
{
    if (in_len != 1) {
        return false;
    }

    qemu_log_mask(LOG_UNIMP,
                  "CUDA: unimplemented command FILE_SERVER_FLAG %d\n",
                  in_data[0]);
    return true;
}

static bool cuda_cmd_set_power_message(CUDAState *s,
                                       const uint8_t *in_data, int in_len,
                                       uint8_t *out_data, int *out_len)
{
    if (in_len != 1) {
        return false;
    }

    qemu_log_mask(LOG_UNIMP,
                  "CUDA: unimplemented command SET_POWER_MESSAGE %d\n",
                  in_data[0]);
    return true;
}

static bool cuda_cmd_get_time(CUDAState *s,
                              const uint8_t *in_data, int in_len,
                              uint8_t *out_data, int *out_len)
{
    uint32_t ti;

    if (in_len != 0) {
        return false;
    }

    ti = s->tick_offset + (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                           / NANOSECONDS_PER_SECOND);
    out_data[0] = ti >> 24;
    out_data[1] = ti >> 16;
    out_data[2] = ti >> 8;
    out_data[3] = ti;
    *out_len = 4;
    return true;
}

static bool cuda_cmd_set_time(CUDAState *s,
                              const uint8_t *in_data, int in_len,
                              uint8_t *out_data, int *out_len)
{
    uint32_t ti;

    if (in_len != 4) {
        return false;
    }

    ti = (((uint32_t)in_data[0]) << 24) + (((uint32_t)in_data[1]) << 16)
         + (((uint32_t)in_data[2]) << 8) + in_data[3];
    s->tick_offset = ti - (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                           / NANOSECONDS_PER_SECOND);
    return true;
}

static const CudaCommand handlers[] = {
    { CUDA_AUTOPOLL, "AUTOPOLL", cuda_cmd_autopoll },
    { CUDA_SET_AUTO_RATE, "SET_AUTO_RATE",  cuda_cmd_set_autorate },
    { CUDA_SET_DEVICE_LIST, "SET_DEVICE_LIST", cuda_cmd_set_device_list },
    { CUDA_POWERDOWN, "POWERDOWN", cuda_cmd_powerdown },
    { CUDA_RESET_SYSTEM, "RESET_SYSTEM", cuda_cmd_reset_system },
    { CUDA_FILE_SERVER_FLAG, "FILE_SERVER_FLAG",
      cuda_cmd_set_file_server_flag },
    { CUDA_SET_POWER_MESSAGES, "SET_POWER_MESSAGES",
      cuda_cmd_set_power_message },
    { CUDA_GET_TIME, "GET_TIME", cuda_cmd_get_time },
    { CUDA_SET_TIME, "SET_TIME", cuda_cmd_set_time },
};

static void cuda_receive_packet(CUDAState *s,
                                const uint8_t *data, int len)
{
    uint8_t obuf[16] = { CUDA_PACKET, 0, data[0] };
    int i, out_len = 0;

    for (i = 0; i < ARRAY_SIZE(handlers); i++) {
        const CudaCommand *desc = &handlers[i];
        if (desc->command == data[0]) {
            CUDA_DPRINTF("handling command %s\n", desc->name);
            out_len = 0;
            if (desc->handler(s, data + 1, len - 1, obuf + 3, &out_len)) {
                cuda_send_packet_to_host(s, obuf, 3 + out_len);
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "CUDA: %s: wrong parameters %d\n",
                              desc->name, len);
                obuf[0] = ERROR_PACKET;
                obuf[1] = 0x5; /* bad parameters */
                obuf[2] = CUDA_PACKET;
                obuf[3] = data[0];
                cuda_send_packet_to_host(s, obuf, 4);
            }
            return;
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR, "CUDA: unknown command 0x%02x\n", data[0]);
    obuf[0] = ERROR_PACKET;
    obuf[1] = 0x2; /* unknown command */
    obuf[2] = CUDA_PACKET;
    obuf[3] = data[0];
    cuda_send_packet_to_host(s, obuf, 4);
}

static void cuda_receive_packet_from_host(CUDAState *s,
                                          const uint8_t *data, int len)
{
#ifdef DEBUG_CUDA_PACKET
    {
        int i;
        printf("cuda_receive_packet_from_host:\n");
        for(i = 0; i < len; i++)
            printf(" %02x", data[i]);
        printf("\n");
    }
#endif
    switch(data[0]) {
    case ADB_PACKET:
        {
            uint8_t obuf[ADB_MAX_OUT_LEN + 3];
            int olen;
            olen = adb_request(&s->adb_bus, obuf + 2, data + 1, len - 1);
            if (olen > 0) {
                obuf[0] = ADB_PACKET;
                obuf[1] = 0x00;
                cuda_send_packet_to_host(s, obuf, olen + 2);
            } else {
                /* error */
                obuf[0] = ADB_PACKET;
                obuf[1] = -olen;
                obuf[2] = data[1];
                olen = 0;
                cuda_send_packet_to_host(s, obuf, olen + 3);
            }
        }
        break;
    case CUDA_PACKET:
        cuda_receive_packet(s, data + 1, len - 1);
        break;
    }
}

static void cuda_writew (void *opaque, hwaddr addr, uint32_t value)
{
}

static void cuda_writel (void *opaque, hwaddr addr, uint32_t value)
{
}

static uint32_t cuda_readw (void *opaque, hwaddr addr)
{
    return 0;
}

static uint32_t cuda_readl (void *opaque, hwaddr addr)
{
    return 0;
}

static const MemoryRegionOps cuda_ops = {
    .old_mmio = {
        .write = {
            cuda_writeb,
            cuda_writew,
            cuda_writel,
        },
        .read = {
            cuda_readb,
            cuda_readw,
            cuda_readl,
        },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static bool cuda_timer_exist(void *opaque, int version_id)
{
    CUDATimer *s = opaque;

    return s->timer != NULL;
}

static const VMStateDescription vmstate_cuda_timer = {
    .name = "cuda_timer",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(latch, CUDATimer),
        VMSTATE_UINT16(counter_value, CUDATimer),
        VMSTATE_INT64(load_time, CUDATimer),
        VMSTATE_INT64(next_irq_time, CUDATimer),
        VMSTATE_TIMER_PTR_TEST(timer, CUDATimer, cuda_timer_exist),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_cuda = {
    .name = "cuda",
    .version_id = 4,
    .minimum_version_id = 4,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(a, CUDAState),
        VMSTATE_UINT8(b, CUDAState),
        VMSTATE_UINT8(last_b, CUDAState),
        VMSTATE_UINT8(dira, CUDAState),
        VMSTATE_UINT8(dirb, CUDAState),
        VMSTATE_UINT8(sr, CUDAState),
        VMSTATE_UINT8(acr, CUDAState),
        VMSTATE_UINT8(last_acr, CUDAState),
        VMSTATE_UINT8(pcr, CUDAState),
        VMSTATE_UINT8(ifr, CUDAState),
        VMSTATE_UINT8(ier, CUDAState),
        VMSTATE_UINT8(anh, CUDAState),
        VMSTATE_INT32(data_in_size, CUDAState),
        VMSTATE_INT32(data_in_index, CUDAState),
        VMSTATE_INT32(data_out_index, CUDAState),
        VMSTATE_UINT8(autopoll, CUDAState),
        VMSTATE_UINT8(autopoll_rate_ms, CUDAState),
        VMSTATE_UINT16(adb_poll_mask, CUDAState),
        VMSTATE_BUFFER(data_in, CUDAState),
        VMSTATE_BUFFER(data_out, CUDAState),
        VMSTATE_UINT32(tick_offset, CUDAState),
        VMSTATE_STRUCT_ARRAY(timers, CUDAState, 2, 1,
                             vmstate_cuda_timer, CUDATimer),
        VMSTATE_TIMER_PTR(adb_poll_timer, CUDAState),
        VMSTATE_TIMER_PTR(sr_delay_timer, CUDAState),
        VMSTATE_END_OF_LIST()
    }
};

static void cuda_reset(DeviceState *dev)
{
    CUDAState *s = CUDA(dev);

    s->b = 0;
    s->a = 0;
    s->dirb = 0xff;
    s->dira = 0;
    s->sr = 0;
    s->acr = 0;
    s->pcr = 0;
    s->ifr = 0;
    s->ier = 0;
    //    s->ier = T1_INT | SR_INT;
    s->anh = 0;
    s->data_in_size = 0;
    s->data_in_index = 0;
    s->data_out_index = 0;
    s->autopoll = 0;

    s->timers[0].latch = 0xffff;
    set_counter(s, &s->timers[0], 0xffff);

    s->timers[1].latch = 0xffff;

    s->sr_delay_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, cuda_set_sr_int, s);
}

static void cuda_realizefn(DeviceState *dev, Error **errp)
{
    CUDAState *s = CUDA(dev);
    struct tm tm;

    s->timers[0].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, cuda_timer1, s);
    s->timers[0].frequency = s->frequency;
    s->timers[1].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, cuda_timer2, s);
    s->timers[1].frequency = (SCALE_US * 6000) / 4700;

    qemu_get_timedate(&tm, 0);
    s->tick_offset = (uint32_t)mktimegm(&tm) + RTC_OFFSET;

    s->adb_poll_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, cuda_adb_poll, s);
    s->autopoll_rate_ms = 20;
    s->adb_poll_mask = 0xffff;
}

static void cuda_initfn(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    CUDAState *s = CUDA(obj);
    int i;

    memory_region_init_io(&s->mem, obj, &cuda_ops, s, "cuda", 0x2000);
    sysbus_init_mmio(d, &s->mem);
    sysbus_init_irq(d, &s->irq);

    for (i = 0; i < ARRAY_SIZE(s->timers); i++) {
        s->timers[i].index = i;
    }

    qbus_create_inplace(&s->adb_bus, sizeof(s->adb_bus), TYPE_ADB_BUS,
                        DEVICE(obj), "adb.0");
}

static Property cuda_properties[] = {
    DEFINE_PROP_UINT64("frequency", CUDAState, frequency, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void cuda_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = cuda_realizefn;
    dc->reset = cuda_reset;
    dc->vmsd = &vmstate_cuda;
    dc->props = cuda_properties;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo cuda_type_info = {
    .name = TYPE_CUDA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CUDAState),
    .instance_init = cuda_initfn,
    .class_init = cuda_class_init,
};

static void cuda_register_types(void)
{
    type_register_static(&cuda_type_info);
}

type_init(cuda_register_types)
