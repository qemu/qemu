/*
 * Raspberry Pi (BCM2835) SD Host Controller
 *
 * Copyright (c) 2017 Antfield SAS
 *
 * Authors:
 *  Clement Deschamps <clement.deschamps@antfield.fr>
 *  Luc Michel <luc.michel@antfield.fr>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sysemu/blockdev.h"
#include "hw/irq.h"
#include "hw/sd/bcm2835_sdhost.h"
#include "migration/vmstate.h"
#include "trace.h"

#define TYPE_BCM2835_SDHOST_BUS "bcm2835-sdhost-bus"
#define BCM2835_SDHOST_BUS(obj) \
    OBJECT_CHECK(SDBus, (obj), TYPE_BCM2835_SDHOST_BUS)

#define SDCMD  0x00 /* Command to SD card              - 16 R/W */
#define SDARG  0x04 /* Argument to SD card             - 32 R/W */
#define SDTOUT 0x08 /* Start value for timeout counter - 32 R/W */
#define SDCDIV 0x0c /* Start value for clock divider   - 11 R/W */
#define SDRSP0 0x10 /* SD card rsp (31:0)         - 32 R   */
#define SDRSP1 0x14 /* SD card rsp (63:32)        - 32 R   */
#define SDRSP2 0x18 /* SD card rsp (95:64)        - 32 R   */
#define SDRSP3 0x1c /* SD card rsp (127:96)       - 32 R   */
#define SDHSTS 0x20 /* SD host status                  - 11 R   */
#define SDVDD  0x30 /* SD card power control           -  1 R/W */
#define SDEDM  0x34 /* Emergency Debug Mode            - 13 R/W */
#define SDHCFG 0x38 /* Host configuration              -  2 R/W */
#define SDHBCT 0x3c /* Host byte count (debug)         - 32 R/W */
#define SDDATA 0x40 /* Data to/from SD card            - 32 R/W */
#define SDHBLC 0x50 /* Host block count (SDIO/SDHC)    -  9 R/W */

#define SDCMD_NEW_FLAG                  0x8000
#define SDCMD_FAIL_FLAG                 0x4000
#define SDCMD_BUSYWAIT                  0x800
#define SDCMD_NO_RESPONSE               0x400
#define SDCMD_LONG_RESPONSE             0x200
#define SDCMD_WRITE_CMD                 0x80
#define SDCMD_READ_CMD                  0x40
#define SDCMD_CMD_MASK                  0x3f

#define SDCDIV_MAX_CDIV                 0x7ff

#define SDHSTS_BUSY_IRPT                0x400
#define SDHSTS_BLOCK_IRPT               0x200
#define SDHSTS_SDIO_IRPT                0x100
#define SDHSTS_REW_TIME_OUT             0x80
#define SDHSTS_CMD_TIME_OUT             0x40
#define SDHSTS_CRC16_ERROR              0x20
#define SDHSTS_CRC7_ERROR               0x10
#define SDHSTS_FIFO_ERROR               0x08
/* Reserved */
/* Reserved */
#define SDHSTS_DATA_FLAG                0x01

#define SDHCFG_BUSY_IRPT_EN     (1 << 10)
#define SDHCFG_BLOCK_IRPT_EN    (1 << 8)
#define SDHCFG_SDIO_IRPT_EN     (1 << 5)
#define SDHCFG_DATA_IRPT_EN     (1 << 4)
#define SDHCFG_SLOW_CARD        (1 << 3)
#define SDHCFG_WIDE_EXT_BUS     (1 << 2)
#define SDHCFG_WIDE_INT_BUS     (1 << 1)
#define SDHCFG_REL_CMD_LINE     (1 << 0)

#define SDEDM_FORCE_DATA_MODE   (1 << 19)
#define SDEDM_CLOCK_PULSE       (1 << 20)
#define SDEDM_BYPASS            (1 << 21)

#define SDEDM_WRITE_THRESHOLD_SHIFT 9
#define SDEDM_READ_THRESHOLD_SHIFT 14
#define SDEDM_THRESHOLD_MASK     0x1f

