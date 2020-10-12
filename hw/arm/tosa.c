/* vim:set shiftwidth=4 ts=4 et: */
/*
 * PXA255 Sharp Zaurus SL-6000 PDA platform
 *
 * Copyright (c) 2008 Dmitry Baryshkov
 *
 * Code based on spitz platform by Andrzej Zaborowski <balrog@zabor.org>
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/runstate.h"
#include "hw/arm/pxa.h"
#include "hw/arm/boot.h"
#include "hw/arm/sharpsl.h"
#include "hw/pcmcia.h"
#include "hw/boards.h"
#include "hw/display/tc6393xb.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"
#include "hw/ssi/ssi.h"
#include "hw/sysbus.h"
#include "hw/misc/led.h"
#include "exec/address-spaces.h"
#include "qom/object.h"

#define TOSA_RAM 0x04000000
#define TOSA_ROM 0x00800000

#define TOSA_GPIO_USB_IN                (5)
#define TOSA_GPIO_nSD_DETECT            (9)
#define TOSA_GPIO_ON_RESET              (19)
#define TOSA_GPIO_CF_IRQ                (21)    /* CF slot0 Ready */
#define TOSA_GPIO_CF_CD                 (13)
#define TOSA_GPIO_TC6393XB_INT          (15)
#define TOSA_GPIO_JC_CF_IRQ             (36)    /* CF slot1 Ready */

#define TOSA_SCOOP_GPIO_BASE            1
#define TOSA_GPIO_IR_POWERDWN           (TOSA_SCOOP_GPIO_BASE + 2)
#define TOSA_GPIO_SD_WP                 (TOSA_SCOOP_GPIO_BASE + 3)
#define TOSA_GPIO_PWR_ON                (TOSA_SCOOP_GPIO_BASE + 4)

#define TOSA_SCOOP_JC_GPIO_BASE         1
#define TOSA_GPIO_BT_LED                (TOSA_SCOOP_JC_GPIO_BASE + 0)
#define TOSA_GPIO_NOTE_LED              (TOSA_SCOOP_JC_GPIO_BASE + 1)
#define TOSA_GPIO_CHRG_ERR_LED          (TOSA_SCOOP_JC_GPIO_BASE + 2)
#define TOSA_GPIO_TC6393XB_L3V_ON       (TOSA_SCOOP_JC_GPIO_BASE + 5)
#define TOSA_GPIO_WLAN_LED              (TOSA_SCOOP_JC_GPIO_BASE + 7)

#define DAC_BASE 0x4e
#define DAC_CH1 0
#define DAC_CH2 1

static void tosa_microdrive_attach(PXA2xxState *cpu)
{
    PCMCIACardState *md;
    DriveInfo *dinfo;

    dinfo = drive_get(IF_IDE, 0, 0);
    if (!dinfo || dinfo->media_cd)
        return;
    md = dscm1xxxx_init(dinfo);
    pxa2xx_pcmcia_attach(cpu->pcmcia[0], md);
}

/*
 * Encapsulation of some GPIO line behaviour for the Tosa board
 *
 * QEMU interface:
 *  + named GPIO inputs "leds[0..3]": assert to light LEDs
 *  + named GPIO input "reset": when asserted, resets the system
 */

#define TYPE_TOSA_MISC_GPIO "tosa-misc-gpio"
OBJECT_DECLARE_SIMPLE_TYPE(TosaMiscGPIOState, TOSA_MISC_GPIO)

struct TosaMiscGPIOState {
    SysBusDevice parent_obj;
};

static void tosa_reset(void *opaque, int line, int level)
{
    if (level) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static void tosa_misc_gpio_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);

    qdev_init_gpio_in_named(dev, tosa_reset, "reset", 1);
}

