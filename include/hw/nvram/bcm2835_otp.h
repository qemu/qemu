/*
 * BCM2835 One-Time Programmable (OTP) Memory
 *
 * Copyright (c) 2024 Rayhan Faizel <rayhan.faizel@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BCM2835_OTP_H
#define BCM2835_OTP_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_BCM2835_OTP "bcm2835-otp"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2835OTPState, BCM2835_OTP)

#define BCM2835_OTP_ROW_COUNT                              66

/* https://elinux.org/BCM2835_registers#OTP */
#define BCM2835_OTP_BOOTMODE_REG                         0x00
#define BCM2835_OTP_CONFIG_REG                           0x04
#define BCM2835_OTP_CTRL_LO_REG                          0x08
#define BCM2835_OTP_CTRL_HI_REG                          0x0c
#define BCM2835_OTP_STATUS_REG                           0x10
#define BCM2835_OTP_BITSEL_REG                           0x14
#define BCM2835_OTP_DATA_REG                             0x18
#define BCM2835_OTP_ADDR_REG                             0x1c
#define BCM2835_OTP_WRITE_DATA_READ_REG                  0x20
#define BCM2835_OTP_INIT_STATUS_REG                      0x24


/* -- Row 32: Undocumented -- */

#define BCM2835_OTP_ROW_32                                 32

/* Lock OTP Programming (Customer OTP and private key) */
#define BCM2835_OTP_ROW_32_LOCK                        BIT(6)

/* -- Row 36-43: Customer OTP -- */

#define BCM2835_OTP_CUSTOMER_OTP                           36
#define BCM2835_OTP_CUSTOMER_OTP_LEN                        8

/* Magic numbers to lock programming of customer OTP and private key */
#define BCM2835_OTP_LOCK_NUM1                      0xffffffff
#define BCM2835_OTP_LOCK_NUM2                      0xaffe0000

/* -- Row 56-63: Device-specific private key -- */

#define BCM2835_OTP_PRIVATE_KEY                            56
#define BCM2835_OTP_PRIVATE_KEY_LEN                         8


struct BCM2835OTPState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    uint32_t otp_rows[BCM2835_OTP_ROW_COUNT];
};


uint32_t bcm2835_otp_get_row(BCM2835OTPState *s, unsigned int row);
void bcm2835_otp_set_row(BCM2835OTPState *s, unsigned int row, uint32_t value);

#endif
