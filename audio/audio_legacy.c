/*
 * QEMU Audio subsystem: legacy configuration handling
 *
 * Copyright (c) 2015-2019 Zoltán Kővágó <DirtY.iCE.hu@gmail.com>
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
#include "audio.h"
#include "audio_int.h"
#include "qemu-common.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-audio.h"
#include "qapi/visitor-impl.h"

#define AUDIO_CAP "audio-legacy"
#include "audio_int.h"

static uint32_t toui32(const char *str)
{
    unsigned long long ret;
    if (parse_uint_full(str, &ret, 10) || ret > UINT32_MAX) {
        dolog("Invalid integer value `%s'\n", str);
        exit(1);
    }
    return ret;
}

/* helper functions to convert env variables */
static void get_bool(const char *env, bool *dst, bool *has_dst)
{
    const char *val = getenv(env);
    if (val) {
        *dst = toui32(val) != 0;
        *has_dst = true;
    }
}

static void get_int(const char *env, uint32_t *dst, bool *has_dst)
{
    const char *val = getenv(env);
    if (val) {
        *dst = toui32(val);
        *has_dst = true;
    }
}

static void get_fmt(const char *env, AudioFormat *dst, bool *has_dst)
{
    const char *val = getenv(env);
    if (val) {
        size_t i;
        for (i = 0; AudioFormat_lookup.size; ++i) {
            if (strcasecmp(val, AudioFormat_lookup.array[i]) == 0) {
                *dst = i;
                *has_dst = true;
                return;
            }
        }

        dolog("Invalid audio format `%s'\n", val);
        exit(1);
    }
}

/* backend specific functions */
/* todo */

/* general */
static void handle_per_direction(
    AudiodevPerDirectionOptions *pdo, const char *prefix)
{
    char buf[64];
    size_t len = strlen(prefix);

    memcpy(buf, prefix, len);
    strcpy(buf + len, "FIXED_SETTINGS");
    get_bool(buf, &pdo->fixed_settings, &pdo->has_fixed_settings);

    strcpy(buf + len, "FIXED_FREQ");
    get_int(buf, &pdo->frequency, &pdo->has_frequency);

    strcpy(buf + len, "FIXED_FMT");
    get_fmt(buf, &pdo->format, &pdo->has_format);

    strcpy(buf + len, "FIXED_CHANNELS");
    get_int(buf, &pdo->channels, &pdo->has_channels);

    strcpy(buf + len, "VOICES");
    get_int(buf, &pdo->voices, &pdo->has_voices);
}

static AudiodevListEntry *legacy_opt(const char *drvname)
{
    AudiodevListEntry *e = g_malloc0(sizeof(AudiodevListEntry));
    e->dev = g_malloc0(sizeof(Audiodev));
    e->dev->id = g_strdup(drvname);
    e->dev->driver = qapi_enum_parse(
        &AudiodevDriver_lookup, drvname, -1, &error_abort);

    audio_create_pdos(e->dev);

    handle_per_direction(audio_get_pdo_in(e->dev), "QEMU_AUDIO_ADC_");
    handle_per_direction(audio_get_pdo_out(e->dev), "QEMU_AUDIO_DAC_");

    get_int("QEMU_AUDIO_TIMER_PERIOD",
            &e->dev->timer_period, &e->dev->has_timer_period);

    return e;
}

AudiodevListHead audio_handle_legacy_opts(void)
{
    const char *drvname = getenv("QEMU_AUDIO_DRV");
    AudiodevListHead head = QSIMPLEQ_HEAD_INITIALIZER(head);

    if (drvname) {
        AudiodevListEntry *e;
        audio_driver *driver = audio_driver_lookup(drvname);
        if (!driver) {
            dolog("Unknown audio driver `%s'\n", drvname);
            exit(1);
        }
        e = legacy_opt(drvname);
        QSIMPLEQ_INSERT_TAIL(&head, e, next);
    } else {
        for (int i = 0; audio_prio_list[i]; i++) {
            audio_driver *driver = audio_driver_lookup(audio_prio_list[i]);
            if (driver && driver->can_be_default) {
                AudiodevListEntry *e = legacy_opt(driver->name);
                QSIMPLEQ_INSERT_TAIL(&head, e, next);
            }
        }
        if (QSIMPLEQ_EMPTY(&head)) {
            dolog("Internal error: no default audio driver available\n");
            exit(1);
        }
    }

    return head;
}

