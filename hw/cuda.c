/*
 * QEMU CUDA support
 * 
 * Copyright (c) 2004 Fabrice Bellard
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
#define T1_INT          0x40            /* Timer 1 interrupt */

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

typedef struct CUDATimer {
    unsigned int latch;
    uint16_t counter_value; /* counter value at load time */
    int64_t load_time;
    int64_t next_irq_time;
    QEMUTimer *timer;
} CUDATimer;

typedef struct CUDAState {
    /* cuda registers */
    uint8_t b;      /* B-side data */
    uint8_t a;      /* A-side data */
    uint8_t dirb;   /* B-side direction (1=output) */
    uint8_t dira;   /* A-side direction (1=output) */
    uint8_t sr;     /* Shift register */
    uint8_t acr;    /* Auxiliary control register */
    uint8_t pcr;    /* Peripheral control register */
    uint8_t ifr;    /* Interrupt flag register */
    uint8_t ier;    /* Interrupt enable register */
    uint8_t anh;    /* A-side data, no handshake */

    CUDATimer timers[2];
    
    uint8_t last_b; /* last value of B register */
    uint8_t last_acr; /* last value of B register */
    
    int data_in_size;
    int data_in_index;
    int data_out_index;

    int irq;
    uint8_t autopoll;
    uint8_t data_in[128];
    uint8_t data_out[16];
} CUDAState;

static CUDAState cuda_state;
ADBBusState adb_bus;

static void cuda_update(CUDAState *s);
static void cuda_receive_packet_from_host(CUDAState *s, 
                                          const uint8_t *data, int len);

static void cuda_update_irq(CUDAState *s)
{
    if (s->ifr & s->ier & SR_INT) {
        pic_set_irq(s->irq, 1);
    } else {
        pic_set_irq(s->irq, 0);
    }
}

static unsigned int get_counter(CUDATimer *s)
{
    int64_t d;
    unsigned int counter;

    d = muldiv64(qemu_get_clock(vm_clock) - s->load_time, 
                 CUDA_TIMER_FREQ, ticks_per_sec);
    if (d <= s->counter_value) {
        counter = d;
    } else {
        counter = s->latch - 1 - ((d - s->counter_value) % s->latch);
    }
    return counter;
}

static void set_counter(CUDATimer *s, unsigned int val)
{
    s->load_time = qemu_get_clock(vm_clock);
    s->counter_value = val;
}

static int64_t get_next_irq_time(CUDATimer *s, int64_t current_time)
{
    int64_t d, next_time, base;
    /* current counter value */
    d = muldiv64(current_time - s->load_time, 
                 CUDA_TIMER_FREQ, ticks_per_sec);
    if (d <= s->counter_value) {
        next_time = s->counter_value + 1;
    } else {
        base = ((d - s->counter_value) % s->latch);
        base = (base * s->latch) + s->counter_value;
        next_time = base + s->latch;
    }
    next_time = muldiv64(next_time, ticks_per_sec, CUDA_TIMER_FREQ) + 
        s->load_time;
    if (next_time <= current_time)
        next_time = current_time + 1;
    return next_time;
}

static void cuda_timer1(void *opaque)
{
    CUDAState *s = opaque;
    CUDATimer *ti = &s->timers[0];

    ti->next_irq_time = get_next_irq_time(ti, ti->next_irq_time);
    qemu_mod_timer(ti->timer, ti->next_irq_time);
    s->ifr |= T1_INT;
    cuda_update_irq(s);
}

