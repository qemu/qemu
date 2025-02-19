/*
 * Nuvoton NPCM8xx PCS Module
 *
 * Copyright 2022 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

/*
 * Disclaimer:
 * Currently we only implemented the default values of the registers and
 * the soft reset feature. These are required to boot up the GMAC module
 * in Linux kernel for NPCM845 boards. Other functionalities are not modeled.
 */

#include "qemu/osdep.h"

#include "exec/hwaddr.h"
#include "hw/registerfields.h"
#include "hw/net/npcm_pcs.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "trace.h"

#define NPCM_PCS_IND_AC_BA      0x1fe
#define NPCM_PCS_IND_SR_CTL     0x1e00
#define NPCM_PCS_IND_SR_MII     0x1f00
#define NPCM_PCS_IND_SR_TIM     0x1f07
#define NPCM_PCS_IND_VR_MII     0x1f80

REG16(NPCM_PCS_SR_CTL_ID1, 0x08)
REG16(NPCM_PCS_SR_CTL_ID2, 0x0a)
REG16(NPCM_PCS_SR_CTL_STS, 0x10)

REG16(NPCM_PCS_SR_MII_CTRL, 0x00)
REG16(NPCM_PCS_SR_MII_STS, 0x02)
REG16(NPCM_PCS_SR_MII_DEV_ID1, 0x04)
REG16(NPCM_PCS_SR_MII_DEV_ID2, 0x06)
REG16(NPCM_PCS_SR_MII_AN_ADV, 0x08)
REG16(NPCM_PCS_SR_MII_LP_BABL, 0x0a)
REG16(NPCM_PCS_SR_MII_AN_EXPN, 0x0c)
REG16(NPCM_PCS_SR_MII_EXT_STS, 0x1e)

REG16(NPCM_PCS_SR_TIM_SYNC_ABL, 0x10)
REG16(NPCM_PCS_SR_TIM_SYNC_TX_MAX_DLY_LWR, 0x12)
REG16(NPCM_PCS_SR_TIM_SYNC_TX_MAX_DLY_UPR, 0x14)
REG16(NPCM_PCS_SR_TIM_SYNC_TX_MIN_DLY_LWR, 0x16)
REG16(NPCM_PCS_SR_TIM_SYNC_TX_MIN_DLY_UPR, 0x18)
REG16(NPCM_PCS_SR_TIM_SYNC_RX_MAX_DLY_LWR, 0x1a)
REG16(NPCM_PCS_SR_TIM_SYNC_RX_MAX_DLY_UPR, 0x1c)
REG16(NPCM_PCS_SR_TIM_SYNC_RX_MIN_DLY_LWR, 0x1e)
REG16(NPCM_PCS_SR_TIM_SYNC_RX_MIN_DLY_UPR, 0x20)

REG16(NPCM_PCS_VR_MII_MMD_DIG_CTRL1, 0x000)
REG16(NPCM_PCS_VR_MII_AN_CTRL, 0x002)
REG16(NPCM_PCS_VR_MII_AN_INTR_STS, 0x004)
REG16(NPCM_PCS_VR_MII_TC, 0x006)
REG16(NPCM_PCS_VR_MII_DBG_CTRL, 0x00a)
REG16(NPCM_PCS_VR_MII_EEE_MCTRL0, 0x00c)
REG16(NPCM_PCS_VR_MII_EEE_TXTIMER, 0x010)
REG16(NPCM_PCS_VR_MII_EEE_RXTIMER, 0x012)
REG16(NPCM_PCS_VR_MII_LINK_TIMER_CTRL, 0x014)
REG16(NPCM_PCS_VR_MII_EEE_MCTRL1, 0x016)
REG16(NPCM_PCS_VR_MII_DIG_STS, 0x020)
REG16(NPCM_PCS_VR_MII_ICG_ERRCNT1, 0x022)
REG16(NPCM_PCS_VR_MII_MISC_STS, 0x030)
REG16(NPCM_PCS_VR_MII_RX_LSTS, 0x040)
REG16(NPCM_PCS_VR_MII_MP_TX_BSTCTRL0, 0x070)
REG16(NPCM_PCS_VR_MII_MP_TX_LVLCTRL0, 0x074)
REG16(NPCM_PCS_VR_MII_MP_TX_GENCTRL0, 0x07a)
REG16(NPCM_PCS_VR_MII_MP_TX_GENCTRL1, 0x07c)
REG16(NPCM_PCS_VR_MII_MP_TX_STS, 0x090)
REG16(NPCM_PCS_VR_MII_MP_RX_GENCTRL0, 0x0b0)
REG16(NPCM_PCS_VR_MII_MP_RX_GENCTRL1, 0x0b2)
REG16(NPCM_PCS_VR_MII_MP_RX_LOS_CTRL0, 0x0ba)
REG16(NPCM_PCS_VR_MII_MP_MPLL_CTRL0, 0x0f0)
REG16(NPCM_PCS_VR_MII_MP_MPLL_CTRL1, 0x0f2)
REG16(NPCM_PCS_VR_MII_MP_MPLL_STS, 0x110)
REG16(NPCM_PCS_VR_MII_MP_MISC_CTRL2, 0x126)
REG16(NPCM_PCS_VR_MII_MP_LVL_CTRL, 0x130)
REG16(NPCM_PCS_VR_MII_MP_MISC_CTRL0, 0x132)
REG16(NPCM_PCS_VR_MII_MP_MISC_CTRL1, 0x134)
REG16(NPCM_PCS_VR_MII_DIG_CTRL2, 0x1c2)
REG16(NPCM_PCS_VR_MII_DIG_ERRCNT_SEL, 0x1c4)

