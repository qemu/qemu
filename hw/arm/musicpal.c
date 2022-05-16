/*
 * Marvell MV88W8618 / Freecom MusicPal emulation.
 *
 * Copyright (c) 2008 Jan Kiszka
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/arm/boot.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/char/serial.h"
#include "qemu/timer.h"
#include "hw/ptimer.h"
#include "hw/qdev-properties.h"
#include "hw/block/flash.h"
#include "ui/console.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"
#include "hw/or-irq.h"
#include "hw/audio/wm8750.h"
#include "sysemu/block-backend.h"
#include "sysemu/runstate.h"
#include "sysemu/dma.h"
#include "ui/pixel_ops.h"
#include "qemu/cutils.h"
#include "qom/object.h"
#include "hw/net/mv88w8618_eth.h"

#define MP_MISC_BASE            0x80002000
#define MP_MISC_SIZE            0x00001000

#define MP_ETH_BASE             0x80008000

#define MP_WLAN_BASE            0x8000C000
#define MP_WLAN_SIZE            0x00000800

#define MP_UART1_BASE           0x8000C840
#define MP_UART2_BASE           0x8000C940

#define MP_GPIO_BASE            0x8000D000
#define MP_GPIO_SIZE            0x00001000

#define MP_FLASHCFG_BASE        0x90006000
#define MP_FLASHCFG_SIZE        0x00001000

#define MP_AUDIO_BASE           0x90007000

#define MP_PIC_BASE             0x90008000
#define MP_PIC_SIZE             0x00001000

#define MP_PIT_BASE             0x90009000
#define MP_PIT_SIZE             0x00001000

#define MP_LCD_BASE             0x9000c000
#define MP_LCD_SIZE             0x00001000

#define MP_SRAM_BASE            0xC0000000
#define MP_SRAM_SIZE            0x00020000

#define MP_RAM_DEFAULT_SIZE     32*1024*1024
#define MP_FLASH_SIZE_MAX       32*1024*1024

#define MP_TIMER1_IRQ           4
#define MP_TIMER2_IRQ           5
#define MP_TIMER3_IRQ           6
#define MP_TIMER4_IRQ           7
#define MP_EHCI_IRQ             8
#define MP_ETH_IRQ              9
#define MP_UART_SHARED_IRQ      11
#define MP_GPIO_IRQ             12
#define MP_RTC_IRQ              28
#define MP_AUDIO_IRQ            30

/* Wolfson 8750 I2C address */
#define MP_WM_ADDR              0x1A

/* LCD register offsets */
#define MP_LCD_IRQCTRL          0x180
#define MP_LCD_IRQSTAT          0x184
#define MP_LCD_SPICTRL          0x1ac
#define MP_LCD_INST             0x1bc
#define MP_LCD_DATA             0x1c0

/* Mode magics */
#define MP_LCD_SPI_DATA         0x00100011
#define MP_LCD_SPI_CMD          0x00104011
#define MP_LCD_SPI_INVALID      0x00000000

/* Commmands */
#define MP_LCD_INST_SETPAGE0    0xB0
/* ... */
#define MP_LCD_INST_SETPAGE7    0xB7

#define MP_LCD_TEXTCOLOR        0xe0e0ff /* RRGGBB */

#define TYPE_MUSICPAL_LCD "musicpal_lcd"
OBJECT_DECLARE_SIMPLE_TYPE(musicpal_lcd_state, MUSICPAL_LCD)

struct musicpal_lcd_state {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t brightness;
    uint32_t mode;
    uint32_t irqctrl;
    uint32_t page;
    uint32_t page_off;
    QemuConsole *con;
    uint8_t video_ram[128*64/8];
};

static uint8_t scale_lcd_color(musicpal_lcd_state *s, uint8_t col)
{
    switch (s->brightness) {
    case 7:
        return col;
    case 0:
        return 0;
    default:
        return (col * s->brightness) / 7;
    }
}

static inline void set_lcd_pixel32(musicpal_lcd_state *s,
                                   int x, int y, uint32_t col)
{
    int dx, dy;
    DisplaySurface *surface = qemu_console_surface(s->con);
    uint32_t *pixel =
        &((uint32_t *) surface_data(surface))[(y * 128 * 3 + x) * 3];

    for (dy = 0; dy < 3; dy++, pixel += 127 * 3) {
        for (dx = 0; dx < 3; dx++, pixel++) {
            *pixel = col;
        }
    }
}

static void lcd_refresh(void *opaque)
{
    musicpal_lcd_state *s = opaque;
    int x, y, col;

    col = rgb_to_pixel32(scale_lcd_color(s, (MP_LCD_TEXTCOLOR >> 16) & 0xff),
                         scale_lcd_color(s, (MP_LCD_TEXTCOLOR >> 8) & 0xff),
                         scale_lcd_color(s, MP_LCD_TEXTCOLOR & 0xff));
    for (x = 0; x < 128; x++) {
        for (y = 0; y < 64; y++) {
            if (s->video_ram[x + (y / 8) * 128] & (1 << (y % 8))) {
                set_lcd_pixel32(s, x, y, col);
            } else {
                set_lcd_pixel32(s, x, y, 0);
            }
        }
    }

    dpy_gfx_update(s->con, 0, 0, 128*3, 64*3);
}

