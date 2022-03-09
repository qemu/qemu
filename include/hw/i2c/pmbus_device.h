/*
 * QEMU PMBus device emulation
 *
 * Copyright 2021 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PMBUS_DEVICE_H
#define HW_PMBUS_DEVICE_H

#include "qemu/bitops.h"
#include "hw/i2c/smbus_slave.h"

enum pmbus_registers {
    PMBUS_PAGE                      = 0x00, /* R/W byte */
    PMBUS_OPERATION                 = 0x01, /* R/W byte */
    PMBUS_ON_OFF_CONFIG             = 0x02, /* R/W byte */
    PMBUS_CLEAR_FAULTS              = 0x03, /* Send Byte */
    PMBUS_PHASE                     = 0x04, /* R/W byte */
    PMBUS_PAGE_PLUS_WRITE           = 0x05, /* Block Write-only */
    PMBUS_PAGE_PLUS_READ            = 0x06, /* Block Read-only */
    PMBUS_WRITE_PROTECT             = 0x10, /* R/W byte */
    PMBUS_STORE_DEFAULT_ALL         = 0x11, /* Send Byte */
    PMBUS_RESTORE_DEFAULT_ALL       = 0x12, /* Send Byte */
    PMBUS_STORE_DEFAULT_CODE        = 0x13, /* Write-only Byte */
    PMBUS_RESTORE_DEFAULT_CODE      = 0x14, /* Write-only Byte */
    PMBUS_STORE_USER_ALL            = 0x15, /* Send Byte */
    PMBUS_RESTORE_USER_ALL          = 0x16, /* Send Byte */
    PMBUS_STORE_USER_CODE           = 0x17, /* Write-only Byte */
    PMBUS_RESTORE_USER_CODE         = 0x18, /* Write-only Byte */
    PMBUS_CAPABILITY                = 0x19, /* Read-Only byte */
    PMBUS_QUERY                     = 0x1A, /* Write-Only */
    PMBUS_SMBALERT_MASK             = 0x1B, /* Block read, Word write */
    PMBUS_VOUT_MODE                 = 0x20, /* R/W byte */
    PMBUS_VOUT_COMMAND              = 0x21, /* R/W word */
    PMBUS_VOUT_TRIM                 = 0x22, /* R/W word */
    PMBUS_VOUT_CAL_OFFSET           = 0x23, /* R/W word */
    PMBUS_VOUT_MAX                  = 0x24, /* R/W word */
    PMBUS_VOUT_MARGIN_HIGH          = 0x25, /* R/W word */
    PMBUS_VOUT_MARGIN_LOW           = 0x26, /* R/W word */
    PMBUS_VOUT_TRANSITION_RATE      = 0x27, /* R/W word */
    PMBUS_VOUT_DROOP                = 0x28, /* R/W word */
    PMBUS_VOUT_SCALE_LOOP           = 0x29, /* R/W word */
    PMBUS_VOUT_SCALE_MONITOR        = 0x2A, /* R/W word */
    PMBUS_VOUT_MIN                  = 0x2B, /* R/W word */
    PMBUS_COEFFICIENTS              = 0x30, /* Read-only block 5 bytes */
    PMBUS_POUT_MAX                  = 0x31, /* R/W word */
    PMBUS_MAX_DUTY                  = 0x32, /* R/W word */
    PMBUS_FREQUENCY_SWITCH          = 0x33, /* R/W word */
    PMBUS_VIN_ON                    = 0x35, /* R/W word */
    PMBUS_VIN_OFF                   = 0x36, /* R/W word */
    PMBUS_INTERLEAVE                = 0x37, /* R/W word */
    PMBUS_IOUT_CAL_GAIN             = 0x38, /* R/W word */
    PMBUS_IOUT_CAL_OFFSET           = 0x39, /* R/W word */
    PMBUS_FAN_CONFIG_1_2            = 0x3A, /* R/W byte */
    PMBUS_FAN_COMMAND_1             = 0x3B, /* R/W word */
    PMBUS_FAN_COMMAND_2             = 0x3C, /* R/W word */
    PMBUS_FAN_CONFIG_3_4            = 0x3D, /* R/W byte */
    PMBUS_FAN_COMMAND_3             = 0x3E, /* R/W word */
    PMBUS_FAN_COMMAND_4             = 0x3F, /* R/W word */
    PMBUS_VOUT_OV_FAULT_LIMIT       = 0x40, /* R/W word */
    PMBUS_VOUT_OV_FAULT_RESPONSE    = 0x41, /* R/W byte */
    PMBUS_VOUT_OV_WARN_LIMIT        = 0x42, /* R/W word */
    PMBUS_VOUT_UV_WARN_LIMIT        = 0x43, /* R/W word */
    PMBUS_VOUT_UV_FAULT_LIMIT       = 0x44, /* R/W word */
    PMBUS_VOUT_UV_FAULT_RESPONSE    = 0x45, /* R/W byte */
    PMBUS_IOUT_OC_FAULT_LIMIT       = 0x46, /* R/W word */
    PMBUS_IOUT_OC_FAULT_RESPONSE    = 0x47, /* R/W byte */
    PMBUS_IOUT_OC_LV_FAULT_LIMIT    = 0x48, /* R/W word */
    PMBUS_IOUT_OC_LV_FAULT_RESPONSE = 0x49, /* R/W byte */
    PMBUS_IOUT_OC_WARN_LIMIT        = 0x4A, /* R/W word */
    PMBUS_IOUT_UC_FAULT_LIMIT       = 0x4B, /* R/W word */
    PMBUS_IOUT_UC_FAULT_RESPONSE    = 0x4C, /* R/W byte */
    PMBUS_OT_FAULT_LIMIT            = 0x4F, /* R/W word */
    PMBUS_OT_FAULT_RESPONSE         = 0x50, /* R/W byte */
    PMBUS_OT_WARN_LIMIT             = 0x51, /* R/W word */
    PMBUS_UT_WARN_LIMIT             = 0x52, /* R/W word */
    PMBUS_UT_FAULT_LIMIT            = 0x53, /* R/W word */
    PMBUS_UT_FAULT_RESPONSE         = 0x54, /* R/W byte */
    PMBUS_VIN_OV_FAULT_LIMIT        = 0x55, /* R/W word */
    PMBUS_VIN_OV_FAULT_RESPONSE     = 0x56, /* R/W byte */
    PMBUS_VIN_OV_WARN_LIMIT         = 0x57, /* R/W word */
    PMBUS_VIN_UV_WARN_LIMIT         = 0x58, /* R/W word */
    PMBUS_VIN_UV_FAULT_LIMIT        = 0x59, /* R/W word */
    PMBUS_VIN_UV_FAULT_RESPONSE     = 0x5A, /* R/W byte */
    PMBUS_IIN_OC_FAULT_LIMIT        = 0x5B, /* R/W word */
    PMBUS_IIN_OC_FAULT_RESPONSE     = 0x5C, /* R/W byte */
    PMBUS_IIN_OC_WARN_LIMIT         = 0x5D, /* R/W word */
    PMBUS_POWER_GOOD_ON             = 0x5E, /* R/W word */
    PMBUS_POWER_GOOD_OFF            = 0x5F, /* R/W word */
    PMBUS_TON_DELAY                 = 0x60, /* R/W word */
    PMBUS_TON_RISE                  = 0x61, /* R/W word */
    PMBUS_TON_MAX_FAULT_LIMIT       = 0x62, /* R/W word */
    PMBUS_TON_MAX_FAULT_RESPONSE    = 0x63, /* R/W byte */
    PMBUS_TOFF_DELAY                = 0x64, /* R/W word */
    PMBUS_TOFF_FALL                 = 0x65, /* R/W word */
    PMBUS_TOFF_MAX_WARN_LIMIT       = 0x66, /* R/W word */
    PMBUS_POUT_OP_FAULT_LIMIT       = 0x68, /* R/W word */
    PMBUS_POUT_OP_FAULT_RESPONSE    = 0x69, /* R/W byte */
    PMBUS_POUT_OP_WARN_LIMIT        = 0x6A, /* R/W word */
    PMBUS_PIN_OP_WARN_LIMIT         = 0x6B, /* R/W word */
    PMBUS_STATUS_BYTE               = 0x78, /* R/W byte */
    PMBUS_STATUS_WORD               = 0x79, /* R/W word */
    PMBUS_STATUS_VOUT               = 0x7A, /* R/W byte */
    PMBUS_STATUS_IOUT               = 0x7B, /* R/W byte */
    PMBUS_STATUS_INPUT              = 0x7C, /* R/W byte */
    PMBUS_STATUS_TEMPERATURE        = 0x7D, /* R/W byte */
    PMBUS_STATUS_CML                = 0x7E, /* R/W byte */
    PMBUS_STATUS_OTHER              = 0x7F, /* R/W byte */
    PMBUS_STATUS_MFR_SPECIFIC       = 0x80, /* R/W byte */
    PMBUS_STATUS_FANS_1_2           = 0x81, /* R/W byte */
    PMBUS_STATUS_FANS_3_4           = 0x82, /* R/W byte */
    PMBUS_READ_EIN                  = 0x86, /* Read-Only block 5 bytes */
    PMBUS_READ_EOUT                 = 0x87, /* Read-Only block 5 bytes */
    PMBUS_READ_VIN                  = 0x88, /* Read-Only word */
    PMBUS_READ_IIN                  = 0x89, /* Read-Only word */
    PMBUS_READ_VCAP                 = 0x8A, /* Read-Only word */
    PMBUS_READ_VOUT                 = 0x8B, /* Read-Only word */
    PMBUS_READ_IOUT                 = 0x8C, /* Read-Only word */
    PMBUS_READ_TEMPERATURE_1        = 0x8D, /* Read-Only word */
    PMBUS_READ_TEMPERATURE_2        = 0x8E, /* Read-Only word */
    PMBUS_READ_TEMPERATURE_3        = 0x8F, /* Read-Only word */
    PMBUS_READ_FAN_SPEED_1          = 0x90, /* Read-Only word */
    PMBUS_READ_FAN_SPEED_2          = 0x91, /* Read-Only word */
    PMBUS_READ_FAN_SPEED_3          = 0x92, /* Read-Only word */
    PMBUS_READ_FAN_SPEED_4          = 0x93, /* Read-Only word */
    PMBUS_READ_DUTY_CYCLE           = 0x94, /* Read-Only word */
    PMBUS_READ_FREQUENCY            = 0x95, /* Read-Only word */
    PMBUS_READ_POUT                 = 0x96, /* Read-Only word */
    PMBUS_READ_PIN                  = 0x97, /* Read-Only word */
    PMBUS_REVISION                  = 0x98, /* Read-Only byte */
    PMBUS_MFR_ID                    = 0x99, /* R/W block */
    PMBUS_MFR_MODEL                 = 0x9A, /* R/W block */
    PMBUS_MFR_REVISION              = 0x9B, /* R/W block */
    PMBUS_MFR_LOCATION              = 0x9C, /* R/W block */
    PMBUS_MFR_DATE                  = 0x9D, /* R/W block */
    PMBUS_MFR_SERIAL                = 0x9E, /* R/W block */
    PMBUS_APP_PROFILE_SUPPORT       = 0x9F, /* Read-Only block-read */
    PMBUS_MFR_VIN_MIN               = 0xA0, /* Read-Only word */
    PMBUS_MFR_VIN_MAX               = 0xA1, /* Read-Only word */
    PMBUS_MFR_IIN_MAX               = 0xA2, /* Read-Only word */
    PMBUS_MFR_PIN_MAX               = 0xA3, /* Read-Only word */
    PMBUS_MFR_VOUT_MIN              = 0xA4, /* Read-Only word */
    PMBUS_MFR_VOUT_MAX              = 0xA5, /* Read-Only word */
    PMBUS_MFR_IOUT_MAX              = 0xA6, /* Read-Only word */
    PMBUS_MFR_POUT_MAX              = 0xA7, /* Read-Only word */
    PMBUS_MFR_TAMBIENT_MAX          = 0xA8, /* Read-Only word */
    PMBUS_MFR_TAMBIENT_MIN          = 0xA9, /* Read-Only word */
    PMBUS_MFR_EFFICIENCY_LL         = 0xAA, /* Read-Only block 14 bytes */
    PMBUS_MFR_EFFICIENCY_HL         = 0xAB, /* Read-Only block 14 bytes */
    PMBUS_MFR_PIN_ACCURACY          = 0xAC, /* Read-Only byte */
    PMBUS_IC_DEVICE_ID              = 0xAD, /* Read-Only block-read */
    PMBUS_IC_DEVICE_REV             = 0xAE, /* Read-Only block-read */
    PMBUS_MFR_MAX_TEMP_1            = 0xC0, /* R/W word */
    PMBUS_MFR_MAX_TEMP_2            = 0xC1, /* R/W word */
    PMBUS_MFR_MAX_TEMP_3            = 0xC2, /* R/W word */
};

