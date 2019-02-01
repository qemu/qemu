/*
 * Arm SSE (Subsystems for Embedded): IoTKit
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
#include "hw/arm/armsse.h"
#include "hw/arm/arm.h"

struct ARMSSEInfo {
    const char *name;
    int sram_banks;
    int num_cpus;
};

static const ARMSSEInfo armsse_variants[] = {
    {
        .name = TYPE_IOTKIT,
        .sram_banks = 1,
        .num_cpus = 1,
    },
};

/* Clock frequency in HZ of the 32KHz "slow clock" */
#define S32KCLK (32 * 1000)

/* Is internal IRQ n shared between CPUs in a multi-core SSE ? */
static bool irq_is_common[32] = {
    [0 ... 5] = true,
    /* 6, 7: per-CPU MHU interrupts */
    [8 ... 12] = true,
    /* 13: per-CPU icache interrupt */
    /* 14: reserved */
    [15 ... 20] = true,
    /* 21: reserved */
    [22 ... 26] = true,
    /* 27: reserved */
    /* 28, 29: per-CPU CTI interrupts */
    /* 30, 31: reserved */
};

/* Create an alias region of @size bytes starting at @base
 * which mirrors the memory starting at @orig.
 */
static void make_alias(ARMSSE *s, MemoryRegion *mr, const char *name,
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
    ARMSSE *s = ARMSSE(opaque);

    s->nsccfg = level;
}

static void armsse_forward_ppc(ARMSSE *s, const char *ppcname, int ppcnum)
{
    /* Each of the 4 AHB and 4 APB PPCs that might be present in a
     * system using the ARMSSE has a collection of control lines which
     * are provided by the security controller and which we want to
     * expose as control lines on the ARMSSE device itself, so the
     * code using the ARMSSE can wire them up to the PPCs.
     */
    SplitIRQ *splitter = &s->ppc_irq_splitter[ppcnum];
    DeviceState *armssedev = DEVICE(s);
    DeviceState *dev_secctl = DEVICE(&s->secctl);
    DeviceState *dev_splitter = DEVICE(splitter);
    char *name;

    name = g_strdup_printf("%s_nonsec", ppcname);
    qdev_pass_gpios(dev_secctl, armssedev, name);
    g_free(name);
    name = g_strdup_printf("%s_ap", ppcname);
    qdev_pass_gpios(dev_secctl, armssedev, name);
    g_free(name);
    name = g_strdup_printf("%s_irq_enable", ppcname);
    qdev_pass_gpios(dev_secctl, armssedev, name);
    g_free(name);
    name = g_strdup_printf("%s_irq_clear", ppcname);
    qdev_pass_gpios(dev_secctl, armssedev, name);
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
    qdev_init_gpio_in_named_with_opaque(armssedev, irq_status_forwarder,
                                        s->irq_status_in[ppcnum], name, 1);
    g_free(name);
}

static void armsse_forward_sec_resp_cfg(ARMSSE *s)
{
    /* Forward the 3rd output from the splitter device as a
     * named GPIO output of the armsse object.
     */
    DeviceState *dev = DEVICE(s);
    DeviceState *dev_splitter = DEVICE(&s->sec_resp_splitter);

    qdev_init_gpio_out_named(dev, &s->sec_resp_cfg, "sec_resp_cfg", 1);
    s->sec_resp_cfg_in = qemu_allocate_irq(irq_status_forwarder,
                                           s->sec_resp_cfg, 1);
    qdev_connect_gpio_out(dev_splitter, 2, s->sec_resp_cfg_in);
}

