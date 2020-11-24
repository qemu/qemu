/*
 * imx6ul adc device
 *
 * Implements imx6ul adc device driver
 * Currently, it does not implement all the functionalities of this chip.
 * Written by Jay Mehta
 *
 * Copyright (c) 2020 Nanosonics Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/adc/imx6ul_adc.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"

#ifndef DEBUG_IMX6UL_ADC
#define DEBUG_IMX6UL_ADC 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX6UL_ADC) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX6UL_ADC, \
                                             __func__, ##args); \
        } \
    } while (0)

#define NUM_IMX6UL_ADC_CHANNELS     16

static const AdcSimChannels adc_simulated_reading_map[NUM_IMX6UL_ADC_CHANNELS] = {
                                                                                    ADC_SIM_CHANNEL_15,
                                                                                    ADC_SIM_CHANNEL_16,
                                                                                    ADC_SIM_CHANNEL_17,
                                                                                    NUM_ADC_SIM_CHANNELS,
                                                                                    NUM_ADC_SIM_CHANNELS,
                                                                                    NUM_ADC_SIM_CHANNELS,
                                                                                    NUM_ADC_SIM_CHANNELS,
                                                                                    NUM_ADC_SIM_CHANNELS,
                                                                                    NUM_ADC_SIM_CHANNELS,
                                                                                    NUM_ADC_SIM_CHANNELS,
                                                                                    NUM_ADC_SIM_CHANNELS,
                                                                                    NUM_ADC_SIM_CHANNELS,
                                                                                    NUM_ADC_SIM_CHANNELS,
                                                                                    NUM_ADC_SIM_CHANNELS,
                                                                                    NUM_ADC_SIM_CHANNELS,
                                                                                    NUM_ADC_SIM_CHANNELS
                                                                                 };

static const char *imx6ul_adc_get_regname(unsigned offset)
{
    switch (offset) {
    case HC_ADDR:
        return "HC";
    case HS_ADDR:
        return "HS";
    case R_ADDR:
        return "R";
    case CFG_ADDR:
        return "cfg";
    case GC_ADDR:
        return "GC";
    case GS_ADDR:
        return "GS";
    case CV_ADDR:
        return "CV";
    case OFS_ADDR:
        return "OFS";
    case CAL_ADDR:
        return "CAL";
    default:
        return "[?]";
    }
}

static inline bool imx6ul_adc_interrupt_is_enabled(IMX6ULADCState *s)
{
    return (s->hc & HC_AIEN_MASK) == HC_AIEN_MASK;
}

static void imx6ul_adc_reset(DeviceState *dev)
{
    IMX6ULADCState *s = IMX6UL_ADC(dev);

    s->hc   = HC_RESET;
    s->hs   = HS_RESET;
    s->r    = R_RESET;
    s->cfg  = CFG_RESET;
    s->gc   = GC_RESET;
    s->gs   = GS_RESET;
    s->cv   = CV_RESET;
    s->ofs  = OFS_RESET;
    s->cal  = CAL_RESET;
}

static inline void imx6ul_adc_raise_interrupt(IMX6ULADCState *s)
{
    // Raise an interrupt if it is configured to generate interrupts.

    if(imx6ul_adc_interrupt_is_enabled(s)) 
    {
        qemu_irq_raise(s->irq);
    }
}

static inline void imx6ul_adc_lower_interrupt(IMX6ULADCState *s)
{
    qemu_irq_lower(s->irq);
}

static void imx6ul_adc_get_simulated_reading(IMX6ULADCState *s)
{
    uint32_t selected_channel = s->hc & HC_ADCH_MASK;
    
    if(selected_channel >= NUM_IMX6UL_ADC_CHANNELS)
    {
        s->r = 0;
        return;
    }

    uint16_t reading = adc_get_sample(s->p_adc_simulator, adc_simulated_reading_map[selected_channel]);

    switch ((s->cfg & CFG_MODE_MASK) >> CFG_MODE_SHIFT) 
    {
        case ADC_8_BIT_MODE:
            s->r = reading & 0xFF;
            break;
        case ADC_10_BIT_MODE:
            s->r = reading & 0x3FF;
            break;
        case ADC_12_BIT_MODE:
            s->r = reading & 0xFFF;
            break;
        default:
            /* 8-bit */
            s->r = reading & 0xFF;
            break;
    }
}

