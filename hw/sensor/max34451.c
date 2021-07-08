/*
 * Maxim MAX34451 PMBus 16-Channel V/I monitor and 12-Channel Sequencer/Marginer
 *
 * Copyright 2021 Google LLC
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

#define TYPE_MAX34451 "max34451"
#define MAX34451(obj) OBJECT_CHECK(MAX34451State, (obj), TYPE_MAX34451)

#define MAX34451_MFR_MODE               0xD1
#define MAX34451_MFR_PSEN_CONFIG        0xD2
#define MAX34451_MFR_VOUT_PEAK          0xD4
#define MAX34451_MFR_IOUT_PEAK          0xD5
#define MAX34451_MFR_TEMPERATURE_PEAK   0xD6
#define MAX34451_MFR_VOUT_MIN           0xD7
#define MAX34451_MFR_NV_LOG_CONFIG      0xD8
#define MAX34451_MFR_FAULT_RESPONSE     0xD9
#define MAX34451_MFR_FAULT_RETRY        0xDA
#define MAX34451_MFR_NV_FAULT_LOG       0xDC
#define MAX34451_MFR_TIME_COUNT         0xDD
#define MAX34451_MFR_MARGIN_CONFIG      0xDF
#define MAX34451_MFR_FW_SERIAL          0xE0
#define MAX34451_MFR_IOUT_AVG           0xE2
#define MAX34451_MFR_CHANNEL_CONFIG     0xE4
#define MAX34451_MFR_TON_SEQ_MAX        0xE6
#define MAX34451_MFR_PWM_CONFIG         0xE7
#define MAX34451_MFR_SEQ_CONFIG         0xE8
#define MAX34451_MFR_STORE_ALL          0xEE
#define MAX34451_MFR_RESTORE_ALL        0xEF
#define MAX34451_MFR_TEMP_SENSOR_CONFIG 0xF0
#define MAX34451_MFR_STORE_SINGLE       0xFC
#define MAX34451_MFR_CRC                0xFE

#define MAX34451_NUM_MARGINED_PSU       12
#define MAX34451_NUM_PWR_DEVICES        16
#define MAX34451_NUM_TEMP_DEVICES       5
#define MAX34451_NUM_PAGES              21

#define DEFAULT_OP_ON                   0x80
#define DEFAULT_CAPABILITY              0x20
#define DEFAULT_ON_OFF_CONFIG           0x1a
#define DEFAULT_VOUT_MODE               0x40
#define DEFAULT_TEMPERATURE             2500
#define DEFAULT_SCALE                   0x7FFF
#define DEFAULT_OV_LIMIT                0x7FFF
#define DEFAULT_OC_LIMIT                0x7FFF
#define DEFAULT_OT_LIMIT                0x7FFF
#define DEFAULT_VMIN                    0x7FFF
#define DEFAULT_TON_FAULT_LIMIT         0xFFFF
#define DEFAULT_CHANNEL_CONFIG          0x20
#define DEFAULT_TEXT                    0x3130313031303130

/**
 * MAX34451State:
 * @code: The command code received
 * @page: Each page corresponds to a device monitored by the Max 34451
 * The page register determines the available commands depending on device
  ___________________________________________________________________________
 |   0   |  Power supply monitored by RS0, controlled by PSEN0, and          |
 |       |  margined with PWM0.                                              |
 |_______|___________________________________________________________________|
 |   1   |  Power supply monitored by RS1, controlled by PSEN1, and          |
 |       |  margined with PWM1.                                              |
 |_______|___________________________________________________________________|
 |   2   |  Power supply monitored by RS2, controlled by PSEN2, and          |
 |       |  margined with PWM2.                                              |
 |_______|___________________________________________________________________|
 |   3   |  Power supply monitored by RS3, controlled by PSEN3, and          |
 |       |  margined with PWM3.                                              |
 |_______|___________________________________________________________________|
 |   4   |  Power supply monitored by RS4, controlled by PSEN4, and          |
 |       |  margined with PWM4.                                              |
 |_______|___________________________________________________________________|
 |   5   |  Power supply monitored by RS5, controlled by PSEN5, and          |
 |       |  margined with PWM5.                                              |
 |_______|___________________________________________________________________|
 |   6   |  Power supply monitored by RS6, controlled by PSEN6, and          |
 |       |  margined with PWM6.                                              |
 |_______|___________________________________________________________________|
 |   7   |  Power supply monitored by RS7, controlled by PSEN7, and          |
 |       |  margined with PWM7.                                              |
 |_______|___________________________________________________________________|
 |   8   |  Power supply monitored by RS8, controlled by PSEN8, and          |
 |       | optionally margined by OUT0 of external DS4424 at I2C address A0h.|
 |_______|___________________________________________________________________|
 |   9   |  Power supply monitored by RS9, controlled by PSEN9, and          |
 |       | optionally margined by OUT1 of external DS4424 at I2C address A0h.|
 |_______|___________________________________________________________________|
 |   10  |  Power supply monitored by RS10, controlled by PSEN10, and        |
 |       | optionally margined by OUT2 of external DS4424 at I2C address A0h.|
 |_______|___________________________________________________________________|
 |   11  |  Power supply monitored by RS11, controlled by PSEN11, and        |
 |       | optionally margined by OUT3 of external DS4424 at I2C address A0h.|
 |_______|___________________________________________________________________|
 |   12  |  ADC channel 12 (monitors voltage or current) or GPI.             |
 |_______|___________________________________________________________________|
 |   13  |  ADC channel 13 (monitors voltage or current) or GPI.             |
 |_______|___________________________________________________________________|
 |   14  |  ADC channel 14 (monitors voltage or current) or GPI.             |
 |_______|___________________________________________________________________|
 |   15  |  ADC channel 15 (monitors voltage or current) or GPI.             |
 |_______|___________________________________________________________________|
 |   16  |  Internal temperature sensor.                                     |
 |_______|___________________________________________________________________|
 |   17  |  External DS75LV temperature sensor with I2C address 90h.         |
 |_______|___________________________________________________________________|
 |   18  |  External DS75LV temperature sensor with I2C address 92h.         |
 |_______|___________________________________________________________________|
 |   19  |  External DS75LV temperature sensor with I2C address 94h.         |
 |_______|___________________________________________________________________|
 |   20  |  External DS75LV temperature sensor with I2C address 96h.         |
 |_______|___________________________________________________________________|
 | 21=E2=80=93254|  Reserved.                                                        |
 |_______|___________________________________________________________________|
 |   255 |  Applies to all pages.                                            |
 |_______|___________________________________________________________________|
 *
 * @operation: Turn on and off power supplies
 * @on_off_config: Configure the power supply on and off transition behaviour
 * @write_protect: protect against changes to the device's memory
 * @vout_margin_high: the voltage when OPERATION is set to margin high
 * @vout_margin_low: the voltage when OPERATION is set to margin low
 * @vout_scale: scale ADC reading to actual device reading if different
 * @iout_cal_gain: set ratio of the voltage at the ADC input to sensed current
 */
