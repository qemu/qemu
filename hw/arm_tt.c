/*
 * TomTom GO 730 with Samsung S3C2443X emulation.
 *
 * Copyright (c) 2010, 2012 Stefan Weil
 *
 * Code based on hw/musicpal.c
 * Copyright (c) 2008 Jan Kiszka
 *
 * This code is licenced under the GNU GPL v2 or later.
 *
 * References:
 * http://www.opentom.org/TomTom_GO_730
 * ARM 920T Technical Reference Manual
 */

#include "sysbus.h"
#include "arm-misc.h"
#include "devices.h"
#include "exec-memory.h"        /* get_system_memory */
#include "net.h"
#include "sysemu.h"
#include "boards.h"
#include "pc.h"
#include "ptimer.h"             /* ptimer_state */
#include "qemu-timer.h"
#include "block.h"
#include "flash.h"
#include "console.h"
#include "i2c.h"

#include "s3c2440.h"

#ifdef TARGET_WORDS_BIGENDIAN
int bigendian = 1;
#else
int bigendian = 0;
#endif

#define logout(fmt, ...) \
    fprintf(stderr, "S3C2443\t%-24s" fmt, __func__, ##__VA_ARGS__)

#define TODO() logout("%s:%u: missing\n", __FILE__, __LINE__)

/*
Base Address of Special Registers
Address    Module
0x51000000 PWM
0x5B000000 AC97
0x50000000 UART
0x5A000000 SDI
0x4F800000 TIC
0x4F000000 SSMC
0x59000000 SPI
0x4E800000 MATRIX
0x58000000 TSADC
0x4E000000 NFCON
0x4D800000 CAM I/F
0x4D000000 STN-LCD
0x57000000 RTC
0x4C800000 TFT-LCD
0x4B800000 CF Card
0x4B000000 DMA
0x55000000 IIS
0x4A800000 HS-MMC
0x54000000 IIC
0x4A000000 INTC
0x49800000 USB Device
0x53000000 WDT
0x49000000 USB HOST
0x48800000 EBI
0x48000000 Module SDRAM
0x52000000 HS-SPI
*/

#define S3C2443X_SYSCON         0x4c000000
#define S3C2443X_IO_PORT        0x56000000

typedef struct {
    S3CState *soc;
} TTState;

typedef struct {
    unsigned offset;
    const char *name;
} OffsetNamePair;

static const char *offset2name(const OffsetNamePair *o2n, unsigned offset)
{
    static char buffer[12];
    const char *name = buffer;
    snprintf(buffer, sizeof(buffer), "0x%08x", offset);
    for (; o2n->name != 0; o2n++) {
        if (offset == o2n->offset) {
            name = o2n->name;
            break;
        }
    }
    return name;
}

#define MP_MISC_BASE            0x80002000
#define MP_MISC_SIZE            0x00001000

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

#define TT_SRAM_BASE            0xC0000000
#define TT_SRAM_SIZE            0x00020000

#define MP_RAM_DEFAULT_SIZE     (64 * MiB)
#define MP_FLASH_SIZE_MAX       32*MiB

#define MP_TIMER1_IRQ           4
#define MP_TIMER2_IRQ           5
#define MP_TIMER3_IRQ           6
#define MP_TIMER4_IRQ           7
#define MP_EHCI_IRQ             8
#define MP_ETH_IRQ              9
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

typedef struct tt_lcd_state {
    SysBusDevice busdev;
    MemoryRegion mmio;
    uint32_t brightness;
    uint32_t mode;
    uint32_t irqctrl;
    uint32_t page;
    uint32_t page_off;
    DisplayState *ds;
    uint8_t video_ram[128*64/8];
} tt_lcd_state;

static uint8_t scale_lcd_color(tt_lcd_state *s, uint8_t col)
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

#define SET_LCD_PIXEL(depth, type) \
static inline void glue(set_lcd_pixel, depth) \
        (tt_lcd_state *s, int x, int y, type col) \
{ \
    int dx, dy; \
    type *pixel = &((type *) ds_get_data(s->ds))[(y * 128 * 3 + x) * 3]; \
\
    for (dy = 0; dy < 3; dy++, pixel += 127 * 3) \
        for (dx = 0; dx < 3; dx++, pixel++) \
            *pixel = col; \
}
SET_LCD_PIXEL(8, uint8_t)
SET_LCD_PIXEL(16, uint16_t)
SET_LCD_PIXEL(32, uint32_t)

#include "pixel_ops.h"

static void lcd_refresh(void *opaque)
{
    tt_lcd_state *s = opaque;
    int x, y, col;

    switch (ds_get_bits_per_pixel(s->ds)) {
    case 0:
        return;
#define LCD_REFRESH(depth, func) \
    case depth: \
        col = func(scale_lcd_color(s, (MP_LCD_TEXTCOLOR >> 16) & 0xff), \
                   scale_lcd_color(s, (MP_LCD_TEXTCOLOR >> 8) & 0xff), \
                   scale_lcd_color(s, MP_LCD_TEXTCOLOR & 0xff)); \
        for (x = 0; x < 128; x++) { \
            for (y = 0; y < 64; y++) { \
                if (s->video_ram[x + (y/8)*128] & (1 << (y % 8))) { \
                    glue(set_lcd_pixel, depth)(s, x, y, col); \
                } else { \
                    glue(set_lcd_pixel, depth)(s, x, y, 0); \
                } \
            } \
        } \
        break;
    LCD_REFRESH(8, rgb_to_pixel8)
    LCD_REFRESH(16, rgb_to_pixel16)
    LCD_REFRESH(32, (is_surface_bgr(s->ds->surface) ?
                     rgb_to_pixel32bgr : rgb_to_pixel32))
    default:
        hw_error("unsupported colour depth %i\n",
                  ds_get_bits_per_pixel(s->ds));
    }

    dpy_update(s->ds, 0, 0, 128*3, 64*3);
}

static void lcd_invalidate(void *opaque)
{
}

static void tt_lcd_gpio_brigthness_in(void *opaque, int irq, int level)
{
    tt_lcd_state *s = opaque;
    s->brightness &= ~(1 << irq);
    s->brightness |= level << irq;
}

static uint64_t tt_lcd_read(void *opaque, target_phys_addr_t offset,
                            unsigned size)
{
    tt_lcd_state *s = opaque;

    switch (offset) {
    case MP_LCD_IRQCTRL:
        return s->irqctrl;

    default:
        return 0;
    }
}

static void tt_lcd_write(void *opaque, target_phys_addr_t offset,
                         uint64_t value, unsigned size)
{
    tt_lcd_state *s = opaque;

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

static const MemoryRegionOps tt_lcd_ops = {
    .read = tt_lcd_read,
    .write = tt_lcd_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static int tt_lcd_init(SysBusDevice *dev)
{
    tt_lcd_state *s = FROM_SYSBUS(tt_lcd_state, dev);

    s->brightness = 7;

    memory_region_init_io(&s->mmio, &tt_lcd_ops, s, "tt-lcd", MP_LCD_SIZE);
    sysbus_init_mmio(dev, &s->mmio);

    s->ds = graphic_console_init(lcd_refresh, lcd_invalidate,
                                 NULL, NULL, s);
    qemu_console_resize(s->ds, 128*3, 64*3);

    qdev_init_gpio_in(&dev->qdev, tt_lcd_gpio_brigthness_in, 3);

    return 0;
}

static const VMStateDescription tt_lcd_vmsd = {
    .name = "tt_lcd",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(brightness, tt_lcd_state),
        VMSTATE_UINT32(mode, tt_lcd_state),
        VMSTATE_UINT32(irqctrl, tt_lcd_state),
        VMSTATE_UINT32(page, tt_lcd_state),
        VMSTATE_UINT32(page_off, tt_lcd_state),
        VMSTATE_BUFFER(video_ram, tt_lcd_state),
        VMSTATE_END_OF_LIST()
    }
};

static void tt_lcd_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    k->init = tt_lcd_init;
}

static DeviceInfo tt_lcd_info = {
    .name = "tt_lcd",
    .size = sizeof(tt_lcd_state),
    .vmsd = &tt_lcd_vmsd,
    .class_init = tt_lcd_class_init,
};

/* PIC register offsets */
#define MP_PIC_STATUS           0x00
#define MP_PIC_ENABLE_SET       0x08
#define MP_PIC_ENABLE_CLR       0x0C

typedef struct mv88w8618_pic_state
{
    SysBusDevice busdev;
    MemoryRegion mmio;
    uint32_t level;
    uint32_t enabled;
    qemu_irq parent_irq;
} mv88w8618_pic_state;

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

static uint64_t mv88w8618_pic_read(void *opaque, target_phys_addr_t offset,
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

static void mv88w8618_pic_write(void *opaque, target_phys_addr_t offset,
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
    mv88w8618_pic_state *s = FROM_SYSBUS(mv88w8618_pic_state,
                                         sysbus_from_qdev(d));

    s->level = 0;
    s->enabled = 0;
}

static const MemoryRegionOps mv88w8618_pic_ops = {
    .read = mv88w8618_pic_read,
    .write = mv88w8618_pic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static int mv88w8618_pic_init(SysBusDevice *dev)
{
    mv88w8618_pic_state *s = FROM_SYSBUS(mv88w8618_pic_state, dev);

    qdev_init_gpio_in(&dev->qdev, mv88w8618_pic_set_irq, 32);
    sysbus_init_irq(dev, &s->parent_irq);
    memory_region_init_io(&s->mmio, &mv88w8618_pic_ops, s,
                          "mv88w8618-pic", MP_PIC_SIZE);
    sysbus_init_mmio(dev, &s->mmio);
    return 0;
}

static const VMStateDescription mv88w8618_pic_vmsd = {
    .name = "mv88w8618_pic",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(level, mv88w8618_pic_state),
        VMSTATE_UINT32(enabled, mv88w8618_pic_state),
        VMSTATE_END_OF_LIST()
    }
};

static void mv88w8618_pic_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    k->init = mv88w8618_pic_init;
}

static DeviceInfo mv88w8618_pic_info = {
    .name = "mv88w8618_pic",
    .size = sizeof(mv88w8618_pic_state),
    .vmsd = &mv88w8618_pic_vmsd,
    .class_init = mv88w8618_pic_class_init,
    .reset = mv88w8618_pic_reset,
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

typedef struct mv88w8618_pit_state {
    SysBusDevice busdev;
    MemoryRegion mmio;
    mv88w8618_timer_state timer[4];
} mv88w8618_pit_state;

static void mv88w8618_timer_tick(void *opaque)
{
    mv88w8618_timer_state *s = opaque;

    qemu_irq_raise(s->irq);
}

static void mv88w8618_timer_init(SysBusDevice *dev, mv88w8618_timer_state *s,
                                 uint32_t freq)
{
    QEMUBH *bh;

    sysbus_init_irq(dev, &s->irq);
    s->freq = freq;

    bh = qemu_bh_new(mv88w8618_timer_tick, s);
    s->ptimer = ptimer_init(bh);
}

static uint64_t mv88w8618_pit_read(void *opaque, target_phys_addr_t offset,
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

static void mv88w8618_pit_write(void *opaque, target_phys_addr_t offset,
                                uint64_t value, unsigned size)
{
    mv88w8618_pit_state *s = opaque;
    mv88w8618_timer_state *t;
    int i;

    switch (offset) {
    case MP_PIT_TIMER1_LENGTH ... MP_PIT_TIMER4_LENGTH:
        t = &s->timer[offset >> 2];
        t->limit = value;
        if (t->limit > 0) {
            ptimer_set_limit(t->ptimer, t->limit, 1);
        } else {
            ptimer_stop(t->ptimer);
        }
        break;

    case MP_PIT_CONTROL:
        for (i = 0; i < 4; i++) {
            t = &s->timer[i];
            if (value & 0xf && t->limit > 0) {
                ptimer_set_limit(t->ptimer, t->limit, 0);
                ptimer_set_freq(t->ptimer, t->freq);
                ptimer_run(t->ptimer, 0);
            } else {
                ptimer_stop(t->ptimer);
            }
            value >>= 4;
        }
        break;

    case MP_BOARD_RESET:
        if (value == MP_BOARD_RESET_MAGIC) {
            qemu_system_reset_request();
        }
        break;
    }
}

static void mv88w8618_pit_reset(DeviceState *d)
{
    mv88w8618_pit_state *s = FROM_SYSBUS(mv88w8618_pit_state,
                                         sysbus_from_qdev(d));
    int i;

    for (i = 0; i < 4; i++) {
        ptimer_stop(s->timer[i].ptimer);
        s->timer[i].limit = 0;
    }
}

static const MemoryRegionOps mv88w8618_pit_ops = {
    .read = mv88w8618_pit_read,
    .write = mv88w8618_pit_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static int mv88w8618_pit_init(SysBusDevice *dev)
{
    mv88w8618_pit_state *s = FROM_SYSBUS(mv88w8618_pit_state, dev);
    int i;

    /* Letting them all run at 1 MHz is likely just a pragmatic
     * simplification. */
    for (i = 0; i < 4; i++) {
        mv88w8618_timer_init(dev, &s->timer[i], 1000000);
    }

    memory_region_init_io(&s->mmio, &mv88w8618_pit_ops, s,
                          "mv88w8618-pit", MP_PIT_SIZE);
    sysbus_init_mmio(dev, &s->mmio);
    return 0;
}

static const VMStateDescription mv88w8618_timer_vmsd = {
    .name = "timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
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
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(timer, mv88w8618_pit_state, 4, 1,
                             mv88w8618_timer_vmsd, mv88w8618_timer_state),
        VMSTATE_END_OF_LIST()
    }
};

static void mv88w8618_pit_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    k->init = mv88w8618_pit_init;
}

static DeviceInfo mv88w8618_pit_info = {
    .name  = "mv88w8618_pit",
    .size  = sizeof(mv88w8618_pit_state),
    .vmsd  = &mv88w8618_pit_vmsd,
    .class_init = mv88w8618_pit_class_init,
    .reset = mv88w8618_pit_reset,
};

/* Flash config register offsets */
#define MP_FLASHCFG_CFGR0    0x04

typedef struct mv88w8618_flashcfg_state {
    SysBusDevice busdev;
    MemoryRegion mmio;
    uint32_t cfgr0;
} mv88w8618_flashcfg_state;

static uint64_t mv88w8618_flashcfg_read(void *opaque,
                                        target_phys_addr_t offset,
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

static void mv88w8618_flashcfg_write(void *opaque, target_phys_addr_t offset,
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
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static int mv88w8618_flashcfg_init(SysBusDevice *dev)
{
    mv88w8618_flashcfg_state *s = FROM_SYSBUS(mv88w8618_flashcfg_state, dev);

    s->cfgr0 = 0xfffe4285; /* Default as set by U-Boot for 8 MB flash */
    memory_region_init_io(&s->mmio, &mv88w8618_flashcfg_ops, s,
                          "mv88w8618-flashcfg", MP_FLASHCFG_SIZE);
    sysbus_init_mmio(dev, &s->mmio);
    return 0;
}

static const VMStateDescription mv88w8618_flashcfg_vmsd = {
    .name = "mv88w8618_flashcfg",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cfgr0, mv88w8618_flashcfg_state),
        VMSTATE_END_OF_LIST()
    }
};

static void mv88w8618_flashcfg_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    k->init = mv88w8618_flashcfg_init;
}

static DeviceInfo mv88w8618_flashcfg_info = {
    .name  = "mv88w8618_flashcfg",
    .size  = sizeof(mv88w8618_flashcfg_state),
    .vmsd  = &mv88w8618_flashcfg_vmsd,
    .class_init = mv88w8618_flashcfg_class_init,
};

/******************************************************************************/

#define S3C2443_MPLLCON         0x10
#define S3C2443_CLKDIV0         0x24

/******************************************************************************/

/* SYSCON register offsets. */
#define SYSCON_MPLLCON          0x10
#define SYSCON_CLKDIV0          0x24

static const OffsetNamePair tt_syscon_names[] = {
    {}
};

static uint64_t tt_syscon_read(void *opaque, target_phys_addr_t offset,
                               unsigned size)
{
    uint32_t value = 0;
    logout("%s\n", offset2name(tt_syscon_names, offset));
    switch (offset) {
        case SYSCON_MPLLCON:
        case SYSCON_CLKDIV0:
        default:
            TODO();
    }
    return value;
}

static void tt_syscon_write(void *opaque, target_phys_addr_t offset,
                                uint64_t value, unsigned size)
{
    logout("%s 0x%08" PRIx64 "\n", offset2name(tt_syscon_names, offset), value);
    switch (offset) {
        default:
            TODO();
    }
}

static const MemoryRegionOps tt_syscon_ops = {
    .read = tt_syscon_read,
    .write = tt_syscon_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

/******************************************************************************/

/* I/O port register offsets. */
#define IOPORT_GPBCON           0x10
#define IOPORT_GPBDAT           0x14
#define IOPORT_GPBUDP           0x18
#define IOPORT_EXTINT0          0x88
#define IOPORT_EXTINT1          0x8c
#define IOPORT_EXTINT2          0x90
#define IOPORT_GSTATUS1         0xb0

/*
tt_ioport_write: 0x00000010
tt_ioport_write: 0x00000018
tt_ioport_write: 0x00000010
tt_ioport_write: 0x00000018
*/

static const OffsetNamePair tt_ioport_names[] = {
    {}
};

static uint64_t tt_ioport_read(void *opaque, target_phys_addr_t offset,
                               unsigned size)
{
    uint32_t value = 0;
    logout("%s\n", offset2name(tt_ioport_names, offset));
    switch (offset) {
        case IOPORT_GPBCON:
            TODO();
            break;
        case IOPORT_GPBDAT:
            TODO();
            break;
        case IOPORT_GPBUDP:
            value = 0x2aaaaa;
            break;
        //~ case IOPORT_EXTINT0:
        //~ case IOPORT_EXTINT1:
        //~ case IOPORT_EXTINT2:
        case IOPORT_GSTATUS1:
            value = 0x32443001;
            break;
        default:
            TODO();
    }
    return value;
}

static void tt_ioport_write(void *opaque, target_phys_addr_t offset,
                                uint64_t value, unsigned size)
{
    logout("%s 0x%08" PRIx64 "\n", offset2name(tt_ioport_names, offset), value);
    switch (offset) {
        case IOPORT_GPBCON:
            TODO();
            break;
        //~ case IOPORT_GPBDAT:
        case IOPORT_GPBUDP:
            TODO();
            break;
        case IOPORT_EXTINT0:
            TODO();
            break;
        case IOPORT_EXTINT1:
            TODO();
            break;
        case IOPORT_EXTINT2:
            TODO();
            break;
        //~ case IOPORT_GSTATUS1:
        default:
            TODO();
    }
}

static const MemoryRegionOps tt_ioport_ops = {
    .read = tt_ioport_read,
    .write = tt_ioport_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

/******************************************************************************/

#if 0
static void tt_syscon_init(void)
{
    memory_region_init_io(&s->syscon, &tt_syscon_ops, s,
                          "tt-syscon", 0x10000);
    memory_region_add_subregion(get_system_memory(), S3C2443X_SYSCON, &s->syscon);
}

static void tt_ioport_init(void)
{
    memory_region_init_io(&s->ioport, &tt_ioport_ops, s,
                          "tt-ioport", 0x10000);
    memory_region_add_subregion(get_system_memory(), S3C2443X_IO_PORT, &s->ioport);
}
#endif

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

typedef struct tt_gpio_state {
    SysBusDevice busdev;
    MemoryRegion mmio;
    uint32_t lcd_brightness;
    uint32_t out_state;
    uint32_t in_state;
    uint32_t ier;
    uint32_t imr;
    uint32_t isr;
    qemu_irq irq;
    qemu_irq out[5]; /* 3 brightness out + 2 lcd (data and clock ) */
} tt_gpio_state;

static void tt_gpio_brightness_update(tt_gpio_state *s) {
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

static void tt_gpio_pin_event(void *opaque, int pin, int level)
{
    tt_gpio_state *s = opaque;
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

static uint64_t tt_gpio_read(void *opaque, target_phys_addr_t offset,
                             unsigned size)
{
    tt_gpio_state *s = opaque;

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

static void tt_gpio_write(void *opaque, target_phys_addr_t offset,
                                uint64_t value, unsigned size)
{
    tt_gpio_state *s = opaque;
    switch (offset) {
    case MP_GPIO_OE_HI: /* used for LCD brightness control */
        s->lcd_brightness = (s->lcd_brightness & MP_GPIO_LCD_BRIGHTNESS) |
                         (value & MP_OE_LCD_BRIGHTNESS);
        tt_gpio_brightness_update(s);
        break;

    case MP_GPIO_OUT_LO:
        s->out_state = (s->out_state & 0xFFFF0000) | (value & 0xFFFF);
        break;
    case MP_GPIO_OUT_HI:
        s->out_state = (s->out_state & 0xFFFF) | (value << 16);
        s->lcd_brightness = (s->lcd_brightness & 0xFFFF) |
                            (s->out_state & MP_GPIO_LCD_BRIGHTNESS);
        tt_gpio_brightness_update(s);
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

static const MemoryRegionOps tt_gpio_ops = {
    .read = tt_gpio_read,
    .write = tt_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void tt_gpio_reset(DeviceState *d)
{
    tt_gpio_state *s = FROM_SYSBUS(tt_gpio_state, sysbus_from_qdev(d));

    s->lcd_brightness = 0;
    s->out_state = 0;
    s->in_state = 0xffffffff;
    s->ier = 0;
    s->imr = 0;
    s->isr = 0;
}

static int tt_gpio_init(SysBusDevice *dev)
{
    tt_gpio_state *s = FROM_SYSBUS(tt_gpio_state, dev);

    sysbus_init_irq(dev, &s->irq);

    memory_region_init_io(&s->mmio, &tt_gpio_ops, s,
                          "tt-gpio", MP_GPIO_SIZE);
    sysbus_init_mmio(dev, &s->mmio);

    qdev_init_gpio_out(&dev->qdev, s->out, ARRAY_SIZE(s->out));

    qdev_init_gpio_in(&dev->qdev, tt_gpio_pin_event, 32);

    return 0;
}

static const VMStateDescription tt_gpio_vmsd = {
    .name = "tt_gpio",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(lcd_brightness, tt_gpio_state),
        VMSTATE_UINT32(out_state, tt_gpio_state),
        VMSTATE_UINT32(in_state, tt_gpio_state),
        VMSTATE_UINT32(ier, tt_gpio_state),
        VMSTATE_UINT32(imr, tt_gpio_state),
        VMSTATE_UINT32(isr, tt_gpio_state),
        VMSTATE_END_OF_LIST()
    }
};

static void tt_gpio_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    k->init = tt_gpio_init;
}

static DeviceInfo tt_gpio_info = {
    .name  = "tt_gpio",
    .size  = sizeof(tt_gpio_state),
    .vmsd  = &tt_gpio_vmsd,
    .class_init = tt_gpio_class_init,
    .reset = tt_gpio_reset,
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

typedef struct tt_key_state {
    SysBusDevice busdev;
    MemoryRegion mmio;
    uint32_t kbd_extended;
    uint32_t pressed_keys;
    qemu_irq out[8];
} tt_key_state;

static void tt_key_event(void *opaque, int keycode)
{
    tt_key_state *s = opaque;
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

static int tt_key_init(SysBusDevice *dev)
{
    tt_key_state *s = FROM_SYSBUS(tt_key_state, dev);

    sysbus_init_mmio(dev, &s->mmio);

    s->kbd_extended = 0;
    s->pressed_keys = 0;

    qdev_init_gpio_out(&dev->qdev, s->out, ARRAY_SIZE(s->out));

    qemu_add_kbd_event_handler(tt_key_event, s);

    return 0;
}

static const VMStateDescription tt_key_vmsd = {
    .name = "tt_key",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(kbd_extended, tt_key_state),
        VMSTATE_UINT32(pressed_keys, tt_key_state),
        VMSTATE_END_OF_LIST()
    }
};

static void tt_key_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    k->init = tt_key_init;
}

static DeviceInfo tt_key_info = {
    .name  = "tt_key",
    .size  = sizeof(tt_key_state),
    .vmsd  = &tt_key_vmsd,
    .class_init = tt_key_class_init,
};

static struct arm_boot_info tt_binfo = {
    .loader_start = 0,
    .loader_start = TT_SRAM_BASE,
    /* GO 730 */
    .board_id = 0x25d,
    .atag_revision = 0x0004000a,
};

static void tt_init(ram_addr_t ram_size,
               const char *boot_device,
               const char *kernel_filename, const char *kernel_cmdline,
               const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    TTState *s;
#if 0
    qemu_irq *cpu_pic;
    qemu_irq pic[32];
    DeviceState *dev;
    DeviceState *i2c_dev;
    DeviceState *lcd_dev;
    DeviceState *key_dev;
    DeviceState *wm8750_dev;
    SysBusDevice *s;
    i2c_bus *i2c;
    unsigned long flash_size;
    DriveInfo *dinfo;
    ram_addr_t ram_off;
    ram_addr_t sram_off;
#endif
    unsigned i;

    if (cpu_model && strcmp(cpu_model, "arm920t")) {
        fprintf(stderr, "only working with cpu arm920t\n");
        exit(1);
    }

    /* Allocate storage for board state. */
    s = g_new0(TTState, 1);

    for (i = 0; i < 3; i++) {
        if (serial_hds[i] == NULL) {
            char name[32];
            snprintf(name, sizeof(name), "serial%u", i);
            serial_hds[i] = qemu_chr_new(name, "vc:80Cx24C", NULL);
        }
    }

    /* Initialise SOC. */
    s->soc = s3c2440_init(ram_size);

    env = s->soc->cpu_env;
    //~ cpu_pic = arm_pic_init_cpu(env);

    //~ ram_off = qemu_ram_alloc(NULL, "arm920.ram", ram_size);
    //~ cpu_register_physical_memory(0x00000000, ram_size, ram_off | IO_MEM_RAM);
    //~ cpu_register_physical_memory(0x30000000, ram_size, ram_off | IO_MEM_RAM);
    //~ cpu_register_physical_memory(0x80000000, ram_size, ram_off | IO_MEM_RAM);
    //~ cpu_register_physical_memory(0xc0000000, ram_size, ram_off | IO_MEM_RAM);

    //~ tt_syscon_init();
    //~ tt_ioport_init();

#if 0
    dev = sysbus_create_simple("tt_gpio", MP_GPIO_BASE, pic[MP_GPIO_IRQ]);
    i2c_dev = sysbus_create_simple("gpio_i2c", 0, NULL);
    i2c = (i2c_bus *)qdev_get_child_bus(i2c_dev, "i2c");

    lcd_dev = sysbus_create_simple("tt_lcd", MP_LCD_BASE, NULL);
    key_dev = sysbus_create_simple("tt_key", 0, NULL);

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

    wm8750_dev = i2c_create_slave(i2c, "wm8750", MP_WM_ADDR);
    dev = qdev_create(NULL, "mv88w8618_audio");
    s = sysbus_from_qdev(dev);
    qdev_prop_set_ptr(dev, "wm8750", wm8750_dev);
    qdev_init_nofail(dev);
    sysbus_mmio_map(s, 0, MP_AUDIO_BASE);
    sysbus_connect_irq(s, 0, pic[MP_AUDIO_IRQ]);
#endif

    tt_binfo.ram_size = ram_size;
    tt_binfo.kernel_filename = kernel_filename;
    tt_binfo.kernel_cmdline = kernel_cmdline;
    tt_binfo.initrd_filename = initrd_filename;
    if (kernel_filename != NULL) {
        /* TODO: load ttsystem. */
        //~ sect_size = 0x11b778, sect_addr = 0x31700000
        //~ sect_size = 0x6a3f45, sect_addr = 0x31000000
        arm_load_kernel(env, &tt_binfo);
    }
}

static void tt_init_go(ram_addr_t ram_size,
               const char *boot_device,
               const char *kernel_filename, const char *kernel_cmdline,
               const char *initrd_filename, const char *cpu_model)
{
    tt_binfo.board_id = 0x25d;
    ram_size = 64 * MiB;
    tt_init(ram_size, boot_device,
            kernel_filename, kernel_cmdline,
            initrd_filename, cpu_model);
}

static void tt_init_666(ram_addr_t ram_size,
               const char *boot_device,
               const char *kernel_filename, const char *kernel_cmdline,
               const char *initrd_filename, const char *cpu_model)
{
    tt_binfo.board_id = 0x666;
    tt_init(ram_size, boot_device,
            kernel_filename, kernel_cmdline,
            initrd_filename, cpu_model);
}

static void tt_init_smdk2443(ram_addr_t ram_size,
               const char *boot_device,
               const char *kernel_filename, const char *kernel_cmdline,
               const char *initrd_filename, const char *cpu_model)
{
    tt_binfo.board_id = 0x43c;
    tt_init(ram_size, boot_device,
            kernel_filename, kernel_cmdline,
            initrd_filename, cpu_model);
}

static QEMUMachine tt_machine = {
    .name = "tt",
    .desc = "OpenTom (ARM920-T)",
    .init = tt_init_go,
};

static QEMUMachine tt_machine_666 = {
    .name = "tt666",
    .desc = "OpenTom (ARM920-T)",
    .init = tt_init_666,
};

static QEMUMachine tt_machine_smdk2443 = {
    .name = "smdk2443",
    .desc = "smdk2443 (ARM920-T)",
    .init = tt_init_smdk2443,
};

static void tt_machine_init(void)
{
    qemu_register_machine(&tt_machine);
    qemu_register_machine(&tt_machine_666);
    qemu_register_machine(&tt_machine_smdk2443);
}

machine_init(tt_machine_init);

static void tt_register_devices(void)
{
    sysbus_register_withprop(&mv88w8618_pic_info);
    sysbus_register_withprop(&mv88w8618_pit_info);
    sysbus_register_withprop(&mv88w8618_flashcfg_info);
    sysbus_register_withprop(&tt_lcd_info);
    sysbus_register_withprop(&tt_gpio_info);
    sysbus_register_withprop(&tt_key_info);
}

device_init(tt_register_devices)
