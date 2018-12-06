/*
 * STM32 Microcontroller ADC module
 *
 * Copyright (C) 2010 Jean-Michel Friedt
 *
 * Source code based on pl011.c
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

#ifdef DEBUG_STM32_ADC
#define DPRINTF(fmt, ...)                                       \
    do { fprintf(stderr, "STM32_ADC: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...)
#endif

// libopencm3/cm3/common.h
#define MMIO32(addr)            (*(volatile uint32_t *)(addr))

// libopencm3/stm32/f1/memorymap.h
#define PERIPH_BASE                     (0x40000000U)
#define PERIPH_BASE_APB2                (PERIPH_BASE + 0x10000)
#define ADC1_BASE                       (PERIPH_BASE_APB2 + 0x2400)
#define ADC2_BASE                       (PERIPH_BASE_APB2 + 0x2800)
#define ADC3_BASE                       (PERIPH_BASE_APB2 + 0x3c00)

// ~/sat/arm-none-eabi/include/libopencm3/stm32/f1/adc.h

#define ADC1                            0 // ADC1_BASE
#define oADC_SR                    0x00
#define oADC_CR1                   0x04
#define oADC_CR2                   0x08
#define oADC_SMPR1                 0x0c
#define oADC_SMPR2                 0x10

/* ADC injected channel data offset register x (ADC_JOFRx) (x=1..4) */
#define oADC_JOFR1		 0x14
#define oADC_JOFR2		 0x18
#define oADC_JOFR3		 0x1c
#define oADC_JOFR4		 0x20

/* ADC watchdog high threshold register (ADC_HTR) */
#define oADC_HTR			 0x24

/* ADC watchdog low threshold register (ADC_LTR) */
#define oADC_LTR			 0x28

/* ADC regular sequence register 1 (ADC_SQR1) */
#define oADC_SQR1			 0x2c

/* ADC regular sequence register 2 (ADC_SQR2) */
#define oADC_SQR2			 0x30

/* ADC regular sequence register 3 (ADC_SQR3) */
#define oADC_SQR3			 0x34

/* ADC injected sequence register (ADC_JSQR) */
#define oADC_JSQR			 0x38

/* ADC injected data register x (ADC_JDRx) (x=1..4) */
#define oADC_JDR1			 0x3c
#define oADC_JDR2			 0x40
#define oADC_JDR3			 0x44
#define oADC_JDR4			 0x48

/* ADC regular data register (ADC_DR) */
#define oADC_DR			 0x4c
/* --- ADC Channels ------------------------------------------------------- */

#define ADC_CHANNEL0		0x00
#define ADC_CHANNEL1		0x01
#define ADC_CHANNEL2		0x02
#define ADC_CHANNEL3		0x03
#define ADC_CHANNEL4		0x04
#define ADC_CHANNEL5		0x05
#define ADC_CHANNEL6		0x06
#define ADC_CHANNEL7		0x07
#define ADC_CHANNEL8		0x08
#define ADC_CHANNEL9            0x09
#define ADC_CHANNEL10           0x0A
#define ADC_CHANNEL11           0x0B
#define ADC_CHANNEL12           0x0C
#define ADC_CHANNEL13           0x0D
#define ADC_CHANNEL14           0x0E
#define ADC_CHANNEL15           0x0F
#define ADC_CHANNEL16           0x10
#define ADC_CHANNEL17           0x11
#define ADC_CHANNEL18           0x12

#define ADC_CHANNEL_MASK        0x1F

/* --- ADC_SR values ------------------------------------------------------- */

#define ADC_SR_STRT                     (1 << 4)
#define ADC_SR_JSTRT                    (1 << 3)
#define ADC_SR_JEOC                     (1 << 2)
#define ADC_SR_EOC                      (1 << 1)
#define ADC_SR_AWD                      (1 << 0)

/* --- ADC_CR1 values ------------------------------------------------------ */

/* AWDEN: Analog watchdog enable on regular channels */
#define ADC_CR1_AWDEN                   (1 << 23)

/* JAWDEN: Analog watchdog enable on injected channels */
#define ADC_CR1_JAWDEN                  (1 << 22)

/* Note: Bits [21:20] are reserved, and must be kept at reset value. */


/* DISCNUM[2:0]: Discontinuous mode channel count. */
/****************************************************************************/
/** @defgroup adc_cr1_discnum ADC Number of channels in discontinuous mode.
@ingroup STM32_adc_defines

@{*/
#define ADC_CR1_DISCNUM_1CHANNELS       (0x0 << 13)
#define ADC_CR1_DISCNUM_2CHANNELS       (0x1 << 13)
#define ADC_CR1_DISCNUM_3CHANNELS       (0x2 << 13)
#define ADC_CR1_DISCNUM_4CHANNELS       (0x3 << 13)
#define ADC_CR1_DISCNUM_5CHANNELS       (0x4 << 13)
#define ADC_CR1_DISCNUM_6CHANNELS       (0x5 << 13)
#define ADC_CR1_DISCNUM_7CHANNELS       (0x6 << 13)
#define ADC_CR1_DISCNUM_8CHANNELS       (0x7 << 13)
/**@}*/
#define ADC_CR1_DISCNUM_MASK            (0x7 << 13)
#define ADC_CR1_DISCNUM_SHIFT           13

/* JDISCEN: */ /** Discontinuous mode on injected channels. */
#define ADC_CR1_JDISCEN                 (1 << 12)

/* DISCEN: */ /** Discontinuous mode on regular channels. */
#define ADC_CR1_DISCEN                  (1 << 11)

/* JAUTO: */ /** Automatic Injection Group conversion. */
#define ADC_CR1_JAUTO                   (1 << 10)

/* AWDSGL: */ /** Enable the watchdog on a single channel in scan mode. */
#define ADC_CR1_AWDSGL                  (1 << 9)

/* SCAN: */ /** Scan mode. */
#define ADC_CR1_SCAN                    (1 << 8)

/* JEOCIE: */ /** Interrupt enable for injected channels. */
#define ADC_CR1_JEOCIE                  (1 << 7)

