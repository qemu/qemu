/*
 * QEMU PowerPC PowerNV Emulation of some ChipTOD behaviour
 *
 * Copyright (c) 2022-2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ChipTOD (aka TOD) is a facility implemented in the nest / pervasive. The
 * purpose is to keep time-of-day across chips and cores.
 *
 * There is a master chip TOD, which sends signals to slave chip TODs to
 * keep them synchronized. There are two sets of configuration registers
 * called primary and secondary, which can be used fail over.
 *
 * The chip TOD also distributes synchronisation signals to the timebase
 * facility in each of the cores on the chip. In particular there is a
 * feature that can move the TOD value in the ChipTOD to and from the TB.
 *
 * Initialisation typically brings all ChipTOD into sync (see tod_state),
 * and then brings each core TB into sync with the ChipTODs (see timebase
 * state and TFMR). This model is a very basic simulation of the init sequence
 * performed by skiboot.
 */

#include "qemu/osdep.h"
#include "system/reset.h"
#include "target/ppc/cpu.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_chip.h"
#include "hw/ppc/pnv_core.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_chiptod.h"
#include "trace.h"

#include <libfdt.h>

/* TOD chip XSCOM addresses */
#define TOD_M_PATH_CTRL_REG             0x00000000 /* Master Path ctrl reg */
#define TOD_PRI_PORT_0_CTRL_REG         0x00000001 /* Primary port0 ctrl reg */
#define TOD_PRI_PORT_1_CTRL_REG         0x00000002 /* Primary port1 ctrl reg */
#define TOD_SEC_PORT_0_CTRL_REG         0x00000003 /* Secondary p0 ctrl reg */
#define TOD_SEC_PORT_1_CTRL_REG         0x00000004 /* Secondary p1 ctrl reg */
#define TOD_S_PATH_CTRL_REG             0x00000005 /* Slave Path ctrl reg */
#define TOD_I_PATH_CTRL_REG             0x00000006 /* Internal Path ctrl reg */

/* -- TOD primary/secondary master/slave control register -- */
#define TOD_PSS_MSS_CTRL_REG            0x00000007

/* -- TOD primary/secondary master/slave status register -- */
#define TOD_PSS_MSS_STATUS_REG          0x00000008

/* TOD chip XSCOM addresses */
#define TOD_CHIP_CTRL_REG               0x00000010 /* Chip control reg */

#define TOD_TX_TTYPE_0_REG              0x00000011
#define TOD_TX_TTYPE_1_REG              0x00000012 /* PSS switch reg */
#define TOD_TX_TTYPE_2_REG              0x00000013 /* Enable step checkers */
#define TOD_TX_TTYPE_3_REG              0x00000014 /* Request TOD reg */
#define TOD_TX_TTYPE_4_REG              0x00000015 /* Send TOD reg */
#define TOD_TX_TTYPE_5_REG              0x00000016 /* Invalidate TOD reg */

#define TOD_MOVE_TOD_TO_TB_REG          0x00000017
#define TOD_LOAD_TOD_MOD_REG            0x00000018
#define TOD_LOAD_TOD_REG                0x00000021
#define TOD_START_TOD_REG               0x00000022
#define TOD_FSM_REG                     0x00000024

#define TOD_TX_TTYPE_CTRL_REG           0x00000027 /* TX TTYPE Control reg */
#define   TOD_TX_TTYPE_PIB_SLAVE_ADDR      PPC_BITMASK(26, 31)

/* -- TOD Error interrupt register -- */
#define TOD_ERROR_REG                   0x00000030

/* PC unit PIB address which recieves the timebase transfer from TOD */
#define   PC_TOD                        0x4A3

/*
 * The TOD FSM:
 * - The reset state is 0 error.
 * - A hardware error detected will transition to state 0 from any state.
 * - LOAD_TOD_MOD and TTYPE5 will transition to state 7 from any state.
 *
 * | state      | action                       | new |
 * |------------+------------------------------+-----|
 * | 0 error    | LOAD_TOD_MOD                 |  7  |
 * | 0 error    | Recv TTYPE5 (invalidate TOD) |  7  |
 * | 7 not_set  | LOAD_TOD (bit-63 = 0)        |  2  |
 * | 7 not_set  | LOAD_TOD (bit-63 = 1)        |  1  |
 * | 7 not_set  | Recv TTYPE4 (send TOD)       |  2  |
 * | 2 running  |                              |     |
 * | 1 stopped  | START_TOD                    |  2  |
 *
 * Note the hardware has additional states but they relate to the sending
 * and receiving and waiting on synchronisation signals between chips and
 * are not described or modeled here.
 */

