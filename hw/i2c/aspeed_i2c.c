/*
 * ARM Aspeed I2C controller
 *
 * Copyright (C) 2016 IBM Corp.
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
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "hw/i2c/aspeed_i2c.h"

/* I2C Global Register */

#define I2C_CTRL_STATUS         0x00        /* Device Interrupt Status */
#define I2C_CTRL_ASSIGN         0x08        /* Device Interrupt Target
                                               Assignment */

/* I2C Device (Bus) Register */

#define I2CD_FUN_CTRL_REG       0x00       /* I2CD Function Control  */
#define   I2CD_BUFF_SEL_MASK               (0x7 << 20)
#define   I2CD_BUFF_SEL(x)                 (x << 20)
#define   I2CD_M_SDA_LOCK_EN               (0x1 << 16)
#define   I2CD_MULTI_MASTER_DIS            (0x1 << 15)
#define   I2CD_M_SCL_DRIVE_EN              (0x1 << 14)
#define   I2CD_MSB_STS                     (0x1 << 9)
#define   I2CD_SDA_DRIVE_1T_EN             (0x1 << 8)
#define   I2CD_M_SDA_DRIVE_1T_EN           (0x1 << 7)
#define   I2CD_M_HIGH_SPEED_EN             (0x1 << 6)
#define   I2CD_DEF_ADDR_EN                 (0x1 << 5)
#define   I2CD_DEF_ALERT_EN                (0x1 << 4)
#define   I2CD_DEF_ARP_EN                  (0x1 << 3)
#define   I2CD_DEF_GCALL_EN                (0x1 << 2)
#define   I2CD_SLAVE_EN                    (0x1 << 1)
#define   I2CD_MASTER_EN                   (0x1)

#define I2CD_AC_TIMING_REG1     0x04       /* Clock and AC Timing Control #1 */
#define I2CD_AC_TIMING_REG2     0x08       /* Clock and AC Timing Control #1 */
#define I2CD_INTR_CTRL_REG      0x0c       /* I2CD Interrupt Control */
#define I2CD_INTR_STS_REG       0x10       /* I2CD Interrupt Status */

#define   I2CD_INTR_SLAVE_ADDR_MATCH       (0x1 << 31) /* 0: addr1 1: addr2 */
#define   I2CD_INTR_SLAVE_ADDR_RX_PENDING  (0x1 << 30)
/* bits[19-16] Reserved */

/* All bits below are cleared by writing 1 */
#define   I2CD_INTR_SLAVE_INACTIVE_TIMEOUT (0x1 << 15)
#define   I2CD_INTR_SDA_DL_TIMEOUT         (0x1 << 14)
#define   I2CD_INTR_BUS_RECOVER_DONE       (0x1 << 13)
#define   I2CD_INTR_SMBUS_ALERT            (0x1 << 12) /* Bus [0-3] only */
#define   I2CD_INTR_SMBUS_ARP_ADDR         (0x1 << 11) /* Removed */
#define   I2CD_INTR_SMBUS_DEV_ALERT_ADDR   (0x1 << 10) /* Removed */
#define   I2CD_INTR_SMBUS_DEF_ADDR         (0x1 << 9)  /* Removed */
#define   I2CD_INTR_GCALL_ADDR             (0x1 << 8)  /* Removed */
#define   I2CD_INTR_SLAVE_ADDR_RX_MATCH    (0x1 << 7)  /* use RX_DONE */
#define   I2CD_INTR_SCL_TIMEOUT            (0x1 << 6)
#define   I2CD_INTR_ABNORMAL               (0x1 << 5)
#define   I2CD_INTR_NORMAL_STOP            (0x1 << 4)
#define   I2CD_INTR_ARBIT_LOSS             (0x1 << 3)
#define   I2CD_INTR_RX_DONE                (0x1 << 2)
#define   I2CD_INTR_TX_NAK                 (0x1 << 1)
#define   I2CD_INTR_TX_ACK                 (0x1 << 0)

