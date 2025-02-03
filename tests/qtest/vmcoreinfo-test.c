/*
 * qtest vmcoreinfo test case
 *
 * Copyright Red Hat. 2025.
 *
 * Authors:
 *  Ani Sinha   <anisinha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "libqos/libqos-pc.h"
#include "libqtest.h"
#include "standard-headers/linux/qemu_fw_cfg.h"
#include "libqos/fw_cfg.h"
#include "qemu/bswap.h"
#include "hw/misc/vmcoreinfo.h"

static void test_vmcoreinfo_write_basic(void)
{
    QFWCFG *fw_cfg;
    QOSState *qs;
    FWCfgVMCoreInfo info;
    size_t filesize;
    uint16_t guest_format;
    uint16_t host_format;
    uint32_t size;
    uint64_t paddr;

    qs = qtest_pc_boot("-device vmcoreinfo");
    fw_cfg = pc_fw_cfg_init(qs->qts);

    memset(&info, 0 , sizeof(info));
    /* read vmcoreinfo and read back the host format */
    filesize = qfw_cfg_read_file(fw_cfg, qs, FW_CFG_VMCOREINFO_FILENAME,
                                &info, sizeof(info));
    g_assert_cmpint(filesize, ==, sizeof(info));

    host_format = le16_to_cpu(info.host_format);
    g_assert_cmpint(host_format, ==, FW_CFG_VMCOREINFO_FORMAT_ELF);

    memset(&info, 0 , sizeof(info));
    info.guest_format = cpu_to_le16(FW_CFG_VMCOREINFO_FORMAT_ELF);
    info.size = cpu_to_le32(1 * MiB);
    info.paddr = cpu_to_le64(0xffffff00);
    info.host_format = cpu_to_le16(host_format);

    /* write the values to the host */
    filesize = qfw_cfg_write_file(fw_cfg, qs, FW_CFG_VMCOREINFO_FILENAME,
                                  &info, sizeof(info));
    g_assert_cmpint(filesize, ==, sizeof(info));

    memset(&info, 0 , sizeof(info));

    /* now read back the values we wrote and compare that they are the same */
    filesize = qfw_cfg_read_file(fw_cfg, qs, FW_CFG_VMCOREINFO_FILENAME,
                                &info, sizeof(info));
    g_assert_cmpint(filesize, ==, sizeof(info));

    size = le32_to_cpu(info.size);
    paddr = le64_to_cpu(info.paddr);
    guest_format = le16_to_cpu(info.guest_format);

    g_assert_cmpint(size, ==, 1 * MiB);
    g_assert_cmpint(paddr, ==, 0xffffff00);
    g_assert_cmpint(guest_format, ==, FW_CFG_VMCOREINFO_FORMAT_ELF);

    pc_fw_cfg_uninit(fw_cfg);
    qtest_shutdown(qs);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") && strcmp(arch, "x86_64")) {
        /* skip for non-x86 */
        exit(EXIT_SUCCESS);
    }

    qtest_add_func("vmcoreinfo/basic-write",
                   test_vmcoreinfo_write_basic);

    return g_test_run();
}