static void lcd_invalidate(void *opaque)
{
}

static void musicpal_lcd_gpio_brightness_in(void *opaque, int irq, int level)
{
    musicpal_lcd_state *s = opaque;
    s->brightness &= ~(1 << irq);
    s->brightness |= level << irq;
}

static uint64_t musicpal_lcd_read(void *opaque, hwaddr offset,
                                  unsigned size)
{
    musicpal_lcd_state *s = opaque;

    switch (offset) {
    case MP_LCD_IRQCTRL:
        return s->irqctrl;

    default:
        return 0;
    }
}

static void musicpal_lcd_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    musicpal_lcd_state *s = opaque;

    switch (offset) {
    case MP_LCD_IRQCTRL:
        s->irqctrl = value;
        break;

    case MP_LCD_SPICTRL:
        if (value == MP_LCD_SPI_DATA || value == MP_LCD_SPI_CMD) {
            s->mode = value;
        } else {
            s->mode = MP_LCD_SPI_INVALID;
        }
        break;

    case MP_LCD_INST:
        if (value >= MP_LCD_INST_SETPAGE0 && value <= MP_LCD_INST_SETPAGE7) {
            s->page = value - MP_LCD_INST_SETPAGE0;
            s->page_off = 0;
        }
        break;

    case MP_LCD_DATA:
        if (s->mode == MP_LCD_SPI_CMD) {
            if (value >= MP_LCD_INST_SETPAGE0 &&
                value <= MP_LCD_INST_SETPAGE7) {
                s->page = value - MP_LCD_INST_SETPAGE0;
                s->page_off = 0;
            }
        } else if (s->mode == MP_LCD_SPI_DATA) {
            s->video_ram[s->page*128 + s->page_off] = value;
            s->page_off = (s->page_off + 1) & 127;
        }
        break;
    }
}

static const MemoryRegionOps musicpal_lcd_ops = {
    .read = musicpal_lcd_read,
    .write = musicpal_lcd_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const GraphicHwOps musicpal_gfx_ops = {
    .invalidate  = lcd_invalidate,
    .gfx_update  = lcd_refresh,
};

static void musicpal_lcd_realize(DeviceState *dev, Error **errp)
{
    musicpal_lcd_state *s = MUSICPAL_LCD(dev);
    s->con = graphic_console_init(dev, 0, &musicpal_gfx_ops, s);
    qemu_console_resize(s->con, 128 * 3, 64 * 3);
}

static void musicpal_lcd_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    musicpal_lcd_state *s = MUSICPAL_LCD(dev);

    s->brightness = 7;

    memory_region_init_io(&s->iomem, obj, &musicpal_lcd_ops, s,
                          "musicpal-lcd", MP_LCD_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_in(dev, musicpal_lcd_gpio_brightness_in, 3);
}

static const VMStateDescription musicpal_lcd_vmsd = {
    .name = "musicpal_lcd",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(brightness, musicpal_lcd_state),
        VMSTATE_UINT32(mode, musicpal_lcd_state),
        VMSTATE_UINT32(irqctrl, musicpal_lcd_state),
        VMSTATE_UINT32(page, musicpal_lcd_state),
        VMSTATE_UINT32(page_off, musicpal_lcd_state),
        VMSTATE_BUFFER(video_ram, musicpal_lcd_state),
        VMSTATE_END_OF_LIST()
    }
};

static void musicpal_lcd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &musicpal_lcd_vmsd;
    dc->realize = musicpal_lcd_realize;
}

static const TypeInfo musicpal_lcd_info = {
    .name          = TYPE_MUSICPAL_LCD,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(musicpal_lcd_state),
    .instance_init = musicpal_lcd_init,
    .class_init    = musicpal_lcd_class_init,
};

/* PIC register offsets */
#define MP_PIC_STATUS           0x00
#define MP_PIC_ENABLE_SET       0x08
#define MP_PIC_ENABLE_CLR       0x0C

#define TYPE_MV88W8618_PIC "mv88w8618_pic"
OBJECT_DECLARE_SIMPLE_TYPE(mv88w8618_pic_state, MV88W8618_PIC)

struct mv88w8618_pic_state {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t level;
    uint32_t enabled;
    qemu_irq parent_irq;
};

static void mv88w8618_pic_update(mv88w8618_pic_state *s)
{
    qemu_set_irq(s->parent_irq, (s->level & s->enabled));
}

static void mv88w8618_pic_set_irq(void *opaque, int irq, int level)
{
    mv88w8618_pic_state *s = opaque;

    if (level) {
        s->level |= 1 << irq;
    } else {
        s->level &= ~(1 << irq);
    }
    mv88w8618_pic_update(s);
}

static uint64_t mv88w8618_pic_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    mv88w8618_pic_state *s = opaque;

    switch (offset) {
    case MP_PIC_STATUS:
        return s->level & s->enabled;

    default:
        return 0;
    }
}

static void mv88w8618_pic_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    mv88w8618_pic_state *s = opaque;

    switch (offset) {
    case MP_PIC_ENABLE_SET:
        s->enabled |= value;
        break;

    case MP_PIC_ENABLE_CLR:
        s->enabled &= ~value;
        s->level &= ~value;
        break;
    }
    mv88w8618_pic_update(s);
}

