/*
 * QEMU PCI to ISA bridge
 *
 * Copyright (c) 2004 Fabrice Bellard
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
#include "vl.h"

//#define DEBUG_PCI

typedef struct PIIX3State {
    PCIDevice dev;
    uint8_t elcr1;
    uint8_t elcr2;
} PIIX3State;

static void piix3_reset(PIIX3State *d)
{
    uint8_t *pci_conf = d->dev.config;

    pci_conf[0x04] = 0x07; // master, memory and I/O
    pci_conf[0x05] = 0x00;
    pci_conf[0x06] = 0x00;
    pci_conf[0x07] = 0x02; // PCI_status_devsel_medium
    pci_conf[0x4c] = 0x4d;
    pci_conf[0x4e] = 0x03;
    pci_conf[0x4f] = 0x00;
    pci_conf[0x60] = 0x80;
    pci_conf[0x69] = 0x02;
    pci_conf[0x70] = 0x80;
    pci_conf[0x76] = 0x0c;
    pci_conf[0x77] = 0x0c;
    pci_conf[0x78] = 0x02;
    pci_conf[0x79] = 0x00;
    pci_conf[0x80] = 0x00;
    pci_conf[0x82] = 0x00;
    pci_conf[0xa0] = 0x08;
    pci_conf[0xa0] = 0x08;
    pci_conf[0xa2] = 0x00;
    pci_conf[0xa3] = 0x00;
    pci_conf[0xa4] = 0x00;
    pci_conf[0xa5] = 0x00;
    pci_conf[0xa6] = 0x00;
    pci_conf[0xa7] = 0x00;
    pci_conf[0xa8] = 0x0f;
    pci_conf[0xaa] = 0x00;
    pci_conf[0xab] = 0x00;
    pci_conf[0xac] = 0x00;
    pci_conf[0xae] = 0x00;

    d->elcr1 = 0x00;
    d->elcr2 = 0x00;
}

static uint32_t piix3_read_config(PCIDevice *d, 
                                 uint32_t address, int len)
{
    uint32_t val;
    val = 0;
    memcpy(&val, d->config + address, len);
    return val;
}

static void piix3_write_config(PCIDevice *d, 
                              uint32_t address, uint32_t val, int len)
{
    memcpy(d->config + address, &val, len);
}

void piix3_init(void)
{
    PIIX3State *d;
    uint8_t *pci_conf;

    d = (PIIX3State *)pci_register_device("PIIX3", sizeof(PIIX3State),
                                          0, -1, 
                                          piix3_read_config, 
                                          piix3_write_config);
    pci_conf = d->dev.config;

    pci_conf[0x00] = 0x86; // Intel
    pci_conf[0x01] = 0x80;
    pci_conf[0x02] = 0x00; // 82371SB PIIX3 PCI-to-ISA bridge (Step A1)
    pci_conf[0x03] = 0x70;
    pci_conf[0x0a] = 0x01; // class_sub = PCI_ISA
    pci_conf[0x0b] = 0x06; // class_base = PCI_bridge
    pci_conf[0x0e] = 0x80; // header_type = PCI_multifunction, generic

    piix3_reset(d);
}
