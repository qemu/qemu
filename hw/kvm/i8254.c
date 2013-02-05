/*
 * KVM in-kernel PIT (i8254) support
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2012      Jan Kiszka, Siemens AG
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
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "hw/timer/i8254.h"
#include "hw/timer/i8254_internal.h"
#include "sysemu/kvm.h"

#define KVM_PIT_REINJECT_BIT 0

#define CALIBRATION_ROUNDS   3

typedef struct KVMPITState {
    PITCommonState pit;
    LostTickPolicy lost_tick_policy;
    bool vm_stopped;
    int64_t kernel_clock_offset;
} KVMPITState;

static int64_t abs64(int64_t v)
{
    return v < 0 ? -v : v;
}

static void kvm_pit_update_clock_offset(KVMPITState *s)
{
    int64_t offset, clock_offset;
    struct timespec ts;
    int i;

    /*
     * Measure the delta between CLOCK_MONOTONIC, the base used for
     * kvm_pit_channel_state::count_load_time, and vm_clock. Take the
     * minimum of several samples to filter out scheduling noise.
     */
    clock_offset = INT64_MAX;
    for (i = 0; i < CALIBRATION_ROUNDS; i++) {
        offset = qemu_get_clock_ns(vm_clock);
        clock_gettime(CLOCK_MONOTONIC, &ts);
        offset -= ts.tv_nsec;
        offset -= (int64_t)ts.tv_sec * 1000000000;
        if (abs64(offset) < abs64(clock_offset)) {
            clock_offset = offset;
        }
    }
    s->kernel_clock_offset = clock_offset;
}

static void kvm_pit_get(PITCommonState *pit)
{
    KVMPITState *s = DO_UPCAST(KVMPITState, pit, pit);
    struct kvm_pit_state2 kpit;
    struct kvm_pit_channel_state *kchan;
    struct PITChannelState *sc;
    int i, ret;

    /* No need to re-read the state if VM is stopped. */
    if (s->vm_stopped) {
        return;
    }

    if (kvm_has_pit_state2()) {
        ret = kvm_vm_ioctl(kvm_state, KVM_GET_PIT2, &kpit);
        if (ret < 0) {
            fprintf(stderr, "KVM_GET_PIT2 failed: %s\n", strerror(ret));
            abort();
        }
        pit->channels[0].irq_disabled = kpit.flags & KVM_PIT_FLAGS_HPET_LEGACY;
    } else {
        /*
         * kvm_pit_state2 is superset of kvm_pit_state struct,
         * so we can use it for KVM_GET_PIT as well.
         */
        ret = kvm_vm_ioctl(kvm_state, KVM_GET_PIT, &kpit);
        if (ret < 0) {
            fprintf(stderr, "KVM_GET_PIT failed: %s\n", strerror(ret));
            abort();
        }
    }
    for (i = 0; i < 3; i++) {
        kchan = &kpit.channels[i];
        sc = &pit->channels[i];
        sc->count = kchan->count;
        sc->latched_count = kchan->latched_count;
        sc->count_latched = kchan->count_latched;
        sc->status_latched = kchan->status_latched;
        sc->status = kchan->status;
        sc->read_state = kchan->read_state;
        sc->write_state = kchan->write_state;
        sc->write_latch = kchan->write_latch;
        sc->rw_mode = kchan->rw_mode;
        sc->mode = kchan->mode;
        sc->bcd = kchan->bcd;
        sc->gate = kchan->gate;
        sc->count_load_time = kchan->count_load_time + s->kernel_clock_offset;
    }

    sc = &pit->channels[0];
    sc->next_transition_time =
        pit_get_next_transition_time(sc, sc->count_load_time);
}

static void kvm_pit_put(PITCommonState *pit)
{
    KVMPITState *s = DO_UPCAST(KVMPITState, pit, pit);
    struct kvm_pit_state2 kpit;
    struct kvm_pit_channel_state *kchan;
    struct PITChannelState *sc;
    int i, ret;

    /* The offset keeps changing as long as the VM is stopped. */
    if (s->vm_stopped) {
        kvm_pit_update_clock_offset(s);
    }

    kpit.flags = pit->channels[0].irq_disabled ? KVM_PIT_FLAGS_HPET_LEGACY : 0;
    for (i = 0; i < 3; i++) {
        kchan = &kpit.channels[i];
        sc = &pit->channels[i];
        kchan->count = sc->count;
        kchan->latched_count = sc->latched_count;
        kchan->count_latched = sc->count_latched;
        kchan->status_latched = sc->status_latched;
        kchan->status = sc->status;
        kchan->read_state = sc->read_state;
        kchan->write_state = sc->write_state;
        kchan->write_latch = sc->write_latch;
        kchan->rw_mode = sc->rw_mode;
        kchan->mode = sc->mode;
        kchan->bcd = sc->bcd;
        kchan->gate = sc->gate;
        kchan->count_load_time = sc->count_load_time - s->kernel_clock_offset;
    }

    ret = kvm_vm_ioctl(kvm_state,
                       kvm_has_pit_state2() ? KVM_SET_PIT2 : KVM_SET_PIT,
                       &kpit);
    if (ret < 0) {
        fprintf(stderr, "%s failed: %s\n",
                kvm_has_pit_state2() ? "KVM_SET_PIT2" : "KVM_SET_PIT",
                strerror(ret));
        abort();
    }
}

