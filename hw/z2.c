/*
 * PXA270-based Zipit Z2 device
 *
 * Copyright (c) 2011 by Vasily Khoruzhick <anarsoul@gmail.com>
 *
 * Code is based on mainstone platform.
 *
 * This code is licensed under the GNU GPL v2.
 */

#include "hw.h"
#include "pxa.h"
#include "arm-misc.h"
#include "devices.h"
#include "i2c.h"
#include "ssi.h"
#include "boards.h"
#include "sysemu.h"
#include "flash.h"
#include "blockdev.h"
#include "console.h"
#include "audio/audio.h"
#include "exec-memory.h"

#ifdef DEBUG_Z2
#define DPRINTF(fmt, ...) \
        printf(fmt, ## __VA_ARGS__)
#else
#define DPRINTF(fmt, ...)
#endif

static struct keymap map[0x100] = {
    [0 ... 0xff] = { -1, -1 },
    [0x3b] = {0, 0}, /* Option = F1 */
    [0xc8] = {0, 1}, /* Up */
    [0xd0] = {0, 2}, /* Down */
    [0xcb] = {0, 3}, /* Left */
    [0xcd] = {0, 4}, /* Right */
    [0xcf] = {0, 5}, /* End */
    [0x0d] = {0, 6}, /* KPPLUS */
    [0xc7] = {1, 0}, /* Home */
    [0x10] = {1, 1}, /* Q */
    [0x17] = {1, 2}, /* I */
    [0x22] = {1, 3}, /* G */
    [0x2d] = {1, 4}, /* X */
    [0x1c] = {1, 5}, /* Enter */
    [0x0c] = {1, 6}, /* KPMINUS */
    [0xc9] = {2, 0}, /* PageUp */
    [0x11] = {2, 1}, /* W */
    [0x18] = {2, 2}, /* O */
    [0x23] = {2, 3}, /* H */
    [0x2e] = {2, 4}, /* C */
    [0x38] = {2, 5}, /* LeftAlt */
    [0xd1] = {3, 0}, /* PageDown */
    [0x12] = {3, 1}, /* E */
    [0x19] = {3, 2}, /* P */
    [0x24] = {3, 3}, /* J */
    [0x2f] = {3, 4}, /* V */
    [0x2a] = {3, 5}, /* LeftShift */
    [0x01] = {4, 0}, /* Esc */
    [0x13] = {4, 1}, /* R */
    [0x1e] = {4, 2}, /* A */
    [0x25] = {4, 3}, /* K */
    [0x30] = {4, 4}, /* B */
    [0x1d] = {4, 5}, /* LeftCtrl */
    [0x0f] = {5, 0}, /* Tab */
    [0x14] = {5, 1}, /* T */
    [0x1f] = {5, 2}, /* S */
    [0x26] = {5, 3}, /* L */
    [0x31] = {5, 4}, /* N */
    [0x39] = {5, 5}, /* Space */
    [0x3c] = {6, 0}, /* Stop = F2 */
    [0x15] = {6, 1}, /* Y */
    [0x20] = {6, 2}, /* D */
    [0x0e] = {6, 3}, /* Backspace */
    [0x32] = {6, 4}, /* M */
    [0x33] = {6, 5}, /* Comma */
    [0x3d] = {7, 0}, /* Play = F3 */
    [0x16] = {7, 1}, /* U */
    [0x21] = {7, 2}, /* F */
    [0x2c] = {7, 3}, /* Z */
    [0x27] = {7, 4}, /* Semicolon */
    [0x34] = {7, 5}, /* Dot */
};

#define Z2_RAM_SIZE     0x02000000
#define Z2_FLASH_BASE   0x00000000
#define Z2_FLASH_SIZE   0x00800000

static struct arm_boot_info z2_binfo = {
    .loader_start   = PXA2XX_SDRAM_BASE,
    .ram_size       = Z2_RAM_SIZE,
};

#define Z2_GPIO_SD_DETECT   96
#define Z2_GPIO_AC_IN       0
#define Z2_GPIO_KEY_ON      1
#define Z2_GPIO_LCD_CS      88

typedef struct {
    SSISlave ssidev;
    int32_t selected;
    int32_t enabled;
    uint8_t buf[3];
    uint32_t cur_reg;
    int pos;
} ZipitLCD;

static uint32_t zipit_lcd_transfer(SSISlave *dev, uint32_t value)
{
    ZipitLCD *z = FROM_SSI_SLAVE(ZipitLCD, dev);
    uint16_t val;
    if (z->selected) {
        z->buf[z->pos] = value & 0xff;
        z->pos++;
    }
    if (z->pos == 3) {
        switch (z->buf[0]) {
        case 0x74:
            DPRINTF("%s: reg: 0x%.2x\n", __func__, z->buf[2]);
            z->cur_reg = z->buf[2];
            break;
        case 0x76:
            val = z->buf[1] << 8 | z->buf[2];
            DPRINTF("%s: value: 0x%.4x\n", __func__, val);
            if (z->cur_reg == 0x22 && val == 0x0000) {
                z->enabled = 1;
                printf("%s: LCD enabled\n", __func__);
            } else if (z->cur_reg == 0x10 && val == 0x0000) {
                z->enabled = 0;
                printf("%s: LCD disabled\n", __func__);
            }
            break;
        default:
            DPRINTF("%s: unknown command!\n", __func__);
            break;
        }
        z->pos = 0;
    }
    return 0;
}

static void z2_lcd_cs(void *opaque, int line, int level)
{
    ZipitLCD *z2_lcd = opaque;
    z2_lcd->selected = !level;
}

static int zipit_lcd_init(SSISlave *dev)
{
    ZipitLCD *z = FROM_SSI_SLAVE(ZipitLCD, dev);
    z->selected = 0;
    z->enabled = 0;
    z->pos = 0;

    return 0;
}

static VMStateDescription vmstate_zipit_lcd_state = {
    .name = "zipit-lcd",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(selected, ZipitLCD),
        VMSTATE_INT32(enabled, ZipitLCD),
        VMSTATE_BUFFER(buf, ZipitLCD),
        VMSTATE_UINT32(cur_reg, ZipitLCD),
        VMSTATE_INT32(pos, ZipitLCD),
        VMSTATE_END_OF_LIST(),
    }
};

