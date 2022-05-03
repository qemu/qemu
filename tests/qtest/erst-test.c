/*
 * QTest testcase for acpi-erst
 *
 * Copyright (c) 2021 Oracle
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include "libqos/libqos-pc.h"
#include "libqtest.h"

#include "hw/pci/pci.h"

static void save_fn(QPCIDevice *dev, int devfn, void *data)
{
    QPCIDevice **pdev = (QPCIDevice **) data;

    *pdev = dev;
}

static QPCIDevice *get_erst_device(QPCIBus *pcibus)
{
    QPCIDevice *dev;

    dev = NULL;
    qpci_device_foreach(pcibus,
        PCI_VENDOR_ID_REDHAT,
        PCI_DEVICE_ID_REDHAT_ACPI_ERST,
        save_fn, &dev);
    g_assert(dev != NULL);

    return dev;
}

typedef struct _ERSTState {
    QOSState *qs;
    QPCIBar reg_bar, mem_bar;
    uint64_t reg_barsize, mem_barsize;
    QPCIDevice *dev;
} ERSTState;

#define ACTION 0
#define VALUE 8

static const char *reg2str(unsigned reg)
{
    switch (reg) {
    case 0:
        return "ACTION";
    case 8:
        return "VALUE";
    default:
        return NULL;
    }
}

static inline uint32_t in_reg32(ERSTState *s, unsigned reg)
{
    const char *name = reg2str(reg);
    uint32_t res;

    res = qpci_io_readl(s->dev, s->reg_bar, reg);
    g_test_message("*%s -> %08x", name, res);

    return res;
}

static inline uint64_t in_reg64(ERSTState *s, unsigned reg)
{
    const char *name = reg2str(reg);
    uint64_t res;

    res = qpci_io_readq(s->dev, s->reg_bar, reg);
    g_test_message("*%s -> %016" PRIx64, name, res);

    return res;
}

static inline void out_reg32(ERSTState *s, unsigned reg, uint32_t v)
{
    const char *name = reg2str(reg);

    g_test_message("%08x -> *%s", v, name);
    qpci_io_writel(s->dev, s->reg_bar, reg, v);
}

static void cleanup_vm(ERSTState *s)
{
    g_free(s->dev);
    qtest_shutdown(s->qs);
}

static void setup_vm_cmd(ERSTState *s, const char *cmd)
{
    const char *arch = qtest_get_arch();

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        s->qs = qtest_pc_boot(cmd);
    } else {
        g_printerr("erst-test tests are only available on x86\n");
        exit(EXIT_FAILURE);
    }
    s->dev = get_erst_device(s->qs->pcibus);

    s->reg_bar = qpci_iomap(s->dev, 0, &s->reg_barsize);
    g_assert_cmpuint(s->reg_barsize, ==, 16);

    s->mem_bar = qpci_iomap(s->dev, 1, &s->mem_barsize);
    g_assert_cmpuint(s->mem_barsize, ==, 0x2000);

    qpci_device_enable(s->dev);
}

static void test_acpi_erst_basic(void)
{
    ERSTState state;
    uint64_t log_address_range;
    uint64_t log_address_length;
    uint32_t log_address_attr;

    setup_vm_cmd(&state,
        "-object memory-backend-file,"
            "mem-path=acpi-erst.XXXXXX,"
            "size=64K,"
            "share=on,"
            "id=nvram "
        "-device acpi-erst,"
            "memdev=nvram");

    out_reg32(&state, ACTION, 0xD);
    log_address_range = in_reg64(&state, VALUE);
    out_reg32(&state, ACTION, 0xE);
    log_address_length = in_reg64(&state, VALUE);
    out_reg32(&state, ACTION, 0xF);
    log_address_attr = in_reg32(&state, VALUE);

    /* Check log_address_range is not 0, ~0 or base */
    g_assert_cmpuint(log_address_range, !=,  0ULL);
    g_assert_cmpuint(log_address_range, !=, ~0ULL);
    g_assert_cmpuint(log_address_range, !=, state.reg_bar.addr);
    g_assert_cmpuint(log_address_range, ==, state.mem_bar.addr);

    /* Check log_address_length is bar1_size */
    g_assert_cmpuint(log_address_length, ==, state.mem_barsize);

    /* Check log_address_attr is 0 */
    g_assert_cmpuint(log_address_attr, ==, 0);

    cleanup_vm(&state);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/acpi-erst/basic", test_acpi_erst_basic);
    ret = g_test_run();
    return ret;
}