static void tosa_gpio_setup(PXA2xxState *cpu,
                DeviceState *scp0,
                DeviceState *scp1,
                TC6393xbState *tmio)
{
    DeviceState *misc_gpio;
    LEDState *led[4];

    misc_gpio = sysbus_create_simple(TYPE_TOSA_MISC_GPIO, -1, NULL);

    /* MMC/SD host */
    pxa2xx_mmci_handlers(cpu->mmc,
                    qdev_get_gpio_in(scp0, TOSA_GPIO_SD_WP),
                    qemu_irq_invert(qdev_get_gpio_in(cpu->gpio, TOSA_GPIO_nSD_DETECT)));

    /* Handle reset */
    qdev_connect_gpio_out(cpu->gpio, TOSA_GPIO_ON_RESET,
                          qdev_get_gpio_in_named(misc_gpio, "reset", 0));

    /* PCMCIA signals: card's IRQ and Card-Detect */
    pxa2xx_pcmcia_set_irq_cb(cpu->pcmcia[0],
                        qdev_get_gpio_in(cpu->gpio, TOSA_GPIO_CF_IRQ),
                        qdev_get_gpio_in(cpu->gpio, TOSA_GPIO_CF_CD));

    pxa2xx_pcmcia_set_irq_cb(cpu->pcmcia[1],
                        qdev_get_gpio_in(cpu->gpio, TOSA_GPIO_JC_CF_IRQ),
                        NULL);

    led[0] = led_create_simple(OBJECT(misc_gpio), GPIO_POLARITY_ACTIVE_HIGH,
                               LED_COLOR_BLUE, "bluetooth");
    led[1] = led_create_simple(OBJECT(misc_gpio), GPIO_POLARITY_ACTIVE_HIGH,
                               LED_COLOR_GREEN, "note");
    led[2] = led_create_simple(OBJECT(misc_gpio), GPIO_POLARITY_ACTIVE_HIGH,
                               LED_COLOR_AMBER, "charger-error");
    led[3] = led_create_simple(OBJECT(misc_gpio), GPIO_POLARITY_ACTIVE_HIGH,
                               LED_COLOR_GREEN, "wlan");

    qdev_connect_gpio_out(scp1, TOSA_GPIO_BT_LED,
                          qdev_get_gpio_in(DEVICE(led[0]), 0));
    qdev_connect_gpio_out(scp1, TOSA_GPIO_NOTE_LED,
                          qdev_get_gpio_in(DEVICE(led[1]), 0));
    qdev_connect_gpio_out(scp1, TOSA_GPIO_CHRG_ERR_LED,
                          qdev_get_gpio_in(DEVICE(led[2]), 0));
    qdev_connect_gpio_out(scp1, TOSA_GPIO_WLAN_LED,
                          qdev_get_gpio_in(DEVICE(led[3]), 0));

    qdev_connect_gpio_out(scp1, TOSA_GPIO_TC6393XB_L3V_ON, tc6393xb_l3v_get(tmio));

    /* UDC Vbus */
    qemu_irq_raise(qdev_get_gpio_in(cpu->gpio, TOSA_GPIO_USB_IN));
}

static uint32_t tosa_ssp_tansfer(SSIPeripheral *dev, uint32_t value)
{
    fprintf(stderr, "TG: %u %02x\n", value >> 5, value & 0x1f);
    return 0;
}

static void tosa_ssp_realize(SSIPeripheral *dev, Error **errp)
{
    /* Nothing to do.  */
}

#define TYPE_TOSA_DAC "tosa_dac"
OBJECT_DECLARE_SIMPLE_TYPE(TosaDACState, TOSA_DAC)

struct TosaDACState {
    I2CSlave parent_obj;

    int len;
    char buf[3];
};

static int tosa_dac_send(I2CSlave *i2c, uint8_t data)
{
    TosaDACState *s = TOSA_DAC(i2c);

    s->buf[s->len] = data;
    if (s->len ++ > 2) {
#ifdef VERBOSE
        fprintf(stderr, "%s: message too long (%i bytes)\n", __func__, s->len);
#endif
        return 1;
    }

    if (s->len == 2) {
        fprintf(stderr, "dac: channel %d value 0x%02x\n",
                s->buf[0], s->buf[1]);
    }

    return 0;
}

