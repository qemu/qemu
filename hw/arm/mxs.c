/*
 * mxs.c
 *
 * Copyright: Michel Pollet <buserror@gmail.com>
 *
 * QEMU Licence
 */

#include "hw/sysbus.h"
#include "hw/arm/mxs.h"
#include "hw/arm/arm.h"
#include "target-arm/cpu.h"
#include "hw/boards.h"

#include "exec/address-spaces.h"

#define D(w)
//#define D(w) w
/*
 * 	0x00000000 - 0x00007fff	On Chip SRAM
 * 			   - 0x5fffffff	External DRAM
 * 	0x60000000 - 0x7fffffff	Default Slave
 * 	0x80000000 - 0x800fffff	Peripheral Space (128KB)
 * 		0x80000000 0x8000		APBH
 * 		----------------------------
 * 		0x80000000 0x2000		icol
 * 		0x80004000 0x2000		DMA
 * 		0x80008000 0x2000		ECC
 * 		0x8000c000 0x2000		GPMI-NAND
 * 		0x8000a000 0x2000		GPMI-NAND
 * 		0x80010000 0x2000		SSP0
 * 		0x80014000 0x2000		ETM
 * 		0x80018000 0x2000		pinctrl
 * 		0x8001c000 0x2000		digctl
 * 		0x80020000 0x2000		EMI
 * 		0x80024000 0x2000		DMA APBX
 * 		0x80028000 0x2000		DCP
 * 		0x8002a000 0x2000		PXP
 * 		0x8002c000 0x2000		ocotp
 * 		0x8002e000 0x2000		axi-ahb
 * 		0x80030000 0x2000		lcdif
 * 		0x80034000 0x2000		SSP1
 * 		0x80038000 0x2000		TVEnc
 *
 * 		0x80040000 0x40000		APBX
 * 		----------------------------
 * 		0x80040000 0x2000		clkctrl
 * 		0x80042000 0x2000		saif0
 * 		0x80044000 0x2000		power
 * 		0x80046000 0x2000		saif1
 * 		0x80048000 0x2000		audio-out
 * 		0x8004c000 0x2000		audio-in
 * 		0x80050000 0x2000		LRADC
 * 		0x80054000 0x2000		SPDIF
 * 		0x80058000 0x2000		i2c
 * 		0x8005c000 0x2000		RTC	fsl,imx23-rtc - fsl,stmp3xxx-rtc
 * 		0x80064000 0x2000		PWM
 * 		0x80068000 0x2000		Timrot
 * 		0x8006c000 0x2000		UART0
 * 		0x8006e000 0x2000		UART1
 * 		0x80070000 0x2000		DUART PL011
 * 		0x8007c000 0x2000		USB PHY
 * 	0x80100000 - 0xc0000000	Default Slave
 * 	0xc0000000 - 0xfffeffff	ROM Alias
 * 	0xffff0000 - 0xffffffff  On Chip ROM
 */

enum {
    HW_CLKCTRL_CPU = 2,
    HW_CLKCTRL_HBUS = 3,
    HW_CLKCTRL_XBUS = 4,
    HW_CLKCTRL_XTAL = 0x5,
    HW_CLKCTRL_PIX = 0x6,
    HW_CLKCTRL_SSP = 0x7,
    HW_CLKCTRL_GPMI = 0x8,
    HW_CLKCTRL_SPDIF = 0x9,
    HW_CLKCTRL_EMI = 0xa,
    HW_CLKCTRL_SAIF = 0xc,
    HW_CLKCTRL_TV = 0xd,
    HW_CLKCTRL_ETM = 0xe,
    HW_CLKCTRL_FRAC = 0xf,
    HW_CLKCTRL_FRAC1 = 0x10,
    HW_CLKCTRL_CLKSEQ = 0x11,
    HW_CLKCTRL_RESET = 0x12,
    HW_CLKCTRL_STATUS = 0x13,
    HW_CLKCTRL_VERSION = 0x14,
    HW_CLKCTRL_MAX
};
typedef struct imx23_clkctrl_state {
    SysBusDevice busdev;
    MemoryRegion iomem;
    uint32_t r[HW_CLKCTRL_MAX];
} imx23_clkctrl_state;

