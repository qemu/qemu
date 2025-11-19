/*
 * Enhanced UART with DMA and Timer (EUART) - QEMU 10.0.2
 *
 * SysBus MMIO device. Map it where you like (we'll map at 0x0A100000).
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/char/euart.h"
#include "system/memory.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"

#define DPRINTF(fmt, ...) \
    do { \
        if (0) { \
            fprintf(stderr, "EUART: " fmt, ## __VA_ARGS__); \
        } \
    } while (0)

/* IRQ helpers */
static void euart_update_irq(EUARTState *s)
{
    uint32_t pending = s->int_status & s->int_enable;
    qemu_set_irq(s->irq, pending != 0);
}

static void euart_raise_irq(EUARTState *s, uint32_t irq_bit)
{
    s->int_status |= irq_bit;
    euart_update_irq(s);
}

/* TX completion: push FIFO to char backend */
static void euart_tx_complete(void *opaque)
{
    EUARTState *s = EUART(opaque);
    
    fprintf(stderr, "EUART: tx_complete called, tx_fifo_len=%u backend_connected=%d\n",
            s->tx_fifo_len, qemu_chr_fe_backend_connected(&s->chr));

    if (s->tx_fifo_len > 0 && qemu_chr_fe_backend_connected(&s->chr)) {
        qemu_chr_fe_write(&s->chr, s->tx_fifo, s->tx_fifo_len);
        fprintf(stderr, "EUART: tx_complete wrote %u bytes from fifo\n", s->tx_fifo_len);
        s->tx_fifo_len = 0;
    }

    s->status |= EUART_STATUS_TX_READY;

    if (s->control & EUART_CTRL_TX_ENABLE) {
        euart_raise_irq(s, EUART_INT_TX);
    }
}

/* -------- FIXED FUNCTION -------- */
/* Immediate TX write when backend connected */
static void euart_transmit_byte(EUARTState *s, uint8_t byte)
{
    /* Immediate transmit if backend available */
    if (qemu_chr_fe_backend_connected(&s->chr)) {
        fprintf(stderr, "EUART: tx immediate backend_connected=1 byte=0x%02x (%c)\n", byte, (byte >= 32 && byte < 127) ? byte : '.');
        ssize_t wr = qemu_chr_fe_write(&s->chr, &byte, 1);
        fprintf(stderr, "EUART: qemu_chr_fe_write returned %zd\n", wr);
        s->status &= ~EUART_STATUS_TX_READY;
        timer_mod(s->tx_timer,
            (int64_t)qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000ULL);
        return;
    }

    /* Otherwise use FIFO as fallback */
    if (s->tx_fifo_len < EUART_FIFO_SIZE) {
        s->tx_fifo[s->tx_fifo_len++] = byte;
        s->status &= ~EUART_STATUS_TX_READY;

        timer_mod(s->tx_timer,
            (int64_t)qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000ULL);
    }
}

static void euart_receive_byte(EUARTState *s, uint8_t byte)
{
    if (s->rx_fifo_len < EUART_FIFO_SIZE) {
        s->rx_fifo[s->rx_fifo_len++] = byte;
        s->status |= EUART_STATUS_RX_READY;

        if (s->control & EUART_CTRL_RX_ENABLE) {
            euart_raise_irq(s, EUART_INT_RX);
        }
    }
}

/* chardev callbacks */
static int euart_can_receive(void *opaque)
{
    EUARTState *s = EUART(opaque);
    return EUART_FIFO_SIZE - s->rx_fifo_len;
}

static void euart_receive(void *opaque, const uint8_t *buf, int size)
{
    EUARTState *s = EUART(opaque);

    for (int i = 0; i < size; i++) {
        fprintf(stderr, "EUART RX BYTE = 0x%02X (%c)\n", buf[i], buf[i]);
        euart_receive_byte(s, buf[i]);
    }
}


static void euart_event(void *opaque, QEMUChrEvent event)
{
    (void)opaque;
    (void)event;
}

