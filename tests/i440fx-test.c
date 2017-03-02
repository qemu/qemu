/*
 * qtest I440FX test case
 *
 * Copyright IBM, Corp. 2012-2013
 * Copyright Red Hat, Inc. 2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Laszlo Ersek      <lersek@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "hw/pci/pci_regs.h"

#define BROKEN 1

typedef struct TestData
{
    int num_cpus;
} TestData;

typedef struct FirmwareTestFixture {
    /* decides whether we're testing -bios or -pflash */
    bool is_bios;
} FirmwareTestFixture;

static QPCIBus *test_start_get_bus(const TestData *s)
{
    char *cmdline;

    cmdline = g_strdup_printf("-smp %d", s->num_cpus);
    qtest_start(cmdline);
    g_free(cmdline);
    return qpci_init_pc(NULL);
}

static void test_i440fx_defaults(gconstpointer opaque)
{
    const TestData *s = opaque;
    QPCIBus *bus;
    QPCIDevice *dev;
    uint32_t value;

    bus = test_start_get_bus(s);
    dev = qpci_device_find(bus, QPCI_DEVFN(0, 0));
    g_assert(dev != NULL);

    /* 3.2.2 */
    g_assert_cmpint(qpci_config_readw(dev, PCI_VENDOR_ID), ==, 0x8086);
    /* 3.2.3 */
    g_assert_cmpint(qpci_config_readw(dev, PCI_DEVICE_ID), ==, 0x1237);
#ifndef BROKEN
    /* 3.2.4 */
    g_assert_cmpint(qpci_config_readw(dev, PCI_COMMAND), ==, 0x0006);
    /* 3.2.5 */
    g_assert_cmpint(qpci_config_readw(dev, PCI_STATUS), ==, 0x0280);
#endif
    /* 3.2.7 */
    g_assert_cmpint(qpci_config_readb(dev, PCI_CLASS_PROG), ==, 0x00);
    g_assert_cmpint(qpci_config_readw(dev, PCI_CLASS_DEVICE), ==, 0x0600);
    /* 3.2.8 */
    g_assert_cmpint(qpci_config_readb(dev, PCI_LATENCY_TIMER), ==, 0x00);
    /* 3.2.9 */
    g_assert_cmpint(qpci_config_readb(dev, PCI_HEADER_TYPE), ==, 0x00);
    /* 3.2.10 */
    g_assert_cmpint(qpci_config_readb(dev, PCI_BIST), ==, 0x00);

    /* 3.2.11 */
    value = qpci_config_readw(dev, 0x50); /* PMCCFG */
    if (s->num_cpus == 1) { /* WPE */
        g_assert(!(value & (1 << 15)));
    } else {
        g_assert((value & (1 << 15)));
    }

    g_assert(!(value & (1 << 6))); /* EPTE */

    /* 3.2.12 */
    g_assert_cmpint(qpci_config_readb(dev, 0x52), ==, 0x00); /* DETURBO */
    /* 3.2.13 */
#ifndef BROKEN
    g_assert_cmpint(qpci_config_readb(dev, 0x53), ==, 0x80); /* DBC */
#endif
    /* 3.2.14 */
    g_assert_cmpint(qpci_config_readb(dev, 0x54), ==, 0x00); /* AXC */
    /* 3.2.15 */
    g_assert_cmpint(qpci_config_readw(dev, 0x55), ==, 0x0000); /* DRT */
#ifndef BROKEN
    /* 3.2.16 */
    g_assert_cmpint(qpci_config_readb(dev, 0x57), ==, 0x01); /* DRAMC */
    /* 3.2.17 */
    g_assert_cmpint(qpci_config_readb(dev, 0x58), ==, 0x10); /* DRAMT */
#endif
    /* 3.2.18 */
    g_assert_cmpint(qpci_config_readb(dev, 0x59), ==, 0x00); /* PAM0 */
    g_assert_cmpint(qpci_config_readb(dev, 0x5A), ==, 0x00); /* PAM1 */
    g_assert_cmpint(qpci_config_readb(dev, 0x5B), ==, 0x00); /* PAM2 */
    g_assert_cmpint(qpci_config_readb(dev, 0x5C), ==, 0x00); /* PAM3 */
    g_assert_cmpint(qpci_config_readb(dev, 0x5D), ==, 0x00); /* PAM4 */
    g_assert_cmpint(qpci_config_readb(dev, 0x5E), ==, 0x00); /* PAM5 */
    g_assert_cmpint(qpci_config_readb(dev, 0x5F), ==, 0x00); /* PAM6 */
#ifndef BROKEN
    /* 3.2.19 */
    g_assert_cmpint(qpci_config_readb(dev, 0x60), ==, 0x01); /* DRB0 */
    g_assert_cmpint(qpci_config_readb(dev, 0x61), ==, 0x01); /* DRB1 */
    g_assert_cmpint(qpci_config_readb(dev, 0x62), ==, 0x01); /* DRB2 */
    g_assert_cmpint(qpci_config_readb(dev, 0x63), ==, 0x01); /* DRB3 */
    g_assert_cmpint(qpci_config_readb(dev, 0x64), ==, 0x01); /* DRB4 */
    g_assert_cmpint(qpci_config_readb(dev, 0x65), ==, 0x01); /* DRB5 */
    g_assert_cmpint(qpci_config_readb(dev, 0x66), ==, 0x01); /* DRB6 */
    g_assert_cmpint(qpci_config_readb(dev, 0x67), ==, 0x01); /* DRB7 */
#endif
    /* 3.2.20 */
    g_assert_cmpint(qpci_config_readb(dev, 0x68), ==, 0x00); /* FDHC */
    /* 3.2.21 */
    g_assert_cmpint(qpci_config_readb(dev, 0x70), ==, 0x00); /* MTT */
#ifndef BROKEN
    /* 3.2.22 */
    g_assert_cmpint(qpci_config_readb(dev, 0x71), ==, 0x10); /* CLT */
#endif
    /* 3.2.23 */
    g_assert_cmpint(qpci_config_readb(dev, 0x72), ==, 0x02); /* SMRAM */
    /* 3.2.24 */
    g_assert_cmpint(qpci_config_readb(dev, 0x90), ==, 0x00); /* ERRCMD */
    /* 3.2.25 */
    g_assert_cmpint(qpci_config_readb(dev, 0x91), ==, 0x00); /* ERRSTS */
    /* 3.2.26 */
    g_assert_cmpint(qpci_config_readb(dev, 0x93), ==, 0x00); /* TRC */

    g_free(dev);
    qpci_free_pc(bus);
    qtest_end();
}