/* STATUS_WORD */
#define PB_STATUS_VOUT           BIT(15)
#define PB_STATUS_IOUT_POUT      BIT(14)
#define PB_STATUS_INPUT          BIT(13)
#define PB_STATUS_WORD_MFR       BIT(12)
#define PB_STATUS_POWER_GOOD_N   BIT(11)
#define PB_STATUS_FAN            BIT(10)
#define PB_STATUS_OTHER          BIT(9)
#define PB_STATUS_UNKNOWN        BIT(8)
/* STATUS_BYTE */
#define PB_STATUS_BUSY           BIT(7)
#define PB_STATUS_OFF            BIT(6)
#define PB_STATUS_VOUT_OV        BIT(5)
#define PB_STATUS_IOUT_OC        BIT(4)
#define PB_STATUS_VIN_UV         BIT(3)
#define PB_STATUS_TEMPERATURE    BIT(2)
#define PB_STATUS_CML            BIT(1)
#define PB_STATUS_NONE_ABOVE     BIT(0)

/* STATUS_VOUT */
#define PB_STATUS_VOUT_OV_FAULT         BIT(7) /* Output Overvoltage Fault */
#define PB_STATUS_VOUT_OV_WARN          BIT(6) /* Output Overvoltage Warning */
#define PB_STATUS_VOUT_UV_WARN          BIT(5) /* Output Undervoltage Warning */
#define PB_STATUS_VOUT_UV_FAULT         BIT(4) /* Output Undervoltage Fault */
#define PB_STATUS_VOUT_MAX              BIT(3)
#define PB_STATUS_VOUT_TON_MAX_FAULT    BIT(2)
#define PB_STATUS_VOUT_TOFF_MAX_WARN    BIT(1)

