/*
 * QTest testcase for Intel HDA
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <string.h>
#include "libqtest.h"
#include "qemu/osdep.h"

#define HDA_ID "hda0"
#define CODEC_DEVICES " -device hda-output,bus=" HDA_ID ".0" \
                      " -device hda-micro,bus=" HDA_ID ".0" \
                      " -device hda-duplex,bus=" HDA_ID ".0"

/* Tests only initialization so far. TODO: Replace with functional tests */
static void ich6_test(void)
{
    qtest_start("-device intel-hda,id=" HDA_ID CODEC_DEVICES);
    qtest_end();
}

static void ich9_test(void)
{
    qtest_start("-machine q35 -device ich9-intel-hda,bus=pcie.0,addr=1b.0,id="
                HDA_ID CODEC_DEVICES);
    qtest_end();
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/intel-hda/ich6", ich6_test);
    qtest_add_func("/intel-hda/ich9", ich9_test);

    ret = g_test_run();

    return ret;
}
