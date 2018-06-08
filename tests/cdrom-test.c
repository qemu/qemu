/*
 * Various tests for emulated CD-ROM drives.
 *
 * Copyright (c) 2018 Red Hat Inc.
 *
 * Author:
 *    Thomas Huth <thuth@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2
 * or later. See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "boot-sector.h"

static char isoimage[] = "cdrom-boot-iso-XXXXXX";

static int exec_genisoimg(const char **args)
{
    gchar *out_err = NULL;
    gint exit_status = -1;
    bool success;

    success = g_spawn_sync(NULL, (gchar **)args, NULL,
                           G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
                           NULL, NULL, NULL, &out_err, &exit_status, NULL);
    if (!success) {
        return -ENOENT;
    }
    if (out_err) {
        fputs(out_err, stderr);
        g_free(out_err);
    }

    return exit_status;
}

static int prepare_image(const char *arch, char *isoimage)
{
    char srcdir[] = "cdrom-test-dir-XXXXXX";
    char *codefile = NULL;
    int ifh, ret = -1;
    const char *args[] = {
        "genisoimage", "-quiet", "-l", "-no-emul-boot",
        "-b", NULL, "-o", isoimage, srcdir, NULL
    };

    ifh = mkstemp(isoimage);
    if (ifh < 0) {
        perror("Error creating temporary iso image file");
        return -1;
    }
    if (!mkdtemp(srcdir)) {
        perror("Error creating temporary directory");
        goto cleanup;
    }

    if (g_str_equal(arch, "i386") || g_str_equal(arch, "x86_64") ||
        g_str_equal(arch, "s390x")) {
        codefile = g_strdup_printf("%s/bootcode-XXXXXX", srcdir);
        ret = boot_sector_init(codefile);
        if (ret) {
            goto cleanup;
        }
    } else {
        /* Just create a dummy file */
        char txt[] = "empty disc";
        codefile = g_strdup_printf("%s/readme.txt", srcdir);
        if (!g_file_set_contents(codefile, txt, sizeof(txt) - 1, NULL)) {
            fprintf(stderr, "Failed to create '%s'\n", codefile);
            goto cleanup;
        }
    }

    args[5] = strchr(codefile, '/') + 1;
    ret = exec_genisoimg(args);
    if (ret) {
        fprintf(stderr, "genisoimage failed: %i\n", ret);
    }

    unlink(codefile);

cleanup:
    g_free(codefile);
    rmdir(srcdir);
    close(ifh);

    return ret;
}

static void test_cdboot(gconstpointer data)
{
    QTestState *qts;

    qts = qtest_startf("-accel kvm:tcg -no-shutdown %s%s", (const char *)data,
                       isoimage);
    boot_sector_test(qts);
    qtest_quit(qts);
}

static void add_x86_tests(void)
{
    qtest_add_data_func("cdrom/boot/default", "-cdrom ", test_cdboot);
    qtest_add_data_func("cdrom/boot/virtio-scsi",
                        "-device virtio-scsi -device scsi-cd,drive=cdr "
                        "-blockdev file,node-name=cdr,filename=", test_cdboot);
    qtest_add_data_func("cdrom/boot/isapc", "-M isapc "
                        "-drive if=ide,media=cdrom,file=", test_cdboot);
    qtest_add_data_func("cdrom/boot/am53c974",
                        "-device am53c974 -device scsi-cd,drive=cd1 "
                        "-drive if=none,id=cd1,format=raw,file=", test_cdboot);
    qtest_add_data_func("cdrom/boot/dc390",
                        "-device dc390 -device scsi-cd,drive=cd1 "
                        "-blockdev file,node-name=cd1,filename=", test_cdboot);
    qtest_add_data_func("cdrom/boot/lsi53c895a",
                        "-device lsi53c895a -device scsi-cd,drive=cd1 "
                        "-blockdev file,node-name=cd1,filename=", test_cdboot);
    qtest_add_data_func("cdrom/boot/megasas", "-M q35 "
                        "-device megasas -device scsi-cd,drive=cd1 "
                        "-blockdev file,node-name=cd1,filename=", test_cdboot);
    qtest_add_data_func("cdrom/boot/megasas-gen2", "-M q35 "
                        "-device megasas-gen2 -device scsi-cd,drive=cd1 "
                        "-blockdev file,node-name=cd1,filename=", test_cdboot);
}

static void add_s390x_tests(void)
{
    qtest_add_data_func("cdrom/boot/default", "-cdrom ", test_cdboot);
    qtest_add_data_func("cdrom/boot/virtio-scsi",
                        "-device virtio-scsi -device scsi-cd,drive=cdr "
                        "-blockdev file,node-name=cdr,filename=", test_cdboot);
}

int main(int argc, char **argv)
{
    int ret;
    const char *arch = qtest_get_arch();
    const char *genisocheck[] = { "genisoimage", "-version", NULL };

    g_test_init(&argc, &argv, NULL);

    if (exec_genisoimg(genisocheck)) {
        /* genisoimage not available - so can't run tests */
        return 0;
    }

    ret = prepare_image(arch, isoimage);
    if (ret) {
        return ret;
    }

    if (g_str_equal(arch, "i386") || g_str_equal(arch, "x86_64")) {
        add_x86_tests();
    } else if (g_str_equal(arch, "s390x")) {
        add_s390x_tests();
    }

    ret = g_test_run();

    unlink(isoimage);

    return ret;
}