typedef struct MAX34451State {
    PMBusDevice parent;

    uint16_t power_good_on[MAX34451_NUM_PWR_DEVICES];
    uint16_t power_good_off[MAX34451_NUM_PWR_DEVICES];
    uint16_t ton_delay[MAX34451_NUM_MARGINED_PSU];
    uint16_t ton_max_fault_limit[MAX34451_NUM_MARGINED_PSU];
    uint16_t toff_delay[MAX34451_NUM_MARGINED_PSU];
    uint8_t status_mfr_specific[MAX34451_NUM_PWR_DEVICES];
    /* Manufacturer specific function */
    uint64_t mfr_location;
    uint64_t mfr_date;
    uint64_t mfr_serial;
    uint16_t mfr_mode;
    uint32_t psen_config[MAX34451_NUM_MARGINED_PSU];
    uint16_t vout_peak[MAX34451_NUM_PWR_DEVICES];
    uint16_t iout_peak[MAX34451_NUM_PWR_DEVICES];
    uint16_t temperature_peak[MAX34451_NUM_TEMP_DEVICES];
    uint16_t vout_min[MAX34451_NUM_PWR_DEVICES];
    uint16_t nv_log_config;
    uint32_t fault_response[MAX34451_NUM_PWR_DEVICES];
    uint16_t fault_retry;
    uint32_t fault_log;
    uint32_t time_count;
    uint16_t margin_config[MAX34451_NUM_MARGINED_PSU];
    uint16_t fw_serial;
    uint16_t iout_avg[MAX34451_NUM_PWR_DEVICES];
    uint16_t channel_config[MAX34451_NUM_PWR_DEVICES];
    uint16_t ton_seq_max[MAX34451_NUM_MARGINED_PSU];
    uint32_t pwm_config[MAX34451_NUM_MARGINED_PSU];
    uint32_t seq_config[MAX34451_NUM_MARGINED_PSU];
    uint16_t temp_sensor_config[MAX34451_NUM_TEMP_DEVICES];
    uint16_t store_single;
    uint16_t crc;
} MAX34451State;