static uint64_t pnv_chiptod_xscom_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    PnvChipTOD *chiptod = PNV_CHIPTOD(opaque);
    uint32_t offset = addr >> 3;
    uint64_t val = 0;

    switch (offset) {
    case TOD_PSS_MSS_STATUS_REG:
        /*
         * ChipTOD does not support configurations other than primary
         * master, does not support errors, etc.
         */
        val |= PPC_BITMASK(6, 10); /* STEP checker validity */
        val |= PPC_BIT(12); /* Primary config master path select */
        if (chiptod->tod_state == tod_running) {
            val |= PPC_BIT(20); /* Is running */
        }
        val |= PPC_BIT(21); /* Is using primary config */
        val |= PPC_BIT(26); /* Is using master path select */

        if (chiptod->primary) {
            val |= PPC_BIT(23); /* Is active master */
        } else if (chiptod->secondary) {
            val |= PPC_BIT(24); /* Is backup master */
        } else {
            val |= PPC_BIT(25); /* Is slave (should backup master set this?) */
        }
        break;
    case TOD_PSS_MSS_CTRL_REG:
        val = chiptod->pss_mss_ctrl_reg;
        break;
    case TOD_TX_TTYPE_CTRL_REG:
        val = 0;
        break;
    case TOD_ERROR_REG:
        val = chiptod->tod_error;
        break;
    case TOD_FSM_REG:
        if (chiptod->tod_state == tod_running) {
            val |= PPC_BIT(4);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "pnv_chiptod: unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }

    trace_pnv_chiptod_xscom_read(addr >> 3, val);

    return val;
}

static void chiptod_receive_ttype(PnvChipTOD *chiptod, uint32_t trigger)
{
    switch (trigger) {
    case TOD_TX_TTYPE_4_REG:
        if (chiptod->tod_state != tod_not_set) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: received TTYPE4 in "
                          " state %d, should be in 7 (TOD_NOT_SET)\n",
                          chiptod->tod_state);
        } else {
            chiptod->tod_state = tod_running;
        }
        break;
    case TOD_TX_TTYPE_5_REG:
        /* Works from any state */
        chiptod->tod_state = tod_not_set;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "pnv_chiptod: received unimplemented "
                      " TTYPE %u\n", trigger);
        break;
    }
}

static void chiptod_power9_broadcast_ttype(PnvChipTOD *sender,
                                            uint32_t trigger)
{
    PnvMachineState *pnv = PNV_MACHINE(qdev_get_machine());
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv9Chip *chip9 = PNV9_CHIP(pnv->chips[i]);
        PnvChipTOD *chiptod = &chip9->chiptod;

        if (chiptod != sender) {
            chiptod_receive_ttype(chiptod, trigger);
        }
    }
}

static void chiptod_power10_broadcast_ttype(PnvChipTOD *sender,
                                            uint32_t trigger)
{
    PnvMachineState *pnv = PNV_MACHINE(qdev_get_machine());
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv10Chip *chip10 = PNV10_CHIP(pnv->chips[i]);
        PnvChipTOD *chiptod = &chip10->chiptod;

        if (chiptod != sender) {
            chiptod_receive_ttype(chiptod, trigger);
        }
    }
}

static PnvCore *pnv_chip_get_core_by_xscom_base(PnvChip *chip,
                                                uint32_t xscom_base)
{
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);
    int i;

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pc = chip->cores[i];
        CPUCore *cc = CPU_CORE(pc);
        int core_hwid = cc->core_id;

        if (pcc->xscom_core_base(chip, core_hwid) == xscom_base) {
            return pc;
        }
    }
    return NULL;
}

