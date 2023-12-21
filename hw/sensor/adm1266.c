/*
 * Analog Devices ADM1266 Cascadable Super Sequencer with Margin Control and
 * Fault Recording with PMBus
 *
 * https://www.analog.com/media/en/technical-documentation/data-sheets/adm1266.pdf
 *
 * Copyright 2023 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/pmbus_device.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TYPE_ADM1266 "adm1266"
OBJECT_DECLARE_SIMPLE_TYPE(ADM1266State, ADM1266)

#define ADM1266_BLACKBOX_CONFIG                 0xD3
#define ADM1266_PDIO_CONFIG                     0xD4
#define ADM1266_READ_STATE                      0xD9
#define ADM1266_READ_BLACKBOX                   0xDE
#define ADM1266_SET_RTC                         0xDF
#define ADM1266_GPIO_SYNC_CONFIGURATION         0xE1
#define ADM1266_BLACKBOX_INFORMATION            0xE6
#define ADM1266_PDIO_STATUS                     0xE9
#define ADM1266_GPIO_STATUS                     0xEA

/* Defaults */
#define ADM1266_OPERATION_DEFAULT               0x80
#define ADM1266_CAPABILITY_DEFAULT              0xA0
#define ADM1266_CAPABILITY_NO_PEC               0x20
#define ADM1266_PMBUS_REVISION_DEFAULT          0x22
#define ADM1266_MFR_ID_DEFAULT                  "ADI"
#define ADM1266_MFR_ID_DEFAULT_LEN              32
#define ADM1266_MFR_MODEL_DEFAULT               "ADM1266-A1"
#define ADM1266_MFR_MODEL_DEFAULT_LEN           32
#define ADM1266_MFR_REVISION_DEFAULT            "25"
#define ADM1266_MFR_REVISION_DEFAULT_LEN        8

#define ADM1266_NUM_PAGES               17
/**
 * PAGE Index
 * Page 0 VH1.
 * Page 1 VH2.
 * Page 2 VH3.
 * Page 3 VH4.
 * Page 4 VP1.
 * Page 5 VP2.
 * Page 6 VP3.
 * Page 7 VP4.
 * Page 8 VP5.
 * Page 9 VP6.
 * Page 10 VP7.
 * Page 11 VP8.
 * Page 12 VP9.
 * Page 13 VP10.
 * Page 14 VP11.
 * Page 15 VP12.
 * Page 16 VP13.
 */
typedef struct ADM1266State {
    PMBusDevice parent;

    char mfr_id[32];
    char mfr_model[32];
    char mfr_rev[8];
} ADM1266State;

static const uint8_t adm1266_ic_device_id[] = {0x03, 0x41, 0x12, 0x66};
static const uint8_t adm1266_ic_device_rev[] = {0x08, 0x01, 0x08, 0x07, 0x0,
                                                0x0, 0x07, 0x41, 0x30};

static void adm1266_exit_reset(Object *obj)
{
    ADM1266State *s = ADM1266(obj);
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);

    pmdev->page = 0;
    pmdev->capability = ADM1266_CAPABILITY_NO_PEC;

    for (int i = 0; i < ADM1266_NUM_PAGES; i++) {
        pmdev->pages[i].operation = ADM1266_OPERATION_DEFAULT;
        pmdev->pages[i].revision = ADM1266_PMBUS_REVISION_DEFAULT;
        pmdev->pages[i].vout_mode = 0;
        pmdev->pages[i].read_vout = pmbus_data2linear_mode(12, 0);
        pmdev->pages[i].vout_margin_high = pmbus_data2linear_mode(15, 0);
        pmdev->pages[i].vout_margin_low = pmbus_data2linear_mode(3, 0);
        pmdev->pages[i].vout_ov_fault_limit = pmbus_data2linear_mode(16, 0);
        pmdev->pages[i].revision = ADM1266_PMBUS_REVISION_DEFAULT;
    }

    strncpy(s->mfr_id, ADM1266_MFR_ID_DEFAULT, 4);
    strncpy(s->mfr_model, ADM1266_MFR_MODEL_DEFAULT, 11);
    strncpy(s->mfr_rev, ADM1266_MFR_REVISION_DEFAULT, 3);
}

