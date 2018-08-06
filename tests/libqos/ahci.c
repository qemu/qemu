/*
 * libqos AHCI functions
 *
 * Copyright (c) 2014 John Snow <jsnow@redhat.com>
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

#include "qemu/osdep.h"

#include "libqtest.h"
#include "libqos/ahci.h"
#include "libqos/pci-pc.h"

#include "qemu-common.h"
#include "qemu/host-utils.h"

#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"

typedef struct AHCICommandProp {
    uint8_t  cmd;        /* Command Code */
    bool     data;       /* Data transfer command? */
    bool     pio;
    bool     dma;
    bool     lba28;
    bool     lba48;
    bool     read;
    bool     write;
    bool     atapi;
    bool     ncq;
    uint64_t size;       /* Static transfer size, for commands like IDENTIFY. */
    uint32_t interrupts; /* Expected interrupts for this command. */
} AHCICommandProp;

AHCICommandProp ahci_command_properties[] = {
    { .cmd = CMD_READ_PIO,       .data = true,  .pio = true,
                                 .lba28 = true, .read = true },
    { .cmd = CMD_WRITE_PIO,      .data = true,  .pio = true,
                                 .lba28 = true, .write = true },
    { .cmd = CMD_READ_PIO_EXT,   .data = true,  .pio = true,
                                 .lba48 = true, .read = true },
    { .cmd = CMD_WRITE_PIO_EXT,  .data = true,  .pio = true,
                                 .lba48 = true, .write = true },
    { .cmd = CMD_READ_DMA,       .data = true,  .dma = true,
                                 .lba28 = true, .read = true },
    { .cmd = CMD_WRITE_DMA,      .data = true,  .dma = true,
                                 .lba28 = true, .write = true },
    { .cmd = CMD_READ_DMA_EXT,   .data = true,  .dma = true,
                                 .lba48 = true, .read = true },
    { .cmd = CMD_WRITE_DMA_EXT,  .data = true,  .dma = true,
                                 .lba48 = true, .write = true },
    { .cmd = CMD_IDENTIFY,       .data = true,  .pio = true,
                                 .size = 512,   .read = true },
    { .cmd = READ_FPDMA_QUEUED,  .data = true,  .dma = true,
                                 .lba48 = true, .read = true, .ncq = true },
    { .cmd = WRITE_FPDMA_QUEUED, .data = true,  .dma = true,
                                 .lba48 = true, .write = true, .ncq = true },
    { .cmd = CMD_READ_MAX,       .lba28 = true },
    { .cmd = CMD_READ_MAX_EXT,   .lba48 = true },
    { .cmd = CMD_FLUSH_CACHE,    .data = false },
    { .cmd = CMD_PACKET,         .data = true,  .size = 16,
                                 .atapi = true, .pio = true },
    { .cmd = CMD_PACKET_ID,      .data = true,  .pio = true,
                                 .size = 512,   .read = true }
};

struct AHCICommand {
    /* Test Management Data */
    uint8_t name;
    uint8_t port;
    uint8_t slot;
    uint8_t errors;
    uint32_t interrupts;
    uint64_t xbytes;
    uint32_t prd_size;
    uint32_t sector_size;
    uint64_t buffer;
    AHCICommandProp *props;
    /* Data to be transferred to the guest */
    AHCICommandHeader header;
    RegH2DFIS fis;
    unsigned char *atapi_cmd;
};

/**
 * Allocate space in the guest using information in the AHCIQState object.
 */
uint64_t ahci_alloc(AHCIQState *ahci, size_t bytes)
{
    g_assert(ahci);
    g_assert(ahci->parent);
    return qmalloc(ahci->parent, bytes);
}

void ahci_free(AHCIQState *ahci, uint64_t addr)
{
    g_assert(ahci);
    g_assert(ahci->parent);
    qfree(ahci->parent, addr);
}

bool is_atapi(AHCIQState *ahci, uint8_t port)
{
    return ahci_px_rreg(ahci, port, AHCI_PX_SIG) == AHCI_SIGNATURE_CDROM;
}

/**
 * Locate, verify, and return a handle to the AHCI device.
 */
QPCIDevice *get_ahci_device(QTestState *qts, uint32_t *fingerprint)
{
    QPCIDevice *ahci;
    uint32_t ahci_fingerprint;
    QPCIBus *pcibus;

    pcibus = qpci_init_pc(qts, NULL);

    /* Find the AHCI PCI device and verify it's the right one. */
    ahci = qpci_device_find(pcibus, QPCI_DEVFN(0x1F, 0x02));
    g_assert(ahci != NULL);

    ahci_fingerprint = qpci_config_readl(ahci, PCI_VENDOR_ID);

    switch (ahci_fingerprint) {
    case AHCI_INTEL_ICH9:
        break;
    default:
        /* Unknown device. */
        g_assert_not_reached();
    }

    if (fingerprint) {
        *fingerprint = ahci_fingerprint;
    }
    return ahci;
}

void free_ahci_device(QPCIDevice *dev)
{
    QPCIBus *pcibus = dev ? dev->bus : NULL;

    /* libqos doesn't have a function for this, so free it manually */
    g_free(dev);
    qpci_free_pc(pcibus);
}

/* Free all memory in-use by the AHCI device. */
void ahci_clean_mem(AHCIQState *ahci)
{
    uint8_t port, slot;

    for (port = 0; port < 32; ++port) {
        if (ahci->port[port].fb) {
            ahci_free(ahci, ahci->port[port].fb);
            ahci->port[port].fb = 0;
        }
        if (ahci->port[port].clb) {
            for (slot = 0; slot < 32; slot++) {
                ahci_destroy_command(ahci, port, slot);
            }
            ahci_free(ahci, ahci->port[port].clb);
            ahci->port[port].clb = 0;
        }
    }
}

/*** Logical Device Initialization ***/

/**
 * Start the PCI device and sanity-check default operation.
 */
