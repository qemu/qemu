/*
 * Copyright (C) 2011       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "xen-host-pci-device.h"

#define XEN_HOST_PCI_MAX_EXT_CAP \
    ((PCIE_CONFIG_SPACE_SIZE - PCI_CONFIG_SPACE_SIZE) / (PCI_CAP_SIZEOF + 4))

#ifdef XEN_HOST_PCI_DEVICE_DEBUG
#  define XEN_HOST_PCI_LOG(f, a...) fprintf(stderr, "%s: " f, __func__, ##a)
#else
#  define XEN_HOST_PCI_LOG(f, a...) (void)0
#endif

/*
 * from linux/ioport.h
 * IO resources have these defined flags.
 */
#define IORESOURCE_BITS         0x000000ff      /* Bus-specific bits */

#define IORESOURCE_TYPE_BITS    0x00000f00      /* Resource type */
#define IORESOURCE_IO           0x00000100
#define IORESOURCE_MEM          0x00000200

#define IORESOURCE_PREFETCH     0x00001000      /* No side effects */
#define IORESOURCE_MEM_64       0x00100000

static int xen_host_pci_sysfs_path(const XenHostPCIDevice *d,
                                   const char *name, char *buf, ssize_t size)
{
    int rc;

    rc = snprintf(buf, size, "/sys/bus/pci/devices/%04x:%02x:%02x.%d/%s",
                  d->domain, d->bus, d->dev, d->func, name);

    if (rc >= size || rc < 0) {
        /* The ouput is truncated or an other error is encountered */
        return -ENODEV;
    }
    return 0;
}


/* This size should be enough to read the first 7 lines of a resource file */
#define XEN_HOST_PCI_RESOURCE_BUFFER_SIZE 400
static int xen_host_pci_get_resource(XenHostPCIDevice *d)
{
    int i, rc, fd;
    char path[PATH_MAX];
    char buf[XEN_HOST_PCI_RESOURCE_BUFFER_SIZE];
    unsigned long long start, end, flags, size;
    char *endptr, *s;
    uint8_t type;

    rc = xen_host_pci_sysfs_path(d, "resource", path, sizeof (path));
    if (rc) {
        return rc;
    }
    fd = open(path, O_RDONLY);
    if (fd == -1) {
        XEN_HOST_PCI_LOG("Error: Can't open %s: %s\n", path, strerror(errno));
        return -errno;
    }

    do {
        rc = read(fd, &buf, sizeof (buf) - 1);
        if (rc < 0 && errno != EINTR) {
            rc = -errno;
            goto out;
        }
    } while (rc < 0);
    buf[rc] = 0;
    rc = 0;

    s = buf;
    for (i = 0; i < PCI_NUM_REGIONS; i++) {
        type = 0;

        start = strtoll(s, &endptr, 16);
        if (*endptr != ' ' || s == endptr) {
            break;
        }
        s = endptr + 1;
        end = strtoll(s, &endptr, 16);
        if (*endptr != ' ' || s == endptr) {
            break;
        }
        s = endptr + 1;
        flags = strtoll(s, &endptr, 16);
        if (*endptr != '\n' || s == endptr) {
            break;
        }
        s = endptr + 1;

        if (start) {
            size = end - start + 1;
        } else {
            size = 0;
        }

        if (flags & IORESOURCE_IO) {
            type |= XEN_HOST_PCI_REGION_TYPE_IO;
        }
        if (flags & IORESOURCE_MEM) {
            type |= XEN_HOST_PCI_REGION_TYPE_MEM;
        }
        if (flags & IORESOURCE_PREFETCH) {
            type |= XEN_HOST_PCI_REGION_TYPE_PREFETCH;
        }
        if (flags & IORESOURCE_MEM_64) {
            type |= XEN_HOST_PCI_REGION_TYPE_MEM_64;
        }

        if (i < PCI_ROM_SLOT) {
            d->io_regions[i].base_addr = start;
            d->io_regions[i].size = size;
            d->io_regions[i].type = type;
            d->io_regions[i].bus_flags = flags & IORESOURCE_BITS;
        } else {
            d->rom.base_addr = start;
            d->rom.size = size;
            d->rom.type = type;
            d->rom.bus_flags = flags & IORESOURCE_BITS;
        }
    }
    if (i != PCI_NUM_REGIONS) {
        /* Invalid format or input to short */
        rc = -ENODEV;
    }

out:
    close(fd);
    return rc;
}

/* This size should be enough to read a long from a file */
#define XEN_HOST_PCI_GET_VALUE_BUFFER_SIZE 22
static int xen_host_pci_get_value(XenHostPCIDevice *d, const char *name,
                                  unsigned int *pvalue, int base)
{
    char path[PATH_MAX];
    char buf[XEN_HOST_PCI_GET_VALUE_BUFFER_SIZE];
    int fd, rc;
    unsigned long value;
    char *endptr;

    rc = xen_host_pci_sysfs_path(d, name, path, sizeof (path));
    if (rc) {
        return rc;
    }
    fd = open(path, O_RDONLY);
    if (fd == -1) {
        XEN_HOST_PCI_LOG("Error: Can't open %s: %s\n", path, strerror(errno));
        return -errno;
    }
    do {
        rc = read(fd, &buf, sizeof (buf) - 1);
        if (rc < 0 && errno != EINTR) {
            rc = -errno;
            goto out;
        }
    } while (rc < 0);
    buf[rc] = 0;
    value = strtol(buf, &endptr, base);
    if (endptr == buf || *endptr != '\n') {
        rc = -1;
    } else if ((value == LONG_MIN || value == LONG_MAX) && errno == ERANGE) {
        rc = -errno;
    } else {
        rc = 0;
        *pvalue = value;
    }
out:
    close(fd);
    return rc;
}

