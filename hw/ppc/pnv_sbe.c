/*
 * QEMU PowerPC PowerNV Emulation of some SBE behaviour
 *
 * Copyright (c) 2022, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "target/ppc/cpu.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_sbe.h"
#include "trace.h"

/*
 * Most register and command definitions come from skiboot.
 *
 * xscom addresses are adjusted to be relative to xscom subregion bases
 */

/*
 * SBE MBOX register address
 *   Reg 0 - 3 : Host to send command packets to SBE
 *   Reg 4 - 7 : SBE to send response packets to Host
 */
#define PSU_HOST_SBE_MBOX_REG0          0x00000000
#define PSU_HOST_SBE_MBOX_REG1          0x00000001
#define PSU_HOST_SBE_MBOX_REG2          0x00000002
#define PSU_HOST_SBE_MBOX_REG3          0x00000003
#define PSU_HOST_SBE_MBOX_REG4          0x00000004
#define PSU_HOST_SBE_MBOX_REG5          0x00000005
#define PSU_HOST_SBE_MBOX_REG6          0x00000006
#define PSU_HOST_SBE_MBOX_REG7          0x00000007
#define PSU_SBE_DOORBELL_REG_RW         0x00000010
#define PSU_SBE_DOORBELL_REG_AND        0x00000011
#define PSU_SBE_DOORBELL_REG_OR         0x00000012
#define PSU_HOST_DOORBELL_REG_RW        0x00000013
#define PSU_HOST_DOORBELL_REG_AND       0x00000014
#define PSU_HOST_DOORBELL_REG_OR        0x00000015

/*
 * Doorbell register to trigger SBE interrupt. Set by OPAL to inform
 * the SBE about a waiting message in the Host/SBE mailbox registers
 */
#define HOST_SBE_MSG_WAITING            PPC_BIT(0)

/*
 * Doorbell register for host bridge interrupt. Set by the SBE to inform
 * host about a response message in the Host/SBE mailbox registers
 */
#define SBE_HOST_RESPONSE_WAITING       PPC_BIT(0)
#define SBE_HOST_MSG_READ               PPC_BIT(1)
#define SBE_HOST_STOP15_EXIT            PPC_BIT(2)
#define SBE_HOST_RESET                  PPC_BIT(3)
#define SBE_HOST_PASSTHROUGH            PPC_BIT(4)
#define SBE_HOST_TIMER_EXPIRY           PPC_BIT(14)
#define SBE_HOST_RESPONSE_MASK          (PPC_BITMASK(0, 4) | \
                                         SBE_HOST_TIMER_EXPIRY)

/* SBE Control Register */
#define SBE_CONTROL_REG_RW              0x00000000

/* SBE interrupt s0/s1 bits */
#define SBE_CONTROL_REG_S0              PPC_BIT(14)
#define SBE_CONTROL_REG_S1              PPC_BIT(15)

struct sbe_msg {
    uint64_t reg[4];
};