/* Register Fields */
#define NPCM_PCS_SR_MII_CTRL_RST            BIT(15)

static const uint16_t npcm_pcs_sr_ctl_cold_reset_values[NPCM_PCS_NR_SR_CTLS] = {
    [R_NPCM_PCS_SR_CTL_ID1]                 = 0x699e,
    [R_NPCM_PCS_SR_CTL_STS]                 = 0x8000,
};

static const uint16_t npcm_pcs_sr_mii_cold_reset_values[NPCM_PCS_NR_SR_MIIS] = {
    [R_NPCM_PCS_SR_MII_CTRL]                = 0x1140,
    [R_NPCM_PCS_SR_MII_STS]                 = 0x0109,
    [R_NPCM_PCS_SR_MII_DEV_ID1]             = 0x699e,
    [R_NPCM_PCS_SR_MII_DEV_ID2]             = 0xced0,
    [R_NPCM_PCS_SR_MII_AN_ADV]              = 0x0020,
    [R_NPCM_PCS_SR_MII_EXT_STS]             = 0xc000,
};

static const uint16_t npcm_pcs_sr_tim_cold_reset_values[NPCM_PCS_NR_SR_TIMS] = {
    [R_NPCM_PCS_SR_TIM_SYNC_ABL]            = 0x0003,
    [R_NPCM_PCS_SR_TIM_SYNC_TX_MAX_DLY_LWR] = 0x0038,
    [R_NPCM_PCS_SR_TIM_SYNC_TX_MIN_DLY_LWR] = 0x0038,
    [R_NPCM_PCS_SR_TIM_SYNC_RX_MAX_DLY_LWR] = 0x0058,
    [R_NPCM_PCS_SR_TIM_SYNC_RX_MIN_DLY_LWR] = 0x0048,
};

static const uint16_t npcm_pcs_vr_mii_cold_reset_values[NPCM_PCS_NR_VR_MIIS] = {
    [R_NPCM_PCS_VR_MII_MMD_DIG_CTRL1]         = 0x2400,
    [R_NPCM_PCS_VR_MII_AN_INTR_STS]           = 0x000a,
    [R_NPCM_PCS_VR_MII_EEE_MCTRL0]            = 0x899c,
    [R_NPCM_PCS_VR_MII_DIG_STS]               = 0x0010,
    [R_NPCM_PCS_VR_MII_MP_TX_BSTCTRL0]        = 0x000a,
    [R_NPCM_PCS_VR_MII_MP_TX_LVLCTRL0]        = 0x007f,
    [R_NPCM_PCS_VR_MII_MP_TX_GENCTRL0]        = 0x0001,
    [R_NPCM_PCS_VR_MII_MP_RX_GENCTRL0]        = 0x0100,
    [R_NPCM_PCS_VR_MII_MP_RX_GENCTRL1]        = 0x1100,
    [R_NPCM_PCS_VR_MII_MP_RX_LOS_CTRL0]       = 0x000e,
    [R_NPCM_PCS_VR_MII_MP_MPLL_CTRL0]         = 0x0100,
    [R_NPCM_PCS_VR_MII_MP_MPLL_CTRL1]         = 0x0032,
    [R_NPCM_PCS_VR_MII_MP_MPLL_STS]           = 0x0001,
    [R_NPCM_PCS_VR_MII_MP_LVL_CTRL]           = 0x0019,
};

