#ifndef IPOD_TOUCH_I2C_H
#define IPOD_TOUCH_I2C_H

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"

#define TYPE_IPOD_TOUCH_I2C                  "ipodtouch.i2c"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchI2CState, IPOD_TOUCH_I2C)

#define I2CCON        0x00      /* I2C Control register */
#define I2CSTAT       0x04      /* I2C Status register */
#define I2CADD        0x08      /* I2C Slave Address register */
#define I2CDS         0x0c      /* I2C Data Shift register */
#define I2CLC         0x10      /* I2C Line Control register */

#define IICREG20	  0x20

#define SR_MODE       0x0       /* Slave Receive Mode */
#define ST_MODE       0x1       /* Slave Transmit Mode */
#define MR_MODE       0x2       /* Master Receive Mode */
#define MT_MODE       0x3       /* Master Transmit Mode */


#define S5L8900_IICCON_ACKEN        (1<<7)
#define S5L8900_IICCON_TXDIV_16     (0<<6)
#define S5L8900_IICCON_TXDIV_512    (1<<6)
#define S5L8900_IICCON_IRQEN        (1<<5)
#define S5L8900_IICCON_IRQPEND      (1<<4)

#define S5L8900_IICSTAT_START       (1<<5)
#define S5L8900_IICSTAT_BUSBUSY     (1<<5)
#define S5L8900_IICSTAT_TXRXEN      (1<<4)
#define S5L8900_IICSTAT_ARBITR      (1<<3)
#define S5L8900_IICSTAT_ASSLAVE     (1<<2)
#define S5L8900_IICSTAT_ADDR0       (1<<1)
#define S5L8900_IICSTAT_LASTBIT     (1<<0)

#define S5L8900_I2C_REG_MEM_SIZE    0x1000

typedef struct IPodTouchI2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    I2CBus *bus;
    qemu_irq irq;

    uint8_t control;
    uint8_t status;
    uint8_t address;
    uint8_t datashift;
    uint8_t line_ctrl;
	uint32_t iicreg20;

	uint8_t active;

    uint8_t ibmr;
    uint8_t data;
} IPodTouchI2CState;

#endif