/* STATUS_IOUT */
#define PB_STATUS_IOUT_OC_FAULT    BIT(7) /* Output Overcurrent Fault */
#define PB_STATUS_IOUT_OC_LV_FAULT BIT(6) /* Output OC And Low Voltage Fault */
#define PB_STATUS_IOUT_OC_WARN     BIT(5) /* Output Overcurrent Warning */
#define PB_STATUS_IOUT_UC_FAULT    BIT(4) /* Output Undercurrent Fault */
#define PB_STATUS_CURR_SHARE       BIT(3) /* Current Share Fault */
#define PB_STATUS_PWR_LIM_MODE     BIT(2) /* In Power Limiting Mode */
#define PB_STATUS_POUT_OP_FAULT    BIT(1) /* Output Overpower Fault */
#define PB_STATUS_POUT_OP_WARN     BIT(0) /* Output Overpower Warning */

/* STATUS_INPUT */
#define PB_STATUS_INPUT_VIN_OV_FAULT    BIT(7) /* Input Overvoltage Fault */
#define PB_STATUS_INPUT_VIN_OV_WARN     BIT(6) /* Input Overvoltage Warning */
#define PB_STATUS_INPUT_VIN_UV_WARN     BIT(5) /* Input Undervoltage Warning */
#define PB_STATUS_INPUT_VIN_UV_FAULT    BIT(4) /* Input Undervoltage Fault */
#define PB_STATUS_INPUT_IIN_OC_FAULT    BIT(2) /* Input Overcurrent Fault */
#define PB_STATUS_INPUT_IIN_OC_WARN     BIT(1) /* Input Overcurrent Warning */
#define PB_STATUS_INPUT_PIN_OP_WARN     BIT(0) /* Input Overpower Warning */