#define I2CD_CMD_REG            0x14       /* I2CD Command/Status */
#define   I2CD_SDA_OE                      (0x1 << 28)
#define   I2CD_SDA_O                       (0x1 << 27)
#define   I2CD_SCL_OE                      (0x1 << 26)
#define   I2CD_SCL_O                       (0x1 << 25)
#define   I2CD_TX_TIMING                   (0x1 << 24)
#define   I2CD_TX_STATUS                   (0x1 << 23)

#define   I2CD_TX_STATE_SHIFT              19 /* Tx State Machine */
#define   I2CD_TX_STATE_MASK                  0xf
#define     I2CD_IDLE                         0x0
#define     I2CD_MACTIVE                      0x8
#define     I2CD_MSTART                       0x9
#define     I2CD_MSTARTR                      0xa
#define     I2CD_MSTOP                        0xb
#define     I2CD_MTXD                         0xc
#define     I2CD_MRXACK                       0xd
#define     I2CD_MRXD                         0xe
#define     I2CD_MTXACK                       0xf
#define     I2CD_SWAIT                        0x1
#define     I2CD_SRXD                         0x4
#define     I2CD_STXACK                       0x5
#define     I2CD_STXD                         0x6
#define     I2CD_SRXACK                       0x7
#define     I2CD_RECOVER                      0x3

#define   I2CD_SCL_LINE_STS                (0x1 << 18)
#define   I2CD_SDA_LINE_STS                (0x1 << 17)
#define   I2CD_BUS_BUSY_STS                (0x1 << 16)
#define   I2CD_SDA_OE_OUT_DIR              (0x1 << 15)
#define   I2CD_SDA_O_OUT_DIR               (0x1 << 14)
#define   I2CD_SCL_OE_OUT_DIR              (0x1 << 13)
#define   I2CD_SCL_O_OUT_DIR               (0x1 << 12)
#define   I2CD_BUS_RECOVER_CMD_EN          (0x1 << 11)
#define   I2CD_S_ALT_EN                    (0x1 << 10)
#define   I2CD_RX_DMA_ENABLE               (0x1 << 9)
#define   I2CD_TX_DMA_ENABLE               (0x1 << 8)

/* Command Bit */
#define   I2CD_M_STOP_CMD                  (0x1 << 5)
#define   I2CD_M_S_RX_CMD_LAST             (0x1 << 4)
#define   I2CD_M_RX_CMD                    (0x1 << 3)
#define   I2CD_S_TX_CMD                    (0x1 << 2)
#define   I2CD_M_TX_CMD                    (0x1 << 1)
#define   I2CD_M_START_CMD                 (0x1)

#define I2CD_DEV_ADDR_REG       0x18       /* Slave Device Address */
#define I2CD_BUF_CTRL_REG       0x1c       /* Pool Buffer Control */
#define I2CD_BYTE_BUF_REG       0x20       /* Transmit/Receive Byte Buffer */
#define   I2CD_BYTE_BUF_TX_SHIFT           0
#define   I2CD_BYTE_BUF_TX_MASK            0xff
#define   I2CD_BYTE_BUF_RX_SHIFT           8
#define   I2CD_BYTE_BUF_RX_MASK            0xff


static inline bool aspeed_i2c_bus_is_master(AspeedI2CBus *bus)
{
    return bus->ctrl & I2CD_MASTER_EN;
}

static inline bool aspeed_i2c_bus_is_enabled(AspeedI2CBus *bus)
{
    return bus->ctrl & (I2CD_MASTER_EN | I2CD_SLAVE_EN);
}

static inline void aspeed_i2c_bus_raise_interrupt(AspeedI2CBus *bus)
{
    bus->intr_status &= bus->intr_ctrl;
    if (bus->intr_status) {
        bus->controller->intr_status |= 1 << bus->id;
        qemu_irq_raise(bus->controller->irq);
    }
}

