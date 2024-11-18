/*
 * Validate -readconfig
 *
 * Copyright (c) 2022 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-machine.h"
#include "qapi/qapi-visit-qom.h"
#include "qapi/qapi-visit-ui.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "qapi/qobject-input-visitor.h"
#include "qobject/qstring.h"
#include "qemu/units.h"

static QTestState *qtest_init_with_config(const char *cfgdata)
{
    GError *error = NULL;
    g_autofree char *args = NULL;
    int cfgfd = -1;
    g_autofree char *cfgpath = NULL;
    QTestState *qts;
    ssize_t ret;

    cfgfd = g_file_open_tmp("readconfig-test-XXXXXX", &cfgpath, &error);
    g_assert_no_error(error);
    g_assert_cmpint(cfgfd, >=, 0);

    ret = qemu_write_full(cfgfd, cfgdata, strlen(cfgdata));
    close(cfgfd);
    if (ret < 0) {
        unlink(cfgpath);
    }
    g_assert_cmpint(ret, ==, strlen(cfgdata));

    args = g_strdup_printf("-nodefaults -machine none -readconfig %s", cfgpath);

    qts = qtest_init(args);

    unlink(cfgpath);

    return qts;
}

static void test_x86_memdev_resp(QObject *res, const char *mem_id, int size)
{
    Visitor *v;
    g_autoptr(MemdevList) memdevs = NULL;
    Memdev *memdev;

    g_assert(res);
    v = qobject_input_visitor_new(res);
    visit_type_MemdevList(v, NULL, &memdevs, &error_abort);

    g_assert(memdevs);
    g_assert(memdevs->value);
    g_assert(!memdevs->next);

    memdev = memdevs->value;
    g_assert_cmpstr(memdev->id, ==, mem_id);
    g_assert_cmpint(memdev->size, ==, size * MiB);

    visit_free(v);
}

static void test_x86_memdev(void)
{
    QDict *resp;
    QTestState *qts;
    const char *cfgdata =
        "[memory]\n"
        "size = \"200\"";

    qts = qtest_init_with_config(cfgdata);
    /* Test valid command */
    resp = qtest_qmp(qts, "{ 'execute': 'query-memdev' }");
    test_x86_memdev_resp(qdict_get(resp, "return"), "ram", 200);
    qobject_unref(resp);

    qtest_quit(qts);
}

/* FIXME: The test is currently broken on FreeBSD */
#if defined(CONFIG_SPICE) && !defined(__FreeBSD__)
static void test_spice_resp(QObject *res)
{
    Visitor *v;
    g_autoptr(SpiceInfo) spice = NULL;

    g_assert(res);
    v = qobject_input_visitor_new(res);
    visit_type_SpiceInfo(v, "spice", &spice, &error_abort);

    g_assert(spice);
    g_assert(spice->enabled);

    visit_free(v);
}

static void test_spice(void)
{
    QDict *resp;
    QTestState *qts;
    const char *cfgdata =
        "[spice]\n"
#ifndef WIN32
        "unix = \"on\"\n"
#endif
        "disable-ticketing = \"on\"\n";

    qts = qtest_init_with_config(cfgdata);
    /* Test valid command */
    resp = qtest_qmp(qts, "{ 'execute': 'query-spice' }");
    test_spice_resp(qdict_get(resp, "return"));
    qobject_unref(resp);

    qtest_quit(qts);
}
#endif

static void test_object_available(QObject *res, const char *name,
                                  const char *type)
{
    Visitor *v;
    g_autoptr(ObjectPropertyInfoList) objs = NULL;
    ObjectPropertyInfoList *tmp;
    ObjectPropertyInfo *obj;
    bool object_available = false;
    g_autofree char *childtype = g_strdup_printf("child<%s>", type);

    g_assert(res);
    v = qobject_input_visitor_new(res);
    visit_type_ObjectPropertyInfoList(v, NULL, &objs, &error_abort);

    g_assert(objs);
    tmp = objs;
    while (tmp) {
        g_assert(tmp->value);

        obj = tmp->value;
        if (g_str_equal(obj->name, name) && g_str_equal(obj->type, childtype)) {
            object_available = true;
            break;
        }

        tmp = tmp->next;
    }

    g_assert(object_available);

    visit_free(v);
}