static uint8_t adm1266_read_byte(PMBusDevice *pmdev)
{
    ADM1266State *s = ADM1266(pmdev);

    switch (pmdev->code) {
    case PMBUS_MFR_ID:                    /* R/W block */
        pmbus_send_string(pmdev, s->mfr_id);
        break;

    case PMBUS_MFR_MODEL:                 /* R/W block */
        pmbus_send_string(pmdev, s->mfr_model);
        break;

    case PMBUS_MFR_REVISION:              /* R/W block */
        pmbus_send_string(pmdev, s->mfr_rev);
        break;

    case PMBUS_IC_DEVICE_ID:
        pmbus_send(pmdev, adm1266_ic_device_id, sizeof(adm1266_ic_device_id));
        break;

    case PMBUS_IC_DEVICE_REV:
        pmbus_send(pmdev, adm1266_ic_device_rev, sizeof(adm1266_ic_device_rev));
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: reading from unimplemented register: 0x%02x\n",
                      __func__, pmdev->code);
        return 0xFF;
    }

    return 0;
}

static int adm1266_write_data(PMBusDevice *pmdev, const uint8_t *buf,
                              uint8_t len)
{
    ADM1266State *s = ADM1266(pmdev);

    switch (pmdev->code) {
    case PMBUS_MFR_ID:                    /* R/W block */
        pmbus_receive_block(pmdev, (uint8_t *)s->mfr_id, sizeof(s->mfr_id));
        break;

    case PMBUS_MFR_MODEL:                 /* R/W block */
        pmbus_receive_block(pmdev, (uint8_t *)s->mfr_model,
                            sizeof(s->mfr_model));
        break;

    case PMBUS_MFR_REVISION:               /* R/W block*/
        pmbus_receive_block(pmdev, (uint8_t *)s->mfr_rev, sizeof(s->mfr_rev));
        break;

    case ADM1266_SET_RTC:   /* do nothing */
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: writing to unimplemented register: 0x%02x\n",
                      __func__, pmdev->code);
        break;
    }
    return 0;
}

static void adm1266_get(Object *obj, Visitor *v, const char *name, void *opaque,
                        Error **errp)
{
    uint16_t value;
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    PMBusVoutMode *mode = (PMBusVoutMode *)&pmdev->pages[0].vout_mode;

    if (strcmp(name, "vout") == 0) {
        value = pmbus_linear_mode2data(*(uint16_t *)opaque, mode->exp);
    } else {
        value = *(uint16_t *)opaque;
    }

    visit_type_uint16(v, name, &value, errp);
}

static void adm1266_set(Object *obj, Visitor *v, const char *name, void *opaque,
                        Error **errp)
{
    uint16_t *internal = opaque;
    uint16_t value;
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    PMBusVoutMode *mode = (PMBusVoutMode *)&pmdev->pages[0].vout_mode;

    if (!visit_type_uint16(v, name, &value, errp)) {
        return;
    }

    *internal = pmbus_data2linear_mode(value, mode->exp);
    pmbus_check_limits(pmdev);
}

static const VMStateDescription vmstate_adm1266 = {
    .name = "ADM1266",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]){
        VMSTATE_PMBUS_DEVICE(parent, ADM1266State),
        VMSTATE_END_OF_LIST()
    }
};

static void adm1266_init(Object *obj)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    uint64_t flags = PB_HAS_VOUT_MODE | PB_HAS_VOUT | PB_HAS_VOUT_MARGIN |
                     PB_HAS_VOUT_RATING | PB_HAS_STATUS_MFR_SPECIFIC;

    for (int i = 0; i < ADM1266_NUM_PAGES; i++) {
        pmbus_page_config(pmdev, i, flags);

        object_property_add(obj, "vout[*]", "uint16",
                            adm1266_get,
                            adm1266_set, NULL, &pmdev->pages[i].read_vout);
    }
}

static void adm1266_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    PMBusDeviceClass *k = PMBUS_DEVICE_CLASS(klass);

    dc->desc = "Analog Devices ADM1266 Hot Swap controller";
    dc->vmsd = &vmstate_adm1266;
    k->write_data = adm1266_write_data;
    k->receive_byte = adm1266_read_byte;
    k->device_num_pages = 17;

    rc->phases.exit = adm1266_exit_reset;
}

static const TypeInfo adm1266_info = {
    .name = TYPE_ADM1266,
    .parent = TYPE_PMBUS_DEVICE,
    .instance_size = sizeof(ADM1266State),
    .instance_init = adm1266_init,
    .class_init = adm1266_class_init,
};

static void adm1266_register_types(void)
{
    type_register_static(&adm1266_info);
}

type_init(adm1266_register_types)