#define PAM_RE 1
#define PAM_WE 2

static void pam_set(QPCIDevice *dev, int index, int flags)
{
    int regno = 0x59 + (index / 2);
    uint8_t reg;

    reg = qpci_config_readb(dev, regno);
    if (index & 1) {
        reg = (reg & 0x0F) | (flags << 4);
    } else {
        reg = (reg & 0xF0) | flags;
    }
    qpci_config_writeb(dev, regno, reg);
}

static gboolean verify_area(uint32_t start, uint32_t end, uint8_t value)
{
    uint32_t size = end - start + 1;
    gboolean ret = TRUE;
    uint8_t *data;
    int i;

    data = g_malloc0(size);
    memread(start, data, size);

    g_test_message("verify_area: data[0] = 0x%x", data[0]);

    for (i = 0; i < size; i++) {
        if (data[i] != value) {
            ret = FALSE;
            break;
        }
    }

    g_free(data);

    return ret;
}

static void write_area(uint32_t start, uint32_t end, uint8_t value)
{
    uint32_t size = end - start + 1;
    uint8_t *data;

    data = g_malloc(size);
    memset(data, value, size);
    memwrite(start, data, size);

    g_free(data);
}

static void test_i440fx_pam(gconstpointer opaque)
{
    const TestData *s = opaque;
    QPCIBus *bus;
    QPCIDevice *dev;
    int i;
    static struct {
        uint32_t start;
        uint32_t end;
    } pam_area[] = {
        { 0, 0 },             /* Reserved */
        { 0xF0000, 0xFFFFF }, /* BIOS Area */
        { 0xC0000, 0xC3FFF }, /* Option ROM */
        { 0xC4000, 0xC7FFF }, /* Option ROM */
        { 0xC8000, 0xCBFFF }, /* Option ROM */
        { 0xCC000, 0xCFFFF }, /* Option ROM */
        { 0xD0000, 0xD3FFF }, /* Option ROM */
        { 0xD4000, 0xD7FFF }, /* Option ROM */
        { 0xD8000, 0xDBFFF }, /* Option ROM */
        { 0xDC000, 0xDFFFF }, /* Option ROM */
        { 0xE0000, 0xE3FFF }, /* BIOS Extension */
        { 0xE4000, 0xE7FFF }, /* BIOS Extension */
        { 0xE8000, 0xEBFFF }, /* BIOS Extension */
        { 0xEC000, 0xEFFFF }, /* BIOS Extension */
    };

    bus = test_start_get_bus(s);
    dev = qpci_device_find(bus, QPCI_DEVFN(0, 0));
    g_assert(dev != NULL);

    for (i = 0; i < ARRAY_SIZE(pam_area); i++) {
        if (pam_area[i].start == pam_area[i].end) {
            continue;
        }

        g_test_message("Checking area 0x%05x..0x%05x",
                       pam_area[i].start, pam_area[i].end);
        /* Switch to RE for the area */
        pam_set(dev, i, PAM_RE);
        /* Verify the RAM is all zeros */
        g_assert(verify_area(pam_area[i].start, pam_area[i].end, 0));

        /* Switch to WE for the area */
        pam_set(dev, i, PAM_RE | PAM_WE);
        /* Write out a non-zero mask to the full area */
        write_area(pam_area[i].start, pam_area[i].end, 0x42);

#ifndef BROKEN
        /* QEMU only supports a limited form of PAM */

        /* Switch to !RE for the area */
        pam_set(dev, i, PAM_WE);
        /* Verify the area is not our mask */
        g_assert(!verify_area(pam_area[i].start, pam_area[i].end, 0x42));
#endif

        /* Verify the area is our new mask */
        g_assert(verify_area(pam_area[i].start, pam_area[i].end, 0x42));

        /* Write out a new mask */
        write_area(pam_area[i].start, pam_area[i].end, 0x82);

#ifndef BROKEN
        /* QEMU only supports a limited form of PAM */

        /* Verify the area is not our mask */
        g_assert(!verify_area(pam_area[i].start, pam_area[i].end, 0x82));

        /* Switch to RE for the area */
        pam_set(dev, i, PAM_RE | PAM_WE);
#endif
        /* Verify the area is our new mask */
        g_assert(verify_area(pam_area[i].start, pam_area[i].end, 0x82));

        /* Reset area */
        pam_set(dev, i, 0);

        /* Verify the area is not our new mask */
        g_assert(!verify_area(pam_area[i].start, pam_area[i].end, 0x82));
    }

    g_free(dev);
    qpci_free_pc(bus);
    qtest_end();
}