static SSISlaveInfo zipit_lcd_info = {
    .qdev.name = "zipit-lcd",
    .qdev.size = sizeof(ZipitLCD),
    .qdev.vmsd = &vmstate_zipit_lcd_state,
    .init = zipit_lcd_init,
    .transfer = zipit_lcd_transfer
};

typedef struct {
    i2c_slave i2c;
    int len;
    uint8_t buf[3];
} AER915State;

static int aer915_send(i2c_slave *i2c, uint8_t data)
{
    AER915State *s = FROM_I2C_SLAVE(AER915State, i2c);
    s->buf[s->len] = data;
    if (s->len++ > 2) {
        DPRINTF("%s: message too long (%i bytes)\n",
            __func__, s->len);
        return 1;
    }

    if (s->len == 2) {
        DPRINTF("%s: reg %d value 0x%02x\n", __func__,
                s->buf[0], s->buf[1]);
    }

    return 0;
}

static void aer915_event(i2c_slave *i2c, enum i2c_event event)
{
    AER915State *s = FROM_I2C_SLAVE(AER915State, i2c);
    switch (event) {
    case I2C_START_SEND:
        s->len = 0;
        break;
    case I2C_START_RECV:
        if (s->len != 1) {
            DPRINTF("%s: short message!?\n", __func__);
        }
        break;
    case I2C_FINISH:
        break;
    default:
        break;
    }
}

