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
#include "qemu/bswap.h"
#include "qobject/qlist.h"
#include "libqtest.h"
#include "libqos/fw_cfg.h"
#include "libqos/libqos.h"
#include "standard-headers/linux/qemu_fw_cfg.h"

#define ARGV_SIZE 256

static char *create_test_img(int secs)
{
    char *template;
    int fd, ret;

    fd = g_file_open_tmp("qtest.XXXXXX", &template, NULL);
    g_assert(fd >= 0);
    ret = ftruncate(fd, (off_t)secs * 512);
    close(fd);

    if (ret) {
        g_free(template);
        template = NULL;
    }

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

static char *img_file_name[backend_last];

static const CHST *cur_ide[4];

static bool is_hd(const CHST *expected_chst)
{
    return expected_chst && expected_chst->cyls;
}

static void test_cmos_byte(QTestState *qts, int reg, int expected)
{
    enum { cmos_base = 0x70 };
    int actual;

    qtest_outb(qts, cmos_base + 0, reg);
    actual = qtest_inb(qts, cmos_base + 1);
    g_assert(actual == expected);
}

static void test_cmos_bytes(QTestState *qts, int reg0, int n,
                            uint8_t expected[])
{
    int i;

    for (i = 0; i < 9; i++) {
        test_cmos_byte(qts, reg0 + i, expected[i]);
    }
}

static void test_cmos_disk_data(QTestState *qts)
{
    test_cmos_byte(qts, 0x12,
                   (is_hd(cur_ide[0]) ? 0xf0 : 0) |
                   (is_hd(cur_ide[1]) ? 0x0f : 0));
}

static void test_cmos_drive_cyl(QTestState *qts, int reg0,
                                const CHST *expected_chst)
{
    if (is_hd(expected_chst)) {
        int c = expected_chst->cyls;
        int h = expected_chst->heads;
        int s = expected_chst->secs;
        uint8_t expected_bytes[9] = {
            c & 0xff, c >> 8, h, 0xff, 0xff, 0xc0 | ((h > 8) << 3),
            c & 0xff, c >> 8, s
        };
        test_cmos_bytes(qts, reg0, 9, expected_bytes);
    } else {
        int i;

        for (i = 0; i < 9; i++) {
            test_cmos_byte(qts, reg0 + i, 0);
        }
    }
}

static void test_cmos_drive1(QTestState *qts)
{
    test_cmos_byte(qts, 0x19, is_hd(cur_ide[0]) ? 47 : 0);
    test_cmos_drive_cyl(qts, 0x1b, cur_ide[0]);
}

static void test_cmos_drive2(QTestState *qts)
{
    test_cmos_byte(qts, 0x1a, is_hd(cur_ide[1]) ? 47 : 0);
    test_cmos_drive_cyl(qts, 0x24, cur_ide[1]);
}

static void test_cmos_disktransflag(QTestState *qts)
{
    int val, i;

    val = 0;
    for (i = 0; i < ARRAY_SIZE(cur_ide); i++) {
        if (is_hd(cur_ide[i])) {
            val |= cur_ide[i]->trans << (2 * i);
        }
    }
    test_cmos_byte(qts, 0x39, val);
}

static void test_cmos(QTestState *qts)
{
    test_cmos_disk_data(qts);
    test_cmos_drive1(qts);
    test_cmos_drive2(qts);
    test_cmos_disktransflag(qts);
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
    int new_argc;
    memset(cur_ide, 0, sizeof(cur_ide));
    new_argc = append_arg(0, argv, argv_sz,
                          g_strdup("-nodefaults"));
    new_argc = append_arg(new_argc, argv, argv_sz,
                          g_strdup("-machine"));
    new_argc = append_arg(new_argc, argv, argv_sz,
                          g_strdup("pc"));
    return new_argc;
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
                     MBRcontents mbr)
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
                      g_strdup_printf("%s%s%s", s1, s2, s3));
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
    char **argv = g_new0(char *, ARGV_SIZE);
    char *args;
    QTestState *qts;

    setup_common(argv, ARGV_SIZE);
    args = g_strjoinv(" ", argv);
    qts = qtest_init(args);
    g_strfreev(argv);
    g_free(args);
    test_cmos(qts);
    qtest_quit(qts);
}