#define BLOB_SIZE ((size_t)65536)
#define ISA_BIOS_MAXSZ ((size_t)(128 * 1024))

/* Create a blob file, and return its absolute pathname as a dynamically
 * allocated string.
 * The file is closed before the function returns.
 * In case of error, NULL is returned. The function prints the error message.
 */
static char *create_blob_file(void)
{
    int ret, fd;
    char *pathname;
    GError *error = NULL;

    ret = -1;
    fd = g_file_open_tmp("blob_XXXXXX", &pathname, &error);
    if (fd == -1) {
        fprintf(stderr, "unable to create blob file: %s\n", error->message);
        g_error_free(error);
    } else {
        if (ftruncate(fd, BLOB_SIZE) == -1) {
            fprintf(stderr, "ftruncate(\"%s\", %zu): %s\n", pathname,
                    BLOB_SIZE, strerror(errno));
        } else {
            void *buf;

            buf = mmap(NULL, BLOB_SIZE, PROT_WRITE, MAP_SHARED, fd, 0);
            if (buf == MAP_FAILED) {
                fprintf(stderr, "mmap(\"%s\", %zu): %s\n", pathname, BLOB_SIZE,
                        strerror(errno));
            } else {
                size_t i;

                for (i = 0; i < BLOB_SIZE; ++i) {
                    ((uint8_t *)buf)[i] = i;
                }
                munmap(buf, BLOB_SIZE);
                ret = 0;
            }
        }
        close(fd);
        if (ret == -1) {
            unlink(pathname);
            g_free(pathname);
        }
    }

    return ret == -1 ? NULL : pathname;
}

