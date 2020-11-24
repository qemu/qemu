/*
 * L6470 Motor Driver SPI device
 *
 * Implements l6470 motor driver device
 * Currently, it does not implement all the functionalities of this chip.
 * Written by Nigel Po
 *
 * Copyright (c) 2020 Nanosonics Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/ssi/ssi.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/qdev-core.h"
#include "hw/sysbus.h"
#include "util/nano_utils.h"
#include "l6470_motor_driver_defines.h"
#include <stdio.h>
#include <limits.h>

#ifndef DEBUG_L6470
#define DEBUG_L6470 0
#endif

#define TYPE_L6470 "l6470"

#define NUM_REGISTERS     MOTOR_REG_ADDR_RESERVED_REG1
#define NUM_MOTOR_DEVICES 8 // ToDo: This can be put in a configuration file that can be accessed when the QEMU starts

#define CS_ACTIVE_STATE   SSI_CS_LOW
#define CS_INACTIVE_LEVEL true

#define DEFAULT_MOTOR_RSP_IDX (NUM_MOTOR_DEVICES - 1)

#define L6470(obj) OBJECT_CHECK(L6470State, (obj), TYPE_L6470)

#define L6470_CMD_MASK     0xE0
#define L6470_PARAM_MASK   0x1F
#define L6470_RUN_DIR_MASK 0x01

#define MOTOR_FIFO_WORDS   ((NUM_MOTOR_DEVICES + (sizeof(uint32_t) - 1)) / sizeof(uint32_t))
#define MAX_SPI_FIFO_WORDS (MOTOR_CMD_RSP_MAX_LENGTH * MOTOR_FIFO_WORDS)

typedef enum {
    SET_PARAM_CMD_BITS  = MOTOR_CMD_SET_PARAM & L6470_CMD_MASK,
    GET_PARAM_CMD_BITS  = MOTOR_CMD_GET_PARAM & L6470_CMD_MASK,
    RUN_CMD_BITS        = MOTOR_CMD_RUN & L6470_CMD_MASK,
    SOFT_HIZ_CMD_BITS   = MOTOR_CMD_SOFT_HIZ & L6470_CMD_MASK,
    GET_STATUS_CMD_BITS = MOTOR_CMD_GET_STATUS & L6470_CMD_MASK
} L6470CommandBits;

typedef struct L6470CmdRspData {
    uint32_t current_idx;
    uint32_t length;
    uint8_t data[MOTOR_CMD_RSP_MAX_LENGTH];
    bool active;
} L6470CmdRspData;

typedef struct L6470FiFoData {
    uint32_t current_idx;
    uint32_t length;
    uint32_t data[MAX_SPI_FIFO_WORDS];
    bool active;
} L6470FiFoData;

typedef struct L6470Device
{
    uint32_t registers[NUM_REGISTERS];
    L6470CmdRspData cmd_data;
    L6470CmdRspData rsp_data;
} L6470Device;

typedef struct L6470State {
    SSISlave parent_obj;

    uint32_t last_cs_state;
    uint32_t last_motor_rsp_idx;
    L6470FiFoData command;
    L6470Device motor_devices[NUM_MOTOR_DEVICES];
} L6470State;

typedef struct L6470RegisterInfo {
    size_t length;
    uint32_t default_value;
} L6470RegisterInfo;

static const L6470RegisterInfo register_info[] = { { 0,                                 0x0000 }, // Dummy Table Entry
                                                   { MOTOR_REG_BYTES_LEN_ABS_POS,       0x0000 },
                                                   { MOTOR_REG_BYTES_LEN_EL_POS,        0x0000 },
                                                   { MOTOR_REG_BYTES_LEN_MARK,          0x0000 },
                                                   { MOTOR_REG_BYTES_LEN_SPEED,         0x0000 },
                                                   { MOTOR_REG_BYTES_LEN_ACC,           0x008A },
                                                   { MOTOR_REG_BYTES_LEN_DEC,           0x008A },
                                                   { MOTOR_REG_BYTES_LEN_MAX_SPEED,     0x0041 },
                                                   { MOTOR_REG_BYTES_LEN_MIN_SPEED,     0x0000 },
                                                   { MOTOR_REG_BYTES_LEN_KVAL_HOLD,     0x0029 },
                                                   { MOTOR_REG_BYTES_LEN_KVAL_RUN,      0x0029 },
                                                   { MOTOR_REG_BYTES_LEN_KVAL_ACC,      0x0029 },
                                                   { MOTOR_REG_BYTES_LEN_KVAL_DEC,      0x0029 },
                                                   { MOTOR_REG_BYTES_LEN_INT_SPD,       0x0408 },
                                                   { MOTOR_REG_BYTES_LEN_ST_SLP,        0x0019 },
                                                   { MOTOR_REG_BYTES_LEN_FN_SLP_ACC,    0x0029 },
                                                   { MOTOR_REG_BYTES_LEN_FN_SLP_DEC,    0x0029 },
                                                   { MOTOR_REG_BYTES_LEN_K_THERM,       0x0000 },
                                                   { MOTOR_REG_BYTES_LEN_ADC_OUT,       0x0000 },
                                                   { MOTOR_REG_BYTES_LEN_OCD_TH,        0x0008 },
                                                   { MOTOR_REG_BYTES_LEN_STALL_TH,      0x0040 },
                                                   { MOTOR_REG_BYTES_LEN_FS_SPD,        0x0027 },
                                                   { MOTOR_REG_BYTES_LEN_STEP_MODE,     0x0007 },
                                                   { MOTOR_REG_BYTES_LEN_ALARM_EN,      0x00FF },
                                                   { MOTOR_REG_BYTES_LEN_CONFIG,        0x2E88 },
                                                   { MOTOR_REG_BYTES_LEN_STATUS,        0x0000 },
                                                   { MOTOR_REG_BYTES_LEN_RESERVED_REG2, 0x0000 },
                                                   { MOTOR_REG_BYTES_LEN_RESERVED_REG1, 0x0000 }
                                              };

static void l6470_reset_all_registers(L6470State * p_state)
{
    for (int motor_idx = 0; motor_idx < NUM_MOTOR_DEVICES; motor_idx++) {
        for (int reg_idx = 0; reg_idx < NUM_REGISTERS; reg_idx++) {
            p_state->motor_devices[motor_idx].registers[reg_idx] = register_info[reg_idx].default_value;
        }
    }
}

static void l6470_clear_data(L6470CmdRspData * p_data)
{
    p_data->current_idx = 0;
    p_data->length = 0;
    p_data->active = false;
    memset(p_data->data, 0, sizeof(p_data->data));
}

static void l6470_clear_fifo_data(L6470FiFoData * p_data)
{
    p_data->current_idx = 0;
    p_data->length = 0;
    p_data->active = false;
    memset(p_data->data, 0, sizeof(p_data->data));
}

static void l6470_write_register_value(L6470Device * p_device, MotorDriverRegisters reg)
{
    // Register data starts at index 1 of the command buffer
    uint32_t param_start_idx = 1;

    uint32_t reg_val = p_device->cmd_data.data[param_start_idx];
    for (int x = 1; x < register_info[reg].length; x++) {
        // Most Significant Byte is sent first
        reg_val = (reg_val << 8) + p_device->cmd_data.data[(x + param_start_idx)];
    }

    p_device->registers[reg] = reg_val;

    DPRINTF(TYPE_L6470, DEBUG_L6470, "%s: p_device->registers[0x%x / %d] = 0x%08x\n", __func__, reg, reg, p_device->registers[reg]);
}

static void l6470_copy_register_value(L6470Device * p_device, MotorDriverRegisters reg)
{
    p_device->rsp_data.current_idx = 0;
    p_device->rsp_data.active = true;
    p_device->rsp_data.length = register_info[reg].length;

    uint32_t reg_val = p_device->registers[reg];
    for (int x = 0, y = register_info[reg].length - 1; x < register_info[reg].length; x++, y--) {
        // Most Significant Byte is sent first
        p_device->rsp_data.data[x] = (reg_val >> (8 * y)) & 0x000000FF;
    }
}

static void l6470_set_motor_state(L6470Device * p_device, MotorDriverStatusRegisterMotorStatus status)
{
    uint32_t reg_val = p_device->registers[MOTOR_REG_ADDR_STATUS];
    reg_val = (reg_val & ~(MOTOR_STATUS_MOT_STATUS_MASK | MOTOR_STATUS_HIZ_MASK)) | status;

    if (status == MOTOR_STATUS_MOT_STATUS_STOPPED) {
        reg_val |= MOTOR_STATUS_HIZ_MASK;

        // It is easier to directly clear the speed register from here.
        p_device->registers[MOTOR_REG_ADDR_SPEED] = 0;
    }

    p_device->registers[MOTOR_REG_ADDR_STATUS] = reg_val;
}

static void l6470_set_motor_direction(L6470Device * p_device, MotorDirection dir)
{
    p_device->registers[MOTOR_REG_ADDR_STATUS] =
        (p_device->registers[MOTOR_REG_ADDR_STATUS] & ~MOTOR_STATUS_DIR_MASK) |
        (dir << MOTOR_STATUS_DIR_SHIFT);
}

static void l6470_clear_motor_error(L6470Device * p_device)
{
    p_device->registers[MOTOR_REG_ADDR_STATUS] &= ~(MOTOR_STATUS_NOTPERF_CMD_MASK | MOTOR_STATUS_WRONG_CMD_MASK);
    p_device->registers[MOTOR_REG_ADDR_STATUS] |= MOTOR_STATUS_UVLO_MASK | MOTOR_STATUS_TH_WRN_MASK | MOTOR_STATUS_TH_SD_MASK |
                                                  MOTOR_STATUS_OCD_MASK | MOTOR_STATUS_STEP_LOSS_A_MASK |
                                                  MOTOR_STATUS_STEP_LOSS_B_MASK | MOTOR_STATUS_SCK_MOD_MASK;
}

static uint32_t l6470_get_command_length(uint8_t cmd)
{
    switch (cmd & L6470_CMD_MASK) {
    case SET_PARAM_CMD_BITS:
        return register_info[cmd].length + 1;

    case GET_PARAM_CMD_BITS:
        return MOTOR_CMD_SIZE_GET_PARAM;

    case RUN_CMD_BITS:
        return MOTOR_CMD_SIZE_RUN;

    case SOFT_HIZ_CMD_BITS:
        return MOTOR_CMD_SIZE_SOFT_HIZ;

    case GET_STATUS_CMD_BITS:
        return MOTOR_CMD_SIZE_GET_STATUS;

    default:
        return 0;
    }
}

static void l6470_perform_command(L6470Device * p_device)
{
    switch (p_device->cmd_data.data[0] & L6470_CMD_MASK) {
    case SET_PARAM_CMD_BITS:
        DPRINTF(TYPE_L6470, DEBUG_L6470, "%s: Set Param command\n", __func__);
        l6470_write_register_value(p_device, (p_device->cmd_data.data[0] & L6470_PARAM_MASK));
        break;

    case GET_PARAM_CMD_BITS:
        DPRINTF(TYPE_L6470, DEBUG_L6470, "%s: Get Param command\n", __func__);
        l6470_copy_register_value(p_device, (p_device->cmd_data.data[0] & L6470_PARAM_MASK));
        break;

    case RUN_CMD_BITS:
        DPRINTF(TYPE_L6470, DEBUG_L6470, "%s: Run command\n", __func__);
        l6470_set_motor_direction(p_device, p_device->cmd_data.data[0] & L6470_RUN_DIR_MASK);
        l6470_set_motor_state(p_device, MOTOR_STATUS_MOT_STATUS_CONST_SPD);
        l6470_write_register_value(p_device, MOTOR_REG_ADDR_SPEED);
        break;

    case SOFT_HIZ_CMD_BITS:
        DPRINTF(TYPE_L6470, DEBUG_L6470, "%s: Soft HiZ command\n", __func__);
        l6470_set_motor_state(p_device, MOTOR_STATUS_MOT_STATUS_STOPPED);
        break;

    case GET_STATUS_CMD_BITS:
        DPRINTF(TYPE_L6470, DEBUG_L6470, "%s: Get Status command\n", __func__);
        l6470_copy_register_value(p_device, MOTOR_REG_ADDR_STATUS);
        l6470_clear_motor_error(p_device);
        break;

    default:
        error_report("%s: Unsupported command: 0x%x\n", __func__, p_device->cmd_data.data[0]);
    }
}

static void l6470_decode_command(L6470State * p_state)
{
    for (int x = 0; x < NUM_MOTOR_DEVICES; x++) {
        L6470CmdRspData * p_cmd_data = &p_state->motor_devices[x].cmd_data;

        // All commands are 1 byte and could have 1 to 3 Bytes Parameter
        // The firs byte of the command has been received:
        // - need to know how long the command will be
        // - clear the response buffer since we are about to process another command
        if ((p_cmd_data->current_idx == 1) && (!p_cmd_data->active)) {
            if (p_cmd_data->length != 0) {
                assert(0);
            }

            p_cmd_data->length = l6470_get_command_length(p_cmd_data->data[0]);
            if (p_cmd_data->length > 0) {
                p_cmd_data->active = true;
            }

            l6470_clear_data(&p_state->motor_devices[x].rsp_data);
        }

        // We have received all the expected bytes of the command
        // It can now be processed and buffer cleared for the next command
        if ((p_cmd_data->current_idx == p_cmd_data->length) && (p_cmd_data->active)) {
            l6470_perform_command(&p_state->motor_devices[x]);
            l6470_clear_data(p_cmd_data);
        }
    }
}

static void l6470_copy_command(L6470State * p_state)
{
    L6470FiFoData * p_cmd = &p_state->command;
    uint32_t num_motors = p_cmd->current_idx * sizeof(uint32_t);
    assert(num_motors <= NUM_MOTOR_DEVICES);

    // 1. Process each FIFO word containing valid data
    // 2. Copy each byte value of the FIFO word to the corresponding motor device
    // 3. Data for the last motor device in the chain is received first
    int motor_idx = num_motors - 1;
    for (int cmd_idx = 0; cmd_idx < p_cmd->current_idx; cmd_idx++) {
        for (int word_idx = sizeof(uint32_t) - 1; (word_idx) >= 0 && (motor_idx >= 0); word_idx--, motor_idx--) {
            uint8_t cmd = (p_cmd->data[cmd_idx] >> (word_idx * CHAR_BIT)) & 0x000000FFu;
            L6470CmdRspData * p_cmd_data = &p_state->motor_devices[motor_idx].cmd_data;

            if ((cmd != MOTOR_CMD_NOP) || (p_cmd_data->active)) {
                // This is not a dummy data used to push commands/response to the appropriate motor device.
                p_cmd_data->data[p_cmd_data->current_idx++] = cmd;
            }

            DPRINTF(TYPE_L6470, DEBUG_L6470, "%s: p_cmd_data->current_idx = %d, p_cmd_data->active = %d\n", __func__, p_cmd_data->current_idx, p_cmd_data->active);
            DPRINTF(TYPE_L6470, DEBUG_L6470, "%s: cmd = 0x%x, cmd_idx = %d, motor_idx = %d\n", __func__, cmd, cmd_idx, motor_idx);
        }
    }

    l6470_clear_fifo_data(p_cmd);
}

static void l6470_process_command(L6470State * p_state)
{
    if (p_state->command.current_idx == 0) {
        return; // Nothing to process
    }

    // 1. Copy command data from FIFO to the respective Motor Device Command buffer
    l6470_copy_command(p_state);

    // 2. Process any command completely received.
    l6470_decode_command(p_state);
}

static void l6470_copy_fifo_command(L6470FiFoData * p_cmd_data, uint32_t cmd)
{
    if (p_cmd_data) {
        if (p_cmd_data->current_idx >= MAX_SPI_FIFO_WORDS) {
            l6470_clear_fifo_data(p_cmd_data);
        }

        p_cmd_data->data[p_cmd_data->current_idx++] = cmd;
    }
}

static uint32_t l6470_get_response(L6470State * p_state)
{
    uint32_t returnValue = MOTOR_CMD_NOP;
    uint32_t num_bytes = (p_state->last_motor_rsp_idx > (sizeof(uint32_t) - 1)) ?
        (sizeof(uint32_t) - 1) : p_state->last_motor_rsp_idx;

    DPRINTF(TYPE_L6470, DEBUG_L6470, "%s: S num_bytes = %d\n", __func__, num_bytes);

    // Motor devices will send 1 byte of the response per transfer
    // There will be at most 4 motors for each word of response
    // The order of response received will start from the last motor device in the Daisy Chain configuration
    for (int idx = num_bytes; idx >= 0; idx--, p_state->last_motor_rsp_idx--) {
        L6470CmdRspData * p_data = &p_state->motor_devices[p_state->last_motor_rsp_idx].rsp_data;
        uint8_t val = p_data->active ? p_data->data[p_data->current_idx] : 0;

        DPRINTF(TYPE_L6470, DEBUG_L6470, "%s: Processing response for motor idx %d\n",
                __func__, p_state->last_motor_rsp_idx);
        DPRINTF(TYPE_L6470, DEBUG_L6470, "%s: val = 0x%x, p_data->current_idx = %d, p_data->active = %d\n",
                __func__, val, p_data->current_idx, p_data->active);

        // Most Significant Byte is sent first
        returnValue = (returnValue << CHAR_BIT) + val;

        p_data->current_idx++;
        if (p_data->current_idx == p_data->length) {
            DPRINTF(TYPE_L6470, DEBUG_L6470, "%s: All response sent. Clearing response data buffer\n", __func__);
            l6470_clear_data(p_data);
        }
    }

    DPRINTF(TYPE_L6470, DEBUG_L6470, "%s: returnValue = 0x%08x\n", __func__, returnValue);

    return returnValue;
}

static uint32_t l6470_transfer(SSISlave *dev, uint32_t val)
{
    L6470State *s = L6470(dev);

    DPRINTF(TYPE_L6470, DEBUG_L6470, "%s: val = 0x%08x\n", __func__, val);

    l6470_copy_fifo_command(&s->command, val);

    return l6470_get_response(s);
}

static int l6470_set_cs(SSISlave *dev, bool select)
{
    L6470State *s = L6470(dev);
    DPRINTF(TYPE_L6470, DEBUG_L6470, "%s: select = 0x%08x\n", __func__, select);

    if (s->last_cs_state != select) {
        if (select == CS_INACTIVE_LEVEL) {
            l6470_process_command(s);
        } else {
            s->last_motor_rsp_idx = DEFAULT_MOTOR_RSP_IDX;
        }
    }

    s->last_cs_state = select;

    return 0;
}

static void l6470_realize(SSISlave *dev, Error **errp)
{
    L6470State *s = L6470(dev);
    qemu_irq cs_line;

    s->last_cs_state = CS_INACTIVE_LEVEL;
    s->last_motor_rsp_idx = DEFAULT_MOTOR_RSP_IDX;
    memset(s->motor_devices, 0, sizeof(s->motor_devices));
    memset(&s->command, 0, sizeof(s->command));

    l6470_reset_all_registers(s);

    dev->cs = CS_INACTIVE_LEVEL;

    // Connect the SSI CS GPIO to the SPI module this device is connected to.
    cs_line = qdev_get_gpio_in_named(DEVICE(dev), SSI_GPIO_CS, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->parent_obj.parent_obj.parent_bus->parent), 1, cs_line);
}

static void l6470_class_init(ObjectClass *klass, void *data)
{
    SSISlaveClass *ssc = SSI_SLAVE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    ssc->realize = l6470_realize;
    ssc->transfer = l6470_transfer;
    ssc->set_cs = l6470_set_cs;
    ssc->cs_polarity = SSI_CS_LOW;

    dc->desc = "L6470 Motor Driver Module";
}

static const TypeInfo l6470_info = {
    .name          = TYPE_L6470,
    .parent        = TYPE_SSI_SLAVE,
    .instance_size = sizeof(L6470State),
    .class_init    = l6470_class_init,
};

static void l6470_register_types(void)
{
    type_register_static(&l6470_info);
}

type_init(l6470_register_types)