static uint64_t imx23_clkctrl_read(
        void *opaque, hwaddr offset, unsigned size)
{
    imx23_clkctrl_state *s = (imx23_clkctrl_state *) opaque;
    uint32_t res = 0;

    switch (offset >> 4) {
        case 0 ... HW_CLKCTRL_MAX:
            res = s->r[offset >> 4];
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            break;
    }
    return res;
}

static void imx23_clkctrl_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    imx23_clkctrl_state *s = (imx23_clkctrl_state *) opaque;

    switch (offset >> 4) {
        case 0 ... HW_CLKCTRL_MAX:
            if ((offset >> 4) == HW_CLKCTRL_RESET)
                printf("QEMU: %s OS reset, ignored\n", __func__);
            mxs_write(&s->r[offset >> 4], offset, value, size);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            break;
    }
}

static const MemoryRegionOps imx23_clkctrl_ops = {
    .read = imx23_clkctrl_read,
    .write = imx23_clkctrl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void imx23_clkctrl_reset(imx23_clkctrl_state *s)
{
    memset(s->r, 0, sizeof(s->r));
    /*
     * These are default values for most of the clock. the
     * linux init code does rely on a few of these to be
     * happy
     */
    s->r[HW_CLKCTRL_CPU] = 0x00010001;
    s->r[HW_CLKCTRL_HBUS] = 0x00000001;
    s->r[HW_CLKCTRL_XBUS] = 0x00000001;
    s->r[HW_CLKCTRL_XTAL] = 0x70000001;
    s->r[HW_CLKCTRL_PIX] = 0x80000001;
    s->r[HW_CLKCTRL_SSP] = 0x80000001;
    s->r[HW_CLKCTRL_GPMI] = 0x80000001;
    s->r[HW_CLKCTRL_SPDIF] = 0x80000000;
    s->r[HW_CLKCTRL_EMI] = 0x80000101;
    s->r[HW_CLKCTRL_SAIF] = 0x80000001;
    s->r[HW_CLKCTRL_TV] = 0x80000001;
    s->r[HW_CLKCTRL_ETM] = 0x80000001;
    s->r[HW_CLKCTRL_FRAC] = 0x92929292;
    s->r[HW_CLKCTRL_FRAC1] = 0x80000000;
    s->r[HW_CLKCTRL_CLKSEQ] = 0x0000001f;
    s->r[HW_CLKCTRL_VERSION] = 0x04000000;
}

static int imx23_clkctrl_init(SysBusDevice *dev)
{
    imx23_clkctrl_state *s = OBJECT_CHECK(imx23_clkctrl_state, dev, "imx23_clkctrl");

    memory_region_init_io(&s->iomem, OBJECT(s), &imx23_clkctrl_ops, s,
            "imx23_clkctrl", 0x2000);
    sysbus_init_mmio(dev, &s->iomem);
    imx23_clkctrl_reset(s);
    return 0;
}

static void imx23_clkctrl_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = imx23_clkctrl_init;
}

static TypeInfo clkctrl_info = {
    .name          = "imx23_clkctrl",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(imx23_clkctrl_state),
    .class_init    = imx23_clkctrl_class_init,
};

static void imx23_clkctrl_register(void)
{
    type_register_static(&clkctrl_info);
}

type_init(imx23_clkctrl_register)

/*
 * The 'catchall' device block is partly for debugging purpose, and
 * partly to sort out issues with 'lone registers' that are checked
 * in blocks that appear to be outside dedicated peripheral space
 *
 * One such is the AMBA signature for the PL011 serial port, where
 * linux relies of finding identifiers when qemu's pl011 doesn't reply
 *
 * Similartly, the USB block has a couple of "non EHCI compliant"
 * registers that are needed to make the EHCI/imx driver happy
 */
