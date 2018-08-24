/*
 * Arm IoT Kit
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/arm/iotkit.h"
#include "hw/arm/arm.h"

/* Clock frequency in HZ of the 32KHz "slow clock" */
#define S32KCLK (32 * 1000)

/* Create an alias region of @size bytes starting at @base
 * which mirrors the memory starting at @orig.
 */
static void make_alias(IoTKit *s, MemoryRegion *mr, const char *name,
                       hwaddr base, hwaddr size, hwaddr orig)
{
    memory_region_init_alias(mr, NULL, name, &s->container, orig, size);
    /* The alias is even lower priority than unimplemented_device regions */
    memory_region_add_subregion_overlap(&s->container, base, mr, -1500);
}

static void irq_status_forwarder(void *opaque, int n, int level)
{
    qemu_irq destirq = opaque;

    qemu_set_irq(destirq, level);
}

static void nsccfg_handler(void *opaque, int n, int level)
{
    IoTKit *s = IOTKIT(opaque);

    s->nsccfg = level;
}

static void iotkit_forward_ppc(IoTKit *s, const char *ppcname, int ppcnum)
{
    /* Each of the 4 AHB and 4 APB PPCs that might be present in a
     * system using the IoTKit has a collection of control lines which
     * are provided by the security controller and which we want to
     * expose as control lines on the IoTKit device itself, so the
     * code using the IoTKit can wire them up to the PPCs.
     */
    SplitIRQ *splitter = &s->ppc_irq_splitter[ppcnum];
    DeviceState *iotkitdev = DEVICE(s);
    DeviceState *dev_secctl = DEVICE(&s->secctl);
    DeviceState *dev_splitter = DEVICE(splitter);
    char *name;

    name = g_strdup_printf("%s_nonsec", ppcname);
    qdev_pass_gpios(dev_secctl, iotkitdev, name);
    g_free(name);
    name = g_strdup_printf("%s_ap", ppcname);
    qdev_pass_gpios(dev_secctl, iotkitdev, name);
    g_free(name);
    name = g_strdup_printf("%s_irq_enable", ppcname);
    qdev_pass_gpios(dev_secctl, iotkitdev, name);
    g_free(name);
    name = g_strdup_printf("%s_irq_clear", ppcname);
    qdev_pass_gpios(dev_secctl, iotkitdev, name);
    g_free(name);

    /* irq_status is a little more tricky, because we need to
     * split it so we can send it both to the security controller
     * and to our OR gate for the NVIC interrupt line.
     * Connect up the splitter's outputs, and create a GPIO input
     * which will pass the line state to the input splitter.
     */
    name = g_strdup_printf("%s_irq_status", ppcname);
    qdev_connect_gpio_out(dev_splitter, 0,
                          qdev_get_gpio_in_named(dev_secctl,
                                                 name, 0));
    qdev_connect_gpio_out(dev_splitter, 1,
                          qdev_get_gpio_in(DEVICE(&s->ppc_irq_orgate), ppcnum));
    s->irq_status_in[ppcnum] = qdev_get_gpio_in(dev_splitter, 0);
    qdev_init_gpio_in_named_with_opaque(iotkitdev, irq_status_forwarder,
                                        s->irq_status_in[ppcnum], name, 1);
    g_free(name);
}

static void iotkit_forward_sec_resp_cfg(IoTKit *s)
{
    /* Forward the 3rd output from the splitter device as a
     * named GPIO output of the iotkit object.
     */
    DeviceState *dev = DEVICE(s);
    DeviceState *dev_splitter = DEVICE(&s->sec_resp_splitter);

    qdev_init_gpio_out_named(dev, &s->sec_resp_cfg, "sec_resp_cfg", 1);
    s->sec_resp_cfg_in = qemu_allocate_irq(irq_status_forwarder,
                                           s->sec_resp_cfg, 1);
    qdev_connect_gpio_out(dev_splitter, 2, s->sec_resp_cfg_in);
}

