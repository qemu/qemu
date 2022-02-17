/*
 * pcie_sriov.h:
 *
 * Implementation of SR/IOV emulation support.
 *
 * Copyright (c) 2015 Knut Omang <knut.omang@oracle.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_PCIE_SRIOV_H
#define QEMU_PCIE_SRIOV_H

struct PCIESriovPF {
    uint16_t num_vfs;   /* Number of virtual functions created */
    uint8_t vf_bar_type[PCI_NUM_REGIONS];   /* Store type for each VF bar */
    const char *vfname; /* Reference to the device type used for the VFs */
    PCIDevice **vf;     /* Pointer to an array of num_vfs VF devices */
};

struct PCIESriovVF {
    PCIDevice *pf;      /* Pointer back to owner physical function */
    uint16_t vf_number; /* Logical VF number of this function */
};

void pcie_sriov_pf_init(PCIDevice *dev, uint16_t offset,
                        const char *vfname, uint16_t vf_dev_id,
                        uint16_t init_vfs, uint16_t total_vfs,
                        uint16_t vf_offset, uint16_t vf_stride);
void pcie_sriov_pf_exit(PCIDevice *dev);

/* Set up a VF bar in the SR/IOV bar area */
void pcie_sriov_pf_init_vf_bar(PCIDevice *dev, int region_num,
                               uint8_t type, dma_addr_t size);

/* Instantiate a bar for a VF */
void pcie_sriov_vf_register_bar(PCIDevice *dev, int region_num,
                                MemoryRegion *memory);

/*
 * Default (minimal) page size support values
 * as required by the SR/IOV standard:
 * 0x553 << 12 = 0x553000 = 4K + 8K + 64K + 256K + 1M + 4M
 */
#define SRIOV_SUP_PGSIZE_MINREQ 0x553

/*
 * Optionally add supported page sizes to the mask of supported page sizes
 * Page size values are interpreted as opt_sup_pgsize << 12.
 */
void pcie_sriov_pf_add_sup_pgsize(PCIDevice *dev, uint16_t opt_sup_pgsize);

/* SR/IOV capability config write handler */
void pcie_sriov_config_write(PCIDevice *dev, uint32_t address,
                             uint32_t val, int len);

/* Reset SR/IOV VF Enable bit to unregister all VFs */
void pcie_sriov_pf_disable_vfs(PCIDevice *dev);

/* Get logical VF number of a VF - only valid for VFs */
uint16_t pcie_sriov_vf_number(PCIDevice *dev);

/*
 * Get the physical function that owns this VF.
 * Returns NULL if dev is not a virtual function
 */
PCIDevice *pcie_sriov_get_pf(PCIDevice *dev);

/*
 * Get the n-th VF of this physical function - only valid for PF.
 * Returns NULL if index is invalid
 */
PCIDevice *pcie_sriov_get_vf_at_index(PCIDevice *dev, int n);

#endif /* QEMU_PCIE_SRIOV_H */
