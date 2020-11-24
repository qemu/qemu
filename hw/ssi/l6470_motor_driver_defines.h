/**
 * @file l6470_motor_driver_defines.h
 *
 * @brief Contains L6470 Motor Driver Definitions.
 *
 * @copyright © Copyright 2018 Nanosonics Limited ACN 095 076 896.
 *            All rights reserved.
 *            This is unpublished proprietary source code of Nanosonics.
 *            The copyright notice does not evidence any actual or intended publication of this source code.
 */

#ifndef L6470_MOTOR_DRIVER_DEFINES_H
#define L6470_MOTOR_DRIVER_DEFINES_H

#include <stdint.h>

/*******************************************************************************
 * Motor Driver Register Information
 ******************************************************************************/

/*! Motor Driver Register map */
typedef enum {
    MOTOR_REG_ADDR_ABS_POS          = 0x01,
    MOTOR_REG_ADDR_EL_POS           = 0x02,
    MOTOR_REG_ADDR_MARK             = 0x03,
    MOTOR_REG_ADDR_SPEED            = 0x04,
    MOTOR_REG_ADDR_ACC              = 0x05,
    MOTOR_REG_ADDR_DEC              = 0x06,
    MOTOR_REG_ADDR_MAX_SPEED        = 0x07,
    MOTOR_REG_ADDR_MIN_SPEED        = 0x08,
    MOTOR_REG_ADDR_FS_SPD           = 0x15,
    MOTOR_REG_ADDR_KVAL_HOLD        = 0x09,
    MOTOR_REG_ADDR_KVAL_RUN         = 0x0A,
    MOTOR_REG_ADDR_KVAL_ACC         = 0x0B,
    MOTOR_REG_ADDR_KVAL_DEC         = 0x0C,
    MOTOR_REG_ADDR_INT_SPD          = 0x0D,
    MOTOR_REG_ADDR_ST_SLP           = 0x0E,
    MOTOR_REG_ADDR_FN_SLP_ACC       = 0x0F,
    MOTOR_REG_ADDR_FN_SLP_DEC       = 0x10,
    MOTOR_REG_ADDR_K_THERM          = 0x11,
    MOTOR_REG_ADDR_ADC_OUT          = 0x12,
    MOTOR_REG_ADDR_OCD_TH           = 0x13,
    MOTOR_REG_ADDR_STALL_TH         = 0x14,
    MOTOR_REG_ADDR_STEP_MODE        = 0x16,
    MOTOR_REG_ADDR_ALARM_EN         = 0x17,
    MOTOR_REG_ADDR_CONFIG           = 0x18,
    MOTOR_REG_ADDR_STATUS           = 0x19,
    MOTOR_REG_ADDR_RESERVED_REG1    = 0x1A,
    MOTOR_REG_ADDR_RESERVED_REG2    = 0x1B
} MotorDriverRegisters;


typedef enum {
    MOTOR_REG_ADDR_R,           //Readable
    MOTOR_REG_ADDR_WR,          //Always writable
    MOTOR_REG_ADDR_WH,          //Writable only when outputs are high impedance
    MOTOR_REG_ADDR_WS,          //Writable only when motor is stopped
    MOTOR_REG_ADDR_INVALID,
} MotorDriverRegistersGroup;




/*! Motor Driver Electrical Position Register bit masks */
typedef enum {
    MOTOR_EL_POS_MICROSTEP_MASK    = 0x007F,
    MOTOR_EL_POS_STEP_MASK         = 0x0180
} MotorDriverElPosMask;

/*! Motor Driver Electrical Position Register bit shift */
typedef enum {
    MOTOR_EL_POS_MICROSTEP_SHIFT    = 0,
    MOTOR_EL_POS_STEP_SHIFT         = 7
} MotorDriverElPosShift;

/*! Motor Driver Minimum Speed Register bit mask */
typedef enum {
    MOTOR_MIN_SPEED_MASK    = 0x0FFF,
    MOTOR_LSPD_OPT_MASK     = 0x1000
} MotorDriverMinSpeedMask;


