/*
 * Virtual hardware watchdog.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * By Richard W.M. Jones (rjones@redhat.com).
 */

#include "qemu/osdep.h"

#include "qemu/module.h"
#include "qemu/timer.h"
#include "sysemu/watchdog.h"
#include "hw/pci/pci.h"
#include "migration/vmstate.h"
#include "qom/object.h"

/*#define I6300ESB_DEBUG 1*/

#ifdef I6300ESB_DEBUG
#define i6300esb_debug(fs,...) \
    fprintf(stderr,"i6300esb: %s: "fs,__func__,##__VA_ARGS__)
#else
#define i6300esb_debug(fs,...)
#endif

/* PCI configuration registers */
#define ESB_CONFIG_REG  0x60            /* Config register                   */
#define ESB_LOCK_REG    0x68            /* WDT lock register                 */

/* Memory mapped registers (offset from base address) */
#define ESB_TIMER1_REG  0x00            /* Timer1 value after each reset     */
#define ESB_TIMER2_REG  0x04            /* Timer2 value after each reset     */
#define ESB_GINTSR_REG  0x08            /* General Interrupt Status Register */
#define ESB_RELOAD_REG  0x0c            /* Reload register                   */

/* Lock register bits */
#define ESB_WDT_FUNC    (0x01 << 2)   /* Watchdog functionality            */
#define ESB_WDT_ENABLE  (0x01 << 1)   /* Enable WDT                        */
#define ESB_WDT_LOCK    (0x01 << 0)   /* Lock (nowayout)                   */

/* Config register bits */
#define ESB_WDT_REBOOT  (0x01 << 5)   /* Enable reboot on timeout          */
#define ESB_WDT_FREQ    (0x01 << 2)   /* Decrement frequency               */
#define ESB_WDT_INTTYPE (0x11 << 0)   /* Interrupt type on timer1 timeout  */

/* Reload register bits */
#define ESB_WDT_RELOAD  (0x01 << 8)    /* prevent timeout                   */

/* Magic constants */
#define ESB_UNLOCK1     0x80            /* Step 1 to unlock reset registers  */
#define ESB_UNLOCK2     0x86            /* Step 2 to unlock reset registers  */

/* Device state. */
struct I6300State {
    PCIDevice dev;
    MemoryRegion io_mem;

    int reboot_enabled;         /* "Reboot" on timer expiry.  The real action
                                 * performed depends on the -watchdog-action
                                 * param passed on QEMU command line.
                                 */
    int clock_scale;            /* Clock scale. */
#define CLOCK_SCALE_1KHZ 0
#define CLOCK_SCALE_1MHZ 1

    int int_type;               /* Interrupt type generated. */
#define INT_TYPE_IRQ 0          /* APIC 1, INT 10 */
#define INT_TYPE_SMI 2
#define INT_TYPE_DISABLED 3

    int free_run;               /* If true, reload timer on expiry. */
    int locked;                 /* If true, enabled field cannot be changed. */
    int enabled;                /* If true, watchdog is enabled. */

    QEMUTimer *timer;           /* The actual watchdog timer. */

    uint32_t timer1_preload;    /* Values preloaded into timer1, timer2. */
    uint32_t timer2_preload;
    int stage;                  /* Stage (1 or 2). */

    int unlock_state;           /* Guest writes 0x80, 0x86 to unlock the
                                 * registers, and we transition through
                                 * states 0 -> 1 -> 2 when this happens.
                                 */

    int previous_reboot_flag;   /* If the watchdog caused the previous
                                 * reboot, this flag will be set.
                                 */
};


#define TYPE_WATCHDOG_I6300ESB_DEVICE "i6300esb"
OBJECT_DECLARE_SIMPLE_TYPE(I6300State, WATCHDOG_I6300ESB_DEVICE)

/* This function is called when the watchdog has either been enabled
 * (hence it starts counting down) or has been keep-alived.
 */
static void i6300esb_restart_timer(I6300State *d, int stage)
{
    int64_t timeout;

    if (!d->enabled)
        return;

    d->stage = stage;

    if (d->stage <= 1)
        timeout = d->timer1_preload;
    else
        timeout = d->timer2_preload;

    if (d->clock_scale == CLOCK_SCALE_1KHZ)
        timeout <<= 15;
    else
        timeout <<= 5;

    /* Get the timeout in nanoseconds. */

    timeout = timeout * 30; /* on a PCI bus, 1 tick is 30 ns*/

    i6300esb_debug("stage %d, timeout %" PRIi64 "\n", d->stage, timeout);

    timer_mod(d->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + timeout);
}

/* This is called when the guest disables the watchdog. */
static void i6300esb_disable_timer(I6300State *d)
{
    i6300esb_debug("timer disabled\n");

    timer_del(d->timer);
}

static void i6300esb_reset(DeviceState *dev)
{
    PCIDevice *pdev = PCI_DEVICE(dev);
    I6300State *d = WATCHDOG_I6300ESB_DEVICE(pdev);

    i6300esb_debug("I6300State = %p\n", d);

    i6300esb_disable_timer(d);

    /* NB: Don't change d->previous_reboot_flag in this function. */

    d->reboot_enabled = 1;
    d->clock_scale = CLOCK_SCALE_1KHZ;
    d->int_type = INT_TYPE_IRQ;
    d->free_run = 0;
    d->locked = 0;
    d->enabled = 0;
    d->timer1_preload = 0xfffff;
    d->timer2_preload = 0xfffff;
    d->stage = 1;
    d->unlock_state = 0;
}

/* This function is called when the watchdog expires.  Note that
 * the hardware has two timers, and so expiry happens in two stages.
 * If d->stage == 1 then we perform the first stage action (usually,
 * sending an interrupt) and then restart the timer again for the
 * second stage.  If the second stage expires then the watchdog
 * really has run out.
 */
static void i6300esb_timer_expired(void *vp)
{
    I6300State *d = vp;

    i6300esb_debug("stage %d\n", d->stage);

    if (d->stage == 1) {
        /* What to do at the end of stage 1? */
        switch (d->int_type) {
        case INT_TYPE_IRQ:
            fprintf(stderr, "i6300esb_timer_expired: I would send APIC 1 INT 10 here if I knew how (XXX)\n");
            break;
        case INT_TYPE_SMI:
            fprintf(stderr, "i6300esb_timer_expired: I would send SMI here if I knew how (XXX)\n");
            break;
        }

        /* Start the second stage. */
        i6300esb_restart_timer(d, 2);
    } else {
        /* Second stage expired, reboot for real. */
        if (d->reboot_enabled) {
            d->previous_reboot_flag = 1;
            watchdog_perform_action(); /* This reboots, exits, etc */
            i6300esb_reset(DEVICE(d));
        }

        /* In "free running mode" we start stage 1 again. */
        if (d->free_run)
            i6300esb_restart_timer(d, 1);
    }
}

static void i6300esb_config_write(PCIDevice *dev, uint32_t addr,
                                  uint32_t data, int len)
{
    I6300State *d = WATCHDOG_I6300ESB_DEVICE(dev);
    int old;

    i6300esb_debug("addr = %x, data = %x, len = %d\n", addr, data, len);

    if (addr == ESB_CONFIG_REG && len == 2) {
        d->reboot_enabled = (data & ESB_WDT_REBOOT) == 0;
        d->clock_scale =
            (data & ESB_WDT_FREQ) != 0 ? CLOCK_SCALE_1MHZ : CLOCK_SCALE_1KHZ;
        d->int_type = (data & ESB_WDT_INTTYPE);
    } else if (addr == ESB_LOCK_REG && len == 1) {
        if (!d->locked) {
            d->locked = (data & ESB_WDT_LOCK) != 0;
            d->free_run = (data & ESB_WDT_FUNC) != 0;
            old = d->enabled;
            d->enabled = (data & ESB_WDT_ENABLE) != 0;
            if (!old && d->enabled) /* Enabled transitioned from 0 -> 1 */
                i6300esb_restart_timer(d, 1);
            else if (!d->enabled)
                i6300esb_disable_timer(d);
        }
    } else {
        pci_default_write_config(dev, addr, data, len);
    }
}

static uint32_t i6300esb_config_read(PCIDevice *dev, uint32_t addr, int len)
{
    I6300State *d = WATCHDOG_I6300ESB_DEVICE(dev);
    uint32_t data;

    i6300esb_debug ("addr = %x, len = %d\n", addr, len);

    if (addr == ESB_CONFIG_REG && len == 2) {
        data =
            (d->reboot_enabled ? 0 : ESB_WDT_REBOOT) |
            (d->clock_scale == CLOCK_SCALE_1MHZ ? ESB_WDT_FREQ : 0) |
            d->int_type;
        return data;
    } else if (addr == ESB_LOCK_REG && len == 1) {
        data =
            (d->free_run ? ESB_WDT_FUNC : 0) |
            (d->locked ? ESB_WDT_LOCK : 0) |
            (d->enabled ? ESB_WDT_ENABLE : 0);
        return data;
    } else {
        return pci_default_read_config(dev, addr, len);
    }
}

static uint32_t i6300esb_mem_readb(void *vp, hwaddr addr)
{
    i6300esb_debug ("addr = %x\n", (int) addr);

    return 0;
}

static uint32_t i6300esb_mem_readw(void *vp, hwaddr addr)
{
    uint32_t data = 0;
    I6300State *d = vp;

    i6300esb_debug("addr = %x\n", (int) addr);

    if (addr == 0xc) {
        /* The previous reboot flag is really bit 9, but there is
         * a bug in the Linux driver where it thinks it's bit 12.
         * Set both.
         */
        data = d->previous_reboot_flag ? 0x1200 : 0;
    }

    return data;
}

static uint32_t i6300esb_mem_readl(void *vp, hwaddr addr)
{
    i6300esb_debug("addr = %x\n", (int) addr);

    return 0;
}

static void i6300esb_mem_writeb(void *vp, hwaddr addr, uint32_t val)
{
    I6300State *d = vp;

    i6300esb_debug("addr = %x, val = %x\n", (int) addr, val);

    if (addr == 0xc && val == 0x80)
        d->unlock_state = 1;
    else if (addr == 0xc && val == 0x86 && d->unlock_state == 1)
        d->unlock_state = 2;
}

static void i6300esb_mem_writew(void *vp, hwaddr addr, uint32_t val)
{
    I6300State *d = vp;

    i6300esb_debug("addr = %x, val = %x\n", (int) addr, val);

    if (addr == 0xc && val == 0x80)
        d->unlock_state = 1;
    else if (addr == 0xc && val == 0x86 && d->unlock_state == 1)
        d->unlock_state = 2;
    else {
        if (d->unlock_state == 2) {
            if (addr == 0xc) {
                if ((val & 0x100) != 0)
                    /* This is the "ping" from the userspace watchdog in
                     * the guest ...
                     */
                    i6300esb_restart_timer(d, 1);

                /* Setting bit 9 resets the previous reboot flag.
                 * There's a bug in the Linux driver where it sets
                 * bit 12 instead.
                 */
                if ((val & 0x200) != 0 || (val & 0x1000) != 0) {
                    d->previous_reboot_flag = 0;
                }
            }

            d->unlock_state = 0;
        }
    }
}

static void i6300esb_mem_writel(void *vp, hwaddr addr, uint32_t val)
{
    I6300State *d = vp;

    i6300esb_debug ("addr = %x, val = %x\n", (int) addr, val);

    if (addr == 0xc && val == 0x80)
        d->unlock_state = 1;
    else if (addr == 0xc && val == 0x86 && d->unlock_state == 1)
        d->unlock_state = 2;
    else {
        if (d->unlock_state == 2) {
            if (addr == 0)
                d->timer1_preload = val & 0xfffff;
            else if (addr == 4)
                d->timer2_preload = val & 0xfffff;

            d->unlock_state = 0;
        }
    }
}

static uint64_t i6300esb_mem_readfn(void *opaque, hwaddr addr, unsigned size)
{
    switch (size) {
    case 1:
        return i6300esb_mem_readb(opaque, addr);
    case 2:
        return i6300esb_mem_readw(opaque, addr);
    case 4:
        return i6300esb_mem_readl(opaque, addr);
    default:
        g_assert_not_reached();
    }
}

static void i6300esb_mem_writefn(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    switch (size) {
    case 1:
        i6300esb_mem_writeb(opaque, addr, value);
        break;
    case 2:
        i6300esb_mem_writew(opaque, addr, value);
        break;
    case 4:
        i6300esb_mem_writel(opaque, addr, value);
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps i6300esb_ops = {
    .read = i6300esb_mem_readfn,
    .write = i6300esb_mem_writefn,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const VMStateDescription vmstate_i6300esb = {
    .name = "i6300esb_wdt",
    /* With this VMSD's introduction, version_id/minimum_version_id were
     * erroneously set to sizeof(I6300State), causing a somewhat random
     * version_id to be set for every build. This eventually broke
     * migration.
     *
     * To correct this without breaking old->new migration for older
     * versions of QEMU, we've set version_id to a value high enough
     * to exceed all past values of sizeof(I6300State) across various
     * build environments, and have reset minimum_version_id to 1,
     * since this VMSD has never changed and thus can accept all past
     * versions.
     *
     * For future changes we can treat these values as we normally would.
     */
    .version_id = 10000,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, I6300State),
        VMSTATE_INT32(reboot_enabled, I6300State),
        VMSTATE_INT32(clock_scale, I6300State),
        VMSTATE_INT32(int_type, I6300State),
        VMSTATE_INT32(free_run, I6300State),
        VMSTATE_INT32(locked, I6300State),
        VMSTATE_INT32(enabled, I6300State),
        VMSTATE_TIMER_PTR(timer, I6300State),
        VMSTATE_UINT32(timer1_preload, I6300State),
        VMSTATE_UINT32(timer2_preload, I6300State),
        VMSTATE_INT32(stage, I6300State),
        VMSTATE_INT32(unlock_state, I6300State),
        VMSTATE_INT32(previous_reboot_flag, I6300State),
        VMSTATE_END_OF_LIST()
    }
};

static void i6300esb_realize(PCIDevice *dev, Error **errp)
{
    I6300State *d = WATCHDOG_I6300ESB_DEVICE(dev);

    i6300esb_debug("I6300State = %p\n", d);

    d->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, i6300esb_timer_expired, d);
    d->previous_reboot_flag = 0;

    memory_region_init_io(&d->io_mem, OBJECT(d), &i6300esb_ops, d,
                          "i6300esb", 0x10);
    pci_register_bar(&d->dev, 0, 0, &d->io_mem);
}

static void i6300esb_exit(PCIDevice *dev)
{
    I6300State *d = WATCHDOG_I6300ESB_DEVICE(dev);

    timer_free(d->timer);
}

static void i6300esb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read = i6300esb_config_read;
    k->config_write = i6300esb_config_write;
    k->realize = i6300esb_realize;
    k->exit = i6300esb_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_ESB_9;
    k->class_id = PCI_CLASS_SYSTEM_OTHER;
    dc->reset = i6300esb_reset;
    dc->vmsd = &vmstate_i6300esb;
    set_bit(DEVICE_CATEGORY_WATCHDOG, dc->categories);
    dc->desc = "Intel 6300ESB";
}

static const TypeInfo i6300esb_info = {
    .name          = TYPE_WATCHDOG_I6300ESB_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(I6300State),
    .class_init    = i6300esb_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void i6300esb_register_types(void)
{
    type_register_static(&i6300esb_info);
}

type_init(i6300esb_register_types)