static void mv88w8618_pic_reset(DeviceState *d)
{
    mv88w8618_pic_state *s = MV88W8618_PIC(d);

    s->level = 0;
    s->enabled = 0;
}

static const MemoryRegionOps mv88w8618_pic_ops = {
    .read = mv88w8618_pic_read,
    .write = mv88w8618_pic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void mv88w8618_pic_init(Object *obj)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    mv88w8618_pic_state *s = MV88W8618_PIC(dev);

    qdev_init_gpio_in(DEVICE(dev), mv88w8618_pic_set_irq, 32);
    sysbus_init_irq(dev, &s->parent_irq);
    memory_region_init_io(&s->iomem, obj, &mv88w8618_pic_ops, s,
                          "musicpal-pic", MP_PIC_SIZE);
    sysbus_init_mmio(dev, &s->iomem);
}

static const VMStateDescription mv88w8618_pic_vmsd = {
    .name = "mv88w8618_pic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(level, mv88w8618_pic_state),
        VMSTATE_UINT32(enabled, mv88w8618_pic_state),
        VMSTATE_END_OF_LIST()
    }
};

static void mv88w8618_pic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = mv88w8618_pic_reset;
    dc->vmsd = &mv88w8618_pic_vmsd;
}

static const TypeInfo mv88w8618_pic_info = {
    .name          = TYPE_MV88W8618_PIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mv88w8618_pic_state),
    .instance_init = mv88w8618_pic_init,
    .class_init    = mv88w8618_pic_class_init,
};

/* PIT register offsets */
#define MP_PIT_TIMER1_LENGTH    0x00
/* ... */
#define MP_PIT_TIMER4_LENGTH    0x0C
#define MP_PIT_CONTROL          0x10
#define MP_PIT_TIMER1_VALUE     0x14
/* ... */
#define MP_PIT_TIMER4_VALUE     0x20
#define MP_BOARD_RESET          0x34

/* Magic board reset value (probably some watchdog behind it) */
#define MP_BOARD_RESET_MAGIC    0x10000

typedef struct mv88w8618_timer_state {
    ptimer_state *ptimer;
    uint32_t limit;
    int freq;
    qemu_irq irq;
} mv88w8618_timer_state;

#define TYPE_MV88W8618_PIT "mv88w8618_pit"
OBJECT_DECLARE_SIMPLE_TYPE(mv88w8618_pit_state, MV88W8618_PIT)

struct mv88w8618_pit_state {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    mv88w8618_timer_state timer[4];
};

static void mv88w8618_timer_tick(void *opaque)
{
    mv88w8618_timer_state *s = opaque;

    qemu_irq_raise(s->irq);
}

static void mv88w8618_timer_init(SysBusDevice *dev, mv88w8618_timer_state *s,
                                 uint32_t freq)
{
    sysbus_init_irq(dev, &s->irq);
    s->freq = freq;

    s->ptimer = ptimer_init(mv88w8618_timer_tick, s, PTIMER_POLICY_LEGACY);
}

static uint64_t mv88w8618_pit_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    mv88w8618_pit_state *s = opaque;
    mv88w8618_timer_state *t;

    switch (offset) {
    case MP_PIT_TIMER1_VALUE ... MP_PIT_TIMER4_VALUE:
        t = &s->timer[(offset-MP_PIT_TIMER1_VALUE) >> 2];
        return ptimer_get_count(t->ptimer);

    default:
        return 0;
    }
}

static void mv88w8618_pit_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    mv88w8618_pit_state *s = opaque;
    mv88w8618_timer_state *t;
    int i;

    switch (offset) {
    case MP_PIT_TIMER1_LENGTH ... MP_PIT_TIMER4_LENGTH:
        t = &s->timer[offset >> 2];
        t->limit = value;
        ptimer_transaction_begin(t->ptimer);
        if (t->limit > 0) {
            ptimer_set_limit(t->ptimer, t->limit, 1);
        } else {
            ptimer_stop(t->ptimer);
        }
        ptimer_transaction_commit(t->ptimer);
        break;

    case MP_PIT_CONTROL:
        for (i = 0; i < 4; i++) {
            t = &s->timer[i];
            ptimer_transaction_begin(t->ptimer);
            if (value & 0xf && t->limit > 0) {
                ptimer_set_limit(t->ptimer, t->limit, 0);
                ptimer_set_freq(t->ptimer, t->freq);
                ptimer_run(t->ptimer, 0);
            } else {
                ptimer_stop(t->ptimer);
            }
            ptimer_transaction_commit(t->ptimer);
            value >>= 4;
        }
        break;

    case MP_BOARD_RESET:
        if (value == MP_BOARD_RESET_MAGIC) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        break;
    }
}

static void mv88w8618_pit_reset(DeviceState *d)
{
    mv88w8618_pit_state *s = MV88W8618_PIT(d);
    int i;

    for (i = 0; i < 4; i++) {
        mv88w8618_timer_state *t = &s->timer[i];
        ptimer_transaction_begin(t->ptimer);
        ptimer_stop(t->ptimer);
        ptimer_transaction_commit(t->ptimer);
        t->limit = 0;
    }
}