/* AWDIE: */ /** Analog watchdog interrupt enable. */
#define ADC_CR1_AWDIE                   (1 << 6)

/* EOCIE: */ /** Interrupt enable EOC. */
#define ADC_CR1_EOCIE                   (1 << 5)

/* AWDCH[4:0]: Analog watchdog channel bits. (Up to 17 other values reserved) */
/* Notes: Depending on part, and ADC peripheral, some channels are connected
 * to V_SS, or to temperature/reference/battery inputs
 */
/****************************************************************************/
/* ADC_CR1 AWDCH[4:0] ADC watchdog channel */
/** @defgroup adc_watchdog_channel ADC watchdog channel
@ingroup STM32xx_adc_defines

@{*/
#define ADC_CR1_AWDCH_CHANNEL0          (0x00 << 0)
#define ADC_CR1_AWDCH_CHANNEL1          (0x01 << 0)
#define ADC_CR1_AWDCH_CHANNEL2          (0x02 << 0)
#define ADC_CR1_AWDCH_CHANNEL3          (0x03 << 0)
#define ADC_CR1_AWDCH_CHANNEL4          (0x04 << 0)
#define ADC_CR1_AWDCH_CHANNEL5          (0x05 << 0)
#define ADC_CR1_AWDCH_CHANNEL6          (0x06 << 0)
#define ADC_CR1_AWDCH_CHANNEL7          (0x07 << 0)
#define ADC_CR1_AWDCH_CHANNEL8          (0x08 << 0)
#define ADC_CR1_AWDCH_CHANNEL9          (0x09 << 0)
#define ADC_CR1_AWDCH_CHANNEL10         (0x0A << 0)
#define ADC_CR1_AWDCH_CHANNEL11         (0x0B << 0)
#define ADC_CR1_AWDCH_CHANNEL12         (0x0C << 0)
#define ADC_CR1_AWDCH_CHANNEL13         (0x0D << 0)
#define ADC_CR1_AWDCH_CHANNEL14         (0x0E << 0)
#define ADC_CR1_AWDCH_CHANNEL15         (0x0F << 0)
#define ADC_CR1_AWDCH_CHANNEL16         (0x10 << 0)
#define ADC_CR1_AWDCH_CHANNEL17         (0x11 << 0)
/**@}*/
#define ADC_CR1_AWDCH_MASK              (0x1F << 0)
#define ADC_CR1_AWDCH_SHIFT             0

/* --- ADC_CR2 values ------------------------------------------------------ */

/* ALIGN: Data alignement. */
#define ADC_CR2_ALIGN_RIGHT             (0 << 11)
#define ADC_CR2_ALIGN_LEFT              (1 << 11)
#define ADC_CR2_ALIGN                   (1 << 11)

/* DMA: Direct memory access mode. (ADC1 and ADC3 only!) */
#define ADC_CR2_DMA                     (1 << 8)

/* CONT: Continous conversion. */
#define ADC_CR2_CONT                    (1 << 1)

/* ADON: A/D converter On/Off. */
/* Note: If any other bit in this register apart from ADON is changed at the
 * same time, then conversion is not triggered. This is to prevent triggering
 * an erroneous conversion.
 * Conclusion: Must be separately written.
 */
#define ADC_CR2_ADON                    (1 << 0)

/* --- ADC_JOFRx, ADC_HTR, ADC_LTR values ---------------------------------- */

#define ADC_JOFFSET_LSB                 0
#define ADC_JOFFSET_MSK                 0xfff
#define ADC_HT_LSB                      0
#define ADC_HT_MSK                      0xfff
#define ADC_LT_LSB                      0
#define ADC_LT_MSK                      0xfff

/* --- ADC_SQR1 values ----------------------------------------------------- */
/* The sequence length field is always in the same place, but sized
 * differently on various parts */
#define ADC_SQR1_L_LSB                  20

/* --- ADC_JSQR values ----------------------------------------------------- */
#define ADC_JSQR_JL_LSB                 20
#define ADC_JSQR_JSQ4_LSB               15
#define ADC_JSQR_JSQ3_LSB               10
#define ADC_JSQR_JSQ2_LSB               5
#define ADC_JSQR_JSQ1_LSB               0

/* JL[2:0]: Discontinous mode channel count injected channels. */
/****************************************************************************/
/** @defgroup adc_jsqr_jl ADC Number of channels in discontinuous injected mode
@ingroup STM32xx_adc_defines

@{*/
#define ADC_JSQR_JL_1CHANNELS       (0x0 << ADC_JSQR_JL_LSB)
#define ADC_JSQR_JL_2CHANNELS       (0x1 << ADC_JSQR_JL_LSB)
#define ADC_JSQR_JL_3CHANNELS       (0x2 << ADC_JSQR_JL_LSB)
#define ADC_JSQR_JL_4CHANNELS       (0x3 << ADC_JSQR_JL_LSB)
/**@}*/
#define ADC_JSQR_JL_MSK                 (0x2 << ADC_JSQR_JL_LSB)
#define ADC_JSQR_JSQ4_MSK               (0x1f << ADC_JSQR_JSQ4_LSB)
#define ADC_JSQR_JSQ3_MSK               (0x1f << ADC_JSQR_JSQ3_LSB)
#define ADC_JSQR_JSQ2_MSK               (0x1f << ADC_JSQR_JSQ2_LSB)
#define ADC_JSQR_JSQ1_MSK               (0x1f << ADC_JSQR_JSQ1_LSB)

#define ADC_JSQR_JSQ_VAL(n, val)	((val) << (((n) - 1) * 5))
#define ADC_JSQR_JL_VAL(val)		(((val) - 1) << ADC_JSQR_JL_LSB)

// #include <libopencm3/stm32/common/adc_common_v1.h>
/* --- ADC Channels ------------------------------------------------------- */
#define ADC_CHANNEL_TEMP        ADC_CHANNEL16
#define ADC_CHANNEL_VREFINT     ADC_CHANNEL17


/* --- ADC_CR1 values ------------------------------------------------------ */

/* Note: Bits [21:20] are reserved, and must be kept at reset value. */