static uint64_t aspeed_i2c_bus_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    AspeedI2CBus *bus = opaque;

    switch (offset) {
    case I2CD_FUN_CTRL_REG:
        return bus->ctrl;
    case I2CD_AC_TIMING_REG1:
        return bus->timing[0];
    case I2CD_AC_TIMING_REG2:
        return bus->timing[1];
    case I2CD_INTR_CTRL_REG:
        return bus->intr_ctrl;
    case I2CD_INTR_STS_REG:
        return bus->intr_status;
    case I2CD_BYTE_BUF_REG:
        return bus->buf;
    case I2CD_CMD_REG:
        return bus->cmd | (i2c_bus_busy(bus->bus) << 16);
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, offset);
        return -1;
    }
}

static void aspeed_i2c_set_state(AspeedI2CBus *bus, uint8_t state)
{
    bus->cmd &= ~(I2CD_TX_STATE_MASK << I2CD_TX_STATE_SHIFT);
    bus->cmd |= (state & I2CD_TX_STATE_MASK) << I2CD_TX_STATE_SHIFT;
}

static uint8_t aspeed_i2c_get_state(AspeedI2CBus *bus)
{
    return (bus->cmd >> I2CD_TX_STATE_SHIFT) & I2CD_TX_STATE_MASK;
}

static void aspeed_i2c_handle_rx_cmd(AspeedI2CBus *bus)
{
    int ret;

    aspeed_i2c_set_state(bus, I2CD_MRXD);
    ret = i2c_recv(bus->bus);
    if (ret < 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read failed\n", __func__);
        ret = 0xff;
    } else {
        bus->intr_status |= I2CD_INTR_RX_DONE;
    }
    bus->buf = (ret & I2CD_BYTE_BUF_RX_MASK) << I2CD_BYTE_BUF_RX_SHIFT;
    if (bus->cmd & I2CD_M_S_RX_CMD_LAST) {
        i2c_nack(bus->bus);
    }
    bus->cmd &= ~(I2CD_M_RX_CMD | I2CD_M_S_RX_CMD_LAST);
    aspeed_i2c_set_state(bus, I2CD_MACTIVE);
}

/*
 * The state machine needs some refinement. It is only used to track
 * invalid STOP commands for the moment.
 */
static void aspeed_i2c_bus_handle_cmd(AspeedI2CBus *bus, uint64_t value)
{
    bus->cmd &= ~0xFFFF;
    bus->cmd |= value & 0xFFFF;

    if (bus->cmd & I2CD_M_START_CMD) {
        uint8_t state = aspeed_i2c_get_state(bus) & I2CD_MACTIVE ?
            I2CD_MSTARTR : I2CD_MSTART;

        aspeed_i2c_set_state(bus, state);

        if (i2c_start_transfer(bus->bus, extract32(bus->buf, 1, 7),
                               extract32(bus->buf, 0, 1))) {
            bus->intr_status |= I2CD_INTR_TX_NAK;
        } else {
            bus->intr_status |= I2CD_INTR_TX_ACK;
        }

        /* START command is also a TX command, as the slave address is
         * sent on the bus */
        bus->cmd &= ~(I2CD_M_START_CMD | I2CD_M_TX_CMD);

        /* No slave found */
        if (!i2c_bus_busy(bus->bus)) {
            return;
        }
        aspeed_i2c_set_state(bus, I2CD_MACTIVE);
    }

    if (bus->cmd & I2CD_M_TX_CMD) {
        aspeed_i2c_set_state(bus, I2CD_MTXD);
        if (i2c_send(bus->bus, bus->buf)) {
            bus->intr_status |= (I2CD_INTR_TX_NAK);
            i2c_end_transfer(bus->bus);
        } else {
            bus->intr_status |= I2CD_INTR_TX_ACK;
        }
        bus->cmd &= ~I2CD_M_TX_CMD;
        aspeed_i2c_set_state(bus, I2CD_MACTIVE);
    }

    if ((bus->cmd & (I2CD_M_RX_CMD | I2CD_M_S_RX_CMD_LAST)) &&
        !(bus->intr_status & I2CD_INTR_RX_DONE)) {
        aspeed_i2c_handle_rx_cmd(bus);
    }

    if (bus->cmd & I2CD_M_STOP_CMD) {
        if (!(aspeed_i2c_get_state(bus) & I2CD_MACTIVE)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: abnormal stop\n", __func__);
            bus->intr_status |= I2CD_INTR_ABNORMAL;
        } else {
            aspeed_i2c_set_state(bus, I2CD_MSTOP);
            i2c_end_transfer(bus->bus);
            bus->intr_status |= I2CD_INTR_NORMAL_STOP;
        }
        bus->cmd &= ~I2CD_M_STOP_CMD;
        aspeed_i2c_set_state(bus, I2CD_IDLE);
    }
}

