/*
 * Hard disk geometry test cases.
 *
 * Copyright (c) 2012 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * Covers only IDE and tests only CMOS contents.  Better than nothing.
 * Improvements welcome.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "libqtest.h"

static char *create_test_img(int secs)
{
    char *template = strdup("/tmp/qtest.XXXXXX");
    int fd, ret;

    fd = mkstemp(template);
    g_assert(fd >= 0);
    ret = ftruncate(fd, (off_t)secs * 512);
    g_assert(ret == 0);
    close(fd);
    return template;
}

typedef struct {
    int cyls, heads, secs, trans;
} CHST;

typedef enum {
    mbr_blank, mbr_lba, mbr_chs,
    mbr_last
} MBRcontents;

typedef enum {
    /* order is relevant */
    backend_small, backend_large, backend_empty,
    backend_last
} Backend;

static const int img_secs[backend_last] = {
    [backend_small] = 61440,
    [backend_large] = 8388608,
    [backend_empty] = -1,
};

static const CHST hd_chst[backend_last][mbr_last] = {
    [backend_small] = {
        [mbr_blank] = { 60, 16, 63, 0 },
        [mbr_lba]   = { 60, 16, 63, 2 },
        [mbr_chs]   = { 60, 16, 63, 0 }
    },
    [backend_large] = {
        [mbr_blank] = { 8322, 16, 63, 1 },
        [mbr_lba]   = { 8322, 16, 63, 1 },
        [mbr_chs]   = { 8322, 16, 63, 0 }
    },
};

static const char *img_file_name[backend_last];

static const CHST *cur_ide[4];

static bool is_hd(const CHST *expected_chst)
{
    return expected_chst && expected_chst->cyls;
}

static void test_cmos_byte(int reg, int expected)
{
    enum { cmos_base = 0x70 };
    int actual;

    outb(cmos_base + 0, reg);
    actual = inb(cmos_base + 1);
    g_assert(actual == expected);
}

static void test_cmos_bytes(int reg0, int n, uint8_t expected[])
{
    int i;

    for (i = 0; i < 9; i++) {
        test_cmos_byte(reg0 + i, expected[i]);
    }
}

static void test_cmos_disk_data(void)
{
    test_cmos_byte(0x12,
                   (is_hd(cur_ide[0]) ? 0xf0 : 0) |
                   (is_hd(cur_ide[1]) ? 0x0f : 0));
}

static void test_cmos_drive_cyl(int reg0, const CHST *expected_chst)
{
    if (is_hd(expected_chst)) {
        int c = expected_chst->cyls;
        int h = expected_chst->heads;
        int s = expected_chst->secs;
        uint8_t expected_bytes[9] = {
            c & 0xff, c >> 8, h, 0xff, 0xff, 0xc0 | ((h > 8) << 3),
            c & 0xff, c >> 8, s
        };
        test_cmos_bytes(reg0, 9, expected_bytes);
    } else {
        int i;

        for (i = 0; i < 9; i++) {
            test_cmos_byte(reg0 + i, 0);
        }
    }
}

static void test_cmos_drive1(void)
{
    test_cmos_byte(0x19, is_hd(cur_ide[0]) ? 47 : 0);
    test_cmos_drive_cyl(0x1b, cur_ide[0]);
}

static void test_cmos_drive2(void)
{
    test_cmos_byte(0x1a, is_hd(cur_ide[1]) ? 47 : 0);
    test_cmos_drive_cyl(0x24, cur_ide[1]);
}

static void test_cmos_disktransflag(void)
{
    int val, i;

    val = 0;
    for (i = 0; i < ARRAY_SIZE(cur_ide); i++) {
        if (is_hd(cur_ide[i])) {
            val |= cur_ide[i]->trans << (2 * i);
        }
    }
    test_cmos_byte(0x39, val);
}

static void test_cmos(void)
{
    test_cmos_disk_data();
    test_cmos_drive1();
    test_cmos_drive2();
    test_cmos_disktransflag();
}

static int append_arg(int argc, char *argv[], int argv_sz, char *arg)
{
    g_assert(argc + 1 < argv_sz);
    argv[argc++] = arg;
    argv[argc] = NULL;
    return argc;
}

static int setup_common(char *argv[], int argv_sz)
{
    memset(cur_ide, 0, sizeof(cur_ide));
    return append_arg(0, argv, argv_sz,
                      g_strdup("-nodefaults"));
}