/* DUALMOD[3:0]: Dual mode selection. (ADC1 only) */
/* Legend:
 * IND: Independent mode.
 * CRSISM: Combined regular simultaneous + injected simultaneous mode.
 * CRSATM: Combined regular simultaneous + alternate trigger mode.
 * CISFIM: Combined injected simultaneous + fast interleaved mode.
 * CISSIM: Combined injected simultaneous + slow interleaved mode.
 * ISM: Injected simultaneous mode only.
 * RSM: Regular simultaneous mode only.
 * FIM: Fast interleaved mode only.
 * SIM: Slow interleaved mode only.
 * ATM: Alternate trigger mode only.
 */
/****************************************************************************/
/* ADC_CR1 DUALMOD[3:0] ADC Mode Selection */
/** @defgroup adc_cr1_dualmod ADC Mode Selection
@ingroup adc_defines

@{*/
/** Independent (non-dual) mode */
#define ADC_CR1_DUALMOD_IND             (0x0 << 16)
/** Combined regular simultaneous + injected simultaneous mode. */
#define ADC_CR1_DUALMOD_CRSISM          (0x1 << 16)
/** Combined regular simultaneous + alternate trigger mode. */
#define ADC_CR1_DUALMOD_CRSATM          (0x2 << 16)
/** Combined injected simultaneous + fast interleaved mode. */
#define ADC_CR1_DUALMOD_CISFIM          (0x3 << 16)
/** Combined injected simultaneous + slow interleaved mode. */
#define ADC_CR1_DUALMOD_CISSIM          (0x4 << 16)
/** Injected simultaneous mode only. */
#define ADC_CR1_DUALMOD_ISM             (0x5 << 16)
/** Regular simultaneous mode only. */
#define ADC_CR1_DUALMOD_RSM             (0x6 << 16)
/** Fast interleaved mode only. */
#define ADC_CR1_DUALMOD_FIM             (0x7 << 16)
/** Slow interleaved mode only. */
#define ADC_CR1_DUALMOD_SIM             (0x8 << 16)
/** Alternate trigger mode only. */
#define ADC_CR1_DUALMOD_ATM             (0x9 << 16)
/**@}*/
#define ADC_CR1_DUALMOD_MASK		(0xF << 16)
#define ADC_CR1_DUALMOD_SHIFT		16

#define ADC_CR1_AWDCH_MAX		17

/* --- ADC_CR2 values ------------------------------------------------------ */

/* TSVREFE: */ /** Temperature sensor and V_REFINT enable. (ADC1 only!) */
#define ADC_CR2_TSVREFE			(1 << 23)

/* SWSTART: */ /** Start conversion of regular channels. */
#define ADC_CR2_SWSTART			(1 << 22)

/* JSWSTART: */ /** Start conversion of injected channels. */
#define ADC_CR2_JSWSTART		(1 << 21)

/* EXTTRIG: */ /** External trigger conversion mode for regular channels. */
#define ADC_CR2_EXTTRIG			(1 << 20)

/* EXTSEL[2:0]: External event select for regular group. */
/* The following are only valid for ADC1 and ADC2. */
/****************************************************************************/
/* ADC_CR2 EXTSEL[2:0] ADC Trigger Identifier for ADC1 and ADC2 */
/** @defgroup adc_trigger_regular_12 ADC Trigger Identifier for ADC1 and ADC2
@ingroup adc_defines

@{*/
/** Timer 1 Compare Output 1 */
#define ADC_CR2_EXTSEL_TIM1_CC1		(0x0 << 17)
/** Timer 1 Compare Output 2 */
#define ADC_CR2_EXTSEL_TIM1_CC2		(0x1 << 17)
/** Timer 1 Compare Output 3 */
#define ADC_CR2_EXTSEL_TIM1_CC3		(0x2 << 17)
/** Timer 2 Compare Output 2 */
#define ADC_CR2_EXTSEL_TIM2_CC2		(0x3 << 17)
/** Timer 3 Trigger Output */
#define ADC_CR2_EXTSEL_TIM3_TRGO	(0x4 << 17)
/** Timer 4 Compare Output 4 */
#define ADC_CR2_EXTSEL_TIM4_CC4		(0x5 << 17)
/** External Interrupt 11 */
#define ADC_CR2_EXTSEL_EXTI11		(0x6 << 17)
/** Software Trigger */
#define ADC_CR2_EXTSEL_SWSTART		(0x7 << 17)
/**@}*/

/* The following are only valid for ADC3 */
/****************************************************************************/
/* ADC_CR2 EXTSEL[2:0] ADC Trigger Identifier for ADC3 */
/** @defgroup adc_trigger_regular_3 ADC Trigger Identifier for ADC3
@ingroup adc_defines

@{*/
/** Timer 2 Compare Output 1 */
#define ADC_CR2_EXTSEL_TIM3_CC1		(0x0 << 17)
/** Timer 2 Compare Output 3 */
#define ADC_CR2_EXTSEL_TIM2_CC3		(0x1 << 17)
/** Timer 1 Compare Output 3 */
#define ADC_CR2_EXTSEL_TIM1_CC3		(0x2 << 17)
/** Timer 8 Compare Output 1 */
#define ADC_CR2_EXTSEL_TIM8_CC1		(0x3 << 17)
/** Timer 8 Trigger Output */
#define ADC_CR2_EXTSEL_TIM8_TRGO	(0x4 << 17)
/** Timer 5 Compare Output 1 */
#define ADC_CR2_EXTSEL_TIM5_CC1		(0x5 << 17)
/** Timer 5 Compare Output 3 */
#define ADC_CR2_EXTSEL_TIM5_CC3		(0x6 << 17)
/**@}*/

#define ADC_CR2_EXTSEL_MASK		(0x7 << 17)
#define ADC_CR2_EXTSEL_SHIFT		17

/* Note: Bit 16 is reserved, must be kept at reset value. */

/* JEXTTRIG: External trigger conversion mode for injected channels. */
#define ADC_CR2_JEXTTRIG		(1 << 15)