static void test_i440fx_firmware(FirmwareTestFixture *fixture,
                                 gconstpointer user_data)
{
    char *fw_pathname, *cmdline;
    uint8_t *buf;
    size_t i, isa_bios_size;

    fw_pathname = create_blob_file();
    g_assert(fw_pathname != NULL);

    /* Better hope the user didn't put metacharacters in TMPDIR and co. */
    cmdline = g_strdup_printf("-S %s%s", fixture->is_bios
                                         ? "-bios "
                                         : "-drive if=pflash,format=raw,file=",
                              fw_pathname);
    g_test_message("qemu cmdline: %s", cmdline);
    qtest_start(cmdline);
    g_free(cmdline);

    /* QEMU has loaded the firmware (because qtest_start() only returns after
     * the QMP handshake completes). We must unlink the firmware blob right
     * here, because any assertion firing below would leak it in the
     * filesystem. This is also the reason why we recreate the blob every time
     * this function is invoked.
     */
    unlink(fw_pathname);
    g_free(fw_pathname);

    /* check below 4G */
    buf = g_malloc0(BLOB_SIZE);
    memread(0x100000000ULL - BLOB_SIZE, buf, BLOB_SIZE);
    for (i = 0; i < BLOB_SIZE; ++i) {
        g_assert_cmphex(buf[i], ==, (uint8_t)i);
    }

    /* check in ISA space too */
    memset(buf, 0, BLOB_SIZE);
    isa_bios_size = ISA_BIOS_MAXSZ < BLOB_SIZE ? ISA_BIOS_MAXSZ : BLOB_SIZE;
    memread(0x100000 - isa_bios_size, buf, isa_bios_size);
    for (i = 0; i < isa_bios_size; ++i) {
        g_assert_cmphex(buf[i], ==,
                        (uint8_t)((BLOB_SIZE - isa_bios_size) + i));
    }

    g_free(buf);
    qtest_end();
}

static void add_firmware_test(const char *testpath,
                              void (*setup_fixture)(FirmwareTestFixture *f,
                                                    gconstpointer test_data))
{
    qtest_add(testpath, FirmwareTestFixture, NULL, setup_fixture,
              test_i440fx_firmware, NULL);
}

static void request_bios(FirmwareTestFixture *fixture,
                         gconstpointer user_data)
{
    fixture->is_bios = true;
}

static void request_pflash(FirmwareTestFixture *fixture,
                           gconstpointer user_data)
{
    fixture->is_bios = false;
}

int main(int argc, char **argv)
{
    TestData data;

    g_test_init(&argc, &argv, NULL);

    data.num_cpus = 1;

    qtest_add_data_func("i440fx/defaults", &data, test_i440fx_defaults);
    qtest_add_data_func("i440fx/pam", &data, test_i440fx_pam);
    add_firmware_test("i440fx/firmware/bios", request_bios);
    add_firmware_test("i440fx/firmware/pflash", request_pflash);

    return g_test_run();
}