/* STATUS_TEMPERATURE */
#define PB_STATUS_OT_FAULT              BIT(7) /* Overtemperature Fault */
#define PB_STATUS_OT_WARN               BIT(6) /* Overtemperature Warning */
#define PB_STATUS_UT_WARN               BIT(5) /* Undertemperature Warning */
#define PB_STATUS_UT_FAULT              BIT(4) /* Undertemperature Fault */

/* STATUS_CML */
#define PB_CML_FAULT_INVALID_CMD     BIT(7) /* Invalid/Unsupported Command */
#define PB_CML_FAULT_INVALID_DATA    BIT(6) /* Invalid/Unsupported Data  */
#define PB_CML_FAULT_PEC             BIT(5) /* Packet Error Check Failed */
#define PB_CML_FAULT_MEMORY          BIT(4) /* Memory Fault Detected */
#define PB_CML_FAULT_PROCESSOR       BIT(3) /* Processor Fault Detected */
#define PB_CML_FAULT_OTHER_COMM      BIT(1) /* Other communication fault */
#define PB_CML_FAULT_OTHER_MEM_LOGIC BIT(0) /* Other Memory Or Logic Fault */

/* OPERATION*/
#define PB_OP_ON                BIT(7) /* PSU is switched on */
#define PB_OP_MARGIN_HIGH       BIT(5) /* PSU vout is set to margin high */
#define PB_OP_MARGIN_LOW        BIT(4) /* PSU vout is set to margin low */

