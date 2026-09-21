/*
 * i.MX 8MM SoC Implementation
 *
 * Based on hw/arm/fsl-imx6.c
 *
 * Copyright (c) 2025, NXP Semiconductors
 * Author: Gaurav Sharma <gaurav.sharma_7@nxp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * iMX8MM Reference Manual - https://www.nxp.com/products/i.MX8MMINI -> Documentation
 */

#include "qemu/osdep.h"
#include "system/address-spaces.h"
#include "hw/arm/bsa.h"
#include "hw/arm/fsl-imx8mm.h"
#include "hw/misc/unimp.h"
#include "hw/core/boards.h"
#include "system/kvm.h"
#include "system/system.h"
#include "target/arm/cpu.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/kvm_arm.h"
#include "qapi/error.h"
#include "qobject/qlist.h"

static const struct {
    hwaddr addr;
    size_t size;
    const char *name;
} fsl_imx8mm_memmap[] = {
    [FSL_IMX8MM_RAM] = { FSL_IMX8MM_RAM_START, FSL_IMX8MM_RAM_SIZE_MAX, "ram" },
    [FSL_IMX8MM_DDR_PHY_BROADCAST] = { 0x3dc00000, 4 * MiB, "ddr_phy_broadcast" },
    [FSL_IMX8MM_DDR_PERF_MON] = { 0x3d800000, 4 * MiB, "ddr_perf_mon" },
    [FSL_IMX8MM_DDR_CTL] = { 0x3d400000, 4 * MiB, "ddr_ctl" },
    [FSL_IMX8MM_DDR_PHY] = { 0x3c000000, 16 * MiB, "ddr_phy" },
    [FSL_IMX8MM_GIC_DIST] = { 0x38800000, 512 * KiB, "gic_dist" },
    [FSL_IMX8MM_GIC_REDIST] = { 0x38880000, 512 * KiB, "gic_redist" },
    [FSL_IMX8MM_VPU] = { 0x38340000, 2 * MiB, "vpu" },
    [FSL_IMX8MM_VPU_BLK_CTRL] = { 0x38330000, 2 * MiB, "vpu_blk_ctrl" },
    [FSL_IMX8MM_VPU_G2_DECODER] = { 0x38310000, 1 * MiB, "vpu_g2_decoder" },
    [FSL_IMX8MM_VPU_G1_DECODER] = { 0x38300000, 1 * MiB, "vpu_g1_decoder" },
    [FSL_IMX8MM_USB2_OTG] = { 0x32e50200, 0x200, "usb2_otg" },
    [FSL_IMX8MM_USB2] = { 0x32e50000, 0x200, "usb2" },
    [FSL_IMX8MM_USB1_OTG] = { 0x32e40200, 0x200, "usb1_otg" },
    [FSL_IMX8MM_USB1] = { 0x32e40000, 0x200, "usb1" },
    [FSL_IMX8MM_GPU2D] = { 0x38000000, 64 * KiB, "gpu2d" },
    [FSL_IMX8MM_QSPI1_RX_BUFFER] = { 0x34000000, 32 * MiB, "qspi1_rx_buffer" },
    [FSL_IMX8MM_PCIE1] = { 0x33800000, 4 * MiB, "pcie1" },
    [FSL_IMX8MM_QSPI1_TX_BUFFER] = { 0x33008000, 32 * KiB, "qspi1_tx_buffer" },
    [FSL_IMX8MM_APBH_DMA] = { 0x33000000, 32 * KiB, "apbh_dma" },

    /* AIPS-4 Begin */
    [FSL_IMX8MM_TZASC] = { 0x32f80000, 64 * KiB, "tzasc" },
    [FSL_IMX8MM_PCIE_PHY1] = { 0x32f00000, 64 * KiB, "pcie_phy1" },
    [FSL_IMX8MM_MEDIA_BLK_CTL] = { 0x32e28000, 256, "media_blk_ctl" },
    [FSL_IMX8MM_LCDIF] = { 0x32e00000, 64 * KiB, "lcdif" },
    [FSL_IMX8MM_MIPI_DSI] = { 0x32e10000, 64 * KiB, "mipi_dsi" },
    [FSL_IMX8MM_MIPI_CSI] = { 0x32e30000, 64 * KiB, "mipi_csi" },
    [FSL_IMX8MM_AIPS4_CONFIGURATION] = { 0x32df0000, 64 * KiB, "aips4_configuration" },
    /* AIPS-4 End */

    [FSL_IMX8MM_INTERCONNECT] = { 0x32700000, 1 * MiB, "interconnect" },

    /* AIPS-3 Begin */
    [FSL_IMX8MM_ENET1] = { 0x30be0000, 64 * KiB, "enet1" },
    [FSL_IMX8MM_SDMA1] = { 0x30bd0000, 64 * KiB, "sdma1" },
    [FSL_IMX8MM_QSPI] = { 0x30bb0000, 64 * KiB, "qspi" },
    [FSL_IMX8MM_USDHC3] = { 0x30b60000, 64 * KiB, "usdhc3" },
    [FSL_IMX8MM_USDHC2] = { 0x30b50000, 64 * KiB, "usdhc2" },
    [FSL_IMX8MM_USDHC1] = { 0x30b40000, 64 * KiB, "usdhc1" },
    [FSL_IMX8MM_SEMAPHORE_HS] = { 0x30ac0000, 64 * KiB, "semaphore_hs" },
    [FSL_IMX8MM_MU_B] = { 0x30ab0000, 64 * KiB, "mu_b" },
    [FSL_IMX8MM_MU_A] = { 0x30aa0000, 64 * KiB, "mu_a" },
    [FSL_IMX8MM_UART4] = { 0x30a60000, 64 * KiB, "uart4" },
    [FSL_IMX8MM_I2C4] = { 0x30a50000, 64 * KiB, "i2c4" },
    [FSL_IMX8MM_I2C3] = { 0x30a40000, 64 * KiB, "i2c3" },
    [FSL_IMX8MM_I2C2] = { 0x30a30000, 64 * KiB, "i2c2" },
    [FSL_IMX8MM_I2C1] = { 0x30a20000, 64 * KiB, "i2c1" },
    [FSL_IMX8MM_AIPS3_CONFIGURATION] = { 0x309f0000, 64 * KiB, "aips3_configuration" },
    [FSL_IMX8MM_CAAM] = { 0x30900000, 256 * KiB, "caam" },
    [FSL_IMX8MM_SPBA1] = { 0x308f0000, 64 * KiB, "spba1" },
    [FSL_IMX8MM_UART2] = { 0x30890000, 64 * KiB, "uart2" },
    [FSL_IMX8MM_UART3] = { 0x30880000, 64 * KiB, "uart3" },
    [FSL_IMX8MM_UART1] = { 0x30860000, 64 * KiB, "uart1" },
    [FSL_IMX8MM_ECSPI3] = { 0x30840000, 64 * KiB, "ecspi3" },
    [FSL_IMX8MM_ECSPI2] = { 0x30830000, 64 * KiB, "ecspi2" },
    [FSL_IMX8MM_ECSPI1] = { 0x30820000, 64 * KiB, "ecspi1" },
    /* AIPS-3 End */

    /* AIPS-2 Begin */
    [FSL_IMX8MM_QOSC] = { 0x307f0000, 64 * KiB, "qosc" },
    [FSL_IMX8MM_PERFMON2] = { 0x307d0000, 64 * KiB, "perfmon2" },
    [FSL_IMX8MM_PERFMON1] = { 0x307c0000, 64 * KiB, "perfmon1" },
    [FSL_IMX8MM_GPT4] = { 0x30700000, 64 * KiB, "gpt4" },
    [FSL_IMX8MM_GPT5] = { 0x306f0000, 64 * KiB, "gpt5" },
    [FSL_IMX8MM_GPT6] = { 0x306e0000, 64 * KiB, "gpt6" },
    [FSL_IMX8MM_SYSCNT_CTRL] = { 0x306c0000, 64 * KiB, "syscnt_ctrl" },
    [FSL_IMX8MM_SYSCNT_CMP] = { 0x306b0000, 64 * KiB, "syscnt_cmp" },
    [FSL_IMX8MM_SYSCNT_RD] = { 0x306a0000, 64 * KiB, "syscnt_rd" },
    [FSL_IMX8MM_PWM4] = { 0x30690000, 64 * KiB, "pwm4" },
    [FSL_IMX8MM_PWM3] = { 0x30680000, 64 * KiB, "pwm3" },
    [FSL_IMX8MM_PWM2] = { 0x30670000, 64 * KiB, "pwm2" },
    [FSL_IMX8MM_PWM1] = { 0x30660000, 64 * KiB, "pwm1" },
    [FSL_IMX8MM_AIPS2_CONFIGURATION] = { 0x305f0000, 64 * KiB, "aips2_configuration" },
    /* AIPS-2 End */

    /* AIPS-1 Begin */
    [FSL_IMX8MM_CSU] = { 0x303e0000, 64 * KiB, "csu" },
    [FSL_IMX8MM_RDC] = { 0x303d0000, 64 * KiB, "rdc" },
    [FSL_IMX8MM_SEMAPHORE2] = { 0x303c0000, 64 * KiB, "semaphore2" },
    [FSL_IMX8MM_SEMAPHORE1] = { 0x303b0000, 64 * KiB, "semaphore1" },
    [FSL_IMX8MM_GPC] = { 0x303a0000, 64 * KiB, "gpc" },
    [FSL_IMX8MM_SRC] = { 0x30390000, 64 * KiB, "src" },
    [FSL_IMX8MM_CCM] = { 0x30380000, 64 * KiB, "ccm" },
    [FSL_IMX8MM_SNVS_HP] = { 0x30370000, 64 * KiB, "snvs_hp" },
    [FSL_IMX8MM_ANA_PLL] = { 0x30360000, 64 * KiB, "ana_pll" },
    [FSL_IMX8MM_OCOTP_CTRL] = { 0x30350000, 64 * KiB, "ocotp_ctrl" },
    [FSL_IMX8MM_IOMUXC_GPR] = { 0x30340000, 64 * KiB, "iomuxc_gpr" },
    [FSL_IMX8MM_IOMUXC] = { 0x30330000, 64 * KiB, "iomuxc" },
    [FSL_IMX8MM_GPT3] = { 0x302f0000, 64 * KiB, "gpt3" },
    [FSL_IMX8MM_GPT2] = { 0x302e0000, 64 * KiB, "gpt2" },
    [FSL_IMX8MM_GPT1] = { 0x302d0000, 64 * KiB, "gpt1" },
    [FSL_IMX8MM_SDMA2] = { 0x302c0000, 64 * KiB, "sdma2" },
    [FSL_IMX8MM_SDMA3] = { 0x302b0000, 64 * KiB, "sdma3" },
    [FSL_IMX8MM_WDOG3] = { 0x302a0000, 64 * KiB, "wdog3" },
    [FSL_IMX8MM_WDOG2] = { 0x30290000, 64 * KiB, "wdog2" },
    [FSL_IMX8MM_WDOG1] = { 0x30280000, 64 * KiB, "wdog1" },
    [FSL_IMX8MM_ANA_OSC] = { 0x30270000, 64 * KiB, "ana_osc" },
    [FSL_IMX8MM_ANA_TSENSOR] = { 0x30260000, 64 * KiB, "ana_tsensor" },
    [FSL_IMX8MM_GPIO5] = { 0x30240000, 64 * KiB, "gpio5" },
    [FSL_IMX8MM_GPIO4] = { 0x30230000, 64 * KiB, "gpio4" },
    [FSL_IMX8MM_GPIO3] = { 0x30220000, 64 * KiB, "gpio3" },
    [FSL_IMX8MM_GPIO2] = { 0x30210000, 64 * KiB, "gpio2" },
    [FSL_IMX8MM_GPIO1] = { 0x30200000, 64 * KiB, "gpio1" },
    [FSL_IMX8MM_AIPS1_CONFIGURATION] = { 0x301f0000, 64 * KiB, "aips1_configuration" },
    [FSL_IMX8MM_SAI6] = { 0x30060000, 64 * KiB, "sai6" },
    [FSL_IMX8MM_SAI5] = { 0x30050000, 64 * KiB, "sai5" },
    [FSL_IMX8MM_SAI3] = { 0x30030000, 64 * KiB, "sai3" },
    [FSL_IMX8MM_SAI2] = { 0x30020000, 64 * KiB, "sai2" },
    [FSL_IMX8MM_SAI1] = { 0x30010000, 64 * KiB, "sai1" },

    /* AIPS-1 End */

    [FSL_IMX8MM_A53_DAP] = { 0x28000000, 16 * MiB, "a53_dap" },
    [FSL_IMX8MM_PCIE1_MEM] = { 0x18000000, 128 * MiB, "pcie1_mem" },
    [FSL_IMX8MM_QSPI_MEM] = { 0x08000000, 256 * MiB, "qspi_mem" },
    [FSL_IMX8MM_OCRAM] = { 0x00900000, 256 * KiB, "ocram" },
    [FSL_IMX8MM_TCM_DTCM] = { 0x00800000, 128 * KiB, "tcm_dtcm" },
    [FSL_IMX8MM_TCM_ITCM] = { 0x007e0000, 128 * KiB, "tcm_itcm" },
    [FSL_IMX8MM_OCRAM_S] = { 0x00180000, 32 * KiB, "ocram_s" },
    [FSL_IMX8MM_CAAM_MEM] = { 0x00100000, 32 * KiB, "caam_mem" },
    [FSL_IMX8MM_BOOT_ROM_PROTECTED] = { 0x0003f000, 4 * KiB, "boot_rom_protected" },
    [FSL_IMX8MM_BOOT_ROM] = { 0x00000000, 252 * KiB, "boot_rom" },
};