static const MemoryRegionOps mv88w8618_pit_ops = {
    .read = mv88w8618_pit_read,
    .write = mv88w8618_pit_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void mv88w8618_pit_init(Object *obj)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    mv88w8618_pit_state *s = MV88W8618_PIT(dev);
    int i;

    /* Letting them all run at 1 MHz is likely just a pragmatic
     * simplification. */
    for (i = 0; i < 4; i++) {
        mv88w8618_timer_init(dev, &s->timer[i], 1000000);
    }

    memory_region_init_io(&s->iomem, obj, &mv88w8618_pit_ops, s,
                          "musicpal-pit", MP_PIT_SIZE);
    sysbus_init_mmio(dev, &s->iomem);
}

static void mv88w8618_pit_finalize(Object *obj)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    mv88w8618_pit_state *s = MV88W8618_PIT(dev);
    int i;

    for (i = 0; i < 4; i++) {
        ptimer_free(s->timer[i].ptimer);
    }
}

static const VMStateDescription mv88w8618_timer_vmsd = {
    .name = "timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER(ptimer, mv88w8618_timer_state),
        VMSTATE_UINT32(limit, mv88w8618_timer_state),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription mv88w8618_pit_vmsd = {
    .name = "mv88w8618_pit",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(timer, mv88w8618_pit_state, 4, 1,
                             mv88w8618_timer_vmsd, mv88w8618_timer_state),
        VMSTATE_END_OF_LIST()
    }
};

static void mv88w8618_pit_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = mv88w8618_pit_reset;
    dc->vmsd = &mv88w8618_pit_vmsd;
}

static const TypeInfo mv88w8618_pit_info = {
    .name          = TYPE_MV88W8618_PIT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mv88w8618_pit_state),
    .instance_init = mv88w8618_pit_init,
    .instance_finalize = mv88w8618_pit_finalize,
    .class_init    = mv88w8618_pit_class_init,
};

/* Flash config register offsets */
#define MP_FLASHCFG_CFGR0    0x04

#define TYPE_MV88W8618_FLASHCFG "mv88w8618_flashcfg"
OBJECT_DECLARE_SIMPLE_TYPE(mv88w8618_flashcfg_state, MV88W8618_FLASHCFG)

struct mv88w8618_flashcfg_state {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t cfgr0;
};

static uint64_t mv88w8618_flashcfg_read(void *opaque,
                                        hwaddr offset,
                                        unsigned size)
{
    mv88w8618_flashcfg_state *s = opaque;

    switch (offset) {
    case MP_FLASHCFG_CFGR0:
        return s->cfgr0;

    default:
        return 0;
    }
}

static void mv88w8618_flashcfg_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size)
{
    mv88w8618_flashcfg_state *s = opaque;

    switch (offset) {
    case MP_FLASHCFG_CFGR0:
        s->cfgr0 = value;
        break;
    }
}

static const MemoryRegionOps mv88w8618_flashcfg_ops = {
    .read = mv88w8618_flashcfg_read,
    .write = mv88w8618_flashcfg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void mv88w8618_flashcfg_init(Object *obj)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    mv88w8618_flashcfg_state *s = MV88W8618_FLASHCFG(dev);

    s->cfgr0 = 0xfffe4285; /* Default as set by U-Boot for 8 MB flash */
    memory_region_init_io(&s->iomem, obj, &mv88w8618_flashcfg_ops, s,
                          "musicpal-flashcfg", MP_FLASHCFG_SIZE);
    sysbus_init_mmio(dev, &s->iomem);
}

static const VMStateDescription mv88w8618_flashcfg_vmsd = {
    .name = "mv88w8618_flashcfg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cfgr0, mv88w8618_flashcfg_state),
        VMSTATE_END_OF_LIST()
    }
};

static void mv88w8618_flashcfg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &mv88w8618_flashcfg_vmsd;
}

static const TypeInfo mv88w8618_flashcfg_info = {
    .name          = TYPE_MV88W8618_FLASHCFG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mv88w8618_flashcfg_state),
    .instance_init = mv88w8618_flashcfg_init,
    .class_init    = mv88w8618_flashcfg_class_init,
};

/* Misc register offsets */
#define MP_MISC_BOARD_REVISION  0x18

#define MP_BOARD_REVISION       0x31

struct MusicPalMiscState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
};

#define TYPE_MUSICPAL_MISC "musicpal-misc"
OBJECT_DECLARE_SIMPLE_TYPE(MusicPalMiscState, MUSICPAL_MISC)

static uint64_t musicpal_misc_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    switch (offset) {
    case MP_MISC_BOARD_REVISION:
        return MP_BOARD_REVISION;

    default:
        return 0;
    }
}

static void musicpal_misc_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
}