/* DMA step */
static void euart_dma_step(void *opaque)
{
    EUARTState *s = EUART(opaque);
    uint8_t buffer[EUART_DMA_CHUNK_SIZE];

    if (!(s->status & EUART_STATUS_DMA_BUSY)) {
        return;
    }

    uint32_t chunk = (s->dma_remaining > EUART_DMA_CHUNK_SIZE)
                         ? EUART_DMA_CHUNK_SIZE
                         : s->dma_remaining;

    if (s->dma_ctrl & EUART_DMA_DIR) {
        /* device -> guest */
        uint32_t n = MIN(chunk, s->rx_fifo_len);

        if (n > 0) {
            cpu_physical_memory_write(s->dma_current_addr, s->rx_fifo, n);

            memmove(s->rx_fifo, s->rx_fifo + n, s->rx_fifo_len - n);
            s->rx_fifo_len -= n;

            if (s->rx_fifo_len == 0)
                s->status &= ~EUART_STATUS_RX_READY;

            s->dma_current_addr += n;
            s->dma_remaining -= n;
            s->dma_len -= n;
        }
    } else {
        /* guest -> device */
        cpu_physical_memory_read(s->dma_current_addr, buffer, chunk);

        for (uint32_t i = 0; i < chunk; i++)
            euart_transmit_byte(s, buffer[i]);

        s->dma_current_addr += chunk;
        s->dma_remaining -= chunk;
        s->dma_len -= chunk;
    }

    if (s->dma_remaining == 0 ||
        ((s->dma_ctrl & EUART_DMA_DIR) && s->rx_fifo_len == 0)) {

        s->status &= ~EUART_STATUS_DMA_BUSY;
        s->dma_ctrl &= ~EUART_DMA_START;

        if (s->dma_ctrl & EUART_DMA_INT_EN)
            euart_raise_irq(s, EUART_INT_DMA);
    } else {
        timer_mod(s->dma_timer,
            (int64_t)qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000ULL);
    }
}

static void euart_start_dma(EUARTState *s)
{
    if (s->dma_len == 0)
        return;

    s->status |= EUART_STATUS_DMA_BUSY;
    s->dma_remaining = s->dma_len;
    s->dma_current_addr =
        (s->dma_ctrl & EUART_DMA_DIR) ? s->dma_dst : s->dma_src;

    euart_dma_step(s);
}

/* Timer logic */
static void euart_periodic_timer_tick(void *opaque)
{
    EUARTState *s = EUART(opaque);

    if (s->timer_ctrl & EUART_TIMER_INT_EN)
        euart_raise_irq(s, EUART_INT_TIMER);

    if ((s->timer_ctrl & EUART_TIMER_EN) &&
        !(s->timer_ctrl & EUART_TIMER_ONE_SHOT)) {

        timer_mod(s->periodic_timer,
            (int64_t)qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
            (s->timer_period * 1000ULL));
    } else {
        s->status &= ~EUART_STATUS_TIMER_ACTIVE;
        s->timer_ctrl &= ~EUART_TIMER_EN;
    }
}

static void euart_start_timer(EUARTState *s)
{
    if (s->timer_period == 0)
        return;

    fprintf(stderr, "EUART: START Timer");
    s->status |= EUART_STATUS_TIMER_ACTIVE;
    timer_mod(s->periodic_timer,
        (int64_t)qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
        (s->timer_period * 1000ULL));
}

static void euart_stop_timer(EUARTState *s)
{
    s->status &= ~EUART_STATUS_TIMER_ACTIVE;
    if (s->periodic_timer)
        timer_del(s->periodic_timer);
}

static void euart_reset_device(EUARTState *s)
{
    s->status = EUART_STATUS_TX_READY;
    s->control = 0;
    s->int_status = 0;
    s->int_enable = 0;
    s->dma_ctrl = 0;
    s->timer_ctrl = 0;

    s->rx_fifo_len = 0;
    s->tx_fifo_len = 0;

    if (s->dma_timer)        timer_del(s->dma_timer);
    if (s->periodic_timer)   timer_del(s->periodic_timer);
    if (s->tx_timer)         timer_del(s->tx_timer);

    euart_update_irq(s);
}