static void test_ide_mbr(bool use_device, MBRcontents mbr)
{
    char **argv = g_new0(char *, ARGV_SIZE);
    char *args;
    int argc;
    Backend i;
    const char *dev;
    QTestState *qts;

    argc = setup_common(argv, ARGV_SIZE);
    for (i = 0; i < backend_last; i++) {
        cur_ide[i] = &hd_chst[i][mbr];
        dev = use_device ? (is_hd(cur_ide[i]) ? "ide-hd" : "ide-cd") : NULL;
        argc = setup_ide(argc, argv, ARGV_SIZE, i, dev, i, mbr);
    }
    args = g_strjoinv(" ", argv);
    qts = qtest_init(args);
    g_strfreev(argv);
    g_free(args);
    test_cmos(qts);
    qtest_quit(qts);
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
    char **argv = g_new0(char *, ARGV_SIZE);
    char *args, *opts;
    int argc;
    int secs = img_secs[backend_small];
    const CHST expected_chst = { secs / (4 * 32) , 4, 32, trans };
    QTestState *qts;

    argc = setup_common(argv, ARGV_SIZE);
    opts = g_strdup_printf("%s,%scyls=%d,heads=%d,secs=%d",
                           dev, trans ? "bios-chs-trans=lba," : "",
                           expected_chst.cyls, expected_chst.heads,
                           expected_chst.secs);
    cur_ide[0] = &expected_chst;
    argc = setup_ide(argc, argv, ARGV_SIZE, 0, opts, backend_small, mbr_chs);
    g_free(opts);
    args = g_strjoinv(" ", argv);
    qts = qtest_init(args);
    g_strfreev(argv);
    g_free(args);
    test_cmos(qts);
    qtest_quit(qts);
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
    char **argv = g_new0(char *, ARGV_SIZE);
    char *args;
    int argc, ide_idx;
    Backend i;
    QTestState *qts;

    argc = setup_common(argv, ARGV_SIZE);
    for (i = 0; i <= backend_empty; i++) {
        ide_idx = backend_empty - i;
        cur_ide[ide_idx] = &hd_chst[i][mbr_blank];
        argc = setup_ide(argc, argv, ARGV_SIZE, ide_idx, NULL, i, mbr_blank);
    }
    args = g_strjoinv(" ", argv);
    qts = qtest_init(args);
    g_strfreev(argv);
    g_free(args);
    test_cmos(qts);
    qtest_quit(qts);
}

typedef struct {
    bool active;
    uint32_t head;
    uint32_t sector;
    uint32_t cyl;
    uint32_t end_head;
    uint32_t end_sector;
    uint32_t end_cyl;
    uint32_t start_sect;
    uint32_t nr_sects;
} MBRpartitions[4];

static MBRpartitions empty_mbr = { {false, 0, 0, 0, 0, 0, 0, 0, 0},
                                   {false, 0, 0, 0, 0, 0, 0, 0, 0},
                                   {false, 0, 0, 0, 0, 0, 0, 0, 0},
                                   {false, 0, 0, 0, 0, 0, 0, 0, 0} };