static void armsse_init(Object *obj)
{
    ARMSSE *s = ARMSSE(obj);
    ARMSSEClass *asc = ARMSSE_GET_CLASS(obj);
    const ARMSSEInfo *info = asc->info;
    int i;

    assert(info->sram_banks <= MAX_SRAM_BANKS);
    assert(info->num_cpus <= SSE_MAX_CPUS);

    memory_region_init(&s->container, obj, "armsse-container", UINT64_MAX);

    for (i = 0; i < info->num_cpus; i++) {
        char *name = g_strdup_printf("armv7m%d", i);
        sysbus_init_child_obj(obj, name, &s->armv7m[i], sizeof(s->armv7m),
                              TYPE_ARMV7M);
        qdev_prop_set_string(DEVICE(&s->armv7m[i]), "cpu-type",
                             ARM_CPU_TYPE_NAME("cortex-m33"));
        g_free(name);
    }

    sysbus_init_child_obj(obj, "secctl", &s->secctl, sizeof(s->secctl),
                          TYPE_IOTKIT_SECCTL);
    sysbus_init_child_obj(obj, "apb-ppc0", &s->apb_ppc0, sizeof(s->apb_ppc0),
                          TYPE_TZ_PPC);
    sysbus_init_child_obj(obj, "apb-ppc1", &s->apb_ppc1, sizeof(s->apb_ppc1),
                          TYPE_TZ_PPC);
    for (i = 0; i < info->sram_banks; i++) {
        char *name = g_strdup_printf("mpc%d", i);
        sysbus_init_child_obj(obj, name, &s->mpc[i],
                              sizeof(s->mpc[i]), TYPE_TZ_MPC);
        g_free(name);
    }
    object_initialize_child(obj, "mpc-irq-orgate", &s->mpc_irq_orgate,
                            sizeof(s->mpc_irq_orgate), TYPE_OR_IRQ,
                            &error_abort, NULL);

    for (i = 0; i < IOTS_NUM_EXP_MPC + info->sram_banks; i++) {
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
    sysbus_init_child_obj(obj, "armsse-sysctl", &s->sysctl,
                          sizeof(s->sysctl), TYPE_IOTKIT_SYSCTL);
    sysbus_init_child_obj(obj, "armsse-sysinfo", &s->sysinfo,
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
    if (info->num_cpus > 1) {
        for (i = 0; i < ARRAY_SIZE(s->cpu_irq_splitter); i++) {
            if (irq_is_common[i]) {
                char *name = g_strdup_printf("cpu-irq-splitter%d", i);
                SplitIRQ *splitter = &s->cpu_irq_splitter[i];

                object_initialize_child(obj, name, splitter, sizeof(*splitter),
                                        TYPE_SPLIT_IRQ, &error_abort, NULL);
                g_free(name);
            }
        }
    }
}

static void armsse_exp_irq(void *opaque, int n, int level)
{
    qemu_irq *irqarray = opaque;

    qemu_set_irq(irqarray[n], level);
}

static void armsse_mpcexp_status(void *opaque, int n, int level)
{
    ARMSSE *s = ARMSSE(opaque);
    qemu_set_irq(s->mpcexp_status_in[n], level);
}

static qemu_irq armsse_get_common_irq_in(ARMSSE *s, int irqno)
{
    /*
     * Return a qemu_irq which can be used to signal IRQ n to
     * all CPUs in the SSE.
     */
    ARMSSEClass *asc = ARMSSE_GET_CLASS(s);
    const ARMSSEInfo *info = asc->info;

    assert(irq_is_common[irqno]);

    if (info->num_cpus == 1) {
        /* Only one CPU -- just connect directly to it */
        return qdev_get_gpio_in(DEVICE(&s->armv7m[0]), irqno);
    } else {
        /* Connect to the splitter which feeds all CPUs */
        return qdev_get_gpio_in(DEVICE(&s->cpu_irq_splitter[irqno]), 0);
    }
}

static void armsse_realize(DeviceState *dev, Error **errp)
{
    ARMSSE *s = ARMSSE(dev);
    ARMSSEClass *asc = ARMSSE_GET_CLASS(dev);
    const ARMSSEInfo *info = asc->info;
    int i;
    MemoryRegion *mr;
    Error *err = NULL;
    SysBusDevice *sbd_apb_ppc0;
    SysBusDevice *sbd_secctl;
    DeviceState *dev_apb_ppc0;
    DeviceState *dev_apb_ppc1;
    DeviceState *dev_secctl;
    DeviceState *dev_splitter;
    uint32_t addr_width_max;

    if (!s->board_memory) {
        error_setg(errp, "memory property was not set");
        return;
    }

    if (!s->mainclk_frq) {
        error_setg(errp, "MAINCLK property was not set");
        return;
    }

    /* max SRAM_ADDR_WIDTH: 24 - log2(SRAM_NUM_BANK) */
    assert(is_power_of_2(info->sram_banks));
    addr_width_max = 24 - ctz32(info->sram_banks);
    if (s->sram_addr_width < 1 || s->sram_addr_width > addr_width_max) {
        error_setg(errp, "SRAM_ADDR_WIDTH must be between 1 and %d",
                   addr_width_max);
        return;
    }

    /* Handling of which devices should be available only to secure
     * code is usually done differently for M profile than for A profile.
     * Instead of putting some devices only into the secure address space,
     * devices exist in both address spaces but with hard-wired security
     * permissions that will cause the CPU to fault for non-secure accesses.
     *
     * The ARMSSE has an IDAU (Implementation Defined Access Unit),
     * which specifies hard-wired security permissions for different
     * areas of the physical address space. For the ARMSSE IDAU, the
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
     * 0x40010000..0x4001ffff  CPU peripherals (none for ARMSSE)
     * 0x40020000..0x4002ffff  system control element peripherals
     * 0x40080000..0x400fffff  base peripheral region 2
     * 0x50000000..0x5fffffff  alias of 0x40000000..0x4fffffff
     */

    memory_region_add_subregion_overlap(&s->container, 0, s->board_memory, -1);

    for (i = 0; i < info->num_cpus; i++) {
        DeviceState *cpudev = DEVICE(&s->armv7m[i]);
        Object *cpuobj = OBJECT(&s->armv7m[i]);
        int j;
        char *gpioname;

        qdev_prop_set_uint32(cpudev, "num-irq", s->exp_numirq + 32);
        /*
         * In real hardware the initial Secure VTOR is set from the INITSVTOR0
         * register in the IoT Kit System Control Register block, and the
         * initial value of that is in turn specifiable by the FPGA that
         * instantiates the IoT Kit. In QEMU we don't implement this wrinkle,
         * and simply set the CPU's init-svtor to the IoT Kit default value.
         * In SSE-200 the situation is similar, except that the default value
         * is a reset-time signal input. Typically a board using the SSE-200
         * will have a system control processor whose boot firmware initializes
         * the INITSVTOR* registers before powering up the CPUs in any case,
         * so the hardware's default value doesn't matter. QEMU doesn't emulate
         * the control processor, so instead we behave in the way that the
         * firmware does. All boards currently known about have firmware that
         * sets the INITSVTOR0 and INITSVTOR1 registers to 0x10000000, like the
         * IoTKit default. We can make this more configurable if necessary.
         */
        qdev_prop_set_uint32(cpudev, "init-svtor", 0x10000000);
        /*
         * Start all CPUs except CPU0 powered down. In real hardware it is
         * a configurable property of the SSE-200 which CPUs start powered up
         * (via the CPUWAIT0_RST and CPUWAIT1_RST parameters), but since all
         * the boards we care about start CPU0 and leave CPU1 powered off,
         * we hard-code that for now. We can add QOM properties for this
         * later if necessary.
         */
        if (i > 0) {
            object_property_set_bool(cpuobj, true, "start-powered-off", &err);
            if (err) {
                error_propagate(errp, err);
                return;
            }
        }
        object_property_set_link(cpuobj, OBJECT(&s->container), "memory", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        object_property_set_link(cpuobj, OBJECT(s), "idau", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        object_property_set_bool(cpuobj, true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }

        /* Connect EXP_IRQ/EXP_CPUn_IRQ GPIOs to the NVIC's lines 32 and up */
        s->exp_irqs[i] = g_new(qemu_irq, s->exp_numirq);
        for (j = 0; j < s->exp_numirq; j++) {
            s->exp_irqs[i][j] = qdev_get_gpio_in(cpudev, i + 32);
        }
        if (i == 0) {
            gpioname = g_strdup("EXP_IRQ");
        } else {
            gpioname = g_strdup_printf("EXP_CPU%d_IRQ", i);
        }
        qdev_init_gpio_in_named_with_opaque(dev, armsse_exp_irq,
                                            s->exp_irqs[i],
                                            gpioname, s->exp_numirq);
        g_free(gpioname);
    }

    /* Wire up the splitters that connect common IRQs to all CPUs */
    if (info->num_cpus > 1) {
        for (i = 0; i < ARRAY_SIZE(s->cpu_irq_splitter); i++) {
            if (irq_is_common[i]) {
                Object *splitter = OBJECT(&s->cpu_irq_splitter[i]);
                DeviceState *devs = DEVICE(splitter);
                int cpunum;

                object_property_set_int(splitter, info->num_cpus,
                                        "num-lines", &err);
                if (err) {
                    error_propagate(errp, err);
                    return;
                }
                object_property_set_bool(splitter, true, "realized", &err);
                if (err) {
                    error_propagate(errp, err);
                    return;
                }
                for (cpunum = 0; cpunum < info->num_cpus; cpunum++) {
                    DeviceState *cpudev = DEVICE(&s->armv7m[cpunum]);

                    qdev_connect_gpio_out(devs, cpunum,
                                          qdev_get_gpio_in(cpudev, i));
                }
            }
        }
    }

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
     * multiple lines, one for each of the PPCs within the ARMSSE and one
     * that will be an output from the ARMSSE to the system.
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

    /* Each SRAM bank lives behind its own Memory Protection Controller */
    for (i = 0; i < info->sram_banks; i++) {
        char *ramname = g_strdup_printf("armsse.sram%d", i);
        SysBusDevice *sbd_mpc;
        uint32_t sram_bank_size = 1 << s->sram_addr_width;

        memory_region_init_ram(&s->sram[i], NULL, ramname,
                               sram_bank_size, &err);
        g_free(ramname);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        object_property_set_link(OBJECT(&s->mpc[i]), OBJECT(&s->sram[i]),
                                 "downstream", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        object_property_set_bool(OBJECT(&s->mpc[i]), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        /* Map the upstream end of the MPC into the right place... */
        sbd_mpc = SYS_BUS_DEVICE(&s->mpc[i]);
        memory_region_add_subregion(&s->container,
                                    0x20000000 + i * sram_bank_size,
                                    sysbus_mmio_get_region(sbd_mpc, 1));
        /* ...and its register interface */
        memory_region_add_subregion(&s->container, 0x50083000 + i * 0x1000,
                                    sysbus_mmio_get_region(sbd_mpc, 0));
    }

    /* We must OR together lines from the MPC splitters to go to the NVIC */
    object_property_set_int(OBJECT(&s->mpc_irq_orgate),
                            IOTS_NUM_EXP_MPC + info->sram_banks,
                            "num-lines", &err);
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
                          armsse_get_common_irq_in(s, 9));

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
                       armsse_get_common_irq_in(s, 3));
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
                       armsse_get_common_irq_in(s, 4));
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
                       armsse_get_common_irq_in(s, 5));
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
                          armsse_get_common_irq_in(s, 10));

    /* 0x40010000 .. 0x4001ffff: private CPU region: unused in IoTKit */

    /* 0x40020000 .. 0x4002ffff : ARMSSE system control peripheral region */
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
                       armsse_get_common_irq_in(s, 2));
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

    /* 0x40080000 .. 0x4008ffff : ARMSSE second Base peripheral region */

    qdev_prop_set_uint32(DEVICE(&s->nswatchdog), "wdogclk-frq", s->mainclk_frq);
    object_property_set_bool(OBJECT(&s->nswatchdog), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->nswatchdog), 0,
                       armsse_get_common_irq_in(s, 1));
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

        armsse_forward_ppc(s, ppcname, i);
        g_free(ppcname);
    }

    for (i = 0; i < IOTS_NUM_APB_EXP_PPC; i++) {
        char *ppcname = g_strdup_printf("apb_ppcexp%d", i);

        armsse_forward_ppc(s, ppcname, i + IOTS_NUM_AHB_EXP_PPC);
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
    for (i = 0; i < IOTS_NUM_EXP_MPC + info->sram_banks; i++) {
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
            qdev_connect_gpio_out_named(DEVICE(&s->mpc[i - IOTS_NUM_EXP_MPC]),
                                        "irq", 0,
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
    qdev_init_gpio_in_named(dev, armsse_mpcexp_status, "mpcexp_status",
                            IOTS_NUM_EXP_MPC);

    armsse_forward_sec_resp_cfg(s);

    /* Forward the MSC related signals */
    qdev_pass_gpios(dev_secctl, dev, "mscexp_status");
    qdev_pass_gpios(dev_secctl, dev, "mscexp_clear");
    qdev_pass_gpios(dev_secctl, dev, "mscexp_ns");
    qdev_connect_gpio_out_named(dev_secctl, "msc_irq", 0,
                                armsse_get_common_irq_in(s, 11));

    /*
     * Expose our container region to the board model; this corresponds
     * to the AHB Slave Expansion ports which allow bus master devices
     * (eg DMA controllers) in the board model to make transactions into
     * devices in the ARMSSE.
     */
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->container);

    system_clock_scale = NANOSECONDS_PER_SECOND / s->mainclk_frq;
}

static void armsse_idau_check(IDAUInterface *ii, uint32_t address,
                              int *iregion, bool *exempt, bool *ns, bool *nsc)
{
    /*
     * For ARMSSE systems the IDAU responses are simple logical functions
     * of the address bits. The NSC attribute is guest-adjustable via the
     * NSCCFG register in the security controller.
     */
    ARMSSE *s = ARMSSE(ii);
    int region = extract32(address, 28, 4);

    *ns = !(region & 1);
    *nsc = (region == 1 && (s->nsccfg & 1)) || (region == 3 && (s->nsccfg & 2));
    /* 0xe0000000..0xe00fffff and 0xf0000000..0xf00fffff are exempt */
    *exempt = (address & 0xeff00000) == 0xe0000000;
    *iregion = region;
}

static const VMStateDescription armsse_vmstate = {
    .name = "iotkit",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(nsccfg, ARMSSE),
        VMSTATE_END_OF_LIST()
    }
};