static void fsl_imx8mm_init(Object *obj)
{
    FslImx8mmState *s = FSL_IMX8MM(obj);
    int i;

    object_initialize_child(obj, "gic", &s->gic, gicv3_class_name());

    object_initialize_child(obj, "ccm", &s->ccm, TYPE_IMX8MP_CCM);

    object_initialize_child(obj, "analog", &s->analog, TYPE_IMX8MP_ANALOG);

    object_initialize_child(obj, "snvs", &s->snvs, TYPE_IMX7_SNVS);

    for (i = 0; i < FSL_IMX8MM_NUM_UARTS; i++) {
        g_autofree char *name = g_strdup_printf("uart%d", i + 1);
        object_initialize_child(obj, name, &s->uart[i], TYPE_IMX_SERIAL);
    }

    for (i = 0; i < FSL_IMX8MM_NUM_GPTS; i++) {
        g_autofree char *name = g_strdup_printf("gpt%d", i + 1);
        object_initialize_child(obj, name, &s->gpt[i], TYPE_IMX8MM_GPT);
    }
    object_initialize_child(obj, "gpt5-gpt6-irq", &s->gpt5_gpt6_irq,
                            TYPE_OR_IRQ);

    for (i = 0; i < FSL_IMX8MM_NUM_I2CS; i++) {
        g_autofree char *name = g_strdup_printf("i2c%d", i + 1);
        object_initialize_child(obj, name, &s->i2c[i], TYPE_IMX_I2C);
    }

    for (i = 0; i < FSL_IMX8MM_NUM_GPIOS; i++) {
        g_autofree char *name = g_strdup_printf("gpio%d", i + 1);
        object_initialize_child(obj, name, &s->gpio[i], TYPE_IMX_GPIO);
    }

    for (i = 0; i < FSL_IMX8MM_NUM_USDHCS; i++) {
        g_autofree char *name = g_strdup_printf("usdhc%d", i + 1);
        object_initialize_child(obj, name, &s->usdhc[i], TYPE_IMX_USDHC);
    }

    for (i = 0; i < FSL_IMX8MM_NUM_USBS; i++) {
        g_autofree char *name = g_strdup_printf("usb%d", i);
        object_initialize_child(obj, name, &s->usb[i], TYPE_USB_DWC3);
    }

    for (i = 0; i < FSL_IMX8MM_NUM_ECSPIS; i++) {
        g_autofree char *name = g_strdup_printf("spi%d", i + 1);
        object_initialize_child(obj, name, &s->spi[i], TYPE_IMX_SPI);
    }

    for (i = 0; i < FSL_IMX8MM_NUM_WDTS; i++) {
        g_autofree char *name = g_strdup_printf("wdt%d", i);
        object_initialize_child(obj, name, &s->wdt[i], TYPE_IMX2_WDT);
    }

    object_initialize_child(obj, "eth0", &s->enet, TYPE_IMX_ENET);

    object_initialize_child(obj, "pcie", &s->pcie, TYPE_DESIGNWARE_PCIE_HOST);
    object_initialize_child(obj, "pcie_phy", &s->pcie_phy,
                            TYPE_FSL_IMX8M_PCIE_PHY);
}

