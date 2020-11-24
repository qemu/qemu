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

#ifndef IMX6UL_ADC_H
#define IMX6UL_ADC_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "qemu/timer.h"
#include "hw/adc/adc_samples_simulator.h"

#define TYPE_IMX6UL_ADC "imx6ul.adc"
OBJECT_DECLARE_SIMPLE_TYPE(IMX6ULADCState, IMX6UL_ADC)

/* i.MX ADC memory map */
#define HC_ADDR                     (0x00)  /* Control register */
#define HS_ADDR                     (0x08)  /* Status register */
#define R_ADDR                      (0x0C)  /* Data result register */
#define CFG_ADDR                    (0x14)  /* Configuration register */
#define GC_ADDR                     (0x18)  /* General control register */
#define GS_ADDR                     (0x1C)  /* General status register */
#define CV_ADDR                     (0x20)  /* Compare value register */
#define OFS_ADDR                    (0x24)  /* Offset correction value register */
#define CAL_ADDR                    (0x28)  /* Calibration value register */

#define IMX6UL_ADC_MEM_SIZE         (0x2C)

#define HC_RESET                    (0x0000001F)  /* Control register reset value */
#define HS_RESET                    (0x00000000)  /* Status register reset value */
#define R_RESET                     (0x00000000)  /* Data result register reset value */
#define CFG_RESET                   (0x00000000)  /* Configuration register reset value */
#define GC_RESET                    (0x00000000)  /* General control register reset value */
#define GS_RESET                    (0x00000000)  /* General status register reset value */
#define CV_RESET                    (0x00000000)  /* Compare value register reset value */
#define OFS_RESET                   (0x00000000)  /* Offset correction value register reset value */
#define CAL_RESET                   (0x00000000)  /* Calibration value register reset value */ 

#define HC_MASK                     (0x0000009F)  /* Control register mask*/
#define HC_AIEN_MASK                (0x00000080)  /* Control register interrupt enable mask*/  
#define HC_ADCH_MASK                (0x0000001F)  /* Control register channel select mask*/
#define HS_MASK                     (0x00000001)  /* Status register mask*/
#define HS_COCO_MASK                (0x00000001)  /* Status register Converison complete mask */
#define R_MASK                      (0x00000FFF)  /* Data result register mask*/
#define CFG_MASK                    (0x0001FFFF)  /* Configuration register mask*/
#define CFG_ADTRG_MASK              (0x00002000)  /* Configuration register ADC trigger mask*/
#define CFG_MODE_MASK               (0x0000000C)  /* Configuration register ADC mode mask*/
#define CFG_MODE_SHIFT              (2U)          /* Configuration register ADC mode shift*/
#define GC_MASK                     (0x000000FF)  /* General control register mask*/
#define GC_CAL_MASK                 (0x00000080)  /* General control register calibration mask*/
#define GS_MASK                     (0x00000007)  /* General status register mask*/
#define GS_CALF_MASK                (0x00000002)  /* General status register calibration status mask*/
#define GS_ADACT_MASK               (0x00000001)  /* General status register conversion active mask*/  
#define CV_MASK                     (0x0FFF0FFF)  /* Compare value register mask*/
#define OFS_MASK                    (0x00001FFF)  /* Offset correction value register mask*/
#define CAL_MASK                    (0x0000000F)  /* Calibration value register mask*/ 

#define CONVERSION_TIMER_MS         (10)          // Conversion will be checked every 10 ms, this is aribitrary and independent of adc settings

typedef enum ADCMode {
    ADC_8_BIT_MODE,
    ADC_10_BIT_MODE,
    ADC_12_BIT_MODE
} ADCMode;

struct IMX6ULADCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;

    ADCSAMPLESIMState * p_adc_simulator;
    QEMUTimer * conversion_timer;

    uint32_t hc;                                /**< Control register, array offset: 0x0, array step: 0x4 */
    uint32_t hs;                                /**< Status register, offset: 0x8 */
    uint32_t r;                                 /**< Data result register, array offset: 0xC, array step: 0x4 */
    uint32_t cfg;                               /**< Configuration register, offset: 0x14 */
    uint32_t gc;                                /**< General control register, offset: 0x18 */
    uint32_t gs;                                /**< General status register, offset: 0x1C */
    uint32_t cv;                                /**< Compare value register, offset: 0x20 */
    uint32_t ofs;                               /**< Offset correction value register, offset: 0x24 */
    uint32_t cal;                               /**< Calibration value register, offset: 0x28 */
};

#endif /* IMX6UL_ADC_H */