static void max34451_check_limits(MAX34451State *s)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(s);

    pmbus_check_limits(pmdev);

    for (int i = 0; i < MAX34451_NUM_PWR_DEVICES; i++) {
        if (pmdev->pages[i].read_vout == 0) { /* PSU disabled */
            continue;
        }

        if (pmdev->pages[i].read_vout > s->vout_peak[i]) {
            s->vout_peak[i] = pmdev->pages[i].read_vout;
        }

        if (pmdev->pages[i].read_vout < s->vout_min[i]) {
            s->vout_min[i] = pmdev->pages[i].read_vout;
        }

        if (pmdev->pages[i].read_iout > s->iout_peak[i]) {
            s->iout_peak[i] = pmdev->pages[i].read_iout;
        }
    }

    for (int i = 0; i < MAX34451_NUM_TEMP_DEVICES; i++) {
        if (pmdev->pages[i + 16].read_temperature_1 > s->temperature_peak[i]) {
            s->temperature_peak[i] = pmdev->pages[i + 16].read_temperature_1;
        }
    }
}

static uint8_t max34451_read_byte(PMBusDevice *pmdev)
{
    MAX34451State *s = MAX34451(pmdev);
    switch (pmdev->code) {

    case PMBUS_POWER_GOOD_ON:
        if (pmdev->page < 16) {
            pmbus_send16(pmdev, s->power_good_on[pmdev->page]);
        }
        break;

    case PMBUS_POWER_GOOD_OFF:
        if (pmdev->page < 16) {
            pmbus_send16(pmdev, s->power_good_off[pmdev->page]);
        }
        break;

    case PMBUS_TON_DELAY:
        if (pmdev->page < 12) {
            pmbus_send16(pmdev, s->ton_delay[pmdev->page]);
        }
        break;

    case PMBUS_TON_MAX_FAULT_LIMIT:
        if (pmdev->page < 12) {
            pmbus_send16(pmdev, s->ton_max_fault_limit[pmdev->page]);
        }
        break;

    case PMBUS_TOFF_DELAY:
        if (pmdev->page < 12) {
            pmbus_send16(pmdev, s->toff_delay[pmdev->page]);
        }
        break;

    case PMBUS_STATUS_MFR_SPECIFIC:
        if (pmdev->page < 16) {
            pmbus_send8(pmdev, s->status_mfr_specific[pmdev->page]);
        }
        break;

    case PMBUS_MFR_ID:
        pmbus_send8(pmdev, 0x4d); /* Maxim */
        break;

    case PMBUS_MFR_MODEL:
        pmbus_send8(pmdev, 0x59);
        break;

    case PMBUS_MFR_LOCATION:
        pmbus_send64(pmdev, s->mfr_location);
        break;

    case PMBUS_MFR_DATE:
        pmbus_send64(pmdev, s->mfr_date);
        break;

    case PMBUS_MFR_SERIAL:
        pmbus_send64(pmdev, s->mfr_serial);
        break;

    case MAX34451_MFR_MODE:
        pmbus_send16(pmdev, s->mfr_mode);
        break;

    case MAX34451_MFR_PSEN_CONFIG:
        if (pmdev->page < 12) {
            pmbus_send32(pmdev, s->psen_config[pmdev->page]);
        }
        break;

    case MAX34451_MFR_VOUT_PEAK:
        if (pmdev->page < 16) {
            pmbus_send16(pmdev, s->vout_peak[pmdev->page]);
        }
        break;

    case MAX34451_MFR_IOUT_PEAK:
        if (pmdev->page < 16) {
            pmbus_send16(pmdev, s->iout_peak[pmdev->page]);
        }
        break;

    case MAX34451_MFR_TEMPERATURE_PEAK:
        if (15 < pmdev->page && pmdev->page < 21) {
            pmbus_send16(pmdev, s->temperature_peak[pmdev->page % 16]);
        } else {
            pmbus_send16(pmdev, s->temperature_peak[0]);
        }
        break;

    case MAX34451_MFR_VOUT_MIN:
        if (pmdev->page < 16) {
            pmbus_send16(pmdev, s->vout_min[pmdev->page]);
        }
        break;

    case MAX34451_MFR_NV_LOG_CONFIG:
        pmbus_send16(pmdev, s->nv_log_config);
        break;

    case MAX34451_MFR_FAULT_RESPONSE:
        if (pmdev->page < 16) {
            pmbus_send32(pmdev, s->fault_response[pmdev->page]);
        }
        break;

    case MAX34451_MFR_FAULT_RETRY:
        pmbus_send32(pmdev, s->fault_retry);
        break;

    case MAX34451_MFR_NV_FAULT_LOG:
        pmbus_send32(pmdev, s->fault_log);
        break;

    case MAX34451_MFR_TIME_COUNT:
        pmbus_send32(pmdev, s->time_count);
        break;

    case MAX34451_MFR_MARGIN_CONFIG:
        if (pmdev->page < 12) {
            pmbus_send16(pmdev, s->margin_config[pmdev->page]);
        }
        break;

    case MAX34451_MFR_FW_SERIAL:
        if (pmdev->page == 255) {
            pmbus_send16(pmdev, 1); /* Firmware revision */
        }
        break;

    case MAX34451_MFR_IOUT_AVG:
        if (pmdev->page < 16) {
            pmbus_send16(pmdev, s->iout_avg[pmdev->page]);
        }
        break;

    case MAX34451_MFR_CHANNEL_CONFIG:
        if (pmdev->page < 16) {
            pmbus_send16(pmdev, s->channel_config[pmdev->page]);
        }
        break;

    case MAX34451_MFR_TON_SEQ_MAX:
        if (pmdev->page < 12) {
            pmbus_send16(pmdev, s->ton_seq_max[pmdev->page]);
        }
        break;

    case MAX34451_MFR_PWM_CONFIG:
        if (pmdev->page < 12) {
            pmbus_send32(pmdev, s->pwm_config[pmdev->page]);
        }
        break;

    case MAX34451_MFR_SEQ_CONFIG:
        if (pmdev->page < 12) {
            pmbus_send32(pmdev, s->seq_config[pmdev->page]);
        }
        break;

    case MAX34451_MFR_TEMP_SENSOR_CONFIG:
        if (15 < pmdev->page && pmdev->page < 21) {
            pmbus_send32(pmdev, s->temp_sensor_config[pmdev->page % 16]);
        }
        break;

    case MAX34451_MFR_STORE_SINGLE:
        pmbus_send32(pmdev, s->store_single);
        break;

    case MAX34451_MFR_CRC:
        pmbus_send32(pmdev, s->crc);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: reading from unsupported register: 0x%02x\n",
                      __func__, pmdev->code);
        break;
    }
    return 0xFF;
}