typedef struct imx23_catchall_state {
    SysBusDevice busdev;
    MemoryRegion iomem;
} imx23_catchall_state;

static uint64_t imx23_catchall_read(
        void *opaque, hwaddr offset, unsigned size)
{
    //  imx23_catchall_state *s = (imx23_catchall_state *)opaque;
    uint32_t res = 0;

    // AMBA signature is not read by the pl11 serial driver, this is a workaround
    const uint8_t cid[] = { 0x0d, 0xf0, 0x05, 0xb1 };
    const uint8_t pid[] = { 0x11, 0x10, 0x34, 0x00 };
    switch (offset) {
        case 0x71fe0 ... 0x71fec:
            res = pid[(offset - 0x71fe0) >> 2];
            break;
        case 0x71ff0 ... 0x71ffc:
            res = cid[(offset - 0x71ff0) >> 2];
            break;
        case 0x80120: // HW_USBCTRL_DCIVERSION
            res = 0x00000001;
            break;
        case 0x80124: // HW_USBCTRL_DCCPARAMS non ehci compliant
            res = 0x00000185; // host & device bits
            break;
        default:
            D(printf("%s %04x (%d) = ", __func__, (int) offset, size);
            printf("%08x\n", res);)
            break;
    }
    return res;
}

static void imx23_catchall_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    //  imx23_catchall_state *s = (imx23_catchall_state *)opaque;

    D(printf("%s %04x %08x(%d)\n", __func__, (int) offset, (int) value, size);)
}

static const MemoryRegionOps imx23_catchall_ops = {
    .read = imx23_catchall_read,
    .write = imx23_catchall_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int imx23_catchall_init(SysBusDevice *dev)
{
    imx23_catchall_state *s = OBJECT_CHECK(imx23_catchall_state, dev, "imx23_catchall");

    memory_region_init_io(&s->iomem, OBJECT(s), &imx23_catchall_ops, s,
            "imx23_catchall", 0x82000);
    sysbus_init_mmio(dev, &s->iomem);
    return 0;
}

static void imx23_catchall_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = imx23_catchall_init;
}

static TypeInfo catchall_info = {
    .name          = "imx23_catchall",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(imx23_catchall_state),
    .class_init    = imx23_catchall_class_init,
};

static void imx23_catchall_register(void)
{
    type_register_static(&catchall_info);
}

type_init(imx23_catchall_register)

ARMCPU * imx233_init(struct arm_boot_info * board_info);

/*
 * Creates an "empty" imx23, with the peripherals, and nothing
 * else attached. Pass in a partially filled up board_info; currently
 * only the ram_size field is used.
 */
ARMCPU * imx233_init(struct arm_boot_info * board_info)
{
    ARMCPU *cpu;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
//    qemu_irq *cpu_pic;
    DeviceState *icoll;

    cpu = cpu_arm_init("arm926");
    if (!cpu) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }

    /* On a real system, the first 32k is a `onboard sram' */
    //  printf("%s ram size : %dMB\n", __func__, (int)ram_size / 1024 / 1024);
    memory_region_init_ram(ram, NULL, "imx233.ram", board_info->ram_size);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(address_space_mem, 0x0, ram);

    sysbus_create_simple("imx23_catchall", MX23_IO_BASE_ADDR, 0);

