/*
 * SMSC LAN9118 PHY emulation
 *
 * Copyright (c) 2009 CodeSourcery, LLC.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU GPL v2
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/net/lan9118_phy.h"
#include "hw/irq.h"
#include "hw/resettable.h"
#include "migration/vmstate.h"
#include "qemu/log.h"

#define PHY_INT_ENERGYON            (1 << 7)
#define PHY_INT_AUTONEG_COMPLETE    (1 << 6)
#define PHY_INT_FAULT               (1 << 5)
#define PHY_INT_DOWN                (1 << 4)
#define PHY_INT_AUTONEG_LP          (1 << 3)
#define PHY_INT_PARFAULT            (1 << 2)
#define PHY_INT_AUTONEG_PAGE        (1 << 1)

static void lan9118_phy_update_irq(Lan9118PhyState *s)
{
    qemu_set_irq(s->irq, !!(s->ints & s->int_mask));
}

uint16_t lan9118_phy_read(Lan9118PhyState *s, int reg)
{
    uint16_t val;

    switch (reg) {
    case 0: /* Basic Control */
        return s->control;
    case 1: /* Basic Status */
        return s->status;
    case 2: /* ID1 */
        return 0x0007;
    case 3: /* ID2 */
        return 0xc0d1;
    case 4: /* Auto-neg advertisement */
        return s->advertise;
    case 5: /* Auto-neg Link Partner Ability */
        return 0x0f71;
    case 6: /* Auto-neg Expansion */
        return 1;
        /* TODO 17, 18, 27, 29, 30, 31 */
    case 29: /* Interrupt source. */
        val = s->ints;
        s->ints = 0;
        lan9118_phy_update_irq(s);
        return val;
    case 30: /* Interrupt mask */
        return s->int_mask;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "lan9118_phy_read: PHY read reg %d\n", reg);
        return 0;
    }
}

void lan9118_phy_write(Lan9118PhyState *s, int reg, uint16_t val)
{
    switch (reg) {
    case 0: /* Basic Control */
        if (val & 0x8000) {
            lan9118_phy_reset(s);
            break;
        }
        s->control = val & 0x7980;
        /* Complete autonegotiation immediately. */
        if (val & 0x1000) {
            s->status |= 0x0020;
        }
        break;
    case 4: /* Auto-neg advertisement */
        s->advertise = (val & 0x2d7f) | 0x80;
        break;
        /* TODO 17, 18, 27, 31 */
    case 30: /* Interrupt mask */
        s->int_mask = val & 0xff;
        lan9118_phy_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "lan9118_phy_write: PHY write reg %d = 0x%04x\n", reg, val);
    }
}

void lan9118_phy_update_link(Lan9118PhyState *s, bool link_down)
{
    s->link_down = link_down;

    /* Autonegotiation status mirrors link status. */
    if (link_down) {
        s->status &= ~0x0024;
        s->ints |= PHY_INT_DOWN;
    } else {
        s->status |= 0x0024;
        s->ints |= PHY_INT_ENERGYON;
        s->ints |= PHY_INT_AUTONEG_COMPLETE;
    }
    lan9118_phy_update_irq(s);
}

void lan9118_phy_reset(Lan9118PhyState *s)
{
    s->control = 0x3000;
    s->status = 0x7809;
    s->advertise = 0x01e1;
    s->int_mask = 0;
    s->ints = 0;
    lan9118_phy_update_link(s, s->link_down);
}

static void lan9118_phy_reset_hold(Object *obj, ResetType type)
{
    Lan9118PhyState *s = LAN9118_PHY(obj);

    lan9118_phy_reset(s);
}

static void lan9118_phy_init(Object *obj)
{
    Lan9118PhyState *s = LAN9118_PHY(obj);

    qdev_init_gpio_out(DEVICE(s), &s->irq, 1);
}

static const VMStateDescription vmstate_lan9118_phy = {
    .name = "lan9118-phy",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(control, Lan9118PhyState),
        VMSTATE_UINT16(status, Lan9118PhyState),
        VMSTATE_UINT16(advertise, Lan9118PhyState),
        VMSTATE_UINT16(ints, Lan9118PhyState),
        VMSTATE_UINT16(int_mask, Lan9118PhyState),
        VMSTATE_BOOL(link_down, Lan9118PhyState),
        VMSTATE_END_OF_LIST()
    }
};

static void lan9118_phy_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    rc->phases.hold = lan9118_phy_reset_hold;
    dc->vmsd = &vmstate_lan9118_phy;
}

static const TypeInfo types[] = {
    {
        .name          = TYPE_LAN9118_PHY,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Lan9118PhyState),
        .instance_init = lan9118_phy_init,
        .class_init    = lan9118_phy_class_init,
    }
};

DEFINE_TYPES(types)
