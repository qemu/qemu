/*
 * STM32 Microcontroller DAC module
 *
 * Copyright (C) 2016 Hariri Yasser,Fatima zohra Lahlou
 *
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

#include "hw/sysbus.h"
#include "hw/arm/stm32.h"
#include "sysemu/char.h"
#include "qemu/bitops.h"
#include <math.h>       // for the sine wave generation
#include <inttypes.h>

/* DEFINITIONS*/

#ifdef DEBUG_STM32_DAC
#define DPRINTF(fmt, ...)                                       \
    do { fprintf(stderr, "STM32_DAC: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...)
#endif

#define DAC_CR_OFFSET 0x00
#define DAC_CR_WAVE1_MASK    0x000000C0
#define DAC_CR_WAVE2_MASK    0x00C00000 
#define DAC_CR_WAVE1_START   6
#define DAC_CR_WAVE2_START   22
#define DAC_CR_MAMP1_MASK    0x00000f00
#define DAC_CR_MAMP2_MASK    0x0f000000 
#define DAC_CR_MAMP1_START   8
#define DAC_CR_MAMP2_START   24
#define DAC_CR_TEN1_BIT   2
#define DAC_CR_TEN2_BIT   18
#define DAC_CR_TSEL1_START   3
#define DAC_CR_TSEL2_START   19
#define DAC_SWTRIGR1_MASK  0x00000001
#define DAC_SWTRIGR2_MASK  0x00000002
#define DAC_SWTRIGR_OFFSET 0x04
#define DAC_DHR12R1_OFFSET 0x08
#define DAC_DHR12L1_OFFSET 0x0c
#define DAC_DHR8R1_OFFSET 0x10
#define DAC_DHR12R2_OFFSET 0x14
#define DAC_DHR12L2_OFFSET 0x18
#define DAC_DHR8R2_OFFSET 0x1c
#define DAC_DHR12RD_OFFSET 0x20
#define DAC_DHR12LD_OFFSET 0x24
#define DAC_DHR8RD_OFFSET 0x28
#define DAC_DOR1_OFFSET 0x2c
#define DAC_DOR2_OFFSET 0x30


struct Stm32Dac {
    /* Inherited */
    SysBusDevice busdev;

    /* Properties */
    stm32_periph_t periph;
    void *stm32_rcc_prop;
    void *stm32_gpio_prop;
    void *stm32_afio_prop;

    /* Private */
    MemoryRegion iomem;

    Stm32Rcc *stm32_rcc;
    Stm32Gpio **stm32_gpio;

    /* nano sec per cycle 
       of APB1 Clock   */
    int64_t ns_per_cycle;

    /* Register Values */
    uint32_t
	DAC_CR,
 	DAC_SWTRIGR,
	DAC_DOR1,
	DAC_DOR2,
        DAC_DHR12R1,
        DAC_DHR12L1,
        DAC_DHR8R1,
        DAC_DHR12R2,
        DAC_DHR12L2,
        DAC_DHR8R2,
        DAC_DHR12RD,
        DAC_DHR12LD,
        DAC_DHR8RD;
    
    /* LFSR VALUE */
    uint16_t
        LFSR_VALUE,
        DACC1_DHR,
        DACC2_DHR,
        TRI_CNT1,   /* triangle counter */
        TRI_CNT2;
    struct QEMUTimer *DOR1_timer;
    struct QEMUTimer *DOR2_timer;
    struct QEMUTimer *TRI_CNT1_timer;
    struct QEMUTimer *TRI_CNT2_timer;
    struct QEMUTimer *CONV1_timer;
    struct QEMUTimer *CONV2_timer;
    struct QEMUTimer *LFSR_timer;


    bool inc_cnt1;
    bool inc_cnt2;
    int Vref; //mv
};


/* Handle a change in the peripheral clock. */
static void stm32_dac_clk_irq_handler(void *opaque, int n, int level)
{
   Stm32Dac *s=(Stm32Dac *)opaque;    

   uint32_t clk_freq = stm32_rcc_get_periph_freq(s->stm32_rcc, s->periph);
   s->ns_per_cycle=1000000000LL/clk_freq;
}

static void stm32_dac_LFSR_update(void *opaque)
{
     Stm32Dac *s=(Stm32Dac *)opaque;    
     int nor=extract32(s->LFSR_VALUE,0,1);
     int xor,i;
     /* Calculate nor between bits of LFSR */
     for(i=1;i<12;i++)
     nor=!(nor | extract32(s->LFSR_VALUE,i,1));
     /* Calculate new 11 bits of LFSR */
     xor =extract32(s->LFSR_VALUE,0,1)^
          extract32(s->LFSR_VALUE,1,1)^
          extract32(s->LFSR_VALUE,4,1)^
          extract32(s->LFSR_VALUE,6,1)^
          nor;
     /* Update LFSR_Value */
     s->LFSR_VALUE =(s->LFSR_VALUE >> 1) |
                    (xor<<11);
    
}

static void stm32_dac_triangular_cnt1_update(void *opaque)
{
    Stm32Dac *s=(Stm32Dac *)opaque;
    uint16_t max_amplitude,MAMP1;
    MAMP1=extract32(s->DAC_CR,DAC_CR_MAMP1_START,4); 
    MAMP1= (MAMP1>11) ? 11 :MAMP1;
    max_amplitude =(1 << (MAMP1+1))-1;

    if(s->inc_cnt1)
       s->TRI_CNT1++;
    else 
       s->TRI_CNT1-- ;

    if(s->TRI_CNT1>=max_amplitude)
       s->inc_cnt1=false;
 
    if(s->TRI_CNT1<=0)
       s->inc_cnt1=true;
     
}

static void stm32_dac_triangular_cnt2_update(void *opaque)
{
    Stm32Dac *s=(Stm32Dac *)opaque;
    uint16_t max_amplitude,MAMP2;
    MAMP2=extract32(s->DAC_CR,DAC_CR_MAMP2_START,4); 
    MAMP2= (MAMP2>11) ? 11 :MAMP2;
    max_amplitude =(1 << (MAMP2+1))-1;
    
    if(s->inc_cnt2)
       s->TRI_CNT2++;
    else 
       s->TRI_CNT2-- ;

    if(s->TRI_CNT2>=max_amplitude)
       s->inc_cnt2=false;
 
    if(s->TRI_CNT2<=0)
       s->inc_cnt2=true;
     
}

static void stm32_dac_load_DOR1_registre(void *opaque) 
{
   Stm32Dac *s=(Stm32Dac *)opaque;
   uint32_t WAVE1,MAMP1,MASK_LFSR;
   uint64_t curr_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
   
   s->DAC_DOR1=s->DACC1_DHR;

   if(extract32(s->DAC_CR,DAC_CR_TEN1_BIT,1) &&
      extract32(s->DAC_SWTRIGR,0,1))
   {
      WAVE1=extract32(s->DAC_CR,DAC_CR_WAVE1_START,2);
      MAMP1=extract32(s->DAC_CR,DAC_CR_MAMP1_START,4); 
      MAMP1= (MAMP1>11) ? 11 :MAMP1;

      /* Triangular generation 
         check (WAVE1=1x) */
      if(WAVE1>1)
      {
        s->DAC_DOR1+=s->TRI_CNT1;
        /*internal triangular counter 1 
       is incremented three APB1 clock 
       cycles after each trigger event*/
        timer_mod(s->TRI_CNT1_timer, curr_time + 3*s->ns_per_cycle);
      }
      /* noise generation */
      if(WAVE1==1)
      {
        /* MASK for LFSR */
        MASK_LFSR= (1 << (MAMP1+1))-1;
        s->DAC_DOR1+=(s->LFSR_VALUE & MASK_LFSR);  
        /* LFSR registre is updated three APB1
          clock cycles after each trigger event */
        timer_mod(s->LFSR_timer, curr_time + 3*s->ns_per_cycle);       
      }   
        /* clear SWTRIG1 ==>
         software triger1 disabled */
      s->DAC_SWTRIGR= (s->DAC_SWTRIGR & (~DAC_SWTRIGR1_MASK));        
   }  

   /* When DAC_DOR1 is loaded with the DAC_DHR1 
      contents,the analog output voltage 
      becomes available after a time of 
      t SETTLING,generaly equal three cycles */

   timer_mod(s->CONV1_timer, curr_time + 3*s->ns_per_cycle);
 
}

static void stm32_dac_load_DOR2_registre(void *opaque) 
{
   Stm32Dac *s=(Stm32Dac *)opaque;
   uint32_t WAVE2,MAMP2,MASK_LFSR;
   uint64_t curr_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
   s->DAC_DOR2=s->DACC2_DHR;

   if(extract32(s->DAC_CR,DAC_CR_TEN2_BIT,1) &&
      extract32(s->DAC_SWTRIGR,1,1))
   {
      WAVE2=extract32(s->DAC_CR,DAC_CR_WAVE2_START,2);
      MAMP2=extract32(s->DAC_CR,DAC_CR_MAMP2_START,4);
      MAMP2= (MAMP2>11) ? 11 :MAMP2;
      /* Triangular generation 
         check (WAVE2=1x) */
      if(WAVE2>1)
      {
       s->DAC_DOR2+=s->TRI_CNT2;
       /*internal triangular counter 2 
       is incremented three APB1 clock 
       cycles after each trigger event*/
       timer_mod(s->TRI_CNT2_timer, curr_time + 3*s->ns_per_cycle);
      }
      
      /* noise generation */
      if(WAVE2==1)
      {
       /* MASK for LFSR */
        MASK_LFSR= (1 << (MAMP2+1))-1;
        s->DAC_DOR2+=(s->LFSR_VALUE & MASK_LFSR);
       /*LFSR registre is updated three APB1 
       clock cycles after each trigger event*/
        timer_mod(s->LFSR_timer, curr_time + 3*s->ns_per_cycle);
      }  
       /* clear SWTRIG2 ==>
       software triger2 disabled */
       s->DAC_SWTRIGR= (s->DAC_SWTRIGR & (~DAC_SWTRIGR2_MASK));          
   }  

       /* When DAC_DORx is loaded with the
       DAC_DHRx contents,the analog output 
       voltage becomes available after a time of 
       t SETTLING,generaly its equal three cycles */

   timer_mod(s->CONV2_timer, curr_time + 3*s->ns_per_cycle);
}


static void stm32_dac_write_DACC1_DHR(Stm32Dac *s,uint32_t value) 
{

   uint64_t curr_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
   s->DACC1_DHR=value;

    /* if DAC channel1 trigger disabled 
       data written into DACC2_DHR register is 
       transferred one APB1 clock cycle 
       later to the DAC_DOR1 register */

   if(!extract32(s->DAC_CR,DAC_CR_TEN1_BIT,1))
    timer_mod(s->DOR1_timer, curr_time + s->ns_per_cycle);
    
}

static void stm32_dac_write_DACC2_DHR(Stm32Dac *s,uint32_t value) 
{

   uint64_t curr_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
   s->DACC2_DHR=value;

    /* if DAC channel2 trigger disabled 
       data written into DACC2_DHR register is 
       transferred one APB1 clock cycle 
       later to the DAC_DOR2 register */

   if(!extract32(s->DAC_CR,DAC_CR_TEN2_BIT,1))
    timer_mod(s->DOR2_timer, curr_time + s->ns_per_cycle);
   
}

static void stm32_dac_write_DAC_SWTRIGR(Stm32Dac *s,uint32_t value) 
{
   uint64_t curr_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
   s->DAC_SWTRIGR=(value & 3);

   /* if software trigger x occured 
      DAC_DORx is loaded after one
      APB1 clock cycle */

   if(value & DAC_SWTRIGR1_MASK)
     timer_mod(s->DOR1_timer, curr_time + s->ns_per_cycle);

   if(value & DAC_SWTRIGR2_MASK)
     timer_mod(s->DOR2_timer, curr_time + s->ns_per_cycle);
   
}

static void stm32_dac_check_pin(Stm32Dac *s,int pin)
{
    Stm32Gpio *gpio_dev = s->stm32_gpio[STM32_GPIO_INDEX_FROM_PERIPH(STM32_GPIOA)];

    if(stm32_gpio_get_mode_bits(gpio_dev, pin) != STM32_GPIO_MODE_IN) 
       hw_error("GPIOA pin %d needs to be configured as "
                  "input",pin);
    if(stm32_gpio_get_config_bits(gpio_dev, pin)!= STM32_GPIO_IN_ANALOG) 
       hw_error("GPIOA pin %d needs to be configured as "
                 "analog input",pin);
}

static void stm32_dac_conv_DACC1(void *opaque) 
{
   Stm32Dac *s=(Stm32Dac *)opaque;
   stm32_dac_check_pin(s,4);

   // TODO: add a `-device dac` option to qemu which allows qemu's full range of I/O redirection options
   //       just writing to a file *in the current directory* is a quick hack that's not production-ready.

   // TODO: factor this with stm32_dav_conv_DACC2
   printf("DAC1output:%d\n",(s->Vref*(s->DAC_DOR1 & 0xfff))/4095);
   FILE* fichier=fopen("DAC_OUT_PUT1.txt", "a");
   fprintf(fichier, "%d\n",(s->Vref*(s->DAC_DOR1 & 0xfff))/4095);
   fclose(fichier);
   
}


static void stm32_dac_conv_DACC2(void *opaque) 
{

   Stm32Dac *s=(Stm32Dac *)opaque;
   stm32_dac_check_pin(s,5);

   // TODO: add a `-device dac` option to qemu which allows qemu's full range of I/O redirection options
   //       just writing to a file *in the current directory* is a quick hack that's not production-ready.

   // TODO: factor this with stm32_dav_conv_DACC1
   printf("DAC2output:%d\n",(s->Vref*(s->DAC_DOR2 & 0xfff))/4095);
   FILE* fichier=fopen("DAC_OUT_PUT2.txt", "a");
   fprintf(fichier, "%d\n",(s->Vref*(s->DAC_DOR2 & 0xfff))/4095);
   fclose(fichier);
}


static void stm32_dac_reset(DeviceState *dev)
{
   Stm32Dac *s = STM32_Dac(dev);
   s->LFSR_VALUE=0xAAA;
   s->Vref=2400;
   s->inc_cnt2=true;
   s->inc_cnt1=true;
   FILE* fichier=fopen("DAC_OUT_PUT1.txt", "w");
   fprintf(fichier, "****DAC_OUT_PUT1 : Result of conversion DAC channel 1****\n");
   fclose(fichier);
   fichier=fopen("DAC_OUT_PUT2.txt", "w");
   fprintf(fichier, "****DAC_OUT_PUT2 : Result of conversion DAC channel 2****\n");
   fclose(fichier);

}

static uint64_t stm32_dac_read(void *opaque, hwaddr offset,
                          unsigned size)
{
  
    Stm32Dac *s = (Stm32Dac *)opaque;

    switch (offset & 0xffffffff) {

       
        case DAC_CR_OFFSET:
            return s->DAC_CR; 
        case DAC_SWTRIGR_OFFSET:
            return s->DAC_SWTRIGR;
        case DAC_DHR12R1_OFFSET:
            return s->DAC_DHR12R1;
        case DAC_DHR12L1_OFFSET:
            return s->DAC_DHR12L1;
        case DAC_DHR8R1_OFFSET:
            return s->DAC_DHR8R1;
        case DAC_DHR12R2_OFFSET:
            return s->DAC_DHR12R2;
        case DAC_DHR12L2_OFFSET:
            return s->DAC_DHR12L2;
        case DAC_DHR8R2_OFFSET:
	    return s->DAC_DHR8R2;
        case DAC_DHR12RD_OFFSET:
            return s->DAC_DHR12RD;
        case DAC_DHR12LD_OFFSET:
            return s->DAC_DHR12LD;
        case DAC_DHR8RD_OFFSET:
            return s->DAC_DHR8RD;
        case DAC_DOR1_OFFSET:
	    return s->DAC_DOR1;
        case DAC_DOR2_OFFSET:
	    return s->DAC_DOR2;

        default:
            STM32_BAD_REG(offset, size);
            return 0;
    }
}

static void stm32_dac_write(void *opaque, hwaddr offset,
                       uint64_t value, unsigned size)
{ 

      Stm32Dac *s = (Stm32Dac *)opaque;

    switch (offset & 0xffffffff) {
       
        case DAC_CR_OFFSET:
           s->DAC_CR=value; 
           if(((s->DAC_CR >> DAC_CR_TSEL1_START) & 0x7)!=0 &&
              ((s->DAC_CR >> DAC_CR_TSEL1_START) & 0x7)!=0x7)
            hw_error("software triger is only supported \n");

           if(((s->DAC_CR >> DAC_CR_TSEL2_START) & 0x7)!=0 &&
              ((s->DAC_CR >> DAC_CR_TSEL2_START) & 0x7)!=0x7)
            hw_error("software triger is only supported \n");
           
           break; 
        case DAC_SWTRIGR_OFFSET:
           stm32_dac_write_DAC_SWTRIGR(s,value);
           break; 
        case DAC_DHR12R1_OFFSET:
           s->DAC_DHR12R1=value;
           stm32_dac_write_DACC1_DHR(s,s->DAC_DHR12R1 & 0xfff);
           break; 
        case DAC_DHR12L1_OFFSET:
           s->DAC_DHR12L1=value; 
           stm32_dac_write_DACC1_DHR(s,(s->DAC_DHR12L1 >> 4) & 0xfff);
           break; 
        case DAC_DHR8R1_OFFSET:
           s->DAC_DHR8R1=value;
           stm32_dac_write_DACC1_DHR(s,s->DAC_DHR8R1 & 0xff);
           break; 
        case DAC_DHR12R2_OFFSET:
           s->DAC_DHR12R2=value;
           stm32_dac_write_DACC2_DHR(s,s->DAC_DHR12R2 & 0xfff);
           break; 
        case DAC_DHR12L2_OFFSET:
           s->DAC_DHR12L2=value; 
           stm32_dac_write_DACC2_DHR(s,(s->DAC_DHR12L2 >> 4) & 0xfff);
           break; 
        case DAC_DHR8R2_OFFSET:
	   s->DAC_DHR8R2=value;
           stm32_dac_write_DACC2_DHR(s,s->DAC_DHR8R2 & 0xff);
           break; 
        case DAC_DHR12RD_OFFSET:
           s->DAC_DHR12RD=value;
           stm32_dac_write_DACC1_DHR(s,s->DAC_DHR12RD & 0xfff);
           stm32_dac_write_DACC2_DHR(s,(s->DAC_DHR12RD >> 16) & 0xfff);
           break; 
        case DAC_DHR12LD_OFFSET:
           s->DAC_DHR12LD=value;
           stm32_dac_write_DACC1_DHR(s,(s->DAC_DHR12LD >> 4) & 0xfff);
           stm32_dac_write_DACC2_DHR(s,(s->DAC_DHR12LD >> 20) & 0xfff);
           break; 
        case DAC_DHR8RD_OFFSET:
           s->DAC_DHR8RD=value;
           stm32_dac_write_DACC1_DHR(s,s->DAC_DHR8RD & 0xff);
           stm32_dac_write_DACC2_DHR(s,(s->DAC_DHR8RD >> 8) & 0xff);
           break; 
        case DAC_DOR1_OFFSET:
	   hw_error("Software attempted to read DOR1 Registre \n");
           break; 
        case DAC_DOR2_OFFSET:
	   hw_error("Software attempted to read DOR2 Registre \n");
           break; 

        default:
            STM32_BAD_REG(offset, size);
            return;
    }
  
}

static const MemoryRegionOps stm32_dac_ops = {
    .read = stm32_dac_read,
    .write = stm32_dac_write,
    .valid.min_access_size = 2,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN
};



/* DEVICE INITIALIZATION */

static int stm32_dac_init(SysBusDevice *dev)
{

    qemu_irq *clk_irq;
    Stm32Dac *s = STM32_Dac(dev);
    s->stm32_rcc = (Stm32Rcc *)s->stm32_rcc_prop;
    s->stm32_gpio = (Stm32Gpio **)s->stm32_gpio_prop;

    memory_region_init_io(&s->iomem, OBJECT(s), &stm32_dac_ops, s,
                          "dac", 0x03ff);
    sysbus_init_mmio(dev, &s->iomem);

    
    s->DOR1_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, 
                    (QEMUTimerCB *)stm32_dac_load_DOR1_registre, s);
    s->DOR2_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, 
                    (QEMUTimerCB *)stm32_dac_load_DOR2_registre, s);
    s->TRI_CNT1_timer =timer_new_ns(QEMU_CLOCK_VIRTUAL, 
                    (QEMUTimerCB *)stm32_dac_triangular_cnt1_update, s);
    s->TRI_CNT2_timer =timer_new_ns(QEMU_CLOCK_VIRTUAL, 
                    (QEMUTimerCB *)stm32_dac_triangular_cnt2_update, s);
    s->CONV1_timer =timer_new_ns(QEMU_CLOCK_VIRTUAL, 
                    (QEMUTimerCB *) stm32_dac_conv_DACC1, s);
    s->CONV2_timer =timer_new_ns(QEMU_CLOCK_VIRTUAL, 
                    (QEMUTimerCB *) stm32_dac_conv_DACC2, s);
    s->LFSR_timer =timer_new_ns(QEMU_CLOCK_VIRTUAL, 
                    (QEMUTimerCB *) stm32_dac_LFSR_update, s);
   
    /* Register handlers to handle updates to the RTC's peripheral clock. */
    clk_irq =
          qemu_allocate_irqs(stm32_dac_clk_irq_handler, (void *)s, 1);
    stm32_rcc_set_periph_clk_irq(s->stm32_rcc, s->periph, clk_irq[0]);
    
    stm32_dac_reset((DeviceState *)s);

    return 0;
}

static Property stm32_dac_properties[] = {
    DEFINE_PROP_PERIPH_T("periph", Stm32Dac, periph, STM32_PERIPH_UNDEFINED),
    DEFINE_PROP_PTR("stm32_rcc", Stm32Dac, stm32_rcc_prop),
    DEFINE_PROP_PTR("stm32_gpio", Stm32Dac, stm32_gpio_prop),
    DEFINE_PROP_END_OF_LIST()
};

static void stm32_dac_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = stm32_dac_init;
    dc->reset = stm32_dac_reset;
    dc->props = stm32_dac_properties;
}

static TypeInfo stm32_dac_info = {
    .name  = "stm32-dac",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(Stm32Dac),
    .class_init = stm32_dac_class_init
};

static void stm32_dac_register_types(void)
{
    type_register_static(&stm32_dac_info);
}

type_init(stm32_dac_register_types)
