/*
 * QTest testcases for TPM TIS on I2C (derived from TPM TIS test)
 *
 * Copyright (c) 2023 IBM Corporation
 * Copyright (c) 2023 Red Hat, Inc.
 *
 * Authors:
 *   Stefan Berger <stefanb@linux.ibm.com>
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "libqtest-single.h"
#include "hw/acpi/tpm.h"
#include "hw/pci/pci_ids.h"
#include "qtest_aspeed.h"
#include "tpm-emu.h"

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

#define I2C_SLAVE_ADDR   0x2e
#define I2C_DEV_BUS_NUM  10

static const uint8_t TPM_CMD[12] =
    "\x80\x01\x00\x00\x00\x0c\x00\x00\x01\x44\x00\x00";

static uint32_t aspeed_bus_addr;

static uint8_t cur_locty = 0xff;

static void tpm_tis_i2c_set_locty(uint8_t locty)
{
    if (cur_locty != locty) {
        cur_locty = locty;
        aspeed_i2c_writeb(global_qtest, aspeed_bus_addr, I2C_SLAVE_ADDR,
                          TPM_I2C_REG_LOC_SEL, locty);
    }
}

static uint8_t tpm_tis_i2c_readb(uint8_t locty, uint8_t reg)
{
    tpm_tis_i2c_set_locty(locty);
    return aspeed_i2c_readb(global_qtest, aspeed_bus_addr, I2C_SLAVE_ADDR, reg);
}

static uint16_t tpm_tis_i2c_readw(uint8_t locty, uint8_t reg)
{
    tpm_tis_i2c_set_locty(locty);
    return aspeed_i2c_readw(global_qtest, aspeed_bus_addr, I2C_SLAVE_ADDR, reg);
}

static uint32_t tpm_tis_i2c_readl(uint8_t locty, uint8_t reg)
{
    tpm_tis_i2c_set_locty(locty);
    return aspeed_i2c_readl(global_qtest, aspeed_bus_addr, I2C_SLAVE_ADDR, reg);
}

static void tpm_tis_i2c_writeb(uint8_t locty, uint8_t reg, uint8_t v)
{
    if (reg != TPM_I2C_REG_LOC_SEL) {
        tpm_tis_i2c_set_locty(locty);
    }
    aspeed_i2c_writeb(global_qtest, aspeed_bus_addr, I2C_SLAVE_ADDR, reg, v);
}

static void tpm_tis_i2c_writel(uint8_t locty, uint8_t reg, uint32_t v)
{
    if (reg != TPM_I2C_REG_LOC_SEL) {
        tpm_tis_i2c_set_locty(locty);
    }
    aspeed_i2c_writel(global_qtest, aspeed_bus_addr, I2C_SLAVE_ADDR, reg, v);
}

static void tpm_tis_i2c_test_basic(const void *data)
{
    uint8_t access;
    uint32_t v, v2;

    /*
     * All register accesses below must work without locality 0 being the
     * active locality. Therefore, ensure access is released.
     */
    tpm_tis_i2c_writeb(0, TPM_I2C_REG_ACCESS,
                       TPM_TIS_ACCESS_ACTIVE_LOCALITY);
    access = tpm_tis_i2c_readb(0, TPM_I2C_REG_ACCESS);
    g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

    /* read interrupt capability -- none are supported */
    v = tpm_tis_i2c_readl(0, TPM_I2C_REG_INT_CAPABILITY);
    g_assert_cmpint(v, ==, 0);

    /* try to enable all interrupts */
    tpm_tis_i2c_writel(0, TPM_I2C_REG_INT_ENABLE, 0xffffffff);
    v = tpm_tis_i2c_readl(0, TPM_I2C_REG_INT_ENABLE);
    /* none could be enabled */
    g_assert_cmpint(v, ==, 0);

    /* enable csum */
    tpm_tis_i2c_writeb(0, TPM_I2C_REG_DATA_CSUM_ENABLE, TPM_DATA_CSUM_ENABLED);
    /* check csum enable register has bit 0 set */
    v = tpm_tis_i2c_readb(0, TPM_I2C_REG_DATA_CSUM_ENABLE);
    g_assert_cmpint(v, ==, TPM_DATA_CSUM_ENABLED);
    /* reading it as 32bit register returns same result */
    v = tpm_tis_i2c_readl(0, TPM_I2C_REG_DATA_CSUM_ENABLE);
    g_assert_cmpint(v, ==, TPM_DATA_CSUM_ENABLED);

    /* disable csum */
    tpm_tis_i2c_writeb(0, TPM_I2C_REG_DATA_CSUM_ENABLE, 0);
    /* check csum enable register has bit 0 clear */
    v = tpm_tis_i2c_readb(0, TPM_I2C_REG_DATA_CSUM_ENABLE);
    g_assert_cmpint(v, ==, 0);

    /* write to unsupported register '1' */
    tpm_tis_i2c_writel(0, 1, 0x12345678);
    v = tpm_tis_i2c_readl(0, 1);
    g_assert_cmpint(v, ==, 0xffffffff);

    /* request use of locality */
    tpm_tis_i2c_writeb(0, TPM_I2C_REG_ACCESS, TPM_TIS_ACCESS_REQUEST_USE);

    /* read byte from STS + 3 */
    v = tpm_tis_i2c_readb(0, TPM_I2C_REG_STS + 3);
    g_assert_cmpint(v, ==, 0);

    /* check STS after writing to STS + 3 */
    v = tpm_tis_i2c_readl(0, TPM_I2C_REG_STS);
    tpm_tis_i2c_writeb(0, TPM_I2C_REG_STS + 3, 0xf);
    v2 = tpm_tis_i2c_readl(0, TPM_I2C_REG_STS);
    g_assert_cmpint(v, ==, v2);

    /* release access */
    tpm_tis_i2c_writeb(0, TPM_I2C_REG_ACCESS,
                       TPM_TIS_ACCESS_ACTIVE_LOCALITY);

    /* select locality 5 -- must not be possible */
    tpm_tis_i2c_writeb(0, TPM_I2C_REG_LOC_SEL, 5);
    v = tpm_tis_i2c_readb(0, TPM_I2C_REG_LOC_SEL);
    g_assert_cmpint(v, ==, 0);
}