static char *create_qcow2_with_mbr(MBRpartitions mbr, uint64_t sectors)
{
    g_autofree char *raw_path = NULL;
    char *qcow2_path;
    char cmd[100 + 2 * PATH_MAX];
    uint8_t buf[512] = {};
    int i, ret, fd, offset;
    uint64_t qcow2_size = sectors * 512;
    uint8_t status, parttype, head, sector, cyl;
    char *qemu_img_path;
    char *qemu_img_abs_path;

    offset = 0xbe;

    for (i = 0; i < 4; i++) {
        status = mbr[i].active ? 0x80 : 0x00;
        g_assert(mbr[i].head < 256);
        g_assert(mbr[i].sector < 64);
        g_assert(mbr[i].cyl < 1024);
        head = mbr[i].head;
        sector = mbr[i].sector + ((mbr[i].cyl & 0x300) >> 2);
        cyl = mbr[i].cyl & 0xff;

        buf[offset + 0x0] = status;
        buf[offset + 0x1] = head;
        buf[offset + 0x2] = sector;
        buf[offset + 0x3] = cyl;

        parttype = 0;
        g_assert(mbr[i].end_head < 256);
        g_assert(mbr[i].end_sector < 64);
        g_assert(mbr[i].end_cyl < 1024);
        head = mbr[i].end_head;
        sector = mbr[i].end_sector + ((mbr[i].end_cyl & 0x300) >> 2);
        cyl = mbr[i].end_cyl & 0xff;

        buf[offset + 0x4] = parttype;
        buf[offset + 0x5] = head;
        buf[offset + 0x6] = sector;
        buf[offset + 0x7] = cyl;

        stl_le_p(&buf[offset + 0x8], mbr[i].start_sect);
        stl_le_p(&buf[offset + 0xc], mbr[i].nr_sects);

        offset += 0x10;
    }

    fd = g_file_open_tmp("qtest.XXXXXX", &raw_path, NULL);
    g_assert(fd >= 0);
    close(fd);

    fd = open(raw_path, O_WRONLY);
    g_assert(fd >= 0);
    ret = write(fd, buf, sizeof(buf));
    g_assert(ret == sizeof(buf));
    close(fd);

    fd = g_file_open_tmp("qtest.XXXXXX", &qcow2_path, NULL);
    g_assert(fd >= 0);
    close(fd);

    qemu_img_path = getenv("QTEST_QEMU_IMG");
    g_assert(qemu_img_path);
    qemu_img_abs_path = realpath(qemu_img_path, NULL);
    g_assert(qemu_img_abs_path);

    ret = snprintf(cmd, sizeof(cmd),
                   "%s convert -f raw -O qcow2 %s %s > /dev/null",
                   qemu_img_abs_path,
                   raw_path, qcow2_path);
    g_assert((0 < ret) && (ret <= sizeof(cmd)));
    ret = system(cmd);
    g_assert(ret == 0);

    ret = snprintf(cmd, sizeof(cmd),
                   "%s resize %s %" PRIu64 " > /dev/null",
                   qemu_img_abs_path,
                   qcow2_path, qcow2_size);
    g_assert((0 < ret) && (ret <= sizeof(cmd)));
    ret = system(cmd);
    g_assert(ret == 0);

    free(qemu_img_abs_path);

    unlink(raw_path);

    return qcow2_path;
}

#define BIOS_GEOMETRY_MAX_SIZE 10000

typedef struct {
    uint32_t c;
    uint32_t h;
    uint32_t s;
} CHS;

typedef struct {
    const char *dev_path;
    CHS chs;
} CHSResult;

static void read_bootdevices(QFWCFG *fw_cfg, CHSResult expected[])
{
    char *buf = g_malloc0(BIOS_GEOMETRY_MAX_SIZE);
    char *cur;
    GList *results = NULL, *cur_result;
    CHSResult *r;
    int i;
    int res;
    bool found;

    qfw_cfg_get_file(fw_cfg, "bios-geometry", buf, BIOS_GEOMETRY_MAX_SIZE);

    for (cur = buf; *cur; cur++) {
        if (*cur == '\n') {
            *cur = '\0';
        }
    }
    cur = buf;

    while (strlen(cur)) {

        r = g_malloc0(sizeof(*r));
        r->dev_path = g_malloc0(strlen(cur) + 1);
        res = sscanf(cur, "%s %" PRIu32 " %" PRIu32 " %" PRIu32,
                     (char *)r->dev_path,
                     &(r->chs.c), &(r->chs.h), &(r->chs.s));

        g_assert(res == 4);

        results = g_list_prepend(results, r);

        cur += strlen(cur) + 1;
    }

    i = 0;

    while (expected[i].dev_path) {
        found = false;
        cur_result = results;
        while (cur_result) {
            r = cur_result->data;
            if (!strcmp(r->dev_path, expected[i].dev_path) &&
                !memcmp(&(r->chs), &(expected[i].chs), sizeof(r->chs))) {
                found = true;
                break;
            }
            cur_result = g_list_next(cur_result);
        }
        g_assert(found);
        g_free((char *)((CHSResult *)cur_result->data)->dev_path);
        g_free(cur_result->data);
        results = g_list_delete_link(results, cur_result);
        i++;
    }

    g_assert(results == NULL);

    g_free(buf);
}

#define MAX_DRIVES 30

typedef struct {
    char **argv;
    int argc;
    char **drives;
    int n_drives;
    int n_scsi_disks;
    int n_scsi_controllers;
    int n_virtio_disks;
} TestArgs;

