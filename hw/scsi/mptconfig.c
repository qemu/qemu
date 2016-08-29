/*
 * QEMU LSI SAS1068 Host Bus Adapter emulation - configuration pages
 *
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * Author: Paolo Bonzini
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */
#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/scsi/scsi.h"

#include "mptsas.h"
#include "mpi.h"
#include "trace.h"

/* Generic functions for marshaling and unmarshaling.  */

#define repl1(x) x
#define repl2(x) x x
#define repl3(x) x x x
#define repl4(x) x x x x
#define repl5(x) x x x x x
#define repl6(x) x x x x x x
#define repl7(x) x x x x x x x
#define repl8(x) x x x x x x x x

#define repl(n, x) glue(repl, n)(x)

typedef union PackValue {
    uint64_t ll;
    char *str;
} PackValue;

static size_t vfill(uint8_t *data, size_t size, const char *fmt, va_list ap)
{
    size_t ofs;
    PackValue val;
    const char *p;

    ofs = 0;
    p = fmt;
    while (*p) {
        memset(&val, 0, sizeof(val));
        switch (*p) {
        case '*':
            p++;
            break;
        case 'b':
        case 'w':
        case 'l':
            val.ll = va_arg(ap, int);
            break;
        case 'q':
            val.ll = va_arg(ap, int64_t);
            break;
        case 's':
            val.str = va_arg(ap, void *);
            break;
        }
        switch (*p++) {
        case 'b':
            if (data) {
                stb_p(data + ofs, val.ll);
            }
            ofs++;
            break;
        case 'w':
            if (data) {
                stw_le_p(data + ofs, val.ll);
            }
            ofs += 2;
            break;
        case 'l':
            if (data) {
                stl_le_p(data + ofs, val.ll);
            }
            ofs += 4;
            break;
        case 'q':
            if (data) {
                stq_le_p(data + ofs, val.ll);
            }
            ofs += 8;
            break;
        case 's':
            {
                int cnt = atoi(p);
                if (data) {
                    if (val.str) {
                        strncpy((void *)data + ofs, val.str, cnt);
                    } else {
                        memset((void *)data + ofs, 0, cnt);
                    }
                }
                ofs += cnt;
                break;
            }
        }
    }

    return ofs;
}

static size_t vpack(uint8_t **p_data, const char *fmt, va_list ap1)
{
    size_t size = 0;
    uint8_t *data = NULL;

    if (p_data) {
        va_list ap2;

        va_copy(ap2, ap1);
        size = vfill(NULL, 0, fmt, ap2);
        *p_data = data = g_malloc(size);
        va_end(ap2);
    }
    return vfill(data, size, fmt, ap1);
}

static size_t fill(uint8_t *data, size_t size, const char *fmt, ...)
{
    va_list ap;
    size_t ret;

    va_start(ap, fmt);
    ret = vfill(data, size, fmt, ap);
    va_end(ap);

    return ret;
}

/* Functions to build the page header and fill in the length, always used
 * through the macros.
 */