#define SDEDM_FSM_MASK           0xf
#define SDEDM_FSM_IDENTMODE      0x0
#define SDEDM_FSM_DATAMODE       0x1
#define SDEDM_FSM_READDATA       0x2
#define SDEDM_FSM_WRITEDATA      0x3
#define SDEDM_FSM_READWAIT       0x4
#define SDEDM_FSM_READCRC        0x5
#define SDEDM_FSM_WRITECRC       0x6
#define SDEDM_FSM_WRITEWAIT1     0x7
#define SDEDM_FSM_POWERDOWN      0x8
#define SDEDM_FSM_POWERUP        0x9
#define SDEDM_FSM_WRITESTART1    0xa
#define SDEDM_FSM_WRITESTART2    0xb
#define SDEDM_FSM_GENPULSES      0xc
#define SDEDM_FSM_WRITEWAIT2     0xd
#define SDEDM_FSM_STARTPOWDOWN   0xf

#define SDDATA_FIFO_WORDS        16

static void bcm2835_sdhost_update_irq(BCM2835SDHostState *s)
{
    uint32_t irq = s->status &
        (SDHSTS_BUSY_IRPT | SDHSTS_BLOCK_IRPT | SDHSTS_SDIO_IRPT);
    trace_bcm2835_sdhost_update_irq(irq);
    qemu_set_irq(s->irq, !!irq);
}

static void bcm2835_sdhost_send_command(BCM2835SDHostState *s)
{
    SDRequest request;
    uint8_t rsp[16];
    int rlen;

    request.cmd = s->cmd & SDCMD_CMD_MASK;
    request.arg = s->cmdarg;

    rlen = sdbus_do_command(&s->sdbus, &request, rsp);
    if (rlen < 0) {
        goto error;
    }
    if (!(s->cmd & SDCMD_NO_RESPONSE)) {
        if (rlen == 0 || (rlen == 4 && (s->cmd & SDCMD_LONG_RESPONSE))) {
            goto error;
        }
        if (rlen != 4 && rlen != 16) {
            goto error;
        }
        if (rlen == 4) {
            s->rsp[0] = ldl_be_p(&rsp[0]);
            s->rsp[1] = s->rsp[2] = s->rsp[3] = 0;
        } else {
            s->rsp[0] = ldl_be_p(&rsp[12]);
            s->rsp[1] = ldl_be_p(&rsp[8]);
            s->rsp[2] = ldl_be_p(&rsp[4]);
            s->rsp[3] = ldl_be_p(&rsp[0]);
        }
    }
    /* We never really delay commands, so if this was a 'busywait' command
     * then we've completed it now and can raise the interrupt.
     */
    if ((s->cmd & SDCMD_BUSYWAIT) && (s->config & SDHCFG_BUSY_IRPT_EN)) {
        s->status |= SDHSTS_BUSY_IRPT;
    }
    return;

error:
    s->cmd |= SDCMD_FAIL_FLAG;
    s->status |= SDHSTS_CMD_TIME_OUT;
}

static void bcm2835_sdhost_fifo_push(BCM2835SDHostState *s, uint32_t value)
{
    int n;

    if (s->fifo_len == BCM2835_SDHOST_FIFO_LEN) {
        /* FIFO overflow */
        return;
    }
    n = (s->fifo_pos + s->fifo_len) & (BCM2835_SDHOST_FIFO_LEN - 1);
    s->fifo_len++;
    s->fifo[n] = value;
}

static uint32_t bcm2835_sdhost_fifo_pop(BCM2835SDHostState *s)
{
    uint32_t value;

    if (s->fifo_len == 0) {
        /* FIFO underflow */
        return 0;
    }
    value = s->fifo[s->fifo_pos];
    s->fifo_len--;
    s->fifo_pos = (s->fifo_pos + 1) & (BCM2835_SDHOST_FIFO_LEN - 1);
    return value;
}

