/*
 * SMSC LAN9118 PHY emulation
 *
 * Copyright (c) 2009 CodeSourcery, LLC.
 * Written by Paul Brook
 *
 * Copyright (c) 2013 Jean-Christophe Dubois. <jcd@tribudubois.net>
 *
 * This code is licensed under the GNU GPL v2
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/net/lan9118_phy.h"
#include "hw/net/mii.h"
#include "hw/irq.h"
#include "hw/resettable.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "trace.h"

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
    case MII_BMCR:
        val = s->control;
        break;
    case MII_BMSR:
        val = s->status;
        break;
    case MII_PHYID1:
        val = SMSCLAN9118_PHYID1;
        break;
    case MII_PHYID2:
        val = SMSCLAN9118_PHYID2;
        break;
    case MII_ANAR:
        val = s->advertise;
        break;
    case MII_ANLPAR:
        val = MII_ANLPAR_PAUSEASY | MII_ANLPAR_PAUSE | MII_ANLPAR_T4 |
              MII_ANLPAR_TXFD | MII_ANLPAR_TX | MII_ANLPAR_10FD |
              MII_ANLPAR_10 | MII_ANLPAR_CSMACD;
        break;
    case MII_ANER:
        val = MII_ANER_NWAY;
        break;
    case 29: /* Interrupt source. */
        val = s->ints;
        s->ints = 0;
        lan9118_phy_update_irq(s);
        break;
    case 30: /* Interrupt mask */
        val = s->int_mask;
        break;
    case 17:
    case 18:
    case 27:
    case 31:
        qemu_log_mask(LOG_UNIMP, "%s: reg %d not implemented\n",
                      __func__, reg);
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad address at offset %d\n",
                      __func__, reg);
        val = 0;
        break;
    }

    trace_lan9118_phy_read(val, reg);

    return val;
}

void lan9118_phy_write(Lan9118PhyState *s, int reg, uint16_t val)
{
    trace_lan9118_phy_write(val, reg);

    switch (reg) {
    case MII_BMCR:
        if (val & MII_BMCR_RESET) {
            lan9118_phy_reset(s);
        } else {
            s->control = val & (MII_BMCR_LOOPBACK | MII_BMCR_SPEED100 |
                                MII_BMCR_AUTOEN | MII_BMCR_PDOWN | MII_BMCR_FD |
                                MII_BMCR_CTST);
            /* Complete autonegotiation immediately. */
            if (val & MII_BMCR_AUTOEN) {
                s->status |= MII_BMSR_AN_COMP;
            }
        }
        break;
    case MII_ANAR:
        s->advertise = (val & (MII_ANAR_RFAULT | MII_ANAR_PAUSE_ASYM |
                               MII_ANAR_PAUSE | MII_ANAR_TXFD | MII_ANAR_10FD |
                               MII_ANAR_10 | MII_ANAR_SELECT))
                     | MII_ANAR_TX;
        break;
    case 30: /* Interrupt mask */
        s->int_mask = val & 0xff;
        lan9118_phy_update_irq(s);
        break;
    case 17:
    case 18:
    case 27:
    case 31:
        qemu_log_mask(LOG_UNIMP, "%s: reg %d not implemented\n",
                      __func__, reg);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad address at offset %d\n",
                      __func__, reg);
        break;
    }
}

void lan9118_phy_update_link(Lan9118PhyState *s, bool link_down)
{
    s->link_down = link_down;

    /* Autonegotiation status mirrors link status. */
    if (link_down) {
        trace_lan9118_phy_update_link("down");
        s->status &= ~(MII_BMSR_AN_COMP | MII_BMSR_LINK_ST);
        s->ints |= PHY_INT_DOWN;
    } else {
        trace_lan9118_phy_update_link("up");
        s->status |= MII_BMSR_AN_COMP | MII_BMSR_LINK_ST;
        s->ints |= PHY_INT_ENERGYON;
        s->ints |= PHY_INT_AUTONEG_COMPLETE;
    }
    lan9118_phy_update_irq(s);
}

void lan9118_phy_reset(Lan9118PhyState *s)
{
    trace_lan9118_phy_reset();

    s->control = MII_BMCR_AUTOEN | MII_BMCR_SPEED100;
    s->status = MII_BMSR_100TX_FD
                | MII_BMSR_100TX_HD
                | MII_BMSR_10T_FD
                | MII_BMSR_10T_HD
                | MII_BMSR_AUTONEG
                | MII_BMSR_EXTCAP;
    s->advertise = MII_ANAR_TXFD
                   | MII_ANAR_TX
                   | MII_ANAR_10FD
                   | MII_ANAR_10
                   | MII_ANAR_CSMACD;
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
        VMSTATE_UINT16(status, Lan9118PhyState),
        VMSTATE_UINT16(control, Lan9118PhyState),
        VMSTATE_UINT16(advertise, Lan9118PhyState),
        VMSTATE_UINT16(ints, Lan9118PhyState),
        VMSTATE_UINT16(int_mask, Lan9118PhyState),
        VMSTATE_BOOL(link_down, Lan9118PhyState),
        VMSTATE_END_OF_LIST()
    }
};

static void lan9118_phy_class_init(ObjectClass *klass, const void *data)
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