void ahci_pci_enable(AHCIQState *ahci)
{
    uint8_t reg;

    start_ahci_device(ahci);

    switch (ahci->fingerprint) {
    case AHCI_INTEL_ICH9:
        /* ICH9 has a register at PCI 0x92 that
         * acts as a master port enabler mask. */
        reg = qpci_config_readb(ahci->dev, 0x92);
        reg |= 0x3F;
        qpci_config_writeb(ahci->dev, 0x92, reg);
        /* 0...0111111b -- bit significant, ports 0-5 enabled. */
        ASSERT_BIT_SET(qpci_config_readb(ahci->dev, 0x92), 0x3F);
        break;
    }

}

/**
 * Map BAR5/ABAR, and engage the PCI device.
 */
void start_ahci_device(AHCIQState *ahci)
{
    /* Map AHCI's ABAR (BAR5) */
    ahci->hba_bar = qpci_iomap(ahci->dev, 5, &ahci->barsize);

    /* turns on pci.cmd.iose, pci.cmd.mse and pci.cmd.bme */
    qpci_device_enable(ahci->dev);
}

/**
 * Test and initialize the AHCI's HBA memory areas.
 * Initialize and start any ports with devices attached.
 * Bring the HBA into the idle state.
 */
void ahci_hba_enable(AHCIQState *ahci)
{
    /* Bits of interest in this section:
     * GHC.AE     Global Host Control / AHCI Enable
     * PxCMD.ST   Port Command: Start
     * PxCMD.SUD  "Spin Up Device"
     * PxCMD.POD  "Power On Device"
     * PxCMD.FRE  "FIS Receive Enable"
     * PxCMD.FR   "FIS Receive Running"
     * PxCMD.CR   "Command List Running"
     */
    uint32_t reg, ports_impl;
    uint16_t i;
    uint8_t num_cmd_slots;

    g_assert(ahci != NULL);

    /* Set GHC.AE to 1 */
    ahci_set(ahci, AHCI_GHC, AHCI_GHC_AE);
    reg = ahci_rreg(ahci, AHCI_GHC);
    ASSERT_BIT_SET(reg, AHCI_GHC_AE);

    /* Cache CAP and CAP2. */
    ahci->cap = ahci_rreg(ahci, AHCI_CAP);
    ahci->cap2 = ahci_rreg(ahci, AHCI_CAP2);

    /* Read CAP.NCS, how many command slots do we have? */
    num_cmd_slots = ((ahci->cap & AHCI_CAP_NCS) >> ctzl(AHCI_CAP_NCS)) + 1;
    g_test_message("Number of Command Slots: %u", num_cmd_slots);

    /* Determine which ports are implemented. */
    ports_impl = ahci_rreg(ahci, AHCI_PI);

    for (i = 0; ports_impl; ports_impl >>= 1, ++i) {
        if (!(ports_impl & 0x01)) {
            continue;
        }

        g_test_message("Initializing port %u", i);

        reg = ahci_px_rreg(ahci, i, AHCI_PX_CMD);
        if (BITCLR(reg, AHCI_PX_CMD_ST | AHCI_PX_CMD_CR |
                   AHCI_PX_CMD_FRE | AHCI_PX_CMD_FR)) {
            g_test_message("port is idle");
        } else {
            g_test_message("port needs to be idled");
            ahci_px_clr(ahci, i, AHCI_PX_CMD,
                        (AHCI_PX_CMD_ST | AHCI_PX_CMD_FRE));
            /* The port has 500ms to disengage. */
            usleep(500000);
            reg = ahci_px_rreg(ahci, i, AHCI_PX_CMD);
            ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_CR);
            ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_FR);
            g_test_message("port is now idle");
            /* The spec does allow for possibly needing a PORT RESET
             * or HBA reset if we fail to idle the port. */
        }

        /* Allocate Memory for the Command List Buffer & FIS Buffer */
        /* PxCLB space ... 0x20 per command, as in 4.2.2 p 36 */
        ahci->port[i].clb = ahci_alloc(ahci, num_cmd_slots * 0x20);
        qtest_memset(ahci->parent->qts, ahci->port[i].clb, 0x00,
                     num_cmd_slots * 0x20);
        g_test_message("CLB: 0x%08" PRIx64, ahci->port[i].clb);
        ahci_px_wreg(ahci, i, AHCI_PX_CLB, ahci->port[i].clb);
        g_assert_cmphex(ahci->port[i].clb, ==,
                        ahci_px_rreg(ahci, i, AHCI_PX_CLB));

        /* PxFB space ... 0x100, as in 4.2.1 p 35 */
        ahci->port[i].fb = ahci_alloc(ahci, 0x100);
        qtest_memset(ahci->parent->qts, ahci->port[i].fb, 0x00, 0x100);
        g_test_message("FB: 0x%08" PRIx64, ahci->port[i].fb);
        ahci_px_wreg(ahci, i, AHCI_PX_FB, ahci->port[i].fb);
        g_assert_cmphex(ahci->port[i].fb, ==,
                        ahci_px_rreg(ahci, i, AHCI_PX_FB));

        /* Clear PxSERR, PxIS, then IS.IPS[x] by writing '1's. */
        ahci_px_wreg(ahci, i, AHCI_PX_SERR, 0xFFFFFFFF);
        ahci_px_wreg(ahci, i, AHCI_PX_IS, 0xFFFFFFFF);
        ahci_wreg(ahci, AHCI_IS, (1 << i));

        /* Verify Interrupts Cleared */
        reg = ahci_px_rreg(ahci, i, AHCI_PX_SERR);
        g_assert_cmphex(reg, ==, 0);

        reg = ahci_px_rreg(ahci, i, AHCI_PX_IS);
        g_assert_cmphex(reg, ==, 0);

        reg = ahci_rreg(ahci, AHCI_IS);
        ASSERT_BIT_CLEAR(reg, (1 << i));

        /* Enable All Interrupts: */
        ahci_px_wreg(ahci, i, AHCI_PX_IE, 0xFFFFFFFF);
        reg = ahci_px_rreg(ahci, i, AHCI_PX_IE);
        g_assert_cmphex(reg, ==, ~((uint32_t)AHCI_PX_IE_RESERVED));

        /* Enable the FIS Receive Engine. */
        ahci_px_set(ahci, i, AHCI_PX_CMD, AHCI_PX_CMD_FRE);
        reg = ahci_px_rreg(ahci, i, AHCI_PX_CMD);
        ASSERT_BIT_SET(reg, AHCI_PX_CMD_FR);

        /* AHCI 1.3 spec: if !STS.BSY, !STS.DRQ and PxSSTS.DET indicates
         * physical presence, a device is present and may be started. However,
         * PxSERR.DIAG.X /may/ need to be cleared a priori. */
        reg = ahci_px_rreg(ahci, i, AHCI_PX_SERR);
        if (BITSET(reg, AHCI_PX_SERR_DIAG_X)) {
            ahci_px_set(ahci, i, AHCI_PX_SERR, AHCI_PX_SERR_DIAG_X);
        }

        reg = ahci_px_rreg(ahci, i, AHCI_PX_TFD);
        if (BITCLR(reg, AHCI_PX_TFD_STS_BSY | AHCI_PX_TFD_STS_DRQ)) {
            reg = ahci_px_rreg(ahci, i, AHCI_PX_SSTS);
            if ((reg & AHCI_PX_SSTS_DET) == SSTS_DET_ESTABLISHED) {
                /* Device Found: set PxCMD.ST := 1 */
                ahci_px_set(ahci, i, AHCI_PX_CMD, AHCI_PX_CMD_ST);
                ASSERT_BIT_SET(ahci_px_rreg(ahci, i, AHCI_PX_CMD),
                               AHCI_PX_CMD_CR);
                g_test_message("Started Device %u", i);
            } else if ((reg & AHCI_PX_SSTS_DET)) {
                /* Device present, but in some unknown state. */
                g_assert_not_reached();
            }
        }
    }

    /* Enable GHC.IE */
    ahci_set(ahci, AHCI_GHC, AHCI_GHC_IE);
    reg = ahci_rreg(ahci, AHCI_GHC);
    ASSERT_BIT_SET(reg, AHCI_GHC_IE);

    ahci->enabled = true;
    /* TODO: The device should now be idling and waiting for commands.
     * In the future, a small test-case to inspect the Register D2H FIS
     * and clear the initial interrupts might be good. */
}