/*! Motor Driver over current threshold options */
typedef enum {
    MOTOR_OCD_TH_375mA     = 0x00,
    MOTOR_OCD_TH_750mA     = 0x01,
    MOTOR_OCD_TH_1125mA    = 0x02,
    MOTOR_OCD_TH_1500mA    = 0x03,
    MOTOR_OCD_TH_1875mA    = 0x04,
    MOTOR_OCD_TH_2250mA    = 0x05,
    MOTOR_OCD_TH_2625mA    = 0x06,
    MOTOR_OCD_TH_3000mA    = 0x07,
    MOTOR_OCD_TH_3375mA    = 0x08,
    MOTOR_OCD_TH_3750mA    = 0x09,
    MOTOR_OCD_TH_4125mA    = 0x0A,
    MOTOR_OCD_TH_4500mA    = 0x0B,
    MOTOR_OCD_TH_4875mA    = 0x0C,
    MOTOR_OCD_TH_5250mA    = 0x0D,
    MOTOR_OCD_TH_5625mA    = 0x0E,
    MOTOR_OCD_TH_6000mA    = 0x0F
} MotorDriverOCDThreshold;

/*! Motor Driver Step Mode Register masks */
typedef enum {
    MOTOR_STEP_MODE_STEP_SEL_MASK    = 0x07,
    MOTOR_STEP_MODE_SYNC_SEL_MASK    = 0x70,
    MOTOR_STEP_MODE_SYNC_EN_MASK     = 0x80
} MotorDriverStepModeMask;

/*! Motor Driver Step Mode Register Step Select options */
typedef enum {
    MOTOR_STEP_MODE_STEP_SEL_1        = 0x00,
    MOTOR_STEP_MODE_STEP_SEL_1_2      = 0x01,
    MOTOR_STEP_MODE_STEP_SEL_1_4      = 0x02,
    MOTOR_STEP_MODE_STEP_SEL_1_8      = 0x03,
    MOTOR_STEP_MODE_STEP_SEL_1_16     = 0x04,
    MOTOR_STEP_MODE_STEP_SEL_1_32     = 0x05,
    MOTOR_STEP_MODE_STEP_SEL_1_64     = 0x06,
    MOTOR_STEP_MODE_STEP_SEL_1_128    = 0x07
} MotorDriverStepModeStepSelect;

/*! Motor Driver Step Mode Register Sync Select options */
typedef enum {
    MOTOR_STEP_MODE_SYNC_SEL_DISABLED    = 0x00,
    MOTOR_STEP_MODE_SYNC_SEL_ENABLED     = 0x80,
    MOTOR_STEP_MODE_SYNC_SEL_1_2         = (MOTOR_STEP_MODE_SYNC_SEL_ENABLED|0x00),
    MOTOR_STEP_MODE_SYNC_SEL_1           = (MOTOR_STEP_MODE_SYNC_SEL_ENABLED|0x10),
    MOTOR_STEP_MODE_SYNC_SEL_2           = (MOTOR_STEP_MODE_SYNC_SEL_ENABLED|0x20),
    MOTOR_STEP_MODE_SYNC_SEL_4           = (MOTOR_STEP_MODE_SYNC_SEL_ENABLED|0x30),
    MOTOR_STEP_MODE_SYNC_SEL_8           = (MOTOR_STEP_MODE_SYNC_SEL_ENABLED|0x40),
    MOTOR_STEP_MODE_SYNC_SEL_16          = (MOTOR_STEP_MODE_SYNC_SEL_ENABLED|0x50),
    MOTOR_STEP_MODE_SYNC_SEL_32          = (MOTOR_STEP_MODE_SYNC_SEL_ENABLED|0x60),
    MOTOR_STEP_MODE_SYNC_SEL_64          = (MOTOR_STEP_MODE_SYNC_SEL_ENABLED|0x70)
} MotorDriverStepModeSyncSelect;

/*! Motor Driver Alarm Enable Register bit masks */
typedef enum {
    MOTOR_ALARM_EN_OVERCURRENT_MASK         = 0x01,
    MOTOR_ALARM_EN_THERMAL_SHUTDOWN_MASK    = 0x02,
    MOTOR_ALARM_EN_THERMAL_WARNING_MASK     = 0x04,
    MOTOR_ALARM_EN_UNDER_VOLTAGE_MASK       = 0x08,
    MOTOR_ALARM_EN_STALL_DET_A_MASK         = 0x10,
    MOTOR_ALARM_EN_STALL_DET_B_MASK         = 0x20,
    MOTOR_ALARM_EN_SW_TURN_ON_MASK          = 0x40,
    MOTOR_ALARM_EN_WRONG_NPERF_CMD_MASK     = 0x80
} MotorDriverAlarmEnableRegisterMask;

