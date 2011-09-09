/* vim:set shiftwidth=4 ts=4 et: */
/*
 * PXA255 Sharp Zaurus SL-6000 PDA platform
 *
 * Copyright (c) 2008 Dmitry Baryshkov
 *
 * Code based on spitz platform by Andrzej Zaborowski <balrog@zabor.org>
 * This code is licensed under the GNU GPL v2.
 */

#include "hw.h"
#include "pxa.h"
#include "arm-misc.h"
#include "devices.h"
#include "sharpsl.h"
#include "pcmcia.h"
#include "block.h"
#include "boards.h"
#include "i2c.h"
#include "ssi.h"
#include "blockdev.h"
#include "sysbus.h"

#define TOSA_RAM    0x04000000
#define TOSA_ROM	0x00800000

#define TOSA_GPIO_USB_IN		(5)
#define TOSA_GPIO_nSD_DETECT	(9)
#define TOSA_GPIO_ON_RESET		(19)
#define TOSA_GPIO_CF_IRQ		(21)	/* CF slot0 Ready */
#define TOSA_GPIO_CF_CD			(13)
#define TOSA_GPIO_TC6393XB_INT  (15)
#define TOSA_GPIO_JC_CF_IRQ		(36)	/* CF slot1 Ready */

#define TOSA_SCOOP_GPIO_BASE	1
#define TOSA_GPIO_IR_POWERDWN	(TOSA_SCOOP_GPIO_BASE + 2)
#define TOSA_GPIO_SD_WP			(TOSA_SCOOP_GPIO_BASE + 3)
#define TOSA_GPIO_PWR_ON		(TOSA_SCOOP_GPIO_BASE + 4)

#define TOSA_SCOOP_JC_GPIO_BASE		1
#define TOSA_GPIO_BT_LED		(TOSA_SCOOP_JC_GPIO_BASE + 0)
#define TOSA_GPIO_NOTE_LED		(TOSA_SCOOP_JC_GPIO_BASE + 1)
#define TOSA_GPIO_CHRG_ERR_LED		(TOSA_SCOOP_JC_GPIO_BASE + 2)
#define TOSA_GPIO_TC6393XB_L3V_ON	(TOSA_SCOOP_JC_GPIO_BASE + 5)
#define TOSA_GPIO_WLAN_LED		(TOSA_SCOOP_JC_GPIO_BASE + 7)

#define	DAC_BASE	0x4e
#define DAC_CH1		0
#define DAC_CH2		1

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

static void tosa_out_switch(void *opaque, int line, int level)
{
    switch (line) {
        case 0:
            fprintf(stderr, "blue LED %s.\n", level ? "on" : "off");
            break;
        case 1:
            fprintf(stderr, "green LED %s.\n", level ? "on" : "off");
            break;
        case 2:
            fprintf(stderr, "amber LED %s.\n", level ? "on" : "off");
            break;
        case 3:
            fprintf(stderr, "wlan LED %s.\n", level ? "on" : "off");
            break;
        default:
            fprintf(stderr, "Uhandled out event: %d = %d\n", line, level);
            break;
    }
}


static void tosa_gpio_setup(PXA2xxState *cpu,
                DeviceState *scp0,
                DeviceState *scp1,
                TC6393xbState *tmio)
{
    qemu_irq *outsignals = qemu_allocate_irqs(tosa_out_switch, cpu, 4);
    /* MMC/SD host */
    pxa2xx_mmci_handlers(cpu->mmc,
                    qdev_get_gpio_in(scp0, TOSA_GPIO_SD_WP),
                    qemu_irq_invert(qdev_get_gpio_in(cpu->gpio, TOSA_GPIO_nSD_DETECT)));

    /* Handle reset */
    qdev_connect_gpio_out(cpu->gpio, TOSA_GPIO_ON_RESET, cpu->reset);

    /* PCMCIA signals: card's IRQ and Card-Detect */
    pxa2xx_pcmcia_set_irq_cb(cpu->pcmcia[0],
                        qdev_get_gpio_in(cpu->gpio, TOSA_GPIO_CF_IRQ),
                        qdev_get_gpio_in(cpu->gpio, TOSA_GPIO_CF_CD));

    pxa2xx_pcmcia_set_irq_cb(cpu->pcmcia[1],
                        qdev_get_gpio_in(cpu->gpio, TOSA_GPIO_JC_CF_IRQ),
                        NULL);

    qdev_connect_gpio_out(scp1, TOSA_GPIO_BT_LED, outsignals[0]);
    qdev_connect_gpio_out(scp1, TOSA_GPIO_NOTE_LED, outsignals[1]);
    qdev_connect_gpio_out(scp1, TOSA_GPIO_CHRG_ERR_LED, outsignals[2]);
    qdev_connect_gpio_out(scp1, TOSA_GPIO_WLAN_LED, outsignals[3]);

    qdev_connect_gpio_out(scp1, TOSA_GPIO_TC6393XB_L3V_ON, tc6393xb_l3v_get(tmio));

    /* UDC Vbus */
    qemu_irq_raise(qdev_get_gpio_in(cpu->gpio, TOSA_GPIO_USB_IN));
}