static void aspeed_i2c_bus_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    AspeedI2CBus *bus = opaque;
    bool handle_rx;

    switch (offset) {
    case I2CD_FUN_CTRL_REG:
        if (value & I2CD_SLAVE_EN) {
            qemu_log_mask(LOG_UNIMP, "%s: slave mode not implemented\n",
                          __func__);
            break;
        }
        bus->ctrl = value & 0x0071C3FF;
        break;
    case I2CD_AC_TIMING_REG1:
        bus->timing[0] = value & 0xFFFFF0F;
        break;
    case I2CD_AC_TIMING_REG2:
        bus->timing[1] = value & 0x7;
        break;
    case I2CD_INTR_CTRL_REG:
        bus->intr_ctrl = value & 0x7FFF;
        break;
    case I2CD_INTR_STS_REG:
        handle_rx = (bus->intr_status & I2CD_INTR_RX_DONE) &&
                (value & I2CD_INTR_RX_DONE);
        bus->intr_status &= ~(value & 0x7FFF);
        if (!bus->intr_status) {
            bus->controller->intr_status &= ~(1 << bus->id);
            qemu_irq_lower(bus->controller->irq);
        }
        if (handle_rx && (bus->cmd & (I2CD_M_RX_CMD | I2CD_M_S_RX_CMD_LAST))) {
            aspeed_i2c_handle_rx_cmd(bus);
            aspeed_i2c_bus_raise_interrupt(bus);
        }
        break;
    case I2CD_DEV_ADDR_REG:
        qemu_log_mask(LOG_UNIMP, "%s: slave mode not implemented\n",
                      __func__);
        break;
    case I2CD_BYTE_BUF_REG:
        bus->buf = (value & I2CD_BYTE_BUF_TX_MASK) << I2CD_BYTE_BUF_TX_SHIFT;
        break;
    case I2CD_CMD_REG:
        if (!aspeed_i2c_bus_is_enabled(bus)) {
            break;
        }

        if (!aspeed_i2c_bus_is_master(bus)) {
            qemu_log_mask(LOG_UNIMP, "%s: slave mode not implemented\n",
                          __func__);
            break;
        }

        aspeed_i2c_bus_handle_cmd(bus, value);
        aspeed_i2c_bus_raise_interrupt(bus);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }
}

static uint64_t aspeed_i2c_ctrl_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    AspeedI2CState *s = opaque;

    switch (offset) {
    case I2C_CTRL_STATUS:
        return s->intr_status;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }

    return -1;
}

static void aspeed_i2c_ctrl_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    switch (offset) {
    case I2C_CTRL_STATUS:
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }
}