/*! Motor Driver Config Register bit masks */
typedef enum {
    MOTOR_CONFIG_OSC_SEL_MASK      = 0x0007,
    MOTOR_CONFIG_EXT_CLK_MASK      = 0x0008,
    MOTOR_CONFIG_SW_MODE_MASK      = 0x0010,
    MOTOR_CONFIG_EN_VSCOMP_MASK    = 0x0020,
    MOTOR_CONFIG_OC_SD_MASK        = 0x0080,
    MOTOR_CONFIG_POW_SR_MASK       = 0x0300,
    MOTOR_CONFIG_F_PWM_DEC_MASK    = 0x1C00,
    MOTOR_CONFIG_F_PWM_INT_MASK    = 0xE000
} MotorDriverConfigRegisterMask;

/*! Motor Driver Config Register bit shift */
typedef enum {
    MOTOR_CONFIG_OSC_SEL_SHIFT      = 0,
    MOTOR_CONFIG_EXT_CLK_SHIFT      = 3,
    MOTOR_CONFIG_SW_MODE_SHIFT      = 4,
    MOTOR_CONFIG_EN_VSCOMP_SHIFT    = 5,
    MOTOR_CONFIG_OC_SD_SHIFT        = 7,
    MOTOR_CONFIG_POW_SR_SHIFT       = 8,
    MOTOR_CONFIG_F_PWM_DEC_SHIFT    = 10,
    MOTOR_CONFIG_F_PWM_INT_SHIFT    = 13
} MotorDriverConfigRegisterShift;

/*! Motor Driver Config Register oscillator select options */
typedef enum {
    MOTOR_CONFIG_INT_16MHZ                  = 0x0000,
    MOTOR_CONFIG_INT_16MHZ_OSCOUT_2MHZ      = 0x0008,
    MOTOR_CONFIG_INT_16MHZ_OSCOUT_4MHZ      = 0x0009,
    MOTOR_CONFIG_INT_16MHZ_OSCOUT_8MHZ      = 0x000A,
    MOTOR_CONFIG_INT_16MHZ_OSCOUT_16MHZ     = 0x000B,
    MOTOR_CONFIG_EXT_8MHZ_XTAL_DRIVE        = 0x0004,
    MOTOR_CONFIG_EXT_16MHZ_XTAL_DRIVE       = 0x0005,
    MOTOR_CONFIG_EXT_24MHZ_XTAL_DRIVE       = 0x0006,
    MOTOR_CONFIG_EXT_32MHZ_XTAL_DRIVE       = 0x0007,
    MOTOR_CONFIG_EXT_8MHZ_OSCOUT_INVERT     = 0x000C,
    MOTOR_CONFIG_EXT_16MHZ_OSCOUT_INVERT    = 0x000D,
    MOTOR_CONFIG_EXT_24MHZ_OSCOUT_INVERT    = 0x000E,
    MOTOR_CONFIG_EXT_32MHZ_OSCOUT_INVERT    = 0x000F
} MotorDriverConfigRegisterOscillatorSelect;

/*! Motor Driver Config Register switch mode option */
typedef enum {
    MOTOR_CONFIG_SW_MODE_HARD_STOP   = 0x0000,
    MOTOR_CONFIG_SW_MODE_USER        = 0x0010
} MotorDriverConfigRegisterSwitchMode;

/*! Motor Driver Config Register supply voltage compensation */
typedef enum {
    MOTOR_CONFIG_VS_COMP_DISABLE    = 0x0000,
    MOTOR_CONFIG_VS_COMP_ENABLE     = 0x0020
} MotorDriverConfigRegisterSupplyVoltageCompensation;

/*! Motor Driver Config Register over current shutdown option */
typedef enum {
    MOTOR_CONFIG_OC_SD_DISABLE    = 0x0000,
    MOTOR_CONFIG_OC_SD_ENABLE     = 0x0080
} MotorDriverConfigRegisterOverCurrentShutdown;

