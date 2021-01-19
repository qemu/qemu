/*
 * QTests for Nuvoton NPCM7xx ADCModules.
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "libqos/libqtest.h"
#include "qapi/qmp/qdict.h"

#define REF_HZ          (25000000)

#define CON_OFFSET      0x0
#define DATA_OFFSET     0x4

#define NUM_INPUTS      8
#define DEFAULT_IREF    2000000
#define CONV_CYCLES     20
#define RESET_CYCLES    10
#define R0_INPUT        500000
#define R1_INPUT        1500000
#define MAX_RESULT      1023

#define DEFAULT_CLKDIV  5

#define FUSE_ARRAY_BA   0xf018a000
#define FCTL_OFFSET     0x14
#define FST_OFFSET      0x0
#define FADDR_OFFSET    0x4
#define FDATA_OFFSET    0x8
#define ADC_CALIB_ADDR  24
#define FUSE_READ       0x2

/* Register field definitions. */
#define CON_MUX(rv) ((rv) << 24)
#define CON_INT_EN  BIT(21)
#define CON_REFSEL  BIT(19)
#define CON_INT     BIT(18)
#define CON_EN      BIT(17)
#define CON_RST     BIT(16)
#define CON_CONV    BIT(14)
#define CON_DIV(rv) extract32(rv, 1, 8)

#define FST_RDST    BIT(1)
#define FDATA_MASK  0xff

#define MAX_ERROR   10000
#define MIN_CALIB_INPUT 100000
#define MAX_CALIB_INPUT 1800000

static const uint32_t input_list[] = {
    100000,
    500000,
    1000000,
    1500000,
    1800000,
    2000000,
};

static const uint32_t vref_list[] = {
    2000000,
    2200000,
    2500000,
};

static const uint32_t iref_list[] = {
    1800000,
    1900000,
    2000000,
    2100000,
    2200000,
};

static const uint32_t div_list[] = {0, 1, 3, 7, 15};

typedef struct ADC {
    int irq;
    uint64_t base_addr;
} ADC;

ADC adc = {
    .irq        = 0,
    .base_addr  = 0xf000c000
};

static uint32_t adc_read_con(QTestState *qts, const ADC *adc)
{
    return qtest_readl(qts, adc->base_addr + CON_OFFSET);
}

static void adc_write_con(QTestState *qts, const ADC *adc, uint32_t value)
{
    qtest_writel(qts, adc->base_addr + CON_OFFSET, value);
}

static uint32_t adc_read_data(QTestState *qts, const ADC *adc)
{
    return qtest_readl(qts, adc->base_addr + DATA_OFFSET);
}

static uint32_t adc_calibrate(uint32_t measured, uint32_t *rv)
{
    return R0_INPUT + (R1_INPUT - R0_INPUT) * (int32_t)(measured - rv[0])
        / (int32_t)(rv[1] - rv[0]);
}

static void adc_qom_set(QTestState *qts, const ADC *adc,
        const char *name, uint32_t value)
{
    QDict *response;
    const char *path = "/machine/soc/adc";

    g_test_message("Setting properties %s of %s with value %u",
            name, path, value);
    response = qtest_qmp(qts, "{ 'execute': 'qom-set',"
            " 'arguments': { 'path': %s, 'property': %s, 'value': %u}}",
            path, name, value);
    /* The qom set message returns successfully. */
    g_assert_true(qdict_haskey(response, "return"));
    qobject_unref(response);
}

static void adc_write_input(QTestState *qts, const ADC *adc,
        uint32_t index, uint32_t value)
{
    char name[100];

    sprintf(name, "adci[%u]", index);
    adc_qom_set(qts, adc, name, value);
}

static void adc_write_vref(QTestState *qts, const ADC *adc, uint32_t value)
{
    adc_qom_set(qts, adc, "vref", value);
}

static uint32_t adc_calculate_output(uint32_t input, uint32_t ref)
{
    uint32_t output;

    g_assert_cmpuint(input, <=, ref);
    output = (input * (MAX_RESULT + 1)) / ref;
    if (output > MAX_RESULT) {
        output = MAX_RESULT;
    }

    return output;
}

static uint32_t adc_prescaler(QTestState *qts, const ADC *adc)
{
    uint32_t div = extract32(adc_read_con(qts, adc), 1, 8);

    return 2 * (div + 1);
}

