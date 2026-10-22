/*
 * QTest for what the Qualcomm DSP machines have in common: the console and
 * the virtio-mmio transports.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqtest-single.h"

#define QCOM_DSP_UART_BASE 0x37000000
#define QCOM_DSP_VIRTIO_BASE 0x37010000
#define QCOM_DSP_VIRTIO_STRIDE 0x1000
#define QCOM_DSP_VIRTIO_COUNT 8

#define PL011_UARTPERIPHID0 0xfe0
#define PL011_PERIPHID0 0x11

#define VIRTIO_MMIO_MAGIC_VALUE 0x000
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MAGIC 0x74726976 /* "virt" */

static const char *const qcom_dsp_machines[] = {
    "qcs6490-cdsp",
    "sa8775p-cdsp",
    "sc8480xp-nsp0",
    "sa8797p-nsp0",
};

static void test_console(const void *machine)
{
    g_autofree char *args = g_strdup_printf("-machine %s",
                                            (const char *)machine);

    qtest_start(args);
    g_assert_cmphex(readl(QCOM_DSP_UART_BASE + PL011_UARTPERIPHID0), ==,
                    PL011_PERIPHID0);
    qtest_end();
}

static void test_virtio_slots(const void *machine)
{
    g_autofree char *args = g_strdup_printf("-machine %s",
                                            (const char *)machine);

    qtest_start(args);
    for (int i = 0; i < QCOM_DSP_VIRTIO_COUNT; i++) {
        uint64_t base = QCOM_DSP_VIRTIO_BASE + i * QCOM_DSP_VIRTIO_STRIDE;

        g_assert_cmphex(readl(base + VIRTIO_MMIO_MAGIC_VALUE), ==,
                        VIRTIO_MAGIC);
        /* No backend is attached, so the transport is empty. */
        g_assert_cmpuint(readl(base + VIRTIO_MMIO_DEVICE_ID), ==, 0);
    }
    qtest_end();
}

/* A drive is attached to a virtio transport without any -device. */
static void test_default_drive(const void *machine)
{
    g_autofree char *args = g_strdup_printf(
        "-machine %s -drive if=virtio,file=null-co://,format=raw",
        (const char *)machine);
    int found = 0;

    qtest_start(args);
    for (int i = 0; i < QCOM_DSP_VIRTIO_COUNT; i++) {
        uint64_t base = QCOM_DSP_VIRTIO_BASE + i * QCOM_DSP_VIRTIO_STRIDE;

        /* virtio-blk is device ID 2 */
        found += readl(base + VIRTIO_MMIO_DEVICE_ID) == 2;
    }
    g_assert_cmpint(found, ==, 1);
    qtest_end();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    for (size_t i = 0; i < ARRAY_SIZE(qcom_dsp_machines); i++) {
        const char *name = qcom_dsp_machines[i];
        g_autofree char *path = NULL;

        path = g_strdup_printf("/qcom-dsp/%s/console", name);
        qtest_add_data_func(path, name, test_console);
        path = g_strdup_printf("/qcom-dsp/%s/virtio-slots", name);
        qtest_add_data_func(path, name, test_virtio_slots);
        path = g_strdup_printf("/qcom-dsp/%s/default-drive", name);
        qtest_add_data_func(path, name, test_default_drive);
    }

    return g_test_run();
}