static void fsl_imx8mm_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    FslImx8mmState *s = FSL_IMX8MM(dev);
    DeviceState *gicdev = DEVICE(&s->gic);
    const char *cpu_type =
        ms->cpu_type ?: ARM_CPU_TYPE_NAME("cortex-a53");
    int i;

    if (ms->smp.cpus > FSL_IMX8MM_NUM_CPUS) {
        error_setg(errp, "%s: Only %d CPUs are supported (%d requested)",
                   TYPE_FSL_IMX8MM, FSL_IMX8MM_NUM_CPUS, ms->smp.cpus);
        return;
    }

    for (i = 0; i < ms->smp.cpus; i++) {
        g_autofree char *name = g_strdup_printf("cpu%d", i);
        object_initialize_child(OBJECT(dev), name,
                                &s->cpu[i], cpu_type);
    }

    /* CPUs */
    for (i = 0; i < ms->smp.cpus; i++) {
        /* On uniprocessor, the CBAR is set to 0 */
        if (ms->smp.cpus > 1 &&
                object_property_find(OBJECT(&s->cpu[i]), "reset-cbar")) {
            object_property_set_int(OBJECT(&s->cpu[i]), "reset-cbar",
                                    fsl_imx8mm_memmap[FSL_IMX8MM_GIC_DIST].addr,
                                    &error_abort);
        }

        /*
         * CNTFID0 base frequency in Hz of system counter
         */
        object_property_set_int(OBJECT(&s->cpu[i]), "cntfrq", 8000000,
                                &error_abort);

        if (object_property_find(OBJECT(&s->cpu[i]), "has_el2")) {
            object_property_set_bool(OBJECT(&s->cpu[i]), "has_el2",
                                     !kvm_enabled(), &error_abort);
        }

        if (object_property_find(OBJECT(&s->cpu[i]), "has_el3")) {
            object_property_set_bool(OBJECT(&s->cpu[i]), "has_el3",
                                     !kvm_enabled(), &error_abort);
        }

        if (i) {
            /*
             * Secondary CPUs start in powered-down state (and can be
             * powered up via the SRC system reset controller)
             */
            object_property_set_bool(OBJECT(&s->cpu[i]), "start-powered-off",
                                     true, &error_abort);
        }

        if (!qdev_realize(DEVICE(&s->cpu[i]), NULL, errp)) {
            return;
        }
    }

    /* GIC */
    {
        SysBusDevice *gicsbd = SYS_BUS_DEVICE(&s->gic);
        QList *redist_region_count;
        bool pmu = object_property_get_bool(OBJECT(first_cpu), "pmu", NULL);

        qdev_prop_set_uint32(gicdev, "num-cpu", ms->smp.cpus);
        qdev_prop_set_uint32(gicdev, "num-irq",
                             FSL_IMX8MM_NUM_IRQS + GIC_INTERNAL);
        redist_region_count = qlist_new();
        qlist_append_int(redist_region_count, ms->smp.cpus);
        qdev_prop_set_array(gicdev, "redist-region-count", redist_region_count);
        object_property_set_link(OBJECT(&s->gic), "sysmem",
                                 OBJECT(get_system_memory()), &error_fatal);
        if (!sysbus_realize(gicsbd, errp)) {
            return;
        }
        sysbus_mmio_map(gicsbd, 0, fsl_imx8mm_memmap[FSL_IMX8MM_GIC_DIST].addr);
        sysbus_mmio_map(gicsbd, 1, fsl_imx8mm_memmap[FSL_IMX8MM_GIC_REDIST].addr);

        /*
         * Wire the outputs from each CPU's generic timer and the GICv3
         * maintenance interrupt signal to the appropriate GIC PPI inputs, and
         * the GIC's IRQ/FIQ interrupt outputs to the CPU's inputs.
         */
        for (i = 0; i < ms->smp.cpus; i++) {
            DeviceState *cpudev = DEVICE(&s->cpu[i]);
            int intidbase = FSL_IMX8MM_NUM_IRQS + i * GIC_INTERNAL;
            qemu_irq irq;

            /*
             * Mapping from the output timer irq lines from the CPU to the
             * GIC PPI inputs.
             */
            static const int timer_irqs[] = {
                [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
                [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
                [GTIMER_HYP]  = ARCH_TIMER_NS_EL2_IRQ,
                [GTIMER_SEC]  = ARCH_TIMER_S_EL1_IRQ,
            };

            for (int j = 0; j < ARRAY_SIZE(timer_irqs); j++) {
                irq = qdev_get_gpio_in(gicdev, intidbase + timer_irqs[j]);
                qdev_connect_gpio_out(cpudev, j, irq);
            }

            irq = qdev_get_gpio_in(gicdev, intidbase + ARCH_GIC_MAINT_IRQ);
            qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt",
                                        0, irq);

            irq = qdev_get_gpio_in(gicdev, intidbase + VIRTUAL_PMU_IRQ);
            qdev_connect_gpio_out_named(cpudev, "pmu-interrupt", 0, irq);

            sysbus_connect_irq(gicsbd, i,
                               qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
            sysbus_connect_irq(gicsbd, i + ms->smp.cpus,
                               qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
            sysbus_connect_irq(gicsbd, i + 2 * ms->smp.cpus,
                               qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
            sysbus_connect_irq(gicsbd, i + 3 * ms->smp.cpus,
                               qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));

            if (kvm_enabled()) {
                if (pmu) {
                    assert(arm_feature(&s->cpu[i].env, ARM_FEATURE_PMU));
                    if (kvm_irqchip_in_kernel()) {
                        kvm_arm_pmu_set_irq(&s->cpu[i], VIRTUAL_PMU_IRQ);
                    }
                    kvm_arm_pmu_init(&s->cpu[i]);
                }
            }
        }
    }

    /* CCM */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ccm), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ccm), 0,
                    fsl_imx8mm_memmap[FSL_IMX8MM_CCM].addr);

    /* Analog */
    object_property_set_uint(OBJECT(&s->analog), "arm-pll-fdiv-ctl0-reset",
                            0x000fa030, &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->analog), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->analog), 0,
                    fsl_imx8mm_memmap[FSL_IMX8MM_ANA_PLL].addr);

    /* UARTs */
    for (i = 0; i < FSL_IMX8MM_NUM_UARTS; i++) {
        static const struct {
            hwaddr addr;
            unsigned int irq;
        } serial_table[FSL_IMX8MM_NUM_UARTS] = {
            { fsl_imx8mm_memmap[FSL_IMX8MM_UART1].addr, FSL_IMX8MM_UART1_IRQ },
            { fsl_imx8mm_memmap[FSL_IMX8MM_UART2].addr, FSL_IMX8MM_UART2_IRQ },
            { fsl_imx8mm_memmap[FSL_IMX8MM_UART3].addr, FSL_IMX8MM_UART3_IRQ },
            { fsl_imx8mm_memmap[FSL_IMX8MM_UART4].addr, FSL_IMX8MM_UART4_IRQ },
        };

        qdev_prop_set_chr(DEVICE(&s->uart[i]), "chardev", serial_hd(i));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart[i]), 0, serial_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0,
                           qdev_get_gpio_in(gicdev, serial_table[i].irq));
    }

    /* On-Chip RAM */
    if (!memory_region_init_ram(&s->ocram, OBJECT(dev), "imx8mm.ocram",
                                fsl_imx8mm_memmap[FSL_IMX8MM_OCRAM].size,
                                errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(),
                                fsl_imx8mm_memmap[FSL_IMX8MM_OCRAM].addr,
                                &s->ocram);

    /* GPTs */
    object_property_set_int(OBJECT(&s->gpt5_gpt6_irq), "num-lines", 2,
                            &error_abort);
    if (!qdev_realize(DEVICE(&s->gpt5_gpt6_irq), NULL, errp)) {
        return;
    }

    qdev_connect_gpio_out(DEVICE(&s->gpt5_gpt6_irq), 0,
                          qdev_get_gpio_in(gicdev, FSL_IMX8MM_GPT5_GPT6_IRQ));

    for (i = 0; i < FSL_IMX8MM_NUM_GPTS; i++) {
        hwaddr gpt_addrs[FSL_IMX8MM_NUM_GPTS] = {
            fsl_imx8mm_memmap[FSL_IMX8MM_GPT1].addr,
            fsl_imx8mm_memmap[FSL_IMX8MM_GPT2].addr,
            fsl_imx8mm_memmap[FSL_IMX8MM_GPT3].addr,
            fsl_imx8mm_memmap[FSL_IMX8MM_GPT4].addr,
            fsl_imx8mm_memmap[FSL_IMX8MM_GPT5].addr,
            fsl_imx8mm_memmap[FSL_IMX8MM_GPT6].addr,
        };

        object_property_set_link(OBJECT(&s->gpt[i]), "clock-control-module",
                                 OBJECT(&s->ccm), &error_abort);

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpt[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpt[i]), 0, gpt_addrs[i]);

        if (i < FSL_IMX8MM_NUM_GPTS - 2) {
            static const unsigned int gpt_irqs[FSL_IMX8MM_NUM_GPTS - 2] = {
                FSL_IMX8MM_GPT1_IRQ,
                FSL_IMX8MM_GPT2_IRQ,
                FSL_IMX8MM_GPT3_IRQ,
                FSL_IMX8MM_GPT4_IRQ,
            };

            sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpt[i]), 0,
                               qdev_get_gpio_in(gicdev, gpt_irqs[i]));
        } else {
            int irq = i - FSL_IMX8MM_NUM_GPTS + 2;

            sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpt[i]), 0,
                               qdev_get_gpio_in(DEVICE(&s->gpt5_gpt6_irq), irq));
        }
    }

    /* I2Cs */
    for (i = 0; i < FSL_IMX8MM_NUM_I2CS; i++) {
        static const struct {
            hwaddr addr;
            unsigned int irq;
        } i2c_table[FSL_IMX8MM_NUM_I2CS] = {
            { fsl_imx8mm_memmap[FSL_IMX8MM_I2C1].addr, FSL_IMX8MM_I2C1_IRQ },
            { fsl_imx8mm_memmap[FSL_IMX8MM_I2C2].addr, FSL_IMX8MM_I2C2_IRQ },
            { fsl_imx8mm_memmap[FSL_IMX8MM_I2C3].addr, FSL_IMX8MM_I2C3_IRQ },
            { fsl_imx8mm_memmap[FSL_IMX8MM_I2C4].addr, FSL_IMX8MM_I2C4_IRQ },
        };

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->i2c[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->i2c[i]), 0, i2c_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->i2c[i]), 0,
                           qdev_get_gpio_in(gicdev, i2c_table[i].irq));
    }

    /* GPIOs */
    for (i = 0; i < FSL_IMX8MM_NUM_GPIOS; i++) {
        static const struct {
            hwaddr addr;
            unsigned int irq_low;
            unsigned int irq_high;
        } gpio_table[FSL_IMX8MM_NUM_GPIOS] = {
            {
                fsl_imx8mm_memmap[FSL_IMX8MM_GPIO1].addr,
                FSL_IMX8MM_GPIO1_LOW_IRQ,
                FSL_IMX8MM_GPIO1_HIGH_IRQ
            },
            {
                fsl_imx8mm_memmap[FSL_IMX8MM_GPIO2].addr,
                FSL_IMX8MM_GPIO2_LOW_IRQ,
                FSL_IMX8MM_GPIO2_HIGH_IRQ
            },
            {
                fsl_imx8mm_memmap[FSL_IMX8MM_GPIO3].addr,
                FSL_IMX8MM_GPIO3_LOW_IRQ,
                FSL_IMX8MM_GPIO3_HIGH_IRQ
            },
            {
                fsl_imx8mm_memmap[FSL_IMX8MM_GPIO4].addr,
                FSL_IMX8MM_GPIO4_LOW_IRQ,
                FSL_IMX8MM_GPIO4_HIGH_IRQ
            },
            {
                fsl_imx8mm_memmap[FSL_IMX8MM_GPIO5].addr,
                FSL_IMX8MM_GPIO5_LOW_IRQ,
                FSL_IMX8MM_GPIO5_HIGH_IRQ
            },
        };
        object_property_set_bool(OBJECT(&s->gpio[i]), "has-edge-sel", true,
                                 &error_abort);
        object_property_set_bool(OBJECT(&s->gpio[i]), "has-upper-pin-irq",
                                 true, &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio[i]), 0, gpio_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio[i]), 0,
                           qdev_get_gpio_in(gicdev, gpio_table[i].irq_low));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio[i]), 1,
                           qdev_get_gpio_in(gicdev, gpio_table[i].irq_high));
    }

    /* USDHCs */
    for (i = 0; i < FSL_IMX8MM_NUM_USDHCS; i++) {
        static const struct {
            hwaddr addr;
            unsigned int irq;
        } usdhc_table[FSL_IMX8MM_NUM_USDHCS] = {
            { fsl_imx8mm_memmap[FSL_IMX8MM_USDHC1].addr, FSL_IMX8MM_USDHC1_IRQ },
            { fsl_imx8mm_memmap[FSL_IMX8MM_USDHC2].addr, FSL_IMX8MM_USDHC2_IRQ },
            { fsl_imx8mm_memmap[FSL_IMX8MM_USDHC3].addr, FSL_IMX8MM_USDHC3_IRQ },
        };

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->usdhc[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->usdhc[i]), 0, usdhc_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usdhc[i]), 0,
                           qdev_get_gpio_in(gicdev, usdhc_table[i].irq));
    }

    /* USBs */
    for (i = 0; i < FSL_IMX8MM_NUM_USBS; i++) {
        static const struct {
            hwaddr addr;
            unsigned int irq;
        } usb_table[FSL_IMX8MM_NUM_USBS] = {
            { fsl_imx8mm_memmap[FSL_IMX8MM_USB1].addr, FSL_IMX8MM_USB1_IRQ },
            { fsl_imx8mm_memmap[FSL_IMX8MM_USB2].addr, FSL_IMX8MM_USB2_IRQ },
        };

        qdev_prop_set_uint32(DEVICE(&s->usb[i].sysbus_xhci), "p2", 1);
        qdev_prop_set_uint32(DEVICE(&s->usb[i].sysbus_xhci), "p3", 1);
        qdev_prop_set_uint32(DEVICE(&s->usb[i].sysbus_xhci), "slots", 2);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->usb[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->usb[i]), 0, usb_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usb[i].sysbus_xhci), 0,
                           qdev_get_gpio_in(gicdev, usb_table[i].irq));
    }

    /* ECSPIs */
    for (i = 0; i < FSL_IMX8MM_NUM_ECSPIS; i++) {
        static const struct {
            hwaddr addr;
            unsigned int irq;
        } spi_table[FSL_IMX8MM_NUM_ECSPIS] = {
            { fsl_imx8mm_memmap[FSL_IMX8MM_ECSPI1].addr, FSL_IMX8MM_ECSPI1_IRQ },
            { fsl_imx8mm_memmap[FSL_IMX8MM_ECSPI2].addr, FSL_IMX8MM_ECSPI2_IRQ },
            { fsl_imx8mm_memmap[FSL_IMX8MM_ECSPI3].addr, FSL_IMX8MM_ECSPI3_IRQ },
        };

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->spi[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[i]), 0, spi_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi[i]), 0,
                           qdev_get_gpio_in(gicdev, spi_table[i].irq));
    }

    /* ENET1 */
    object_property_set_uint(OBJECT(&s->enet), "phy-num", s->phy_num,
                             &error_abort);
    object_property_set_uint(OBJECT(&s->enet), "tx-ring-num", 3, &error_abort);
    qemu_configure_nic_device(DEVICE(&s->enet), true, NULL);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->enet), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->enet), 0,
                    fsl_imx8mm_memmap[FSL_IMX8MM_ENET1].addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->enet), 0,
                       qdev_get_gpio_in(gicdev, FSL_IMX8MM_ENET1_MAC_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->enet), 1,
                       qdev_get_gpio_in(gicdev, FSL_IMX6_ENET1_MAC_1588_IRQ));

    /* SNVS */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->snvs), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->snvs), 0,
                    fsl_imx8mm_memmap[FSL_IMX8MM_SNVS_HP].addr);

    /* Watchdogs */
    for (i = 0; i < FSL_IMX8MM_NUM_WDTS; i++) {
        static const struct {
            hwaddr addr;
            unsigned int irq;
        } wdog_table[FSL_IMX8MM_NUM_WDTS] = {
            { fsl_imx8mm_memmap[FSL_IMX8MM_WDOG1].addr, FSL_IMX8MM_WDOG1_IRQ },
            { fsl_imx8mm_memmap[FSL_IMX8MM_WDOG2].addr, FSL_IMX8MM_WDOG2_IRQ },
            { fsl_imx8mm_memmap[FSL_IMX8MM_WDOG3].addr, FSL_IMX8MM_WDOG3_IRQ },
        };

        object_property_set_bool(OBJECT(&s->wdt[i]), "pretimeout-support",
                                 true, &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->wdt[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->wdt[i]), 0, wdog_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->wdt[i]), 0,
                           qdev_get_gpio_in(gicdev, wdog_table[i].irq));
    }

    /* PCIe */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pcie), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pcie), 0,
                    fsl_imx8mm_memmap[FSL_IMX8MM_PCIE1].addr);

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie), 0,
                       qdev_get_gpio_in(gicdev, FSL_IMX8MM_PCI_INTA_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie), 1,
                       qdev_get_gpio_in(gicdev, FSL_IMX8MM_PCI_INTB_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie), 2,
                       qdev_get_gpio_in(gicdev, FSL_IMX8MM_PCI_INTC_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie), 3,
                       qdev_get_gpio_in(gicdev, FSL_IMX8MM_PCI_INTD_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie), 4,
                       qdev_get_gpio_in(gicdev, FSL_IMX8MM_PCI_MSI_IRQ));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pcie_phy), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pcie_phy), 0,
                    fsl_imx8mm_memmap[FSL_IMX8MM_PCIE_PHY1].addr);

    /* Unimplemented devices */
    for (i = 0; i < ARRAY_SIZE(fsl_imx8mm_memmap); i++) {
        switch (i) {
        case FSL_IMX8MM_ANA_PLL:
        case FSL_IMX8MM_CCM:
        case FSL_IMX8MM_GIC_DIST:
        case FSL_IMX8MM_GIC_REDIST:
        case FSL_IMX8MM_GPIO1 ... FSL_IMX8MM_GPIO5:
        case FSL_IMX8MM_GPT1 ... FSL_IMX8MM_GPT6:
        case FSL_IMX8MM_ECSPI1 ... FSL_IMX8MM_ECSPI3:
        case FSL_IMX8MM_ENET1:
        case FSL_IMX8MM_I2C1 ... FSL_IMX8MM_I2C4:
        case FSL_IMX8MM_PCIE1:
        case FSL_IMX8MM_PCIE_PHY1:
        case FSL_IMX8MM_RAM:
        case FSL_IMX8MM_OCRAM:
        case FSL_IMX8MM_SNVS_HP:
        case FSL_IMX8MM_UART1 ... FSL_IMX8MM_UART4:
        case FSL_IMX8MM_USB1 ... FSL_IMX8MM_USB2:
        case FSL_IMX8MM_USDHC1 ... FSL_IMX8MM_USDHC3:
        case FSL_IMX8MM_WDOG1 ... FSL_IMX8MM_WDOG3:
            /* device implemented and treated above */
            break;

        default:
            create_unimplemented_device(fsl_imx8mm_memmap[i].name,
                                        fsl_imx8mm_memmap[i].addr,
                                        fsl_imx8mm_memmap[i].size);
            break;
        }
    }
}

static const Property fsl_imx8mm_properties[] = {
    DEFINE_PROP_UINT32("fec1-phy-num", FslImx8mmState, phy_num, 0),
    DEFINE_PROP_BOOL("fec1-phy-connected", FslImx8mmState, phy_connected, true),
};

static void fsl_imx8mm_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, fsl_imx8mm_properties);
    dc->realize = fsl_imx8mm_realize;

    dc->desc = "i.MX 8MM SoC";
}

static const TypeInfo fsl_imx8mm_types[] = {
    {
        .name = TYPE_FSL_IMX8MM,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(FslImx8mmState),
        .instance_init = fsl_imx8mm_init,
        .class_init = fsl_imx8mm_class_init,
    },
};

DEFINE_TYPES(fsl_imx8mm_types)
