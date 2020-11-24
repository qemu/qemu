/*
 * ADC samples simulator device
 *
 * Implements a wrapper device to parse simulated ADC samples from a file and supply them to different ADC peripherals
 * Written by Jay Mehta
 *
 * Copyright (c) 2020 Nanosonics Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef ADC_SAMPLES_SIMULATOR_H
#define ADC_SAMPLES_SIMULATOR_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "qemu/timer.h"

#define TYPE_ADCSAMPLESIM           "adc_sample_sim"

#define NAME_ADCSAMPLESIM           "adcsim"

OBJECT_DECLARE_SIMPLE_TYPE(ADCSAMPLESIMState, ADCSAMPLESIM)

#define MAX_FILE_NAME_LENGTH        (100u)

typedef enum AdcSimChannels {
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
    ADC_SIM_CHANNEL_15,
    ADC_SIM_CHANNEL_16,
    ADC_SIM_CHANNEL_17,
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
    ADC_SIM_CHANNEL_28,
    NUM_ADC_SIM_CHANNELS,
} AdcSimChannels;

typedef struct ADCSAMPLESIMState {
    DeviceState parent_obj;
    uint16_t * p_samples;
    uint32_t sample_index;
    char * samples_filename;
    uint32_t samples_file_size;
    bool samples_file_being_changed;
} ADCSAMPLESIMState;

uint16_t adc_get_sample(ADCSAMPLESIMState *s, AdcSimChannels channel);

void adc_update_sample_index(ADCSAMPLESIMState *s);

#endif /* ADC_SAMPLES_SIMULATOR_H */