static void npcm_pcs_soft_reset(NPCMPCSState *s)
{
    memcpy(s->sr_ctl, npcm_pcs_sr_ctl_cold_reset_values,
           NPCM_PCS_NR_SR_CTLS * sizeof(uint16_t));
    memcpy(s->sr_mii, npcm_pcs_sr_mii_cold_reset_values,
           NPCM_PCS_NR_SR_MIIS * sizeof(uint16_t));
    memcpy(s->sr_tim, npcm_pcs_sr_tim_cold_reset_values,
           NPCM_PCS_NR_SR_TIMS * sizeof(uint16_t));
    memcpy(s->vr_mii, npcm_pcs_vr_mii_cold_reset_values,
           NPCM_PCS_NR_VR_MIIS * sizeof(uint16_t));
}

static uint16_t npcm_pcs_read_sr_ctl(NPCMPCSState *s, hwaddr offset)
{
    hwaddr regno = offset / sizeof(uint16_t);

    if (regno >= NPCM_PCS_NR_SR_CTLS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: SR_CTL read offset 0x%04" HWADDR_PRIx
                      " is out of range.\n",
                      DEVICE(s)->canonical_path, offset);
        return 0;
    }

    return s->sr_ctl[regno];
}

static uint16_t npcm_pcs_read_sr_mii(NPCMPCSState *s, hwaddr offset)
{
    hwaddr regno = offset / sizeof(uint16_t);

    if (regno >= NPCM_PCS_NR_SR_MIIS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: SR_MII read offset 0x%04" HWADDR_PRIx
                      " is out of range.\n",
                      DEVICE(s)->canonical_path, offset);
        return 0;
    }

    return s->sr_mii[regno];
}

static uint16_t npcm_pcs_read_sr_tim(NPCMPCSState *s, hwaddr offset)
{
    hwaddr regno = offset / sizeof(uint16_t);

    if (regno >= NPCM_PCS_NR_SR_TIMS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: SR_TIM read offset 0x%04" HWADDR_PRIx
                      " is out of range.\n",
                      DEVICE(s)->canonical_path, offset);
        return 0;
    }

    return s->sr_tim[regno];
}

static uint16_t npcm_pcs_read_vr_mii(NPCMPCSState *s, hwaddr offset)
{
    hwaddr regno = offset / sizeof(uint16_t);

    if (regno >= NPCM_PCS_NR_VR_MIIS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: VR_MII read offset 0x%04" HWADDR_PRIx
                      " is out of range.\n",
                      DEVICE(s)->canonical_path, offset);
        return 0;
    }

    return s->vr_mii[regno];
}

static void npcm_pcs_write_sr_ctl(NPCMPCSState *s, hwaddr offset, uint16_t v)
{
    hwaddr regno = offset / sizeof(uint16_t);

    if (regno >= NPCM_PCS_NR_SR_CTLS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: SR_CTL write offset 0x%04" HWADDR_PRIx
                      " is out of range.\n",
                      DEVICE(s)->canonical_path, offset);
        return;
    }

    s->sr_ctl[regno] = v;
}

static void npcm_pcs_write_sr_mii(NPCMPCSState *s, hwaddr offset, uint16_t v)
{
    hwaddr regno = offset / sizeof(uint16_t);

    if (regno >= NPCM_PCS_NR_SR_MIIS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: SR_MII write offset 0x%04" HWADDR_PRIx
                      " is out of range.\n",
                      DEVICE(s)->canonical_path, offset);
        return;
    }

    s->sr_mii[regno] = v;

    if ((offset == A_NPCM_PCS_SR_MII_CTRL) && (v & NPCM_PCS_SR_MII_CTRL_RST)) {
        /* Trigger a soft reset */
        npcm_pcs_soft_reset(s);
    }
}

static void npcm_pcs_write_sr_tim(NPCMPCSState *s, hwaddr offset, uint16_t v)
{
    hwaddr regno = offset / sizeof(uint16_t);

    if (regno >= NPCM_PCS_NR_SR_TIMS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: SR_TIM write offset 0x%04" HWADDR_PRIx
                      " is out of range.\n",
                      DEVICE(s)->canonical_path, offset);
        return;
    }

    s->sr_tim[regno] = v;
}

static void npcm_pcs_write_vr_mii(NPCMPCSState *s, hwaddr offset, uint16_t v)
{
    hwaddr regno = offset / sizeof(uint16_t);

    if (regno >= NPCM_PCS_NR_VR_MIIS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: VR_MII write offset 0x%04" HWADDR_PRIx
                      " is out of range.\n",
                      DEVICE(s)->canonical_path, offset);
        return;
    }

    s->vr_mii[regno] = v;
}