static TestArgs *create_args(void)
{
    TestArgs *args = g_malloc0(sizeof(*args));
    args->argv = g_new0(char *, ARGV_SIZE);
    args->argc = append_arg(args->argc, args->argv,
                            ARGV_SIZE, g_strdup("-nodefaults"));
    args->drives = g_new0(char *, MAX_DRIVES);
    return args;
}

static void add_drive_with_mbr(TestArgs *args,
                               MBRpartitions mbr, uint64_t sectors)
{
    char *img_file_name;
    char part[300];
    int ret;

    g_assert(args->n_drives < MAX_DRIVES);

    img_file_name = create_qcow2_with_mbr(mbr, sectors);

    args->drives[args->n_drives] = img_file_name;
    ret = snprintf(part, sizeof(part),
                   "-drive file=%s,if=none,format=qcow2,id=disk%d",
                   img_file_name, args->n_drives);
    g_assert((0 < ret) && (ret <= sizeof(part)));
    args->argc = append_arg(args->argc, args->argv, ARGV_SIZE, g_strdup(part));
    args->n_drives++;
}

static void add_ide_disk(TestArgs *args,
                         int drive_idx, int bus, int unit, int c, int h, int s)
{
    char part[300];
    int ret;

    ret = snprintf(part, sizeof(part),
                   "-device ide-hd,drive=disk%d,bus=ide.%d,unit=%d,"
                   "lcyls=%d,lheads=%d,lsecs=%d",
                   drive_idx, bus, unit, c, h, s);
    g_assert((0 < ret) && (ret <= sizeof(part)));
    args->argc = append_arg(args->argc, args->argv, ARGV_SIZE, g_strdup(part));
}

static void add_scsi_controller(TestArgs *args,
                                const char *type,
                                const char *bus,
                                int addr)
{
    char part[300];
    int ret;

    ret = snprintf(part, sizeof(part),
                   "-device %s,id=scsi%d,bus=%s,addr=%d",
                   type, args->n_scsi_controllers, bus, addr);
    g_assert((0 < ret) && (ret <= sizeof(part)));
    args->argc = append_arg(args->argc, args->argv, ARGV_SIZE, g_strdup(part));
    args->n_scsi_controllers++;
}

static void add_scsi_disk(TestArgs *args,
                          int drive_idx, int bus,
                          int channel, int scsi_id, int lun,
                          int c, int h, int s)
{
    char part[300];
    int ret;

    ret = snprintf(part, sizeof(part),
                   "-device scsi-hd,id=scsi-disk%d,drive=disk%d,"
                   "bus=scsi%d.0,"
                   "channel=%d,scsi-id=%d,lun=%d,"
                   "lcyls=%d,lheads=%d,lsecs=%d",
                   args->n_scsi_disks, drive_idx, bus, channel, scsi_id, lun,
                   c, h, s);
    g_assert((0 < ret) && (ret <= sizeof(part)));
    args->argc = append_arg(args->argc, args->argv, ARGV_SIZE, g_strdup(part));
    args->n_scsi_disks++;
}

static void add_virtio_disk(TestArgs *args,
                            int drive_idx, const char *bus, int addr,
                            int c, int h, int s)
{
    char part[300];
    int ret;

    ret = snprintf(part, sizeof(part),
                   "-device virtio-blk-pci,id=virtio-disk%d,"
                   "drive=disk%d,bus=%s,addr=%d,"
                   "lcyls=%d,lheads=%d,lsecs=%d",
                   args->n_virtio_disks, drive_idx, bus, addr, c, h, s);
    g_assert((0 < ret) && (ret <= sizeof(part)));
    args->argc = append_arg(args->argc, args->argv, ARGV_SIZE, g_strdup(part));
    args->n_virtio_disks++;
}

static void test_override(TestArgs *args, const char *arch,
                          CHSResult expected[])
{
    QTestState *qts;
    char *joined_args;
    QFWCFG *fw_cfg;
    int i;

    joined_args = g_strjoinv(" ", args->argv);

    qts = qtest_initf("-machine %s %s", arch, joined_args);
    fw_cfg = pc_fw_cfg_init(qts);

    read_bootdevices(fw_cfg, expected);

    g_free(joined_args);
    qtest_quit(qts);

    g_free(fw_cfg);

    for (i = 0; i < args->n_drives; i++) {
        unlink(args->drives[i]);
        g_free(args->drives[i]);
    }
    g_free(args->drives);
    g_strfreev(args->argv);
    g_free(args);
}