/*! Motor Driver Config Register power bdrige slew rate option */
typedef enum {
    MOTOR_CONFIG_POW_SR_320V_us    = 0x0000,
    MOTOR_CONFIG_POW_SR_075V_us    = 0x0100,
    MOTOR_CONFIG_POW_SR_110V_us    = 0x0200,
    MOTOR_CONFIG_POW_SR_260V_us    = 0x0300
} MotorDriverConfigRegisterPowerBridgeSlewRate;

/*! Motor Driver Config Register PWM integer division factor */
typedef enum {
    MOTOR_CONFIG_PWM_DIV_1    = ((0x00) << MOTOR_CONFIG_F_PWM_INT_SHIFT),
    MOTOR_CONFIG_PWM_DIV_2    = ((0x01) << MOTOR_CONFIG_F_PWM_INT_SHIFT),
    MOTOR_CONFIG_PWM_DIV_3    = ((0x02) << MOTOR_CONFIG_F_PWM_INT_SHIFT),
    MOTOR_CONFIG_PWM_DIV_4    = ((0x03) << MOTOR_CONFIG_F_PWM_INT_SHIFT),
    MOTOR_CONFIG_PWM_DIV_5    = ((0x04) << MOTOR_CONFIG_F_PWM_INT_SHIFT),
    MOTOR_CONFIG_PWM_DIV_6    = ((0x05) << MOTOR_CONFIG_F_PWM_INT_SHIFT),
    MOTOR_CONFIG_PWM_DIV_7    = ((0x06) << MOTOR_CONFIG_F_PWM_INT_SHIFT)
} MotorDriverConfigRegisterPWMDivisionFactor;

/*! Motor Driver Config Register PWM multiplication factor */
typedef enum {
    MOTOR_CONFIG_PWM_MUL_0_625    = ((0x00) << MOTOR_CONFIG_F_PWM_DEC_SHIFT),
    MOTOR_CONFIG_PWM_MUL_0_75     = ((0x01) << MOTOR_CONFIG_F_PWM_DEC_SHIFT),
    MOTOR_CONFIG_PWM_MUL_0_875    = ((0x02) << MOTOR_CONFIG_F_PWM_DEC_SHIFT),
    MOTOR_CONFIG_PWM_MUL_1        = ((0x03) << MOTOR_CONFIG_F_PWM_DEC_SHIFT),
    MOTOR_CONFIG_PWM_MUL_1_25     = ((0x04) << MOTOR_CONFIG_F_PWM_DEC_SHIFT),
    MOTOR_CONFIG_PWM_MUL_1_5      = ((0x05) << MOTOR_CONFIG_F_PWM_DEC_SHIFT),
    MOTOR_CONFIG_PWM_MUL_1_75     = ((0x06) << MOTOR_CONFIG_F_PWM_DEC_SHIFT),
    MOTOR_CONFIG_PWM_MUL_2        = ((0x07) << MOTOR_CONFIG_F_PWM_DEC_SHIFT)
} MotorDriverConfigRegisterPWMMultiplicationFactor;

/*! Motor Driver Status Register bit masks */
typedef enum {
    MOTOR_STATUS_HIZ_MASK            = 0x0001,
    MOTOR_STATUS_BUSY_MASK           = 0x0002,
    MOTOR_STATUS_SW_F_MASK           = 0x0004,
    MOTOR_STATUS_SW_EVN_MASK         = 0x0008,
    MOTOR_STATUS_DIR_MASK            = 0x0010,
    MOTOR_STATUS_MOT_STATUS_MASK     = 0x0060,
    MOTOR_STATUS_NOTPERF_CMD_MASK    = 0x0080,
    MOTOR_STATUS_WRONG_CMD_MASK      = 0x0100,
    MOTOR_STATUS_UVLO_MASK           = 0x0200,
    MOTOR_STATUS_TH_WRN_MASK         = 0x0400,
    MOTOR_STATUS_TH_SD_MASK          = 0x0800,
    MOTOR_STATUS_OCD_MASK            = 0x1000,
    MOTOR_STATUS_STEP_LOSS_A_MASK    = 0x2000,
    MOTOR_STATUS_STEP_LOSS_B_MASK    = 0x4000,
    MOTOR_STATUS_SCK_MOD_MASK        = 0x8000
} MotorDriverStatusRegisterMask;