/**
 * Pick the first implemented and running port
 */
unsigned ahci_port_select(AHCIQState *ahci)
{
    uint32_t ports, reg;
    unsigned i;

    ports = ahci_rreg(ahci, AHCI_PI);
    for (i = 0; i < 32; ports >>= 1, ++i) {
        if (ports == 0) {
            i = 32;
        }

        if (!(ports & 0x01)) {
            continue;
        }

        reg = ahci_px_rreg(ahci, i, AHCI_PX_CMD);
        if (BITSET(reg, AHCI_PX_CMD_ST)) {
            break;
        }
    }
    g_assert(i < 32);
    return i;
}

/**
 * Clear a port's interrupts and status information prior to a test.
 */
void ahci_port_clear(AHCIQState *ahci, uint8_t port)
{
    uint32_t reg;

    /* Clear out this port's interrupts (ignore the init register d2h fis) */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_IS);
    ahci_px_wreg(ahci, port, AHCI_PX_IS, reg);
    g_assert_cmphex(ahci_px_rreg(ahci, port, AHCI_PX_IS), ==, 0);

    /* Wipe the FIS-Receive Buffer */
    qtest_memset(ahci->parent->qts, ahci->port[port].fb, 0x00, 0x100);
}

/**
 * Check a port for errors.
 */
void ahci_port_check_error(AHCIQState *ahci, uint8_t port,
                           uint32_t imask, uint8_t emask)
{
    uint32_t reg;

    /* The upper 9 bits of the IS register all indicate errors. */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_IS);
    reg &= ~imask;
    reg >>= 23;
    g_assert_cmphex(reg, ==, 0);

    /* The Sata Error Register should be empty. */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_SERR);
    g_assert_cmphex(reg, ==, 0);

    /* The TFD also has two error sections. */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_TFD);
    if (!emask) {
        ASSERT_BIT_CLEAR(reg, AHCI_PX_TFD_STS_ERR);
    } else {
        ASSERT_BIT_SET(reg, AHCI_PX_TFD_STS_ERR);
    }
    ASSERT_BIT_CLEAR(reg, AHCI_PX_TFD_ERR & (~emask << 8));
    ASSERT_BIT_SET(reg, AHCI_PX_TFD_ERR & (emask << 8));
}

void ahci_port_check_interrupts(AHCIQState *ahci, uint8_t port,
                                uint32_t intr_mask)
{
    uint32_t reg;

    /* Check for expected interrupts */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_IS);
    ASSERT_BIT_SET(reg, intr_mask);

    /* Clear expected interrupts and assert all interrupts now cleared. */
    ahci_px_wreg(ahci, port, AHCI_PX_IS, intr_mask);
    g_assert_cmphex(ahci_px_rreg(ahci, port, AHCI_PX_IS), ==, 0);
}

void ahci_port_check_nonbusy(AHCIQState *ahci, uint8_t port, uint8_t slot)
{
    uint32_t reg;

    /* Assert that the command slot is no longer busy (NCQ) */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_SACT);
    ASSERT_BIT_CLEAR(reg, (1 << slot));

    /* Non-NCQ */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_CI);
    ASSERT_BIT_CLEAR(reg, (1 << slot));

    /* And assert that we are generally not busy. */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_TFD);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_TFD_STS_BSY);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_TFD_STS_DRQ);
}

void ahci_port_check_d2h_sanity(AHCIQState *ahci, uint8_t port, uint8_t slot)
{
    RegD2HFIS *d2h = g_malloc0(0x20);
    uint32_t reg;

    qtest_memread(ahci->parent->qts, ahci->port[port].fb + 0x40, d2h, 0x20);
    g_assert_cmphex(d2h->fis_type, ==, 0x34);

    reg = ahci_px_rreg(ahci, port, AHCI_PX_TFD);
    g_assert_cmphex((reg & AHCI_PX_TFD_ERR) >> 8, ==, d2h->error);
    g_assert_cmphex((reg & AHCI_PX_TFD_STS), ==, d2h->status);

    g_free(d2h);
}