static int max34451_write_data(PMBusDevice *pmdev, const uint8_t *buf,
                               uint8_t len)
{
    MAX34451State *s = MAX34451(pmdev);

    if (len == 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: writing empty data\n", __func__);
        return -1;
    }

    pmdev->code = buf[0]; /* PMBus command code */

    if (len == 1) {
        return 0;
    }

    /* Exclude command code from buffer */
    buf++;
    len--;
    uint8_t index = pmdev->page;

    switch (pmdev->code) {
    case MAX34451_MFR_STORE_ALL:
    case MAX34451_MFR_RESTORE_ALL:
    case MAX34451_MFR_STORE_SINGLE:
        /*
         * TODO: hardware behaviour is to move the contents of volatile
         * memory to non-volatile memory.
         */
        break;

    case PMBUS_POWER_GOOD_ON: /* R/W word */
        if (pmdev->page < MAX34451_NUM_PWR_DEVICES) {
            s->power_good_on[pmdev->page] = pmbus_receive16(pmdev);
        }
        break;

    case PMBUS_POWER_GOOD_OFF: /* R/W word */
        if (pmdev->page < MAX34451_NUM_PWR_DEVICES) {
            s->power_good_off[pmdev->page] = pmbus_receive16(pmdev);
        }
        break;

    case PMBUS_TON_DELAY: /* R/W word */
        if (pmdev->page < 12) {
            s->ton_delay[pmdev->page] = pmbus_receive16(pmdev);
        }
        break;

    case PMBUS_TON_MAX_FAULT_LIMIT: /* R/W word */
        if (pmdev->page < 12) {
            s->ton_max_fault_limit[pmdev->page]
                = pmbus_receive16(pmdev);
        }
        break;

    case PMBUS_TOFF_DELAY: /* R/W word */
        if (pmdev->page < 12) {
            s->toff_delay[pmdev->page] = pmbus_receive16(pmdev);
        }
        break;

    case PMBUS_MFR_LOCATION: /* R/W 64 */
        s->mfr_location = pmbus_receive64(pmdev);
        break;

    case PMBUS_MFR_DATE: /* R/W 64 */
        s->mfr_date = pmbus_receive64(pmdev);
        break;

    case PMBUS_MFR_SERIAL: /* R/W 64 */
        s->mfr_serial = pmbus_receive64(pmdev);
        break;

    case MAX34451_MFR_MODE: /* R/W word */
         s->mfr_mode = pmbus_receive16(pmdev);
        break;

    case MAX34451_MFR_PSEN_CONFIG: /* R/W 32 */
        if (pmdev->page < 12) {
            s->psen_config[pmdev->page] = pmbus_receive32(pmdev);
        }
        break;

    case MAX34451_MFR_VOUT_PEAK: /* R/W word */
        if (pmdev->page < 16) {
            s->vout_peak[pmdev->page] = pmbus_receive16(pmdev);
        }
        break;

    case MAX34451_MFR_IOUT_PEAK: /* R/W word */
        if (pmdev->page < 16) {
            s->iout_peak[pmdev->page] = pmbus_receive16(pmdev);
        }
        break;

    case MAX34451_MFR_TEMPERATURE_PEAK: /* R/W word */
        if (15 < pmdev->page && pmdev->page < 21) {
            s->temperature_peak[pmdev->page % 16]
                = pmbus_receive16(pmdev);
        }
        break;

    case MAX34451_MFR_VOUT_MIN: /* R/W word */
        if (pmdev->page < 16) {
            s->vout_min[pmdev->page] = pmbus_receive16(pmdev);
        }
        break;

    case MAX34451_MFR_NV_LOG_CONFIG: /* R/W word */
         s->nv_log_config = pmbus_receive16(pmdev);
        break;

    case MAX34451_MFR_FAULT_RESPONSE: /* R/W 32 */
        if (pmdev->page < 16) {
            s->fault_response[pmdev->page] = pmbus_receive32(pmdev);
        }
        break;

    case MAX34451_MFR_FAULT_RETRY: /* R/W word */
        s->fault_retry = pmbus_receive16(pmdev);
        break;

    case MAX34451_MFR_TIME_COUNT: /* R/W 32 */
        s->time_count = pmbus_receive32(pmdev);
        break;

    case MAX34451_MFR_MARGIN_CONFIG: /* R/W word */
        if (pmdev->page < 12) {
            s->margin_config[pmdev->page] = pmbus_receive16(pmdev);
        }
        break;

    case MAX34451_MFR_CHANNEL_CONFIG: /* R/W word */
        if (pmdev->page < 16) {
            s->channel_config[pmdev->page] = pmbus_receive16(pmdev);
        }
        break;

    case MAX34451_MFR_TON_SEQ_MAX: /* R/W word */
        if (pmdev->page < 12) {
            s->ton_seq_max[pmdev->page] = pmbus_receive16(pmdev);
        }
        break;

    case MAX34451_MFR_PWM_CONFIG: /* R/W 32 */
        if (pmdev->page < 12) {
            s->pwm_config[pmdev->page] = pmbus_receive32(pmdev);
        }
        break;

    case MAX34451_MFR_SEQ_CONFIG:  /* R/W 32 */
        if (pmdev->page < 12) {
            s->seq_config[pmdev->page] = pmbus_receive32(pmdev);
        }
        break;

    case MAX34451_MFR_TEMP_SENSOR_CONFIG:  /* R/W word */
        if (15 < pmdev->page && pmdev->page < 21) {
            s->temp_sensor_config[pmdev->page % 16]
                = pmbus_receive16(pmdev);
        }
        break;

    case MAX34451_MFR_CRC: /* R/W word */
        s->crc = pmbus_receive16(pmdev);
        break;

    case MAX34451_MFR_NV_FAULT_LOG:
    case MAX34451_MFR_FW_SERIAL:
    case MAX34451_MFR_IOUT_AVG:
        /* Read only commands */
        pmdev->pages[index].status_word |= PMBUS_STATUS_CML;
        pmdev->pages[index].status_cml |= PB_CML_FAULT_INVALID_DATA;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: writing to read-only register 0x%02x\n",
                      __func__, pmdev->code);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: writing to unsupported register: 0x%02x\n",
                      __func__, pmdev->code);
        break;
    }

    return 0;
}