static uint32_t tosa_ssp_tansfer(SSISlave *dev, uint32_t value)
{
    fprintf(stderr, "TG: %d %02x\n", value >> 5, value & 0x1f);
    return 0;
}

static int tosa_ssp_init(SSISlave *dev)
{
    /* Nothing to do.  */
    return 0;
}

typedef struct {
    i2c_slave i2c;
    int len;
    char buf[3];
} TosaDACState;

static int tosa_dac_send(i2c_slave *i2c, uint8_t data)
{
    TosaDACState *s = FROM_I2C_SLAVE(TosaDACState, i2c);
    s->buf[s->len] = data;
    if (s->len ++ > 2) {
#ifdef VERBOSE
        fprintf(stderr, "%s: message too long (%i bytes)\n", __FUNCTION__, s->len);
#endif
        return 1;
    }

    if (s->len == 2) {
        fprintf(stderr, "dac: channel %d value 0x%02x\n",
                s->buf[0], s->buf[1]);
    }

    return 0;
}

static void tosa_dac_event(i2c_slave *i2c, enum i2c_event event)
{
    TosaDACState *s = FROM_I2C_SLAVE(TosaDACState, i2c);
    s->len = 0;
    switch (event) {
    case I2C_START_SEND:
        break;
    case I2C_START_RECV:
        printf("%s: recv not supported!!!\n", __FUNCTION__);
        break;
    case I2C_FINISH:
#ifdef VERBOSE
        if (s->len < 2)
            printf("%s: message too short (%i bytes)\n", __FUNCTION__, s->len);
        if (s->len > 2)
            printf("%s: message too long\n", __FUNCTION__);
#endif
        break;
    default:
        break;
    }
}

static int tosa_dac_recv(i2c_slave *s)
{
    printf("%s: recv not supported!!!\n", __FUNCTION__);
    return -1;
}

static int tosa_dac_init(i2c_slave *i2c)
{
    /* Nothing to do.  */
    return 0;
}

static void tosa_tg_init(PXA2xxState *cpu)
{
    i2c_bus *bus = pxa2xx_i2c_bus(cpu->i2c[0]);
    i2c_create_slave(bus, "tosa_dac", DAC_BASE);
    ssi_create_slave(cpu->ssp[1], "tosa-ssp");
}


static struct arm_boot_info tosa_binfo = {
    .loader_start = PXA2XX_SDRAM_BASE,
    .ram_size = 0x04000000,
};

static void tosa_init(ram_addr_t ram_size,
                const char *boot_device,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    PXA2xxState *cpu;
    TC6393xbState *tmio;
    DeviceState *scp0, *scp1;

    if (!cpu_model)
        cpu_model = "pxa255";

    cpu = pxa255_init(tosa_binfo.ram_size);

    cpu_register_physical_memory(0, TOSA_ROM,
                    qemu_ram_alloc(NULL, "tosa.rom", TOSA_ROM) | IO_MEM_ROM);

    tmio = tc6393xb_init(0x10000000,
            qdev_get_gpio_in(cpu->gpio, TOSA_GPIO_TC6393XB_INT));

    scp0 = sysbus_create_simple("scoop", 0x08800000, NULL);
    scp1 = sysbus_create_simple("scoop", 0x14800040, NULL);

    tosa_gpio_setup(cpu, scp0, scp1, tmio);

    tosa_microdrive_attach(cpu);

    tosa_tg_init(cpu);

    tosa_binfo.kernel_filename = kernel_filename;
    tosa_binfo.kernel_cmdline = kernel_cmdline;
    tosa_binfo.initrd_filename = initrd_filename;
    tosa_binfo.board_id = 0x208;
    arm_load_kernel(cpu->env, &tosa_binfo);
    sl_bootparam_write(SL_PXA_PARAM_BASE);
}

static QEMUMachine tosapda_machine = {
    .name = "tosa",
    .desc = "Tosa PDA (PXA255)",
    .init = tosa_init,
};

static void tosapda_machine_init(void)
{
    qemu_register_machine(&tosapda_machine);
}

machine_init(tosapda_machine_init);

static I2CSlaveInfo tosa_dac_info = {
    .qdev.name = "tosa_dac",
    .qdev.size = sizeof(TosaDACState),
    .init = tosa_dac_init,
    .event = tosa_dac_event,
    .recv = tosa_dac_recv,
    .send = tosa_dac_send
};

static SSISlaveInfo tosa_ssp_info = {
    .qdev.name = "tosa-ssp",
    .qdev.size = sizeof(SSISlave),
    .init = tosa_ssp_init,
    .transfer = tosa_ssp_tansfer
};

static void tosa_register_devices(void)
{
    i2c_register_slave(&tosa_dac_info);
    ssi_register_slave(&tosa_ssp_info);
}

device_init(tosa_register_devices)