static void test_object_rng(void)
{
    QDict *resp;
    QTestState *qts;
    const char *cfgdata =
        "[object]\n"
        "qom-type = \"rng-builtin\"\n"
        "id = \"rng0\"\n";

    qts = qtest_init_with_config(cfgdata);
    /* Test valid command */
    resp = qtest_qmp(qts,
                     "{ 'execute': 'qom-list',"
                     "  'arguments': {'path': '/objects' }}");
    test_object_available(qdict_get(resp, "return"), "rng0", "rng-builtin");
    qobject_unref(resp);

    qtest_quit(qts);
}

static void test_docs_config_ich9(void)
{
    QTestState *qts;
    QDict *resp;
    QObject *qobj;

    qts = qtest_initf("-nodefaults -readconfig docs/config/ich9-ehci-uhci.cfg");

    resp = qtest_qmp(qts, "{ 'execute': 'qom-list',"
                          "  'arguments': {'path': '/machine/peripheral' }}");
    qobj = qdict_get(resp, "return");
    test_object_available(qobj, "ehci", "ich9-usb-ehci1");
    test_object_available(qobj, "uhci-1", "ich9-usb-uhci1");
    test_object_available(qobj, "uhci-2", "ich9-usb-uhci2");
    test_object_available(qobj, "uhci-3", "ich9-usb-uhci3");
    qobject_unref(resp);

    qtest_quit(qts);
}

#if defined(CONFIG_POSIX) && defined(CONFIG_SLIRP)

static char *make_temp_img(const char *template, const char *format, int size)
{
    GError *error = NULL;
    char *temp_name;
    int fd;

    /* Create a temporary image names */
    fd = g_file_open_tmp(template, &temp_name, &error);
    if (fd == -1) {
        fprintf(stderr, "unable to create file: %s\n", error->message);
        g_error_free(error);
        return NULL;
    }
    close(fd);

    if (!mkimg(temp_name, format, size)) {
        fprintf(stderr, "qemu-img failed to create %s\n", temp_name);
        g_free(temp_name);
        return NULL;
    }

    return temp_name;
}

struct device {
    const char *name;
    const char *type;
};

static void test_docs_q35(const char *input_file, struct device *devices)
{
    QTestState *qts;
    QDict *resp;
    QObject *qobj;
    int ret, i;
    g_autofree char *cfg_file = NULL, *sedcmd = NULL;
    g_autofree char *hd_file = NULL, *cd_file = NULL;

    /* Check that all the devices are available in the QEMU binary */
    for (i = 0; devices[i].name; i++) {
        if (!qtest_has_device(devices[i].type)) {
            g_test_skip("one of the required devices is not available");
            return;
        }
    }

    hd_file = make_temp_img("qtest_disk_XXXXXX.qcow2", "qcow2", 1);
    cd_file = make_temp_img("qtest_cdrom_XXXXXX.iso", "raw", 1);
    if (!hd_file || !cd_file) {
        g_test_skip("could not create disk images");
        goto cleanup;
    }

    /* Create a temporary config file where we replace the disk image names */
    ret = g_file_open_tmp("q35-emulated-XXXXXX.cfg", &cfg_file, NULL);
    if (ret == -1) {
        g_test_skip("could not create temporary config file");
        goto cleanup;
    }
    close(ret);

    sedcmd = g_strdup_printf("sed -e 's,guest.qcow2,%s,' -e 's,install.iso,%s,'"
                             " %s %s > '%s'",
                             hd_file, cd_file,
                             !qtest_has_accel("kvm") ? "-e '/accel/d'" : "",
                             input_file, cfg_file);
    ret = system(sedcmd);
    if (ret) {
        g_test_skip("could not modify temporary config file");
        goto cleanup;
    }

    qts = qtest_initf("-machine none -nodefaults -readconfig %s", cfg_file);

    /* Check memory size */
    resp = qtest_qmp(qts, "{ 'execute': 'query-memdev' }");
    test_x86_memdev_resp(qdict_get(resp, "return"), "pc.ram", 1024);
    qobject_unref(resp);

    resp = qtest_qmp(qts, "{ 'execute': 'qom-list',"
                          "  'arguments': {'path': '/machine/peripheral' }}");
    qobj = qdict_get(resp, "return");

    /* Check that all the devices have been created */
    for (i = 0; devices[i].name; i++) {
        test_object_available(qobj, devices[i].name, devices[i].type);
    }

    qobject_unref(resp);

    qtest_quit(qts);

cleanup:
    if (hd_file) {
        unlink(hd_file);
    }
    if (cd_file) {
        unlink(cd_file);
    }
    if (cfg_file) {
        unlink(cfg_file);
    }
}

