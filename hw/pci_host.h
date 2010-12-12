/*
 * QEMU Common PCI Host bridge configuration data space access routines.
 *
 * Copyright (c) 2006 Fabrice Bellard
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

/* Worker routines for a PCI host controller that uses an {address,data}
   register pair to access PCI configuration space.  */

#ifndef PCI_HOST_H
#define PCI_HOST_H

#include "sysbus.h"
#include "rwhandler.h"

struct PCIHostState {
    SysBusDevice busdev;
    ReadWriteHandler conf_handler;
    ReadWriteHandler data_handler;
    uint32_t config_reg;
    PCIBus *bus;
};

void pci_data_write(PCIBus *s, uint32_t addr, uint32_t val, int len);
uint32_t pci_data_read(PCIBus *s, uint32_t addr, int len);

/* for mmio */
int pci_host_conf_register_mmio(PCIHostState *s, int endian);
int pci_host_data_register_mmio(PCIHostState *s, int endian);

/* for ioio */
void pci_host_conf_register_ioport(pio_addr_t ioport, PCIHostState *s);
void pci_host_data_register_ioport(pio_addr_t ioport, PCIHostState *s);

#endif /* PCI_HOST_H */