static uint32_t cuda_readb(void *opaque, target_phys_addr_t addr)
{
    CUDAState *s = opaque;
    uint32_t val;

    addr = (addr >> 9) & 0xf;
    switch(addr) {
    case 0:
        val = s->b;
        break;
    case 1:
        val = s->a;
        break;
    case 2:
        val = s->dirb;
        break;
    case 3:
        val = s->dira;
        break;
    case 4:
        val = get_counter(&s->timers[0]) & 0xff;
        s->ifr &= ~T1_INT;
        cuda_update_irq(s);
        break;
    case 5:
        val = get_counter(&s->timers[0]) >> 8;
        s->ifr &= ~T1_INT;
        cuda_update_irq(s);
        break;
    case 6:
        val = s->timers[0].latch & 0xff;
        break;
    case 7:
        val = (s->timers[0].latch >> 8) & 0xff;
        break;
    case 8:
        val = get_counter(&s->timers[1]) & 0xff;
        break;
    case 9:
        val = get_counter(&s->timers[1]) >> 8;
        break;
    case 10:
        if (s->data_in_index < s->data_in_size) {
            val = s->data_in[s->data_in_index];
        } else {
            val = 0;
        }
        break;
    case 11:
        val = s->acr;
        break;
    case 12:
        val = s->pcr;
        break;
    case 13:
        val = s->ifr;
        break;
    case 14:
        val = s->ier;
        break;
    default:
    case 15:
        val = s->anh;
        break;
    }
#ifdef DEBUG_CUDA
    printf("cuda: read: reg=0x%x val=%02x\n", addr, val);
#endif
    return val;
}

static void cuda_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    CUDAState *s = opaque;
    
    addr = (addr >> 9) & 0xf;
#ifdef DEBUG_CUDA
    printf("cuda: write: reg=0x%x val=%02x\n", addr, val);
#endif

    switch(addr) {
    case 0:
        s->b = val;
        cuda_update(s);
        break;
    case 1:
        s->a = val;
        break;
    case 2:
        s->dirb = val;
        break;
    case 3:
        s->dira = val;
        break;
    case 4:
        val = val | (get_counter(&s->timers[0]) & 0xff00);
        set_counter(&s->timers[0], val);
        break;
    case 5:
        val = (val << 8) |  (get_counter(&s->timers[0]) & 0xff);
        set_counter(&s->timers[0], val);
        break;
    case 6:
        s->timers[0].latch = (s->timers[0].latch & 0xff00) | val;
        break;
    case 7:
        s->timers[0].latch = (s->timers[0].latch & 0xff) | (val << 8);
        break;
    case 8:
        val = val | (get_counter(&s->timers[1]) & 0xff00);
        set_counter(&s->timers[1], val);
        break;
    case 9:
        val = (val << 8) |  (get_counter(&s->timers[1]) & 0xff);
        set_counter(&s->timers[1], val);
        break;
    case 10:
        s->sr = val;
        break;
    case 11:
        s->acr = val;
        if ((s->acr & T1MODE) == T1MODE_CONT) {
            if ((s->last_acr & T1MODE) != T1MODE_CONT) {
                CUDATimer *ti = &s->timers[0];
                /* activate timer interrupt */
                ti->next_irq_time = get_next_irq_time(ti, qemu_get_clock(vm_clock));
                qemu_mod_timer(ti->timer, ti->next_irq_time);
            }
        } else {
            if ((s->last_acr & T1MODE) == T1MODE_CONT) {
                CUDATimer *ti = &s->timers[0];
                qemu_del_timer(ti->timer);
            }
        }
        cuda_update(s);
        break;
    case 12:
        s->pcr = val;
        break;
    case 13:
        /* reset bits */
        s->ifr &= ~val;
        cuda_update_irq(s);
        break;
    case 14:
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
    case 15:
        s->anh = val;
        break;
    }
}

/* NOTE: TIP and TREQ are negated */
static void cuda_update(CUDAState *s)
{
    if (s->data_in_index < s->data_in_size) {
        /* data input */
        if (!(s->b & TIP) && 
            (s->b & (TACK | TIP)) != (s->last_b & (TACK | TIP))) {
            s->sr = s->data_in[s->data_in_index++];
            s->ifr |= SR_INT;
            cuda_update_irq(s);
        }
    }
    if (s->data_in_index < s->data_in_size) {
        /* there is some data to read */
        s->b = (s->b & ~TREQ);
    } else {
        s->b = (s->b | TREQ);
    }

    if (s->acr & SR_OUT) {
        /* data output */
        if (!(s->b & TIP) && 
            (s->b & (TACK | TIP)) != (s->last_b & (TACK | TIP))) {
            if (s->data_out_index < sizeof(s->data_out)) {
                s->data_out[s->data_out_index++] = s->sr;
            }
            s->ifr |= SR_INT;
            cuda_update_irq(s);
        }
    }

    /* check end of data output */
    if (!(s->acr & SR_OUT) && (s->last_acr & SR_OUT)) {
        if (s->data_out_index > 0)
            cuda_receive_packet_from_host(s, s->data_out, s->data_out_index);
        s->data_out_index = 0;
    }
    s->last_acr = s->acr;
    s->last_b = s->b;
}

