/*
 * PMBus device for Renesas Digital Multiphase Voltage Regulators
 *
 * Copyright 2022 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_ISL_PMBUS_VR_H
#define HW_MISC_ISL_PMBUS_VR_H

#include "hw/i2c/pmbus_device.h"
#include "qom/object.h"

#define TYPE_ISL69260   "isl69260"
#define TYPE_RAA228000  "raa228000"
#define TYPE_RAA229004  "raa229004"

struct ISLState {
    PMBusDevice parent;
};

OBJECT_DECLARE_SIMPLE_TYPE(ISLState, ISL69260)

#define ISL_CAPABILITY_DEFAULT                 0x40
#define ISL_OPERATION_DEFAULT                  0x80
#define ISL_ON_OFF_CONFIG_DEFAULT              0x16
#define ISL_VOUT_MODE_DEFAULT                  0x40
#define ISL_VOUT_COMMAND_DEFAULT               0x0384
#define ISL_VOUT_MAX_DEFAULT                   0x08FC
#define ISL_VOUT_MARGIN_HIGH_DEFAULT           0x0640
#define ISL_VOUT_MARGIN_LOW_DEFAULT            0xFA
#define ISL_VOUT_TRANSITION_RATE_DEFAULT       0x64
#define ISL_VOUT_OV_FAULT_LIMIT_DEFAULT        0x076C
#define ISL_OT_FAULT_LIMIT_DEFAULT             0x7D
#define ISL_OT_WARN_LIMIT_DEFAULT              0x07D0
#define ISL_VIN_OV_WARN_LIMIT_DEFAULT          0x36B0
#define ISL_VIN_UV_WARN_LIMIT_DEFAULT          0x1F40
#define ISL_IIN_OC_FAULT_LIMIT_DEFAULT         0x32
#define ISL_TON_DELAY_DEFAULT                  0x14
#define ISL_TON_RISE_DEFAULT                   0x01F4
#define ISL_TOFF_FALL_DEFAULT                  0x01F4
#define ISL_REVISION_DEFAULT                   0x33
#define ISL_READ_VOUT_DEFAULT                  1000
#define ISL_READ_IOUT_DEFAULT                  40
#define ISL_READ_POUT_DEFAULT                  4
#define ISL_READ_TEMP_DEFAULT                  25
#define ISL_READ_VIN_DEFAULT                   1100
#define ISL_READ_IIN_DEFAULT                   40
#define ISL_READ_PIN_DEFAULT                   4

#endif