static const MemoryRegionOps aspeed_i2c_bus_ops = {
    .read = aspeed_i2c_bus_read,
    .write = aspeed_i2c_bus_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps aspeed_i2c_ctrl_ops = {
    .read = aspeed_i2c_ctrl_read,
    .write = aspeed_i2c_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const VMStateDescription aspeed_i2c_bus_vmstate = {
    .name = TYPE_ASPEED_I2C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(id, AspeedI2CBus),
        VMSTATE_UINT32(ctrl, AspeedI2CBus),
        VMSTATE_UINT32_ARRAY(timing, AspeedI2CBus, 2),
        VMSTATE_UINT32(intr_ctrl, AspeedI2CBus),
        VMSTATE_UINT32(intr_status, AspeedI2CBus),
        VMSTATE_UINT32(cmd, AspeedI2CBus),
        VMSTATE_UINT32(buf, AspeedI2CBus),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription aspeed_i2c_vmstate = {
    .name = TYPE_ASPEED_I2C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(intr_status, AspeedI2CState),
        VMSTATE_STRUCT_ARRAY(busses, AspeedI2CState,
                             ASPEED_I2C_NR_BUSSES, 1, aspeed_i2c_bus_vmstate,
                             AspeedI2CBus),
        VMSTATE_END_OF_LIST()
    }
};

static void aspeed_i2c_reset(DeviceState *dev)
{
    int i;
    AspeedI2CState *s = ASPEED_I2C(dev);

    s->intr_status = 0;

    for (i = 0; i < ASPEED_I2C_NR_BUSSES; i++) {
        s->busses[i].intr_ctrl = 0;
        s->busses[i].intr_status = 0;
        s->busses[i].cmd = 0;
        s->busses[i].buf = 0;
        i2c_end_transfer(s->busses[i].bus);
    }
}

/*
 * Address Definitions
 *
 *   0x000 ... 0x03F: Global Register
 *   0x040 ... 0x07F: Device 1
 *   0x080 ... 0x0BF: Device 2
 *   0x0C0 ... 0x0FF: Device 3
 *   0x100 ... 0x13F: Device 4
 *   0x140 ... 0x17F: Device 5
 *   0x180 ... 0x1BF: Device 6
 *   0x1C0 ... 0x1FF: Device 7
 *   0x200 ... 0x2FF: Buffer Pool  (unused in linux driver)
 *   0x300 ... 0x33F: Device 8
 *   0x340 ... 0x37F: Device 9
 *   0x380 ... 0x3BF: Device 10
 *   0x3C0 ... 0x3FF: Device 11
 *   0x400 ... 0x43F: Device 12
 *   0x440 ... 0x47F: Device 13
 *   0x480 ... 0x4BF: Device 14
 *   0x800 ... 0xFFF: Buffer Pool  (unused in linux driver)
 */
static void aspeed_i2c_realize(DeviceState *dev, Error **errp)
{
    int i;
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedI2CState *s = ASPEED_I2C(dev);

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_i2c_ctrl_ops, s,
                          "aspeed.i2c", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    for (i = 0; i < ASPEED_I2C_NR_BUSSES; i++) {
        char name[16];
        int offset = i < 7 ? 1 : 5;
        snprintf(name, sizeof(name), "aspeed.i2c.%d", i);
        s->busses[i].controller = s;
        s->busses[i].id = i;
        s->busses[i].bus = i2c_init_bus(dev, name);
        memory_region_init_io(&s->busses[i].mr, OBJECT(dev),
                              &aspeed_i2c_bus_ops, &s->busses[i], name, 0x40);
        memory_region_add_subregion(&s->iomem, 0x40 * (i + offset),
                                    &s->busses[i].mr);
    }
}

static void aspeed_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &aspeed_i2c_vmstate;
    dc->reset = aspeed_i2c_reset;
    dc->realize = aspeed_i2c_realize;
    dc->desc = "Aspeed I2C Controller";
}

static const TypeInfo aspeed_i2c_info = {
    .name          = TYPE_ASPEED_I2C,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedI2CState),
    .class_init    = aspeed_i2c_class_init,
};

static void aspeed_i2c_register_types(void)
{
    type_register_static(&aspeed_i2c_info);
}

type_init(aspeed_i2c_register_types)


I2CBus *aspeed_i2c_get_bus(DeviceState *dev, int busnr)
{
    AspeedI2CState *s = ASPEED_I2C(dev);
    I2CBus *bus = NULL;

    if (busnr >= 0 && busnr < ASPEED_I2C_NR_BUSSES) {
        bus = s->busses[busnr].bus;
    }

    return bus;
}