/*! Motor Driver Status Register bit shift */
typedef enum {
    MOTOR_STATUS_HIZ_SHIFT            = 0,
    MOTOR_STATUS_BUSY_SHIFT           = 1,
    MOTOR_STATUS_SW_F_SHIFT           = 2,
    MOTOR_STATUS_SW_EVN_SHIFT         = 3,
    MOTOR_STATUS_DIR_SHIFT            = 4,
    MOTOR_STATUS_MOT_STATUS_SHIFT     = 5,
    MOTOR_STATUS_NOTPERF_CMD_SHIFT    = 7,
    MOTOR_STATUS_WRONG_CMD_SHIFT      = 8,
    MOTOR_STATUS_UVLO_SHIFT           = 9,
    MOTOR_STATUS_TH_WRN_SHIFT         = 10,
    MOTOR_STATUS_TH_SD_SHIFT          = 11,
    MOTOR_STATUS_OCD_SHIFT            = 12,
    MOTOR_STATUS_STEP_LOSS_A_SHIFT    = 13,
    MOTOR_STATUS_STEP_LOSS_B_SHIFT    = 14,
    MOTOR_STATUS_SCK_MOD_SHIFT        = 15
} MotorDriverStatusRegisterShift;

/*! Motor Driver Status Register motor states */
typedef enum {
    MOTOR_STATUS_MOT_STATUS_STOPPED         = ((0x0000) << MOTOR_STATUS_MOT_STATUS_SHIFT),
    MOTOR_STATUS_MOT_STATUS_ACCELERATION    = ((0x0001) << MOTOR_STATUS_MOT_STATUS_SHIFT),
    MOTOR_STATUS_MOT_STATUS_DECELERATION    = ((0x0002) << MOTOR_STATUS_MOT_STATUS_SHIFT),
    MOTOR_STATUS_MOT_STATUS_CONST_SPD       = ((0x0003) << MOTOR_STATUS_MOT_STATUS_SHIFT)
} MotorDriverStatusRegisterMotorStatus;

/*******************************************************************************
 * Motor Driver Commands
 ******************************************************************************/

/*! Motor Driver Commands */
typedef enum {
    MOTOR_CMD_NOP              = 0x00,
    MOTOR_CMD_SET_PARAM        = 0x00,
    MOTOR_CMD_GET_PARAM        = 0x20,
    MOTOR_CMD_STEP_CLOCK       = 0x58,
    MOTOR_CMD_RUN              = 0x50,
    MOTOR_CMD_MOVE             = 0x40,
    MOTOR_CMD_GO_TO            = 0x60,
    MOTOR_CMD_GO_TO_DIR        = 0x68,
    MOTOR_CMD_GO_UNTIL         = 0x82,
    MOTOR_CMD_RELEASE_SW       = 0x92,
    MOTOR_CMD_GO_HOME          = 0x70,
    MOTOR_CMD_GO_MARK          = 0x78,
    MOTOR_CMD_RESET_POS        = 0xD8,
    MOTOR_CMD_RESET_DEVICE     = 0xC0,
    MOTOR_CMD_SOFT_STOP        = 0xB0,
    MOTOR_CMD_HARD_STOP        = 0xB8,
    MOTOR_CMD_SOFT_HIZ         = 0xA0,
    MOTOR_CMD_HARD_HIZ         = 0xA8,
    MOTOR_CMD_GET_STATUS       = 0xD0,
    MOTOR_CMD_RESERVED_CMD1    = 0xEB,
    MOTOR_CMD_RESERVED_CMD2    = 0xF8
} MotorDriverCommands;

/*! Motor Driver motor move direction */
typedef enum {
    MOTOR_FWD    = 0x01,
    MOTOR_REV    = 0x00
} MotorDirection;

/*! Motor Driver action options */
typedef enum {
    MOTOR_ACTION_RESET    = 0x00,
    MOTOR_ACTION_COPY     = 0x08
} MotorAction;

typedef enum
{
    MOTOR_COMMAND_FAILED,
    MOTOR_COMMAND_SUCCESS
} MotorCommandStatus;

/*******************************************************************************
 * Motor Driver Command and Respponse Lengths
 ******************************************************************************/

/*! Maximum length of command and response */
#define MOTOR_CMD_RSP_MAX_LENGTH            (4)