static const MemoryRegionOps musicpal_misc_ops = {
    .read = musicpal_misc_read,
    .write = musicpal_misc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void musicpal_misc_init(Object *obj)
{
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    MusicPalMiscState *s = MUSICPAL_MISC(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &musicpal_misc_ops, NULL,
                          "musicpal-misc", MP_MISC_SIZE);
    sysbus_init_mmio(sd, &s->iomem);
}

static const TypeInfo musicpal_misc_info = {
    .name = TYPE_MUSICPAL_MISC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = musicpal_misc_init,
    .instance_size = sizeof(MusicPalMiscState),
};

/* WLAN register offsets */
#define MP_WLAN_MAGIC1          0x11c
#define MP_WLAN_MAGIC2          0x124

static uint64_t mv88w8618_wlan_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    switch (offset) {
    /* Workaround to allow loading the binary-only wlandrv.ko crap
     * from the original Freecom firmware. */
    case MP_WLAN_MAGIC1:
        return ~3;
    case MP_WLAN_MAGIC2:
        return -1;

    default:
        return 0;
    }
}

static void mv88w8618_wlan_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
}

static const MemoryRegionOps mv88w8618_wlan_ops = {
    .read = mv88w8618_wlan_read,
    .write =mv88w8618_wlan_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void mv88w8618_wlan_realize(DeviceState *dev, Error **errp)
{
    MemoryRegion *iomem = g_new(MemoryRegion, 1);

    memory_region_init_io(iomem, OBJECT(dev), &mv88w8618_wlan_ops, NULL,
                          "musicpal-wlan", MP_WLAN_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), iomem);
}

/* GPIO register offsets */
#define MP_GPIO_OE_LO           0x008
#define MP_GPIO_OUT_LO          0x00c
#define MP_GPIO_IN_LO           0x010
#define MP_GPIO_IER_LO          0x014
#define MP_GPIO_IMR_LO          0x018
#define MP_GPIO_ISR_LO          0x020
#define MP_GPIO_OE_HI           0x508
#define MP_GPIO_OUT_HI          0x50c
#define MP_GPIO_IN_HI           0x510
#define MP_GPIO_IER_HI          0x514
#define MP_GPIO_IMR_HI          0x518
#define MP_GPIO_ISR_HI          0x520

/* GPIO bits & masks */
#define MP_GPIO_LCD_BRIGHTNESS  0x00070000
#define MP_GPIO_I2C_DATA_BIT    29
#define MP_GPIO_I2C_CLOCK_BIT   30

/* LCD brightness bits in GPIO_OE_HI */
#define MP_OE_LCD_BRIGHTNESS    0x0007

#define TYPE_MUSICPAL_GPIO "musicpal_gpio"
OBJECT_DECLARE_SIMPLE_TYPE(musicpal_gpio_state, MUSICPAL_GPIO)

struct musicpal_gpio_state {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t lcd_brightness;
    uint32_t out_state;
    uint32_t in_state;
    uint32_t ier;
    uint32_t imr;
    uint32_t isr;
    qemu_irq irq;
    qemu_irq out[5]; /* 3 brightness out + 2 lcd (data and clock ) */
};

static void musicpal_gpio_brightness_update(musicpal_gpio_state *s) {
    int i;
    uint32_t brightness;

    /* compute brightness ratio */
    switch (s->lcd_brightness) {
    case 0x00000007:
        brightness = 0;
        break;

    case 0x00020000:
        brightness = 1;
        break;

    case 0x00020001:
        brightness = 2;
        break;

    case 0x00040000:
        brightness = 3;
        break;

    case 0x00010006:
        brightness = 4;
        break;

    case 0x00020005:
        brightness = 5;
        break;

    case 0x00040003:
        brightness = 6;
        break;

    case 0x00030004:
    default:
        brightness = 7;
    }

    /* set lcd brightness GPIOs  */
    for (i = 0; i <= 2; i++) {
        qemu_set_irq(s->out[i], (brightness >> i) & 1);
    }
}

static void musicpal_gpio_pin_event(void *opaque, int pin, int level)
{
    musicpal_gpio_state *s = opaque;
    uint32_t mask = 1 << pin;
    uint32_t delta = level << pin;
    uint32_t old = s->in_state & mask;

    s->in_state &= ~mask;
    s->in_state |= delta;

    if ((old ^ delta) &&
        ((level && (s->imr & mask)) || (!level && (s->ier & mask)))) {
        s->isr = mask;
        qemu_irq_raise(s->irq);
    }
}

static uint64_t musicpal_gpio_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    musicpal_gpio_state *s = opaque;

    switch (offset) {
    case MP_GPIO_OE_HI: /* used for LCD brightness control */
        return s->lcd_brightness & MP_OE_LCD_BRIGHTNESS;

    case MP_GPIO_OUT_LO:
        return s->out_state & 0xFFFF;
    case MP_GPIO_OUT_HI:
        return s->out_state >> 16;

    case MP_GPIO_IN_LO:
        return s->in_state & 0xFFFF;
    case MP_GPIO_IN_HI:
        return s->in_state >> 16;

    case MP_GPIO_IER_LO:
        return s->ier & 0xFFFF;
    case MP_GPIO_IER_HI:
        return s->ier >> 16;

    case MP_GPIO_IMR_LO:
        return s->imr & 0xFFFF;
    case MP_GPIO_IMR_HI:
        return s->imr >> 16;

    case MP_GPIO_ISR_LO:
        return s->isr & 0xFFFF;
    case MP_GPIO_ISR_HI:
        return s->isr >> 16;

    default:
        return 0;
    }
}