static PnvCore *chiptod_power9_tx_ttype_target(PnvChipTOD *chiptod,
                                               uint64_t val)
{
    /*
     * skiboot uses Core ID for P9, though SCOM should work too.
     */
    if (val & PPC_BIT(35)) { /* SCOM addressing */
        uint32_t addr = val >> 32;
        uint32_t reg = addr & 0xfff;

        if (reg != PC_TOD) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: SCOM addressing: "
                          "unimplemented slave register 0x%" PRIx32 "\n", reg);
            return NULL;
        }

        return pnv_chip_get_core_by_xscom_base(chiptod->chip, addr & ~0xfff);

    } else { /* Core ID addressing */
        uint32_t core_id = GETFIELD(TOD_TX_TTYPE_PIB_SLAVE_ADDR, val) & 0x1f;
        return pnv_chip_find_core(chiptod->chip, core_id);
    }
}

static PnvCore *chiptod_power10_tx_ttype_target(PnvChipTOD *chiptod,
                                               uint64_t val)
{
    /*
     * skiboot uses SCOM for P10 because Core ID was unable to be made to
     * work correctly. For this reason only SCOM addressing is implemented.
     */
    if (val & PPC_BIT(35)) { /* SCOM addressing */
        uint32_t addr = val >> 32;
        uint32_t reg = addr & 0xfff;

        if (reg != PC_TOD) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: SCOM addressing: "
                          "unimplemented slave register 0x%" PRIx32 "\n", reg);
            return NULL;
        }

        /*
         * This may not deal with P10 big-core addressing at the moment.
         * The big-core code in skiboot syncs small cores, but it targets
         * the even PIR (first small-core) when syncing second small-core.
         */
        return pnv_chip_get_core_by_xscom_base(chiptod->chip, addr & ~0xfff);

    } else { /* Core ID addressing */
        qemu_log_mask(LOG_UNIMP, "pnv_chiptod: TX TTYPE Core ID "
                      "addressing is not implemented for POWER10\n");
        return NULL;
    }
}

static void pnv_chiptod_xscom_write(void *opaque, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    PnvChipTOD *chiptod = PNV_CHIPTOD(opaque);
    PnvChipTODClass *pctc = PNV_CHIPTOD_GET_CLASS(chiptod);
    uint32_t offset = addr >> 3;

    trace_pnv_chiptod_xscom_write(addr >> 3, val);

    switch (offset) {
    case TOD_PSS_MSS_CTRL_REG:
        /* Is this correct? */
        if (chiptod->primary) {
            val |= PPC_BIT(1); /* TOD is master */
        } else {
            val &= ~PPC_BIT(1);
        }
        val |= PPC_BIT(2); /* Drawer is master (don't simulate multi-drawer) */
        chiptod->pss_mss_ctrl_reg = val & PPC_BITMASK(0, 31);
        break;

    case TOD_TX_TTYPE_CTRL_REG:
        /*
         * This register sets the target of the TOD value transfer initiated
         * by TOD_MOVE_TOD_TO_TB. The TOD is able to send the address to
         * any target register, though in practice only the PC TOD register
         * should be used. ChipTOD has a "SCOM addressing" mode which fully
         * specifies the SCOM address, and a core-ID mode which uses the
         * core ID to target the PC TOD for a given core.
         */
        chiptod->slave_pc_target = pctc->tx_ttype_target(chiptod, val);
        if (!chiptod->slave_pc_target) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: xscom write reg"
                          " TOD_TX_TTYPE_CTRL_REG val 0x%" PRIx64
                          " invalid slave address\n", val);
        }
        break;
    case TOD_ERROR_REG:
        chiptod->tod_error &= ~val;
        break;
    case TOD_LOAD_TOD_MOD_REG:
        if (!(val & PPC_BIT(0))) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: xscom write reg"
                          " TOD_LOAD_TOD_MOD_REG with bad val 0x%" PRIx64"\n",
                          val);
        } else {
            chiptod->tod_state = tod_not_set;
        }
        break;
    case TOD_LOAD_TOD_REG:
        if (chiptod->tod_state != tod_not_set) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: LOAD_TOG_REG in "
                          " state %d, should be in 7 (TOD_NOT_SET)\n",
                          chiptod->tod_state);
        } else {
            if (val & PPC_BIT(63)) {
                chiptod->tod_state = tod_stopped;
            } else {
                chiptod->tod_state = tod_running;
            }
        }
        break;

    case TOD_MOVE_TOD_TO_TB_REG:
        /*
         * XXX: it should be a cleaner model to have this drive a SCOM
         * transaction to the target address, and implement the state machine
         * in the PnvCore. For now, this hack makes things work.
         */
        if (chiptod->tod_state != tod_running) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: xscom write reg"
                          " TOD_MOVE_TOD_TO_TB_REG in bad state %d\n",
                          chiptod->tod_state);
        } else if (!(val & PPC_BIT(0))) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: xscom write reg"
                          " TOD_MOVE_TOD_TO_TB_REG with bad val 0x%" PRIx64"\n",
                          val);
        } else if (chiptod->slave_pc_target == NULL) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: xscom write reg"
                          " TOD_MOVE_TOD_TO_TB_REG with no slave target\n");
        } else {
            PnvCore *pc = chiptod->slave_pc_target;

            /*
             * Moving TOD to TB will set the TB of all threads in a
             * core, so skiboot only does this once per thread0, so
             * that is where we keep the timebase state machine.
             *
             * It is likely possible for TBST to be driven from other
             * threads in the core, but for now we only implement it for
             * thread 0.
             */

            if (pc->tod_state.tb_ready_for_tod) {
                pc->tod_state.tod_sent_to_tb = 1;
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: xscom write reg"
                              " TOD_MOVE_TOD_TO_TB_REG with TB not ready to"
                              " receive TOD\n");
            }
        }
        break;
    case TOD_START_TOD_REG:
        if (chiptod->tod_state != tod_stopped) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: LOAD_TOG_REG in "
                          " state %d, should be in 1 (TOD_STOPPED)\n",
                          chiptod->tod_state);
        } else {
            chiptod->tod_state = tod_running;
        }
        break;
    case TOD_TX_TTYPE_4_REG:
    case TOD_TX_TTYPE_5_REG:
        pctc->broadcast_ttype(chiptod, offset);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "pnv_chiptod: unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }
}