void ahci_port_check_pio_sanity(AHCIQState *ahci, AHCICommand *cmd)
{
    PIOSetupFIS *pio = g_malloc0(0x20);
    uint8_t port = cmd->port;

    /* We cannot check the Status or E_Status registers, because
     * the status may have again changed between the PIO Setup FIS
     * and the conclusion of the command with the D2H Register FIS. */
    qtest_memread(ahci->parent->qts, ahci->port[port].fb + 0x20, pio, 0x20);
    g_assert_cmphex(pio->fis_type, ==, 0x5f);

    /* Data transferred by PIO will either be:
     * (1) 12 or 16 bytes for an ATAPI command packet (QEMU always uses 12), or
     * (2) Actual data from the drive.
     * If we do both, (2) winds up erasing any evidence of (1).
     */
    if (cmd->props->atapi && (cmd->xbytes == 0 || cmd->props->dma)) {
        g_assert(le16_to_cpu(pio->tx_count) == 12 ||
                 le16_to_cpu(pio->tx_count) == 16);
    } else {
        /* The AHCI test suite here does not test any PIO command that specifies
         * a DRQ block larger than one sector (like 0xC4), so this should always
         * be one sector or less. */
        size_t pio_len = ((cmd->xbytes % cmd->sector_size) ?
                          (cmd->xbytes % cmd->sector_size) : cmd->sector_size);
        g_assert_cmphex(le16_to_cpu(pio->tx_count), ==, pio_len);
    }
    g_free(pio);
}

void ahci_port_check_cmd_sanity(AHCIQState *ahci, AHCICommand *cmd)
{
    AHCICommandHeader cmdh;

    ahci_get_command_header(ahci, cmd->port, cmd->slot, &cmdh);
    /* Physical Region Descriptor Byte Count is not required to work for NCQ. */
    if (!cmd->props->ncq) {
        g_assert_cmphex(cmd->xbytes, ==, cmdh.prdbc);
    }
}

/* Get the command in #slot of port #port. */
void ahci_get_command_header(AHCIQState *ahci, uint8_t port,
                             uint8_t slot, AHCICommandHeader *cmd)
{
    uint64_t ba = ahci->port[port].clb;
    ba += slot * sizeof(AHCICommandHeader);
    qtest_memread(ahci->parent->qts, ba, cmd, sizeof(AHCICommandHeader));

    cmd->flags = le16_to_cpu(cmd->flags);
    cmd->prdtl = le16_to_cpu(cmd->prdtl);
    cmd->prdbc = le32_to_cpu(cmd->prdbc);
    cmd->ctba = le64_to_cpu(cmd->ctba);
}

/* Set the command in #slot of port #port. */
void ahci_set_command_header(AHCIQState *ahci, uint8_t port,
                             uint8_t slot, AHCICommandHeader *cmd)
{
    AHCICommandHeader tmp = { .flags = 0 };
    uint64_t ba = ahci->port[port].clb;
    ba += slot * sizeof(AHCICommandHeader);

    tmp.flags = cpu_to_le16(cmd->flags);
    tmp.prdtl = cpu_to_le16(cmd->prdtl);
    tmp.prdbc = cpu_to_le32(cmd->prdbc);
    tmp.ctba = cpu_to_le64(cmd->ctba);

    qtest_memwrite(ahci->parent->qts, ba, &tmp, sizeof(AHCICommandHeader));
}

void ahci_destroy_command(AHCIQState *ahci, uint8_t port, uint8_t slot)
{
    AHCICommandHeader cmd;

    /* Obtain the Nth Command Header */
    ahci_get_command_header(ahci, port, slot, &cmd);
    if (cmd.ctba == 0) {
        /* No address in it, so just return -- it's empty. */
        goto tidy;
    }

    /* Free the Table */
    ahci_free(ahci, cmd.ctba);

 tidy:
    /* NULL the header. */
    memset(&cmd, 0x00, sizeof(cmd));
    ahci_set_command_header(ahci, port, slot, &cmd);
    ahci->port[port].ctba[slot] = 0;
    ahci->port[port].prdtl[slot] = 0;
}

void ahci_write_fis(AHCIQState *ahci, AHCICommand *cmd)
{
    RegH2DFIS tmp = cmd->fis;
    uint64_t addr = cmd->header.ctba;

    /* NCQ commands use exclusively 8 bit fields and needs no adjustment.
     * Only the count field needs to be adjusted for non-NCQ commands.
     * The auxiliary FIS fields are defined per-command and are not currently
     * implemented in libqos/ahci.o, but may or may not need to be flipped. */
    if (!cmd->props->ncq) {
        tmp.count = cpu_to_le16(tmp.count);
    }

    qtest_memwrite(ahci->parent->qts, addr, &tmp, sizeof(tmp));
}

unsigned ahci_pick_cmd(AHCIQState *ahci, uint8_t port)
{
    unsigned i;
    unsigned j;
    uint32_t reg;

    reg = ahci_px_rreg(ahci, port, AHCI_PX_CI);

    /* Pick the least recently used command slot that's available */
    for (i = 0; i < 32; ++i) {
        j = ((ahci->port[port].next + i) % 32);
        if (reg & (1 << j)) {
            continue;
        }
        ahci_destroy_command(ahci, port, j);
        ahci->port[port].next = (j + 1) % 32;
        return j;
    }

    g_test_message("All command slots were busy.");
    g_assert_not_reached();
}

inline unsigned size_to_prdtl(unsigned bytes, unsigned bytes_per_prd)
{
    /* Each PRD can describe up to 4MiB */
    g_assert_cmphex(bytes_per_prd, <=, 4096 * 1024);
    g_assert_cmphex(bytes_per_prd & 0x01, ==, 0x00);
    return (bytes + bytes_per_prd - 1) / bytes_per_prd;
}

const AHCIOpts default_opts = { .size = 0 };

