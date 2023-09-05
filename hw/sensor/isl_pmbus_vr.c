/*
 * PMBus device for Renesas Digital Multiphase Voltage Regulators
 *
 * Copyright 2021 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sensor/isl_pmbus_vr.h"
#include "hw/qdev-properties.h"
#include "qapi/visitor.h"
#include "qemu/log.h"
#include "qemu/module.h"

static uint8_t isl_pmbus_vr_read_byte(PMBusDevice *pmdev)
{
    ISLState *s = ISL69260(pmdev);

    switch (pmdev->code) {
    case PMBUS_IC_DEVICE_ID:
        if (!s->ic_device_id_len) {
            break;
        }
        pmbus_send(pmdev, s->ic_device_id, s->ic_device_id_len);
        pmbus_idle(pmdev);
        return 0;
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: reading from unsupported register: 0x%02x\n",
                  __func__, pmdev->code);
    return PMBUS_ERR_BYTE;
}

static int isl_pmbus_vr_write_data(PMBusDevice *pmdev, const uint8_t *buf,
                                   uint8_t len)
{
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: write to unsupported register: 0x%02x\n",
                  __func__, pmdev->code);
    return PMBUS_ERR_BYTE;
}

/* TODO: Implement coefficients support in pmbus_device.c for qmp */
static void isl_pmbus_vr_get(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    visit_type_uint16(v, name, (uint16_t *)opaque, errp);
}

static void isl_pmbus_vr_set(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    uint16_t *internal = opaque;
    uint16_t value;
    if (!visit_type_uint16(v, name, &value, errp)) {
        return;
    }

    *internal = value;
    pmbus_check_limits(pmdev);
}

static void isl_pmbus_vr_exit_reset(Object *obj)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);

    pmdev->page = 0;
    pmdev->capability = ISL_CAPABILITY_DEFAULT;
    for (int i = 0; i < pmdev->num_pages; i++) {
        pmdev->pages[i].operation = ISL_OPERATION_DEFAULT;
        pmdev->pages[i].on_off_config = ISL_ON_OFF_CONFIG_DEFAULT;
        pmdev->pages[i].vout_mode = ISL_VOUT_MODE_DEFAULT;
        pmdev->pages[i].vout_command = ISL_VOUT_COMMAND_DEFAULT;
        pmdev->pages[i].vout_max = ISL_VOUT_MAX_DEFAULT;
        pmdev->pages[i].vout_margin_high = ISL_VOUT_MARGIN_HIGH_DEFAULT;
        pmdev->pages[i].vout_margin_low = ISL_VOUT_MARGIN_LOW_DEFAULT;
        pmdev->pages[i].vout_transition_rate = ISL_VOUT_TRANSITION_RATE_DEFAULT;
        pmdev->pages[i].vout_ov_fault_limit = ISL_VOUT_OV_FAULT_LIMIT_DEFAULT;
        pmdev->pages[i].ot_fault_limit = ISL_OT_FAULT_LIMIT_DEFAULT;
        pmdev->pages[i].ot_warn_limit = ISL_OT_WARN_LIMIT_DEFAULT;
        pmdev->pages[i].vin_ov_warn_limit = ISL_VIN_OV_WARN_LIMIT_DEFAULT;
        pmdev->pages[i].vin_uv_warn_limit = ISL_VIN_UV_WARN_LIMIT_DEFAULT;
        pmdev->pages[i].iin_oc_fault_limit = ISL_IIN_OC_FAULT_LIMIT_DEFAULT;
        pmdev->pages[i].ton_delay = ISL_TON_DELAY_DEFAULT;
        pmdev->pages[i].ton_rise = ISL_TON_RISE_DEFAULT;
        pmdev->pages[i].toff_fall = ISL_TOFF_FALL_DEFAULT;
        pmdev->pages[i].revision = ISL_REVISION_DEFAULT;

        pmdev->pages[i].read_vout = ISL_READ_VOUT_DEFAULT;
        pmdev->pages[i].read_iout = ISL_READ_IOUT_DEFAULT;
        pmdev->pages[i].read_pout = ISL_READ_POUT_DEFAULT;
        pmdev->pages[i].read_vin = ISL_READ_VIN_DEFAULT;
        pmdev->pages[i].read_iin = ISL_READ_IIN_DEFAULT;
        pmdev->pages[i].read_pin = ISL_READ_PIN_DEFAULT;
        pmdev->pages[i].read_temperature_1 = ISL_READ_TEMP_DEFAULT;
        pmdev->pages[i].read_temperature_2 = ISL_READ_TEMP_DEFAULT;
        pmdev->pages[i].read_temperature_3 = ISL_READ_TEMP_DEFAULT;
    }
}

