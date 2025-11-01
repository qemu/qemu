/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "monitor/qdev.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/audio/model.h"

struct audio_model {
    const char *name;
    const char *descr;
    const char *typename;
    void (*init)(const char *audiodev);
};

static struct audio_model audio_models[9];
static int audio_models_count;

void audio_register_model_with_cb(const char *name, const char *descr,
                                  void (*init_audio_model)(const char *audiodev))
{
    assert(audio_models_count < ARRAY_SIZE(audio_models) - 1);
    audio_models[audio_models_count].name = name;
    audio_models[audio_models_count].descr = descr;
    audio_models[audio_models_count].init = init_audio_model;
    audio_models_count++;
}

void audio_register_model(const char *name, const char *descr,
                          const char *typename)
{
    assert(audio_models_count < ARRAY_SIZE(audio_models) - 1);
    audio_models[audio_models_count].name = name;
    audio_models[audio_models_count].descr = descr;
    audio_models[audio_models_count].typename = typename;
    audio_models_count++;
}

void audio_print_available_models(void)
{
    struct audio_model *c;

    if (audio_models_count) {
        printf("Valid audio device model names:\n");
        for (c = audio_models; c->name; ++c) {
            printf("%-11s %s\n", c->name, c->descr);
        }
    } else {
        printf("Machine has no user-selectable audio hardware "
               "(it may or may not have always-present audio hardware).\n");
    }
}

static struct audio_model *selected;
static const char *audiodev_id;

void audio_set_model(const char *name, const char *audiodev)
{
    struct audio_model *c;

    if (selected) {
        error_report("only one -audio option is allowed");
        exit(1);
    }

    for (c = audio_models; c->name; ++c) {
        if (g_str_equal(c->name, name)) {
            selected = c;
            audiodev_id = audiodev;
            break;
        }
    }

    if (!c->name) {
        error_report("Unknown audio device model `%s'", name);
        audio_print_available_models();
        exit(1);
    }
}

void audio_model_init(void)
{
    struct audio_model *c = selected;

    if (!c) {
        return;
    }

    if (c->typename) {
        DeviceState *dev = qdev_new(c->typename);
        BusState *bus = qdev_find_default_bus(DEVICE_GET_CLASS(dev), &error_fatal);
        qdev_prop_set_string(dev, "audiodev", audiodev_id);
        qdev_realize_and_unref(dev, bus, &error_fatal);
    } else {
        c->init(audiodev_id);
    }
}