/* PAGES */
#define PB_MAX_PAGES            0x1F
#define PB_ALL_PAGES            0xFF

#define PMBUS_ERR_BYTE          0xFF

#define TYPE_PMBUS_DEVICE "pmbus-device"
OBJECT_DECLARE_TYPE(PMBusDevice, PMBusDeviceClass,
                    PMBUS_DEVICE)

/* flags */
#define PB_HAS_COEFFICIENTS        BIT_ULL(9)
#define PB_HAS_VIN                 BIT_ULL(10)
#define PB_HAS_VOUT                BIT_ULL(11)
#define PB_HAS_VOUT_MARGIN         BIT_ULL(12)
#define PB_HAS_VIN_RATING          BIT_ULL(13)
#define PB_HAS_VOUT_RATING         BIT_ULL(14)
#define PB_HAS_VOUT_MODE           BIT_ULL(15)
#define PB_HAS_IOUT                BIT_ULL(21)
#define PB_HAS_IIN                 BIT_ULL(22)
#define PB_HAS_IOUT_RATING         BIT_ULL(23)
#define PB_HAS_IIN_RATING          BIT_ULL(24)
#define PB_HAS_IOUT_GAIN           BIT_ULL(25)
#define PB_HAS_POUT                BIT_ULL(30)
#define PB_HAS_PIN                 BIT_ULL(31)
#define PB_HAS_EIN                 BIT_ULL(32)
#define PB_HAS_EOUT                BIT_ULL(33)
#define PB_HAS_POUT_RATING         BIT_ULL(34)
#define PB_HAS_PIN_RATING          BIT_ULL(35)
#define PB_HAS_TEMPERATURE         BIT_ULL(40)
#define PB_HAS_TEMP2               BIT_ULL(41)
#define PB_HAS_TEMP3               BIT_ULL(42)
#define PB_HAS_TEMP_RATING         BIT_ULL(43)
#define PB_HAS_MFR_INFO            BIT_ULL(50)
#define PB_HAS_STATUS_MFR_SPECIFIC BIT_ULL(51)

struct PMBusDeviceClass {
    SMBusDeviceClass parent_class;
    uint8_t device_num_pages;

