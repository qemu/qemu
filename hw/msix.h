#ifndef QEMU_MSIX_H
#define QEMU_MSIX_H

#include "qemu-common.h"

int msix_init(struct PCIDevice *dev, unsigned short nentries,
              unsigned bar_nr, unsigned bar_size,
              a_target_phys_addr page_size);

void msix_write_config(PCIDevice *pci_dev, uint32_t address,
                       uint32_t val, int len);

void msix_mmio_map(PCIDevice *pci_dev, int region_num,
                   uint32_t addr, uint32_t size, int type);

int msix_uninit(PCIDevice *d);

void msix_save(PCIDevice *dev, QEMUFile *f);
void msix_load(PCIDevice *dev, QEMUFile *f);

int msix_enabled(PCIDevice *dev);
int msix_present(PCIDevice *dev);

uint32_t msix_bar_size(PCIDevice *dev);

int msix_vector_use(PCIDevice *dev, unsigned vector);
void msix_vector_unuse(PCIDevice *dev, unsigned vector);

void msix_notify(PCIDevice *dev, unsigned vector);

void msix_reset(PCIDevice *dev);

extern int msix_supported;

#endif