static void test_docs_q35_emulated(void)
{
    struct device devices[] = {
        { "ich9-pcie-port-1", "ioh3420" },
        { "ich9-pcie-port-2", "ioh3420" },
        { "ich9-pcie-port-3", "ioh3420" },
        { "ich9-pcie-port-4", "ioh3420" },
        { "ich9-pci-bridge", "i82801b11-bridge" },
        { "ich9-ehci-1", "ich9-usb-ehci1" },
        { "ich9-ehci-2", "ich9-usb-ehci2" },
        { "ich9-uhci-1", "ich9-usb-uhci1" },
        { "ich9-uhci-2", "ich9-usb-uhci2" },
        { "ich9-uhci-3", "ich9-usb-uhci3" },
        { "ich9-uhci-4", "ich9-usb-uhci4" },
        { "ich9-uhci-5", "ich9-usb-uhci5" },
        { "ich9-uhci-6", "ich9-usb-uhci6" },
        { "sata-disk", "ide-hd" },
        { "sata-optical-disk", "ide-cd" },
        { "net", "e1000" },
        { "video", "VGA" },
        { "ich9-hda-audio", "ich9-intel-hda" },
        { "ich9-hda-duplex", "hda-duplex" },
        { NULL, NULL }
    };

    test_docs_q35("docs/config/q35-emulated.cfg", devices);
}

static void test_docs_q35_virtio_graphical(void)
{
    struct device devices[] = {
        { "pcie.1", "pcie-root-port" },
        { "pcie.2", "pcie-root-port" },
        { "pcie.3", "pcie-root-port" },
        { "pcie.4", "pcie-root-port" },
        { "pcie.5", "pcie-root-port" },
        { "pcie.6", "pcie-root-port" },
        { "pcie.7", "pcie-root-port" },
        { "pcie.8", "pcie-root-port" },
        { "scsi", "virtio-scsi-pci" },
        { "scsi-disk", "scsi-hd" },
        { "scsi-optical-disk", "scsi-cd" },
        { "net", "virtio-net-pci" },
        { "usb", "nec-usb-xhci" },
        { "tablet", "usb-tablet" },
        { "video", "qxl-vga" },
        { "sound", "ich9-intel-hda" },
        { "duplex", "hda-duplex" },
        { NULL, NULL }
    };

    test_docs_q35("docs/config/q35-virtio-graphical.cfg", devices);
}

static void test_docs_q35_virtio_serial(void)
{
    struct device devices[] = {
        { "pcie.1", "pcie-root-port" },
        { "pcie.2", "pcie-root-port" },
        { "pcie.3", "pcie-root-port" },
        { "pcie.4", "pcie-root-port" },
        { "pcie.5", "pcie-root-port" },
        { "pcie.6", "pcie-root-port" },
        { "pcie.7", "pcie-root-port" },
        { "pcie.8", "pcie-root-port" },
        { "scsi", "virtio-scsi-pci" },
        { "scsi-disk", "scsi-hd" },
        { "scsi-optical-disk", "scsi-cd" },
        { "net", "virtio-net-pci" },
        { NULL, NULL }
    };

    test_docs_q35("docs/config/q35-virtio-serial.cfg", devices);
}

#endif /* CONFIG_LINUX */

int main(int argc, char *argv[])
{
    const char *arch;
    g_test_init(&argc, &argv, NULL);

    arch = qtest_get_arch();

    if (g_str_equal(arch, "i386") ||
        g_str_equal(arch, "x86_64")) {
        qtest_add_func("readconfig/x86/memdev", test_x86_memdev);
        if (qtest_has_device("ich9-usb-ehci1") &&
            qtest_has_device("ich9-usb-uhci1")) {
            qtest_add_func("readconfig/x86/ich9-ehci-uhci", test_docs_config_ich9);
        }
#if defined(CONFIG_POSIX) && defined(CONFIG_SLIRP)
        qtest_add_func("readconfig/x86/q35-emulated", test_docs_q35_emulated);
        qtest_add_func("readconfig/x86/q35-virtio-graphical",
                       test_docs_q35_virtio_graphical);
        if (g_test_slow()) {
            /*
             * q35-virtio-serial.cfg is a subset of q35-virtio-graphical.cfg,
             * so we can skip the test in quick mode
             */
            qtest_add_func("readconfig/x86/q35-virtio-serial",
                           test_docs_q35_virtio_serial);
        }
#endif
    }
#if defined(CONFIG_SPICE) && !defined(__FreeBSD__)
    qtest_add_func("readconfig/spice", test_spice);
#endif

    qtest_add_func("readconfig/object-rng", test_object_rng);

    return g_test_run();
}