    /**
     * Implement quick_cmd, receive byte, and write_data to support non-standard
     * PMBus functionality
     */
    void (*quick_cmd)(PMBusDevice *dev, uint8_t read);
    int (*write_data)(PMBusDevice *dev, const uint8_t *buf, uint8_t len);
    uint8_t (*receive_byte)(PMBusDevice *dev);
};

/*
 * According to the spec, each page may offer the full range of PMBus commands
 * available for each output or non-PMBus device.
 * Therefore, we can't assume that any registers will always be the same across
 * all pages.
 * The page 0xFF is intended for writes to all pages
 */
typedef struct PMBusPage {
    uint64_t page_flags;

    uint8_t page;                      /* R/W byte */
    uint8_t operation;                 /* R/W byte */
    uint8_t on_off_config;             /* R/W byte */
    uint8_t write_protect;             /* R/W byte */
    uint8_t phase;                     /* R/W byte */
    uint8_t vout_mode;                 /* R/W byte */
    uint16_t vout_command;             /* R/W word */
    uint16_t vout_trim;                /* R/W word */
    uint16_t vout_cal_offset;          /* R/W word */
    uint16_t vout_max;                 /* R/W word */
    uint16_t vout_margin_high;         /* R/W word */
    uint16_t vout_margin_low;          /* R/W word */
    uint16_t vout_transition_rate;     /* R/W word */
    uint16_t vout_droop;               /* R/W word */
    uint16_t vout_scale_loop;          /* R/W word */
    uint16_t vout_scale_monitor;       /* R/W word */
    uint16_t vout_min;                 /* R/W word */
    uint8_t coefficients[5];           /* Read-only block 5 bytes */
    uint16_t pout_max;                 /* R/W word */
    uint16_t max_duty;                 /* R/W word */
    uint16_t frequency_switch;         /* R/W word */
    uint16_t vin_on;                   /* R/W word */
    uint16_t vin_off;                  /* R/W word */
    uint16_t iout_cal_gain;            /* R/W word */
    uint16_t iout_cal_offset;          /* R/W word */
    uint8_t fan_config_1_2;            /* R/W byte */
    uint16_t fan_command_1;            /* R/W word */
    uint16_t fan_command_2;            /* R/W word */
    uint8_t fan_config_3_4;            /* R/W byte */
    uint16_t fan_command_3;            /* R/W word */
    uint16_t fan_command_4;            /* R/W word */
    uint16_t vout_ov_fault_limit;      /* R/W word */
    uint8_t vout_ov_fault_response;    /* R/W byte */
    uint16_t vout_ov_warn_limit;       /* R/W word */
    uint16_t vout_uv_warn_limit;       /* R/W word */
    uint16_t vout_uv_fault_limit;      /* R/W word */
    uint8_t vout_uv_fault_response;    /* R/W byte */
    uint16_t iout_oc_fault_limit;      /* R/W word */
    uint8_t iout_oc_fault_response;    /* R/W byte */
    uint16_t iout_oc_lv_fault_limit;   /* R/W word */
    uint8_t iout_oc_lv_fault_response; /* R/W byte */
    uint16_t iout_oc_warn_limit;       /* R/W word */
    uint16_t iout_uc_fault_limit;      /* R/W word */
    uint8_t iout_uc_fault_response;    /* R/W byte */
    uint16_t ot_fault_limit;           /* R/W word */
    uint8_t ot_fault_response;         /* R/W byte */
    uint16_t ot_warn_limit;            /* R/W word */
    uint16_t ut_warn_limit;            /* R/W word */
    uint16_t ut_fault_limit;           /* R/W word */
    uint8_t ut_fault_response;         /* R/W byte */
    uint16_t vin_ov_fault_limit;       /* R/W word */
    uint8_t vin_ov_fault_response;     /* R/W byte */
    uint16_t vin_ov_warn_limit;        /* R/W word */
    uint16_t vin_uv_warn_limit;        /* R/W word */
    uint16_t vin_uv_fault_limit;       /* R/W word */
    uint8_t vin_uv_fault_response;     /* R/W byte */
    uint16_t iin_oc_fault_limit;       /* R/W word */
    uint8_t iin_oc_fault_response;     /* R/W byte */
    uint16_t iin_oc_warn_limit;        /* R/W word */
    uint16_t power_good_on;            /* R/W word */
    uint16_t power_good_off;           /* R/W word */
    uint16_t ton_delay;                /* R/W word */
    uint16_t ton_rise;                 /* R/W word */
    uint16_t ton_max_fault_limit;      /* R/W word */
    uint8_t ton_max_fault_response;    /* R/W byte */
    uint16_t toff_delay;               /* R/W word */
    uint16_t toff_fall;                /* R/W word */
    uint16_t toff_max_warn_limit;      /* R/W word */
    uint16_t pout_op_fault_limit;      /* R/W word */
    uint8_t pout_op_fault_response;    /* R/W byte */
    uint16_t pout_op_warn_limit;       /* R/W word */
    uint16_t pin_op_warn_limit;        /* R/W word */
    uint16_t status_word;              /* R/W word */
    uint8_t status_vout;               /* R/W byte */
    uint8_t status_iout;               /* R/W byte */
    uint8_t status_input;              /* R/W byte */
    uint8_t status_temperature;        /* R/W byte */
    uint8_t status_cml;                /* R/W byte */
    uint8_t status_other;              /* R/W byte */
    uint8_t status_mfr_specific;       /* R/W byte */
    uint8_t status_fans_1_2;           /* R/W byte */
    uint8_t status_fans_3_4;           /* R/W byte */
    uint8_t read_ein[5];               /* Read-Only block 5 bytes */
    uint8_t read_eout[5];              /* Read-Only block 5 bytes */
    uint16_t read_vin;                 /* Read-Only word */
    uint16_t read_iin;                 /* Read-Only word */
    uint16_t read_vcap;                /* Read-Only word */
    uint16_t read_vout;                /* Read-Only word */
    uint16_t read_iout;                /* Read-Only word */
    uint16_t read_temperature_1;       /* Read-Only word */
    uint16_t read_temperature_2;       /* Read-Only word */
    uint16_t read_temperature_3;       /* Read-Only word */
    uint16_t read_fan_speed_1;         /* Read-Only word */
    uint16_t read_fan_speed_2;         /* Read-Only word */
    uint16_t read_fan_speed_3;         /* Read-Only word */
    uint16_t read_fan_speed_4;         /* Read-Only word */
    uint16_t read_duty_cycle;          /* Read-Only word */
    uint16_t read_frequency;           /* Read-Only word */
    uint16_t read_pout;                /* Read-Only word */
    uint16_t read_pin;                 /* Read-Only word */
    uint8_t revision;                  /* Read-Only byte */
    const char *mfr_id;                /* R/W block */
    const char *mfr_model;             /* R/W block */
    const char *mfr_revision;          /* R/W block */
    const char *mfr_location;          /* R/W block */
    const char *mfr_date;              /* R/W block */
    const char *mfr_serial;            /* R/W block */
    const char *app_profile_support;   /* Read-Only block-read */
    uint16_t mfr_vin_min;              /* Read-Only word */
    uint16_t mfr_vin_max;              /* Read-Only word */
    uint16_t mfr_iin_max;              /* Read-Only word */
    uint16_t mfr_pin_max;              /* Read-Only word */
    uint16_t mfr_vout_min;             /* Read-Only word */
    uint16_t mfr_vout_max;             /* Read-Only word */
    uint16_t mfr_iout_max;             /* Read-Only word */
    uint16_t mfr_pout_max;             /* Read-Only word */
    uint16_t mfr_tambient_max;         /* Read-Only word */
    uint16_t mfr_tambient_min;         /* Read-Only word */
    uint8_t mfr_efficiency_ll[14];     /* Read-Only block 14 bytes */
    uint8_t mfr_efficiency_hl[14];     /* Read-Only block 14 bytes */
    uint8_t mfr_pin_accuracy;          /* Read-Only byte */
    uint16_t mfr_max_temp_1;           /* R/W word */
    uint16_t mfr_max_temp_2;           /* R/W word */
    uint16_t mfr_max_temp_3;           /* R/W word */
} PMBusPage;