static void max34451_get(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    visit_type_uint16(v, name, (uint16_t *)opaque, errp);
}

static void max34451_set(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    MAX34451State *s = MAX34451(obj);
    uint16_t *internal = opaque;
    uint16_t value;
    if (!visit_type_uint16(v, name, &value, errp)) {
        return;
    }

    *internal = value;
    max34451_check_limits(s);
}

/* used to init uint16_t arrays */
static inline void *memset_word(void *s, uint16_t c, size_t n)
{
    size_t i;
    uint16_t *p = s;

    for (i = 0; i < n; i++) {
        p[i] = c;
    }

    return s;
}

static void max34451_exit_reset(Object *obj)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    MAX34451State *s = MAX34451(obj);
    pmdev->capability = DEFAULT_CAPABILITY;

    for (int i = 0; i < MAX34451_NUM_PAGES; i++) {
        pmdev->pages[i].operation = DEFAULT_OP_ON;
        pmdev->pages[i].on_off_config = DEFAULT_ON_OFF_CONFIG;
        pmdev->pages[i].revision = 0x11;
        pmdev->pages[i].vout_mode = DEFAULT_VOUT_MODE;
    }

    for (int i = 0; i < MAX34451_NUM_PWR_DEVICES; i++) {
        pmdev->pages[i].vout_scale_monitor = DEFAULT_SCALE;
        pmdev->pages[i].vout_ov_fault_limit = DEFAULT_OV_LIMIT;
        pmdev->pages[i].vout_ov_warn_limit = DEFAULT_OV_LIMIT;
        pmdev->pages[i].iout_oc_warn_limit = DEFAULT_OC_LIMIT;
        pmdev->pages[i].iout_oc_fault_limit = DEFAULT_OC_LIMIT;
    }

    for (int i = 0; i < MAX34451_NUM_MARGINED_PSU; i++) {
        pmdev->pages[i].ton_max_fault_limit = DEFAULT_TON_FAULT_LIMIT;
    }

    for (int i = 16; i < MAX34451_NUM_TEMP_DEVICES + 16; i++) {
        pmdev->pages[i].read_temperature_1 = DEFAULT_TEMPERATURE;
        pmdev->pages[i].ot_warn_limit = DEFAULT_OT_LIMIT;
        pmdev->pages[i].ot_fault_limit = DEFAULT_OT_LIMIT;
    }

    memset_word(s->ton_max_fault_limit, DEFAULT_TON_FAULT_LIMIT,
                MAX34451_NUM_MARGINED_PSU);
    memset_word(s->channel_config, DEFAULT_CHANNEL_CONFIG,
                MAX34451_NUM_PWR_DEVICES);
    memset_word(s->vout_min, DEFAULT_VMIN, MAX34451_NUM_PWR_DEVICES);

    s->mfr_location = DEFAULT_TEXT;
    s->mfr_date = DEFAULT_TEXT;
    s->mfr_serial = DEFAULT_TEXT;
}