static int aer915_recv(i2c_slave *slave)
{
    int retval = 0x00;
    AER915State *s = FROM_I2C_SLAVE(AER915State, slave);

    switch (s->buf[0]) {
    /* Return hardcoded battery voltage,
     * 0xf0 means ~4.1V
     */
    case 0x02:
        retval = 0xf0;
        break;
    /* Return 0x00 for other regs,
     * we don't know what they are for,
     * anyway they return 0x00 on real hardware.
     */
    default:
        break;
    }

    return retval;
}

static int aer915_init(i2c_slave *i2c)
{
    /* Nothing to do.  */
    return 0;
}

static VMStateDescription vmstate_aer915_state = {
    .name = "aer915",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(len, AER915State),
        VMSTATE_BUFFER(buf, AER915State),
        VMSTATE_END_OF_LIST(),
    }
};

static I2CSlaveInfo aer915_info = {
    .qdev.name = "aer915",
    .qdev.size = sizeof(AER915State),
    .qdev.vmsd = &vmstate_aer915_state,
    .init = aer915_init,
    .event = aer915_event,
    .recv = aer915_recv,
    .send = aer915_send
};

static void z2_init(ram_addr_t ram_size,
                const char *boot_device,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    MemoryRegion *address_space_mem = get_system_memory();
    uint32_t sector_len = 0x10000;
    PXA2xxState *cpu;
    DriveInfo *dinfo;
    int be;
    void *z2_lcd;
    i2c_bus *bus;
    DeviceState *wm;

    if (!cpu_model) {
        cpu_model = "pxa270-c5";
    }

    /* Setup CPU & memory */
    cpu = pxa270_init(address_space_mem, z2_binfo.ram_size, cpu_model);

#ifdef TARGET_WORDS_BIGENDIAN
    be = 1;
#else
    be = 0;
#endif
    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (!dinfo) {
        fprintf(stderr, "Flash image must be given with the "
                "'pflash' parameter\n");
        exit(1);
    }

    if (!pflash_cfi01_register(Z2_FLASH_BASE,
                               NULL, "z2.flash0", Z2_FLASH_SIZE,
                               dinfo->bdrv, sector_len,
                               Z2_FLASH_SIZE / sector_len, 4, 0, 0, 0, 0,
                               be)) {
        fprintf(stderr, "qemu: Error registering flash memory.\n");
        exit(1);
    }

    /* setup keypad */
    pxa27x_register_keypad(cpu->kp, map, 0x100);

    /* MMC/SD host */
    pxa2xx_mmci_handlers(cpu->mmc,
        NULL,
        qdev_get_gpio_in(cpu->gpio, Z2_GPIO_SD_DETECT));

    ssi_register_slave(&zipit_lcd_info);
    i2c_register_slave(&aer915_info);
    z2_lcd = ssi_create_slave(cpu->ssp[1], "zipit-lcd");
    bus = pxa2xx_i2c_bus(cpu->i2c[0]);
    i2c_create_slave(bus, "aer915", 0x55);
    wm = i2c_create_slave(bus, "wm8750", 0x1b);
    cpu->i2s->opaque = wm;
    cpu->i2s->codec_out = wm8750_dac_dat;
    cpu->i2s->codec_in = wm8750_adc_dat;
    wm8750_data_req_set(wm, cpu->i2s->data_req, cpu->i2s);

    qdev_connect_gpio_out(cpu->gpio, Z2_GPIO_LCD_CS,
        qemu_allocate_irqs(z2_lcd_cs, z2_lcd, 1)[0]);

    if (kernel_filename) {
        z2_binfo.kernel_filename = kernel_filename;
        z2_binfo.kernel_cmdline = kernel_cmdline;
        z2_binfo.initrd_filename = initrd_filename;
        z2_binfo.board_id = 0x6dd;
        arm_load_kernel(cpu->env, &z2_binfo);
    }
}

static QEMUMachine z2_machine = {
    .name = "z2",
    .desc = "Zipit Z2 (PXA27x)",
    .init = z2_init,
};

static void z2_machine_init(void)
{
    qemu_register_machine(&z2_machine);
}

machine_init(z2_machine_init);
