/*
 * Vectored Interrupt Controller for nios2 processor
 *
 * Copyright (c) 2022 Neuroblade
 *
 * Interface:
 * QOM property "cpu": link to the Nios2 CPU (must be set)
 * Unnamed GPIO inputs 0..NIOS2_VIC_MAX_IRQ-1: input IRQ lines
 * IRQ should be connected to nios2 IRQ0.
 *
 * Reference: "Embedded Peripherals IP User Guide
 *             for Intel® Quartus® Prime Design Suite: 21.4"
 * Chapter 38 "Vectored Interrupt Controller Core"
 * See: https://www.intel.com/content/www/us/en/docs/programmable/683130/21-4/vectored-interrupt-controller-core.html
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"

#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qom/object.h"
#include "hw/intc/nios2_vic.h"
#include "cpu.h"


enum {
    INT_CONFIG0 = 0,
    INT_CONFIG31 = 31,
    INT_ENABLE = 32,
    INT_ENABLE_SET = 33,
    INT_ENABLE_CLR = 34,
    INT_PENDING = 35,
    INT_RAW_STATUS = 36,
    SW_INTERRUPT = 37,
    SW_INTERRUPT_SET = 38,
    SW_INTERRUPT_CLR = 39,
    VIC_CONFIG = 40,
    VIC_STATUS = 41,
    VEC_TBL_BASE = 42,
    VEC_TBL_ADDR = 43,
    CSR_COUNT /* Last! */
};

/* Requested interrupt level (INT_CONFIG[0:5]) */
static inline uint32_t vic_int_config_ril(const Nios2VIC *vic, int irq_num)
{
    return extract32(vic->int_config[irq_num], 0, 6);
}

/* Requested NMI (INT_CONFIG[6]) */
static inline uint32_t vic_int_config_rnmi(const Nios2VIC *vic, int irq_num)
{
    return extract32(vic->int_config[irq_num], 6, 1);
}

/* Requested register set (INT_CONFIG[7:12]) */
static inline uint32_t vic_int_config_rrs(const Nios2VIC *vic, int irq_num)
{
    return extract32(vic->int_config[irq_num], 7, 6);
}

static inline uint32_t vic_config_vec_size(const Nios2VIC *vic)
{
    return 1 << (2 + extract32(vic->vic_config, 0, 3));
}

static inline uint32_t vic_int_pending(const Nios2VIC *vic)
{
    return (vic->int_raw_status | vic->sw_int) & vic->int_enable;
}

static void vic_update_irq(Nios2VIC *vic)
{
    Nios2CPU *cpu = NIOS2_CPU(vic->cpu);
    uint32_t pending = vic_int_pending(vic);
    int irq = -1;
    int max_ril = 0;
    /* Note that if RIL is 0 for an interrupt it is effectively disabled */

    vic->vec_tbl_addr = 0;
    vic->vic_status = 0;

    if (pending == 0) {
        qemu_irq_lower(vic->output_int);
        return;
    }

    for (int i = 0; i < NIOS2_VIC_MAX_IRQ; i++) {
        if (pending & BIT(i)) {
            int ril = vic_int_config_ril(vic, i);
            if (ril > max_ril) {
                irq = i;
                max_ril = ril;
            }
        }
    }

    if (irq < 0) {
        qemu_irq_lower(vic->output_int);
        return;
    }

    vic->vec_tbl_addr = irq * vic_config_vec_size(vic) + vic->vec_tbl_base;
    vic->vic_status = irq | BIT(31);

    /*
     * In hardware, the interface between the VIC and the CPU is via the
     * External Interrupt Controller interface, where the interrupt controller
     * presents the CPU with a packet of data containing:
     *  - Requested Handler Address (RHA): 32 bits
     *  - Requested Register Set (RRS) : 6 bits
     *  - Requested Interrupt Level (RIL) : 6 bits
     *  - Requested NMI flag (RNMI) : 1 bit
     * In our emulation, we implement this by writing the data directly to
     * fields in the CPU object and then raising the IRQ line to tell
     * the CPU that we've done so.
     */

    cpu->rha = vic->vec_tbl_addr;
    cpu->ril = max_ril;
    cpu->rrs = vic_int_config_rrs(vic, irq);
    cpu->rnmi = vic_int_config_rnmi(vic, irq);

    qemu_irq_raise(vic->output_int);
}

static void vic_set_irq(void *opaque, int irq_num, int level)
{
    Nios2VIC *vic = opaque;

    vic->int_raw_status = deposit32(vic->int_raw_status, irq_num, 1, !!level);
    vic_update_irq(vic);
}