static const VMStateDescription vmstate_max34451 = {
    .name = TYPE_MAX34451,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]){
        VMSTATE_PMBUS_DEVICE(parent, MAX34451State),
        VMSTATE_UINT16_ARRAY(power_good_on, MAX34451State,
                             MAX34451_NUM_PWR_DEVICES),
        VMSTATE_UINT16_ARRAY(power_good_off, MAX34451State,
                             MAX34451_NUM_PWR_DEVICES),
        VMSTATE_UINT16_ARRAY(ton_delay, MAX34451State,
                             MAX34451_NUM_MARGINED_PSU),
        VMSTATE_UINT16_ARRAY(ton_max_fault_limit, MAX34451State,
                             MAX34451_NUM_MARGINED_PSU),
        VMSTATE_UINT16_ARRAY(toff_delay, MAX34451State,
                             MAX34451_NUM_MARGINED_PSU),
        VMSTATE_UINT8_ARRAY(status_mfr_specific, MAX34451State,
                             MAX34451_NUM_PWR_DEVICES),
        VMSTATE_UINT64(mfr_location, MAX34451State),
        VMSTATE_UINT64(mfr_date, MAX34451State),
        VMSTATE_UINT64(mfr_serial, MAX34451State),
        VMSTATE_UINT16(mfr_mode, MAX34451State),
        VMSTATE_UINT32_ARRAY(psen_config, MAX34451State,
                             MAX34451_NUM_MARGINED_PSU),
        VMSTATE_UINT16_ARRAY(vout_peak, MAX34451State,
                             MAX34451_NUM_PWR_DEVICES),
        VMSTATE_UINT16_ARRAY(iout_peak, MAX34451State,
                             MAX34451_NUM_PWR_DEVICES),
        VMSTATE_UINT16_ARRAY(temperature_peak, MAX34451State,
                             MAX34451_NUM_TEMP_DEVICES),
        VMSTATE_UINT16_ARRAY(vout_min, MAX34451State, MAX34451_NUM_PWR_DEVICES),
        VMSTATE_UINT16(nv_log_config, MAX34451State),
        VMSTATE_UINT32_ARRAY(fault_response, MAX34451State,
                             MAX34451_NUM_PWR_DEVICES),
        VMSTATE_UINT16(fault_retry, MAX34451State),
        VMSTATE_UINT32(fault_log, MAX34451State),
        VMSTATE_UINT32(time_count, MAX34451State),
        VMSTATE_UINT16_ARRAY(margin_config, MAX34451State,
                             MAX34451_NUM_MARGINED_PSU),
        VMSTATE_UINT16(fw_serial, MAX34451State),
        VMSTATE_UINT16_ARRAY(iout_avg, MAX34451State, MAX34451_NUM_PWR_DEVICES),
        VMSTATE_UINT16_ARRAY(channel_config, MAX34451State,
                             MAX34451_NUM_PWR_DEVICES),
        VMSTATE_UINT16_ARRAY(ton_seq_max, MAX34451State,
                             MAX34451_NUM_MARGINED_PSU),
        VMSTATE_UINT32_ARRAY(pwm_config, MAX34451State,
                             MAX34451_NUM_MARGINED_PSU),
        VMSTATE_UINT32_ARRAY(seq_config, MAX34451State,
                             MAX34451_NUM_MARGINED_PSU),
        VMSTATE_UINT16_ARRAY(temp_sensor_config, MAX34451State,
                             MAX34451_NUM_TEMP_DEVICES),
        VMSTATE_UINT16(store_single, MAX34451State),
        VMSTATE_UINT16(crc, MAX34451State),
        VMSTATE_END_OF_LIST()
    }
};