static void iotkit_init(Object *obj)
{
    IoTKit *s = IOTKIT(obj);
    int i;

    memory_region_init(&s->container, obj, "iotkit-container", UINT64_MAX);

    sysbus_init_child_obj(obj, "armv7m", &s->armv7m, sizeof(s->armv7m),
                          TYPE_ARMV7M);
    qdev_prop_set_string(DEVICE(&s->armv7m), "cpu-type",
                         ARM_CPU_TYPE_NAME("cortex-m33"));

    sysbus_init_child_obj(obj, "secctl", &s->secctl, sizeof(s->secctl),
                          TYPE_IOTKIT_SECCTL);
    sysbus_init_child_obj(obj, "apb-ppc0", &s->apb_ppc0, sizeof(s->apb_ppc0),
                          TYPE_TZ_PPC);
    sysbus_init_child_obj(obj, "apb-ppc1", &s->apb_ppc1, sizeof(s->apb_ppc1),
                          TYPE_TZ_PPC);
    sysbus_init_child_obj(obj, "mpc", &s->mpc, sizeof(s->mpc), TYPE_TZ_MPC);
    object_initialize_child(obj, "mpc-irq-orgate", &s->mpc_irq_orgate,
                            sizeof(s->mpc_irq_orgate), TYPE_OR_IRQ,
                            &error_abort, NULL);

    for (i = 0; i < ARRAY_SIZE(s->mpc_irq_splitter); i++) {
        char *name = g_strdup_printf("mpc-irq-splitter-%d", i);
        SplitIRQ *splitter = &s->mpc_irq_splitter[i];

        object_initialize_child(obj, name, splitter, sizeof(*splitter),
                                TYPE_SPLIT_IRQ, &error_abort, NULL);
        g_free(name);
    }
    sysbus_init_child_obj(obj, "timer0", &s->timer0, sizeof(s->timer0),
                          TYPE_CMSDK_APB_TIMER);
    sysbus_init_child_obj(obj, "timer1", &s->timer1, sizeof(s->timer1),
                          TYPE_CMSDK_APB_TIMER);
    sysbus_init_child_obj(obj, "s32ktimer", &s->s32ktimer, sizeof(s->s32ktimer),
                          TYPE_CMSDK_APB_TIMER);
    sysbus_init_child_obj(obj, "dualtimer", &s->dualtimer, sizeof(s->dualtimer),
                          TYPE_CMSDK_APB_DUALTIMER);
    sysbus_init_child_obj(obj, "s32kwatchdog", &s->s32kwatchdog,
                          sizeof(s->s32kwatchdog), TYPE_CMSDK_APB_WATCHDOG);
    sysbus_init_child_obj(obj, "nswatchdog", &s->nswatchdog,
                          sizeof(s->nswatchdog), TYPE_CMSDK_APB_WATCHDOG);
    sysbus_init_child_obj(obj, "swatchdog", &s->swatchdog,
                          sizeof(s->swatchdog), TYPE_CMSDK_APB_WATCHDOG);
    sysbus_init_child_obj(obj, "iotkit-sysctl", &s->sysctl,
                          sizeof(s->sysctl), TYPE_IOTKIT_SYSCTL);
    sysbus_init_child_obj(obj, "iotkit-sysinfo", &s->sysinfo,
                          sizeof(s->sysinfo), TYPE_IOTKIT_SYSINFO);
    object_initialize_child(obj, "nmi-orgate", &s->nmi_orgate,
                            sizeof(s->nmi_orgate), TYPE_OR_IRQ,
                            &error_abort, NULL);
    object_initialize_child(obj, "ppc-irq-orgate", &s->ppc_irq_orgate,
                            sizeof(s->ppc_irq_orgate), TYPE_OR_IRQ,
                            &error_abort, NULL);
    object_initialize_child(obj, "sec-resp-splitter", &s->sec_resp_splitter,
                            sizeof(s->sec_resp_splitter), TYPE_SPLIT_IRQ,
                            &error_abort, NULL);
    for (i = 0; i < ARRAY_SIZE(s->ppc_irq_splitter); i++) {
        char *name = g_strdup_printf("ppc-irq-splitter-%d", i);
        SplitIRQ *splitter = &s->ppc_irq_splitter[i];

        object_initialize_child(obj, name, splitter, sizeof(*splitter),
                                TYPE_SPLIT_IRQ, &error_abort, NULL);
        g_free(name);
    }
}

static void iotkit_exp_irq(void *opaque, int n, int level)
{
    IoTKit *s = IOTKIT(opaque);

    qemu_set_irq(s->exp_irqs[n], level);
}

