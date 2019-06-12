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
#include "qemu-common.h"
#include "hw/hw.h"
#include "hw/ppc/mac.h"
#include "hw/input/adb.h"
#include "hw/misc/mos6522.h"
#include "hw/misc/macio/cuda.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

/* Bits in B data register: all active low */
#define TREQ            0x08    /* Transfer request (input) */
#define TACK            0x10    /* Transfer acknowledge (output) */
#define TIP             0x20    /* Transfer in progress (output) */

/* commands (1st byte) */
#define ADB_PACKET      0
#define CUDA_PACKET     1
#define ERROR_PACKET    2
#define TIMER_PACKET    3
#define POWER_PACKET    4
#define MACIIC_PACKET   5
#define PMU_PACKET      6

#define CUDA_TIMER_FREQ (4700000 / 6)

/* CUDA returns time_t's offset from Jan 1, 1904, not 1970 */
#define RTC_OFFSET                      2082844800

static void cuda_receive_packet_from_host(CUDAState *s,
                                          const uint8_t *data, int len);

/* MacOS uses timer 1 for calibration on startup, so we use
 * the timebase frequency and cuda_get_counter_value() with
 * cuda_get_load_time() to steer MacOS to calculate calibrate its timers
 * correctly for both TCG and KVM (see commit b981289c49 "PPC: Cuda: Use cuda
 * timer to expose tbfreq to guest" for more information) */

static uint64_t cuda_get_counter_value(MOS6522State *s, MOS6522Timer *ti)
{
    MOS6522CUDAState *mcs = container_of(s, MOS6522CUDAState, parent_obj);
    CUDAState *cs = container_of(mcs, CUDAState, mos6522_cuda);

    /* Reverse of the tb calculation algorithm that Mac OS X uses on bootup */
    uint64_t tb_diff = muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                                cs->tb_frequency, NANOSECONDS_PER_SECOND) -
                           ti->load_time;

    return (tb_diff * 0xBF401675E5DULL) / (cs->tb_frequency << 24);
}

static uint64_t cuda_get_load_time(MOS6522State *s, MOS6522Timer *ti)
{
    MOS6522CUDAState *mcs = container_of(s, MOS6522CUDAState, parent_obj);
    CUDAState *cs = container_of(mcs, CUDAState, mos6522_cuda);

    uint64_t load_time = muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                                  cs->tb_frequency, NANOSECONDS_PER_SECOND);
    return load_time;
}

static void cuda_set_sr_int(void *opaque)
{
    CUDAState *s = opaque;
    MOS6522CUDAState *mcs = &s->mos6522_cuda;
    MOS6522State *ms = MOS6522(mcs);
    MOS6522DeviceClass *mdc = MOS6522_DEVICE_GET_CLASS(ms);

    mdc->set_sr_int(ms);
}

static void cuda_delay_set_sr_int(CUDAState *s)
{
    int64_t expire;

    trace_cuda_delay_set_sr_int();

    expire = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->sr_delay_ns;
    timer_mod(s->sr_delay_timer, expire);
}

/* NOTE: TIP and TREQ are negated */
static void cuda_update(CUDAState *s)
{
    MOS6522CUDAState *mcs = &s->mos6522_cuda;
    MOS6522State *ms = MOS6522(mcs);
    int packet_received, len;

    packet_received = 0;
    if (!(ms->b & TIP)) {
        /* transfer requested from host */

        if (ms->acr & SR_OUT) {
            /* data output */
            if ((ms->b & (TACK | TIP)) != (s->last_b & (TACK | TIP))) {
                if (s->data_out_index < sizeof(s->data_out)) {
                    trace_cuda_data_send(ms->sr);
                    s->data_out[s->data_out_index++] = ms->sr;
                    cuda_delay_set_sr_int(s);
                }
            }
        } else {
            if (s->data_in_index < s->data_in_size) {
                /* data input */
                if ((ms->b & (TACK | TIP)) != (s->last_b & (TACK | TIP))) {
                    ms->sr = s->data_in[s->data_in_index++];
                    trace_cuda_data_recv(ms->sr);
                    /* indicate end of transfer */
                    if (s->data_in_index >= s->data_in_size) {
                        ms->b = (ms->b | TREQ);
                    }
                    cuda_delay_set_sr_int(s);
                }
            }
        }
    } else {
        /* no transfer requested: handle sync case */
        if ((s->last_b & TIP) && (ms->b & TACK) != (s->last_b & TACK)) {
            /* update TREQ state each time TACK change state */
            if (ms->b & TACK) {
                ms->b = (ms->b | TREQ);
            } else {
                ms->b = (ms->b & ~TREQ);
            }
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
                ms->b = (ms->b & ~TREQ);
            }
        }
    }

    s->last_acr = ms->acr;
    s->last_b = ms->b;

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
    int i;

    trace_cuda_packet_send(len);
    for (i = 0; i < len; i++) {
        trace_cuda_packet_send_data(i, data[i]);
    }

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
    timer_mod(s->adb_poll_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
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

    qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    return true;
}

static bool cuda_cmd_reset_system(CUDAState *s,
                                  const uint8_t *in_data, int in_len,
                                  uint8_t *out_data, int *out_len)
{
    if (in_len != 0) {
        return false;
    }

    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
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
            trace_cuda_receive_packet_cmd(desc->name);
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
    int i;

    trace_cuda_packet_receive(len);
    for (i = 0; i < len; i++) {
        trace_cuda_packet_receive_data(i, data[i]);
    }

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

static uint64_t mos6522_cuda_read(void *opaque, hwaddr addr, unsigned size)
{
    CUDAState *s = opaque;
    MOS6522CUDAState *mcs = &s->mos6522_cuda;
    MOS6522State *ms = MOS6522(mcs);

    addr = (addr >> 9) & 0xf;
    return mos6522_read(ms, addr, size);
}

static void mos6522_cuda_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    CUDAState *s = opaque;
    MOS6522CUDAState *mcs = &s->mos6522_cuda;
    MOS6522State *ms = MOS6522(mcs);

    addr = (addr >> 9) & 0xf;
    mos6522_write(ms, addr, val, size);
}