/**
 * ahci_exec: execute a given command on a specific
 * AHCI port.
 *
 * @ahci: The device to send the command to
 * @port: The port number of the SATA device we wish
 *        to have execute this command
 * @op:   The S/ATA command to execute, or if opts.atapi
 *        is true, the SCSI command code.
 * @opts: Optional arguments to modify execution behavior.
 */
void ahci_exec(AHCIQState *ahci, uint8_t port,
               uint8_t op, const AHCIOpts *opts_in)
{
    AHCICommand *cmd;
    int rc;
    AHCIOpts *opts;

    opts = g_memdup((opts_in == NULL ? &default_opts : opts_in),
                    sizeof(AHCIOpts));

    /* No guest buffer provided, create one. */
    if (opts->size && !opts->buffer) {
        opts->buffer = ahci_alloc(ahci, opts->size);
        g_assert(opts->buffer);
        qtest_memset(ahci->parent->qts, opts->buffer, 0x00, opts->size);
    }

    /* Command creation */
    if (opts->atapi) {
        uint16_t bcl = opts->set_bcl ? opts->bcl : ATAPI_SECTOR_SIZE;
        cmd = ahci_atapi_command_create(op, bcl, opts->atapi_dma);
    } else {
        cmd = ahci_command_create(op);
    }
    ahci_command_adjust(cmd, opts->lba, opts->buffer,
                        opts->size, opts->prd_size);

    if (opts->pre_cb) {
        rc = opts->pre_cb(ahci, cmd, opts);
        g_assert_cmpint(rc, ==, 0);
    }

    /* Write command to memory and issue it */
    ahci_command_commit(ahci, cmd, port);
    ahci_command_issue_async(ahci, cmd);
    if (opts->error) {
        qtest_qmp_eventwait(ahci->parent->qts, "STOP");
    }
    if (opts->mid_cb) {
        rc = opts->mid_cb(ahci, cmd, opts);
        g_assert_cmpint(rc, ==, 0);
    }
    if (opts->error) {
        qtest_qmp_send(ahci->parent->qts, "{'execute':'cont' }");
        qtest_qmp_eventwait(ahci->parent->qts, "RESUME");
    }

    /* Wait for command to complete and verify sanity */
    ahci_command_wait(ahci, cmd);
    ahci_command_verify(ahci, cmd);
    if (opts->post_cb) {
        rc = opts->post_cb(ahci, cmd, opts);
        g_assert_cmpint(rc, ==, 0);
    }
    ahci_command_free(cmd);
    if (opts->buffer != opts_in->buffer) {
        ahci_free(ahci, opts->buffer);
    }
    g_free(opts);
}

/* Issue a command, expecting it to fail and STOP the VM */
AHCICommand *ahci_guest_io_halt(AHCIQState *ahci, uint8_t port,
                                uint8_t ide_cmd, uint64_t buffer,
                                size_t bufsize, uint64_t sector)
{
    AHCICommand *cmd;

    cmd = ahci_command_create(ide_cmd);
    ahci_command_adjust(cmd, sector, buffer, bufsize, 0);
    ahci_command_commit(ahci, cmd, port);
    ahci_command_issue_async(ahci, cmd);
    qtest_qmp_eventwait(ahci->parent->qts, "STOP");

    return cmd;
}

/* Resume a previously failed command and verify/finalize */
void ahci_guest_io_resume(AHCIQState *ahci, AHCICommand *cmd)
{
    /* Complete the command */
    qtest_qmp_send(ahci->parent->qts, "{'execute':'cont' }");
    qtest_qmp_eventwait(ahci->parent->qts, "RESUME");
    ahci_command_wait(ahci, cmd);
    ahci_command_verify(ahci, cmd);
    ahci_command_free(cmd);
}

/* Given a guest buffer address, perform an IO operation */
void ahci_guest_io(AHCIQState *ahci, uint8_t port, uint8_t ide_cmd,
                   uint64_t buffer, size_t bufsize, uint64_t sector)
{
    AHCICommand *cmd;
    cmd = ahci_command_create(ide_cmd);
    ahci_command_set_buffer(cmd, buffer);
    ahci_command_set_size(cmd, bufsize);
    if (sector) {
        ahci_command_set_offset(cmd, sector);
    }
    ahci_command_commit(ahci, cmd, port);
    ahci_command_issue(ahci, cmd);
    ahci_command_verify(ahci, cmd);
    ahci_command_free(cmd);
}

static AHCICommandProp *ahci_command_find(uint8_t command_name)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(ahci_command_properties); i++) {
        if (ahci_command_properties[i].cmd == command_name) {
            return &ahci_command_properties[i];
        }
    }

    return NULL;
}

/* Given a HOST buffer, create a buffer address and perform an IO operation. */
void ahci_io(AHCIQState *ahci, uint8_t port, uint8_t ide_cmd,
             void *buffer, size_t bufsize, uint64_t sector)
{
    uint64_t ptr;
    AHCICommandProp *props;

    props = ahci_command_find(ide_cmd);
    g_assert(props);
    ptr = ahci_alloc(ahci, bufsize);
    g_assert(!bufsize || ptr);
    qtest_memset(ahci->parent->qts, ptr, 0x00, bufsize);

    if (bufsize && props->write) {
        qtest_bufwrite(ahci->parent->qts, ptr, buffer, bufsize);
    }

    ahci_guest_io(ahci, port, ide_cmd, ptr, bufsize, sector);

    if (bufsize && props->read) {
        qtest_bufread(ahci->parent->qts, ptr, buffer, bufsize);
    }

    ahci_free(ahci, ptr);
}

/**
 * Initializes a basic command header in memory.
 * We assume that this is for an ATA command using RegH2DFIS.
 */
static void command_header_init(AHCICommand *cmd)
{
    AHCICommandHeader *hdr = &cmd->header;
    AHCICommandProp *props = cmd->props;

    hdr->flags = 5;             /* RegH2DFIS is 5 DW long. Must be < 32 */
    hdr->flags |= CMDH_CLR_BSY; /* Clear the BSY bit when done */
    if (props->write) {
        hdr->flags |= CMDH_WRITE;
    }
    if (props->atapi) {
        hdr->flags |= CMDH_ATAPI;
    }
    /* Other flags: PREFETCH, RESET, and BIST */
    hdr->prdtl = size_to_prdtl(cmd->xbytes, cmd->prd_size);
    hdr->prdbc = 0;
    hdr->ctba = 0;
}