static void kvm_pit_set_gate(PITCommonState *s, PITChannelState *sc, int val)
{
    kvm_pit_get(s);

    switch (sc->mode) {
    default:
    case 0:
    case 4:
        /* XXX: just disable/enable counting */
        break;
    case 1:
    case 2:
    case 3:
    case 5:
        if (sc->gate < val) {
            /* restart counting on rising edge */
            sc->count_load_time = qemu_get_clock_ns(vm_clock);
        }
        break;
    }
    sc->gate = val;

    kvm_pit_put(s);
}

static void kvm_pit_get_channel_info(PITCommonState *s, PITChannelState *sc,
                                     PITChannelInfo *info)
{
    kvm_pit_get(s);

    pit_get_channel_info_common(s, sc, info);
}

static void kvm_pit_reset(DeviceState *dev)
{
    PITCommonState *s = DO_UPCAST(PITCommonState, dev.qdev, dev);

    pit_reset_common(s);

    kvm_pit_put(s);
}

static void kvm_pit_irq_control(void *opaque, int n, int enable)
{
    PITCommonState *pit = opaque;
    PITChannelState *s = &pit->channels[0];

    kvm_pit_get(pit);

    s->irq_disabled = !enable;

    kvm_pit_put(pit);
}

static void kvm_pit_vm_state_change(void *opaque, int running,
                                    RunState state)
{
    KVMPITState *s = opaque;

    if (running) {
        kvm_pit_update_clock_offset(s);
        s->vm_stopped = false;
    } else {
        kvm_pit_update_clock_offset(s);
        kvm_pit_get(&s->pit);
        s->vm_stopped = true;
    }
}

static int kvm_pit_initfn(PITCommonState *pit)
{
    KVMPITState *s = DO_UPCAST(KVMPITState, pit, pit);
    struct kvm_pit_config config = {
        .flags = 0,
    };
    int ret;

    if (kvm_check_extension(kvm_state, KVM_CAP_PIT2)) {
        ret = kvm_vm_ioctl(kvm_state, KVM_CREATE_PIT2, &config);
    } else {
        ret = kvm_vm_ioctl(kvm_state, KVM_CREATE_PIT);
    }
    if (ret < 0) {
        fprintf(stderr, "Create kernel PIC irqchip failed: %s\n",
                strerror(ret));
        return ret;
    }
    switch (s->lost_tick_policy) {
    case LOST_TICK_DELAY:
        break; /* enabled by default */
    case LOST_TICK_DISCARD:
        if (kvm_check_extension(kvm_state, KVM_CAP_REINJECT_CONTROL)) {
            struct kvm_reinject_control control = { .pit_reinject = 0 };

            ret = kvm_vm_ioctl(kvm_state, KVM_REINJECT_CONTROL, &control);
            if (ret < 0) {
                fprintf(stderr,
                        "Can't disable in-kernel PIT reinjection: %s\n",
                        strerror(ret));
                return ret;
            }
        }
        break;
    default:
        return -EINVAL;
    }

    memory_region_init_reservation(&pit->ioports, "kvm-pit", 4);

    qdev_init_gpio_in(&pit->dev.qdev, kvm_pit_irq_control, 1);

    qemu_add_vm_change_state_handler(kvm_pit_vm_state_change, s);

    return 0;
}

static Property kvm_pit_properties[] = {
    DEFINE_PROP_HEX32("iobase", KVMPITState, pit.iobase,  -1),
    DEFINE_PROP_LOSTTICKPOLICY("lost_tick_policy", KVMPITState,
                               lost_tick_policy, LOST_TICK_DELAY),
    DEFINE_PROP_END_OF_LIST(),
};

static void kvm_pit_class_init(ObjectClass *klass, void *data)
{
    PITCommonClass *k = PIT_COMMON_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->init = kvm_pit_initfn;
    k->set_channel_gate = kvm_pit_set_gate;
    k->get_channel_info = kvm_pit_get_channel_info;
    k->pre_save = kvm_pit_get;
    k->post_load = kvm_pit_put;
    dc->reset = kvm_pit_reset;
    dc->props = kvm_pit_properties;
}

static const TypeInfo kvm_pit_info = {
    .name          = "kvm-pit",
    .parent        = TYPE_PIT_COMMON,
    .instance_size = sizeof(KVMPITState),
    .class_init = kvm_pit_class_init,
};

static void kvm_pit_register(void)
{
    type_register_static(&kvm_pit_info);
}

type_init(kvm_pit_register)