static void tpm_tis_i2c_test_check_localities(const void *data)
{
    uint8_t locty, l;
    uint8_t access;
    uint32_t capability, i2c_cap;
    uint32_t didvid;
    uint32_t rid;

    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES; locty++) {
        access = tpm_tis_i2c_readb(locty, TPM_I2C_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        capability = tpm_tis_i2c_readl(locty, TPM_I2C_REG_INTF_CAPABILITY);
        i2c_cap = (TPM_I2C_CAP_INTERFACE_TYPE |
                   TPM_I2C_CAP_INTERFACE_VER  |
                   TPM_I2C_CAP_TPM2_FAMILY    |
                   TPM_I2C_CAP_LOCALITY_CAP   |
                   TPM_I2C_CAP_BUS_SPEED      |
                   TPM_I2C_CAP_DEV_ADDR_CHANGE);
        g_assert_cmpint(capability, ==, i2c_cap);

        didvid = tpm_tis_i2c_readl(locty, TPM_I2C_REG_DID_VID);
        g_assert_cmpint(didvid, ==, (1 << 16) | PCI_VENDOR_ID_IBM);

        rid = tpm_tis_i2c_readl(locty, TPM_I2C_REG_RID);
        g_assert_cmpint(rid, !=, 0);
        g_assert_cmpint(rid, !=, 0xffffffff);

        /* locality selection must be at locty */
        l = tpm_tis_i2c_readb(locty, TPM_I2C_REG_LOC_SEL);
        g_assert_cmpint(l, ==, locty);
    }
}