static uint64_t imx6ul_adc_read(void *opaque, hwaddr offset,
                             unsigned size)
{
    uint32_t value;
    IMX6ULADCState *s = IMX6UL_ADC(opaque);

    switch (offset) {
    case HC_ADDR:
        value = s->hc;
        break;
    case HS_ADDR:
        value = s->hs;
        imx6ul_adc_lower_interrupt(s);      // Lower the interrupt once status register is read for COCO bit
        break;
    case R_ADDR:
        value = s->r;
        s->r = 0;                           // Reading data register will clear previous data.
        break;
    case CFG_ADDR:
        value = s->cfg;
        break;
    case GC_ADDR:
        value = s->gc;
        break;
    case GS_ADDR:
        value = s->gs;
        break;
    case CV_ADDR:
        value = s->cv;
        break;
    case OFS_ADDR:
        value = s->ofs;
        break;  
    case CAL_ADDR:
        value = s->cal;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad address at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX6UL_ADC, __func__, offset);
        value = 0;
        break;
    }

    DPRINTF("read %s [0x%" HWADDR_PRIx "] -> 0x%02x\n", imx6ul_adc_get_regname(offset), offset, value);

    return (uint64_t)value;
}

static void imx6ul_adc_write(void *opaque, hwaddr offset,
                          uint64_t value, unsigned size)
{
    IMX6ULADCState *s = IMX6UL_ADC(opaque);

    DPRINTF("write %s [0x%" HWADDR_PRIx "] <- 0x%02x\n", imx6ul_adc_get_regname(offset), offset, (int)value);

    value &= 0xffffffff;

    switch (offset) 
    {
        case HC_ADDR:
            s->hc = value & HC_MASK;

            // A write to HC register will trigger a conversion in following conditions
            // ADC channel value is not equal to HC_ADCH_MASK
            // A conversion is not in progress, if it is in progress then the conversion is aborted but in our use case,
            //      it is simply ignored.
            // ADC Trigger is set to software trigger as that is the only supported mode.

            if(((s->gs & GS_ADACT_MASK) == 0)     &&
               ((s->cfg & CFG_ADTRG_MASK) == 0)   &&
               ((s->hc & HC_ADCH_MASK) != HC_ADCH_MASK))
            {
                s->gs |= GS_ADACT_MASK;             // Conversion is now in progress
            }
            break;
        case HS_ADDR:
            DPRINTF("Write to %s [0x%" HWADDR_PRIx "] register ignored.\n", imx6ul_adc_get_regname(offset), offset);
            break;
        case R_ADDR:
            DPRINTF("Write to %s [0x%" HWADDR_PRIx "] register ignored.\n", imx6ul_adc_get_regname(offset), offset);
            break;
        case CFG_ADDR:
            s->cfg = value & CFG_MASK;
            break;
        case GC_ADDR:
            s->gc = value & GC_MASK;
            if((s->gc & GC_CAL_MASK) == GC_CAL_MASK)    // Calibration started
            {
                if((s->gs & GS_ADACT_MASK) == 0)        // Fail calibration immediately if software trigger not enabled
                {
                    s->hs |= HS_COCO_MASK;              // Write conversion complete flag
                    s->gs &= ~GS_CALF_MASK;             // Set calibration successful
                    s->cal = 0x9;                       // Write dummy value to calibration register
                    imx6ul_adc_raise_interrupt(s);      // Raise interrupt if enabled
                }
                else
                {
                    s->gs |= GS_CALF_MASK;              // Set calibration failed
                }

                s->gc &= ~GC_CAL_MASK;              // Clear calibration bit to indicate completion
            }
            break;
        case GS_ADDR:
            s->gs = ((value & GS_MASK) & (~GS_ADACT_MASK));     // Ignore ADACT bit in GS register as it is read-only
            break;
        case CV_ADDR:
            s->cv = value & CV_MASK;
            break;
        case OFS_ADDR:
            s->ofs = value & OFS_MASK;
            break;
        case CAL_ADDR:
            s->cal = value & CAL_MASK;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad address at offset 0x%"
                          HWADDR_PRIx "\n", TYPE_IMX6UL_ADC, __func__, offset);
            break;
    }
}

