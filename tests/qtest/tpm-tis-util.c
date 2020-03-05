/*
 * QTest testcase for TPM TIS: common test functions used for both
 * the ISA and SYSBUS devices
 *
 * Copyright (c) 2018 Red Hat, Inc.
 * Copyright (c) 2018 IBM Corporation
 *
 * Authors:
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *   Stefan Berger <stefanb@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "hw/acpi/tpm.h"
#include "io/channel-socket.h"
#include "libqtest-single.h"
#include "qemu/module.h"
#include "tpm-emu.h"
#include "tpm-util.h"
#include "tpm-tis-util.h"

#define DEBUG_TIS_TEST 0

#define DPRINTF(fmt, ...) do { \
    if (DEBUG_TIS_TEST) { \
        printf(fmt, ## __VA_ARGS__); \
    } \
} while (0)

#define DPRINTF_ACCESS \
    DPRINTF("%s: %d: locty=%d l=%d access=0x%02x pending_request_flag=0x%x\n", \
            __func__, __LINE__, locty, l, access, pending_request_flag)

#define DPRINTF_STS \
    DPRINTF("%s: %d: sts = 0x%08x\n", __func__, __LINE__, sts)

static const uint8_t TPM_CMD[12] =
    "\x80\x01\x00\x00\x00\x0c\x00\x00\x01\x44\x00\x00";

void tpm_tis_test_check_localities(const void *data)
{
    uint8_t locty;
    uint8_t access;
    uint32_t ifaceid;
    uint32_t capability;
    uint32_t didvid;
    uint32_t rid;

    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES; locty++) {
        access = readb(TIS_REG(0, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        capability = readl(TIS_REG(locty, TPM_TIS_REG_INTF_CAPABILITY));
        g_assert_cmpint(capability, ==, TPM_TIS_CAPABILITIES_SUPPORTED2_0);

        ifaceid = readl(TIS_REG(locty, TPM_TIS_REG_INTERFACE_ID));
        g_assert_cmpint(ifaceid, ==, TPM_TIS_IFACE_ID_SUPPORTED_FLAGS2_0);

        didvid = readl(TIS_REG(locty, TPM_TIS_REG_DID_VID));
        g_assert_cmpint(didvid, !=, 0);
        g_assert_cmpint(didvid, !=, 0xffffffff);

        rid = readl(TIS_REG(locty, TPM_TIS_REG_RID));
        g_assert_cmpint(rid, !=, 0);
        g_assert_cmpint(rid, !=, 0xffffffff);
    }
}

void tpm_tis_test_check_access_reg(const void *data)
{
    uint8_t locty;
    uint8_t access;

    /* do not test locality 4 (hw only) */
    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES - 1; locty++) {
        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of locality */
        writeb(TIS_REG(locty, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);

        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* release access */
        writeb(TIS_REG(locty, TPM_TIS_REG_ACCESS),
               TPM_TIS_ACCESS_ACTIVE_LOCALITY);
        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
    }
}

/*
 * Test case for seizing access by a higher number locality
 */
void tpm_tis_test_check_access_reg_seize(const void *data)
{
    int locty, l;
    uint8_t access;
    uint8_t pending_request_flag;

    /* do not test locality 4 (hw only) */
    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES - 1; locty++) {
        pending_request_flag = 0;

        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of locality */
        writeb(TIS_REG(locty, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);
        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* lower localities cannot seize access */
        for (l = 0; l < locty; l++) {
            /* lower locality is not active */
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* try to request use from 'l' */
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);

            /*
             * requesting use from 'l' was not possible;
             * we must see REQUEST_USE and possibly PENDING_REQUEST
             */
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /*
             * locality 'locty' must be unchanged;
             * we must see PENDING_REQUEST
             */
            access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        TPM_TIS_ACCESS_PENDING_REQUEST |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* try to seize from 'l' */
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_SEIZE);
            /* seize from 'l' was not possible */
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* locality 'locty' must be unchanged */
            access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        TPM_TIS_ACCESS_PENDING_REQUEST |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /*
             * on the next loop we will have a PENDING_REQUEST flag
             * set for locality 'l'
             */
            pending_request_flag = TPM_TIS_ACCESS_PENDING_REQUEST;
        }

        /*
         * higher localities can 'seize' access but not 'request use';
         * note: this will activate first l+1, then l+2 etc.
         */
        for (l = locty + 1; l < TPM_TIS_NUM_LOCALITIES - 1; l++) {
            /* try to 'request use' from 'l' */
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);

            /*
             * requesting use from 'l' was not possible; we should see
             * REQUEST_USE and may see PENDING_REQUEST
             */
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /*
             * locality 'l-1' must be unchanged; we should always
             * see PENDING_REQUEST from 'l' requesting access
             */
            access = readb(TIS_REG(l - 1, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        TPM_TIS_ACCESS_PENDING_REQUEST |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* try to seize from 'l' */
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_SEIZE);

            /* seize from 'l' was possible */
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* l - 1 should show that it has BEEN_SEIZED */
            access = readb(TIS_REG(l - 1, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_BEEN_SEIZED |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* clear the BEEN_SEIZED flag and make sure it's gone */
            writeb(TIS_REG(l - 1, TPM_TIS_REG_ACCESS),
                   TPM_TIS_ACCESS_BEEN_SEIZED);

            access = readb(TIS_REG(l - 1, TPM_TIS_REG_ACCESS));
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
        }

        /*
         * PENDING_REQUEST will not be set if locty = 0 since all localities
         * were active; in case of locty = 1, locality 0 will be active
         * but no PENDING_REQUEST anywhere
         */
        if (locty <= 1) {
            pending_request_flag = 0;
        }

        /* release access from l - 1; this activates locty - 1 */
        l--;

        access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
        DPRINTF_ACCESS;

        DPRINTF("%s: %d: relinquishing control on l = %d\n",
                __func__, __LINE__, l);
        writeb(TIS_REG(l, TPM_TIS_REG_ACCESS),
               TPM_TIS_ACCESS_ACTIVE_LOCALITY);

        access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
        DPRINTF_ACCESS;
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    pending_request_flag |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        for (l = locty - 1; l >= 0; l--) {
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* release this locality */
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS),
                   TPM_TIS_ACCESS_ACTIVE_LOCALITY);

            if (l == 1) {
                pending_request_flag = 0;
            }
        }

        /* no locality may be active now */
        for (l = 0; l < TPM_TIS_NUM_LOCALITIES - 1; l++) {
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
        }
    }
}