/* visitor to print -audiodev option */
typedef struct {
    Visitor visitor;

    bool comma;
    GList *path;
} LegacyPrintVisitor;

static void lv_start_struct(Visitor *v, const char *name, void **obj,
                            size_t size, Error **errp)
{
    LegacyPrintVisitor *lv = (LegacyPrintVisitor *) v;
    lv->path = g_list_append(lv->path, g_strdup(name));
}

static void lv_end_struct(Visitor *v, void **obj)
{
    LegacyPrintVisitor *lv = (LegacyPrintVisitor *) v;
    lv->path = g_list_delete_link(lv->path, g_list_last(lv->path));
}

static void lv_print_key(Visitor *v, const char *name)
{
    GList *e;
    LegacyPrintVisitor *lv = (LegacyPrintVisitor *) v;
    if (lv->comma) {
        putchar(',');
    } else {
        lv->comma = true;
    }

    for (e = lv->path; e; e = e->next) {
        if (e->data) {
            printf("%s.", (const char *) e->data);
        }
    }

    printf("%s=", name);
}

static void lv_type_int64(Visitor *v, const char *name, int64_t *obj,
                          Error **errp)
{
    lv_print_key(v, name);
    printf("%" PRIi64, *obj);
}

static void lv_type_uint64(Visitor *v, const char *name, uint64_t *obj,
                           Error **errp)
{
    lv_print_key(v, name);
    printf("%" PRIu64, *obj);
}

static void lv_type_bool(Visitor *v, const char *name, bool *obj, Error **errp)
{
    lv_print_key(v, name);
    printf("%s", *obj ? "on" : "off");
}

static void lv_type_str(Visitor *v, const char *name, char **obj, Error **errp)
{
    const char *str = *obj;
    lv_print_key(v, name);

    while (*str) {
        if (*str == ',') {
            putchar(',');
        }
        putchar(*str++);
    }
}

static void lv_complete(Visitor *v, void *opaque)
{
    LegacyPrintVisitor *lv = (LegacyPrintVisitor *) v;
    assert(lv->path == NULL);
}

static void lv_free(Visitor *v)
{
    LegacyPrintVisitor *lv = (LegacyPrintVisitor *) v;

    g_list_free_full(lv->path, g_free);
    g_free(lv);
}

static Visitor *legacy_visitor_new(void)
{
    LegacyPrintVisitor *lv = g_malloc0(sizeof(LegacyPrintVisitor));

    lv->visitor.start_struct = lv_start_struct;
    lv->visitor.end_struct = lv_end_struct;
    /* lists not supported */
    lv->visitor.type_int64 = lv_type_int64;
    lv->visitor.type_uint64 = lv_type_uint64;
    lv->visitor.type_bool = lv_type_bool;
    lv->visitor.type_str = lv_type_str;

    lv->visitor.type = VISITOR_OUTPUT;
    lv->visitor.complete = lv_complete;
    lv->visitor.free = lv_free;

    return &lv->visitor;
}

void audio_legacy_help(void)
{
    AudiodevListHead head;
    AudiodevListEntry *e;

    printf("Environment variable based configuration deprecated.\n");
    printf("Please use the new -audiodev option.\n");

    head = audio_handle_legacy_opts();
    printf("\nEquivalent -audiodev to your current environment variables:\n");
    if (!getenv("QEMU_AUDIO_DRV")) {
        printf("(Since you didn't specify QEMU_AUDIO_DRV, I'll list all "
               "possibilities)\n");
    }

    QSIMPLEQ_FOREACH(e, &head, next) {
        Visitor *v;
        Audiodev *dev = e->dev;
        printf("-audiodev ");

        v = legacy_visitor_new();
        visit_type_Audiodev(v, NULL, &dev, &error_abort);
        visit_free(v);

        printf("\n");
    }
    audio_free_audiodev_list(&head);
}