static uint64_t npcm_pcs_read(void *opaque, hwaddr offset, unsigned size)
{
    NPCMPCSState *s = opaque;
    uint16_t v = 0;

    if (offset == NPCM_PCS_IND_AC_BA) {
        v = s->indirect_access_base;
    } else {
        switch (s->indirect_access_base) {
        case NPCM_PCS_IND_SR_CTL:
            v = npcm_pcs_read_sr_ctl(s, offset);
            break;

        case NPCM_PCS_IND_SR_MII:
            v = npcm_pcs_read_sr_mii(s, offset);
            break;

        case NPCM_PCS_IND_SR_TIM:
            v = npcm_pcs_read_sr_tim(s, offset);
            break;

        case NPCM_PCS_IND_VR_MII:
            v = npcm_pcs_read_vr_mii(s, offset);
            break;

        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Read with invalid indirect address base: 0x%"
                          PRIx16 "\n", DEVICE(s)->canonical_path,
                          s->indirect_access_base);
        }
    }

    trace_npcm_pcs_reg_read(DEVICE(s)->canonical_path, s->indirect_access_base,
                            offset, v);
    return v;
}

static void npcm_pcs_write(void *opaque, hwaddr offset,
                              uint64_t v, unsigned size)
{
    NPCMPCSState *s = opaque;

    trace_npcm_pcs_reg_write(DEVICE(s)->canonical_path, s->indirect_access_base,
                             offset, v);
    if (offset == NPCM_PCS_IND_AC_BA) {
        s->indirect_access_base = v;
    } else {
        switch (s->indirect_access_base) {
        case NPCM_PCS_IND_SR_CTL:
            npcm_pcs_write_sr_ctl(s, offset, v);
            break;

        case NPCM_PCS_IND_SR_MII:
            npcm_pcs_write_sr_mii(s, offset, v);
            break;

        case NPCM_PCS_IND_SR_TIM:
            npcm_pcs_write_sr_tim(s, offset, v);
            break;

        case NPCM_PCS_IND_VR_MII:
            npcm_pcs_write_vr_mii(s, offset, v);
            break;

        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Write with invalid indirect address base: 0x%02"
                          PRIx16 "\n", DEVICE(s)->canonical_path,
                          s->indirect_access_base);
        }
    }
}

static void npcm_pcs_enter_reset(Object *obj, ResetType type)
{
    NPCMPCSState *s = NPCM_PCS(obj);

    npcm_pcs_soft_reset(s);
}

static const struct MemoryRegionOps npcm_pcs_ops = {
    .read = npcm_pcs_read,
    .write = npcm_pcs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 2,
        .max_access_size = 2,
        .unaligned = false,
    },
};

static void npcm_pcs_realize(DeviceState *dev, Error **errp)
{
    NPCMPCSState *pcs = NPCM_PCS(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&pcs->iomem, OBJECT(pcs), &npcm_pcs_ops, pcs,
                          TYPE_NPCM_PCS, 8 * KiB);
    sysbus_init_mmio(sbd, &pcs->iomem);
}

static const VMStateDescription vmstate_npcm_pcs = {
    .name = TYPE_NPCM_PCS,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(indirect_access_base, NPCMPCSState),
        VMSTATE_UINT16_ARRAY(sr_ctl, NPCMPCSState, NPCM_PCS_NR_SR_CTLS),
        VMSTATE_UINT16_ARRAY(sr_mii, NPCMPCSState, NPCM_PCS_NR_SR_MIIS),
        VMSTATE_UINT16_ARRAY(sr_tim, NPCMPCSState, NPCM_PCS_NR_SR_TIMS),
        VMSTATE_UINT16_ARRAY(vr_mii, NPCMPCSState, NPCM_PCS_NR_VR_MIIS),
        VMSTATE_END_OF_LIST(),
    },
};

static void npcm_pcs_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "NPCM PCS Controller";
    dc->realize = npcm_pcs_realize;
    dc->vmsd = &vmstate_npcm_pcs;
    rc->phases.enter = npcm_pcs_enter_reset;
}

static const TypeInfo npcm_pcs_types[] = {
    {
        .name = TYPE_NPCM_PCS,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(NPCMPCSState),
        .class_init = npcm_pcs_class_init,
    },
};
DEFINE_TYPES(npcm_pcs_types)