#define MPTSAS_CONFIG_PACK(number, type, version, fmt, ...)                  \
    mptsas_config_pack(data, "b*bbb" fmt, version, number, type,             \
                       ## __VA_ARGS__)

static size_t mptsas_config_pack(uint8_t **data, const char *fmt, ...)
{
    va_list ap;
    size_t ret;

    va_start(ap, fmt);
    ret = vpack(data, fmt, ap);
    va_end(ap);

    if (data) {
        assert(ret / 4 < 256 && (ret % 4) == 0);
        stb_p(*data + 1, ret / 4);
    }
    return ret;
}

#define MPTSAS_CONFIG_PACK_EXT(number, type, version, fmt, ...)              \
    mptsas_config_pack_ext(data, "b*bbb*wb*b" fmt, version, number,          \
                           MPI_CONFIG_PAGETYPE_EXTENDED, type, ## __VA_ARGS__)

static size_t mptsas_config_pack_ext(uint8_t **data, const char *fmt, ...)
{
    va_list ap;
    size_t ret;

    va_start(ap, fmt);
    ret = vpack(data, fmt, ap);
    va_end(ap);

    if (data) {
        assert(ret < 65536 && (ret % 4) == 0);
        stw_le_p(*data + 4, ret / 4);
    }
    return ret;
}

/* Manufacturing pages */

static
size_t mptsas_config_manufacturing_0(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(0, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "s16s8s16s16s16",
                              "QEMU MPT Fusion",
                              "2.5",
                              "QEMU MPT Fusion",
                              "QEMU",
                              "0000111122223333");
}

static
size_t mptsas_config_manufacturing_1(MPTSASState *s, uint8_t **data, int address)
{
    /* VPD - all zeros */
    return MPTSAS_CONFIG_PACK(1, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "*s256");
}

static
size_t mptsas_config_manufacturing_2(MPTSASState *s, uint8_t **data, int address)
{
    PCIDeviceClass *pcic = PCI_DEVICE_GET_CLASS(s);
    return MPTSAS_CONFIG_PACK(2, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "wb*b*l",
                              pcic->device_id, pcic->revision);
}

static
size_t mptsas_config_manufacturing_3(MPTSASState *s, uint8_t **data, int address)
{
    PCIDeviceClass *pcic = PCI_DEVICE_GET_CLASS(s);
    return MPTSAS_CONFIG_PACK(3, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "wb*b*l",
                              pcic->device_id, pcic->revision);
}

static
size_t mptsas_config_manufacturing_4(MPTSASState *s, uint8_t **data, int address)
{
    /* All zeros */
    return MPTSAS_CONFIG_PACK(4, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x05,
                              "*l*b*b*b*b*b*b*w*s56*l*l*l*l*l*l"
                              "*b*b*w*b*b*w*l*l");
}

static
size_t mptsas_config_manufacturing_5(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(5, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x02,
                              "q*b*b*w*l*l", s->sas_addr);
}

static
size_t mptsas_config_manufacturing_6(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(6, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "*l");
}

static
size_t mptsas_config_manufacturing_7(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(7, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "*l*l*l*s16*b*b*w", MPTSAS_NUM_PORTS);
}

static
size_t mptsas_config_manufacturing_8(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(8, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "*l");
}

static
size_t mptsas_config_manufacturing_9(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(9, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "*l");
}

static
size_t mptsas_config_manufacturing_10(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(10, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "*l");
}

/* I/O unit pages */

static
size_t mptsas_config_io_unit_0(MPTSASState *s, uint8_t **data, int address)
{
    PCIDevice *pci = PCI_DEVICE(s);
    uint64_t unique_value = 0x53504D554D4551LL;  /* "QEMUMPTx" */

    unique_value |= (uint64_t)pci->devfn << 56;
    return MPTSAS_CONFIG_PACK(0, MPI_CONFIG_PAGETYPE_IO_UNIT, 0x00,
                              "q", unique_value);
}

static
size_t mptsas_config_io_unit_1(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(1, MPI_CONFIG_PAGETYPE_IO_UNIT, 0x02, "l",
                              0x41 /* single function, RAID disabled */ );
}

static
size_t mptsas_config_io_unit_2(MPTSASState *s, uint8_t **data, int address)
{
    PCIDevice *pci = PCI_DEVICE(s);
    uint8_t devfn = pci->devfn;
    return MPTSAS_CONFIG_PACK(2, MPI_CONFIG_PAGETYPE_IO_UNIT, 0x02,
                              "llbbw*b*b*w*b*b*w*b*b*w*l",
                              0, 0x100, 0 /* pci bus? */, devfn, 0);
}

static
size_t mptsas_config_io_unit_3(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(3, MPI_CONFIG_PAGETYPE_IO_UNIT, 0x01,
                              "*b*b*w*l");
}

static
size_t mptsas_config_io_unit_4(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(4, MPI_CONFIG_PAGETYPE_IO_UNIT, 0x00, "*l*l*q");
}

/* I/O controller pages */

static
size_t mptsas_config_ioc_0(MPTSASState *s, uint8_t **data, int address)
{
    PCIDeviceClass *pcic = PCI_DEVICE_GET_CLASS(s);

    return MPTSAS_CONFIG_PACK(0, MPI_CONFIG_PAGETYPE_IOC, 0x01,
                              "*l*lwwb*b*b*blww",
                              pcic->vendor_id, pcic->device_id, pcic->revision,
                              pcic->class_id, pcic->subsystem_vendor_id,
                              pcic->subsystem_id);
}

static
size_t mptsas_config_ioc_1(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(1, MPI_CONFIG_PAGETYPE_IOC, 0x03,
                              "*l*l*b*b*b*b");
}

static
size_t mptsas_config_ioc_2(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(2, MPI_CONFIG_PAGETYPE_IOC, 0x04,
                              "*l*b*b*b*b");
}

static
size_t mptsas_config_ioc_3(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(3, MPI_CONFIG_PAGETYPE_IOC, 0x00,
                              "*b*b*w");
}

static
size_t mptsas_config_ioc_4(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(4, MPI_CONFIG_PAGETYPE_IOC, 0x00,
                              "*b*b*w");
}

static
size_t mptsas_config_ioc_5(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(5, MPI_CONFIG_PAGETYPE_IOC, 0x00,
                              "*l*b*b*w");
}

static
size_t mptsas_config_ioc_6(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(6, MPI_CONFIG_PAGETYPE_IOC, 0x01,
                              "*l*b*b*b*b*b*b*b*b*b*b*w*l*l*l*l*b*b*w"
                              "*w*w*w*w*l*l*l");
}

/* SAS I/O unit pages (extended) */

#define MPTSAS_CONFIG_SAS_IO_UNIT_0_SIZE 16

#define MPI_SAS_IOUNIT0_RATE_FAILED_SPEED_NEGOTIATION 0x02
#define MPI_SAS_IOUNIT0_RATE_1_5                      0x08
#define MPI_SAS_IOUNIT0_RATE_3_0                      0x09

#define MPI_SAS_DEVICE_INFO_NO_DEVICE                 0x00000000
#define MPI_SAS_DEVICE_INFO_END_DEVICE                0x00000001
#define MPI_SAS_DEVICE_INFO_SSP_TARGET                0x00000400

#define MPI_SAS_DEVICE0_ASTATUS_NO_ERRORS             0x00

#define MPI_SAS_DEVICE0_FLAGS_DEVICE_PRESENT          0x0001
#define MPI_SAS_DEVICE0_FLAGS_DEVICE_MAPPED           0x0002
#define MPI_SAS_DEVICE0_FLAGS_MAPPING_PERSISTENT      0x0004



static SCSIDevice *mptsas_phy_get_device(MPTSASState *s, int i,
                                         int *phy_handle, int *dev_handle)
{
    SCSIDevice *d = scsi_device_find(&s->bus, 0, i, 0);

    if (phy_handle) {
        *phy_handle = i + 1;
    }
    if (dev_handle) {
        *dev_handle = d ? i + 1 + MPTSAS_NUM_PORTS : 0;
    }
    return d;
}

static
size_t mptsas_config_sas_io_unit_0(MPTSASState *s, uint8_t **data, int address)
{
    size_t size = MPTSAS_CONFIG_PACK_EXT(0, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT, 0x04,
                                         "*w*wb*b*w"
                                         repl(MPTSAS_NUM_PORTS, "*s16"),
                                         MPTSAS_NUM_PORTS);

    if (data) {
        size_t ofs = size - MPTSAS_NUM_PORTS * MPTSAS_CONFIG_SAS_IO_UNIT_0_SIZE;
        int i;

        for (i = 0; i < MPTSAS_NUM_PORTS; i++) {
            int phy_handle, dev_handle;
            SCSIDevice *dev = mptsas_phy_get_device(s, i, &phy_handle, &dev_handle);

            fill(*data + ofs, MPTSAS_CONFIG_SAS_IO_UNIT_0_SIZE,
                 "bbbblwwl", i, 0, 0,
                 (dev
                  ? MPI_SAS_IOUNIT0_RATE_3_0
                  : MPI_SAS_IOUNIT0_RATE_FAILED_SPEED_NEGOTIATION),
                 (dev
                  ? MPI_SAS_DEVICE_INFO_END_DEVICE | MPI_SAS_DEVICE_INFO_SSP_TARGET
                  : MPI_SAS_DEVICE_INFO_NO_DEVICE),
                 dev_handle,
                 dev_handle,
                 0);
            ofs += MPTSAS_CONFIG_SAS_IO_UNIT_0_SIZE;
        }
        assert(ofs == size);
    }
    return size;
}

#define MPTSAS_CONFIG_SAS_IO_UNIT_1_SIZE 12

static
size_t mptsas_config_sas_io_unit_1(MPTSASState *s, uint8_t **data, int address)
{
    size_t size = MPTSAS_CONFIG_PACK_EXT(1, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT, 0x07,
                                         "*w*w*w*wb*b*b*b"
                                         repl(MPTSAS_NUM_PORTS, "*s12"),
                                         MPTSAS_NUM_PORTS);

    if (data) {
        size_t ofs = size - MPTSAS_NUM_PORTS * MPTSAS_CONFIG_SAS_IO_UNIT_1_SIZE;
        int i;

        for (i = 0; i < MPTSAS_NUM_PORTS; i++) {
            SCSIDevice *dev = mptsas_phy_get_device(s, i, NULL, NULL);
            fill(*data + ofs, MPTSAS_CONFIG_SAS_IO_UNIT_1_SIZE,
                 "bbbblww", i, 0, 0,
                 (MPI_SAS_IOUNIT0_RATE_3_0 << 4) | MPI_SAS_IOUNIT0_RATE_1_5,
                 (dev
                  ? MPI_SAS_DEVICE_INFO_END_DEVICE | MPI_SAS_DEVICE_INFO_SSP_TARGET
                  : MPI_SAS_DEVICE_INFO_NO_DEVICE),
                 0, 0);
            ofs += MPTSAS_CONFIG_SAS_IO_UNIT_1_SIZE;
        }
        assert(ofs == size);
    }
    return size;
}

static
size_t mptsas_config_sas_io_unit_2(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK_EXT(2, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT, 0x06,
                                  "*b*b*w*w*w*b*b*w");
}

static
size_t mptsas_config_sas_io_unit_3(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK_EXT(3, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT, 0x06,
                                  "*l*l*l*l*l*l*l*l*l");
}

/* SAS PHY pages (extended) */

static int mptsas_phy_addr_get(MPTSASState *s, int address)
{
    int i;
    if ((address >> MPI_SAS_PHY_PGAD_FORM_SHIFT) == 0) {
        i = address & 255;
    } else if ((address >> MPI_SAS_PHY_PGAD_FORM_SHIFT) == 1) {
        i = address & 65535;
    } else {
        return -EINVAL;
    }

    if (i >= MPTSAS_NUM_PORTS) {
        return -EINVAL;
    }

    return i;
}

static
size_t mptsas_config_phy_0(MPTSASState *s, uint8_t **data, int address)
{
    int phy_handle = -1;
    int dev_handle = -1;
    int i = mptsas_phy_addr_get(s, address);
    SCSIDevice *dev;

    if (i < 0) {
        trace_mptsas_config_sas_phy(s, address, i, phy_handle, dev_handle, 0);
        return i;
    }

    dev = mptsas_phy_get_device(s, i, &phy_handle, &dev_handle);
    trace_mptsas_config_sas_phy(s, address, i, phy_handle, dev_handle, 0);

    return MPTSAS_CONFIG_PACK_EXT(0, MPI_CONFIG_EXTPAGETYPE_SAS_PHY, 0x01,
                                  "w*wqwb*blbb*b*b*l",
                                  dev_handle, s->sas_addr, dev_handle, i,
                                  (dev
                                   ? MPI_SAS_DEVICE_INFO_END_DEVICE /* | MPI_SAS_DEVICE_INFO_SSP_TARGET?? */
                                   : MPI_SAS_DEVICE_INFO_NO_DEVICE),
                                  (MPI_SAS_IOUNIT0_RATE_3_0 << 4) | MPI_SAS_IOUNIT0_RATE_1_5,
                                  (MPI_SAS_IOUNIT0_RATE_3_0 << 4) | MPI_SAS_IOUNIT0_RATE_1_5);
}

static
size_t mptsas_config_phy_1(MPTSASState *s, uint8_t **data, int address)
{
    int phy_handle = -1;
    int dev_handle = -1;
    int i = mptsas_phy_addr_get(s, address);

    if (i < 0) {
        trace_mptsas_config_sas_phy(s, address, i, phy_handle, dev_handle, 1);
        return i;
    }

    (void) mptsas_phy_get_device(s, i, &phy_handle, &dev_handle);
    trace_mptsas_config_sas_phy(s, address, i, phy_handle, dev_handle, 1);

    return MPTSAS_CONFIG_PACK_EXT(1, MPI_CONFIG_EXTPAGETYPE_SAS_PHY, 0x01,
                                  "*l*l*l*l*l");
}

/* SAS device pages (extended) */

static int mptsas_device_addr_get(MPTSASState *s, int address)
{
    uint32_t handle, i;
    uint32_t form = address >> MPI_SAS_PHY_PGAD_FORM_SHIFT;
    if (form == MPI_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE) {
        handle = address & MPI_SAS_DEVICE_PGAD_GNH_HANDLE_MASK;
        do {
            if (handle == 65535) {
                handle = MPTSAS_NUM_PORTS + 1;
            } else {
                ++handle;
            }
            i = handle - 1 - MPTSAS_NUM_PORTS;
        } while (i < MPTSAS_NUM_PORTS && !scsi_device_find(&s->bus, 0, i, 0));

    } else if (form == MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID) {
        if (address & MPI_SAS_DEVICE_PGAD_BT_BUS_MASK) {
            return -EINVAL;
        }
        i = address & MPI_SAS_DEVICE_PGAD_BT_TID_MASK;

    } else if (form == MPI_SAS_DEVICE_PGAD_FORM_HANDLE) {
        handle = address & MPI_SAS_DEVICE_PGAD_H_HANDLE_MASK;
        i = handle - 1 - MPTSAS_NUM_PORTS;

    } else {
        return -EINVAL;
    }

    if (i >= MPTSAS_NUM_PORTS) {
        return -EINVAL;
    }

    return i;
}

static
size_t mptsas_config_sas_device_0(MPTSASState *s, uint8_t **data, int address)
{
    int phy_handle = -1;
    int dev_handle = -1;
    int i = mptsas_device_addr_get(s, address);
    SCSIDevice *dev = mptsas_phy_get_device(s, i, &phy_handle, &dev_handle);

    trace_mptsas_config_sas_device(s, address, i, phy_handle, dev_handle, 0);
    if (!dev) {
        return -ENOENT;
    }

    return MPTSAS_CONFIG_PACK_EXT(0, MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE, 0x05,
                                  "*w*wqwbbwbblwb*b",
                                  dev->wwn, phy_handle, i,
                                  MPI_SAS_DEVICE0_ASTATUS_NO_ERRORS,
                                  dev_handle, i, 0,
                                  MPI_SAS_DEVICE_INFO_END_DEVICE | MPI_SAS_DEVICE_INFO_SSP_TARGET,
                                  (MPI_SAS_DEVICE0_FLAGS_DEVICE_PRESENT |
                                   MPI_SAS_DEVICE0_FLAGS_DEVICE_MAPPED |
                                   MPI_SAS_DEVICE0_FLAGS_MAPPING_PERSISTENT), i);
}

static
size_t mptsas_config_sas_device_1(MPTSASState *s, uint8_t **data, int address)
{
    int phy_handle = -1;
    int dev_handle = -1;
    int i = mptsas_device_addr_get(s, address);
    SCSIDevice *dev = mptsas_phy_get_device(s, i, &phy_handle, &dev_handle);

    trace_mptsas_config_sas_device(s, address, i, phy_handle, dev_handle, 1);
    if (!dev) {
        return -ENOENT;
    }

    return MPTSAS_CONFIG_PACK_EXT(1, MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE, 0x00,
                                  "*lq*lwbb*s20",
                                  dev->wwn, dev_handle, i, 0);
}

static
size_t mptsas_config_sas_device_2(MPTSASState *s, uint8_t **data, int address)
{
    int phy_handle = -1;
    int dev_handle = -1;
    int i = mptsas_device_addr_get(s, address);
    SCSIDevice *dev = mptsas_phy_get_device(s, i, &phy_handle, &dev_handle);

    trace_mptsas_config_sas_device(s, address, i, phy_handle, dev_handle, 2);
    if (!dev) {
        return -ENOENT;
    }

    return MPTSAS_CONFIG_PACK_EXT(2, MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE, 0x01,
                                  "ql", dev->wwn, 0);
}

typedef struct MPTSASConfigPage {
    uint8_t number;
    uint8_t type;
    size_t (*mpt_config_build)(MPTSASState *s, uint8_t **data, int address);
} MPTSASConfigPage;

static const MPTSASConfigPage mptsas_config_pages[] = {
    {
        0, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_0,
    }, {
        1, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_1,
    }, {
        2, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_2,
    }, {
        3, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_3,
    }, {
        4, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_4,
    }, {
        5, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_5,
    }, {
        6, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_6,
    }, {
        7, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_7,
    }, {
        8, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_8,
    }, {
        9, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_9,
    }, {
        10, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_10,
    }, {
        0, MPI_CONFIG_PAGETYPE_IO_UNIT,
        mptsas_config_io_unit_0,
    }, {
        1, MPI_CONFIG_PAGETYPE_IO_UNIT,
        mptsas_config_io_unit_1,
    }, {
        2, MPI_CONFIG_PAGETYPE_IO_UNIT,
        mptsas_config_io_unit_2,
    }, {
        3, MPI_CONFIG_PAGETYPE_IO_UNIT,
        mptsas_config_io_unit_3,
    }, {
        4, MPI_CONFIG_PAGETYPE_IO_UNIT,
        mptsas_config_io_unit_4,
    }, {
        0, MPI_CONFIG_PAGETYPE_IOC,
        mptsas_config_ioc_0,
    }, {
        1, MPI_CONFIG_PAGETYPE_IOC,
        mptsas_config_ioc_1,
    }, {
        2, MPI_CONFIG_PAGETYPE_IOC,
        mptsas_config_ioc_2,
    }, {
        3, MPI_CONFIG_PAGETYPE_IOC,
        mptsas_config_ioc_3,
    }, {
        4, MPI_CONFIG_PAGETYPE_IOC,
        mptsas_config_ioc_4,
    }, {
        5, MPI_CONFIG_PAGETYPE_IOC,
        mptsas_config_ioc_5,
    }, {
        6, MPI_CONFIG_PAGETYPE_IOC,
        mptsas_config_ioc_6,
    }, {
        0, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT,
        mptsas_config_sas_io_unit_0,
    }, {
        1, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT,
        mptsas_config_sas_io_unit_1,
    }, {
        2, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT,
        mptsas_config_sas_io_unit_2,
    }, {
        3, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT,
        mptsas_config_sas_io_unit_3,
    }, {
        0, MPI_CONFIG_EXTPAGETYPE_SAS_PHY,
        mptsas_config_phy_0,
    }, {
        1, MPI_CONFIG_EXTPAGETYPE_SAS_PHY,
        mptsas_config_phy_1,
    }, {
        0, MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE,
        mptsas_config_sas_device_0,
    }, {
        1, MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE,
        mptsas_config_sas_device_1,
    }, {
       2,  MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE,
        mptsas_config_sas_device_2,
    }
};

static const MPTSASConfigPage *mptsas_find_config_page(int type, int number)
{
    const MPTSASConfigPage *page;
    int i;

    for (i = 0; i < ARRAY_SIZE(mptsas_config_pages); i++) {
        page = &mptsas_config_pages[i];
        if (page->type == type && page->number == number) {
            return page;
        }
    }

    return NULL;
}

void mptsas_process_config(MPTSASState *s, MPIMsgConfig *req)
{
    PCIDevice *pci = PCI_DEVICE(s);

    MPIMsgConfigReply reply;
    const MPTSASConfigPage *page;
    size_t length;
    uint8_t type;
    uint8_t *data = NULL;
    uint32_t flags_and_length;
    uint32_t dmalen;
    uint64_t pa;

    mptsas_fix_config_endianness(req);

    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_msg) < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_reply) < sizeof(reply));

    /* Copy common bits from the request into the reply. */
    memset(&reply, 0, sizeof(reply));
    reply.Action      = req->Action;
    reply.Function    = req->Function;
    reply.MsgContext  = req->MsgContext;
    reply.MsgLength   = sizeof(reply) / 4;
    reply.PageType    = req->PageType;
    reply.PageNumber  = req->PageNumber;
    reply.PageLength  = req->PageLength;
    reply.PageVersion = req->PageVersion;

    type = req->PageType & MPI_CONFIG_PAGETYPE_MASK;
    if (type == MPI_CONFIG_PAGETYPE_EXTENDED) {
        type = req->ExtPageType;
        if (type <= MPI_CONFIG_PAGETYPE_MASK) {
            reply.IOCStatus = MPI_IOCSTATUS_CONFIG_INVALID_TYPE;
            goto out;
        }

        reply.ExtPageType = req->ExtPageType;
    }

    page = mptsas_find_config_page(type, req->PageNumber);

    switch(req->Action) {
    case MPI_CONFIG_ACTION_PAGE_DEFAULT:
    case MPI_CONFIG_ACTION_PAGE_HEADER:
    case MPI_CONFIG_ACTION_PAGE_READ_NVRAM:
    case MPI_CONFIG_ACTION_PAGE_READ_CURRENT:
    case MPI_CONFIG_ACTION_PAGE_READ_DEFAULT:
    case MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT:
    case MPI_CONFIG_ACTION_PAGE_WRITE_NVRAM:
        break;

    default:
        reply.IOCStatus = MPI_IOCSTATUS_CONFIG_INVALID_ACTION;
        goto out;
    }

    if (!page) {
        page = mptsas_find_config_page(type, 1);
        if (page) {
            reply.IOCStatus = MPI_IOCSTATUS_CONFIG_INVALID_PAGE;
        } else {
            reply.IOCStatus = MPI_IOCSTATUS_CONFIG_INVALID_TYPE;
        }
        goto out;
    }

    if (req->Action == MPI_CONFIG_ACTION_PAGE_DEFAULT ||
        req->Action == MPI_CONFIG_ACTION_PAGE_HEADER) {
        length = page->mpt_config_build(s, NULL, req->PageAddress);
        if ((ssize_t)length < 0) {
            reply.IOCStatus = MPI_IOCSTATUS_CONFIG_INVALID_PAGE;
            goto out;
        } else {
            goto done;
        }
    }

    if (req->Action == MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT ||
        req->Action == MPI_CONFIG_ACTION_PAGE_WRITE_NVRAM) {
        length = page->mpt_config_build(s, NULL, req->PageAddress);
        if ((ssize_t)length < 0) {
            reply.IOCStatus = MPI_IOCSTATUS_CONFIG_INVALID_PAGE;
        } else {
            reply.IOCStatus = MPI_IOCSTATUS_CONFIG_CANT_COMMIT;
        }
        goto out;
    }

    flags_and_length = req->PageBufferSGE.FlagsLength;
    dmalen = flags_and_length & MPI_SGE_LENGTH_MASK;
    if (dmalen == 0) {
        length = page->mpt_config_build(s, NULL, req->PageAddress);
        if ((ssize_t)length < 0) {
            reply.IOCStatus = MPI_IOCSTATUS_CONFIG_INVALID_PAGE;
            goto out;
        } else {
            goto done;
        }
    }

    if (flags_and_length & MPI_SGE_FLAGS_64_BIT_ADDRESSING) {
        pa = req->PageBufferSGE.u.Address64;
    } else {
        pa = req->PageBufferSGE.u.Address32;
    }

    /* Only read actions left.  */
    length = page->mpt_config_build(s, &data, req->PageAddress);
    if ((ssize_t)length < 0) {
        reply.IOCStatus = MPI_IOCSTATUS_CONFIG_INVALID_PAGE;
        goto out;
    } else {
        assert(data[2] == page->number);
        pci_dma_write(pci, pa, data, MIN(length, dmalen));
        goto done;
    }

    abort();

done:
    if (type > MPI_CONFIG_PAGETYPE_MASK) {
        reply.ExtPageLength = length / 4;
        reply.ExtPageType   = req->ExtPageType;
    } else {
        reply.PageLength    = length / 4;
    }

out:
    mptsas_fix_config_reply_endianness(&reply);
    mptsas_reply(s, (MPIDefaultReply *)&reply);
    g_free(data);
}