static const MemoryRegionOps mos6522_cuda_ops = {
    .read = mos6522_cuda_read,
    .write = mos6522_cuda_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const VMStateDescription vmstate_cuda = {
    .name = "cuda",
    .version_id = 5,
    .minimum_version_id = 5,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(mos6522_cuda.parent_obj, CUDAState, 0, vmstate_mos6522,
                       MOS6522State),
        VMSTATE_UINT8(last_b, CUDAState),
        VMSTATE_UINT8(last_acr, CUDAState),
        VMSTATE_INT32(data_in_size, CUDAState),
        VMSTATE_INT32(data_in_index, CUDAState),
        VMSTATE_INT32(data_out_index, CUDAState),
        VMSTATE_UINT8(autopoll, CUDAState),
        VMSTATE_UINT8(autopoll_rate_ms, CUDAState),
        VMSTATE_UINT16(adb_poll_mask, CUDAState),
        VMSTATE_BUFFER(data_in, CUDAState),
        VMSTATE_BUFFER(data_out, CUDAState),
        VMSTATE_UINT32(tick_offset, CUDAState),
        VMSTATE_TIMER_PTR(adb_poll_timer, CUDAState),
        VMSTATE_TIMER_PTR(sr_delay_timer, CUDAState),
        VMSTATE_END_OF_LIST()
    }
};

static void cuda_reset(DeviceState *dev)
{
    CUDAState *s = CUDA(dev);

    s->data_in_size = 0;
    s->data_in_index = 0;
    s->data_out_index = 0;
    s->autopoll = 0;
}

static void cuda_realize(DeviceState *dev, Error **errp)
{
    CUDAState *s = CUDA(dev);
    SysBusDevice *sbd;
    MOS6522State *ms;
    DeviceState *d;
    struct tm tm;

    /* Pass IRQ from 6522 */
    d = DEVICE(&s->mos6522_cuda);
    ms = MOS6522(d);
    sbd = SYS_BUS_DEVICE(s);
    sysbus_pass_irq(sbd, SYS_BUS_DEVICE(ms));

    qemu_get_timedate(&tm, 0);
    s->tick_offset = (uint32_t)mktimegm(&tm) + RTC_OFFSET;

    s->sr_delay_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, cuda_set_sr_int, s);
    s->sr_delay_ns = 20 * SCALE_US;

    s->adb_poll_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, cuda_adb_poll, s);
    s->adb_poll_mask = 0xffff;
    s->autopoll_rate_ms = 20;
}

static void cuda_init(Object *obj)
{
    CUDAState *s = CUDA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_child_obj(obj, "mos6522-cuda", &s->mos6522_cuda,
                          sizeof(s->mos6522_cuda), TYPE_MOS6522_CUDA);

    memory_region_init_io(&s->mem, obj, &mos6522_cuda_ops, s, "cuda", 0x2000);
    sysbus_init_mmio(sbd, &s->mem);

    qbus_create_inplace(&s->adb_bus, sizeof(s->adb_bus), TYPE_ADB_BUS,
                        DEVICE(obj), "adb.0");
}

static Property cuda_properties[] = {
    DEFINE_PROP_UINT64("timebase-frequency", CUDAState, tb_frequency, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void cuda_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = cuda_realize;
    dc->reset = cuda_reset;
    dc->vmsd = &vmstate_cuda;
    dc->props = cuda_properties;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo cuda_type_info = {
    .name = TYPE_CUDA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CUDAState),
    .instance_init = cuda_init,
    .class_init = cuda_class_init,
};

static void mos6522_cuda_portB_write(MOS6522State *s)
{
    MOS6522CUDAState *mcs = container_of(s, MOS6522CUDAState, parent_obj);
    CUDAState *cs = container_of(mcs, CUDAState, mos6522_cuda);

    cuda_update(cs);
}

static void mos6522_cuda_reset(DeviceState *dev)
{
    MOS6522State *ms = MOS6522(dev);
    MOS6522DeviceClass *mdc = MOS6522_DEVICE_GET_CLASS(ms);

    mdc->parent_reset(dev);

    ms->timers[0].frequency = CUDA_TIMER_FREQ;
    ms->timers[1].frequency = (SCALE_US * 6000) / 4700;
}

static void mos6522_cuda_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MOS6522DeviceClass *mdc = MOS6522_DEVICE_CLASS(oc);

    dc->reset = mos6522_cuda_reset;
    mdc->portB_write = mos6522_cuda_portB_write;
    mdc->get_timer1_counter_value = cuda_get_counter_value;
    mdc->get_timer2_counter_value = cuda_get_counter_value;
    mdc->get_timer1_load_time = cuda_get_load_time;
    mdc->get_timer2_load_time = cuda_get_load_time;
}

static const TypeInfo mos6522_cuda_type_info = {
    .name = TYPE_MOS6522_CUDA,
    .parent = TYPE_MOS6522,
    .instance_size = sizeof(MOS6522CUDAState),
    .class_init = mos6522_cuda_class_init,
};

static void cuda_register_types(void)
{
    type_register_static(&mos6522_cuda_type_info);
    type_register_static(&cuda_type_info);
}

type_init(cuda_register_types)