static int64_t adc_calculate_steps(uint32_t cycles, uint32_t prescale,
        uint32_t clkdiv)
{
    return (NANOSECONDS_PER_SECOND / (REF_HZ >> clkdiv)) * cycles * prescale;
}

static void adc_wait_conv_finished(QTestState *qts, const ADC *adc,
        uint32_t clkdiv)
{
    uint32_t prescaler = adc_prescaler(qts, adc);

    /*
     * ADC should takes roughly 20 cycles to convert one sample. So we assert it
     * should take 10~30 cycles here.
     */
    qtest_clock_step(qts, adc_calculate_steps(CONV_CYCLES / 2, prescaler,
                clkdiv));
    /* ADC is still converting. */
    g_assert_true(adc_read_con(qts, adc) & CON_CONV);
    qtest_clock_step(qts, adc_calculate_steps(CONV_CYCLES, prescaler, clkdiv));
    /* ADC has finished conversion. */
    g_assert_false(adc_read_con(qts, adc) & CON_CONV);
}

/* Check ADC can be reset to default value. */
static void test_init(gconstpointer adc_p)
{
    const ADC *adc = adc_p;

    QTestState *qts = qtest_init("-machine quanta-gsj");
    adc_write_con(qts, adc, CON_REFSEL | CON_INT);
    g_assert_cmphex(adc_read_con(qts, adc), ==, CON_REFSEL);
    qtest_quit(qts);
}

/* Check ADC can convert from an internal reference. */
static void test_convert_internal(gconstpointer adc_p)
{
    const ADC *adc = adc_p;
    uint32_t index, input, output, expected_output;
    QTestState *qts = qtest_init("-machine quanta-gsj");
    qtest_irq_intercept_in(qts, "/machine/soc/a9mpcore/gic");

    for (index = 0; index < NUM_INPUTS; ++index) {
        for (size_t i = 0; i < ARRAY_SIZE(input_list); ++i) {
            input = input_list[i];
            expected_output = adc_calculate_output(input, DEFAULT_IREF);

            adc_write_input(qts, adc, index, input);
            adc_write_con(qts, adc, CON_MUX(index) | CON_REFSEL | CON_INT |
                    CON_EN | CON_CONV);
            adc_wait_conv_finished(qts, adc, DEFAULT_CLKDIV);
            g_assert_cmphex(adc_read_con(qts, adc), ==, CON_MUX(index) |
                    CON_REFSEL | CON_EN);
            g_assert_false(qtest_get_irq(qts, adc->irq));
            output = adc_read_data(qts, adc);
            g_assert_cmpuint(output, ==, expected_output);
        }
    }

    qtest_quit(qts);
}

/* Check ADC can convert from an external reference. */
static void test_convert_external(gconstpointer adc_p)
{
    const ADC *adc = adc_p;
    uint32_t index, input, vref, output, expected_output;
    QTestState *qts = qtest_init("-machine quanta-gsj");
    qtest_irq_intercept_in(qts, "/machine/soc/a9mpcore/gic");

    for (index = 0; index < NUM_INPUTS; ++index) {
        for (size_t i = 0; i < ARRAY_SIZE(input_list); ++i) {
            for (size_t j = 0; j < ARRAY_SIZE(vref_list); ++j) {
                input = input_list[i];
                vref = vref_list[j];
                expected_output = adc_calculate_output(input, vref);

                adc_write_input(qts, adc, index, input);
                adc_write_vref(qts, adc, vref);
                adc_write_con(qts, adc, CON_MUX(index) | CON_INT | CON_EN |
                        CON_CONV);
                adc_wait_conv_finished(qts, adc, DEFAULT_CLKDIV);
                g_assert_cmphex(adc_read_con(qts, adc), ==,
                        CON_MUX(index) | CON_EN);
                g_assert_false(qtest_get_irq(qts, adc->irq));
                output = adc_read_data(qts, adc);
                g_assert_cmpuint(output, ==, expected_output);
            }
        }
    }

    qtest_quit(qts);
}