/*! Command Length */
#define MOTOR_CMD_SIZE_NOP                  (1)
#define MOTOR_CMD_SIZE_SET_PARAM            (4)
#define MOTOR_CMD_SIZE_GET_PARAM            (1)
#define MOTOR_CMD_SIZE_RUN                  (4)
#define MOTOR_CMD_SIZE_STEP_CLOCK           (1)
#define MOTOR_CMD_SIZE_MOVE                 (4)
#define MOTOR_CMD_SIZE_GO_TO                (4)
#define MOTOR_CMD_SIZE_GO_TO_DIR            (4)
#define MOTOR_CMD_SIZE_GO_UNTIL             (4)
#define MOTOR_CMD_SIZE_RELEASE_SW           (1)
#define MOTOR_CMD_SIZE_GO_HOME              (1)
#define MOTOR_CMD_SIZE_GO_MARK              (1)
#define MOTOR_CMD_SIZE_RESET_POS            (1)
#define MOTOR_CMD_SIZE_RESET_DEVICE         (1)
#define MOTOR_CMD_SIZE_SOFT_STOP            (1)
#define MOTOR_CMD_SIZE_HARD_STOP            (1)
#define MOTOR_CMD_SIZE_SOFT_HIZ             (1)
#define MOTOR_CMD_SIZE_HARD_HIZ             (1)
#define MOTOR_CMD_SIZE_GET_STATUS           (1)

/*! Command Response Length */
#define MOTOR_CMD_RSP_SIZE_NOP              (0)
#define MOTOR_CMD_RSP_SIZE_SET_PARAM        (0)
#define MOTOR_CMD_RSP_SIZE_GET_PARAM        (3)
#define MOTOR_CMD_RSP_SIZE_RUN              (0)
#define MOTOR_CMD_RSP_SIZE_STEP_CLOCK       (0)
#define MOTOR_CMD_RSP_SIZE_MOVE             (0)
#define MOTOR_CMD_RSP_SIZE_GO_TO            (0)
#define MOTOR_CMD_RSP_SIZE_GO_TO_DIR        (0)
#define MOTOR_CMD_RSP_SIZE_GO_UNTIL         (0)
#define MOTOR_CMD_RSP_SIZE_RELEASE_SW       (0)
#define MOTOR_CMD_RSP_SIZE_GO_HOME          (0)
#define MOTOR_CMD_RSP_SIZE_GO_MARK          (0)
#define MOTOR_CMD_RSP_SIZE_RESET_POS        (0)
#define MOTOR_CMD_RSP_SIZE_RESET_DEVICE     (0)
#define MOTOR_CMD_RSP_SIZE_SOFT_STOP        (0)
#define MOTOR_CMD_RSP_SIZE_HARD_STOP        (0)
#define MOTOR_CMD_RSP_SIZE_SOFT_HIZ         (0)
#define MOTOR_CMD_RSP_SIZE_HARD_HIZ         (0)
#define MOTOR_CMD_RSP_SIZE_GET_STATUS       (2)

#define MOTOR_REG_3_BYTES_LEN               (3)
#define MOTOR_REG_2_BYTES_LEN               (2)
#define MOTOR_REG_1_BYTES_LEN               (1)

#define MOTOR_REG_BYTES_LEN_ABS_POS         (MOTOR_REG_3_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_EL_POS          (MOTOR_REG_2_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_MARK            (MOTOR_REG_3_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_SPEED           (MOTOR_REG_3_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_ACC             (MOTOR_REG_2_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_DEC             (MOTOR_REG_2_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_MAX_SPEED       (MOTOR_REG_2_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_MIN_SPEED       (MOTOR_REG_2_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_FS_SPD          (MOTOR_REG_2_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_KVAL_HOLD       (MOTOR_REG_1_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_KVAL_RUN        (MOTOR_REG_1_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_KVAL_ACC        (MOTOR_REG_1_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_KVAL_DEC        (MOTOR_REG_1_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_INT_SPD         (MOTOR_REG_2_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_ST_SLP          (MOTOR_REG_1_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_FN_SLP_ACC      (MOTOR_REG_1_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_FN_SLP_DEC      (MOTOR_REG_1_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_K_THERM         (MOTOR_REG_1_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_ADC_OUT         (MOTOR_REG_1_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_OCD_TH          (MOTOR_REG_1_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_STALL_TH        (MOTOR_REG_1_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_STEP_MODE       (MOTOR_REG_1_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_ALARM_EN        (MOTOR_REG_1_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_CONFIG          (MOTOR_REG_2_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_STATUS          (MOTOR_REG_2_BYTES_LEN)
#define MOTOR_REG_BYTES_LEN_RESERVED_REG2   (0)
#define MOTOR_REG_BYTES_LEN_RESERVED_REG1   (0)