static void test_override_ide(void)
{
    TestArgs *args = create_args();
    CHSResult expected[] = {
        {"/pci@i0cf8/ide@1,1/drive@0/disk@0", {10000, 120, 30} },
        {"/pci@i0cf8/ide@1,1/drive@0/disk@1", {9000, 120, 30} },
        {"/pci@i0cf8/ide@1,1/drive@1/disk@0", {0, 1, 1} },
        {"/pci@i0cf8/ide@1,1/drive@1/disk@1", {1, 0, 0} },
        {NULL, {0, 0, 0} }
    };
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_ide_disk(args, 0, 0, 0, 10000, 120, 30);
    add_ide_disk(args, 1, 0, 1, 9000, 120, 30);
    add_ide_disk(args, 2, 1, 0, 0, 1, 1);
    add_ide_disk(args, 3, 1, 1, 1, 0, 0);
    test_override(args, "pc", expected);
}

static void test_override_sata(void)
{
    TestArgs *args = create_args();
    CHSResult expected[] = {
        {"/pci@i0cf8/pci8086,2922@1f,2/drive@0/disk@0", {10000, 120, 30} },
        {"/pci@i0cf8/pci8086,2922@1f,2/drive@1/disk@0", {9000, 120, 30} },
        {"/pci@i0cf8/pci8086,2922@1f,2/drive@2/disk@0", {0, 1, 1} },
        {"/pci@i0cf8/pci8086,2922@1f,2/drive@3/disk@0", {1, 0, 0} },
        {NULL, {0, 0, 0} }
    };
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_ide_disk(args, 0, 0, 0, 10000, 120, 30);
    add_ide_disk(args, 1, 1, 0, 9000, 120, 30);
    add_ide_disk(args, 2, 2, 0, 0, 1, 1);
    add_ide_disk(args, 3, 3, 0, 1, 0, 0);
    test_override(args, "q35", expected);
}

static void test_override_scsi(void)
{
    TestArgs *args = create_args();
    CHSResult expected[] = {
        {"/pci@i0cf8/scsi@3/channel@0/disk@0,0", {10000, 120, 30} },
        {"/pci@i0cf8/scsi@3/channel@0/disk@1,0", {9000, 120, 30} },
        {"/pci@i0cf8/scsi@3/channel@0/disk@2,0", {1, 0, 0} },
        {"/pci@i0cf8/scsi@3/channel@0/disk@3,0", {0, 1, 0} },
        {NULL, {0, 0, 0} }
    };
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_scsi_controller(args, "lsi53c895a", "pci.0", 3);
    add_scsi_disk(args, 0, 0, 0, 0, 0, 10000, 120, 30);
    add_scsi_disk(args, 1, 0, 0, 1, 0, 9000, 120, 30);
    add_scsi_disk(args, 2, 0, 0, 2, 0, 1, 0, 0);
    add_scsi_disk(args, 3, 0, 0, 3, 0, 0, 1, 0);
    test_override(args, "pc", expected);
}

static void setup_pci_bridge(TestArgs *args, const char *id)
{

    char *br;
    br = g_strdup_printf("-device pcie-pci-bridge,bus=pcie.0,id=%s", id);

    args->argc = append_arg(args->argc, args->argv, ARGV_SIZE, br);
}

static void test_override_scsi_q35(void)
{
    TestArgs *args = create_args();
    CHSResult expected[] = {
        {   "/pci@i0cf8/pci-bridge@1/scsi@3/channel@0/disk@0,0",
            {10000, 120, 30}
        },
        {"/pci@i0cf8/pci-bridge@1/scsi@3/channel@0/disk@1,0", {9000, 120, 30} },
        {"/pci@i0cf8/pci-bridge@1/scsi@3/channel@0/disk@2,0", {1, 0, 0} },
        {"/pci@i0cf8/pci-bridge@1/scsi@3/channel@0/disk@3,0", {0, 1, 0} },
        {NULL, {0, 0, 0} }
    };
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    setup_pci_bridge(args, "pcie-pci-br");
    add_scsi_controller(args, "lsi53c895a", "pcie-pci-br", 3);
    add_scsi_disk(args, 0, 0, 0, 0, 0, 10000, 120, 30);
    add_scsi_disk(args, 1, 0, 0, 1, 0, 9000, 120, 30);
    add_scsi_disk(args, 2, 0, 0, 2, 0, 1, 0, 0);
    add_scsi_disk(args, 3, 0, 0, 3, 0, 0, 1, 0);
    test_override(args, "q35", expected);
}

