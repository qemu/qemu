/*
 * pcie_aer.h
 *
 * Copyright (c) 2010 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_PCIE_AER_H
#define QEMU_PCIE_AER_H

#include "hw.h"

/* definitions which PCIExpressDevice uses */

/* AER log */
struct PCIEAERLog {
    /* This structure is saved/loaded.
       So explicitly size them instead of unsigned int */

    /* the number of currently recorded log in log member */
    uint16_t log_num;

    /*
     * The maximum number of the log. Errors can be logged up to this.
     *
     * This is configurable property.
     * The specified value will be clipped down to PCIE_AER_LOG_MAX_LIMIT
     * to avoid unreasonable memory usage.
     * I bet that 128 log size would be big enough, otherwise too many errors
     * for system to function normaly. But could consecutive errors occur?
     */
#define PCIE_AER_LOG_MAX_DEFAULT        8
#define PCIE_AER_LOG_MAX_LIMIT          128
#define PCIE_AER_LOG_MAX_UNSET          0xffff
    uint16_t log_max;

    /* Error log. log_max-sized array */
    PCIEAERErr *log;
};

/* aer error message: error signaling message has only error sevirity and
   source id. See 2.2.8.3 error signaling messages */
struct PCIEAERMsg {
    /*
     * PCI_ERR_ROOT_CMD_{COR, NONFATAL, FATAL}_EN
     * = PCI_EXP_DEVCTL_{CERE, NFERE, FERE}
     */
    uint32_t severity;

    uint16_t source_id; /* bdf */
};

static inline bool
pcie_aer_msg_is_uncor(const PCIEAERMsg *msg)
{
    return msg->severity == PCI_ERR_ROOT_CMD_NONFATAL_EN ||
        msg->severity == PCI_ERR_ROOT_CMD_FATAL_EN;
}

/* error */
struct PCIEAERErr {
    uint32_t status;    /* error status bits */
    uint16_t source_id; /* bdf */

#define PCIE_AER_ERR_IS_CORRECTABLE     0x1     /* correctable/uncorrectable */
#define PCIE_AER_ERR_MAYBE_ADVISORY     0x2     /* maybe advisory non-fatal */
#define PCIE_AER_ERR_HEADER_VALID       0x4     /* TLP header is logged */
#define PCIE_AER_ERR_TLP_PREFIX_PRESENT 0x8     /* TLP Prefix is logged */
    uint16_t flags;

    uint32_t header[4]; /* TLP header */
    uint32_t prefix[4]; /* TLP header prefix */
};

extern const VMStateDescription vmstate_pcie_aer_log;

int pcie_aer_init(PCIDevice *dev, uint16_t offset);
void pcie_aer_exit(PCIDevice *dev);
void pcie_aer_write_config(PCIDevice *dev,
                           uint32_t addr, uint32_t val, int len);

/* aer root port */
void pcie_aer_root_set_vector(PCIDevice *dev, unsigned int vector);
void pcie_aer_root_init(PCIDevice *dev);
void pcie_aer_root_reset(PCIDevice *dev);
void pcie_aer_root_write_config(PCIDevice *dev,
                                uint32_t addr, uint32_t val, int len,
                                uint32_t root_cmd_prev);

/* error injection */
int pcie_aer_inject_error(PCIDevice *dev, const PCIEAERErr *err);

#endif /* QEMU_PCIE_AER_H */