static void bcm2835_sdhost_fifo_run(BCM2835SDHostState *s)
{
    uint32_t value = 0;
    int n;
    int is_read;
    int is_write;

    is_read = (s->cmd & SDCMD_READ_CMD) != 0;
    is_write = (s->cmd & SDCMD_WRITE_CMD) != 0;
    if (s->datacnt != 0 && (is_write || sdbus_data_ready(&s->sdbus))) {
        if (is_read) {
            n = 0;
            while (s->datacnt && s->fifo_len < BCM2835_SDHOST_FIFO_LEN) {
                value |= (uint32_t)sdbus_read_data(&s->sdbus) << (n * 8);
                s->datacnt--;
                n++;
                if (n == 4) {
                    bcm2835_sdhost_fifo_push(s, value);
                    s->status |= SDHSTS_DATA_FLAG;
                    if (s->config & SDHCFG_DATA_IRPT_EN) {
                        s->status |= SDHSTS_SDIO_IRPT;
                    }
                    n = 0;
                    value = 0;
                }
            }
            if (n != 0) {
                bcm2835_sdhost_fifo_push(s, value);
                s->status |= SDHSTS_DATA_FLAG;
                if (s->config & SDHCFG_DATA_IRPT_EN) {
                    s->status |= SDHSTS_SDIO_IRPT;
                }
            }
        } else if (is_write) { /* write */
            n = 0;
            while (s->datacnt > 0 && (s->fifo_len > 0 || n > 0)) {
                if (n == 0) {
                    value = bcm2835_sdhost_fifo_pop(s);
                    s->status |= SDHSTS_DATA_FLAG;
                    if (s->config & SDHCFG_DATA_IRPT_EN) {
                        s->status |= SDHSTS_SDIO_IRPT;
                    }
                    n = 4;
                }
                n--;
                s->datacnt--;
                sdbus_write_data(&s->sdbus, value & 0xff);
                value >>= 8;
            }
        }
        if (s->datacnt == 0) {
            s->edm &= ~SDEDM_FSM_MASK;
            s->edm |= SDEDM_FSM_DATAMODE;
            trace_bcm2835_sdhost_edm_change("datacnt 0", s->edm);
        }
        if (is_write) {
            /* set block interrupt at end of each block transfer */
            if (s->hbct && s->datacnt % s->hbct == 0 &&
                (s->config & SDHCFG_BLOCK_IRPT_EN)) {
                s->status |= SDHSTS_BLOCK_IRPT;
            }
            /* set data interrupt after each transfer */
            s->status |= SDHSTS_DATA_FLAG;
            if (s->config & SDHCFG_DATA_IRPT_EN) {
                s->status |= SDHSTS_SDIO_IRPT;
            }
        }
    }

    bcm2835_sdhost_update_irq(s);

    s->edm &= ~(0x1f << 4);
    s->edm |= ((s->fifo_len & 0x1f) << 4);
    trace_bcm2835_sdhost_edm_change("fifo run", s->edm);
}

static uint64_t bcm2835_sdhost_read(void *opaque, hwaddr offset,
    unsigned size)
{
    BCM2835SDHostState *s = (BCM2835SDHostState *)opaque;
    uint32_t res = 0;

    switch (offset) {
    case SDCMD:
        res = s->cmd;
        break;
    case SDHSTS:
        res = s->status;
        break;
    case SDRSP0:
        res = s->rsp[0];
        break;
    case SDRSP1:
        res = s->rsp[1];
        break;
    case SDRSP2:
        res = s->rsp[2];
        break;
    case SDRSP3:
        res = s->rsp[3];
        break;
    case SDEDM:
        res = s->edm;
        break;
    case SDVDD:
        res = s->vdd;
        break;
    case SDDATA:
        res = bcm2835_sdhost_fifo_pop(s);
        bcm2835_sdhost_fifo_run(s);
        break;
    case SDHBCT:
        res = s->hbct;
        break;
    case SDHBLC:
        res = s->hblc;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        res = 0;
        break;
    }

    trace_bcm2835_sdhost_read(offset, res, size);

    return res;
}

