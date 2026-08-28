/*
 * RISC-V board helpers for FDT generation.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RISCV_VIRT_FDT_H
#define RISCV_VIRT_FDT_H

#include "target/riscv/cpu.h"
#include "hw/core/boards.h"
#include "hw/riscv/riscv_hart.h"
#include "exec/hwaddr.h"

#define FDT_PCI_ADDR_CELLS    3
#define FDT_PCI_INT_CELLS     1
#define FDT_PLIC_ADDR_CELLS   0
#define FDT_PLIC_INT_CELLS    1
#define FDT_APLIC_INT_CELLS   2
#define FDT_APLIC_ADDR_CELLS  0
#define FDT_IMSIC_INT_CELLS   0
#define FDT_MAX_INT_CELLS     2
#define FDT_MAX_INT_MAP_WIDTH (FDT_PCI_ADDR_CELLS + FDT_PCI_INT_CELLS + \
                                 1 + FDT_MAX_INT_CELLS)
#define FDT_PLIC_INT_MAP_WIDTH  (FDT_PCI_ADDR_CELLS + FDT_PCI_INT_CELLS + \
                                 1 + FDT_PLIC_INT_CELLS)
#define FDT_APLIC_INT_MAP_WIDTH (FDT_PCI_ADDR_CELLS + FDT_PCI_INT_CELLS + \
                                 1 + FDT_APLIC_INT_CELLS)

typedef enum RISCVAIAType {
    AIA_TYPE_NONE = 0,
    AIA_TYPE_APLIC,
    AIA_TYPE_APLIC_IMSIC,
} RISCVAIAType;

typedef struct IMSICFdtProps {
    /*
     * Machines will statically allocate RISCVHartArrayState[] pointer,
     * e.g. "RISCVHartArrayState soc[VIRT_SOCKETS_MAX]".  We'll have
     * to use a void* pointer to handle a soc with variable sizes.
     */
    void *soc;
    hwaddr imsic_m_base;
    hwaddr imsic_s_base;
    int socket_count;
    int smp_cpus;
    int imsic_group_max_size;
    int irqchip_num_msis;
    int aia_guests;
} IMSICFdtProps;

typedef struct APLICFdtProps {
    const MemMapEntry *aplic_m;
    const MemMapEntry *aplic_s;
    const MemMapEntry *platform_bus;
    int platform_bus_irq;
    int socket;
    int num_harts;
    bool numa_enabled;
    int irqchip_num_sources;
    int aia_type;
} APLICFdtProps;

void *riscv_create_board_device_tree(const char *model, const char *compatible,
                                     int *fdt_size);
void riscv_create_fdt_socket_memory(void *fdt, hwaddr addr, uint64_t size,
                                    int socket_id, bool numa_enabled);
void riscv_create_fdt_clint(void *fdt, hwaddr addr, uint64_t size,
                            uint32_t *intc_phandles, int num_harts);
void riscv_create_fdt_socket_clint(void *fdt, hwaddr addr, uint64_t size,
                                   int socket_id, uint32_t *intc_phandles,
                                   int num_harts, bool numa_enabled);
void riscv_fdt_create_cpu_socket_subnode(void *fdt,
                                         uint64_t timebase_frequency);
void riscv_create_fdt_socket_cpus(void *fdt, RISCVCPU *socket_harts,
                                  int socket_id, int num_harts_socket,
                                  int socket_hartid_base, uint32_t *phandle,
                                  uint32_t *intc_phandles, bool numa_enabled,
                                  bool is_32_bit);
void riscv_create_fdt_socket_cpu_sifive(void *fdt, char *clust_name,
                                        int cpu_id, int socket_id,
                                        int socket_hartid_base,
                                        uint32_t *phandle,
                                        uint32_t *intc_phandles);
void riscv_create_fdt_plic(void *fdt, hwaddr addr, uint64_t size,
                           uint32_t plic_phandle, uint32_t int_cells,
                           uint32_t addr_cells, uint32_t *plic_cells,
                           uint32_t cells_size, uint32_t ndev_sources,
                           bool numa_enabled, int socket);
void riscv_pmu_generate_fdt_node(void *fdt, uint32_t cmask, char *pmu_name);
void riscv_create_fdt_flash(void *fdt, hwaddr flashbase, hwaddr flashsize);
void riscv_create_fdt_syscon(void *fdt, uint32_t *next_phandle,
                             hwaddr addr, hwaddr size,
                             uint32_t reboot, uint32_t poweroff,
                             bool sifive_test_compat);
uint32_t riscv_create_fdt_riscv_iommu_sys(void *fdt, hwaddr addr, hwaddr size,
                                          uint32_t *next_phandle,
                                          uint32_t irq_chip,
                                          uint32_t msi_phandle,
                                          uint32_t iommu_sys_irq);
void riscv_create_fdt_pcie(void *fdt, int aia_type, bool has_iommu_sys,
                           const MemMapEntry *pcie_ecam,
                           const MemMapEntry *pcie_pio,
                           const MemMapEntry *pcie_mmio,
                           const MemMapEntry *high_pcie,
                           uint32_t irq_pcie_phandle,
                           uint32_t msi_pcie_phandle,
                           uint32_t iommu_sys_phandle, uint32_t pcie_irq);
void riscv_create_fdt_imsic(void *fdt, IMSICFdtProps *fdt_props,
                            uint32_t *next_phandle, uint32_t *intc_phandles,
                            uint32_t *msi_m_phandle, uint32_t *msi_s_phandle);
void riscv_create_fdt_socket_aplic(void *fdt, APLICFdtProps *props,
                                   uint32_t msi_m_phandle,
                                   uint32_t msi_s_phandle,
                                   uint32_t *next_phandle,
                                   uint32_t *intc_phandles,
                                   uint32_t *aplic_phandles);
#endif