static uint64_t pnv_sbe_power9_xscom_ctrl_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    uint32_t offset = addr >> 3;
    uint64_t val = 0;

    switch (offset) {
    default:
        qemu_log_mask(LOG_UNIMP, "SBE Unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }

    trace_pnv_sbe_xscom_ctrl_read(addr, val);

    return val;
}

static void pnv_sbe_power9_xscom_ctrl_write(void *opaque, hwaddr addr,
                                       uint64_t val, unsigned size)
{
    uint32_t offset = addr >> 3;

    trace_pnv_sbe_xscom_ctrl_write(addr, val);

    switch (offset) {
    default:
        qemu_log_mask(LOG_UNIMP, "SBE Unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }
}

static const MemoryRegionOps pnv_sbe_power9_xscom_ctrl_ops = {
    .read = pnv_sbe_power9_xscom_ctrl_read,
    .write = pnv_sbe_power9_xscom_ctrl_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_sbe_set_host_doorbell(PnvSBE *sbe, uint64_t val)
{
    val &= SBE_HOST_RESPONSE_MASK; /* Is this right? What does HW do? */
    sbe->host_doorbell = val;

    trace_pnv_sbe_reg_set_host_doorbell(val);
    qemu_set_irq(sbe->psi_irq, !!val);
}

/* SBE Target Type */
#define SBE_TARGET_TYPE_PROC            0x00
#define SBE_TARGET_TYPE_EX              0x01
#define SBE_TARGET_TYPE_PERV            0x02
#define SBE_TARGET_TYPE_MCS             0x03
#define SBE_TARGET_TYPE_EQ              0x04
#define SBE_TARGET_TYPE_CORE            0x05

/* SBE MBOX command class */
#define SBE_MCLASS_FIRST                0xD1
#define SBE_MCLASS_CORE_STATE           0xD1
#define SBE_MCLASS_SCOM                 0xD2
#define SBE_MCLASS_RING                 0xD3
#define SBE_MCLASS_TIMER                0xD4
#define SBE_MCLASS_MPIPL                0xD5
#define SBE_MCLASS_SECURITY             0xD6
#define SBE_MCLASS_GENERIC              0xD7
#define SBE_MCLASS_LAST                 0xD7

/*
 * Commands are provided in xxyy form where:
 *   - xx : command class
 *   - yy : command
 *
 * Both request and response message uses same seq ID,
 * command class and command.
 */
#define SBE_CMD_CTRL_DEADMAN_LOOP       0xD101
#define SBE_CMD_MULTI_SCOM              0xD201
#define SBE_CMD_PUT_RING_FORM_IMAGE     0xD301
#define SBE_CMD_CONTROL_TIMER           0xD401
#define SBE_CMD_GET_ARCHITECTED_REG     0xD501
#define SBE_CMD_CLR_ARCHITECTED_REG     0xD502
#define SBE_CMD_SET_UNSEC_MEM_WINDOW    0xD601
#define SBE_CMD_GET_SBE_FFDC            0xD701
#define SBE_CMD_GET_CAPABILITY          0xD702
#define SBE_CMD_READ_SBE_SEEPROM        0xD703
#define SBE_CMD_SET_FFDC_ADDR           0xD704
#define SBE_CMD_QUIESCE_SBE             0xD705
#define SBE_CMD_SET_FABRIC_ID_MAP       0xD706
#define SBE_CMD_STASH_MPIPL_CONFIG      0xD707

/* SBE MBOX control flags */

/* Generic flags */
#define SBE_CMD_CTRL_RESP_REQ           0x0100
#define SBE_CMD_CTRL_ACK_REQ            0x0200

/* Deadman loop */
#define CTRL_DEADMAN_LOOP_START         0x0001
#define CTRL_DEADMAN_LOOP_STOP          0x0002

/* Control timer */
#define CONTROL_TIMER_START             0x0001
#define CONTROL_TIMER_STOP              0x0002

/* Stash MPIPL config */
#define SBE_STASH_KEY_SKIBOOT_BASE      0x03

static void sbe_timer(void *opaque)
{
    PnvSBE *sbe = opaque;

    trace_pnv_sbe_cmd_timer_expired();

    pnv_sbe_set_host_doorbell(sbe, sbe->host_doorbell | SBE_HOST_TIMER_EXPIRY);
}

static void do_sbe_msg(PnvSBE *sbe)
{
    struct sbe_msg msg;
    uint16_t cmd, ctrl_flags, seq_id;
    int i;

    memset(&msg, 0, sizeof(msg));

    for (i = 0; i < 4; i++) {
        msg.reg[i] = sbe->mbox[i];
    }

    cmd = msg.reg[0];
    seq_id = msg.reg[0] >> 16;
    ctrl_flags = msg.reg[0] >> 32;

    trace_pnv_sbe_msg_recv(cmd, seq_id, ctrl_flags);

    if (ctrl_flags & SBE_CMD_CTRL_ACK_REQ) {
        pnv_sbe_set_host_doorbell(sbe, sbe->host_doorbell | SBE_HOST_MSG_READ);
    }

    switch (cmd) {
    case SBE_CMD_CONTROL_TIMER:
        if (ctrl_flags & CONTROL_TIMER_START) {
            uint64_t us = msg.reg[1];
            trace_pnv_sbe_cmd_timer_start(us);
            timer_mod(sbe->timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + us);
        }
        if (ctrl_flags & CONTROL_TIMER_STOP) {
            trace_pnv_sbe_cmd_timer_stop();
            timer_del(sbe->timer);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "SBE Unimplemented command: 0x%x\n", cmd);
    }
}

static void pnv_sbe_set_sbe_doorbell(PnvSBE *sbe, uint64_t val)
{
    val &= HOST_SBE_MSG_WAITING;
    sbe->sbe_doorbell = val;

    if (val & HOST_SBE_MSG_WAITING) {
        sbe->sbe_doorbell &= ~HOST_SBE_MSG_WAITING;
        do_sbe_msg(sbe);
    }
}

static uint64_t pnv_sbe_power9_xscom_mbox_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    PnvSBE *sbe = PNV_SBE(opaque);
    uint32_t offset = addr >> 3;
    uint64_t val = 0;

    if (offset <= PSU_HOST_SBE_MBOX_REG7) {
        uint32_t idx = offset - PSU_HOST_SBE_MBOX_REG0;
        val = sbe->mbox[idx];
    } else {
        switch (offset) {
        case PSU_SBE_DOORBELL_REG_RW:
            val = sbe->sbe_doorbell;
            break;
        case PSU_HOST_DOORBELL_REG_RW:
            val = sbe->host_doorbell;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "SBE Unimplemented register: Ox%"
                          HWADDR_PRIx "\n", addr >> 3);
        }
    }

    trace_pnv_sbe_xscom_mbox_read(addr, val);

    return val;
}

static void pnv_sbe_power9_xscom_mbox_write(void *opaque, hwaddr addr,
                                       uint64_t val, unsigned size)
{
    PnvSBE *sbe = PNV_SBE(opaque);
    uint32_t offset = addr >> 3;

    trace_pnv_sbe_xscom_mbox_write(addr, val);

    if (offset <= PSU_HOST_SBE_MBOX_REG7) {
        uint32_t idx = offset - PSU_HOST_SBE_MBOX_REG0;
        sbe->mbox[idx] = val;
    } else {
        switch (offset) {
        case PSU_SBE_DOORBELL_REG_RW:
            pnv_sbe_set_sbe_doorbell(sbe, val);
            break;
        case PSU_SBE_DOORBELL_REG_AND:
            pnv_sbe_set_sbe_doorbell(sbe, sbe->sbe_doorbell & val);
            break;
        case PSU_SBE_DOORBELL_REG_OR:
            pnv_sbe_set_sbe_doorbell(sbe, sbe->sbe_doorbell | val);
            break;

        case PSU_HOST_DOORBELL_REG_RW:
            pnv_sbe_set_host_doorbell(sbe, val);
            break;
        case PSU_HOST_DOORBELL_REG_AND:
            pnv_sbe_set_host_doorbell(sbe, sbe->host_doorbell & val);
            break;
        case PSU_HOST_DOORBELL_REG_OR:
            pnv_sbe_set_host_doorbell(sbe, sbe->host_doorbell | val);
            break;

        default:
            qemu_log_mask(LOG_UNIMP, "SBE Unimplemented register: Ox%"
                          HWADDR_PRIx "\n", addr >> 3);
        }
    }
}

static const MemoryRegionOps pnv_sbe_power9_xscom_mbox_ops = {
    .read = pnv_sbe_power9_xscom_mbox_read,
    .write = pnv_sbe_power9_xscom_mbox_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_sbe_power9_class_init(ObjectClass *klass, void *data)
{
    PnvSBEClass *psc = PNV_SBE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PowerNV SBE Controller (POWER9)";
    psc->xscom_ctrl_size = PNV9_XSCOM_SBE_CTRL_SIZE;
    psc->xscom_ctrl_ops = &pnv_sbe_power9_xscom_ctrl_ops;
    psc->xscom_mbox_size = PNV9_XSCOM_SBE_MBOX_SIZE;
    psc->xscom_mbox_ops = &pnv_sbe_power9_xscom_mbox_ops;
}

static const TypeInfo pnv_sbe_power9_type_info = {
    .name          = TYPE_PNV9_SBE,
    .parent        = TYPE_PNV_SBE,
    .instance_size = sizeof(PnvSBE),
    .class_init    = pnv_sbe_power9_class_init,
};

static void pnv_sbe_power10_class_init(ObjectClass *klass, void *data)
{
    PnvSBEClass *psc = PNV_SBE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PowerNV SBE Controller (POWER10)";
    psc->xscom_ctrl_size = PNV10_XSCOM_SBE_CTRL_SIZE;
    psc->xscom_ctrl_ops = &pnv_sbe_power9_xscom_ctrl_ops;
    psc->xscom_mbox_size = PNV10_XSCOM_SBE_MBOX_SIZE;
    psc->xscom_mbox_ops = &pnv_sbe_power9_xscom_mbox_ops;
}

static const TypeInfo pnv_sbe_power10_type_info = {
    .name          = TYPE_PNV10_SBE,
    .parent        = TYPE_PNV9_SBE,
    .class_init    = pnv_sbe_power10_class_init,
};

static void pnv_sbe_realize(DeviceState *dev, Error **errp)
{
    PnvSBE *sbe = PNV_SBE(dev);
    PnvSBEClass *psc = PNV_SBE_GET_CLASS(sbe);

    /* XScom regions for SBE registers */
    pnv_xscom_region_init(&sbe->xscom_ctrl_regs, OBJECT(dev),
                          psc->xscom_ctrl_ops, sbe, "xscom-sbe-ctrl",
                          psc->xscom_ctrl_size);
    pnv_xscom_region_init(&sbe->xscom_mbox_regs, OBJECT(dev),
                          psc->xscom_mbox_ops, sbe, "xscom-sbe-mbox",
                          psc->xscom_mbox_size);

    qdev_init_gpio_out(dev, &sbe->psi_irq, 1);

    sbe->timer = timer_new_us(QEMU_CLOCK_VIRTUAL, sbe_timer, sbe);
}

static void pnv_sbe_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_sbe_realize;
    dc->desc = "PowerNV SBE Controller";
    dc->user_creatable = false;
}

static const TypeInfo pnv_sbe_type_info = {
    .name          = TYPE_PNV_SBE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvSBE),
    .class_init    = pnv_sbe_class_init,
    .class_size    = sizeof(PnvSBEClass),
    .abstract      = true,
};

static void pnv_sbe_register_types(void)
{
    type_register_static(&pnv_sbe_type_info);
    type_register_static(&pnv_sbe_power9_type_info);
    type_register_static(&pnv_sbe_power10_type_info);
}

type_init(pnv_sbe_register_types);