static void imx6ul_adc_conversion_callback(void *opaque)
{
    IMX6ULADCState *s = IMX6UL_ADC(opaque);

    if((s->gs & GS_ADACT_MASK) == GS_ADACT_MASK)    // Conversion is in progress
    {
        DPRINTF("ADC conversion is active.\n");
        imx6ul_adc_get_simulated_reading(s);
        s->hs |= HS_COCO_MASK;              // Write conversion complete flag

        s->gs &= ~GS_ADACT_MASK;            // Conversion is not in progress anymore
        imx6ul_adc_raise_interrupt(s);
    }
    timer_mod(s->conversion_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + CONVERSION_TIMER_MS);    // Set timer to expire in CONVERSION_TIMER_MS
}

static const MemoryRegionOps imx6ul_adc_ops = {
    .read = imx6ul_adc_read,
    .write = imx6ul_adc_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription imx6ul_adc_vmstate = {
    .name = TYPE_IMX6UL_ADC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(hc, IMX6ULADCState),
        VMSTATE_UINT32(hs, IMX6ULADCState),
        VMSTATE_UINT32(r, IMX6ULADCState),
        VMSTATE_UINT32(cfg, IMX6ULADCState),
        VMSTATE_UINT32(gc, IMX6ULADCState),
        VMSTATE_UINT32(gs, IMX6ULADCState),
        VMSTATE_UINT32(cv, IMX6ULADCState),
        VMSTATE_UINT32(ofs, IMX6ULADCState),
        VMSTATE_UINT32(cal, IMX6ULADCState),
        VMSTATE_END_OF_LIST()
    }
};

static void imx6ul_adc_realize(DeviceState *dev, Error **errp)
{
    IMX6ULADCState *s = IMX6UL_ADC(dev);

    char adc_sim_device_path[100];
    
    sprintf(adc_sim_device_path, "/machine/%s", NAME_ADCSAMPLESIM);
    s->p_adc_simulator = ADCSAMPLESIM(object_resolve_path(adc_sim_device_path, NULL));

    if(s->p_adc_simulator == NULL)
    {
        error_report("%s: ADC simulator device not found.\n", __func__);
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &imx6ul_adc_ops, s, TYPE_IMX6UL_ADC,
                          IMX6UL_ADC_MEM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    s->conversion_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, imx6ul_adc_conversion_callback, s);
    timer_mod(s->conversion_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + CONVERSION_TIMER_MS);    // Set timer to expire in CONVERSION_TIMER_MS
}

static void imx6ul_adc_unrealize(DeviceState *dev)
{
    IMX6ULADCState *s = IMX6UL_ADC(dev);
    timer_deinit(s->conversion_timer);
}

static void imx6ul_adc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &imx6ul_adc_vmstate;
    dc->reset = imx6ul_adc_reset;
    dc->realize = imx6ul_adc_realize;
    dc->unrealize = imx6ul_adc_unrealize;
    dc->desc = "i.MX6UL adc device driver";
}

static const TypeInfo imx6ul_adc_type_info = {
    .name = TYPE_IMX6UL_ADC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMX6ULADCState),
    .class_init = imx6ul_adc_class_init,
};

static void imx6ul_adc_register_types(void)
{
    type_register_static(&imx6ul_adc_type_info);
}

type_init(imx6ul_adc_register_types)
