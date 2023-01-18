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

void e1000e_wait_isr(QE1000E *d, uint16_t msg_id);
void e1000e_tx_ring_push(QE1000E *d, void *descr);
void e1000e_rx_ring_push(QE1000E *d, void *descr);

#endif