static void test_override_scsi_2_controllers(void)
{
    TestArgs *args = create_args();
    CHSResult expected[] = {
        {"/pci@i0cf8/scsi@3/channel@0/disk@0,0", {10000, 120, 30} },
        {"/pci@i0cf8/scsi@3/channel@0/disk@1,0", {9000, 120, 30} },
        {"/pci@i0cf8/scsi@4/channel@0/disk@0,1", {1, 0, 0} },
        {"/pci@i0cf8/scsi@4/channel@0/disk@1,2", {0, 1, 0} },
        {NULL, {0, 0, 0} }
    };
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_scsi_controller(args, "lsi53c895a", "pci.0", 3);
    add_scsi_controller(args, "virtio-scsi-pci", "pci.0", 4);
    add_scsi_disk(args, 0, 0, 0, 0, 0, 10000, 120, 30);
    add_scsi_disk(args, 1, 0, 0, 1, 0, 9000, 120, 30);
    add_scsi_disk(args, 2, 1, 0, 0, 1, 1, 0, 0);
    add_scsi_disk(args, 3, 1, 0, 1, 2, 0, 1, 0);
    test_override(args, "pc", expected);
}

static void test_override_virtio_blk(void)
{
    TestArgs *args = create_args();
    CHSResult expected[] = {
        {"/pci@i0cf8/scsi@3/disk@0,0", {10000, 120, 30} },
        {"/pci@i0cf8/scsi@4/disk@0,0", {9000, 120, 30} },
        {NULL, {0, 0, 0} }
    };
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_virtio_disk(args, 0, "pci.0", 3, 10000, 120, 30);
    add_virtio_disk(args, 1, "pci.0", 4, 9000, 120, 30);
    test_override(args, "pc", expected);
}

static void test_override_virtio_blk_q35(void)
{
    TestArgs *args = create_args();
    CHSResult expected[] = {
        {"/pci@i0cf8/pci-bridge@1/scsi@3/disk@0,0", {10000, 120, 30} },
        {"/pci@i0cf8/pci-bridge@1/scsi@4/disk@0,0", {9000, 120, 30} },
        {NULL, {0, 0, 0} }
    };
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    setup_pci_bridge(args, "pcie-pci-br");
    add_virtio_disk(args, 0, "pcie-pci-br", 3, 10000, 120, 30);
    add_virtio_disk(args, 1, "pcie-pci-br", 4, 9000, 120, 30);
    test_override(args, "q35", expected);
}

static void test_override_zero_chs(void)
{
    TestArgs *args = create_args();
    CHSResult expected[] = {
        {NULL, {0, 0, 0} }
    };
    add_drive_with_mbr(args, empty_mbr, 1);
    add_ide_disk(args, 0, 1, 1, 0, 0, 0);
    test_override(args, "pc", expected);
}

static void test_override_zero_chs_q35(void)
{
    TestArgs *args = create_args();
    CHSResult expected[] = {
        {NULL, {0, 0, 0} }
    };
    add_drive_with_mbr(args, empty_mbr, 1);
    add_ide_disk(args, 0, 0, 0, 0, 0, 0);
    test_override(args, "q35", expected);
}

static void test_override_hot_unplug(TestArgs *args, const char *devid,
                                     CHSResult expected[], CHSResult expected2[])
{
    QTestState *qts;
    char *joined_args;
    QFWCFG *fw_cfg;
    int i;

    joined_args = g_strjoinv(" ", args->argv);

    qts = qtest_initf("%s", joined_args);
    fw_cfg = pc_fw_cfg_init(qts);

    read_bootdevices(fw_cfg, expected);

    /* unplug device an restart */
    qtest_qmp_device_del_send(qts, devid);

    qtest_system_reset(qts);

    read_bootdevices(fw_cfg, expected2);

    g_free(joined_args);
    qtest_quit(qts);

    g_free(fw_cfg);

    for (i = 0; i < args->n_drives; i++) {
        unlink(args->drives[i]);
        g_free(args->drives[i]);
    }
    g_free(args->drives);
    g_strfreev(args->argv);
    g_free(args);
}