/* JEXTSEL[2:0]: External event selection for injected group. */
/* The following are only valid for ADC1 and ADC2. */
/****************************************************************************/
/* ADC_CR2 JEXTSEL[2:0] ADC Injected Trigger Identifier for ADC1 and ADC2 */
/** @defgroup adc_trigger_injected_12 ADC Injected Trigger Identifier for ADC1
and ADC2
@ingroup adc_defines

@{*/
/** Timer 1 Trigger Output */
#define ADC_CR2_JEXTSEL_TIM1_TRGO	(0x0 << 12)
/** Timer 1 Compare Output 4 */
#define ADC_CR2_JEXTSEL_TIM1_CC4	(0x1 << 12)
/** Timer 2 Trigger Output */
#define ADC_CR2_JEXTSEL_TIM2_TRGO	(0x2 << 12)
/** Timer 2 Compare Output 1 */
#define ADC_CR2_JEXTSEL_TIM2_CC1	(0x3 << 12)
/** Timer 3 Compare Output 4 */
#define ADC_CR2_JEXTSEL_TIM3_CC4	(0x4 << 12)
/** Timer 4 Trigger Output */
#define ADC_CR2_JEXTSEL_TIM4_TRGO	(0x5 << 12)
/** External Interrupt 15 */
#define ADC_CR2_JEXTSEL_EXTI15		(0x6 << 12)
/** Injected Software Trigger */
#define ADC_CR2_JEXTSEL_JSWSTART	(0x7 << 12) /* Software start. */
/**@}*/

/* --- ADC_SMPR1 values ---------------------------------------------------- */
#define ADC_SMPR1_SMP17_LSB		21
#define ADC_SMPR1_SMP16_LSB		18
#define ADC_SMPR1_SMP15_LSB		15
#define ADC_SMPR1_SMP14_LSB		12
#define ADC_SMPR1_SMP13_LSB		9
#define ADC_SMPR1_SMP12_LSB		6
#define ADC_SMPR1_SMP11_LSB		3
#define ADC_SMPR1_SMP10_LSB		0
#define ADC_SMPR1_SMP17_MSK		(0x7 << ADC_SMP17_LSB)
#define ADC_SMPR1_SMP16_MSK		(0x7 << ADC_SMP16_LSB)
#define ADC_SMPR1_SMP15_MSK		(0x7 << ADC_SMP15_LSB)
#define ADC_SMPR1_SMP14_MSK		(0x7 << ADC_SMP14_LSB)
#define ADC_SMPR1_SMP13_MSK		(0x7 << ADC_SMP13_LSB)
#define ADC_SMPR1_SMP12_MSK		(0x7 << ADC_SMP12_LSB)
#define ADC_SMPR1_SMP11_MSK		(0x7 << ADC_SMP11_LSB)
#define ADC_SMPR1_SMP10_MSK		(0x7 << ADC_SMP10_LSB)

/* --- ADC_SMPR2 values ---------------------------------------------------- */

#define ADC_SMPR2_SMP9_LSB		27
#define ADC_SMPR2_SMP8_LSB		24
#define ADC_SMPR2_SMP7_LSB		21
#define ADC_SMPR2_SMP6_LSB		18
#define ADC_SMPR2_SMP5_LSB		15
#define ADC_SMPR2_SMP4_LSB		12
#define ADC_SMPR2_SMP3_LSB		9
#define ADC_SMPR2_SMP2_LSB		6
#define ADC_SMPR2_SMP1_LSB		3
#define ADC_SMPR2_SMP0_LSB		0
#define ADC_SMPR2_SMP9_MSK		(0x7 << ADC_SMP9_LSB)
#define ADC_SMPR2_SMP8_MSK		(0x7 << ADC_SMP8_LSB)
#define ADC_SMPR2_SMP7_MSK		(0x7 << ADC_SMP7_LSB)
#define ADC_SMPR2_SMP6_MSK		(0x7 << ADC_SMP6_LSB)
#define ADC_SMPR2_SMP5_MSK		(0x7 << ADC_SMP5_LSB)
#define ADC_SMPR2_SMP4_MSK		(0x7 << ADC_SMP4_LSB)
#define ADC_SMPR2_SMP3_MSK		(0x7 << ADC_SMP3_LSB)
#define ADC_SMPR2_SMP2_MSK		(0x7 << ADC_SMP2_LSB)
#define ADC_SMPR2_SMP1_MSK		(0x7 << ADC_SMP1_LSB)
#define ADC_SMPR2_SMP0_MSK		(0x7 << ADC_SMP0_LSB)

/* --- ADC_SMPRx values --------------------------------------------------- */
/****************************************************************************/
/* ADC_SMPRG ADC Sample Time Selection for Channels */
/** @defgroup adc_sample_rg ADC Sample Time Selection for All Channels
@ingroup adc_defines

@{*/
#define ADC_SMPR_SMP_1DOT5CYC		0x0
#define ADC_SMPR_SMP_7DOT5CYC		0x1
#define ADC_SMPR_SMP_13DOT5CYC		0x2
#define ADC_SMPR_SMP_28DOT5CYC		0x3
#define ADC_SMPR_SMP_41DOT5CYC		0x4
#define ADC_SMPR_SMP_55DOT5CYC		0x5
#define ADC_SMPR_SMP_71DOT5CYC		0x6
#define ADC_SMPR_SMP_239DOT5CYC		0x7
/**@}*/


/* --- ADC_SQR1 values ----------------------------------------------------- */

#define ADC_SQR_MAX_CHANNELS_REGULAR	16

#define ADC_SQR1_SQ16_LSB		15
#define ADC_SQR1_SQ15_LSB		10
#define ADC_SQR1_SQ14_LSB		5
#define ADC_SQR1_SQ13_LSB		0
#define ADC_SQR1_L_MSK			(0xf << ADC_SQR1_L_LSB)
#define ADC_SQR1_SQ16_MSK		(0x1f << ADC_SQR1_SQ16_LSB)
#define ADC_SQR1_SQ15_MSK		(0x1f << ADC_SQR1_SQ15_LSB)
#define ADC_SQR1_SQ14_MSK		(0x1f << ADC_SQR1_SQ14_LSB)
#define ADC_SQR1_SQ13_MSK		(0x1f << ADC_SQR1_SQ13_LSB)