static void musicpal_gpio_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    musicpal_gpio_state *s = opaque;
    switch (offset) {
    case MP_GPIO_OE_HI: /* used for LCD brightness control */
        s->lcd_brightness = (s->lcd_brightness & MP_GPIO_LCD_BRIGHTNESS) |
                         (value & MP_OE_LCD_BRIGHTNESS);
        musicpal_gpio_brightness_update(s);
        break;

    case MP_GPIO_OUT_LO:
        s->out_state = (s->out_state & 0xFFFF0000) | (value & 0xFFFF);
        break;
    case MP_GPIO_OUT_HI:
        s->out_state = (s->out_state & 0xFFFF) | (value << 16);
        s->lcd_brightness = (s->lcd_brightness & 0xFFFF) |
                            (s->out_state & MP_GPIO_LCD_BRIGHTNESS);
        musicpal_gpio_brightness_update(s);
        qemu_set_irq(s->out[3], (s->out_state >> MP_GPIO_I2C_DATA_BIT) & 1);
        qemu_set_irq(s->out[4], (s->out_state >> MP_GPIO_I2C_CLOCK_BIT) & 1);
        break;

    case MP_GPIO_IER_LO:
        s->ier = (s->ier & 0xFFFF0000) | (value & 0xFFFF);
        break;
    case MP_GPIO_IER_HI:
        s->ier = (s->ier & 0xFFFF) | (value << 16);
        break;

    case MP_GPIO_IMR_LO:
        s->imr = (s->imr & 0xFFFF0000) | (value & 0xFFFF);
        break;
    case MP_GPIO_IMR_HI:
        s->imr = (s->imr & 0xFFFF) | (value << 16);
        break;
    }
}

static const MemoryRegionOps musicpal_gpio_ops = {
    .read = musicpal_gpio_read,
    .write = musicpal_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void musicpal_gpio_reset(DeviceState *d)
{
    musicpal_gpio_state *s = MUSICPAL_GPIO(d);

    s->lcd_brightness = 0;
    s->out_state = 0;
    s->in_state = 0xffffffff;
    s->ier = 0;
    s->imr = 0;
    s->isr = 0;
}

static void musicpal_gpio_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    musicpal_gpio_state *s = MUSICPAL_GPIO(dev);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, obj, &musicpal_gpio_ops, s,
                          "musicpal-gpio", MP_GPIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_out(dev, s->out, ARRAY_SIZE(s->out));

    qdev_init_gpio_in(dev, musicpal_gpio_pin_event, 32);
}

static const VMStateDescription musicpal_gpio_vmsd = {
    .name = "musicpal_gpio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(lcd_brightness, musicpal_gpio_state),
        VMSTATE_UINT32(out_state, musicpal_gpio_state),
        VMSTATE_UINT32(in_state, musicpal_gpio_state),
        VMSTATE_UINT32(ier, musicpal_gpio_state),
        VMSTATE_UINT32(imr, musicpal_gpio_state),
        VMSTATE_UINT32(isr, musicpal_gpio_state),
        VMSTATE_END_OF_LIST()
    }
};

static void musicpal_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = musicpal_gpio_reset;
    dc->vmsd = &musicpal_gpio_vmsd;
}

static const TypeInfo musicpal_gpio_info = {
    .name          = TYPE_MUSICPAL_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(musicpal_gpio_state),
    .instance_init = musicpal_gpio_init,
    .class_init    = musicpal_gpio_class_init,
};

/* Keyboard codes & masks */
#define KEY_RELEASED            0x80
#define KEY_CODE                0x7f

#define KEYCODE_TAB             0x0f
#define KEYCODE_ENTER           0x1c
#define KEYCODE_F               0x21
#define KEYCODE_M               0x32

#define KEYCODE_EXTENDED        0xe0
#define KEYCODE_UP              0x48
#define KEYCODE_DOWN            0x50
#define KEYCODE_LEFT            0x4b
#define KEYCODE_RIGHT           0x4d

#define MP_KEY_WHEEL_VOL       (1 << 0)
#define MP_KEY_WHEEL_VOL_INV   (1 << 1)
#define MP_KEY_WHEEL_NAV       (1 << 2)
#define MP_KEY_WHEEL_NAV_INV   (1 << 3)
#define MP_KEY_BTN_FAVORITS    (1 << 4)
#define MP_KEY_BTN_MENU        (1 << 5)
#define MP_KEY_BTN_VOLUME      (1 << 6)
#define MP_KEY_BTN_NAVIGATION  (1 << 7)

#define TYPE_MUSICPAL_KEY "musicpal_key"
OBJECT_DECLARE_SIMPLE_TYPE(musicpal_key_state, MUSICPAL_KEY)

struct musicpal_key_state {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t kbd_extended;
    uint32_t pressed_keys;
    qemu_irq out[8];
};