static void iotkit_mpcexp_status(void *opaque, int n, int level)
{
    IoTKit *s = IOTKIT(opaque);
    qemu_set_irq(s->mpcexp_status_in[n], level);
}

static void iotkit_realize(DeviceState *dev, Error **errp)
{
    IoTKit *s = IOTKIT(dev);
    int i;
    MemoryRegion *mr;
    Error *err = NULL;
    SysBusDevice *sbd_apb_ppc0;
    SysBusDevice *sbd_secctl;
    DeviceState *dev_apb_ppc0;
    DeviceState *dev_apb_ppc1;
    DeviceState *dev_secctl;
    DeviceState *dev_splitter;

    if (!s->board_memory) {
        error_setg(errp, "memory property was not set");
        return;
    }

    if (!s->mainclk_frq) {
        error_setg(errp, "MAINCLK property was not set");
        return;
    }

    /* Handling of which devices should be available only to secure
     * code is usually done differently for M profile than for A profile.
     * Instead of putting some devices only into the secure address space,
     * devices exist in both address spaces but with hard-wired security
     * permissions that will cause the CPU to fault for non-secure accesses.
     *
     * The IoTKit has an IDAU (Implementation Defined Access Unit),
     * which specifies hard-wired security permissions for different
     * areas of the physical address space. For the IoTKit IDAU, the
     * top 4 bits of the physical address are the IDAU region ID, and
     * if bit 28 (ie the lowest bit of the ID) is 0 then this is an NS
     * region, otherwise it is an S region.
     *
     * The various devices and RAMs are generally all mapped twice,
     * once into a region that the IDAU defines as secure and once
     * into a non-secure region. They sit behind either a Memory
     * Protection Controller (for RAM) or a Peripheral Protection
     * Controller (for devices), which allow a more fine grained
     * configuration of whether non-secure accesses are permitted.
     *
     * (The other place that guest software can configure security
     * permissions is in the architected SAU (Security Attribution
     * Unit), which is entirely inside the CPU. The IDAU can upgrade
     * the security attributes for a region to more restrictive than
     * the SAU specifies, but cannot downgrade them.)
     *
     * 0x10000000..0x1fffffff  alias of 0x00000000..0x0fffffff
     * 0x20000000..0x2007ffff  32KB FPGA block RAM
     * 0x30000000..0x3fffffff  alias of 0x20000000..0x2fffffff
     * 0x40000000..0x4000ffff  base peripheral region 1
     * 0x40010000..0x4001ffff  CPU peripherals (none for IoTKit)
     * 0x40020000..0x4002ffff  system control element peripherals
     * 0x40080000..0x400fffff  base peripheral region 2
     * 0x50000000..0x5fffffff  alias of 0x40000000..0x4fffffff
     */

    memory_region_add_subregion_overlap(&s->container, 0, s->board_memory, -1);

    qdev_prop_set_uint32(DEVICE(&s->armv7m), "num-irq", s->exp_numirq + 32);
    /* In real hardware the initial Secure VTOR is set from the INITSVTOR0
     * register in the IoT Kit System Control Register block, and the
     * initial value of that is in turn specifiable by the FPGA that
     * instantiates the IoT Kit. In QEMU we don't implement this wrinkle,
     * and simply set the CPU's init-svtor to the IoT Kit default value.
     */
    qdev_prop_set_uint32(DEVICE(&s->armv7m), "init-svtor", 0x10000000);
    object_property_set_link(OBJECT(&s->armv7m), OBJECT(&s->container),
                             "memory", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    object_property_set_link(OBJECT(&s->armv7m), OBJECT(s), "idau", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    object_property_set_bool(OBJECT(&s->armv7m), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    /* Connect our EXP_IRQ GPIOs to the NVIC's lines 32 and up. */
    s->exp_irqs = g_new(qemu_irq, s->exp_numirq);
    for (i = 0; i < s->exp_numirq; i++) {
        s->exp_irqs[i] = qdev_get_gpio_in(DEVICE(&s->armv7m), i + 32);
    }
    qdev_init_gpio_in_named(dev, iotkit_exp_irq, "EXP_IRQ", s->exp_numirq);

    /* Set up the big aliases first */
    make_alias(s, &s->alias1, "alias 1", 0x10000000, 0x10000000, 0x00000000);
    make_alias(s, &s->alias2, "alias 2", 0x30000000, 0x10000000, 0x20000000);
    /* The 0x50000000..0x5fffffff region is not a pure alias: it has
     * a few extra devices that only appear there (generally the
     * control interfaces for the protection controllers).
     * We implement this by mapping those devices over the top of this
     * alias MR at a higher priority.
     */
    make_alias(s, &s->alias3, "alias 3", 0x50000000, 0x10000000, 0x40000000);


    /* Security controller */
    object_property_set_bool(OBJECT(&s->secctl), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sbd_secctl = SYS_BUS_DEVICE(&s->secctl);
    dev_secctl = DEVICE(&s->secctl);
    sysbus_mmio_map(sbd_secctl, 0, 0x50080000);
    sysbus_mmio_map(sbd_secctl, 1, 0x40080000);

    s->nsc_cfg_in = qemu_allocate_irq(nsccfg_handler, s, 1);
    qdev_connect_gpio_out_named(dev_secctl, "nsc_cfg", 0, s->nsc_cfg_in);

    /* The sec_resp_cfg output from the security controller must be split into
     * multiple lines, one for each of the PPCs within the IoTKit and one
     * that will be an output from the IoTKit to the system.
     */
    object_property_set_int(OBJECT(&s->sec_resp_splitter), 3,
                            "num-lines", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    object_property_set_bool(OBJECT(&s->sec_resp_splitter), true,
                             "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    dev_splitter = DEVICE(&s->sec_resp_splitter);
    qdev_connect_gpio_out_named(dev_secctl, "sec_resp_cfg", 0,
                                qdev_get_gpio_in(dev_splitter, 0));

    /* This RAM lives behind the Memory Protection Controller */
    memory_region_init_ram(&s->sram0, NULL, "iotkit.sram0", 0x00008000, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    object_property_set_link(OBJECT(&s->mpc), OBJECT(&s->sram0),
                             "downstream", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    object_property_set_bool(OBJECT(&s->mpc), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    /* Map the upstream end of the MPC into the right place... */
    memory_region_add_subregion(&s->container, 0x20000000,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->mpc),
                                                       1));
    /* ...and its register interface */
    memory_region_add_subregion(&s->container, 0x50083000,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->mpc),
                                                       0));

    /* We must OR together lines from the MPC splitters to go to the NVIC */
    object_property_set_int(OBJECT(&s->mpc_irq_orgate),
                            IOTS_NUM_EXP_MPC + IOTS_NUM_MPC, "num-lines", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    object_property_set_bool(OBJECT(&s->mpc_irq_orgate), true,
                             "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    qdev_connect_gpio_out(DEVICE(&s->mpc_irq_orgate), 0,
                          qdev_get_gpio_in(DEVICE(&s->armv7m), 9));

    /* Devices behind APB PPC0:
     *   0x40000000: timer0
     *   0x40001000: timer1
     *   0x40002000: dual timer
     * We must configure and realize each downstream device and connect
     * it to the appropriate PPC port; then we can realize the PPC and
     * map its upstream ends to the right place in the container.
     */
    qdev_prop_set_uint32(DEVICE(&s->timer0), "pclk-frq", s->mainclk_frq);
    object_property_set_bool(OBJECT(&s->timer0), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->timer0), 0,
                       qdev_get_gpio_in(DEVICE(&s->armv7m), 3));
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->timer0), 0);
    object_property_set_link(OBJECT(&s->apb_ppc0), OBJECT(mr), "port[0]", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    qdev_prop_set_uint32(DEVICE(&s->timer1), "pclk-frq", s->mainclk_frq);
    object_property_set_bool(OBJECT(&s->timer1), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->timer1), 0,
                       qdev_get_gpio_in(DEVICE(&s->armv7m), 4));
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->timer1), 0);
    object_property_set_link(OBJECT(&s->apb_ppc0), OBJECT(mr), "port[1]", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }


    qdev_prop_set_uint32(DEVICE(&s->dualtimer), "pclk-frq", s->mainclk_frq);
    object_property_set_bool(OBJECT(&s->dualtimer), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dualtimer), 0,
                       qdev_get_gpio_in(DEVICE(&s->armv7m), 5));
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->dualtimer), 0);
    object_property_set_link(OBJECT(&s->apb_ppc0), OBJECT(mr), "port[2]", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_bool(OBJECT(&s->apb_ppc0), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    sbd_apb_ppc0 = SYS_BUS_DEVICE(&s->apb_ppc0);
    dev_apb_ppc0 = DEVICE(&s->apb_ppc0);

    mr = sysbus_mmio_get_region(sbd_apb_ppc0, 0);
    memory_region_add_subregion(&s->container, 0x40000000, mr);
    mr = sysbus_mmio_get_region(sbd_apb_ppc0, 1);
    memory_region_add_subregion(&s->container, 0x40001000, mr);
    mr = sysbus_mmio_get_region(sbd_apb_ppc0, 2);
    memory_region_add_subregion(&s->container, 0x40002000, mr);
    for (i = 0; i < IOTS_APB_PPC0_NUM_PORTS; i++) {
        qdev_connect_gpio_out_named(dev_secctl, "apb_ppc0_nonsec", i,
                                    qdev_get_gpio_in_named(dev_apb_ppc0,
                                                           "cfg_nonsec", i));
        qdev_connect_gpio_out_named(dev_secctl, "apb_ppc0_ap", i,
                                    qdev_get_gpio_in_named(dev_apb_ppc0,
                                                           "cfg_ap", i));
    }
    qdev_connect_gpio_out_named(dev_secctl, "apb_ppc0_irq_enable", 0,
                                qdev_get_gpio_in_named(dev_apb_ppc0,
                                                       "irq_enable", 0));
    qdev_connect_gpio_out_named(dev_secctl, "apb_ppc0_irq_clear", 0,
                                qdev_get_gpio_in_named(dev_apb_ppc0,
                                                       "irq_clear", 0));
    qdev_connect_gpio_out(dev_splitter, 0,
                          qdev_get_gpio_in_named(dev_apb_ppc0,
                                                 "cfg_sec_resp", 0));

    /* All the PPC irq lines (from the 2 internal PPCs and the 8 external
     * ones) are sent individually to the security controller, and also
     * ORed together to give a single combined PPC interrupt to the NVIC.
     */
    object_property_set_int(OBJECT(&s->ppc_irq_orgate),
                            NUM_PPCS, "num-lines", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    object_property_set_bool(OBJECT(&s->ppc_irq_orgate), true,
                             "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    qdev_connect_gpio_out(DEVICE(&s->ppc_irq_orgate), 0,
                          qdev_get_gpio_in(DEVICE(&s->armv7m), 10));

    /* 0x40010000 .. 0x4001ffff: private CPU region: unused in IoTKit */

    /* 0x40020000 .. 0x4002ffff : IoTKit system control peripheral region */
    /* Devices behind APB PPC1:
     *   0x4002f000: S32K timer
     */
    qdev_prop_set_uint32(DEVICE(&s->s32ktimer), "pclk-frq", S32KCLK);
    object_property_set_bool(OBJECT(&s->s32ktimer), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->s32ktimer), 0,
                       qdev_get_gpio_in(DEVICE(&s->armv7m), 2));
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->s32ktimer), 0);
    object_property_set_link(OBJECT(&s->apb_ppc1), OBJECT(mr), "port[0]", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_bool(OBJECT(&s->apb_ppc1), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->apb_ppc1), 0);
    memory_region_add_subregion(&s->container, 0x4002f000, mr);

    dev_apb_ppc1 = DEVICE(&s->apb_ppc1);
    qdev_connect_gpio_out_named(dev_secctl, "apb_ppc1_nonsec", 0,
                                qdev_get_gpio_in_named(dev_apb_ppc1,
                                                       "cfg_nonsec", 0));
    qdev_connect_gpio_out_named(dev_secctl, "apb_ppc1_ap", 0,
                                qdev_get_gpio_in_named(dev_apb_ppc1,
                                                       "cfg_ap", 0));
    qdev_connect_gpio_out_named(dev_secctl, "apb_ppc1_irq_enable", 0,
                                qdev_get_gpio_in_named(dev_apb_ppc1,
                                                       "irq_enable", 0));
    qdev_connect_gpio_out_named(dev_secctl, "apb_ppc1_irq_clear", 0,
                                qdev_get_gpio_in_named(dev_apb_ppc1,
                                                       "irq_clear", 0));
    qdev_connect_gpio_out(dev_splitter, 1,
                          qdev_get_gpio_in_named(dev_apb_ppc1,
                                                 "cfg_sec_resp", 0));

    object_property_set_bool(OBJECT(&s->sysinfo), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    /* System information registers */
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sysinfo), 0, 0x40020000);
    /* System control registers */
    object_property_set_bool(OBJECT(&s->sysctl), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sysctl), 0, 0x50021000);

    /* This OR gate wires together outputs from the secure watchdogs to NMI */
    object_property_set_int(OBJECT(&s->nmi_orgate), 2, "num-lines", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    object_property_set_bool(OBJECT(&s->nmi_orgate), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    qdev_connect_gpio_out(DEVICE(&s->nmi_orgate), 0,
                          qdev_get_gpio_in_named(DEVICE(&s->armv7m), "NMI", 0));

    qdev_prop_set_uint32(DEVICE(&s->s32kwatchdog), "wdogclk-frq", S32KCLK);
    object_property_set_bool(OBJECT(&s->s32kwatchdog), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->s32kwatchdog), 0,
                       qdev_get_gpio_in(DEVICE(&s->nmi_orgate), 0));
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->s32kwatchdog), 0, 0x5002e000);

    /* 0x40080000 .. 0x4008ffff : IoTKit second Base peripheral region */

    qdev_prop_set_uint32(DEVICE(&s->nswatchdog), "wdogclk-frq", s->mainclk_frq);
    object_property_set_bool(OBJECT(&s->nswatchdog), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->nswatchdog), 0,
                       qdev_get_gpio_in(DEVICE(&s->armv7m), 1));
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->nswatchdog), 0, 0x40081000);

    qdev_prop_set_uint32(DEVICE(&s->swatchdog), "wdogclk-frq", s->mainclk_frq);
    object_property_set_bool(OBJECT(&s->swatchdog), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->swatchdog), 0,
                       qdev_get_gpio_in(DEVICE(&s->nmi_orgate), 1));
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->swatchdog), 0, 0x50081000);

    for (i = 0; i < ARRAY_SIZE(s->ppc_irq_splitter); i++) {
        Object *splitter = OBJECT(&s->ppc_irq_splitter[i]);

        object_property_set_int(splitter, 2, "num-lines", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        object_property_set_bool(splitter, true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
    }

    for (i = 0; i < IOTS_NUM_AHB_EXP_PPC; i++) {
        char *ppcname = g_strdup_printf("ahb_ppcexp%d", i);

        iotkit_forward_ppc(s, ppcname, i);
        g_free(ppcname);
    }

    for (i = 0; i < IOTS_NUM_APB_EXP_PPC; i++) {
        char *ppcname = g_strdup_printf("apb_ppcexp%d", i);

        iotkit_forward_ppc(s, ppcname, i + IOTS_NUM_AHB_EXP_PPC);
        g_free(ppcname);
    }

    for (i = NUM_EXTERNAL_PPCS; i < NUM_PPCS; i++) {
        /* Wire up IRQ splitter for internal PPCs */
        DeviceState *devs = DEVICE(&s->ppc_irq_splitter[i]);
        char *gpioname = g_strdup_printf("apb_ppc%d_irq_status",
                                         i - NUM_EXTERNAL_PPCS);
        TZPPC *ppc = (i == NUM_EXTERNAL_PPCS) ? &s->apb_ppc0 : &s->apb_ppc1;

        qdev_connect_gpio_out(devs, 0,
                              qdev_get_gpio_in_named(dev_secctl, gpioname, 0));
        qdev_connect_gpio_out(devs, 1,
                              qdev_get_gpio_in(DEVICE(&s->ppc_irq_orgate), i));
        qdev_connect_gpio_out_named(DEVICE(ppc), "irq", 0,
                                    qdev_get_gpio_in(devs, 0));
        g_free(gpioname);
    }

    /* Wire up the splitters for the MPC IRQs */
    for (i = 0; i < IOTS_NUM_EXP_MPC + IOTS_NUM_MPC; i++) {
        SplitIRQ *splitter = &s->mpc_irq_splitter[i];
        DeviceState *dev_splitter = DEVICE(splitter);

        object_property_set_int(OBJECT(splitter), 2, "num-lines", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        object_property_set_bool(OBJECT(splitter), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }

        if (i < IOTS_NUM_EXP_MPC) {
            /* Splitter input is from GPIO input line */
            s->mpcexp_status_in[i] = qdev_get_gpio_in(dev_splitter, 0);
            qdev_connect_gpio_out(dev_splitter, 0,
                                  qdev_get_gpio_in_named(dev_secctl,
                                                         "mpcexp_status", i));
        } else {
            /* Splitter input is from our own MPC */
            qdev_connect_gpio_out_named(DEVICE(&s->mpc), "irq", 0,
                                        qdev_get_gpio_in(dev_splitter, 0));
            qdev_connect_gpio_out(dev_splitter, 0,
                                  qdev_get_gpio_in_named(dev_secctl,
                                                         "mpc_status", 0));
        }

        qdev_connect_gpio_out(dev_splitter, 1,
                              qdev_get_gpio_in(DEVICE(&s->mpc_irq_orgate), i));
    }
    /* Create GPIO inputs which will pass the line state for our
     * mpcexp_irq inputs to the correct splitter devices.
     */
    qdev_init_gpio_in_named(dev, iotkit_mpcexp_status, "mpcexp_status",
                            IOTS_NUM_EXP_MPC);

    iotkit_forward_sec_resp_cfg(s);

    /* Forward the MSC related signals */
    qdev_pass_gpios(dev_secctl, dev, "mscexp_status");
    qdev_pass_gpios(dev_secctl, dev, "mscexp_clear");
    qdev_pass_gpios(dev_secctl, dev, "mscexp_ns");
    qdev_connect_gpio_out_named(dev_secctl, "msc_irq", 0,
                                qdev_get_gpio_in(DEVICE(&s->armv7m), 11));

    /*
     * Expose our container region to the board model; this corresponds
     * to the AHB Slave Expansion ports which allow bus master devices
     * (eg DMA controllers) in the board model to make transactions into
     * devices in the IoTKit.
     */
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->container);

    system_clock_scale = NANOSECONDS_PER_SECOND / s->mainclk_frq;
}

static void iotkit_idau_check(IDAUInterface *ii, uint32_t address,
                              int *iregion, bool *exempt, bool *ns, bool *nsc)
{
    /* For IoTKit systems the IDAU responses are simple logical functions
     * of the address bits. The NSC attribute is guest-adjustable via the
     * NSCCFG register in the security controller.
     */
    IoTKit *s = IOTKIT(ii);
    int region = extract32(address, 28, 4);

    *ns = !(region & 1);
    *nsc = (region == 1 && (s->nsccfg & 1)) || (region == 3 && (s->nsccfg & 2));
    /* 0xe0000000..0xe00fffff and 0xf0000000..0xf00fffff are exempt */
    *exempt = (address & 0xeff00000) == 0xe0000000;
    *iregion = region;
}

static const VMStateDescription iotkit_vmstate = {
    .name = "iotkit",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(nsccfg, IoTKit),
        VMSTATE_END_OF_LIST()
    }
};

static Property iotkit_properties[] = {
    DEFINE_PROP_LINK("memory", IoTKit, board_memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_UINT32("EXP_NUMIRQ", IoTKit, exp_numirq, 64),
    DEFINE_PROP_UINT32("MAINCLK", IoTKit, mainclk_frq, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void iotkit_reset(DeviceState *dev)
{
    IoTKit *s = IOTKIT(dev);

    s->nsccfg = 0;
}

static void iotkit_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    IDAUInterfaceClass *iic = IDAU_INTERFACE_CLASS(klass);

    dc->realize = iotkit_realize;
    dc->vmsd = &iotkit_vmstate;
    dc->props = iotkit_properties;
    dc->reset = iotkit_reset;
    iic->check = iotkit_idau_check;
}

static const TypeInfo iotkit_info = {
    .name = TYPE_IOTKIT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IoTKit),
    .instance_init = iotkit_init,
    .class_init = iotkit_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_IDAU_INTERFACE },
        { }
    }
};

static void iotkit_register_types(void)
{
    type_register_static(&iotkit_info);
}

type_init(iotkit_register_types);