static void command_table_init(AHCICommand *cmd)
{
    RegH2DFIS *fis = &(cmd->fis);
    uint16_t sect_count = (cmd->xbytes / cmd->sector_size);

    fis->fis_type = REG_H2D_FIS;
    fis->flags = REG_H2D_FIS_CMD; /* "Command" bit */
    fis->command = cmd->name;

    if (cmd->props->ncq) {
        NCQFIS *ncqfis = (NCQFIS *)fis;
        /* NCQ is weird and re-uses FIS frames for unrelated data.
         * See SATA 3.2, 13.6.4.1 READ FPDMA QUEUED for an example. */
        ncqfis->sector_low = sect_count & 0xFF;
        ncqfis->sector_hi = (sect_count >> 8) & 0xFF;
        ncqfis->device = NCQ_DEVICE_MAGIC;
        /* Force Unit Access is bit 7 in the device register */
        ncqfis->tag = 0;  /* bits 3-7 are the NCQ tag */
        ncqfis->prio = 0; /* bits 6,7 are a prio tag */
        /* RARC bit is bit 0 of TAG field */
    } else {
        fis->feature_low = 0x00;
        fis->feature_high = 0x00;
        if (cmd->props->lba28 || cmd->props->lba48) {
            fis->device = ATA_DEVICE_LBA;
        }
        fis->count = (cmd->xbytes / cmd->sector_size);
    }
    fis->icc = 0x00;
    fis->control = 0x00;
    memset(fis->aux, 0x00, ARRAY_SIZE(fis->aux));
}

void ahci_command_enable_atapi_dma(AHCICommand *cmd)
{
    RegH2DFIS *fis = &(cmd->fis);
    g_assert(cmd->props->atapi);
    fis->feature_low |= 0x01;
    /* PIO is still used to transfer the ATAPI command */
    g_assert(cmd->props->pio);
    cmd->props->dma = true;
    /* BUG: We expect the DMA Setup interrupt for DMA commands */
    /* cmd->interrupts |= AHCI_PX_IS_DSS; */
}

AHCICommand *ahci_command_create(uint8_t command_name)
{
    AHCICommandProp *props = ahci_command_find(command_name);
    AHCICommand *cmd;

    g_assert(props);
    cmd = g_new0(AHCICommand, 1);
    g_assert(!(props->dma && props->pio) || props->atapi);
    g_assert(!(props->lba28 && props->lba48));
    g_assert(!(props->read && props->write));
    g_assert(!props->size || props->data);
    g_assert(!props->ncq || props->lba48);

    /* Defaults and book-keeping */
    cmd->props = g_memdup(props, sizeof(AHCICommandProp));
    cmd->name = command_name;
    cmd->xbytes = props->size;
    cmd->prd_size = 4096;
    cmd->buffer = 0xabad1dea;
    cmd->sector_size = props->atapi ? ATAPI_SECTOR_SIZE : AHCI_SECTOR_SIZE;

    if (!cmd->props->ncq) {
        cmd->interrupts = AHCI_PX_IS_DHRS;
    }
    /* BUG: We expect the DPS interrupt for data commands */
    /* cmd->interrupts |= props->data ? AHCI_PX_IS_DPS : 0; */
    /* BUG: We expect the DMA Setup interrupt for DMA commands */
    /* cmd->interrupts |= props->dma ? AHCI_PX_IS_DSS : 0; */
    cmd->interrupts |= props->ncq ? AHCI_PX_IS_SDBS : 0;

    command_header_init(cmd);
    command_table_init(cmd);

    return cmd;
}

AHCICommand *ahci_atapi_command_create(uint8_t scsi_cmd, uint16_t bcl, bool dma)
{
    AHCICommand *cmd = ahci_command_create(CMD_PACKET);
    cmd->atapi_cmd = g_malloc0(16);
    cmd->atapi_cmd[0] = scsi_cmd;
    stw_le_p(&cmd->fis.lba_lo[1], bcl);
    if (dma) {
        ahci_command_enable_atapi_dma(cmd);
    } else {
        cmd->interrupts |= bcl ? AHCI_PX_IS_PSS : 0;
    }
    return cmd;
}

void ahci_atapi_test_ready(AHCIQState *ahci, uint8_t port,
                           bool ready, uint8_t expected_sense)
{
    AHCICommand *cmd = ahci_atapi_command_create(CMD_ATAPI_TEST_UNIT_READY, 0, false);
    ahci_command_set_size(cmd, 0);
    if (!ready) {
        cmd->interrupts |= AHCI_PX_IS_TFES;
        cmd->errors |= expected_sense << 4;
    }
    ahci_command_commit(ahci, cmd, port);
    ahci_command_issue(ahci, cmd);
    ahci_command_verify(ahci, cmd);
    ahci_command_free(cmd);
}

static int copy_buffer(AHCIQState *ahci, AHCICommand *cmd,
                        const AHCIOpts *opts)
{
    unsigned char *rx = opts->opaque;
    qtest_bufread(ahci->parent->qts, opts->buffer, rx, opts->size);
    return 0;
}

void ahci_atapi_get_sense(AHCIQState *ahci, uint8_t port,
                          uint8_t *sense, uint8_t *asc)
{
    unsigned char *rx;
    AHCIOpts opts = {
        .size = 18,
        .atapi = true,
        .post_cb = copy_buffer,
    };
    rx = g_malloc(18);
    opts.opaque = rx;

    ahci_exec(ahci, port, CMD_ATAPI_REQUEST_SENSE, &opts);

    *sense = rx[2];
    *asc = rx[12];

    g_free(rx);
}

void ahci_atapi_eject(AHCIQState *ahci, uint8_t port)
{
    AHCICommand *cmd = ahci_atapi_command_create(CMD_ATAPI_START_STOP_UNIT, 0, false);
    ahci_command_set_size(cmd, 0);

    cmd->atapi_cmd[4] = 0x02; /* loej = true */
    ahci_command_commit(ahci, cmd, port);
    ahci_command_issue(ahci, cmd);
    ahci_command_verify(ahci, cmd);
    ahci_command_free(cmd);
}