/*******************************************************************************
 * Motor Driver Limits
 ******************************************************************************/

/*! Motor Driver Current Speed Limits */
#define MOTOR_CURRENT_SPEED_PARAM_STEPS_MIN    (0)
#define MOTOR_CURRENT_SPEED_PARAM_STEPS_MAX    (15625)
#define MOTOR_CURRENT_SPEED_PARAM_STEPS_RES    (0.01490116)

/*! Motor Driver Max Speed Limits */
#define MOTOR_MAX_SPEED_PARAM_STEPS_MIN        (15.25)
#define MOTOR_MAX_SPEED_PARAM_STEPS_MAX        (15610)
#define MOTOR_MAX_SPEED_PARAM_STEPS_RES        (15.258789)

/*! Motor Driver Min Speed Limits */
#define MOTOR_MIN_SPEED_PARAM_STEPS_MIN        (0)
#define MOTOR_MIN_SPEED_PARAM_STEPS_MAX        (976.3)
#define MOTOR_MIN_SPEED_PARAM_STEPS_RES        (0.238418579)

/*! Motor Driver Acceleration/Deceleration Rate Limits */
#define MOTOR_ACC_DEC_RATE_PARAM_STEPS_MIN     (14.55)
#define MOTOR_ACC_DEC_RATE_PARAM_STEPS_MAX     (59590)
#define MOTOR_ACC_DEC_RATe_PARAM_STEPS_RES     (14.5519152)

/*! Motor Driver Full Step Mode Speed Threshold Limits */
#define MOTOR_FS_SPD_PARAM_STEPS_MIN           (7.63)
#define MOTOR_FS_SPD_PARAM_STEPS_MAX           (15625)
#define MOTOR_FS_SPD_PARAM_STEPS_RES           (15.258789)

/*! Motor Driver Intersect Speed Limits */
#define MOTOR_INT_SPEED_PARAM_STEPS_MIN        (0)
#define MOTOR_INT_SPEED_PARAM_STEPS_MAX        (976.5)
#define MOTOR_INT_SPEED_PARAM_STEPS_RES        (0.059604645)

/*! Motor Driver Stall Threshold in mA Limits */
#define MOTOR_STALL_TH_PARAM_MA_MIN            (31.25)
#define MOTOR_STALL_TH_PARAM_MA_MAX            (4000)
#define MOTOR_STALL_TH_PARAM_MA_RES            (31.25)

/*! Motor Driver Thermal Drift Compensation Coefficient Limits */
#define MOTOR_K_THERM_PARAM_STEPS_MIN          (1)
#define MOTOR_K_THERM_PARAM_STEPS_MAX          (1.46875)
#define MOTOR_K_THERM_PARAM_STEPS_RES          (0.03125)

/*! Motor Driver PWM Output Voltage Percentage Limits */
#define MOTOR_KVAL_PARAM_PERCENT_MIN           (0)
#define MOTOR_KVAL_PARAM_PERCENT_MAX           (99.6)
#define MOTOR_KVAL_PARAM_PRECENT_RES           (0.390625)

/*! Motor Driver OCD Threshold Voltage Percentage Limits */
#define MOTOR_OCD_TH_PARAM_PERCENT_MIN         (375)
#define MOTOR_OCD_TH_PARAM_PERCENT_MAX         (6000)
#define MOTOR_OCD_TH_PARAM_PRECENT_RES         (375)

/*******************************************************************************
 * Motor Driver Data Converter
 ******************************************************************************/

/*! Convert Current Speed in step/s to register value */
#define Motor_Speed_Steps_to_RegVal(steps)            ((uint32_t)(((steps) * 67.108864f) + 0.5f))