static void max34451_init(Object *obj)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    uint64_t psu_flags = PB_HAS_VOUT | PB_HAS_IOUT | PB_HAS_VOUT_MODE |
                         PB_HAS_IOUT_GAIN;

    for (int i = 0; i < MAX34451_NUM_PWR_DEVICES; i++) {
        pmbus_page_config(pmdev, i, psu_flags);
    }

    for (int i = 0; i < MAX34451_NUM_MARGINED_PSU; i++) {
        pmbus_page_config(pmdev, i, psu_flags | PB_HAS_VOUT_MARGIN);
    }

    for (int i = 16; i < MAX34451_NUM_TEMP_DEVICES + 16; i++) {
        pmbus_page_config(pmdev, i, PB_HAS_TEMPERATURE | PB_HAS_VOUT_MODE);
    }

    /* get and set the voltage in millivolts, max is 32767 mV */
    for (int i = 0; i < MAX34451_NUM_PWR_DEVICES; i++) {
        object_property_add(obj, "vout[*]", "uint16",
                            max34451_get,
                            max34451_set, NULL, &pmdev->pages[i].read_vout);
    }

    /*
     * get and set the temperature of the internal temperature sensor in
     * centidegrees Celcius i.e.: 2500 -> 25.00 C, max is 327.67 C
     */
    for (int i = 0; i < MAX34451_NUM_TEMP_DEVICES; i++) {
        object_property_add(obj, "temperature[*]", "uint16",
                            max34451_get,
                            max34451_set,
                            NULL,
                            &pmdev->pages[i + 16].read_temperature_1);
    }

}

static void max34451_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    PMBusDeviceClass *k = PMBUS_DEVICE_CLASS(klass);
    dc->desc = "Maxim MAX34451 16-Channel V/I monitor";
    dc->vmsd = &vmstate_max34451;
    k->write_data = max34451_write_data;
    k->receive_byte = max34451_read_byte;
    k->device_num_pages = MAX34451_NUM_PAGES;
    rc->phases.exit = max34451_exit_reset;
}

static const TypeInfo max34451_info = {
    .name = TYPE_MAX34451,
    .parent = TYPE_PMBUS_DEVICE,
    .instance_size = sizeof(MAX34451State),
    .instance_init = max34451_init,
    .class_init = max34451_class_init,
};

static void max34451_register_types(void)
{
    type_register_static(&max34451_info);
}

type_init(max34451_register_types)