/* State */
struct PMBusDevice {
    SMBusDevice smb;

    uint8_t num_pages;
    uint8_t code;
    uint8_t page;

    /*
     * PMBus registers are stored in a PMBusPage structure allocated by
     * calling pmbus_pages_alloc()
     */
    PMBusPage *pages;
    uint8_t capability;


    int32_t in_buf_len;
    uint8_t *in_buf;
    int32_t out_buf_len;
    uint8_t out_buf[SMBUS_DATA_MAX_LEN];
};

/**
 * Direct mode coefficients
 * @var m - mantissa
 * @var b - offset
 * @var R - exponent
 */
typedef struct PMBusCoefficients {
    int32_t m;     /* mantissa */
    int64_t b;     /* offset */
    int32_t R;     /* exponent */
} PMBusCoefficients;

/**
 * Convert sensor values to direct mode format
 *
 * Y = (m * x - b) * 10^R
 *
 * @return uint16_t
 */
uint16_t pmbus_data2direct_mode(PMBusCoefficients c, uint32_t value);

/**
 * Convert direct mode formatted data into sensor reading
 *
 * X = (Y * 10^-R - b) / m
 *
 * @return uint32_t
 */
uint32_t pmbus_direct_mode2data(PMBusCoefficients c, uint16_t value);