static uint64_t euart_read(void *opaque, hwaddr offset, unsigned size)
{
    EUARTState *s = EUART(opaque);
    uint64_t ret = 0;

    switch (offset) {
    case EUART_REG_DATA:
        if (s->rx_fifo_len > 0) {
            ret = s->rx_fifo[0];
            memmove(s->rx_fifo,
                    s->rx_fifo + 1,
                    s->rx_fifo_len - 1);
            s->rx_fifo_len--;
            if (s->rx_fifo_len == 0)
                s->status &= ~EUART_STATUS_RX_READY;
        }
        break;

    case EUART_REG_STATUS: ret = s->status; break;
    case EUART_REG_CONTROL: ret = s->control; break;
    case EUART_REG_INT_STATUS: ret = s->int_status; break;
    case EUART_REG_INT_ENABLE: ret = s->int_enable; break;
    case EUART_REG_DMA_SRC: ret = s->dma_src; break;
    case EUART_REG_DMA_DST: ret = s->dma_dst; break;
    case EUART_REG_DMA_LEN: ret = s->dma_len; break;
    case EUART_REG_DMA_CTRL: ret = s->dma_ctrl; break;
    case EUART_REG_TIMER_PERIOD: ret = s->timer_period; break;
    case EUART_REG_TIMER_CTRL: ret = s->timer_ctrl; break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "EUART: Bad offset 0x%" HWADDR_PRIx "\n", offset);
        break;
    }

    return ret;
}

static void euart_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    EUARTState *s = EUART(opaque);

    fprintf(stderr, "EUART: WRITE offset=0x%lx value=0x%lx\n",
            (unsigned long)offset, (unsigned long)value);

    switch (offset) {

    case EUART_REG_DATA:
        /* -------- FIXED: mask to 8-bit -------- */
        if (s->control & EUART_CTRL_TX_ENABLE)
            euart_transmit_byte(s, (uint8_t)(value & 0xFF));
        break;

    case EUART_REG_CONTROL:
        s->control = value & 0x7;
        if (value & EUART_CTRL_RESET)
            euart_reset_device(s);
        break;

    case EUART_REG_INT_STATUS:
        s->int_status &= ~value;
        euart_update_irq(s);
        break;

    case EUART_REG_INT_ENABLE:
        s->int_enable = value & 0xF;
        euart_update_irq(s);
        break;

    case EUART_REG_DMA_SRC:
    s->dma_src = value;
    fprintf(stderr, "EUART: DMA_SRC write -> 0x%016" PRIx64 "\n", s->dma_src);
    break;
    case EUART_REG_DMA_DST:
    s->dma_dst = value;
    fprintf(stderr, "EUART: DMA_DST write -> 0x%016" PRIx64 "\n", s->dma_dst);
    break;
    case EUART_REG_DMA_LEN:
    s->dma_len = (uint32_t)value;
    fprintf(stderr, "EUART: DMA_LEN write -> %u\n", s->dma_len);
    break;
    case EUART_REG_DMA_CTRL:
    s->dma_ctrl = (uint32_t)(value & 0x7);
    fprintf(stderr, "EUART: DMA_CTRL write -> 0x%02x\n", s->dma_ctrl);
    if (value & EUART_DMA_START) {
        fprintf(stderr, "EUART: DMA START requested\n");
        euart_start_dma(s);
    }
    break;


    case EUART_REG_TIMER_PERIOD:
        s->timer_period = value;
        break;

    case EUART_REG_TIMER_CTRL: {
        uint32_t old = s->timer_ctrl;
        s->timer_ctrl = value & 0x7;

        if ((value & EUART_TIMER_EN) && !(old & EUART_TIMER_EN))
            euart_start_timer(s);
        else if (!(value & EUART_TIMER_EN) && (old & EUART_TIMER_EN))
            euart_stop_timer(s);
        break;
    }

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "EUART: Bad offset 0x%" HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps euart_ops = {
    .read = euart_read,
    .write = euart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,   /* <-- changed from 4 to 8 */
    },
};


static void euart_prop_check_chardev(const Object *obj, const char *name,
                                     Object *val, Error **errp)
{
    if (val && !object_dynamic_cast(val, TYPE_CHARDEV)) {
        error_setg(errp, "Invalid chardev backend for EUART");
    }
}