/* --- ADC_SQR2 values ----------------------------------------------------- */

#define ADC_SQR2_SQ12_LSB		25
#define ADC_SQR2_SQ11_LSB		20
#define ADC_SQR2_SQ10_LSB		15
#define ADC_SQR2_SQ9_LSB		10
#define ADC_SQR2_SQ8_LSB		5
#define ADC_SQR2_SQ7_LSB		0
#define ADC_SQR2_SQ12_MSK		(0x1f << ADC_SQR2_SQ12_LSB)
#define ADC_SQR2_SQ11_MSK		(0x1f << ADC_SQR2_SQ11_LSB)
#define ADC_SQR2_SQ10_MSK		(0x1f << ADC_SQR2_SQ10_LSB)
#define ADC_SQR2_SQ9_MSK		(0x1f << ADC_SQR2_SQ9_LSB)
#define ADC_SQR2_SQ8_MSK		(0x1f << ADC_SQR2_SQ8_LSB)
#define ADC_SQR2_SQ7_MSK		(0x1f << ADC_SQR2_SQ7_LSB)

/* --- ADC_SQR3 values ----------------------------------------------------- */

#define ADC_SQR3_SQ6_LSB		25
#define ADC_SQR3_SQ5_LSB		20
#define ADC_SQR3_SQ4_LSB		15
#define ADC_SQR3_SQ3_LSB		10
#define ADC_SQR3_SQ2_LSB		5
#define ADC_SQR3_SQ1_LSB		0
#define ADC_SQR3_SQ6_MSK		(0x1f << ADC_SQR3_SQ6_LSB)
#define ADC_SQR3_SQ5_MSK		(0x1f << ADC_SQR3_SQ5_LSB)
#define ADC_SQR3_SQ4_MSK		(0x1f << ADC_SQR3_SQ4_LSB)
#define ADC_SQR3_SQ3_MSK		(0x1f << ADC_SQR3_SQ3_LSB)
#define ADC_SQR3_SQ2_MSK		(0x1f << ADC_SQR3_SQ2_LSB)
#define ADC_SQR3_SQ1_MSK		(0x1f << ADC_SQR3_SQ1_LSB)

/* --- ADC_JDRx, ADC_DR values --------------------------------------------- */

#define ADC_JDATA_LSB			0
#define ADC_DATA_LSB			0
#define ADC_ADC2DATA_LSB		16 /* ADC1 only (dual mode) */
#define ADC_JDATA_MSK			(0xffff << ADC_JDATA_LSB)
#define ADC_DATA_MSK			(0xffff << ADC_DA)
#define ADC_ADC2DATA_MSK		(0xffff << ADC_ADC2DATA_LSB)
					/* ADC1 only (dual mode) */

/** Timer 1 Trigger Output */
#define ADC_CR2_JEXTSEL_TIM1_TRGO       (0x0 << 12)
/** Timer 1 Compare Output 4 */
#define ADC_CR2_JEXTSEL_TIM1_CC4        (0x1 << 12)
/** Timer 4 Compare Output 3 */
#define ADC_CR2_JEXTSEL_TIM4_CC3        (0x2 << 12)
/** Timer 8 Compare Output 2 */
#define ADC_CR2_JEXTSEL_TIM8_CC2        (0x3 << 12)
/** Timer 8 Compare Output 4 */
#define ADC_CR2_JEXTSEL_TIM8_CC4        (0x4 << 12)
/** Timer 5 Trigger Output */
#define ADC_CR2_JEXTSEL_TIM5_TRGO       (0x5 << 12)
/** Timer 5 Compare Output 4 */
#define ADC_CR2_JEXTSEL_TIM5_CC4        (0x6 << 12)
/** Injected Software Trigger */
#define ADC_CR2_JEXTSEL_JSWSTART        (0x7 << 12) /* Software start. */
/**@}*/

#define ADC_CR2_JEXTSEL_MASK            (0x7 << 12)
#define ADC_CR2_JEXTSEL_SHIFT           12

/* ALIGN: Data alignment. */
#define ADC_CR2_ALIGN_RIGHT             (0 << 11)
#define ADC_CR2_ALIGN_LEFT              (1 << 11)
#define ADC_CR2_ALIGN                   (1 << 11)

/* Note: Bits [10:9] are reserved and must be kept at reset value. */

/* DMA: Direct memory access mode. (ADC1 and ADC3 only!) */
#define ADC_CR2_DMA                     (1 << 8)
/* RSTCAL: Reset calibration. */
#define ADC_CR2_RSTCAL                  (1 << 3)

/* CAL: A/D Calibration. */
#define ADC_CR2_CAL                     (1 << 2)

/* CONT: Continous conversion. */
#define ADC_CR2_CONT                    (1 << 1)

/* ADON: A/D converter On/Off. */
/* Note: If any other bit in this register apart from ADON is changed at the
 * same time, then conversion is not triggered. This is to prevent triggering
 * an erroneous conversion.
 * Conclusion: Must be separately written.
 */
#define ADC_CR2_ADON                    (1 << 0)

/* --- Function prototypes ------------------------------------------------- */

struct Stm32Adc {
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
    uint64_t ns_per_sample[8]; /*8 possibility of numbers cycles for each conversion 
                                (recover from: time register 1 (SMPR1),time register 2 (SMPR2))*/

    /* Register Values */
    uint32_t
	 ADC_SR,
	 ADC_CR1,
	 ADC_CR2,
	 ADC_SMPR1,
	 ADC_SMPR2,
	 ADC_JOFR1,
	 ADC_JOFR2,
	 ADC_JOFR3,
	 ADC_JOFR4,
	 ADC_HTR,
	 ADC_LTR,
	 ADC_SQR1,
	 ADC_SQR2,
	 ADC_SQR3,
	 ADC_JSQR,
	 ADC_JDR1,
	 ADC_JDR2,
	 ADC_JDR3,
	 ADC_JDR4,
	 ADC_DR;
        

    bool sr_read_since_ore_set;

    bool converting;

    struct QEMUTimer *conv_timer;

    CharDriverState *chr;

    uint32_t afio_board_map;