static void musicpal_key_event(void *opaque, int keycode)
{
    musicpal_key_state *s = opaque;
    uint32_t event = 0;
    int i;

    if (keycode == KEYCODE_EXTENDED) {
        s->kbd_extended = 1;
        return;
    }

    if (s->kbd_extended) {
        switch (keycode & KEY_CODE) {
        case KEYCODE_UP:
            event = MP_KEY_WHEEL_NAV | MP_KEY_WHEEL_NAV_INV;
            break;

        case KEYCODE_DOWN:
            event = MP_KEY_WHEEL_NAV;
            break;

        case KEYCODE_LEFT:
            event = MP_KEY_WHEEL_VOL | MP_KEY_WHEEL_VOL_INV;
            break;

        case KEYCODE_RIGHT:
            event = MP_KEY_WHEEL_VOL;
            break;
        }
    } else {
        switch (keycode & KEY_CODE) {
        case KEYCODE_F:
            event = MP_KEY_BTN_FAVORITS;
            break;

        case KEYCODE_TAB:
            event = MP_KEY_BTN_VOLUME;
            break;

        case KEYCODE_ENTER:
            event = MP_KEY_BTN_NAVIGATION;
            break;

        case KEYCODE_M:
            event = MP_KEY_BTN_MENU;
            break;
        }
        /* Do not repeat already pressed buttons */
        if (!(keycode & KEY_RELEASED) && (s->pressed_keys & event)) {
            event = 0;
        }
    }

    if (event) {
        /* Raise GPIO pin first if repeating a key */
        if (!(keycode & KEY_RELEASED) && (s->pressed_keys & event)) {
            for (i = 0; i <= 7; i++) {
                if (event & (1 << i)) {
                    qemu_set_irq(s->out[i], 1);
                }
            }
        }
        for (i = 0; i <= 7; i++) {
            if (event & (1 << i)) {
                qemu_set_irq(s->out[i], !!(keycode & KEY_RELEASED));
            }
        }
        if (keycode & KEY_RELEASED) {
            s->pressed_keys &= ~event;
        } else {
            s->pressed_keys |= event;
        }
    }

    s->kbd_extended = 0;
}

static void musicpal_key_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    musicpal_key_state *s = MUSICPAL_KEY(dev);

    memory_region_init(&s->iomem, obj, "dummy", 0);
    sysbus_init_mmio(sbd, &s->iomem);

    s->kbd_extended = 0;
    s->pressed_keys = 0;

    qdev_init_gpio_out(dev, s->out, ARRAY_SIZE(s->out));

    qemu_add_kbd_event_handler(musicpal_key_event, s);
}

static const VMStateDescription musicpal_key_vmsd = {
    .name = "musicpal_key",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(kbd_extended, musicpal_key_state),
        VMSTATE_UINT32(pressed_keys, musicpal_key_state),
        VMSTATE_END_OF_LIST()
    }
};

static void musicpal_key_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &musicpal_key_vmsd;
}

static const TypeInfo musicpal_key_info = {
    .name          = TYPE_MUSICPAL_KEY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(musicpal_key_state),
    .instance_init = musicpal_key_init,
    .class_init    = musicpal_key_class_init,
};

static struct arm_boot_info musicpal_binfo = {
    .loader_start = 0x0,
    .board_id = 0x20e,
};