static void euart_realize(DeviceState *dev, Error **errp)
{
    EUARTState *s = EUART(dev);

    s->dma_timer      = timer_new(QEMU_CLOCK_VIRTUAL, 0, euart_dma_step, s);
    s->periodic_timer = timer_new(QEMU_CLOCK_VIRTUAL, 0, euart_periodic_timer_tick, s);
    s->tx_timer       = timer_new(QEMU_CLOCK_VIRTUAL, 0, euart_tx_complete, s);

    /* QEMU-10 link property */
    Object *obj = object_property_get_link(OBJECT(dev), "chardev", errp);
    if (*errp)
        return;

    if (obj) {
        Chardev *chr = CHARDEV(obj);

        fprintf(stderr, "EUART: realize found chardev object=%p\n", (void *)obj);

        qemu_chr_fe_init(&s->chr, chr, errp);
        if (*errp) {
            fprintf(stderr, "EUART: qemu_chr_fe_init FAILED\n");
            return;
        }

        fprintf(stderr, "EUART: qemu_chr_fe_init OK\n");

        qemu_chr_fe_set_handlers(
            &s->chr,
            euart_can_receive,
            euart_receive,
            euart_event,
            NULL,
            s,
            NULL,
            true);
    } else {
        fprintf(stderr, "EUART: realize no chardev linked\n");
    }

    euart_reset_device(s);
}

static void euart_unrealize(DeviceState *dev)
{
    EUARTState *s = EUART(dev);
    timer_free(s->dma_timer);
    timer_free(s->periodic_timer);
    timer_free(s->tx_timer);
}

static void euart_init(Object *obj)
{
    EUARTState *s = EUART(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->dma_timer = NULL;
    s->periodic_timer = NULL;
    s->tx_timer = NULL;

    memory_region_init_io(&s->iomem, obj, &euart_ops, s,
                          TYPE_EUART, EUART_REG_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    euart_reset_device(s);
}

static const VMStateDescription vmstate_euart = {
    .name = TYPE_EUART,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(data, EUARTState),
        VMSTATE_UINT32(status, EUARTState),
        VMSTATE_UINT32(control, EUARTState),
        VMSTATE_UINT32(int_status, EUARTState),
        VMSTATE_UINT32(int_enable, EUARTState),
        VMSTATE_UINT64(dma_src, EUARTState),
        VMSTATE_UINT64(dma_dst, EUARTState),
        VMSTATE_UINT32(dma_len, EUARTState),
        VMSTATE_UINT32(dma_ctrl, EUARTState),
        VMSTATE_UINT32(timer_period, EUARTState),
        VMSTATE_UINT32(timer_ctrl, EUARTState),
        VMSTATE_UINT8_ARRAY(rx_fifo, EUARTState, EUART_FIFO_SIZE),
        VMSTATE_UINT8_ARRAY(tx_fifo, EUARTState, EUART_FIFO_SIZE),
        VMSTATE_UINT32(rx_fifo_len, EUARTState),
        VMSTATE_UINT32(tx_fifo_len, EUARTState),
        VMSTATE_UINT32(dma_remaining, EUARTState),
        VMSTATE_UINT64(dma_current_addr, EUARTState),
        VMSTATE_END_OF_LIST()
    }
};

static void euart_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize   = euart_realize;
    dc->unrealize = euart_unrealize;
    dc->vmsd      = &vmstate_euart;
    dc->user_creatable = true;
    dc->categories[DEVICE_CATEGORY_MISC] = true;

    /* QEMU 10: link property */
    object_class_property_add_link(
        oc,
        "chardev",
        TYPE_CHARDEV,
        offsetof(EUARTState, chr),
        euart_prop_check_chardev,
        OBJ_PROP_LINK_STRONG
    );
}

static const TypeInfo euart_info = {
    .name          = TYPE_EUART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(EUARTState),
    .instance_init = euart_init,
    .class_init    = euart_class_init,
};

static void euart_register_types(void)
{
    type_register_static(&euart_info);
}

type_init(euart_register_types)