static void setup_mbr(int img_idx, MBRcontents mbr)
{
    static const uint8_t part_lba[16] = {
        /* chs 0,1,1 (lba 63) to chs 0,127,63 (8001 sectors) */
        0x80, 1, 1, 0, 6, 127, 63, 0, 63, 0, 0, 0, 0x41, 0x1F, 0, 0,
    };
    static const uint8_t part_chs[16] = {
        /* chs 0,1,1 (lba 63) to chs 7,15,63 (8001 sectors) */
        0x80, 1, 1, 0, 6,  15, 63, 7, 63, 0, 0, 0, 0x41, 0x1F, 0, 0,
    };
    uint8_t buf[512];
    int fd, ret;

    memset(buf, 0, sizeof(buf));

    if (mbr != mbr_blank) {
        buf[0x1fe] = 0x55;
        buf[0x1ff] = 0xAA;
        memcpy(buf + 0x1BE, mbr == mbr_lba ? part_lba : part_chs, 16);
    }

    fd = open(img_file_name[img_idx], O_WRONLY);
    g_assert(fd >= 0);
    ret = write(fd, buf, sizeof(buf));
    g_assert(ret == sizeof(buf));
    close(fd);
}

static int setup_ide(int argc, char *argv[], int argv_sz,
                     int ide_idx, const char *dev, int img_idx,
                     MBRcontents mbr, const char *opts)
{
    char *s1, *s2, *s3;

    s1 = g_strdup_printf("-drive id=drive%d,if=%s",
                         ide_idx, dev ? "none" : "ide");
    s2 = dev ? g_strdup("") : g_strdup_printf(",index=%d", ide_idx);

    if (img_secs[img_idx] >= 0) {
        setup_mbr(img_idx, mbr);
        s3 = g_strdup_printf(",format=raw,file=%s", img_file_name[img_idx]);
    } else {
        s3 = g_strdup(",media=cdrom");
    }
    argc = append_arg(argc, argv, argv_sz,
                      g_strdup_printf("%s%s%s%s", s1, s2, s3, opts));
    g_free(s1);
    g_free(s2);
    g_free(s3);

    if (dev) {
        argc = append_arg(argc, argv, argv_sz,
                          g_strdup_printf("-device %s,drive=drive%d,"
                                          "bus=ide.%d,unit=%d",
                                          dev, ide_idx,
                                          ide_idx / 2, ide_idx % 2));
    }
    return argc;
}

/*
 * Test case: no IDE devices
 */
static void test_ide_none(void)
{
    char *argv[256];

    setup_common(argv, ARRAY_SIZE(argv));
    qtest_start(g_strjoinv(" ", argv));
    test_cmos();
    qtest_end();
}

static void test_ide_mbr(bool use_device, MBRcontents mbr)
{
    char *argv[256];
    int argc;
    Backend i;
    const char *dev;

    argc = setup_common(argv, ARRAY_SIZE(argv));
    for (i = 0; i < backend_last; i++) {
        cur_ide[i] = &hd_chst[i][mbr];
        dev = use_device ? (is_hd(cur_ide[i]) ? "ide-hd" : "ide-cd") : NULL;
        argc = setup_ide(argc, argv, ARRAY_SIZE(argv), i, dev, i, mbr, "");
    }
    qtest_start(g_strjoinv(" ", argv));
    test_cmos();
    qtest_end();
}

/*
 * Test case: IDE devices (if=ide) with blank MBRs
 */
static void test_ide_drive_mbr_blank(void)
{
    test_ide_mbr(false, mbr_blank);
}

/*
 * Test case: IDE devices (if=ide) with MBRs indicating LBA is in use
 */
static void test_ide_drive_mbr_lba(void)
{
    test_ide_mbr(false, mbr_lba);
}

/*
 * Test case: IDE devices (if=ide) with MBRs indicating CHS is in use
 */
static void test_ide_drive_mbr_chs(void)
{
    test_ide_mbr(false, mbr_chs);
}

/*
 * Test case: IDE devices (if=none) with blank MBRs
 */
static void test_ide_device_mbr_blank(void)
{
    test_ide_mbr(true, mbr_blank);
}

/*
 * Test case: IDE devices (if=none) with MBRs indicating LBA is in use
 */