static const MemoryRegionOps pnv_chiptod_xscom_ops = {
    .read = pnv_chiptod_xscom_read,
    .write = pnv_chiptod_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static int pnv_chiptod_dt_xscom(PnvXScomInterface *dev, void *fdt,
                                int xscom_offset,
                                const char compat[], size_t compat_size)
{
    PnvChipTOD *chiptod = PNV_CHIPTOD(dev);
    g_autofree char *name = NULL;
    int offset;
    uint32_t chiptod_pcba = PNV9_XSCOM_CHIPTOD_BASE;
    uint32_t reg[] = {
        cpu_to_be32(chiptod_pcba),
        cpu_to_be32(PNV9_XSCOM_CHIPTOD_SIZE)
    };

    name = g_strdup_printf("chiptod@%x", chiptod_pcba);
    offset = fdt_add_subnode(fdt, xscom_offset, name);
    _FDT(offset);

    if (chiptod->primary) {
        _FDT((fdt_setprop(fdt, offset, "primary", NULL, 0)));
    } else if (chiptod->secondary) {
        _FDT((fdt_setprop(fdt, offset, "secondary", NULL, 0)));
    }

    _FDT((fdt_setprop(fdt, offset, "reg", reg, sizeof(reg))));
    _FDT((fdt_setprop(fdt, offset, "compatible", compat, compat_size)));
    return 0;
}

static int pnv_chiptod_power9_dt_xscom(PnvXScomInterface *dev, void *fdt,
                             int xscom_offset)
{
    const char compat[] = "ibm,power-chiptod\0ibm,power9-chiptod";

    return pnv_chiptod_dt_xscom(dev, fdt, xscom_offset, compat, sizeof(compat));
}

static const Property pnv_chiptod_properties[] = {
    DEFINE_PROP_BOOL("primary", PnvChipTOD, primary, false),
    DEFINE_PROP_BOOL("secondary", PnvChipTOD, secondary, false),
    DEFINE_PROP_LINK("chip", PnvChipTOD , chip, TYPE_PNV_CHIP, PnvChip *),
};

static void pnv_chiptod_power9_class_init(ObjectClass *klass, void *data)
{
    PnvChipTODClass *pctc = PNV_CHIPTOD_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xdc = PNV_XSCOM_INTERFACE_CLASS(klass);

    dc->desc = "PowerNV ChipTOD Controller (POWER9)";
    device_class_set_props(dc, pnv_chiptod_properties);

    xdc->dt_xscom = pnv_chiptod_power9_dt_xscom;

    pctc->broadcast_ttype = chiptod_power9_broadcast_ttype;
    pctc->tx_ttype_target = chiptod_power9_tx_ttype_target;

    pctc->xscom_size = PNV_XSCOM_CHIPTOD_SIZE;
}

static const TypeInfo pnv_chiptod_power9_type_info = {
    .name          = TYPE_PNV9_CHIPTOD,
    .parent        = TYPE_PNV_CHIPTOD,
    .instance_size = sizeof(PnvChipTOD),
    .class_init    = pnv_chiptod_power9_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static int pnv_chiptod_power10_dt_xscom(PnvXScomInterface *dev, void *fdt,
                             int xscom_offset)
{
    const char compat[] = "ibm,power-chiptod\0ibm,power10-chiptod";

    return pnv_chiptod_dt_xscom(dev, fdt, xscom_offset, compat, sizeof(compat));
}

static void pnv_chiptod_power10_class_init(ObjectClass *klass, void *data)
{
    PnvChipTODClass *pctc = PNV_CHIPTOD_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xdc = PNV_XSCOM_INTERFACE_CLASS(klass);

    dc->desc = "PowerNV ChipTOD Controller (POWER10)";
    device_class_set_props(dc, pnv_chiptod_properties);

    xdc->dt_xscom = pnv_chiptod_power10_dt_xscom;

    pctc->broadcast_ttype = chiptod_power10_broadcast_ttype;
    pctc->tx_ttype_target = chiptod_power10_tx_ttype_target;

    pctc->xscom_size = PNV_XSCOM_CHIPTOD_SIZE;
}

static const TypeInfo pnv_chiptod_power10_type_info = {
    .name          = TYPE_PNV10_CHIPTOD,
    .parent        = TYPE_PNV_CHIPTOD,
    .instance_size = sizeof(PnvChipTOD),
    .class_init    = pnv_chiptod_power10_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_chiptod_reset(void *dev)
{
    PnvChipTOD *chiptod = PNV_CHIPTOD(dev);

    chiptod->pss_mss_ctrl_reg = 0;
    if (chiptod->primary) {
        chiptod->pss_mss_ctrl_reg |= PPC_BIT(1); /* TOD is master */
    }
    /* Drawer is master (we do not simulate multi-drawer) */
    chiptod->pss_mss_ctrl_reg |= PPC_BIT(2);

    chiptod->tod_error = 0;
    chiptod->tod_state = tod_error;
}

static void pnv_chiptod_realize(DeviceState *dev, Error **errp)
{
    PnvChipTOD *chiptod = PNV_CHIPTOD(dev);
    PnvChipTODClass *pctc = PNV_CHIPTOD_GET_CLASS(chiptod);

    /* XScom regions for ChipTOD registers */
    pnv_xscom_region_init(&chiptod->xscom_regs, OBJECT(dev),
                          &pnv_chiptod_xscom_ops, chiptod, "xscom-chiptod",
                          pctc->xscom_size);

    qemu_register_reset(pnv_chiptod_reset, chiptod);
}

static void pnv_chiptod_unrealize(DeviceState *dev)
{
    PnvChipTOD *chiptod = PNV_CHIPTOD(dev);

    qemu_unregister_reset(pnv_chiptod_reset, chiptod);
}

static void pnv_chiptod_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_chiptod_realize;
    dc->unrealize = pnv_chiptod_unrealize;
    dc->desc = "PowerNV ChipTOD Controller";
    dc->user_creatable = false;
}

static const TypeInfo pnv_chiptod_type_info = {
    .name          = TYPE_PNV_CHIPTOD,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvChipTOD),
    .class_init    = pnv_chiptod_class_init,
    .class_size    = sizeof(PnvChipTODClass),
    .abstract      = true,
};

static void pnv_chiptod_register_types(void)
{
    type_register_static(&pnv_chiptod_type_info);
    type_register_static(&pnv_chiptod_power9_type_info);
    type_register_static(&pnv_chiptod_power10_type_info);
}

type_init(pnv_chiptod_register_types);
