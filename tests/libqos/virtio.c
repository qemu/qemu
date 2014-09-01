/*
 * libqos virtio driver
 *
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include "libqtest.h"
#include "libqos/virtio.h"

uint8_t qvirtio_config_readb(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr)
{
    return bus->config_readb(d, addr);
}

uint16_t qvirtio_config_readw(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr)
{
    return bus->config_readw(d, addr);
}

uint32_t qvirtio_config_readl(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr)
{
    return bus->config_readl(d, addr);
}

uint64_t qvirtio_config_readq(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr)
{
    return bus->config_readq(d, addr);
}

void qvirtio_reset(const QVirtioBus *bus, QVirtioDevice *d)
{
    bus->set_status(d, QVIRTIO_RESET);
    g_assert_cmphex(bus->get_status(d), ==, QVIRTIO_RESET);
}

void qvirtio_set_acknowledge(const QVirtioBus *bus, QVirtioDevice *d)
{
    bus->set_status(d, bus->get_status(d) | QVIRTIO_ACKNOWLEDGE);
    g_assert_cmphex(bus->get_status(d), ==, QVIRTIO_ACKNOWLEDGE);
}

void qvirtio_set_driver(const QVirtioBus *bus, QVirtioDevice *d)
{
    bus->set_status(d, bus->get_status(d) | QVIRTIO_DRIVER);
    g_assert_cmphex(bus->get_status(d), ==,
                                    QVIRTIO_DRIVER | QVIRTIO_ACKNOWLEDGE);
}