static inline int xen_host_pci_get_hex_value(XenHostPCIDevice *d,
                                             const char *name,
                                             unsigned int *pvalue)
{
    return xen_host_pci_get_value(d, name, pvalue, 16);
}

static inline int xen_host_pci_get_dec_value(XenHostPCIDevice *d,
                                             const char *name,
                                             unsigned int *pvalue)
{
    return xen_host_pci_get_value(d, name, pvalue, 10);
}

static bool xen_host_pci_dev_is_virtfn(XenHostPCIDevice *d)
{
    char path[PATH_MAX];
    struct stat buf;

    if (xen_host_pci_sysfs_path(d, "physfn", path, sizeof (path))) {
        return false;
    }
    return !stat(path, &buf);
}

static int xen_host_pci_config_open(XenHostPCIDevice *d)
{
    char path[PATH_MAX];
    int rc;

    rc = xen_host_pci_sysfs_path(d, "config", path, sizeof (path));
    if (rc) {
        return rc;
    }
    d->config_fd = open(path, O_RDWR);
    if (d->config_fd < 0) {
        return -errno;
    }
    return 0;
}

static int xen_host_pci_config_read(XenHostPCIDevice *d,
                                    int pos, void *buf, int len)
{
    int rc;

    do {
        rc = pread(d->config_fd, buf, len, pos);
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));
    if (rc != len) {
        return -errno;
    }
    return 0;
}

static int xen_host_pci_config_write(XenHostPCIDevice *d,
                                     int pos, const void *buf, int len)
{
    int rc;

    do {
        rc = pwrite(d->config_fd, buf, len, pos);
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));
    if (rc != len) {
        return -errno;
    }
    return 0;
}


int xen_host_pci_get_byte(XenHostPCIDevice *d, int pos, uint8_t *p)
{
    uint8_t buf;
    int rc = xen_host_pci_config_read(d, pos, &buf, 1);
    if (!rc) {
        *p = buf;
    }
    return rc;
}

int xen_host_pci_get_word(XenHostPCIDevice *d, int pos, uint16_t *p)
{
    uint16_t buf;
    int rc = xen_host_pci_config_read(d, pos, &buf, 2);
    if (!rc) {
        *p = le16_to_cpu(buf);
    }
    return rc;
}

int xen_host_pci_get_long(XenHostPCIDevice *d, int pos, uint32_t *p)
{
    uint32_t buf;
    int rc = xen_host_pci_config_read(d, pos, &buf, 4);
    if (!rc) {
        *p = le32_to_cpu(buf);
    }
    return rc;
}

int xen_host_pci_get_block(XenHostPCIDevice *d, int pos, uint8_t *buf, int len)
{
    return xen_host_pci_config_read(d, pos, buf, len);
}

int xen_host_pci_set_byte(XenHostPCIDevice *d, int pos, uint8_t data)
{
    return xen_host_pci_config_write(d, pos, &data, 1);
}

int xen_host_pci_set_word(XenHostPCIDevice *d, int pos, uint16_t data)
{
    data = cpu_to_le16(data);
    return xen_host_pci_config_write(d, pos, &data, 2);
}

int xen_host_pci_set_long(XenHostPCIDevice *d, int pos, uint32_t data)
{
    data = cpu_to_le32(data);
    return xen_host_pci_config_write(d, pos, &data, 4);
}

int xen_host_pci_set_block(XenHostPCIDevice *d, int pos, uint8_t *buf, int len)
{
    return xen_host_pci_config_write(d, pos, buf, len);
}

int xen_host_pci_find_ext_cap_offset(XenHostPCIDevice *d, uint32_t cap)
{
    uint32_t header = 0;
    int max_cap = XEN_HOST_PCI_MAX_EXT_CAP;
    int pos = PCI_CONFIG_SPACE_SIZE;

    do {
        if (xen_host_pci_get_long(d, pos, &header)) {
            break;
        }
        /*
         * If we have no capabilities, this is indicated by cap ID,
         * cap version and next pointer all being 0.
         */
        if (header == 0) {
            break;
        }

        if (PCI_EXT_CAP_ID(header) == cap) {
            return pos;
        }

        pos = PCI_EXT_CAP_NEXT(header);
        if (pos < PCI_CONFIG_SPACE_SIZE) {
            break;
        }

        max_cap--;
    } while (max_cap > 0);

    return -1;
}

int xen_host_pci_device_get(XenHostPCIDevice *d, uint16_t domain,
                            uint8_t bus, uint8_t dev, uint8_t func)
{
    unsigned int v;
    int rc = 0;

    d->config_fd = -1;
    d->domain = domain;
    d->bus = bus;
    d->dev = dev;
    d->func = func;

    rc = xen_host_pci_config_open(d);
    if (rc) {
        goto error;
    }
    rc = xen_host_pci_get_resource(d);
    if (rc) {
        goto error;
    }
    rc = xen_host_pci_get_hex_value(d, "vendor", &v);
    if (rc) {
        goto error;
    }
    d->vendor_id = v;
    rc = xen_host_pci_get_hex_value(d, "device", &v);
    if (rc) {
        goto error;
    }
    d->device_id = v;
    rc = xen_host_pci_get_dec_value(d, "irq", &v);
    if (rc) {
        goto error;
    }
    d->irq = v;
    d->is_virtfn = xen_host_pci_dev_is_virtfn(d);

    return 0;
error:
    if (d->config_fd >= 0) {
        close(d->config_fd);
        d->config_fd = -1;
    }
    return rc;
}

void xen_host_pci_device_put(XenHostPCIDevice *d)
{
    if (d->config_fd >= 0) {
        close(d->config_fd);
        d->config_fd = -1;
    }
}