    qemu_irq irq;
    int curr_irq_level;
    int Vref; //mv
    int Vdda; //mv
};
/* functions added to adc*/ 
static void stm32_ADC_GPIO_check(Stm32Adc *s,int channel);
static void stm32_ADC_update_ns_per_sample(Stm32Adc* s);
static uint64_t stm32_ADC_get_nbr_cycle_per_sample(Stm32Adc* s,int channel);
static int stm32_ADC_get_channel_number(Stm32Adc* s,int convert_number);
static void stm32_ADC_SR_write(Stm32Adc *s, uint32_t new_value);
static void stm32_ADC_SQR1_write(Stm32Adc *s,uint32_t new_value);
static void stm32_ADC_CR2_write(Stm32Adc *s,uint32_t new_value);
static uint32_t stm32_ADC_DR_read(Stm32Adc *s);

/* functions modified in adc*/ 
static void stm32_adc_clk_irq_handler(void *opaque, int n, int level);
static void stm32_ADC_update_irq(Stm32Adc *s);
static void stm32_adc_start_conv(Stm32Adc *s);
static void stm32_adc_reset(DeviceState *dev);

/* HELPER FUNCTIONS */

/* Handle a change in the peripheral clock. */
static void stm32_adc_clk_irq_handler(void *opaque, int n, int level)
{

    Stm32Adc *s = (Stm32Adc *)opaque;
    assert(n == 0);

    /* Only update the ns per sample if the IRQ is being set. */
    if(level) {
        stm32_ADC_update_ns_per_sample(s);
    }

}


/* Routine which updates the ADC IRQ.  This should be called whenever
 * an interrupt-related flag is updated.
 */
static void stm32_ADC_update_irq(Stm32Adc *s) {
    int new_irq_level = 
        ((s->ADC_CR1 >> 5) & (s->ADC_SR >> 1)) | 
        ((s->ADC_CR1 >> 7) & (s->ADC_SR >> 2)) |
        ((s->ADC_CR1 >> 6) & (s->ADC_SR >> 0));
       
    /* Only trigger an interrupt if the IRQ level changes.  We probably could
     * set the level regardless, but we will just check for good measure.
     */
     
    if((new_irq_level & 1) ^ s->curr_irq_level ) {
        qemu_set_irq(s->irq, new_irq_level);
        s->curr_irq_level = new_irq_level;
    }
}

static void stm32_adc_conv_complete(Stm32Adc *s)
{
  s-> ADC_SR|=ADC_SR_EOC;  // jmf : indicates end of conversion
  stm32_ADC_update_irq(s); 
}


static void stm32_adc_start_conv(Stm32Adc *s)
{
    uint64_t curr_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int channel_number=stm32_ADC_get_channel_number(s,1);
    // Write result of conversion
      if(channel_number==16){
      s->Vdda=rand()%(1200+1) + 2400; //Vdda belongs to the interval [2400 3600] mv
      s->Vref=rand()%(s->Vdda-2400+1) + 2400; //Vref belongs to the interval [2400 Vdda] mv
      s->ADC_DR= s->Vdda - s->Vref; 
      }
      else if(channel_number==17){
      s->ADC_DR= (s->Vref=rand()%(s->Vdda-2400+1) + 2400); //Vref [2400 Vdda] mv
      }
      else{
      s->ADC_DR=((int)(1024.*(sin(2*M_PI*qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)/1e9)+1.))&0xfff);
      }
      s->ADC_SR&=~ADC_SR_EOC;  // jmf : indicates ongoing conversion
      s->ADC_CR2&=~ADC_CR2_SWSTART;
      // calls conv_complete when expires      
      timer_mod(s->conv_timer,  curr_time + stm32_ADC_get_nbr_cycle_per_sample(s,channel_number)); 
}


/* TIMER HANDLERS */
/* When the convert delay is complete, mark the conversion as complete */
static void stm32_adc_conv_timer_expire(void *opaque) {
    Stm32Adc *s = (Stm32Adc *)opaque;
    stm32_adc_conv_complete(s);
}

/* Checks the ADC GPIO PIN Mode and Config */
static void stm32_ADC_GPIO_check(Stm32Adc *s,int channel)
{
 int ADC_periph,ADC_pin,config;
 if(channel<16){  
        if(channel<=15 && channel>=10){
        ADC_periph=STM32_GPIOC;
        ADC_pin=channel-10; //PC(0-5) IN10-IN15
        }
        else if(channel==9 || channel==8){
        ADC_periph=STM32_GPIOB;
        ADC_pin=channel-8; //PB(0-1) IN8-IN9
        }
        else{
        ADC_periph=STM32_GPIOA;
        ADC_pin=channel; //PA(0-7) IN0-IN7
        }

          
   Stm32Gpio *gpio_dev = s->stm32_gpio[STM32_GPIO_INDEX_FROM_PERIPH(ADC_periph)];
   if(stm32_gpio_get_mode_bits(gpio_dev, ADC_pin) != STM32_GPIO_MODE_IN) {
        hw_error("GPIO%c pin:%d needs to be configured as input",'A'+ADC_periph-1,ADC_pin);
    }

    config = stm32_gpio_get_config_bits(gpio_dev, ADC_pin);
    if(config != STM32_GPIO_IN_ANALOG)
        hw_error("GPIO%c pin:%d needs to be configured as Analog input",'A'+ADC_periph-1,ADC_pin);

             }

}

static int stm32_ADC_get_channel_number(Stm32Adc* s,int convert_number)
{
   assert(convert_number>=1 && convert_number<=16);

   if(convert_number>=1 && convert_number<=6)
   return( (s->ADC_SQR3 >> 5*(convert_number-1)) & 0x0000001f);

   else if(convert_number>=7 && convert_number<=12)
   return((s->ADC_SQR2 >> 5*(convert_number-7)) & 0x0000001f);

   else //(convert_number>=13 && convert_number<=16)
   return((s->ADC_SQR1 >> 5*(convert_number-13)) & 0x0000001f);

}

