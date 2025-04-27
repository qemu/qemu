/*
 * QEMU Intel IGD Passthrough Host Bridge Emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/pci-host/i440fx.h"
#include "qapi/error.h"

typedef struct {
    uint8_t offset;
    uint8_t len;
} IGDHostInfo;

/* Here we just expose minimal host bridge offset subset. */
static const IGDHostInfo igd_host_bridge_infos[] = {
    {PCI_REVISION_ID,         2},
    {PCI_SUBSYSTEM_VENDOR_ID, 2},
    {PCI_SUBSYSTEM_ID,        2},
    {0x50,                    2}, /* SNB: processor graphics control register */
    {0x52,                    2}, /* processor graphics control register */
    {0xa4,                    4}, /* SNB: graphics base of stolen memory */
    {0xa8,                    4}, /* SNB: base of GTT stolen memory */
};

static void host_pci_config_read(int pos, int len, uint32_t *val, Error **errp)
{
    int rc, config_fd;
    /* Access real host bridge. */
    char *path = g_strdup_printf("/sys/bus/pci/devices/%04x:%02x:%02x.%d/%s",
                                 0, 0, 0, 0, "config");

    config_fd = open(path, O_RDWR);
    if (config_fd < 0) {
        error_setg_errno(errp, errno, "Failed to open: %s", path);
        goto out;
    }

    if (lseek(config_fd, pos, SEEK_SET) != pos) {
        error_setg_errno(errp, errno, "Failed to seek: %s", path);
        goto out_close_fd;
    }

    do {
        rc = read(config_fd, (uint8_t *)val, len);
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));
    if (rc != len) {
        error_setg_errno(errp, errno, "Failed to read: %s", path);
    }

 out_close_fd:
    close(config_fd);
 out:
    g_free(path);
}

static void igd_pt_i440fx_realize(PCIDevice *pci_dev, Error **errp)
{
    ERRP_GUARD();
    uint32_t val = 0;
    size_t i;
    int pos, len;

    for (i = 0; i < ARRAY_SIZE(igd_host_bridge_infos); i++) {
        pos = igd_host_bridge_infos[i].offset;
        len = igd_host_bridge_infos[i].len;
        host_pci_config_read(pos, len, &val, errp);
        if (*errp) {
            return;
        }
        pci_default_write_config(pci_dev, pos, val, len);
    }
}

static void igd_passthrough_i440fx_class_init(ObjectClass *klass,
                                              const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = igd_pt_i440fx_realize;
    dc->desc = "IGD Passthrough Host bridge";
}

static const TypeInfo igd_passthrough_i440fx_info = {
    .name          = TYPE_IGD_PASSTHROUGH_I440FX_PCI_DEVICE,
    .parent        = TYPE_I440FX_PCI_DEVICE,
    .instance_size = sizeof(PCII440FXState),
    .class_init    = igd_passthrough_i440fx_class_init,
};

static void igd_pt_i440fx_register_types(void)
{
    type_register_static(&igd_passthrough_i440fx_info);
}

type_init(igd_pt_i440fx_register_types)
