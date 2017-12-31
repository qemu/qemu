/*
 * STM32 Microcontroller RTC (Real times and Clock) module
 *
 * Copyright (C) 2016 Hariri Yasser,Fatima zohra LaaHlou
 *
 * Source code based on omap_clk.c
 * Implementation based on ST Microelectronics "RM0008 Reference Manual Rev 10"
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/arm/stm32.h"
#include "hw/arm/stm32_clktree.h"
#include "qemu/bitops.h"
#include <stdio.h>
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "hw/ptimer.h"
/* DEFINITIONS*/

/* See README for DEBUG details. */
//#define DEBUG_STM32_RCC

#ifdef DEBUG_STM32_RTC
#define DPRINTF(fmt, ...)                                       \
    do { printf("STM32_RTC: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...)
#endif


#define RTC_CRH_OFFSET 0x00
#define RTC_CRH_SECIE_BIT 0
#define RTC_CRH_ALRIE_BIT 1
#define RTC_CRH_OWIE_BIT 2

#define RTC_CRL_OFFSET 0x04
#define RTC_CRL_SECF_BIT 0
#define RTC_CRL_ALRF_BIT 1
#define RTC_CRL_OWF_BIT 2
#define RTC_CRL_RSF_BIT 3
#define RTC_CRL_CNF_BIT 4
#define RTC_CRL_RTOFF_BIT 5

#define RTC_PRLH_OFFSET 0x08
#define RTC_PRLL_OFFSET 0x0c
#define RTC_DIVH_OFFSET 0x10
#define RTC_DIVL_OFFSET 0x14
#define RTC_CNTH_OFFSET 0x18
#define RTC_CNTL_OFFSET 0x1c
#define RTC_ALRH_OFFSET 0x20
#define RTC_ALRL_OFFSET 0x24


struct Stm32Rtc {
    /* Inherited */
    SysBusDevice busdev;
    stm32_periph_t periph;

    /* Properties */
    
    void *stm32_rcc_prop;

    /* Private */
    MemoryRegion iomem;
    Stm32Rcc *stm32_rcc;
    /* Register */
    uint16_t
        RTC_CR[2], /* 0 CRL 1 CRH */
        RTC_PRL[2], /* 0 PRLL 1 PRLH */
        RTC_CNT[2], /* 0 CNTL 1 CNTH  */
        RTC_ALR[2]; /* 0 ALRL 1 ALRH */

    ptimer_state    *ptimer;        /* tick timer */
    uint32_t freq,
             prescaler;
    
    qemu_irq irq;
    int curr_irq_level;
};



/* HELPER FUNCTIONS */

/*Function Called if output freq of RTC change*/

static void stm32_rtc_clk_irq_handler(void *opaque, int n, int level)
{

    Stm32Rtc *s=(Stm32Rtc*)opaque;        
    s->freq=stm32_rcc_get_periph_freq(s->stm32_rcc,s->periph);
    if(s->freq){
    ptimer_set_freq(s->ptimer,s->freq);
    ptimer_set_count(s->ptimer,s->prescaler+1);
    ptimer_run(s->ptimer, 1);
    }    
    
}

static void stm32_rtc_update_irq(Stm32Rtc *s) {

     int new_irq_level =
       ((s->RTC_CR[1] >> RTC_CRH_SECIE_BIT) & (s->RTC_CR[0] >> RTC_CRL_SECF_BIT)) |
       ((s->RTC_CR[1] >> RTC_CRH_ALRIE_BIT) & (s->RTC_CR[0] >> RTC_CRL_ALRF_BIT)) |
       ((s->RTC_CR[1] >> RTC_CRH_OWIE_BIT) & (s->RTC_CR[0] >> RTC_CRL_OWF_BIT)); 

    /* Only trigger an interrupt if the IRQ level changes.  We probably could
     * set the level regardless, but we will just check for good measure.
     */
    if((new_irq_level & 0x01) ^ s->curr_irq_level) {
        qemu_set_irq(s->irq, new_irq_level);
        s->curr_irq_level = new_irq_level;
    }
}


static int stm32_rtc_check_alarm(Stm32Rtc* s)
{
   return ((s->RTC_CNT[1] << 16) | (s->RTC_CNT[0])) == 
          ((s->RTC_ALR[1] << 16) | (s->RTC_ALR[0]));
}

/* functon called each cycle of
   f_TR_CLK=RTCCLK/(PRL[19-0]+1)*/
static void stm32_rtc_tick(void *opaque)
{
    Stm32Rtc *s=(Stm32Rtc*)opaque; 
    /* increment count (systeme date) eache cycle of f_TR_CLK   */
    uint32_t new_CNT=((s->RTC_CNT[1] << 16 ) | (s->RTC_CNT[0])) + 1; 

    s->RTC_CNT[0]=new_CNT & 0xffff;
    s->RTC_CNT[1]=new_CNT >> 16;

    /* set second flag each cycle of f_TR_CLK */
    if(s->RTC_CR[1]){
    s->RTC_CR[0]|=(1 << RTC_CRL_SECF_BIT); 
    }
     
    /* set ALR flag if ALR interrupt 
     enabled and ALR event occured  */
   if(s->RTC_CR[1] & (1<<RTC_CRH_ALRIE_BIT)){
    s->RTC_CR[0]|= (stm32_rtc_check_alarm(s)<< RTC_CRL_ALRF_BIT);
    }
   
     /* set syncro bit if equal 0 */
   if(!(s->RTC_CR[0]&(1<<RTC_CRL_RSF_BIT))){
     s->RTC_CR[0]|=(1 << RTC_CRL_RSF_BIT);
    
   }

    stm32_rtc_update_irq(s);
    /*divise frequence clock by (PR[19-0]+ 1)
      see datasheet page 480 prescaler registre */ 
    ptimer_set_count(s->ptimer,s->prescaler+1);
    ptimer_run(s->ptimer, 1);
}


static void stm32_rtc_reset(DeviceState *dev)
{
   Stm32Rtc *s = STM32_Rtc(dev);
   s->RTC_CR[0]=0x0020;
   s->RTC_CR[1]=0x0000;
   s->RTC_PRL[0]=0x8000;
   s->RTC_CNT[0]=0x0000;
   s->RTC_ALR[0]=0xFFFF;
   s->RTC_PRL[1]=0x0000;
   s->RTC_CNT[1]=0x0000;
   s->RTC_ALR[1]=0xFFFF;
   s->prescaler=((s->RTC_PRL[1]&0x000f)<<16)|
                           s->RTC_PRL[0];

}

static uint64_t stm32_rtc_read(void *opaque, hwaddr offset,
                          unsigned size)
{
  

    Stm32Rtc *s = (Stm32Rtc *)opaque;

    switch (offset & 0xffffffff) {

        case RTC_PRLH_OFFSET:
         hw_error("attempted to read\
                   PRLH registre");
            return 0;
        case RTC_PRLL_OFFSET:
         hw_error("attempted to read\
                   PRLL registre");
            return 0;
        case RTC_CRH_OFFSET:
            return s->RTC_CR[1]; 
        case RTC_CRL_OFFSET:
            return s->RTC_CR[0];
        case RTC_DIVH_OFFSET:
            return s->RTC_PRL[1];
        case RTC_DIVL_OFFSET:
            return s->RTC_PRL[0];
        case RTC_CNTH_OFFSET:
            return s->RTC_CNT[1];
        case RTC_CNTL_OFFSET:
            return s->RTC_CNT[0];;
        case RTC_ALRH_OFFSET:
            return s->RTC_ALR[1];
        case RTC_ALRL_OFFSET:
	    return s->RTC_ALR[0];
        default:
            STM32_BAD_REG(offset, size);
            return 0;
    }
}

static void stm32_rtc_write(void *opaque, hwaddr offset,
                       uint64_t value, unsigned size)
{ 


      Stm32Rtc *s = (Stm32Rtc *)opaque;
    /* software can only write in (PRL,ALR,CNT)
       registre if CNF bit is set */

    if(((offset & 0xffffffff)!=RTC_CRH_OFFSET) &&
       ((offset & 0xffffffff)!=RTC_CRL_OFFSET) &&
       !(s->RTC_CR[0]&(1<<RTC_CRL_CNF_BIT))){

          hw_error("you are must enter to configuration \
                    mode for write in any registre");
      }       
     /*ongoing writing operation */
    s->RTC_CR[0]&= ~(1 << RTC_CRL_RTOFF_BIT);
 
    switch (offset & 0xffffffff) {
        case RTC_CRH_OFFSET:
            s->RTC_CR[1]=value & 0xffff;
           break;
        case RTC_CRL_OFFSET:
            /*software can only clear 
             (RSF OWF ALRF SECF) bit,  
             writing 1 has no effect */
            s->RTC_CR[0]&=(value & 0xf) ; 
             /* write CNF bit */
            s->RTC_CR[0]|=(value & 0x10); 
           break;
        case RTC_PRLH_OFFSET:
            s->RTC_PRL[1]=value & 0x000f;
             /* update prescaler if it's changed */
            if(s->prescaler^(((s->RTC_PRL[1]&0x000f)<<16)|
                           s->RTC_PRL[0])){
               s->prescaler=((s->RTC_PRL[1]&0x000f)<<16)|
                           s->RTC_PRL[0];
            }
            
           break;
        case RTC_PRLL_OFFSET:
            s->RTC_PRL[0]=value & 0xffff;
             /* update prescaler if it's changed */
            if(s->prescaler^(((s->RTC_PRL[1]&0x000f)<<16)|
                           s->RTC_PRL[0])){
               s->prescaler=((s->RTC_PRL[1]&0x000f)<<16)|
                           s->RTC_PRL[0];
            }
           break;
        case RTC_DIVH_OFFSET:
            hw_error("attempted to write\
                      in DIVH registre");
           break;
        case RTC_DIVL_OFFSET:
            hw_error("attempted to write\
                      in DIVL registre");
           break;
        case RTC_CNTH_OFFSET:
            s->RTC_CNT[1]=(value & 0xffff);
           break;
        case RTC_CNTL_OFFSET:
            s->RTC_CNT[0]=(value & 0xffff);
	   break;
        case RTC_ALRH_OFFSET:
            s->RTC_ALR[1]=value & 0xffff;
	   break;
        case RTC_ALRL_OFFSET:
	    s->RTC_ALR[0]=value & 0xffff;
	   break;
        default:
            STM32_BAD_REG(offset, size); 
            return;
    }

    /* set RTOFF bit for mark end of write operation */
    s->RTC_CR[0]|= (1 << RTC_CRL_RTOFF_BIT); 

}

static const MemoryRegionOps stm32_rtc_ops = {
    .read = stm32_rtc_read,
    .write = stm32_rtc_write,
    .valid.min_access_size = 2,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN
};



/* DEVICE INITIALIZATION */

static int stm32_rtc_init(SysBusDevice *dev)
{

    qemu_irq *clk_irq;
    Stm32Rtc *s = STM32_Rtc(dev);
    QEMUBH *bh;
    s->stm32_rcc = (Stm32Rcc *)s->stm32_rcc_prop;

    memory_region_init_io(&s->iomem, OBJECT(s), &stm32_rtc_ops, s,
                          "rtc", 0x03ff);
    sysbus_init_mmio(dev, &s->iomem);
    sysbus_init_irq(dev, &s->irq);

    bh = qemu_bh_new(stm32_rtc_tick, s);
    s->ptimer = ptimer_init(bh);
    
    /* Register handlers to handle updates to the RTC's peripheral clock. */
    clk_irq =
          qemu_allocate_irqs(stm32_rtc_clk_irq_handler, (void *)s, 1);
    stm32_rcc_set_periph_clk_irq(s->stm32_rcc, s->periph, clk_irq[0]);
    
    stm32_rtc_reset((DeviceState *)s);

    return 0;
}

static Property stm32_rtc_properties[] = {
    DEFINE_PROP_PERIPH_T("periph", Stm32Rtc, periph, STM32_PERIPH_UNDEFINED),
    DEFINE_PROP_PTR("stm32_rcc", Stm32Rtc, stm32_rcc_prop),
    DEFINE_PROP_END_OF_LIST()
};

static void stm32_rtc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = stm32_rtc_init;
    dc->reset = stm32_rtc_reset;
    dc->props = stm32_rtc_properties;
}

static TypeInfo stm32_rtc_info = {
    .name  = "stm32-rtc",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(Stm32Rtc),
    .class_init = stm32_rtc_class_init
};

static void stm32_rtc_register_types(void)
{
    type_register_static(&stm32_rtc_info);
}

type_init(stm32_rtc_register_types)