void ahci_atapi_load(AHCIQState *ahci, uint8_t port)
{
    AHCICommand *cmd = ahci_atapi_command_create(CMD_ATAPI_START_STOP_UNIT, 0, false);
    ahci_command_set_size(cmd, 0);

    cmd->atapi_cmd[4] = 0x03; /* loej,start = true */
    ahci_command_commit(ahci, cmd, port);
    ahci_command_issue(ahci, cmd);
    ahci_command_verify(ahci, cmd);
    ahci_command_free(cmd);
}

void ahci_command_free(AHCICommand *cmd)
{
    g_free(cmd->atapi_cmd);
    g_free(cmd->props);
    g_free(cmd);
}

void ahci_command_set_flags(AHCICommand *cmd, uint16_t cmdh_flags)
{
    cmd->header.flags |= cmdh_flags;
}

void ahci_command_clr_flags(AHCICommand *cmd, uint16_t cmdh_flags)
{
    cmd->header.flags &= ~cmdh_flags;
}

static void ahci_atapi_command_set_offset(AHCICommand *cmd, uint64_t lba)
{
    unsigned char *cbd = cmd->atapi_cmd;
    g_assert(cbd);

    switch (cbd[0]) {
    case CMD_ATAPI_READ_10:
    case CMD_ATAPI_READ_CD:
        g_assert_cmpuint(lba, <=, UINT32_MAX);
        stl_be_p(&cbd[2], lba);
        break;
    case CMD_ATAPI_REQUEST_SENSE:
    case CMD_ATAPI_TEST_UNIT_READY:
    case CMD_ATAPI_START_STOP_UNIT:
        g_assert_cmpuint(lba, ==, 0x00);
        break;
    default:
        /* SCSI doesn't have uniform packet formats,
         * so you have to add support for it manually. Sorry! */
        fprintf(stderr, "The Libqos AHCI driver does not support the "
                "set_offset operation for ATAPI command 0x%02x, "
                "please add support.\n",
                cbd[0]);
        g_assert_not_reached();
    }
}

void ahci_command_set_offset(AHCICommand *cmd, uint64_t lba_sect)
{
    RegH2DFIS *fis = &(cmd->fis);

    if (cmd->props->atapi) {
        ahci_atapi_command_set_offset(cmd, lba_sect);
        return;
    } else if (!cmd->props->data && !lba_sect) {
        /* Not meaningful, ignore. */
        return;
    } else if (cmd->props->lba28) {
        g_assert_cmphex(lba_sect, <=, 0xFFFFFFF);
    } else if (cmd->props->lba48 || cmd->props->ncq) {
        g_assert_cmphex(lba_sect, <=, 0xFFFFFFFFFFFF);
    } else {
        /* Can't set offset if we don't know the format. */
        g_assert_not_reached();
    }

    /* LBA28 uses the low nibble of the device/control register for LBA24:27 */
    fis->lba_lo[0] = (lba_sect & 0xFF);
    fis->lba_lo[1] = (lba_sect >> 8) & 0xFF;
    fis->lba_lo[2] = (lba_sect >> 16) & 0xFF;
    if (cmd->props->lba28) {
        fis->device = (fis->device & 0xF0) | ((lba_sect >> 24) & 0x0F);
    }
    fis->lba_hi[0] = (lba_sect >> 24) & 0xFF;
    fis->lba_hi[1] = (lba_sect >> 32) & 0xFF;
    fis->lba_hi[2] = (lba_sect >> 40) & 0xFF;
}

void ahci_command_set_buffer(AHCICommand *cmd, uint64_t buffer)
{
    cmd->buffer = buffer;
}

static void ahci_atapi_set_size(AHCICommand *cmd, uint64_t xbytes)
{
    unsigned char *cbd = cmd->atapi_cmd;
    uint64_t nsectors = xbytes / ATAPI_SECTOR_SIZE;
    uint32_t tmp;
    g_assert(cbd);

    switch (cbd[0]) {
    case CMD_ATAPI_READ_10:
        g_assert_cmpuint(nsectors, <=, UINT16_MAX);
        stw_be_p(&cbd[7], nsectors);
        break;
    case CMD_ATAPI_READ_CD:
        /* 24bit BE store */
        g_assert_cmpuint(nsectors, <, 1ULL << 24);
        tmp = nsectors;
        cbd[6] = (tmp & 0xFF0000) >> 16;
        cbd[7] = (tmp & 0xFF00) >> 8;
        cbd[8] = (tmp & 0xFF);
        break;
    case CMD_ATAPI_REQUEST_SENSE:
        g_assert_cmpuint(xbytes, <=, UINT8_MAX);
        cbd[4] = (uint8_t)xbytes;
        break;
    case CMD_ATAPI_TEST_UNIT_READY:
    case CMD_ATAPI_START_STOP_UNIT:
        g_assert_cmpuint(xbytes, ==, 0);
        break;
    default:
        /* SCSI doesn't have uniform packet formats,
         * so you have to add support for it manually. Sorry! */
        fprintf(stderr, "The Libqos AHCI driver does not support the set_size "
                "operation for ATAPI command 0x%02x, please add support.\n",
                cbd[0]);
        g_assert_not_reached();
    }
}