static void test_ide_device_mbr_lba(void)
{
    test_ide_mbr(true, mbr_lba);
}

/*
 * Test case: IDE devices (if=none) with MBRs indicating CHS is in use
 */
static void test_ide_device_mbr_chs(void)
{
    test_ide_mbr(true, mbr_chs);
}

static void test_ide_drive_user(const char *dev, bool trans)
{
    char *argv[256], *opts;
    int argc;
    int secs = img_secs[backend_small];
    const CHST expected_chst = { secs / (4 * 32) , 4, 32, trans };

    argc = setup_common(argv, ARRAY_SIZE(argv));
    opts = g_strdup_printf("%s,%s%scyls=%d,heads=%d,secs=%d",
                           dev ?: "",
                           trans && dev ? "bios-chs-" : "",
                           trans ? "trans=lba," : "",
                           expected_chst.cyls, expected_chst.heads,
                           expected_chst.secs);
    cur_ide[0] = &expected_chst;
    argc = setup_ide(argc, argv, ARRAY_SIZE(argv),
                     0, dev ? opts : NULL, backend_small, mbr_chs,
                     dev ? "" : opts);
    g_free(opts);
    qtest_start(g_strjoinv(" ", argv));
    test_cmos();
    qtest_end();
}

/*
 * Test case: IDE device (if=ide) with explicit CHS
 */
static void test_ide_drive_user_chs(void)
{
    test_ide_drive_user(NULL, false);
}

/*
 * Test case: IDE device (if=ide) with explicit CHS and translation
 */
static void test_ide_drive_user_chst(void)
{
    test_ide_drive_user(NULL, true);
}

/*
 * Test case: IDE device (if=none) with explicit CHS
 */
static void test_ide_device_user_chs(void)
{
    test_ide_drive_user("ide-hd", false);
}

/*
 * Test case: IDE device (if=none) with explicit CHS and translation
 */
static void test_ide_device_user_chst(void)
{
    test_ide_drive_user("ide-hd", true);
}

/*
 * Test case: IDE devices (if=ide), but use index=0 for CD-ROM
 */
static void test_ide_drive_cd_0(void)
{
    char *argv[256];
    int argc, ide_idx;
    Backend i;

    argc = setup_common(argv, ARRAY_SIZE(argv));
    for (i = 0; i <= backend_empty; i++) {
        ide_idx = backend_empty - i;
        cur_ide[ide_idx] = &hd_chst[i][mbr_blank];
        argc = setup_ide(argc, argv, ARRAY_SIZE(argv),
                         ide_idx, NULL, i, mbr_blank, "");
    }
    qtest_start(g_strjoinv(" ", argv));
    test_cmos();
    qtest_end();
}

int main(int argc, char **argv)
{
    Backend i;
    int ret;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < backend_last; i++) {
        if (img_secs[i] >= 0) {
            img_file_name[i] = create_test_img(img_secs[i]);
        } else {
            img_file_name[i] = NULL;
        }
    }

    qtest_add_func("hd-geo/ide/none", test_ide_none);
    qtest_add_func("hd-geo/ide/drive/mbr/blank", test_ide_drive_mbr_blank);
    qtest_add_func("hd-geo/ide/drive/mbr/lba", test_ide_drive_mbr_lba);
    qtest_add_func("hd-geo/ide/drive/mbr/chs", test_ide_drive_mbr_chs);
    qtest_add_func("hd-geo/ide/drive/user/chs", test_ide_drive_user_chs);
    qtest_add_func("hd-geo/ide/drive/user/chst", test_ide_drive_user_chst);
    qtest_add_func("hd-geo/ide/drive/cd_0", test_ide_drive_cd_0);
    qtest_add_func("hd-geo/ide/device/mbr/blank", test_ide_device_mbr_blank);
    qtest_add_func("hd-geo/ide/device/mbr/lba", test_ide_device_mbr_lba);
    qtest_add_func("hd-geo/ide/device/mbr/chs", test_ide_device_mbr_chs);
    qtest_add_func("hd-geo/ide/device/user/chs", test_ide_device_user_chs);
    qtest_add_func("hd-geo/ide/device/user/chst", test_ide_device_user_chst);

    ret = g_test_run();

    for (i = 0; i < backend_last; i++) {
        unlink(img_file_name[i]);
    }

    return ret;
}