/*
 * Test case for getting access when higher number locality relinquishes access
 */
void tpm_tis_test_check_access_reg_release(const void *data)
{
    int locty, l;
    uint8_t access;
    uint8_t pending_request_flag;

    /* do not test locality 4 (hw only) */
    for (locty = TPM_TIS_NUM_LOCALITIES - 2; locty >= 0; locty--) {
        pending_request_flag = 0;

        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of locality */
        writeb(TIS_REG(locty, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);
        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of all other localities */
        for (l = 0; l < TPM_TIS_NUM_LOCALITIES - 1; l++) {
            if (l == locty) {
                continue;
            }
            /*
             * request use of locality 'l' -- we MUST see REQUEST USE and
             * may see PENDING_REQUEST
             */
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
            pending_request_flag = TPM_TIS_ACCESS_PENDING_REQUEST;
        }
        /* release locality 'locty' */
        writeb(TIS_REG(locty, TPM_TIS_REG_ACCESS),
               TPM_TIS_ACCESS_ACTIVE_LOCALITY);
        /*
         * highest locality should now be active; release it and make sure the
         * next higest locality is active afterwards
         */
        for (l = TPM_TIS_NUM_LOCALITIES - 2; l >= 0; l--) {
            if (l == locty) {
                continue;
            }
            /* 'l' should be active now */
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
            /* 'l' relinquishes access */
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS),
                   TPM_TIS_ACCESS_ACTIVE_LOCALITY);
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            if (l == 1 || (locty <= 1 && l == 2)) {
                pending_request_flag = 0;
            }
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
        }
    }
}

/*
 * Test case for transmitting packets
 */
void tpm_tis_test_check_transmit(const void *data)
{
    const TestState *s = data;
    uint8_t access;
    uint32_t sts;
    uint16_t bcount;
    size_t i;

    /* request use of locality 0 */
    writeb(TIS_REG(0, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);
    access = readb(TIS_REG(0, TPM_TIS_REG_ACCESS));
    g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

    sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
    DPRINTF_STS;

    g_assert_cmpint(sts & 0xff, ==, 0);
    g_assert_cmpint(sts & TPM_TIS_STS_TPM_FAMILY_MASK, ==,
                    TPM_TIS_STS_TPM_FAMILY2_0);

    bcount = (sts >> 8) & 0xffff;
    g_assert_cmpint(bcount, >=, 128);

    writel(TIS_REG(0, TPM_TIS_REG_STS), TPM_TIS_STS_COMMAND_READY);
    sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
    DPRINTF_STS;
    g_assert_cmpint(sts & 0xff, ==, TPM_TIS_STS_COMMAND_READY);

    /* transmit command */
    for (i = 0; i < sizeof(TPM_CMD); i++) {
        writeb(TIS_REG(0, TPM_TIS_REG_DATA_FIFO), TPM_CMD[i]);
        sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
        DPRINTF_STS;
        if (i < sizeof(TPM_CMD) - 1) {
            g_assert_cmpint(sts & 0xff, ==,
                            TPM_TIS_STS_EXPECT | TPM_TIS_STS_VALID);
        } else {
            g_assert_cmpint(sts & 0xff, ==, TPM_TIS_STS_VALID);
        }
        g_assert_cmpint((sts >> 8) & 0xffff, ==, --bcount);
    }
    /* start processing */
    writeb(TIS_REG(0, TPM_TIS_REG_STS), TPM_TIS_STS_TPM_GO);

    uint64_t end_time = g_get_monotonic_time() + 50 * G_TIME_SPAN_SECOND;
    do {
        sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
        if ((sts & TPM_TIS_STS_DATA_AVAILABLE) != 0) {
            break;
        }
    } while (g_get_monotonic_time() < end_time);

    sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
    DPRINTF_STS;
    g_assert_cmpint(sts & 0xff, == ,
                    TPM_TIS_STS_VALID | TPM_TIS_STS_DATA_AVAILABLE);
    bcount = (sts >> 8) & 0xffff;

    /* read response */
    uint8_t tpm_msg[sizeof(struct tpm_hdr)];
    g_assert_cmpint(sizeof(tpm_msg), ==, bcount);

    for (i = 0; i < sizeof(tpm_msg); i++) {
        tpm_msg[i] = readb(TIS_REG(0, TPM_TIS_REG_DATA_FIFO));
        sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
        DPRINTF_STS;
        if (sts & TPM_TIS_STS_DATA_AVAILABLE) {
            g_assert_cmpint((sts >> 8) & 0xffff, ==, --bcount);
        }
    }
    g_assert_cmpmem(tpm_msg, sizeof(tpm_msg), s->tpm_msg, sizeof(*s->tpm_msg));

    /* relinquish use of locality 0 */
    writeb(TIS_REG(0, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_ACTIVE_LOCALITY);
    access = readb(TIS_REG(0, TPM_TIS_REG_ACCESS));
}