static int tosa_dac_event(I2CSlave *i2c, enum i2c_event event)
{
    TosaDACState *s = TOSA_DAC(i2c);

    s->len = 0;
    switch (event) {
    case I2C_START_SEND:
        break;
    case I2C_START_RECV:
        printf("%s: recv not supported!!!\n", __func__);
        break;
    case I2C_FINISH:
#ifdef VERBOSE
        if (s->len < 2)
            printf("%s: message too short (%i bytes)\n", __func__, s->len);
        if (s->len > 2)
            printf("%s: message too long\n", __func__);
#endif
        break;
    default:
        break;
    }

    return 0;
}

static uint8_t tosa_dac_recv(I2CSlave *s)
{
    printf("%s: recv not supported!!!\n", __func__);
    return 0xff;
}

static void tosa_tg_init(PXA2xxState *cpu)
{
    I2CBus *bus = pxa2xx_i2c_bus(cpu->i2c[0]);
    i2c_slave_create_simple(bus, TYPE_TOSA_DAC, DAC_BASE);
    ssi_create_peripheral(cpu->ssp[1], "tosa-ssp");
}


static struct arm_boot_info tosa_binfo = {
    .loader_start = PXA2XX_SDRAM_BASE,
    .ram_size = 0x04000000,
};

static void tosa_init(MachineState *machine)
{
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *rom = g_new(MemoryRegion, 1);
    PXA2xxState *mpu;
    TC6393xbState *tmio;
    DeviceState *scp0, *scp1;

    mpu = pxa255_init(address_space_mem, tosa_binfo.ram_size);

    memory_region_init_rom(rom, NULL, "tosa.rom", TOSA_ROM, &error_fatal);
    memory_region_add_subregion(address_space_mem, 0, rom);

    tmio = tc6393xb_init(address_space_mem, 0x10000000,
            qdev_get_gpio_in(mpu->gpio, TOSA_GPIO_TC6393XB_INT));

    scp0 = sysbus_create_simple("scoop", 0x08800000, NULL);
    scp1 = sysbus_create_simple("scoop", 0x14800040, NULL);

    tosa_gpio_setup(mpu, scp0, scp1, tmio);

    tosa_microdrive_attach(mpu);

    tosa_tg_init(mpu);

    tosa_binfo.board_id = 0x208;
    arm_load_kernel(mpu->cpu, machine, &tosa_binfo);
    sl_bootparam_write(SL_PXA_PARAM_BASE);
}

static void tosapda_machine_init(MachineClass *mc)
{
    mc->desc = "Sharp SL-6000 (Tosa) PDA (PXA255)";
    mc->init = tosa_init;
    mc->block_default_type = IF_IDE;
    mc->ignore_memory_transaction_failures = true;
}

DEFINE_MACHINE("tosa", tosapda_machine_init)

static void tosa_dac_class_init(ObjectClass *klass, void *data)
{
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = tosa_dac_event;
    k->recv = tosa_dac_recv;
    k->send = tosa_dac_send;
}

static const TypeInfo tosa_dac_info = {
    .name          = TYPE_TOSA_DAC,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(TosaDACState),
    .class_init    = tosa_dac_class_init,
};

static void tosa_ssp_class_init(ObjectClass *klass, void *data)
{
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);

    k->realize = tosa_ssp_realize;
    k->transfer = tosa_ssp_tansfer;
}

static const TypeInfo tosa_ssp_info = {
    .name          = "tosa-ssp",
    .parent        = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(SSIPeripheral),
    .class_init    = tosa_ssp_class_init,
};

static const TypeInfo tosa_misc_gpio_info = {
    .name          = TYPE_TOSA_MISC_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TosaMiscGPIOState),
    .instance_init = tosa_misc_gpio_init,
    /*
     * No class init required: device has no internal state so does not
     * need to set up reset or vmstate, and has no realize method.
     */
};

static void tosa_register_types(void)
{
    type_register_static(&tosa_dac_info);
    type_register_static(&tosa_ssp_info);
    type_register_static(&tosa_misc_gpio_info);
}

type_init(tosa_register_types)
