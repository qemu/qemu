/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef QGRAPH_E1000E_H
#define QGRAPH_E1000E_H

#include "qgraph.h"
#include "pci.h"

#define E1000E_RX0_MSG_ID           (0)
#define E1000E_TX0_MSG_ID           (1)

#define E1000E_ADDRESS { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 }

typedef struct QE1000E QE1000E;
typedef struct QE1000E_PCI QE1000E_PCI;

struct QE1000E {
    uint64_t tx_ring;
    uint64_t rx_ring;
};

struct QE1000E_PCI {
    QOSGraphObject obj;
    QPCIDevice pci_dev;
    QPCIBar mac_regs;
    QE1000E e1000e;
};

static inline void e1000e_macreg_write(QE1000E *d, uint32_t reg, uint32_t val)
{
    QE1000E_PCI *d_pci = container_of(d, QE1000E_PCI, e1000e);
    qpci_io_writel(&d_pci->pci_dev, d_pci->mac_regs, reg, val);
}

static inline uint32_t e1000e_macreg_read(QE1000E *d, uint32_t reg)
{
    QE1000E_PCI *d_pci = container_of(d, QE1000E_PCI, e1000e);
    return qpci_io_readl(&d_pci->pci_dev, d_pci->mac_regs, reg);
}

void e1000e_wait_isr(QE1000E *d, uint16_t msg_id);
void e1000e_tx_ring_push(QE1000E *d, void *descr);
void e1000e_rx_ring_push(QE1000E *d, void *descr);

#endif