static void musicpal_init(MachineState *machine)
{
    ARMCPU *cpu;
    DeviceState *dev;
    DeviceState *pic;
    DeviceState *uart_orgate;
    DeviceState *i2c_dev;
    DeviceState *lcd_dev;
    DeviceState *key_dev;
    I2CSlave *wm8750_dev;
    SysBusDevice *s;
    I2CBus *i2c;
    int i;
    unsigned long flash_size;
    DriveInfo *dinfo;
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *sram = g_new(MemoryRegion, 1);

    /* For now we use a fixed - the original - RAM size */
    if (machine->ram_size != mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    cpu = ARM_CPU(cpu_create(machine->cpu_type));

    memory_region_add_subregion(address_space_mem, 0, machine->ram);

    memory_region_init_ram(sram, NULL, "musicpal.sram", MP_SRAM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(address_space_mem, MP_SRAM_BASE, sram);

    pic = sysbus_create_simple(TYPE_MV88W8618_PIC, MP_PIC_BASE,
                               qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ));
    sysbus_create_varargs(TYPE_MV88W8618_PIT, MP_PIT_BASE,
                          qdev_get_gpio_in(pic, MP_TIMER1_IRQ),
                          qdev_get_gpio_in(pic, MP_TIMER2_IRQ),
                          qdev_get_gpio_in(pic, MP_TIMER3_IRQ),
                          qdev_get_gpio_in(pic, MP_TIMER4_IRQ), NULL);

    /* Logically OR both UART IRQs together */
    uart_orgate = DEVICE(object_new(TYPE_OR_IRQ));
    object_property_set_int(OBJECT(uart_orgate), "num-lines", 2, &error_fatal);
    qdev_realize_and_unref(uart_orgate, NULL, &error_fatal);
    qdev_connect_gpio_out(DEVICE(uart_orgate), 0,
                          qdev_get_gpio_in(pic, MP_UART_SHARED_IRQ));

    serial_mm_init(address_space_mem, MP_UART1_BASE, 2,
                   qdev_get_gpio_in(uart_orgate, 0),
                   1825000, serial_hd(0), DEVICE_NATIVE_ENDIAN);
    serial_mm_init(address_space_mem, MP_UART2_BASE, 2,
                   qdev_get_gpio_in(uart_orgate, 1),
                   1825000, serial_hd(1), DEVICE_NATIVE_ENDIAN);

    /* Register flash */
    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (dinfo) {
        BlockBackend *blk = blk_by_legacy_dinfo(dinfo);

        flash_size = blk_getlength(blk);
        if (flash_size != 8*1024*1024 && flash_size != 16*1024*1024 &&
            flash_size != 32*1024*1024) {
            error_report("Invalid flash image size");
            exit(1);
        }

        /*
         * The original U-Boot accesses the flash at 0xFE000000 instead of
         * 0xFF800000 (if there is 8 MB flash). So remap flash access if the
         * image is smaller than 32 MB.
         */
        pflash_cfi02_register(0x100000000ULL - MP_FLASH_SIZE_MAX,
                              "musicpal.flash", flash_size,
                              blk, 0x10000,
                              MP_FLASH_SIZE_MAX / flash_size,
                              2, 0x00BF, 0x236D, 0x0000, 0x0000,
                              0x5555, 0x2AAA, 0);
    }
    sysbus_create_simple(TYPE_MV88W8618_FLASHCFG, MP_FLASHCFG_BASE, NULL);

    qemu_check_nic_model(&nd_table[0], "mv88w8618");
    dev = qdev_new(TYPE_MV88W8618_ETH);
    qdev_set_nic_properties(dev, &nd_table[0]);
    object_property_set_link(OBJECT(dev), "dma-memory",
                             OBJECT(get_system_memory()), &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, MP_ETH_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(pic, MP_ETH_IRQ));

    sysbus_create_simple("mv88w8618_wlan", MP_WLAN_BASE, NULL);

    sysbus_create_simple(TYPE_MUSICPAL_MISC, MP_MISC_BASE, NULL);

    dev = sysbus_create_simple(TYPE_MUSICPAL_GPIO, MP_GPIO_BASE,
                               qdev_get_gpio_in(pic, MP_GPIO_IRQ));
    i2c_dev = sysbus_create_simple("gpio_i2c", -1, NULL);
    i2c = (I2CBus *)qdev_get_child_bus(i2c_dev, "i2c");

    lcd_dev = sysbus_create_simple(TYPE_MUSICPAL_LCD, MP_LCD_BASE, NULL);
    key_dev = sysbus_create_simple(TYPE_MUSICPAL_KEY, -1, NULL);

    /* I2C read data */
    qdev_connect_gpio_out(i2c_dev, 0,
                          qdev_get_gpio_in(dev, MP_GPIO_I2C_DATA_BIT));
    /* I2C data */
    qdev_connect_gpio_out(dev, 3, qdev_get_gpio_in(i2c_dev, 0));
    /* I2C clock */
    qdev_connect_gpio_out(dev, 4, qdev_get_gpio_in(i2c_dev, 1));

    for (i = 0; i < 3; i++) {
        qdev_connect_gpio_out(dev, i, qdev_get_gpio_in(lcd_dev, i));
    }
    for (i = 0; i < 4; i++) {
        qdev_connect_gpio_out(key_dev, i, qdev_get_gpio_in(dev, i + 8));
    }
    for (i = 4; i < 8; i++) {
        qdev_connect_gpio_out(key_dev, i, qdev_get_gpio_in(dev, i + 15));
    }

    wm8750_dev = i2c_slave_create_simple(i2c, TYPE_WM8750, MP_WM_ADDR);
    dev = qdev_new(TYPE_MV88W8618_AUDIO);
    s = SYS_BUS_DEVICE(dev);
    object_property_set_link(OBJECT(dev), "wm8750", OBJECT(wm8750_dev),
                             NULL);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, MP_AUDIO_BASE);
    sysbus_connect_irq(s, 0, qdev_get_gpio_in(pic, MP_AUDIO_IRQ));

    musicpal_binfo.ram_size = MP_RAM_DEFAULT_SIZE;
    arm_load_kernel(cpu, machine, &musicpal_binfo);
}

static void musicpal_machine_init(MachineClass *mc)
{
    mc->desc = "Marvell 88w8618 / MusicPal (ARM926EJ-S)";
    mc->init = musicpal_init;
    mc->ignore_memory_transaction_failures = true;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
    mc->default_ram_size = MP_RAM_DEFAULT_SIZE;
    mc->default_ram_id = "musicpal.ram";
}

DEFINE_MACHINE("musicpal", musicpal_machine_init)

static void mv88w8618_wlan_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mv88w8618_wlan_realize;
}

static const TypeInfo mv88w8618_wlan_info = {
    .name          = "mv88w8618_wlan",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusDevice),
    .class_init    = mv88w8618_wlan_class_init,
};

static void musicpal_register_types(void)
{
    type_register_static(&mv88w8618_pic_info);
    type_register_static(&mv88w8618_pit_info);
    type_register_static(&mv88w8618_flashcfg_info);
    type_register_static(&mv88w8618_wlan_info);
    type_register_static(&musicpal_lcd_info);
    type_register_static(&musicpal_gpio_info);
    type_register_static(&musicpal_key_info);
    type_register_static(&musicpal_misc_info);
}

type_init(musicpal_register_types)