static void cuda_send_packet_to_host(CUDAState *s, 
                                     const uint8_t *data, int len)
{
    memcpy(s->data_in, data, len);
    s->data_in_size = len;
    s->data_in_index = 0;
    cuda_update(s);
    s->ifr |= SR_INT;
    cuda_update_irq(s);
}

void adb_send_packet(ADBBusState *bus, const uint8_t *buf, int len)
{
    CUDAState *s = &cuda_state;
    uint8_t data[16];

    memcpy(data + 1, buf, len);
    data[0] = ADB_PACKET;
    cuda_send_packet_to_host(s, data, len + 1);
}

static void cuda_receive_packet(CUDAState *s, 
                                const uint8_t *data, int len)
{
    uint8_t obuf[16];
    int ti;

    switch(data[0]) {
    case CUDA_AUTOPOLL:
        s->autopoll = data[1];
        obuf[0] = CUDA_PACKET;
        obuf[1] = data[1];
        cuda_send_packet_to_host(s, obuf, 2);
        break;
    case CUDA_GET_TIME:
        /* XXX: add time support ? */
        ti = 0;
        obuf[0] = CUDA_PACKET;
        obuf[1] = 0;
        obuf[2] = 0;
        obuf[3] = ti >> 24;
        obuf[4] = ti >> 16;
        obuf[5] = ti >> 8;
        obuf[6] = ti;
        cuda_send_packet_to_host(s, obuf, 7);
        break;
    case CUDA_SET_TIME:
    case CUDA_FILE_SERVER_FLAG:
    case CUDA_SET_DEVICE_LIST:
    case CUDA_SET_AUTO_RATE:
    case CUDA_SET_POWER_MESSAGES:
        obuf[0] = CUDA_PACKET;
        obuf[1] = 0;
        cuda_send_packet_to_host(s, obuf, 2);
        break;
    default:
        break;
    }
}

static void cuda_receive_packet_from_host(CUDAState *s, 
                                          const uint8_t *data, int len)
{
    switch(data[0]) {
    case ADB_PACKET:
        adb_receive_packet(&adb_bus, data + 1, len - 1);
        break;
    case CUDA_PACKET:
        cuda_receive_packet(s, data + 1, len - 1);
        break;
    }
}

static void cuda_writew (void *opaque, target_phys_addr_t addr, uint32_t value)
{
}

static void cuda_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
}

static uint32_t cuda_readw (void *opaque, target_phys_addr_t addr)
{
    return 0;
}

static uint32_t cuda_readl (void *opaque, target_phys_addr_t addr)
{
    return 0;
}

static CPUWriteMemoryFunc *cuda_write[] = {
    &cuda_writeb,
    &cuda_writew,
    &cuda_writel,
};

static CPUReadMemoryFunc *cuda_read[] = {
    &cuda_readb,
    &cuda_readw,
    &cuda_readl,
};

int cuda_init(void)
{
    CUDAState *s = &cuda_state;
    int cuda_mem_index;

    s->timers[0].latch = 0x10000;
    set_counter(&s->timers[0], 0xffff);
    s->timers[0].timer = qemu_new_timer(vm_clock, cuda_timer1, s);
    s->timers[1].latch = 0x10000;
    set_counter(&s->timers[1], 0xffff);
    cuda_mem_index = cpu_register_io_memory(0, cuda_read, cuda_write, s);
    return cuda_mem_index;
}