static void tpm_tis_i2c_test_check_access_reg(const void *data)
{
    uint8_t locty;
    uint8_t access;

    /* do not test locality 4 (hw only) */
    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES - 1; locty++) {
        access = tpm_tis_i2c_readb(locty, TPM_I2C_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of locality */
        tpm_tis_i2c_writeb(locty, TPM_I2C_REG_ACCESS,
                           TPM_TIS_ACCESS_REQUEST_USE);

        access = tpm_tis_i2c_readb(locty, TPM_I2C_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* release access */
        tpm_tis_i2c_writeb(locty, TPM_I2C_REG_ACCESS,
                           TPM_TIS_ACCESS_ACTIVE_LOCALITY);
        access = tpm_tis_i2c_readb(locty, TPM_I2C_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
    }
}

/*
 * Test case for seizing access by a higher number locality
 */
static void tpm_tis_i2c_test_check_access_reg_seize(const void *data)
{
    int locty, l;
    uint8_t access;
    uint8_t pending_request_flag;

    /* do not test locality 4 (hw only) */
    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES - 1; locty++) {
        pending_request_flag = 0;

        access = tpm_tis_i2c_readb(locty, TPM_I2C_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of locality */
        tpm_tis_i2c_writeb(locty,
                           TPM_I2C_REG_ACCESS, TPM_TIS_ACCESS_REQUEST_USE);
        access = tpm_tis_i2c_readb(locty, TPM_I2C_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* lower localities cannot seize access */
        for (l = 0; l < locty; l++) {
            /* lower locality is not active */
            access = tpm_tis_i2c_readb(l, TPM_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* try to request use from 'l' */
            tpm_tis_i2c_writeb(l,
                               TPM_I2C_REG_ACCESS,
                               TPM_TIS_ACCESS_REQUEST_USE);

            /*
             * requesting use from 'l' was not possible;
             * we must see REQUEST_USE and possibly PENDING_REQUEST
             */
            access = tpm_tis_i2c_readb(l, TPM_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /*
             * locality 'locty' must be unchanged;
             * we must see PENDING_REQUEST
             */
            access = tpm_tis_i2c_readb(locty, TPM_I2C_REG_ACCESS);
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        TPM_TIS_ACCESS_PENDING_REQUEST |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* try to seize from 'l' */
            tpm_tis_i2c_writeb(l,
                               TPM_I2C_REG_ACCESS, TPM_TIS_ACCESS_SEIZE);
            /* seize from 'l' was not possible */
            access = tpm_tis_i2c_readb(l, TPM_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* locality 'locty' must be unchanged */
            access = tpm_tis_i2c_readb(locty, TPM_I2C_REG_ACCESS);
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
            tpm_tis_i2c_writeb(l, TPM_I2C_REG_ACCESS,
                               TPM_TIS_ACCESS_REQUEST_USE);

            /*
             * requesting use from 'l' was not possible; we should see
             * REQUEST_USE and may see PENDING_REQUEST
             */
            access = tpm_tis_i2c_readb(l, TPM_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /*
             * locality 'l-1' must be unchanged; we should always
             * see PENDING_REQUEST from 'l' requesting access
             */
            access = tpm_tis_i2c_readb(l - 1, TPM_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        TPM_TIS_ACCESS_PENDING_REQUEST |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* try to seize from 'l' */
            tpm_tis_i2c_writeb(l, TPM_I2C_REG_ACCESS, TPM_TIS_ACCESS_SEIZE);

            /* seize from 'l' was possible */
            access = tpm_tis_i2c_readb(l, TPM_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* l - 1 should show that it has BEEN_SEIZED */
            access = tpm_tis_i2c_readb(l - 1, TPM_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_BEEN_SEIZED |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* clear the BEEN_SEIZED flag and make sure it's gone */
            tpm_tis_i2c_writeb(l - 1, TPM_I2C_REG_ACCESS,
                               TPM_TIS_ACCESS_BEEN_SEIZED);

            access = tpm_tis_i2c_readb(l - 1, TPM_I2C_REG_ACCESS);
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

        access = tpm_tis_i2c_readb(l, TPM_I2C_REG_ACCESS);
        DPRINTF_ACCESS;

        DPRINTF("%s: %d: relinquishing control on l = %d\n",
                __func__, __LINE__, l);
        tpm_tis_i2c_writeb(l, TPM_I2C_REG_ACCESS,
                           TPM_TIS_ACCESS_ACTIVE_LOCALITY);

        access = tpm_tis_i2c_readb(l, TPM_I2C_REG_ACCESS);
        DPRINTF_ACCESS;
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    pending_request_flag |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        for (l = locty - 1; l >= 0; l--) {
            access = tpm_tis_i2c_readb(l, TPM_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* release this locality */
            tpm_tis_i2c_writeb(l, TPM_I2C_REG_ACCESS,
                               TPM_TIS_ACCESS_ACTIVE_LOCALITY);

            if (l == 1) {
                pending_request_flag = 0;
            }
        }

        /* no locality may be active now */
        for (l = 0; l < TPM_TIS_NUM_LOCALITIES - 1; l++) {
            access = tpm_tis_i2c_readb(l, TPM_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
        }
    }
}

/*
 * Test case for getting access when higher number locality relinquishes access
 */
static void tpm_tis_i2c_test_check_access_reg_release(const void *data)
{
    int locty, l;
    uint8_t access;
    uint8_t pending_request_flag;

    /* do not test locality 4 (hw only) */
    for (locty = TPM_TIS_NUM_LOCALITIES - 2; locty >= 0; locty--) {
        pending_request_flag = 0;

        access = tpm_tis_i2c_readb(locty, TPM_I2C_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of locality */
        tpm_tis_i2c_writeb(locty, TPM_I2C_REG_ACCESS,
                           TPM_TIS_ACCESS_REQUEST_USE);
        access = tpm_tis_i2c_readb(locty, TPM_I2C_REG_ACCESS);
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
            tpm_tis_i2c_writeb(l, TPM_I2C_REG_ACCESS,
                               TPM_TIS_ACCESS_REQUEST_USE);
            access = tpm_tis_i2c_readb(l, TPM_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
            pending_request_flag = TPM_TIS_ACCESS_PENDING_REQUEST;
        }
        /* release locality 'locty' */
        tpm_tis_i2c_writeb(locty, TPM_I2C_REG_ACCESS,
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
            access = tpm_tis_i2c_readb(l, TPM_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
            /* 'l' relinquishes access */
            tpm_tis_i2c_writeb(l, TPM_I2C_REG_ACCESS,
                               TPM_TIS_ACCESS_ACTIVE_LOCALITY);
            access = tpm_tis_i2c_readb(l, TPM_I2C_REG_ACCESS);
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
static void tpm_tis_i2c_test_check_transmit(const void *data)
{
    const TPMTestState *s = data;
    uint8_t access;
    uint32_t sts, v;
    uint16_t bcount, csum, bcount2;
    size_t i;

    /* enable csum */
    tpm_tis_i2c_writeb(0, TPM_I2C_REG_DATA_CSUM_ENABLE, TPM_DATA_CSUM_ENABLED);
    /* check csum enable register has bit 0 set */
    v = tpm_tis_i2c_readb(0, TPM_I2C_REG_DATA_CSUM_ENABLE);
    g_assert_cmpint(v, ==, TPM_DATA_CSUM_ENABLED);
    /* reading it as 32bit register returns same result */
    v = tpm_tis_i2c_readl(0, TPM_I2C_REG_DATA_CSUM_ENABLE);
    g_assert_cmpint(v, ==, TPM_DATA_CSUM_ENABLED);

    /* request use of locality 0 */
    tpm_tis_i2c_writeb(0, TPM_I2C_REG_ACCESS, TPM_TIS_ACCESS_REQUEST_USE);
    access = tpm_tis_i2c_readb(0, TPM_I2C_REG_ACCESS);
    g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

    sts = tpm_tis_i2c_readl(0, TPM_I2C_REG_STS);
    DPRINTF_STS;

    g_assert_cmpint(sts & 0xff, ==, 0);

    bcount = (sts >> 8) & 0xffff;
    g_assert_cmpint(bcount, >=, 128);

    /* read bcount from STS + 1 must work also */
    bcount2 = tpm_tis_i2c_readw(0, TPM_I2C_REG_STS + 1);
    g_assert_cmpint(bcount, ==, bcount2);

    /* ic2 must have bits 26-31 zero */
    g_assert_cmpint(sts & (0x1f << 26), ==, 0);

    tpm_tis_i2c_writel(0, TPM_I2C_REG_STS, TPM_TIS_STS_COMMAND_READY);
    sts = tpm_tis_i2c_readl(0, TPM_I2C_REG_STS);
    DPRINTF_STS;
    g_assert_cmpint(sts & 0xff, ==, TPM_TIS_STS_COMMAND_READY);

    /* transmit command */
    for (i = 0; i < sizeof(TPM_CMD); i++) {
        tpm_tis_i2c_writeb(0, TPM_I2C_REG_DATA_FIFO, TPM_CMD[i]);
        sts = tpm_tis_i2c_readl(0, TPM_I2C_REG_STS);
        DPRINTF_STS;
        if (i < sizeof(TPM_CMD) - 1) {
            g_assert_cmpint(sts & 0xff, ==,
                            TPM_TIS_STS_EXPECT | TPM_TIS_STS_VALID);
        } else {
            g_assert_cmpint(sts & 0xff, ==, TPM_TIS_STS_VALID);
        }
        g_assert_cmpint((sts >> 8) & 0xffff, ==, --bcount);
    }
    /* read the checksum */
    csum = tpm_tis_i2c_readw(0, TPM_I2C_REG_DATA_CSUM_GET);
    g_assert_cmpint(csum, ==, 0x6733);

    /* start processing */
    tpm_tis_i2c_writeb(0, TPM_I2C_REG_STS, TPM_TIS_STS_TPM_GO);

    uint64_t end_time = g_get_monotonic_time() + 50 * G_TIME_SPAN_SECOND;
    do {
        sts = tpm_tis_i2c_readl(0, TPM_I2C_REG_STS);
        if ((sts & TPM_TIS_STS_DATA_AVAILABLE) != 0) {
            break;
        }
    } while (g_get_monotonic_time() < end_time);

    sts = tpm_tis_i2c_readl(0, TPM_I2C_REG_STS);
    DPRINTF_STS;
    g_assert_cmpint(sts & 0xff, == ,
                    TPM_TIS_STS_VALID | TPM_TIS_STS_DATA_AVAILABLE);
    bcount = (sts >> 8) & 0xffff;

    /* read response */
    uint8_t tpm_msg[sizeof(struct tpm_hdr)];
    g_assert_cmpint(sizeof(tpm_msg), ==, bcount);

    for (i = 0; i < sizeof(tpm_msg); i++) {
        tpm_msg[i] = tpm_tis_i2c_readb(0, TPM_I2C_REG_DATA_FIFO);
        sts = tpm_tis_i2c_readl(0, TPM_I2C_REG_STS);
        DPRINTF_STS;
        if (sts & TPM_TIS_STS_DATA_AVAILABLE) {
            g_assert_cmpint((sts >> 8) & 0xffff, ==, --bcount);
        }
    }
    g_assert_cmpmem(tpm_msg, sizeof(tpm_msg), s->tpm_msg, sizeof(*s->tpm_msg));

    /* relinquish use of locality 0 */
    tpm_tis_i2c_writeb(0,
                       TPM_I2C_REG_ACCESS, TPM_TIS_ACCESS_ACTIVE_LOCALITY);
    access = tpm_tis_i2c_readb(0, TPM_I2C_REG_ACCESS);
}

int main(int argc, char **argv)
{
    int ret;
    char *args;
    char *tmp_path = g_dir_make_tmp("qemu-tpm-tis-i2c-test.XXXXXX", NULL);
    GThread *thread;
    TPMTestState test;

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    test.addr = g_new0(SocketAddress, 1);
    test.addr->type = SOCKET_ADDRESS_TYPE_UNIX;
    test.addr->u.q_unix.path = g_build_filename(tmp_path, "sock", NULL);
    g_mutex_init(&test.data_mutex);
    g_cond_init(&test.data_cond);
    test.data_cond_signal = false;
    test.tpm_version = TPM_VERSION_2_0;

    thread = g_thread_new(NULL, tpm_emu_ctrl_thread, &test);
    tpm_emu_test_wait_cond(&test);

    aspeed_bus_addr = ast2600_i2c_calc_bus_addr(I2C_DEV_BUS_NUM);

    args = g_strdup_printf(
        "-machine rainier-bmc -accel tcg "
        "-chardev socket,id=chr,path=%s "
        "-tpmdev emulator,id=tpm0,chardev=chr "
        "-device tpm-tis-i2c,tpmdev=tpm0,bus=aspeed.i2c.bus.%d,address=0x%x",
        test.addr->u.q_unix.path,
        I2C_DEV_BUS_NUM,
        I2C_SLAVE_ADDR);
    qtest_start(args);

    qtest_add_data_func("/tpm-tis-i2c/test_basic", &test,
                        tpm_tis_i2c_test_basic);

    qtest_add_data_func("/tpm-tis-i2c/test_check_localities", &test,
                        tpm_tis_i2c_test_check_localities);

    qtest_add_data_func("/tpm-tis-i2c/check_access_reg", &test,
                        tpm_tis_i2c_test_check_access_reg);

    qtest_add_data_func("/tpm-tis-i2c/check_access_reg_seize", &test,
                        tpm_tis_i2c_test_check_access_reg_seize);

    qtest_add_data_func("/tpm-tis-i2c/check_access_reg_release", &test,
                        tpm_tis_i2c_test_check_access_reg_release);

    qtest_add_data_func("/tpm-tis-i2c/test_check_transmit", &test,
                        tpm_tis_i2c_test_check_transmit);

    ret = g_test_run();

    qtest_end();

    g_thread_join(thread);
    g_unlink(test.addr->u.q_unix.path);
    qapi_free_SocketAddress(test.addr);
    g_rmdir(tmp_path);
    g_free(tmp_path);
    g_free(args);
    return ret;
}