static uint64_t stm32_ADC_get_nbr_cycle_per_sample(Stm32Adc* s,int channel)
{
  assert(channel<=17 && channel>=0);
  int index_cycle;
       
           
  if (channel<=17 && channel>=10)
     index_cycle=(s->ADC_SMPR1 >> 3*(channel-10)) & 0x00000007; //recover index cycle from ADC sample time register 1 (SMPR1)
  else 
     index_cycle=(s->ADC_SMPR2 >> 3*channel) & 0x00000007;  //recover index cycle from ADC sample time register 2 (SMPR2)
        
  /* index_cycle(0-7) numbers of cycles correspondent 
     (1.5, 7.5, 13.5, 28.5, 41.5, 55.5, 71.5, 239.5) */  
   return s->ns_per_sample[index_cycle];
}

/* REGISTER IMPLEMENTATION */
static void stm32_ADC_SR_write(Stm32Adc *s, uint32_t new_value)
{
    /* The ADC_SR flags can be cleared, but not set. */
    if(new_value & ADC_SR_EOC)
     hw_error("Software attempted to set ADC SR_EOC bit\n");
    if(new_value & ADC_SR_JEOC)
     hw_error("Software attempted to set ADC SR_JEOC bit\n");  
    if(new_value & ADC_SR_AWD)
     hw_error("Software attempted to set ADC SR_EOC bit\n");
    if(new_value & ADC_SR_JSTRT)
     hw_error("Software attempted to set ADC SR_JSTRT bit\n");
    if(new_value & ADC_SR_STRT)
     hw_error("Software attempted to set ADC SR_STRT bit\n");
  
    s->ADC_SR= new_value & 0x0000001f;

    stm32_ADC_update_irq(s); //modification of ADC_SR requiere update of interrupt 
}

static void stm32_ADC_SQR1_write(Stm32Adc *s,uint32_t new_value)
{  
    /* check if number of conversion (ADC_SQR1[24 20]) greater than 1 */  
     if((new_value >> 20 & 0x0000000f) > 1) 
     hw_error("Mode Single conversion is only implemented\n");

     s->ADC_SQR1=new_value & 0x00ffffff;
}

static void stm32_ADC_CR2_write(Stm32Adc *s,uint32_t new_value)
{      
    s->ADC_CR2=new_value & 0x00fef90f; 
 
    if (s->ADC_CR2&ADC_CR2_SWSTART )  
    {
      if(!(s->ADC_CR2 & ADC_CR2_ADON))   //CR2_ADON should be set (for Enable ADC) before start conversion
         hw_error("Attempted to start conversion while ADC was disabled\n");

      stm32_ADC_GPIO_check(s,stm32_ADC_get_channel_number(s,1)); // check GPIO (Mode and config)  ANALOG INTPUT?  
      stm32_adc_start_conv(s); // jmf : software conv
    }
   
}

static uint32_t stm32_ADC_DR_read(Stm32Adc *s)
{
    
   /* check ADC Enable */  
   if(!(s->ADC_CR2 & ADC_CR2_ADON))
     hw_error("Attempted to read from ADC_DR while ADC was disabled\n");
  
   /* check conversion complete*/
   if(s->ADC_SR & ADC_SR_EOC)
     {           
       s->ADC_SR &=~((uint32_t)ADC_SR_EOC); //cleared SR_EOC flag by reading ADC_DR   
       stm32_ADC_update_irq(s); // (SR_EOC=0) requiere interrupt update
       return s->ADC_DR;
     }
   else
     {
       hw_error("Attempted to read ADC_DR while conversion is not complete\n");
       return 0;
     }
}



static void stm32_adc_reset(DeviceState *dev)
{
    Stm32Adc *s = STM32_ADC(dev);
    s->ADC_SR=0x00000000;
    s->ADC_CR1=0x00000000;
    s->ADC_CR2=0x00000000;
    s->ADC_SMPR1=0x00000000;
    s->ADC_SMPR2=0x00000000;
    s->ADC_JOFR1=0x00000000;
    s->ADC_JOFR2=0x00000000;
    s->ADC_JOFR3=0x00000000;
    s->ADC_JOFR4=0x00000000;
    s->ADC_HTR=0x00000000;
    s->ADC_LTR=0x00000000;
    s->ADC_SQR1=0x00000000;
    s->ADC_SQR2=0x00000000;
    s->ADC_SQR3=0x00000000;
    s->ADC_JSQR=0x00000000;
    s->ADC_JDR1=0x00000000;
    s->ADC_JDR2=0x00000000;
    s->ADC_JDR3=0x00000000;
    s->ADC_JDR4=0x00000000;
    s->ADC_DR=0x00000000;

    stm32_ADC_update_irq(s);
}

static uint64_t stm32_adc_read(void *opaque, hwaddr offset,
                          unsigned size)
{
    Stm32Adc *s = (Stm32Adc *)opaque;
    int start = (offset & 3) * 8;
    int length = size * 8;

    switch (offset & 0xfffffffc) {
        case oADC_SR : 	return (extract64(s->ADC_SR,  start, length));
        case oADC_CR1: 	return extract64(s->ADC_CR1,  start, length);
        case oADC_CR2: 	return (extract64(s->ADC_CR2, start, length)&~ADC_CR2_RSTCAL&~ADC_CR2_CAL); // jmf : calibration complete
        case oADC_SMPR1:return extract64(s->ADC_SMPR1,start, length);
        case oADC_SMPR2:return extract64(s->ADC_SMPR2,start, length);
        case oADC_JOFR1:return extract64(s->ADC_JOFR1,start, length);
	case oADC_JOFR2:return extract64(s->ADC_JOFR2,start, length);
	case oADC_JOFR3:return extract64(s->ADC_JOFR3,start, length);
	case oADC_JOFR4:return extract64(s->ADC_JOFR4,start, length);
	case oADC_HTR: 	return extract64(s->ADC_HTR,  start, length);
	case oADC_LTR : return extract64(s->ADC_LTR,  start, length);
	case oADC_SQR1: return extract64(s->ADC_SQR1, start, length);
	case oADC_SQR2: return extract64(s->ADC_SQR2, start, length);
	case oADC_SQR3: return extract64(s->ADC_SQR3, start, length);
	case oADC_JSQR: return extract64(s->ADC_JSQR, start, length);
	case oADC_JDR1: return extract64(s->ADC_JDR1, start, length);
	case oADC_JDR2: return extract64(s->ADC_JDR2, start, length);
	case oADC_JDR3: return extract64(s->ADC_JDR3, start, length);
	case oADC_JDR4: return extract64(s->ADC_JDR4, start, length);
	case oADC_DR  : return extract64(stm32_ADC_DR_read(s), start, length); 
        default:
		fprintf(stderr, "jmf unknown read : %lld, size %d\n",(long long)offset,size);
            STM32_BAD_REG(offset, size);
            return 0;
    }
}

