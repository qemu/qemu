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
#include "qapi/qmp/qdict.h"

static char isoimage[] = "cdrom-boot-iso-XXXXXX";

static int exec_xorrisofs(const char **args)
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

static int prepare_image(const char *arch, char *isoimagepath)
{
    char srcdir[] = "cdrom-test-dir-XXXXXX";
    char *codefile = NULL;
    int ifh, ret = -1;
    const char *args[] = {
        "xorrisofs", "-quiet", "-l", "-no-emul-boot",
        "-b", NULL, "-o", isoimagepath, srcdir, NULL
    };

    ifh = mkstemp(isoimagepath);
    if (ifh < 0) {
        perror("Error creating temporary iso image file");
        return -1;
    }
    if (!g_mkdtemp(srcdir)) {
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
    ret = exec_xorrisofs(args);
    if (ret) {
        fprintf(stderr, "xorrisofs failed: %i\n", ret);
    }

    unlink(codefile);

cleanup:
    g_free(codefile);
    rmdir(srcdir);
    close(ifh);

    return ret;
}

/**
 * Check that at least the -cdrom parameter is basically working, i.e. we can
 * see the filename of the ISO image in the output of "info block" afterwards
 */
static void test_cdrom_param(gconstpointer data)
{
    QTestState *qts;
    char *resp;

    qts = qtest_initf("-M %s -cdrom %s", (const char *)data, isoimage);
    resp = qtest_hmp(qts, "info block");
    g_assert(strstr(resp, isoimage) != 0);
    g_free(resp);
    qtest_quit(qts);
}

static void add_cdrom_param_tests(const char **machines)
{
    while (*machines) {
        if (qtest_has_machine(*machines)) {
            char *testname = g_strdup_printf("cdrom/param/%s", *machines);
            qtest_add_data_func(testname, *machines, test_cdrom_param);
            g_free(testname);
        }
        machines++;
    }
}

static void test_cdboot(gconstpointer data)
{
    QTestState *qts;

    qts = qtest_initf("-accel kvm -accel tcg -no-shutdown %s%s", (const char *)data,
                      isoimage);
    boot_sector_test(qts);
    qtest_quit(qts);
}

static void add_x86_tests(void)
{
    if (!qtest_has_accel("tcg") && !qtest_has_accel("kvm")) {
        g_test_skip("No KVM or TCG accelerator available, skipping boot tests");
        return;
    }

    if (qtest_has_machine("pc")) {
        qtest_add_data_func("cdrom/boot/default", "-cdrom ", test_cdboot);
        if (qtest_has_device("virtio-scsi-ccw")) {
            qtest_add_data_func("cdrom/boot/virtio-scsi",
                                "-device virtio-scsi -device scsi-cd,drive=cdr "
                                "-blockdev file,node-name=cdr,filename=",
                                test_cdboot);
        }

        if (qtest_has_device("am53c974")) {
            qtest_add_data_func("cdrom/boot/am53c974",
                                "-device am53c974 -device scsi-cd,drive=cd1 "
                                "-drive if=none,id=cd1,format=raw,file=",
                                test_cdboot);
        }
        if (qtest_has_device("dc390")) {
            qtest_add_data_func("cdrom/boot/dc390",
                                "-device dc390 -device scsi-cd,drive=cd1 "
                                "-blockdev file,node-name=cd1,filename=",
                                test_cdboot);
        }
        if (qtest_has_device("lsi53c895a")) {
            qtest_add_data_func("cdrom/boot/lsi53c895a",
                                "-device lsi53c895a -device scsi-cd,drive=cd1 "
                                "-blockdev file,node-name=cd1,filename=",
                                test_cdboot);
        }
    }

    /*
     * Unstable CI test under load
     * See https://lists.gnu.org/archive/html/qemu-devel/2019-02/msg05509.html
     */
    if (g_test_slow() && qtest_has_machine("isapc")) {
        qtest_add_data_func("cdrom/boot/isapc", "-M isapc "
                            "-drive if=ide,media=cdrom,file=", test_cdboot);
    }

    if (qtest_has_machine("q35")) {
        if (qtest_has_device("megasas")) {
            qtest_add_data_func("cdrom/boot/megasas", "-M q35 "
                                "-device megasas -device scsi-cd,drive=cd1 "
                                "-blockdev file,node-name=cd1,filename=",
                                test_cdboot);
        }
        if (qtest_has_device("megasas-gen2")) {
            qtest_add_data_func("cdrom/boot/megasas-gen2", "-M q35 "
                                "-device megasas-gen2 -device scsi-cd,drive=cd1 "
                                "-blockdev file,node-name=cd1,filename=",
                                test_cdboot);
        }
    }
}

static void add_s390x_tests(void)
{
    if (!qtest_has_accel("tcg") && !qtest_has_accel("kvm")) {
        g_test_skip("No KVM or TCG accelerator available, skipping boot tests");
    }
    if (!qtest_has_device("virtio-blk-ccw")) {
        return;
    }

    qtest_add_data_func("cdrom/boot/default", "-cdrom ", test_cdboot);

    if (!qtest_has_device("virtio-scsi-ccw")) {
        return;
    }

    qtest_add_data_func("cdrom/boot/virtio-scsi",
                        "-device virtio-scsi -device scsi-cd,drive=cdr "
                        "-blockdev file,node-name=cdr,filename=", test_cdboot);
    qtest_add_data_func("cdrom/boot/with-bootindex",
                        "-device virtio-serial -device virtio-scsi "
                        "-device virtio-blk,drive=d1 "
                        "-drive driver=null-co,read-zeroes=on,if=none,id=d1 "
                        "-device virtio-blk,drive=d2,bootindex=1 "
                        "-drive if=none,id=d2,media=cdrom,file=", test_cdboot);
    qtest_add_data_func("cdrom/boot/as-fallback-device",
                        "-device virtio-serial -device virtio-scsi "
                        "-device virtio-blk,drive=d1,bootindex=1 "
                        "-drive driver=null-co,read-zeroes=on,if=none,id=d1 "
                        "-device virtio-blk,drive=d2,bootindex=2 "
                        "-drive if=none,id=d2,media=cdrom,file=", test_cdboot);
    qtest_add_data_func("cdrom/boot/as-last-option",
                        "-device virtio-serial -device virtio-scsi "
                        "-device virtio-blk,drive=d1,bootindex=1 "
                        "-drive driver=null-co,read-zeroes=on,if=none,id=d1 "
                        "-device virtio-blk,drive=d2,bootindex=2 "
                        "-drive driver=null-co,read-zeroes=on,if=none,id=d2 "
                        "-device virtio-blk,drive=d3,bootindex=3 "
                        "-drive driver=null-co,read-zeroes=on,if=none,id=d3 "
                        "-device scsi-hd,drive=d4,bootindex=4 "
                        "-drive driver=null-co,read-zeroes=on,if=none,id=d4 "
                        "-device scsi-hd,drive=d5,bootindex=5 "
                        "-drive driver=null-co,read-zeroes=on,if=none,id=d5 "
                        "-device virtio-blk,drive=d6,bootindex=6 "
                        "-drive driver=null-co,read-zeroes=on,if=none,id=d6 "
                        "-device scsi-hd,drive=d7,bootindex=7 "
                        "-drive driver=null-co,read-zeroes=on,if=none,id=d7 "
                        "-device scsi-cd,drive=d8,bootindex=8 "
                        "-drive if=none,id=d8,media=cdrom,file=", test_cdboot);
    if (qtest_has_device("x-terminal3270")) {
        qtest_add_data_func("cdrom/boot/without-bootindex",
                            "-device virtio-scsi -device virtio-serial "
                            "-device x-terminal3270 -device virtio-blk,drive=d1 "
                            "-drive driver=null-co,read-zeroes=on,if=none,id=d1 "
                            "-device virtio-blk,drive=d2 "
                            "-drive if=none,id=d2,media=cdrom,file=",
                            test_cdboot);
    }
}

int main(int argc, char **argv)
{
    int ret;
    const char *arch = qtest_get_arch();
    const char *xorrisocheck[] = { "xorrisofs", "-version", NULL };

    g_test_init(&argc, &argv, NULL);

    if (exec_xorrisofs(xorrisocheck)) {
        /* xorrisofs not available - so can't run tests */
        return g_test_run();
    }

    ret = prepare_image(arch, isoimage);
    if (ret) {
        return ret;
    }

    if (g_str_equal(arch, "i386") || g_str_equal(arch, "x86_64")) {
        add_x86_tests();
    } else if (g_str_equal(arch, "s390x")) {
        add_s390x_tests();
    } else if (g_str_equal(arch, "ppc64")) {
        const char *ppcmachines[] = {
            "pseries", "mac99", "g3beige", "40p", NULL
        };
        add_cdrom_param_tests(ppcmachines);
    } else if (g_str_equal(arch, "sparc")) {
        const char *sparcmachines[] = {
            "LX", "SPARCClassic", "SPARCbook", "SS-10", "SS-20", "SS-4",
            "SS-5", "SS-600MP", "Voyager", "leon3_generic", NULL
        };
        add_cdrom_param_tests(sparcmachines);
    } else if (g_str_equal(arch, "sparc64")) {
        const char *sparc64machines[] = {
            "niagara", "sun4u", "sun4v", NULL
        };
        add_cdrom_param_tests(sparc64machines);
    } else if (!strncmp(arch, "mips64", 6)) {
        const char *mips64machines[] = {
            "magnum", "malta", "pica61", NULL
        };
        add_cdrom_param_tests(mips64machines);
    } else if (g_str_equal(arch, "arm") || g_str_equal(arch, "aarch64")) {
        const char *armmachines[] = {
            "realview-eb", "realview-eb-mpcore", "realview-pb-a8",
            "realview-pbx-a9", "versatileab", "versatilepb", "vexpress-a15",
            "vexpress-a9", NULL
        };
        add_cdrom_param_tests(armmachines);
        if (qtest_has_device("virtio-blk-pci")) {
            const char *virtmachine[] = { "virt", NULL };
            add_cdrom_param_tests(virtmachine);
        }
    } else if (g_str_equal(arch, "loongarch64")) {
        if (qtest_has_device("virtio-blk-pci")) {
            const char *virtmachine[] = { "virt", NULL };
            add_cdrom_param_tests(virtmachine);
        }
    } else {
        const char *nonemachine[] = { "none", NULL };
        add_cdrom_param_tests(nonemachine);
    }

    ret = g_test_run();

    unlink(isoimage);

    return ret;
}
