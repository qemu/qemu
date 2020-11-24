/*
 * FRAM SPI device
 *
 * Implements mb85rs fram device
 * Currently, it does not implement all the functionalities of this chip.
 * Written by Jay Mehta
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
#include "hw/qdev-core.h"
#include "util/nano_utils.h"
#include "hw/gpio/imx_gpio.h"
#include <stdio.h>

#ifndef DEBUG_MB85RS
#define DEBUG_MB85RS 0
#endif

#define TYPE_MB85RS "mb85rs"
#define FRAM_FILE_NAME "fram_memory.bin"
#define FRAM_CS_GPIO    3

#define MB85RS(obj) OBJECT_CHECK(MB85RSState, (obj), TYPE_MB85RS)

typedef enum Commands {
    NO_COMMAND          =        0x00u,                                       // Default dummy command for this driver to use for initialisation and resetting
    WRITE_ENABLE        =        0x06u,                                       // WREN Set Write Enable Latch 0000 0110B
    WRITE_DISABLE       =        0x04u,                                       // WRDI Reset Write Enable Latch 0000 0100B
    READ_STATUS_REG     =        0x05u,                                       // RDSR Read Status Register 0000 0101B
    WRITE_STATUS_REG    =        0x01u,                                       // WRSR Write Status Register 0000 0001B
    READ_ADDRESS        =        0x03u,                                       // READ Read Memory Code 0000 0011B
    WRITE_ADDRESS       =        0x02u,                                       // WRITE Write Memory Code 0000 0010B
    READ_DEVICE_ID      =        0x9Fu,                                       // RDID Read Device ID 1001 1111B
    FAST_READ_ADDRESS   =        0x0Bu,                                       // FSTRD Fast Read Memory Code 0000 1011B
    SLEEP               =        0xB9u,                                       // Sleep Mode 1011 1001B
} Commands;

#define FRAM_SIZE_BYTES             0x10000u                                    // 65536 words of 8 bit each
#define MAX_MEMORY_ADDRESS          0xFFFFu
#define MIN_MEMORY_ADDRESS          0x0000u
#define DEVICE_ID_LENGTH            4

static const uint8_t device_id[DEVICE_ID_LENGTH] = {0x04, 0x7F, 0x26, 0x03};
static char fram_file_path[NANO_MAX_ABSOLUTE_PATH_LENGTH];

typedef struct MB85RSState {
    SSISlave parent_obj;
    Commands current_command;
    uint8_t memory[FRAM_SIZE_BYTES];
    uint16_t current_address;
    uint32_t addr_byte_count;
    FILE * memory_fp;
} MB85RSState;

static const VMStateDescription vmstate_mb85rs = {
    .name = TYPE_MB85RS,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_SSI_SLAVE(parent_obj, MB85RSState),
        VMSTATE_UINT32(current_command, MB85RSState),
        VMSTATE_UINT8_ARRAY(memory, MB85RSState, FRAM_SIZE_BYTES),
        VMSTATE_UINT16(current_address, MB85RSState),
        VMSTATE_UINT32(addr_byte_count, MB85RSState),
        VMSTATE_END_OF_LIST()
    },
};

static uint32_t mb85rs_transfer(SSISlave *dev, uint32_t val)
{
    MB85RSState *s = MB85RS(dev);
    uint32_t returnValue = 0x00;

    DPRINTF(TYPE_MB85RS, DEBUG_MB85RS, "Function called. val = %d\n", val);

    if(s->current_command == NO_COMMAND)
    {
        s->current_command = val;
    }
    else if((s->current_command == READ_ADDRESS ||
             s->current_command == WRITE_ADDRESS) &&
            (s->addr_byte_count < 2))                   //  Need to receive 2 bytes of address after a command byte is received for read/write command
    {
        if(s->addr_byte_count < 1)
        {
            s->current_address = (val << 8) & 0xFF00u;
            s->addr_byte_count++;
        }
        else if(s->addr_byte_count < 2)
        {
            s->current_address |= (val & 0x00FFu);
            s->addr_byte_count++;
        }
    }
    else
    {
        DPRINTF(TYPE_MB85RS, DEBUG_MB85RS, "Command = %d, Address = %d\n", s->current_command, s->current_address);
        switch(s->current_command)
        {
            case READ_ADDRESS:
                returnValue = s->memory[s->current_address++];
                break;
            case WRITE_ADDRESS:
                s->memory[s->current_address++] = (uint8_t) val;
                break;
            case READ_DEVICE_ID:
                if(s->current_address < DEVICE_ID_LENGTH)
                {
                    returnValue = device_id[s->current_address++];
                }
            default:                // For all other commands, simply return 0, handling can be updated if needed.
                break;
        }
    }

    return returnValue;
}

static void mb85rs_set_cs(void *opaque, int n, int level)
{
    MB85RSState *s = MB85RS(opaque);
    DPRINTF(TYPE_MB85RS, DEBUG_MB85RS, "Function called. select = %d\n", level);
    if(level)
    {
        s->current_address = MIN_MEMORY_ADDRESS;
        
        s->addr_byte_count = 0;

        if(s->current_command == WRITE_ADDRESS && s->memory_fp)
        {
            // Write whole memory to the file
            if(fclose(s->memory_fp) == EOF)
            {
                DPRINTF(TYPE_MB85RS, DEBUG_MB85RS, "Failed to close memory file correctly.\n");
            }

            s->memory_fp = fopen(fram_file_path, "wb+");

            if(s->memory_fp)
            {
                if(fwrite(s->memory, sizeof(s->memory[0]), FRAM_SIZE_BYTES, s->memory_fp) != FRAM_SIZE_BYTES)
                {
                    DPRINTF(TYPE_MB85RS, DEBUG_MB85RS, "Failed to write to memory file correctly.\n");
                }
            }
            else
            {
                DPRINTF(TYPE_MB85RS, DEBUG_MB85RS, "Failed to overwrite memory file correctly.\n");
            }
        }

        s->current_command = NO_COMMAND;
    }
}

static void mb85rs_realize(SSISlave *dev, Error **errp)
{
    char path[100];
    MB85RSState *s = MB85RS(dev);
    
    qdev_init_gpio_in(DEVICE(s), mb85rs_set_cs, 32);
    sprintf(path, "/machine/soc/gpio%d", FRAM_CS_GPIO);
    IMXGPIOState * imx_gpio = IMX_GPIO(object_resolve_path(path, NULL));
    if(imx_gpio == NULL)
    {
        DPRINTF(TYPE_MB85RS, DEBUG_MB85RS, "FRAM_CS_GPIO device not found.\n");
    }

    qdev_connect_gpio_out(DEVICE(imx_gpio), 22, qdev_get_gpio_in(DEVICE(s), 22));

    s->current_address = MIN_MEMORY_ADDRESS;
    s->current_command = NO_COMMAND;
    s->addr_byte_count = 0;

    strncpy(fram_file_path, get_cur_app_abs_dir(), NANO_MAX_ABSOLUTE_PATH_LENGTH);
    strcat(fram_file_path, FRAM_FILE_NAME);

    s->memory_fp = fopen(fram_file_path, "ab+");     // Create a file if it doesn't exist or open an existing file in append mode

    if(!s->memory_fp)
    {
        DPRINTF(TYPE_MB85RS, DEBUG_MB85RS, "Failed to open/create memory file correctly.\n");
    }
    else
    {
        if(fread(s->memory, sizeof(s->memory[0]), FRAM_SIZE_BYTES, s->memory_fp) != FRAM_SIZE_BYTES)
        {
            memset(s->memory, 0, FRAM_SIZE_BYTES);
            DPRINTF(TYPE_MB85RS, DEBUG_MB85RS, "Failed to read memory file correctly.\n");
        }
    }
}

static void mb85rs_class_init(ObjectClass *klass, void *data)
{
    SSISlaveClass *ssc = SSI_SLAVE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    ssc->realize = mb85rs_realize;
    ssc->transfer = mb85rs_transfer;
    ssc->cs_polarity = SSI_CS_LOW;

    dc->vmsd = &vmstate_mb85rs;
    dc->desc = "mb85rs FRAM module";
}

static const TypeInfo mb85rs_info = {
    .name          = TYPE_MB85RS,
    .parent        = TYPE_SSI_SLAVE,
    .instance_size = sizeof(MB85RSState),
    .class_init    = mb85rs_class_init,
};

static void mb85rs_register_types(void)
{
    type_register_static(&mb85rs_info);
}

type_init(mb85rs_register_types)
