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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/qdev-core.h"
#include "util/nano_utils.h"
#include "hw/qdev-properties.h"
#include "hw/adc/adc_samples_simulator.h"
#include "hw/irq.h"
#include <stdio.h>
#include <sys/stat.h>
#include "qapi/visitor.h"

#ifndef DEBUG_ADCSAMPLESIM
#define DEBUG_ADCSAMPLESIM          0
#endif

#define ADC_SAMPLES_FILE            "adc_samples.bin"

uint16_t adc_get_sample(ADCSAMPLESIMState *s, AdcSimChannels channel)
{   
    if(!s || !s->p_samples || (channel >= NUM_ADC_SIM_CHANNELS) || ((s->sample_index + channel) >= (s->samples_file_size / 2)))
    {
        return (uint16_t) 0;
    }

    while(s->samples_file_being_changed)
    {
        ;       // wait until sample file change process completes
    }

    DPRINTF(TYPE_ADCSAMPLESIM, DEBUG_ADCSAMPLESIM, "channel = %d, returnValue = 0x%x\n", (uint32_t) channel, s->p_samples[s->sample_index + channel]);
    return s->p_samples[s->sample_index + channel];
}

void adc_update_sample_index(ADCSAMPLESIMState *s)
{
    if(s)
    {
        s->sample_index += NUM_ADC_SIM_CHANNELS;
        DPRINTF(TYPE_ADCSAMPLESIM, DEBUG_ADCSAMPLESIM, "sample_index = %d\n", s->sample_index);
        if(s->sample_index >= (s->samples_file_size / 2))      // divide by 2 since each reading is 2 bytes
        {
            s->sample_index = 0;
        }
    }
}

static bool adc_parse_sample_file(ADCSAMPLESIMState *s, uint16_t ** data)
{
    bool result = false;
    char samples_file_path[NANO_MAX_ABSOLUTE_PATH_LENGTH] = {0};

    strncpy(samples_file_path, get_cur_app_abs_dir(), NANO_MAX_ABSOLUTE_PATH_LENGTH);

    if((s->samples_filename) && (s->samples_filename[0] != '\0'))
    {
        strncat(samples_file_path, s->samples_filename, MAX_FILE_NAME_LENGTH);
    }
    else
    {
        strncat(samples_file_path, ADC_SAMPLES_FILE, MAX_FILE_NAME_LENGTH);
    }

    FILE * samples_file = fopen(samples_file_path, "rb");     // Open an existing file in read only mode.

    if(!samples_file)
    {
        error_report("%s: Failed to open ADC samples file correctly.\n", __func__);
    }
    else
    {
        struct stat stbuf;
     
        if ((stat(samples_file_path, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode))) 
        {
            error_report("%s: Failed to obtain file information.\n", __func__);
        }
        else
        {
            s->samples_file_size = (uint32_t) stbuf.st_size;

            DPRINTF(TYPE_ADCSAMPLESIM, DEBUG_ADCSAMPLESIM, "ADC samples file size = %d.\n", s->samples_file_size);

            *data = g_malloc0(s->samples_file_size);
            if(!(*data))
            {
                error_report("%s: Failed to allocate memory for ADC samples.\n", __func__);
            }
            else if(fread(*data, s->samples_file_size, 1, samples_file) != 1)
            {
                error_report("%s: Failed to read all the ADC samples.\n", __func__);
            }
            else
            {
                result = true;
            }
        }
        fclose(samples_file);
    }

    return result;
}

/* --- custom set/get string properties --- */

static void release_string(Object *obj, const char *name, void *opaque)
{
    Property *prop = opaque;
    g_free(*(char **)qdev_get_prop_ptr(DEVICE(obj), prop));
}

static void get_string(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    char **ptr = qdev_get_prop_ptr(dev, prop);

    if (!*ptr) {
        char *str = (char *)"";
        visit_type_str(v, name, &str, errp);
    } else {
        visit_type_str(v, name, ptr, errp);
    }
}

static void set_string(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    ADCSAMPLESIMState *s = ADCSAMPLESIM(dev);
    Property *prop = opaque;
    char **ptr = qdev_get_prop_ptr(dev, prop);
    char *str;
    uint16_t * data = NULL;

    if(strcmp(name, "samples_filename") != 0)
    {
        return;         // Only supports setting the filename
    }

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }
    g_free(*ptr);
    *ptr = str;

    s->samples_file_being_changed = true;

    if(adc_parse_sample_file(s, &data))
    {
        g_free(s->p_samples);
        s->p_samples = data;
        s->sample_index = 0;
    }
    s->samples_file_being_changed = false;
}

const PropertyInfo adc_sample_sim_prop_string = {
    .name  = "str",
    .release = release_string,
    .get   = get_string,
    .set   = set_string,
};

static Property properties_adc_sample_sim[] = {
    DEFINE_PROP_UINT32("sample_index", ADCSAMPLESIMState, sample_index, 0),
    DEFINE_PROP("samples_filename", ADCSAMPLESIMState, samples_filename, adc_sample_sim_prop_string, char*),
    DEFINE_PROP_UINT32("samples_file_size", ADCSAMPLESIMState, samples_file_size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void adc_sample_sim_realize(DeviceState *dev, Error **errp)
{
    ADCSAMPLESIMState *s = ADCSAMPLESIM(dev);

    s->sample_index = 0;
    s->samples_file_being_changed = false;
    s->p_samples = NULL;

    adc_parse_sample_file(s, &s->p_samples);
}

static void adc_sample_sim_unrealize(DeviceState *dev)
{
    ADCSAMPLESIMState *s = ADCSAMPLESIM(dev);
    g_free(s->p_samples);
}

static void adc_sample_sim_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, properties_adc_sample_sim);
    dc->realize = adc_sample_sim_realize;
    dc->desc = "ADC sample simulator";
    dc->unrealize = adc_sample_sim_unrealize;
}

static const TypeInfo adc_sample_sim_info = {
    .name          = TYPE_ADCSAMPLESIM,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(ADCSAMPLESIMState),
    .class_init    = adc_sample_sim_class_init,
};

static void adc_sample_sim_register_types(void)
{
    type_register_static(&adc_sample_sim_info);
}

type_init(adc_sample_sim_register_types)