static void stm32_adc_write(void *opaque, hwaddr offset,
                       uint64_t value, unsigned size)
{
    Stm32Adc *s = (Stm32Adc *)opaque;
    
    stm32_rcc_check_periph_clk((Stm32Rcc *)s->stm32_rcc, s->periph);

    switch (offset & 0xfffffffc) {
        case oADC_SR : stm32_ADC_SR_write(s,value);break;
        case oADC_CR1: (s->ADC_CR1=value & 0x00cfffff); stm32_ADC_update_irq(s); break; //write CR1 requiere update interrupts 
        case oADC_CR2: stm32_ADC_CR2_write(s,value);break;
        case oADC_SMPR1:(s->ADC_SMPR1=value & 0x00ffffff);break;
        case oADC_SMPR2:(s->ADC_SMPR2=value & 0x3fffffff);break;
        case oADC_JOFR1:(s->ADC_JOFR1=value & 0x00000fff);break;
 	case oADC_JOFR2:(s->ADC_JOFR2=value & 0x00000fff);break;
 	case oADC_JOFR3:(s->ADC_JOFR3=value & 0x00000fff);break;
 	case oADC_JOFR4:(s->ADC_JOFR4=value & 0x00000fff);break;
	case oADC_HTR:  (s->ADC_HTR=value & 0x00000fff);break;
 	case oADC_LTR:  (s->ADC_LTR=value & 0x00000fff);break;
	case oADC_SQR1: stm32_ADC_SQR1_write(s,value);break;
	case oADC_SQR2: (s->ADC_SQR2=value & 0x3fffffff);break;
	case oADC_SQR3: (s->ADC_SQR3=value & 0x3fffffff);break;
	case oADC_JSQR: (s->ADC_JSQR=value & 0x003fffff);break;
	
        default: fprintf(stderr, "jmf unknown write : %lld, size %d\n",(long long)offset,size);
            STM32_BAD_REG(offset, 2);
            break;
    }
}
void stm32_ADC_update_ns_per_sample(Stm32Adc *s)
{
  uint32_t clk_freq = stm32_rcc_get_periph_freq(s->stm32_rcc, s->periph);
  int i;
  //convert cycles to ns
  if(clk_freq){  
  s->ns_per_sample[0]=(1000000000LL*1.5)/clk_freq;s->ns_per_sample[1]=(1000000000LL*7.5)/clk_freq;
  s->ns_per_sample[2]=(1000000000LL*13.5)/clk_freq;s->ns_per_sample[3]=(1000000000LL*28.5)/clk_freq;
  s->ns_per_sample[4]=(1000000000LL*41.5)/clk_freq;s->ns_per_sample[5]=(1000000000LL*55.5)/clk_freq;
  s->ns_per_sample[6]=(1000000000LL*71.5)/clk_freq;s->ns_per_sample[7]=(1000000000LL*239.5)/clk_freq;
  }
  else{
  for(i=0;i<8;i++)
  s->ns_per_sample[i]=0;
  }
}
static const MemoryRegionOps stm32_adc_ops = {
    .read = stm32_adc_read,
    .write = stm32_adc_write,
    .valid.min_access_size = 2,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN
};



/* DEVICE INITIALIZATION */

static int stm32_adc_init(SysBusDevice *dev)
{
    qemu_irq *clk_irq;
    Stm32Adc *s = STM32_ADC(dev);
    s->stm32_rcc = (Stm32Rcc *)s->stm32_rcc_prop;
    s->stm32_gpio = (Stm32Gpio **)s->stm32_gpio_prop;
    memory_region_init_io(&s->iomem, OBJECT(s), &stm32_adc_ops, s, "adc", 0x03ff);  
        // jmf : 3FF = length, cf RM0008 p.52
    sysbus_init_mmio(dev, &s->iomem);
    sysbus_init_irq(dev, &s->irq);
    s->conv_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, (QEMUTimerCB *)stm32_adc_conv_timer_expire, s);

    /* Register handlers to handle updates to the ADC's peripheral clock. */
    clk_irq = qemu_allocate_irqs(stm32_adc_clk_irq_handler, (void *)s, 1);   // jmf : segfault
    
    stm32_rcc_set_periph_clk_irq(s->stm32_rcc, s->periph, clk_irq[0]);
    stm32_adc_reset((DeviceState *)s);
    s->Vdda=rand()%(1200+1) +2400; //Vdda belongs to the interval [2400 3600] mv
    s->Vref=rand()%(s->Vdda-2400+1) +2400; //Vref belongs to the interval [2400 Vdda] mv
    return 0;
}

static Property stm32_adc_properties[] = {
    DEFINE_PROP_PERIPH_T("periph", Stm32Adc, periph, STM32_PERIPH_UNDEFINED),
    DEFINE_PROP_PTR("stm32_rcc", Stm32Adc, stm32_rcc_prop),
    DEFINE_PROP_PTR("stm32_gpio", Stm32Adc, stm32_gpio_prop),
    DEFINE_PROP_END_OF_LIST()
};

static void stm32_adc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = stm32_adc_init;
    dc->reset = stm32_adc_reset;
    dc->props = stm32_adc_properties;
}

static TypeInfo stm32_adc_info = {
    .name  = "stm32-adc",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(Stm32Adc),
    .class_init = stm32_adc_class_init
};

static void stm32_adc_register_types(void)
{
    type_register_static(&stm32_adc_info);
}

type_init(stm32_adc_register_types)