static void test_override_scsi_hot_unplug(void)
{
    TestArgs *args = create_args();
    CHSResult expected[] = {
        {"/pci@i0cf8/scsi@2/channel@0/disk@0,0", {10000, 120, 30} },
        {"/pci@i0cf8/scsi@2/channel@0/disk@1,0", {20, 20, 20} },
        {NULL, {0, 0, 0} }
    };
    CHSResult expected2[] = {
        {"/pci@i0cf8/scsi@2/channel@0/disk@1,0", {20, 20, 20} },
        {NULL, {0, 0, 0} }
    };
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_scsi_controller(args, "virtio-scsi-pci", "pci.0", 2);
    add_scsi_disk(args, 0, 0, 0, 0, 0, 10000, 120, 30);
    add_scsi_disk(args, 1, 0, 0, 1, 0, 20, 20, 20);

    args->argc = append_arg(args->argc, args->argv, ARGV_SIZE,
                            g_strdup("-machine pc"));

    test_override_hot_unplug(args, "scsi-disk0", expected, expected2);
}

static void test_override_scsi_hot_unplug_q35(void)
{
    TestArgs *args = create_args();
    CHSResult expected[] = {
        {
            "/pci@i0cf8/pci-bridge@1/pci-bridge@0/scsi@2/channel@0/disk@0,0",
            {10000, 120, 30}
        },
        {
            "/pci@i0cf8/pci-bridge@1/pci-bridge@0/scsi@2/channel@0/disk@1,0",
            {20, 20, 20}
        },
        {NULL, {0, 0, 0} }
    };
    CHSResult expected2[] = {
        {
            "/pci@i0cf8/pci-bridge@1/pci-bridge@0/scsi@2/channel@0/disk@1,0",
            {20, 20, 20}
        },
        {NULL, {0, 0, 0} }
    };

    args->argc = append_arg(args->argc, args->argv, ARGV_SIZE,
                            g_strdup("-device pcie-root-port,id=p0 "
                                     "-device pcie-pci-bridge,bus=p0,id=b1 "
                                     "-machine q35"));

    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_scsi_controller(args, "virtio-scsi-pci", "b1", 2);
    add_scsi_disk(args, 0, 0, 0, 0, 0, 10000, 120, 30);
    add_scsi_disk(args, 1, 0, 0, 1, 0, 20, 20, 20);

    test_override_hot_unplug(args, "scsi-disk0", expected, expected2);
}

static void test_override_virtio_hot_unplug(void)
{
    TestArgs *args = create_args();
    CHSResult expected[] = {
        {"/pci@i0cf8/scsi@2/disk@0,0", {10000, 120, 30} },
        {"/pci@i0cf8/scsi@3/disk@0,0", {20, 20, 20} },
        {NULL, {0, 0, 0} }
    };
    CHSResult expected2[] = {
        {"/pci@i0cf8/scsi@3/disk@0,0", {20, 20, 20} },
        {NULL, {0, 0, 0} }
    };
    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_virtio_disk(args, 0, "pci.0", 2, 10000, 120, 30);
    add_virtio_disk(args, 1, "pci.0", 3, 20, 20, 20);

    args->argc = append_arg(args->argc, args->argv, ARGV_SIZE,
                            g_strdup("-machine pc"));

    test_override_hot_unplug(args, "virtio-disk0", expected, expected2);
}

static void test_override_virtio_hot_unplug_q35(void)
{
    TestArgs *args = create_args();
    CHSResult expected[] = {
        {
            "/pci@i0cf8/pci-bridge@1/pci-bridge@0/scsi@2/disk@0,0",
            {10000, 120, 30}
        },
        {
            "/pci@i0cf8/pci-bridge@1/pci-bridge@0/scsi@3/disk@0,0",
            {20, 20, 20}
        },
        {NULL, {0, 0, 0} }
    };
    CHSResult expected2[] = {
        {
            "/pci@i0cf8/pci-bridge@1/pci-bridge@0/scsi@3/disk@0,0",
            {20, 20, 20}
        },
        {NULL, {0, 0, 0} }
    };

    args->argc = append_arg(args->argc, args->argv, ARGV_SIZE,
                            g_strdup("-device pcie-root-port,id=p0 "
                                     "-device pcie-pci-bridge,bus=p0,id=b1 "
                                     "-machine q35"));

    add_drive_with_mbr(args, empty_mbr, 1);
    add_drive_with_mbr(args, empty_mbr, 1);
    add_virtio_disk(args, 0, "b1", 2, 10000, 120, 30);
    add_virtio_disk(args, 1, "b1", 3, 20, 20, 20);

    test_override_hot_unplug(args, "virtio-disk0", expected, expected2);
}