/**
 * Convert sensor values to linear mode format
 *
 * L = D * 2^(-e)
 *
 * @return uint16
 */
uint16_t pmbus_data2linear_mode(uint16_t value, int exp);

/**
 * Convert linear mode formatted data into sensor reading
 *
 * D = L * 2^e
 *
 * @return uint16
 */
uint16_t pmbus_linear_mode2data(uint16_t value, int exp);

/**
 * @brief Send a block of data over PMBus
 * Assumes that the bytes in the block are already ordered correctly,
 * also assumes the length has been prepended to the block if necessary
 *     | low_byte | ... | high_byte |
 * @param state - maintains state of the PMBus device
 * @param data - byte array to be sent by device
 * @param len - number
 */
void pmbus_send(PMBusDevice *state, const uint8_t *data, uint16_t len);
void pmbus_send8(PMBusDevice *state, uint8_t data);
void pmbus_send16(PMBusDevice *state, uint16_t data);
void pmbus_send32(PMBusDevice *state, uint32_t data);
void pmbus_send64(PMBusDevice *state, uint64_t data);

/**
 * @brief Send a string over PMBus with length prepended.
 * Length is calculated using str_len()
 */
void pmbus_send_string(PMBusDevice *state, const char *data);

/**
 * @brief Receive data over PMBus
 * These methods help track how much data is being received over PMBus
 * Log to GUEST_ERROR if too much or too little is sent.
 */
uint8_t pmbus_receive8(PMBusDevice *pmdev);
uint16_t pmbus_receive16(PMBusDevice *pmdev);
uint32_t pmbus_receive32(PMBusDevice *pmdev);
uint64_t pmbus_receive64(PMBusDevice *pmdev);

/**
 * PMBus page config must be called before any page is first used.
 * It will allocate memory for all the pages if needed.
 * Passed in flags overwrite existing flags if any.
 * @param page_index the page to which the flags are applied, setting page_index
 * to 0xFF applies the passed in flags to all pages.
 * @param flags
 */
int pmbus_page_config(PMBusDevice *pmdev, uint8_t page_index, uint64_t flags);

/**
 * Update the status registers when sensor values change.
 * Useful if modifying sensors through qmp, this way status registers get
 * updated
 */
void pmbus_check_limits(PMBusDevice *pmdev);

extern const VMStateDescription vmstate_pmbus_device;

#define VMSTATE_PMBUS_DEVICE(_field, _state) {                       \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(PMBusDevice),                               \
    .vmsd       = &vmstate_pmbus_device,                             \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, PMBusDevice), \
}

#endif
