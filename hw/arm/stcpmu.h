/* pmuregs.h
 *
 * Register definitions for the Simtec PMU.
 *
 * Copyright 2006 Simtec Electronics.
 */

#ifndef PMUREGS_H
#define PMUREGS_H

/* Versions of the Simtec Power management interface. */
/* Version 1.0 interfcae */
#define STCPMU_V1_02		 2

/* Version 1.2 interface never existed */

/* Version 1.3 interface */
#define STCPMU_V1_30		30
#define STCPMU_V1_31		31
#define STCPMU_V1_32		32
#define STCPMU_V1_33		33
#define STCPMU_V1_34		34

#define STCPMU_VCURR STCPMU_V1_34 /**< Current revision of the PMU interface. */

/* IIC registers */

/* Version 1.20 regs */
#define IICREG_IDENT		0	/**< PMU ident (SBPM) */
#define IICREG_VER		1	/**< PMU version information. */
#define IICREG_DDCEN		2	/**< Enables/disables the DDC_EN pin */
#define IICREG_PWR		3	/**< Soft power switch */
#define IICREG_RST		4	/**< Press the reset button */
#define IICREG_GWO		5	/**< Global Wake On ... */
#define IICREG_WOL		6	/**< Wake On LAN */
#define IICREG_WOR		7	/**< Wake On Ring */
#define IICREG_SND		8	/**< Play note */
#define IICREG_UNQID		9	/**< Unique ID */
#define IICREG_SLEEP		10	/**< Enter Sleep mode */

/* Version 1.30 regs */
#define IICREG_IRQEN		5	/**< Non zero to enable irqs */

#define IICREG_STATUS		11	/**< (0x0b) status of last operation */

#define IICREG_GPIO_PRESENT	20	/**< (0x14) Pullup enables */
#define IICREG_GPIO_PULLUP	21	/**< (0x15) Pullup enables */
#define IICREG_GPIO_DDR		22	/**< (0x16) Direction, 1=out, 0=in */
#define IICREG_GPIO_STATUS	23	/**< (0x17) GPIO current status (rd) */
#define IICREG_GPIO_SET		23	/**< (0x17) GPIO output bit set */
#define IICREG_GPIO_CLEAR	24	/**< (0x18) GPIO output bit clear */
#define IICREG_GPIO_IRQSOURCE	25	/**< (0x19) Source IRQ mask */
#define IICREG_GPIO_IRQEDGE	26	/**< (0x1a) IRQ Edge/Level select  */
#define IICREG_GPIO_IRQPOLARITY	27	/**< (0x1b) IRQ polarity */
#define IICREG_GPIO_IRQSTATUS	28	/**< (0x1c) IRQs pending, write clears*/
#define IICREG_GPIO_IRQDELAY	29	/**< (0x1d) IRQ delay mask */
#define IICREG_GPIO_DELAY	30	/**< (0x1e) delay time in deciseconds */
#define IICREG_GPIO_IRQBOTHEDGE 31	/**< (0x1f) IRQs on either edge */
#define IICREG_GPIO_IRQFIRST	32	/**< (0x20) First IRQ detected */
#define IICREG_GPIO_IRQRAW	33	/**< (0x21) IRQ raw status */

#define IICREG_ADC_INFO         39      /**< Information about the ADC. */
#define IICREG_ADC_PRESENT	40	/**< ADC presence indicators. */
#define IICREG_ADC_IRQSOURCE	41      /**< ADC IRQ source enables. */
#define IICREG_ADC_IRQSTATUS	42	/**< ADC IRQ status. */
#define IICREG_ADC_POLARITY	43	/**< ADC IRQ polarity. */
#define IICREG_ADC_0		44	/**< ADC 0 value. */
#define IICREG_ADC_1		45	/**< ADC 1 value. */
#define IICREG_ADC_2		46	/**< ADC 2 value. */
#define IICREG_ADC_3		47	/**< ADC 3 value. */
#define IICREG_ADC_4		48	/**< ADC 4 value. */
#define IICREG_ADC_5		49	/**< ADC 5 value. */
#define IICREG_ADC_6		50	/**< ADC 6 value. */
#define IICREG_ADC_7		51	/**< ADC 7 value. */
#define IICREG_ADC_0_THRESHOLD	52	/**< ADC 0 threshold. */
#define IICREG_ADC_1_THRESHOLD	53	/**< ADC 1 threshold. */
#define IICREG_ADC_2_THRESHOLD	54	/**< ADC 2 threshold. */
#define IICREG_ADC_3_THRESHOLD	55	/**< ADC 3 threshold. */
#define IICREG_ADC_4_THRESHOLD	56	/**< ADC 4 threshold. */
#define IICREG_ADC_5_THRESHOLD	57	/**< ADC 5 threshold. */
#define IICREG_ADC_6_THRESHOLD	58	/**< ADC 6 threshold. */
#define IICREG_ADC_7_THRESHOLD	59	/**< ADC 7 threshold. */

/* Version 1.32 registers */

#define IICREG_HWINFO		12	/**< (0x0C) Hardware specific information. */

/* Version 1.33 registers */
#define IICREG_IMPSPEC		13	/**< (0x0D) Implementation specific. */

/* Version 1.34 registers */
#define IICREG_WDG_POR		64	/**< (0x40) Power-On / Reset watchdog. */
#define IICREG_WDG_BUSBEAT	65	/**< (0x41) Bus heartbeat watchdog. */

/* DEBUG registers - only present in debug builds */
#define IICREG_SCRATCH          128


#define IICREG_EEBASE 0xC0

/* eeprom area */
#define EEPROT 0x08	/* number of write once protected bytes */
#define EELNGH 0x40
#define IICREG_EE0   (IICREG_EEBASE + EEPROT) /* EEPROM location 0 (allowing for uniqueID) */
#define IICREG_EEMAX ((IICREG_EE0 + (EELNGH - EEPROT))-1)

/* EEPROM config byte locations */
#define EELOC_WOL (EEPROT + 0)
#define EELOC_WOR (EEPROT + 1)

/* ident bytes */
#define IICIDENT_0	0x53	/* S */
#define IICIDENT_1	0x42	/* B */
#define IICIDENT_2	0x50	/* P */
#define IICIDENT_3	0x4d	/* M */


/* Guard value for potentially hazardous operations (reset, sleep and power off) */
#define IIC_GUARD 0x55

/* status codes - pmu status of last request */
#define PMUSTATUS_OK		(0)
#define PMUSTATUS_ERROR		(1)	/* general failed operation */
#define PMUSTATUS_ACCESS	(2)	/* no writable register here */
#define PMUSTATUS_REGISTER	(3)	/* no readable register here */
#define PMUSTATUS_SHORT		(4)	/* not enough data for operation */
#define PMUSTATUS_INVALID	(5)	/* guard invalid */

#endif
