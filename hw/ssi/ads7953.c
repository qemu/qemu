/*
 * ADC SPI device
 *
 * Implements ads7953 adc device.
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
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/qdev-core.h"
#include "util/nano_utils.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"
#include "hw/ssi/ssi.h"
#include "hw/adc/adc_samples_simulator.h"

#ifndef DEBUG_ADS7953
#define DEBUG_ADS7953 0
#endif

#define TYPE_ADS7953        "ads7953"

#define ADS7953(obj) OBJECT_CHECK(ADS7953State, (obj), TYPE_ADS7953)

#define MODE_MASK                   (0xF000u)
#define END_CHANNEL_MASK            (0x03C0u)
#define END_CHANNEL_SHIFT           (6u)
#define SETTINGS_MASK               (0x07ffu)
#define MANUAL_CHANNEL_MASK         (0x0780u)
#define MANUAL_CHANNEL_SHIFT        (7u)
#define READING_MASK                (0x0FFFu)

#define AUTO_MODES_RESET_CH         (BIT(10))
#define OUTPUT_GPIO_MASK            (BIT(4))
#define GPIO_OR_CHANNEL_SHIFT       (12u)

#define SELECT_MANUAL_MODE          BIT(12)
#define SELECT_AUTO_1_MODE          BIT(13)
#define SELECT_AUTO_2_MODE          BIT(13) | BIT(12)
#define SELECT_AUTO_1_PR            BIT(15)
#define SELECT_AUTO_2_PR            BIT(15) | BIT(12)
#define SELECT_CONTROL_REG          BIT(11)
#define CONTINUE_SAME_MODE          0x00

typedef enum Channels {
    CHANNEL_0,
    CHANNEL_1,
    CHANNEL_2,
    CHANNEL_3,
    CHANNEL_4,
    CHANNEL_5,
    CHANNEL_6,
    CHANNEL_7,
    CHANNEL_8,
    CHANNEL_9,
    CHANNEL_10,
    CHANNEL_11,
    CHANNEL_12,
    CHANNEL_13,
    CHANNEL_14,
    CHANNEL_15,
    NUM_CHANNELS,
} Channels;

#define NUM_AUTO_CHANNELS       (NUM_CHANNELS - 1)              // 15 auto channels are used, hence (16 - 1)
#define NUM_MANUAL_CHANNELS     (11)                            // 1 channel containing 11 manual channels
#define NUM_ALL_ADC_CHANNELS    (NUM_AUTO_CHANNELS + NUM_MANUAL_CHANNELS)

static const AdcSimChannels adc_simulated_reading_map[NUM_ALL_ADC_CHANNELS] = {
                                                                                ADC_SIM_CHANNEL_0,
                                                                                ADC_SIM_CHANNEL_1,
                                                                                ADC_SIM_CHANNEL_2,
                                                                                ADC_SIM_CHANNEL_3,
                                                                                ADC_SIM_CHANNEL_4,
                                                                                ADC_SIM_CHANNEL_5,
                                                                                ADC_SIM_CHANNEL_6,
                                                                                ADC_SIM_CHANNEL_7,
                                                                                ADC_SIM_CHANNEL_8,
                                                                                ADC_SIM_CHANNEL_9,
                                                                                ADC_SIM_CHANNEL_10,
                                                                                ADC_SIM_CHANNEL_11,
                                                                                ADC_SIM_CHANNEL_12,
                                                                                ADC_SIM_CHANNEL_13,
                                                                                ADC_SIM_CHANNEL_14,
                                                                                ADC_SIM_CHANNEL_18,
                                                                                ADC_SIM_CHANNEL_19,
                                                                                ADC_SIM_CHANNEL_20,
                                                                                ADC_SIM_CHANNEL_21,
                                                                                ADC_SIM_CHANNEL_22,
                                                                                ADC_SIM_CHANNEL_23,
                                                                                ADC_SIM_CHANNEL_24,
                                                                                ADC_SIM_CHANNEL_25,
                                                                                ADC_SIM_CHANNEL_26,
                                                                                ADC_SIM_CHANNEL_27,
                                                                                ADC_SIM_CHANNEL_28
                                                                             };

typedef enum Mode {
    MANUAL_MODE = SELECT_MANUAL_MODE,
    AUTO_1_MODE = SELECT_AUTO_1_MODE,
    AUTO_2_MODE = SELECT_AUTO_2_MODE
} Mode;

typedef struct ADS7953State {
    SSISlave parent_obj;
    ADCSAMPLESIMState * p_adc_simulator;
    Channels channel_to_select;
    Channels channel_to_sample;
    Channels channel_to_send;
    Mode current_mode;
    bool mode_changed;
    Channels end_channel;
    uint16_t settings_bits;
    uint8_t gpio_values;
} ADS7953State;

static Property properties_ads7953[] = {
    DEFINE_PROP_UINT32("channel_to_select", ADS7953State, channel_to_select, 0),
    DEFINE_PROP_UINT32("channel_to_sample", ADS7953State, channel_to_sample, 0),
    DEFINE_PROP_UINT32("channel_to_send", ADS7953State, channel_to_send, 0),
    DEFINE_PROP_UINT32("current_mode", ADS7953State, current_mode, 0),
    DEFINE_PROP_BOOL("mode_changed", ADS7953State, mode_changed, false),
    DEFINE_PROP_UINT32("end_channel", ADS7953State, end_channel, 0),
    DEFINE_PROP_UINT16("settings_bits", ADS7953State, settings_bits, 0),
    DEFINE_PROP_UINT8("gpio_values", ADS7953State, gpio_values, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static uint16_t ads7953_get_simulated_reading(ADS7953State * s, uint32_t val)
{
    static uint32_t manual_channel_index_offset = 0;        // This is used to read different values for manual channel 15, 
                                                            // so everytime it reads channel 15, it will provide a different reading
    uint32_t simulated_reading_index = 0;

    if(s->current_mode == MANUAL_MODE && s->channel_to_send == CHANNEL_15)          // For manual mode, add an index offset to read a different value everytime
    {
        simulated_reading_index = s->channel_to_send + manual_channel_index_offset;
        if(simulated_reading_index >= NUM_ALL_ADC_CHANNELS)
        {
            simulated_reading_index = s->channel_to_send;
            manual_channel_index_offset = 0;
        }
        else if((val & SELECT_CONTROL_REG) == SELECT_CONTROL_REG)   // Only increase offset to get next manual channel when Settings are overwritten
                                                                    // There are 3 Manual mode SPI transactions for each manual channel read
                                                                    // So, only need to switch to the next manual channel every 3rd SPI transaction 
                                                                    // when manual mode and manual channel is selected
        {
            manual_channel_index_offset++;
        }
    }
    else
    {
        simulated_reading_index = s->channel_to_send;
        manual_channel_index_offset = 0;
    }

    return (adc_get_sample(s->p_adc_simulator, adc_simulated_reading_map[simulated_reading_index]) & READING_MASK);
}

static uint32_t ads7953_transfer(SSISlave *dev, uint32_t val)
{
    ADS7953State *s = ADS7953(dev);
    uint32_t returnValue = 0x00;

    DPRINTF(TYPE_ADS7953, DEBUG_ADS7953, "Function called. Device Id = %s, val = 0x%x\n", dev->parent_obj.id, val);

    uint32_t mode_command = val & MODE_MASK;
    s->mode_changed = false;    // By default its either the same mode given again or continue same mode command 
                                // or its a configuration command, in those cases it will be same mode as before or as reset.

    switch(mode_command)
    {
        case SELECT_AUTO_2_PR:
            s->end_channel = (val & END_CHANNEL_MASK) >> END_CHANNEL_SHIFT;
            break;
        case SELECT_AUTO_1_PR:
            break;              // Not supported at the moment
        case MANUAL_MODE:
        case AUTO_1_MODE:
        case AUTO_2_MODE:
            if(s->current_mode != mode_command)
            {
                s->mode_changed = true;
            }
            s->current_mode = mode_command;     // Set the new mode to be used.

            // Update the setting bits if needed otherwise same setting bits will be kept from previous frame
            if((val & SELECT_CONTROL_REG) == SELECT_CONTROL_REG)
            {
                s->settings_bits = val & SETTINGS_MASK;
            }
            break;
        case CONTINUE_SAME_MODE:                // Continue with same mode and settings as per previous frame
            break;
        default:
            error_report("%s: Invalid mode command given. Mode command = %d\n", __func__, mode_command);
            return returnValue;
    }

    // This simulates the 2 frame delay as per ADS7953 datasheet
    // In the nth frame, a channel is selected.
    // In the n+1 frame, the channel selected in nth frame is sampled while another channel is selected.
    // In the n+2 frame, the channel selected in nth frame and sampled in n+1 frame is actually sent out.
    s->channel_to_send = s->channel_to_sample;
    s->channel_to_sample = s->channel_to_select;

    switch(s->current_mode)
    {
        case MANUAL_MODE:   //Always update the channel to select as per the bits in the manual mode command.
            s->channel_to_select = (val & MANUAL_CHANNEL_MASK) >> MANUAL_CHANNEL_SHIFT;
            break;
        case AUTO_1_MODE:
            break;          // Not supported at the moment
        case AUTO_2_MODE:
            // Default to channel 0 when auto-2 mode is entered or reset channel bit is forced
            if(s->mode_changed || ((s->settings_bits & AUTO_MODES_RESET_CH) == AUTO_MODES_RESET_CH))
            {
                s->channel_to_select = CHANNEL_0;
            }
            else
            {
                s->channel_to_select++;
                if(s->channel_to_select >= s->end_channel)
                {
                    s->channel_to_select = CHANNEL_0;
                }
            }

            break;

        default:
            error_report("%s: Invalid mode set. Mode = %d\n", __func__, s->current_mode);
            return returnValue;
    }

    DPRINTF(TYPE_ADS7953, DEBUG_ADS7953, "Channel to send = %d, channel to sample = %d, channel to select = %d\n", s->channel_to_send, s->channel_to_sample, s->channel_to_select);

    if(s->channel_to_send >= NUM_CHANNELS)
    {
        error_report("%s: Channel to send (%d) is more the max number of channels.\n", __func__, s->channel_to_send);
        s->channel_to_send = CHANNEL_0;
    }

    if((s->settings_bits & OUTPUT_GPIO_MASK) == OUTPUT_GPIO_MASK)
    {
        returnValue = (s->gpio_values << GPIO_OR_CHANNEL_SHIFT) | ads7953_get_simulated_reading(s, val);
    }
    else
    {
        returnValue = (s->channel_to_send << GPIO_OR_CHANNEL_SHIFT) | ads7953_get_simulated_reading(s, val);
    }

    // This indicates a new read loop.
    // First we read channel 0 to 14 in auto_2 mode.Then Channel 15 is read 11 times in manual mode.
    // Then we switch back to auto-2 mode and repeat.
    // At this time, mode is changed to auto-2 while channel to send is still channel 15.
    if(s->mode_changed && s->current_mode == AUTO_2_MODE && s->channel_to_send == CHANNEL_15)
    {
        adc_update_sample_index(s->p_adc_simulator);
    }

    DPRINTF(TYPE_ADS7953, DEBUG_ADS7953, "returnValue = 0x%x\n", returnValue);
    return returnValue;
}

static void ads7953_realize(SSISlave *dev, Error **errp)
{
    ADS7953State *s = ADS7953(dev);
    char adc_sim_device_path[100];
    
    sprintf(adc_sim_device_path, "/machine/%s", NAME_ADCSAMPLESIM);
    s->p_adc_simulator = ADCSAMPLESIM(object_resolve_path(adc_sim_device_path, NULL));

    if(s->p_adc_simulator == NULL)
    {
        error_report("%s: ADC simulator device not found.\n", __func__);
    }

    s->channel_to_select = CHANNEL_0;
    s->channel_to_sample = CHANNEL_0;
    s->channel_to_send = CHANNEL_0;
    s->current_mode = MANUAL_MODE;
    s->mode_changed = false;
    s->end_channel = CHANNEL_0;
    s->settings_bits = 0;
    s->gpio_values = 0;
}

static void ads7953_class_init(ObjectClass *klass, void *data)
{
    SSISlaveClass *ssc = SSI_SLAVE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, properties_ads7953);
    ssc->realize = ads7953_realize;
    ssc->transfer = ads7953_transfer;
    ssc->cs_polarity = SSI_CS_LOW;

    dc->desc = "ads7953 ADC module";
}

static const TypeInfo ads7953_info = {
    .name          = TYPE_ADS7953,
    .parent        = TYPE_SSI_SLAVE,
    .instance_size = sizeof(ADS7953State),
    .class_init    = ads7953_class_init,
};

static void ads7953_register_types(void)
{
    type_register_static(&ads7953_info);
}

type_init(ads7953_register_types)