/* The raa228000 uses different direct mode coefficients from most isl devices */
static void raa228000_exit_reset(Object *obj)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);

    isl_pmbus_vr_exit_reset(obj);

    pmdev->pages[0].read_iout = 0;
    pmdev->pages[0].read_pout = 0;
    pmdev->pages[0].read_vout = 0;
    pmdev->pages[0].read_vin = 0;
    pmdev->pages[0].read_iin = 0;
    pmdev->pages[0].read_pin = 0;
    pmdev->pages[0].read_temperature_1 = 0;
    pmdev->pages[0].read_temperature_2 = 0;
    pmdev->pages[0].read_temperature_3 = 0;
}

static void isl69259_exit_reset(Object *obj)
{
    ISLState *s = ISL69260(obj);
    static const uint8_t ic_device_id[] = {0x04, 0x00, 0x81, 0xD2, 0x49, 0x3c};
    g_assert(sizeof(ic_device_id) <= sizeof(s->ic_device_id));

    isl_pmbus_vr_exit_reset(obj);

    s->ic_device_id_len = sizeof(ic_device_id);
    memcpy(s->ic_device_id, ic_device_id, sizeof(ic_device_id));
}

static void isl_pmbus_vr_add_props(Object *obj, uint64_t *flags, uint8_t pages)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    for (int i = 0; i < pages; i++) {
        if (flags[i] & PB_HAS_VIN) {
            object_property_add(obj, "vin[*]", "uint16",
                                isl_pmbus_vr_get,
                                isl_pmbus_vr_set,
                                NULL, &pmdev->pages[i].read_vin);
        }

        if (flags[i] & PB_HAS_VOUT) {
            object_property_add(obj, "vout[*]", "uint16",
                                isl_pmbus_vr_get,
                                isl_pmbus_vr_set,
                                NULL, &pmdev->pages[i].read_vout);
        }

        if (flags[i] & PB_HAS_IIN) {
            object_property_add(obj, "iin[*]", "uint16",
                                isl_pmbus_vr_get,
                                isl_pmbus_vr_set,
                                NULL, &pmdev->pages[i].read_iin);
        }

        if (flags[i] & PB_HAS_IOUT) {
            object_property_add(obj, "iout[*]", "uint16",
                                isl_pmbus_vr_get,
                                isl_pmbus_vr_set,
                                NULL, &pmdev->pages[i].read_iout);
        }

        if (flags[i] & PB_HAS_PIN) {
            object_property_add(obj, "pin[*]", "uint16",
                                isl_pmbus_vr_get,
                                isl_pmbus_vr_set,
                                NULL, &pmdev->pages[i].read_pin);
        }

        if (flags[i] & PB_HAS_POUT) {
            object_property_add(obj, "pout[*]", "uint16",
                                isl_pmbus_vr_get,
                                isl_pmbus_vr_set,
                                NULL, &pmdev->pages[i].read_pout);
        }

        if (flags[i] & PB_HAS_TEMPERATURE) {
            object_property_add(obj, "temp1[*]", "uint16",
                                isl_pmbus_vr_get,
                                isl_pmbus_vr_set,
                                NULL, &pmdev->pages[i].read_temperature_1);
        }

        if (flags[i] & PB_HAS_TEMP2) {
            object_property_add(obj, "temp2[*]", "uint16",
                                isl_pmbus_vr_get,
                                isl_pmbus_vr_set,
                                NULL, &pmdev->pages[i].read_temperature_2);
        }

        if (flags[i] & PB_HAS_TEMP3) {
            object_property_add(obj, "temp3[*]", "uint16",
                                isl_pmbus_vr_get,
                                isl_pmbus_vr_set,
                                NULL, &pmdev->pages[i].read_temperature_3);
        }
    }
}