int main(int argc, char **argv)
{
    Backend i;
    int ret;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < backend_last; i++) {
        if (img_secs[i] >= 0) {
            img_file_name[i] = create_test_img(img_secs[i]);
            if (!img_file_name[i]) {
                g_test_message("Could not create test images.");
                goto test_add_done;
            }
        } else {
            img_file_name[i] = NULL;
        }
    }

    if (qtest_has_machine("pc")) {
        qtest_add_func("hd-geo/ide/none", test_ide_none);
        qtest_add_func("hd-geo/ide/drive/mbr/blank", test_ide_drive_mbr_blank);
        qtest_add_func("hd-geo/ide/drive/mbr/lba", test_ide_drive_mbr_lba);
        qtest_add_func("hd-geo/ide/drive/mbr/chs", test_ide_drive_mbr_chs);
        qtest_add_func("hd-geo/ide/drive/cd_0", test_ide_drive_cd_0);
        qtest_add_func("hd-geo/ide/device/mbr/blank", test_ide_device_mbr_blank);
        qtest_add_func("hd-geo/ide/device/mbr/lba", test_ide_device_mbr_lba);
        qtest_add_func("hd-geo/ide/device/mbr/chs", test_ide_device_mbr_chs);
        qtest_add_func("hd-geo/ide/device/user/chs", test_ide_device_user_chs);
        qtest_add_func("hd-geo/ide/device/user/chst", test_ide_device_user_chst);
    }

    if (!have_qemu_img()) {
        g_test_message("QTEST_QEMU_IMG not set or qemu-img missing; "
                       "skipping hd-geo/override/* tests");
        goto test_add_done;
    }

    if (qtest_has_machine("pc")) {
        qtest_add_func("hd-geo/override/ide", test_override_ide);
        if (qtest_has_device("lsi53c895a")) {
            qtest_add_func("hd-geo/override/scsi", test_override_scsi);
            if (qtest_has_device("virtio-scsi-pci")) {
                qtest_add_func("hd-geo/override/scsi_2_controllers",
                               test_override_scsi_2_controllers);
            }
        }
        qtest_add_func("hd-geo/override/zero_chs", test_override_zero_chs);
        if (qtest_has_device("virtio-scsi-pci")) {
            qtest_add_func("hd-geo/override/scsi_hot_unplug",
                           test_override_scsi_hot_unplug);
        }
        if (qtest_has_device("virtio-blk-pci")) {
            qtest_add_func("hd-geo/override/virtio_hot_unplug",
                           test_override_virtio_hot_unplug);
            qtest_add_func("hd-geo/override/virtio_blk",
                           test_override_virtio_blk);
        }
    }

    if (qtest_has_machine("q35")) {
        qtest_add_func("hd-geo/override/sata", test_override_sata);
        qtest_add_func("hd-geo/override/zero_chs_q35",
                       test_override_zero_chs_q35);
        if (qtest_has_device("lsi53c895a")) {
            qtest_add_func("hd-geo/override/scsi_q35",
                           test_override_scsi_q35);
        }
        if (qtest_has_device("virtio-scsi-pci")) {
            qtest_add_func("hd-geo/override/scsi_hot_unplug_q35",
                           test_override_scsi_hot_unplug_q35);
        }
        if (qtest_has_device("virtio-blk-pci")) {
            qtest_add_func("hd-geo/override/virtio_hot_unplug_q35",
                           test_override_virtio_hot_unplug_q35);
            qtest_add_func("hd-geo/override/virtio_blk_q35",
                           test_override_virtio_blk_q35);
        }
    }

test_add_done:
    ret = g_test_run();

    for (i = 0; i < backend_last; i++) {
        if (img_file_name[i]) {
            unlink(img_file_name[i]);
            g_free(img_file_name[i]);
        }
    }

    return ret;
}