static void nios2_vic_reset(DeviceState *dev)
{
    Nios2VIC *vic = NIOS2_VIC(dev);

    memset(&vic->int_config, 0, sizeof(vic->int_config));
    vic->vic_config = 0;
    vic->int_raw_status = 0;
    vic->int_enable = 0;
    vic->sw_int = 0;
    vic->vic_status = 0;
    vic->vec_tbl_base = 0;
    vic->vec_tbl_addr = 0;
}

static uint64_t nios2_vic_csr_read(void *opaque, hwaddr offset, unsigned size)
{
    Nios2VIC *vic = opaque;
    int index = offset / 4;

    switch (index) {
    case INT_CONFIG0 ... INT_CONFIG31:
        return vic->int_config[index - INT_CONFIG0];
    case INT_ENABLE:
        return vic->int_enable;
    case INT_PENDING:
        return vic_int_pending(vic);
    case INT_RAW_STATUS:
        return vic->int_raw_status;
    case SW_INTERRUPT:
        return vic->sw_int;
    case VIC_CONFIG:
        return vic->vic_config;
    case VIC_STATUS:
        return vic->vic_status;
    case VEC_TBL_BASE:
        return vic->vec_tbl_base;
    case VEC_TBL_ADDR:
        return vic->vec_tbl_addr;
    default:
        return 0;
    }
}

static void nios2_vic_csr_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size)
{
    Nios2VIC *vic = opaque;
    int index = offset / 4;

    switch (index) {
    case INT_CONFIG0 ... INT_CONFIG31:
        vic->int_config[index - INT_CONFIG0] = value;
        break;
    case INT_ENABLE:
        vic->int_enable = value;
        break;
    case INT_ENABLE_SET:
        vic->int_enable |= value;
        break;
    case INT_ENABLE_CLR:
        vic->int_enable &= ~value;
        break;
    case SW_INTERRUPT:
        vic->sw_int = value;
        break;
    case SW_INTERRUPT_SET:
        vic->sw_int |= value;
        break;
    case SW_INTERRUPT_CLR:
        vic->sw_int &= ~value;
        break;
    case VIC_CONFIG:
        vic->vic_config = value;
        break;
    case VEC_TBL_BASE:
        vic->vec_tbl_base = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "nios2-vic: write to invalid CSR address %#"
                      HWADDR_PRIx "\n", offset);
    }

    vic_update_irq(vic);
}

static const MemoryRegionOps nios2_vic_csr_ops = {
    .read = nios2_vic_csr_read,
    .write = nios2_vic_csr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 }
};

static void nios2_vic_realize(DeviceState *dev, Error **errp)
{
    Nios2VIC *vic = NIOS2_VIC(dev);

    if (!vic->cpu) {
        /* This is a programming error in the code using this device */
        error_setg(errp, "nios2-vic 'cpu' link property was not set");
        return;
    }

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &vic->output_int);
    qdev_init_gpio_in(dev, vic_set_irq, NIOS2_VIC_MAX_IRQ);

    memory_region_init_io(&vic->csr, OBJECT(dev), &nios2_vic_csr_ops, vic,
                          "nios2.vic.csr", CSR_COUNT * sizeof(uint32_t));
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &vic->csr);
}

static Property nios2_vic_properties[] = {
    DEFINE_PROP_LINK("cpu", Nios2VIC, cpu, TYPE_CPU, CPUState *),
    DEFINE_PROP_END_OF_LIST()
};

static const VMStateDescription nios2_vic_vmstate = {
    .name = "nios2-vic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]){
        VMSTATE_UINT32_ARRAY(int_config, Nios2VIC, 32),
        VMSTATE_UINT32(vic_config, Nios2VIC),
        VMSTATE_UINT32(int_raw_status, Nios2VIC),
        VMSTATE_UINT32(int_enable, Nios2VIC),
        VMSTATE_UINT32(sw_int, Nios2VIC),
        VMSTATE_UINT32(vic_status, Nios2VIC),
        VMSTATE_UINT32(vec_tbl_base, Nios2VIC),
        VMSTATE_UINT32(vec_tbl_addr, Nios2VIC),
        VMSTATE_END_OF_LIST()
    },
};

static void nios2_vic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = nios2_vic_reset;
    dc->realize = nios2_vic_realize;
    dc->vmsd = &nios2_vic_vmstate;
    device_class_set_props(dc, nios2_vic_properties);
}

static const TypeInfo nios2_vic_info = {
    .name = TYPE_NIOS2_VIC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Nios2VIC),
    .class_init = nios2_vic_class_init,
};

static void nios2_vic_register_types(void)
{
    type_register_static(&nios2_vic_info);
}

type_init(nios2_vic_register_types);