void ahci_command_set_sizes(AHCICommand *cmd, uint64_t xbytes,
                            unsigned prd_size)
{
    uint16_t sect_count;

    /* Each PRD can describe up to 4MiB, and must not be odd. */
    g_assert_cmphex(prd_size, <=, 4096 * 1024);
    g_assert_cmphex(prd_size & 0x01, ==, 0x00);
    if (prd_size) {
        cmd->prd_size = prd_size;
    }
    cmd->xbytes = xbytes;
    sect_count = (cmd->xbytes / cmd->sector_size);

    if (cmd->props->ncq) {
        NCQFIS *nfis = (NCQFIS *)&(cmd->fis);
        nfis->sector_low = sect_count & 0xFF;
        nfis->sector_hi = (sect_count >> 8) & 0xFF;
    } else if (cmd->props->atapi) {
        ahci_atapi_set_size(cmd, xbytes);
    } else {
        /* For writes, the PIO Setup FIS interrupt only comes from DRQs
         * after the first.
         */
        if (cmd->props->pio && sect_count > (cmd->props->read ? 0 : 1)) {
            cmd->interrupts |= AHCI_PX_IS_PSS;
        }
        cmd->fis.count = sect_count;
    }
    cmd->header.prdtl = size_to_prdtl(cmd->xbytes, cmd->prd_size);
}

void ahci_command_set_size(AHCICommand *cmd, uint64_t xbytes)
{
    ahci_command_set_sizes(cmd, xbytes, cmd->prd_size);
}

void ahci_command_set_prd_size(AHCICommand *cmd, unsigned prd_size)
{
    ahci_command_set_sizes(cmd, cmd->xbytes, prd_size);
}

void ahci_command_adjust(AHCICommand *cmd, uint64_t offset, uint64_t buffer,
                         uint64_t xbytes, unsigned prd_size)
{
    ahci_command_set_sizes(cmd, xbytes, prd_size);
    ahci_command_set_buffer(cmd, buffer);
    ahci_command_set_offset(cmd, offset);
}

void ahci_command_commit(AHCIQState *ahci, AHCICommand *cmd, uint8_t port)
{
    uint16_t i, prdtl;
    uint64_t table_size, table_ptr, remaining;
    PRD prd;

    /* This command is now tied to this port/command slot */
    cmd->port = port;
    cmd->slot = ahci_pick_cmd(ahci, port);

    if (cmd->props->ncq) {
        NCQFIS *nfis = (NCQFIS *)&cmd->fis;
        nfis->tag = (cmd->slot << 3) & 0xFC;
    }

    /* Create a buffer for the command table */
    prdtl = size_to_prdtl(cmd->xbytes, cmd->prd_size);
    table_size = CMD_TBL_SIZ(prdtl);
    table_ptr = ahci_alloc(ahci, table_size);
    g_assert(table_ptr);
    /* AHCI 1.3: Must be aligned to 0x80 */
    g_assert((table_ptr & 0x7F) == 0x00);
    cmd->header.ctba = table_ptr;

    /* Commit the command header (part of the Command List Buffer) */
    ahci_set_command_header(ahci, port, cmd->slot, &(cmd->header));
    /* Now, write the command table (FIS, ACMD, and PRDT) -- FIS first, */
    ahci_write_fis(ahci, cmd);
    /* Then ATAPI CMD, if needed */
    if (cmd->props->atapi) {
        qtest_memwrite(ahci->parent->qts, table_ptr + 0x40, cmd->atapi_cmd, 16);
    }

    /* Construct and write the PRDs to the command table */
    g_assert_cmphex(prdtl, ==, cmd->header.prdtl);
    remaining = cmd->xbytes;
    for (i = 0; i < prdtl; ++i) {
        prd.dba = cpu_to_le64(cmd->buffer + (cmd->prd_size * i));
        prd.res = 0;
        if (remaining > cmd->prd_size) {
            /* Note that byte count is 0-based. */
            prd.dbc = cpu_to_le32(cmd->prd_size - 1);
            remaining -= cmd->prd_size;
        } else {
            /* Again, dbc is 0-based. */
            prd.dbc = cpu_to_le32(remaining - 1);
            remaining = 0;
        }
        prd.dbc |= cpu_to_le32(0x80000000); /* Request DPS Interrupt */

        /* Commit the PRD entry to the Command Table */
        qtest_memwrite(ahci->parent->qts, table_ptr + 0x80 + (i * sizeof(PRD)),
                       &prd, sizeof(PRD));
    }

    /* Bookmark the PRDTL and CTBA values */
    ahci->port[port].ctba[cmd->slot] = table_ptr;
    ahci->port[port].prdtl[cmd->slot] = prdtl;
}

void ahci_command_issue_async(AHCIQState *ahci, AHCICommand *cmd)
{
    if (cmd->props->ncq) {
        ahci_px_wreg(ahci, cmd->port, AHCI_PX_SACT, (1 << cmd->slot));
    }

    ahci_px_wreg(ahci, cmd->port, AHCI_PX_CI, (1 << cmd->slot));
}

void ahci_command_wait(AHCIQState *ahci, AHCICommand *cmd)
{
    /* We can't rely on STS_BSY until the command has started processing.
     * Therefore, we also use the Command Issue bit as indication of
     * a command in-flight. */

#define RSET(REG, MASK) (BITSET(ahci_px_rreg(ahci, cmd->port, (REG)), (MASK)))

    while (RSET(AHCI_PX_TFD, AHCI_PX_TFD_STS_BSY) ||
           RSET(AHCI_PX_CI, 1 << cmd->slot) ||
           (cmd->props->ncq && RSET(AHCI_PX_SACT, 1 << cmd->slot))) {
        usleep(50);
    }

}

void ahci_command_issue(AHCIQState *ahci, AHCICommand *cmd)
{
    ahci_command_issue_async(ahci, cmd);
    ahci_command_wait(ahci, cmd);
}

void ahci_command_verify(AHCIQState *ahci, AHCICommand *cmd)
{
    uint8_t slot = cmd->slot;
    uint8_t port = cmd->port;

    ahci_port_check_error(ahci, port, cmd->interrupts, cmd->errors);
    ahci_port_check_interrupts(ahci, port, cmd->interrupts);
    ahci_port_check_nonbusy(ahci, port, slot);
    ahci_port_check_cmd_sanity(ahci, cmd);
    if (cmd->interrupts & AHCI_PX_IS_DHRS) {
        ahci_port_check_d2h_sanity(ahci, port, slot);
    }
    if (cmd->props->pio) {
        ahci_port_check_pio_sanity(ahci, cmd);
    }
}

uint8_t ahci_command_slot(AHCICommand *cmd)
{
    return cmd->slot;
}
