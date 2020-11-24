/*
 * Copyright (c) 2020 Nanosonics
 *
 * Nanosonics IMX6UL PWM emulation.
 *
 *
 * This code is licensed under the GPL, version 2 or later.
 * See the file `COPYING' in the top level directory.
 *
 * It (partially) emulates nanosonics platform with a Freescale
 * i.MX6ul SoC
 */

#include "qemu/osdep.h"
#include "hw/pwm/nano_pwm.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "util/nano_utils.h"
#include "hw/display/nano_fb.h"

#define NANO_PWM_MEM_SIZE 0x18

#define LED_PWM_DEFAULT_PERIOD_REG_VALUE        0xFFFF

#define PWM_PWMCR_EN_MASK                        (0x1U)

#define NANO_PWM_CR   0x0
#define NANO_PWM_SR   0x4
#define NANO_PWM_IR   0x8
#define NANO_PWM_SAR  0xC
#define NANO_PWM_PR  0x10
#define NANO_PWM_CNR 0x14

#define NANO_STARTBTN_LED_PWM_INDEX 3
#define NANO_RED_LED_PWM_INDEX 4
#define NANO_GREEN_LED_PWM_INDEX 6


static uint64_t nano_pwm_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    NANOPWMState *s = NANOPWM(opaque);

    switch (addr) 
    {
    case NANO_PWM_CR:	
        return s->pwm_cr;

    case NANO_PWM_SR:	
        return s->pwm_sr;

    case NANO_PWM_IR:	
        return s->pwm_ir;

    case NANO_PWM_SAR:
        return s->pwm_sar;

    case NANO_PWM_PR:	
        return s->pwm_pr;

    case NANO_PWM_CNR:
        return s->pwm_cnr;

    default:
        break;
    }
    return 0;
}

static void nano_pwm_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    NANOPWMState *s = NANOPWM(opaque);

    switch (addr) 
    {
    case NANO_PWM_CR:	
        s->pwm_cr = value;
        if(s->pwm_sar != LED_PWM_DEFAULT_PERIOD_REG_VALUE) {
            break;
        }
        switch(s->pwm_index)
        {
        case NANO_STARTBTN_LED_PWM_INDEX:
            updateStartButtonLedStatus(value & PWM_PWMCR_EN_MASK);
            break;

        case NANO_RED_LED_PWM_INDEX:
            updateRGBLedStatus(value & PWM_PWMCR_EN_MASK ? eRed : eOff);
            break;

        case NANO_GREEN_LED_PWM_INDEX:
            updateRGBLedStatus(value & PWM_PWMCR_EN_MASK ? eGreen : eOff);
            break;

        default:
            break;
        }
        if(!(value & PWM_PWMCR_EN_MASK))
        {
            s->pwm_sar = 0;
        }
        
        break;

    case NANO_PWM_SR:	
        s->pwm_sr = value;
        break;

    case NANO_PWM_IR:	
        s->pwm_ir = value;
        break;

    case NANO_PWM_SAR:	
        s->pwm_sar = value;
        break;

    case NANO_PWM_PR:
        s->pwm_pr = value;
        break;

    case NANO_PWM_CNR:
        s->pwm_cnr = value;
        break;

    default:
        break;
    }

}

static const MemoryRegionOps nano_pwm_ops = {
    .read = nano_pwm_read,
    .write = nano_pwm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void nano_pwm_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice* sbd = SYS_BUS_DEVICE(dev);
    NANOPWMState *s = NANOPWM(dev);
    
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->pwm_irq);
}

static void nano_pwm_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = nano_pwm_realize; 
    dc->desc = "nano pwm device";
}

static void nano_pwm_init(Object *obj)
{
    NANOPWMState *s = NANOPWM(obj);
    memory_region_init_io(&s->iomem, obj, &nano_pwm_ops, s, TYPE_NANOPWM, NANO_PWM_MEM_SIZE);
}

static const TypeInfo nano_pwm_info = {
    .name          = TYPE_NANOPWM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NANOPWMState),
    .class_init    = nano_pwm_class_init,
    .instance_init = nano_pwm_init,      
    
};

static void nano_pwm_register_types(void)
{
    type_register_static(&nano_pwm_info);
}

type_init(nano_pwm_register_types)