/* Check ADC interrupt files if and only if CON_INT_EN is set. */
static void test_interrupt(gconstpointer adc_p)
{
    const ADC *adc = adc_p;
    uint32_t index, input, output, expected_output;
    QTestState *qts = qtest_init("-machine quanta-gsj");

    index = 1;
    input = input_list[1];
    expected_output = adc_calculate_output(input, DEFAULT_IREF);

    qtest_irq_intercept_in(qts, "/machine/soc/a9mpcore/gic");
    adc_write_input(qts, adc, index, input);
    g_assert_false(qtest_get_irq(qts, adc->irq));
    adc_write_con(qts, adc, CON_MUX(index) | CON_INT_EN | CON_REFSEL | CON_INT
            | CON_EN | CON_CONV);
    adc_wait_conv_finished(qts, adc, DEFAULT_CLKDIV);
    g_assert_cmphex(adc_read_con(qts, adc), ==, CON_MUX(index) | CON_INT_EN
            | CON_REFSEL | CON_INT | CON_EN);
    g_assert_true(qtest_get_irq(qts, adc->irq));
    output = adc_read_data(qts, adc);
    g_assert_cmpuint(output, ==, expected_output);

    qtest_quit(qts);
}

/* Check ADC is reset after setting ADC_RST for 10 ADC cycles. */
static void test_reset(gconstpointer adc_p)
{
    const ADC *adc = adc_p;
    QTestState *qts = qtest_init("-machine quanta-gsj");

    for (size_t i = 0; i < ARRAY_SIZE(div_list); ++i) {
        uint32_t div = div_list[i];

        adc_write_con(qts, adc, CON_INT | CON_EN | CON_RST | CON_DIV(div));
        qtest_clock_step(qts, adc_calculate_steps(RESET_CYCLES,
                    adc_prescaler(qts, adc), DEFAULT_CLKDIV));
        g_assert_false(adc_read_con(qts, adc) & CON_EN);
    }
    qtest_quit(qts);
}

/* Check ADC Calibration works as desired. */
static void test_calibrate(gconstpointer adc_p)
{
    int i, j;
    const ADC *adc = adc_p;

    for (j = 0; j < ARRAY_SIZE(iref_list); ++j) {
        uint32_t iref = iref_list[j];
        uint32_t expected_rv[] = {
            adc_calculate_output(R0_INPUT, iref),
            adc_calculate_output(R1_INPUT, iref),
        };
        char buf[100];
        QTestState *qts;

        sprintf(buf, "-machine quanta-gsj -global npcm7xx-adc.iref=%u", iref);
        qts = qtest_init(buf);

        /* Check the converted value is correct using the calibration value. */
        for (i = 0; i < ARRAY_SIZE(input_list); ++i) {
            uint32_t input;
            uint32_t output;
            uint32_t expected_output;
            uint32_t calibrated_voltage;
            uint32_t index = 0;

            input = input_list[i];
            /* Calibration only works for input range 0.1V ~ 1.8V. */
            if (input < MIN_CALIB_INPUT || input > MAX_CALIB_INPUT) {
                continue;
            }
            expected_output = adc_calculate_output(input, iref);

            adc_write_input(qts, adc, index, input);
            adc_write_con(qts, adc, CON_MUX(index) | CON_REFSEL | CON_INT |
                    CON_EN | CON_CONV);
            adc_wait_conv_finished(qts, adc, DEFAULT_CLKDIV);
            g_assert_cmphex(adc_read_con(qts, adc), ==,
                    CON_REFSEL | CON_MUX(index) | CON_EN);
            output = adc_read_data(qts, adc);
            g_assert_cmpuint(output, ==, expected_output);

            calibrated_voltage = adc_calibrate(output, expected_rv);
            g_assert_cmpuint(calibrated_voltage, >, input - MAX_ERROR);
            g_assert_cmpuint(calibrated_voltage, <, input + MAX_ERROR);
        }

        qtest_quit(qts);
    }
}

static void adc_add_test(const char *name, const ADC* wd,
        GTestDataFunc fn)
{
    g_autofree char *full_name = g_strdup_printf("npcm7xx_adc/%s",  name);
    qtest_add_data_func(full_name, wd, fn);
}
#define add_test(name, td) adc_add_test(#name, td, test_##name)

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    add_test(init, &adc);
    add_test(convert_internal, &adc);
    add_test(convert_external, &adc);
    add_test(interrupt, &adc);
    add_test(reset, &adc);
    add_test(calibrate, &adc);

    return g_test_run();
}