static Property armsse_properties[] = {
    DEFINE_PROP_LINK("memory", ARMSSE, board_memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_UINT32("EXP_NUMIRQ", ARMSSE, exp_numirq, 64),
    DEFINE_PROP_UINT32("MAINCLK", ARMSSE, mainclk_frq, 0),
    DEFINE_PROP_UINT32("SRAM_ADDR_WIDTH", ARMSSE, sram_addr_width, 15),
    DEFINE_PROP_END_OF_LIST()
};

static void armsse_reset(DeviceState *dev)
{
    ARMSSE *s = ARMSSE(dev);

    s->nsccfg = 0;
}

static void armsse_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    IDAUInterfaceClass *iic = IDAU_INTERFACE_CLASS(klass);
    ARMSSEClass *asc = ARMSSE_CLASS(klass);

    dc->realize = armsse_realize;
    dc->vmsd = &armsse_vmstate;
    dc->props = armsse_properties;
    dc->reset = armsse_reset;
    iic->check = armsse_idau_check;
    asc->info = data;
}

static const TypeInfo armsse_info = {
    .name = TYPE_ARMSSE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMSSE),
    .instance_init = armsse_init,
    .abstract = true,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_IDAU_INTERFACE },
        { }
    }
};

static void armsse_register_types(void)
{
    int i;

    type_register_static(&armsse_info);

    for (i = 0; i < ARRAY_SIZE(armsse_variants); i++) {
        TypeInfo ti = {
            .name = armsse_variants[i].name,
            .parent = TYPE_ARMSSE,
            .class_init = armsse_class_init,
            .class_data = (void *)&armsse_variants[i],
        };
        type_register(&ti);
    }
}

type_init(armsse_register_types);