static void raa22xx_init(Object *obj)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    uint64_t flags[2];

    flags[0] = PB_HAS_VIN | PB_HAS_VOUT | PB_HAS_VOUT_MODE |
               PB_HAS_VOUT_RATING | PB_HAS_VOUT_MARGIN | PB_HAS_IIN |
               PB_HAS_IOUT | PB_HAS_PIN | PB_HAS_POUT | PB_HAS_TEMPERATURE |
               PB_HAS_TEMP2 | PB_HAS_TEMP3 | PB_HAS_STATUS_MFR_SPECIFIC;
    flags[1] = PB_HAS_IIN | PB_HAS_PIN | PB_HAS_TEMPERATURE | PB_HAS_TEMP3 |
               PB_HAS_VOUT | PB_HAS_VOUT_MODE | PB_HAS_VOUT_MARGIN |
               PB_HAS_VOUT_RATING | PB_HAS_IOUT | PB_HAS_POUT |
               PB_HAS_STATUS_MFR_SPECIFIC;

    pmbus_page_config(pmdev, 0, flags[0]);
    pmbus_page_config(pmdev, 1, flags[1]);
    isl_pmbus_vr_add_props(obj, flags, ARRAY_SIZE(flags));
}

static void raa228000_init(Object *obj)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    uint64_t flags[1];

    flags[0] = PB_HAS_VIN | PB_HAS_VOUT | PB_HAS_VOUT_MODE |
               PB_HAS_VOUT_RATING | PB_HAS_VOUT_MARGIN | PB_HAS_IIN |
               PB_HAS_IOUT | PB_HAS_PIN | PB_HAS_POUT | PB_HAS_TEMPERATURE |
               PB_HAS_TEMP2 | PB_HAS_TEMP3 | PB_HAS_STATUS_MFR_SPECIFIC;

    pmbus_page_config(pmdev, 0, flags[0]);
    isl_pmbus_vr_add_props(obj, flags, 1);
}

static void isl_pmbus_vr_class_init(ObjectClass *klass, void *data,
                                    uint8_t pages)
{
    PMBusDeviceClass *k = PMBUS_DEVICE_CLASS(klass);
    k->write_data = isl_pmbus_vr_write_data;
    k->receive_byte = isl_pmbus_vr_read_byte;
    k->device_num_pages = pages;
}

static void isl69260_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->desc = "Renesas ISL69260 Digital Multiphase Voltage Regulator";
    rc->phases.exit = isl_pmbus_vr_exit_reset;
    isl_pmbus_vr_class_init(klass, data, 2);
}

static void raa228000_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->desc = "Renesas 228000 Digital Multiphase Voltage Regulator";
    rc->phases.exit = raa228000_exit_reset;
    isl_pmbus_vr_class_init(klass, data, 1);
}

static void raa229004_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->desc = "Renesas 229004 Digital Multiphase Voltage Regulator";
    rc->phases.exit = isl_pmbus_vr_exit_reset;
    isl_pmbus_vr_class_init(klass, data, 2);
}

static void isl69259_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->desc = "Renesas ISL69259 Digital Multiphase Voltage Regulator";
    rc->phases.exit = isl69259_exit_reset;
    isl_pmbus_vr_class_init(klass, data, 2);
}

static const TypeInfo isl69259_info = {
    .name = TYPE_ISL69259,
    .parent = TYPE_ISL69260,
    .class_init = isl69259_class_init,
};

static const TypeInfo isl69260_info = {
    .name = TYPE_ISL69260,
    .parent = TYPE_PMBUS_DEVICE,
    .instance_size = sizeof(ISLState),
    .instance_init = raa22xx_init,
    .class_init = isl69260_class_init,
};

static const TypeInfo raa229004_info = {
    .name = TYPE_RAA229004,
    .parent = TYPE_PMBUS_DEVICE,
    .instance_size = sizeof(ISLState),
    .instance_init = raa22xx_init,
    .class_init = raa229004_class_init,
};

static const TypeInfo raa228000_info = {
    .name = TYPE_RAA228000,
    .parent = TYPE_PMBUS_DEVICE,
    .instance_size = sizeof(ISLState),
    .instance_init = raa228000_init,
    .class_init = raa228000_class_init,
};

static void isl_pmbus_vr_register_types(void)
{
    type_register_static(&isl69259_info);
    type_register_static(&isl69260_info);
    type_register_static(&raa228000_info);
    type_register_static(&raa229004_info);
}

type_init(isl_pmbus_vr_register_types)