/*! Convert Current Speed from register value to step/s */
#define Motor_Speed_RegVal_to_Steps(speed)            (((double)speed) * MOTOR_CURRENT_SPEED_PARAM_STEPS_RES)

/*! Convert Acceleratio/Deceleration rates in step/s to register value */
#define Motor_AccDec_Steps_to_RegVal(steps)           ((uint16_t)(((steps) * 0.068719476736f) + 0.5f))

/*! Convert Acceleratio/Deceleration rates from register value to step/s */
#define Motor_AccDec_RegVal_to_Steps(rate)            (((double)rate) * MOTOR_ACC_DEC_RATe_PARAM_STEPS_RES)

/*! Convert Max Speed in step/s to register value */
#define Motor_MaxSpd_Steps_to_RegVal(steps)           ((uint16_t)(((steps) * 0.065536) + 0.5f))

/*! Convert Max Speed from register value to step/s */
#define Motor_MaxSpd_RegVal_to_Steps(speed)           (((double)speed) * MOTOR_MAX_SPEED_PARAM_STEPS_RES)

/*! Convert Min Speed in step/s to register value */
#define Motor_MinSpd_Steps_to_RegVal(steps)           ((uint16_t)(((steps) * 4.194304f) + 0.5f))

/*! Convert Min Speed from register value to step/s */
#define Motor_MinSpd_RegVal_to_Steps(speed)           (((double)speed) * MOTOR_MIN_SPEED_PARAM_STEPS_RES)

/*! Convert Full step mode speed threshold in step/s to register value */
#define Motor_FSSpd_Steps_to_RegVal(steps)            ((uint16_t)(((steps) * 0.065536) - 0.5))

/*! Convert Full step mode speed threshold from register value to step/s */
#define Motor_FSSpd_RegVal_to_Steps(speed)            ((((double)speed) + 0.5) * MOTOR_FS_SPD_PARAM_STEPS_RES)

/*! Convert Intersect speed in step/s to register value */
#define Motor_IntSpd_Steps_to_RegVal(steps)           ((uint16_t)((steps) * 16.777216))

/*! Convert Intersect speed from register value to step/s */
#define Motor_IntSpd_RegVal_to_Steps(speed)           (((double)speed) * MOTOR_INT_SPEED_PARAM_STEPS_RES)

/*! Convert KTherm compensation coefficient value to register value */
#define Motor_KTherm_CompCof_to_RegVal(cof)           ((uint8_t)(((cof) - MOTOR_K_THERM_PARAM_STEPS_MIN) / MOTOR_K_THERM_PARAM_STEPS_RES))

/*! Convert KTherm from register value to compensation coefficient value */
#define Motor_KTherm_RegVal_to_CompCof(KTherm)        ((((double)KTherm) * MOTOR_K_THERM_PARAM_STEPS_RES) + MOTOR_K_THERM_PARAM_STEPS_MIN)

/*! Convert Stall threshold in milliamps to register value */
#define Motor_StallTh_MilliAmps_to_RegVal(StallTh)    ((uint8_t)(((StallTh) / MOTOR_STALL_TH_PARAM_MA_RES) - 1))

/*! Convert Stall threshold from register value to milliamps */
#define Motor_StallTh_RegVal_to_MilliAmps(StallTh)    (((((double)StallTh) + 1) * MOTOR_STALL_TH_PARAM_MA_RES))

/*! Convert KVAL percentage to register value */
#define Motor_Kval_Perc_to_RegVal(perc)               ((uint8_t)((perc) / MOTOR_KVAL_PARAM_PRECENT_RES))

/*! Convert KVAL from register value to percentage */
#define Motor_Kval_RegVal_to_Perc(kval)               (((double)kval) * MOTOR_KVAL_PARAM_PRECENT_RES)

/*! Convert Over Current threshold in milliamps to register value */
#define Motor_OcdTh_MilliAmps_to_RegVal(OcdTh)        ((uint8_t)(((OcdTh) / MOTOR_OCD_TH_PARAM_PRECENT_RES) - 1))

/*! Convert Over Current threshold from register value to milliamps */
#define Motor_OcdTh_RegVal_to_MilliAmps(OcdTh)        ((((double)OcdTh) + 1) * MOTOR_OCD_TH_PARAM_PRECENT_RES)

#endif /* L6470_MOTOR_DRIVER_DEFINES_H */