//    cpu_pic = arm_pic_init_cpu(cpu);

    sysbus_create_simple("imx23_clkctrl", MX23_CLKCTRL_BASE_ADDR, 0);

    icoll = sysbus_create_varargs("mxs_icoll", MX23_ICOLL_BASE_ADDR,
            qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ),
            qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_FIQ), NULL);

    sysbus_create_varargs("mxs_timrot", MX23_TIMROT_BASE_ADDR,
            qdev_get_gpio_in(icoll, MX23_INT_TIMER0),
            qdev_get_gpio_in(icoll, MX23_INT_TIMER1),
            qdev_get_gpio_in(icoll, MX23_INT_TIMER2),
            qdev_get_gpio_in(icoll, MX23_INT_TIMER3),
            NULL);

    sysbus_create_simple("imx23_digctl", MX23_DIGCTL_BASE_ADDR, 0);
    sysbus_create_varargs("imx23_pinctrl", MX23_PINCTRL_BASE_ADDR,
            qdev_get_gpio_in(icoll, MX23_INT_GPIO0),
            qdev_get_gpio_in(icoll, MX23_INT_GPIO1),
            qdev_get_gpio_in(icoll, MX23_INT_GPIO2),
            NULL);

    sysbus_create_simple("pl011", MX23_DUART_BASE_ADDR,
            qdev_get_gpio_in(icoll, MX23_INT_DUART));
    /*
     *  enable the port, like the bootloader would
     */
    {
        uint32_t enable = 0x301;
        cpu_physical_memory_rw(MX23_DUART_BASE_ADDR + 0x4 /* CR */,
                (uint8_t*) &enable, 4, 1);
    }
    sysbus_create_varargs("mxs_uart", MX23_AUART1_BASE_ADDR,
            qdev_get_gpio_in(icoll, MX23_INT_AUART1),
            NULL);
    sysbus_create_varargs("mxs_uart", MX23_AUART2_BASE_ADDR,
            qdev_get_gpio_in(icoll, MX23_INT_AUART2),
            NULL);
    sysbus_create_varargs("mxs_rtc", MX23_RTC_BASE_ADDR,
            qdev_get_gpio_in(icoll, MX23_INT_RTC_ALARM),
            NULL);
    sysbus_create_varargs("mxs_usb", MX23_USBCTRL_BASE_ADDR,
            qdev_get_gpio_in(icoll, MX23_INT_USB_CTRL),
            NULL);
    sysbus_create_simple("mxs_usbphy", MX23_USBPHY_BASE_ADDR, 0);

    sysbus_create_varargs("mxs_apbh_dma", MX23_APBH_DMA_BASE_ADDR,
            qdev_get_gpio_in(icoll, MX23_INT_SSP1_DMA),
            qdev_get_gpio_in(icoll, MX23_INT_SSP2_DMA),
            NULL);
    sysbus_create_varargs("mxs_apbx_dma", MX23_APBX_DMA_BASE_ADDR,
            qdev_get_gpio_in(icoll, MX23_INT_ADC_DMA),
            qdev_get_gpio_in(icoll, MX23_INT_DAC_DMA),
            qdev_get_gpio_in(icoll, MX23_INT_SPDIF_DMA),
            qdev_get_gpio_in(icoll, MX23_INT_I2C_DMA),
            qdev_get_gpio_in(icoll, MX23_INT_SAIF1_DMA),
            qdev_get_gpio_in(icoll, MX23_INT_AUART1_RX_DMA),
            qdev_get_gpio_in(icoll, MX23_INT_AUART1_TX_DMA),
            qdev_get_gpio_in(icoll, MX23_INT_AUART2_RX_DMA),
            qdev_get_gpio_in(icoll, MX23_INT_AUART2_TX_DMA),
            qdev_get_gpio_in(icoll, MX23_INT_SAIF2_DMA),
            NULL);
    sysbus_create_varargs("mxs_ssp", MX23_SSP1_BASE_ADDR,
            qdev_get_gpio_in(icoll, MX23_INT_SSP1_DMA),
            qdev_get_gpio_in(icoll, MX23_INT_SSP1_ERROR),
            NULL);
    sysbus_create_varargs("mxs_ssp", MX23_SSP2_BASE_ADDR,
            qdev_get_gpio_in(icoll, MX23_INT_SSP2_DMA),
            qdev_get_gpio_in(icoll, MX23_INT_SSP2_ERROR),
            NULL);

    return cpu;
}