static void bcm2835_sdhost_write(void *opaque, hwaddr offset,
    uint64_t value, unsigned size)
{
    BCM2835SDHostState *s = (BCM2835SDHostState *)opaque;

    trace_bcm2835_sdhost_write(offset, value, size);

    switch (offset) {
    case SDCMD:
        s->cmd = value;
        if (value & SDCMD_NEW_FLAG) {
            bcm2835_sdhost_send_command(s);
            bcm2835_sdhost_fifo_run(s);
            s->cmd &= ~SDCMD_NEW_FLAG;
        }
        break;
    case SDTOUT:
        break;
    case SDCDIV:
        break;
    case SDHSTS:
        s->status &= ~value;
        bcm2835_sdhost_update_irq(s);
        break;
    case SDARG:
        s->cmdarg = value;
        break;
    case SDEDM:
        if ((value & 0xf) == 0xf) {
            /* power down */
            value &= ~0xf;
        }
        s->edm = value;
        trace_bcm2835_sdhost_edm_change("guest register write", s->edm);
        break;
    case SDHCFG:
        s->config = value;
        bcm2835_sdhost_fifo_run(s);
        break;
    case SDVDD:
        s->vdd = value;
        break;
    case SDDATA:
        bcm2835_sdhost_fifo_push(s, value);
        bcm2835_sdhost_fifo_run(s);
        break;
    case SDHBCT:
        s->hbct = value;
        break;
    case SDHBLC:
        s->hblc = value;
        s->datacnt = s->hblc * s->hbct;
        bcm2835_sdhost_fifo_run(s);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        break;
    }
}

static const MemoryRegionOps bcm2835_sdhost_ops = {
    .read = bcm2835_sdhost_read,
    .write = bcm2835_sdhost_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_bcm2835_sdhost = {
    .name = TYPE_BCM2835_SDHOST,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cmd, BCM2835SDHostState),
        VMSTATE_UINT32(cmdarg, BCM2835SDHostState),
        VMSTATE_UINT32(status, BCM2835SDHostState),
        VMSTATE_UINT32_ARRAY(rsp, BCM2835SDHostState, 4),
        VMSTATE_UINT32(config, BCM2835SDHostState),
        VMSTATE_UINT32(edm, BCM2835SDHostState),
        VMSTATE_UINT32(vdd, BCM2835SDHostState),
        VMSTATE_UINT32(hbct, BCM2835SDHostState),
        VMSTATE_UINT32(hblc, BCM2835SDHostState),
        VMSTATE_INT32(fifo_pos, BCM2835SDHostState),
        VMSTATE_INT32(fifo_len, BCM2835SDHostState),
        VMSTATE_UINT32_ARRAY(fifo, BCM2835SDHostState, BCM2835_SDHOST_FIFO_LEN),
        VMSTATE_UINT32(datacnt, BCM2835SDHostState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_sdhost_init(Object *obj)
{
    BCM2835SDHostState *s = BCM2835_SDHOST(obj);

    qbus_create_inplace(&s->sdbus, sizeof(s->sdbus),
                        TYPE_BCM2835_SDHOST_BUS, DEVICE(s), "sd-bus");

    memory_region_init_io(&s->iomem, obj, &bcm2835_sdhost_ops, s,
                          TYPE_BCM2835_SDHOST, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq);
}

static void bcm2835_sdhost_reset(DeviceState *dev)
{
    BCM2835SDHostState *s = BCM2835_SDHOST(dev);

    s->cmd = 0;
    s->cmdarg = 0;
    s->edm = 0x0000c60f;
    trace_bcm2835_sdhost_edm_change("device reset", s->edm);
    s->config = 0;
    s->hbct = 0;
    s->hblc = 0;
    s->datacnt = 0;
    s->fifo_pos = 0;
    s->fifo_len = 0;
}

static void bcm2835_sdhost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = bcm2835_sdhost_reset;
    dc->vmsd = &vmstate_bcm2835_sdhost;
}

static TypeInfo bcm2835_sdhost_info = {
    .name          = TYPE_BCM2835_SDHOST,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835SDHostState),
    .class_init    = bcm2835_sdhost_class_init,
    .instance_init = bcm2835_sdhost_init,
};

static const TypeInfo bcm2835_sdhost_bus_info = {
    .name = TYPE_BCM2835_SDHOST_BUS,
    .parent = TYPE_SD_BUS,
    .instance_size = sizeof(SDBus),
};

static void bcm2835_sdhost_register_types(void)
{
    type_register_static(&bcm2835_sdhost_info);
    type_register_static(&bcm2835_sdhost_bus_info);
}

type_init(bcm2835_sdhost_register_types)
