/*
 * Board-construction helpers shared by the Hexagon machines.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_HEXAGON_HEX_BOARD_H
#define HW_HEXAGON_HEX_BOARD_H

#include "hw/hexagon/hexagon.h"

/* The L2VIC interrupt specifiers of the 32 per-cpu lines precede its lines. */
#define HEX_BOARD_L2VIC_SPI_BASE 32

/*
 * Create the device tree with its root, /soc and /chosen (including an
 * rng-seed) nodes.  Returns its size in @fdt_size.
 */
void hex_board_create_fdt(MachineState *ms, const char *model,
                          const char *compatible, int *fdt_size);

/* Add the L2VIC node and make it the interrupt parent of /soc. */
int32_t hex_board_fdt_add_l2vic(MachineState *ms,
                                const struct hexagon_machine_config *cfg);

/* Add the /soc/vtcm and /soc/hvx nodes, for what the config provides. */
void hex_board_fdt_add_hvx(MachineState *ms,
                           const struct hexagon_machine_config *cfg);

/* Add one /cpus/cpu@N node per CPU of the machine. */
void hex_board_fdt_add_cpus(MachineState *ms);

/* Create the L2 TCM RAM, if the config has any. */
void hex_board_create_tcm(const struct hexagon_machine_config *cfg,
                          MemoryRegion *tcm);

/* Create the PL011 console and its fixed clock, and describe them. */
void hex_board_create_uart(MachineState *ms, hwaddr base, hwaddr size,
                           int irq, int32_t l2vic_phandle);

/* Create @count virtio-mmio transports on consecutive lines and slots. */
void hex_board_create_virtio(MachineState *ms, hwaddr base, hwaddr size,
                             int irq_base, int count,
                             int32_t l2vic_phandle);

/*
 * Load an ELF for the guest and return its entry point.  If @translate is
 * given it maps the ELF's addresses.  The highest address used is returned
 * in @high, if non-NULL.  Exits on failure.
 */
uint64_t hex_board_load_elf(const char *filename,
                            uint64_t (*translate)(void *opaque, uint64_t addr),
                            void *opaque, uint64_t *high);

/* Copy the device tree to @addr and refresh its rng-seed on reset. */
void hex_board_load_fdt(MachineState *ms, int fdt_size, hwaddr addr);

/*
 * Create the machine's CPUs, with CPU 0 starting at @start if @have_start,
 * and realize the cluster.
 */
void hex_board_create_cpus(MachineState *ms, bool have_start, uint64_t start);

#endif /* HW_HEXAGON_HEX_BOARD_H */
