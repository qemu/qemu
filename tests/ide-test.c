/*
 * IDE test cases
 *
 * Copyright (c) 2013 Kevin Wolf <kwolf@redhat.com>
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
#include "libqos/libqos.h"
#include "libqos/pci-pc.h"
#include "libqos/malloc-pc.h"

#include "qemu-common.h"
#include "qemu/bswap.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"

#define TEST_IMAGE_SIZE 64 * 1024 * 1024

#define IDE_PCI_DEV     1
#define IDE_PCI_FUNC    1

#define IDE_BASE 0x1f0
#define IDE_PRIMARY_IRQ 14

#define ATAPI_BLOCK_SIZE 2048

/* How many bytes to receive via ATAPI PIO at one time.
 * Must be less than 0xFFFF. */
#define BYTE_COUNT_LIMIT 5120

enum {
    reg_data        = 0x0,
    reg_feature     = 0x1,
    reg_nsectors    = 0x2,
    reg_lba_low     = 0x3,
    reg_lba_middle  = 0x4,
    reg_lba_high    = 0x5,
    reg_device      = 0x6,
    reg_status      = 0x7,
    reg_command     = 0x7,
};

enum {
    BSY     = 0x80,
    DRDY    = 0x40,
    DF      = 0x20,
    DRQ     = 0x08,
    ERR     = 0x01,
};

enum {
    DEV     = 0x10,
    LBA     = 0x40,
};

enum {
    bmreg_cmd       = 0x0,
    bmreg_status    = 0x2,
    bmreg_prdt      = 0x4,
};

enum {
    CMD_READ_DMA    = 0xc8,
    CMD_WRITE_DMA   = 0xca,
    CMD_FLUSH_CACHE = 0xe7,
    CMD_IDENTIFY    = 0xec,
    CMD_PACKET      = 0xa0,

    CMDF_ABORT      = 0x100,
    CMDF_NO_BM      = 0x200,
};

enum {
    BM_CMD_START    =  0x1,
    BM_CMD_WRITE    =  0x8, /* write = from device to memory */
};

enum {
    BM_STS_ACTIVE   =  0x1,
    BM_STS_ERROR    =  0x2,
    BM_STS_INTR     =  0x4,
};

enum {
    PRDT_EOT        = 0x80000000,
};

#define assert_bit_set(data, mask) g_assert_cmphex((data) & (mask), ==, (mask))
#define assert_bit_clear(data, mask) g_assert_cmphex((data) & (mask), ==, 0)

static QPCIBus *pcibus = NULL;
static QGuestAllocator *guest_malloc;

static char tmp_path[] = "/tmp/qtest.XXXXXX";
static char debug_path[] = "/tmp/qtest-blkdebug.XXXXXX";

static void ide_test_start(const char *cmdline_fmt, ...)
{
    va_list ap;
    char *cmdline;

    va_start(ap, cmdline_fmt);
    cmdline = g_strdup_vprintf(cmdline_fmt, ap);
    va_end(ap);

    qtest_start(cmdline);
    guest_malloc = pc_alloc_init();

    g_free(cmdline);
}

static void ide_test_quit(void)
{
    pc_alloc_uninit(guest_malloc);
    guest_malloc = NULL;
    qtest_end();
}

static QPCIDevice *get_pci_device(QPCIBar *bmdma_bar, QPCIBar *ide_bar)
{
    QPCIDevice *dev;
    uint16_t vendor_id, device_id;

    if (!pcibus) {
        pcibus = qpci_init_pc(NULL);
    }

    /* Find PCI device and verify it's the right one */
    dev = qpci_device_find(pcibus, QPCI_DEVFN(IDE_PCI_DEV, IDE_PCI_FUNC));
    g_assert(dev != NULL);

    vendor_id = qpci_config_readw(dev, PCI_VENDOR_ID);
    device_id = qpci_config_readw(dev, PCI_DEVICE_ID);
    g_assert(vendor_id == PCI_VENDOR_ID_INTEL);
    g_assert(device_id == PCI_DEVICE_ID_INTEL_82371SB_1);

    /* Map bmdma BAR */
    *bmdma_bar = qpci_iomap(dev, 4, NULL);

    *ide_bar = qpci_legacy_iomap(dev, IDE_BASE);

    qpci_device_enable(dev);

    return dev;
}

static void free_pci_device(QPCIDevice *dev)
{
    /* libqos doesn't have a function for this, so free it manually */
    g_free(dev);
}

typedef struct PrdtEntry {
    uint32_t addr;
    uint32_t size;
} QEMU_PACKED PrdtEntry;

#define assert_bit_set(data, mask) g_assert_cmphex((data) & (mask), ==, (mask))
#define assert_bit_clear(data, mask) g_assert_cmphex((data) & (mask), ==, 0)

static int send_dma_request(int cmd, uint64_t sector, int nb_sectors,
                            PrdtEntry *prdt, int prdt_entries,
                            void(*post_exec)(QPCIDevice *dev, QPCIBar ide_bar,
                                             uint64_t sector, int nb_sectors))
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uintptr_t guest_prdt;
    size_t len;
    bool from_dev;
    uint8_t status;
    int flags;

    dev = get_pci_device(&bmdma_bar, &ide_bar);

    flags = cmd & ~0xff;
    cmd &= 0xff;

    switch (cmd) {
    case CMD_READ_DMA:
    case CMD_PACKET:
        /* Assuming we only test data reads w/ ATAPI, otherwise we need to know
         * the SCSI command being sent in the packet, too. */
        from_dev = true;
        break;
    case CMD_WRITE_DMA:
        from_dev = false;
        break;
    default:
        g_assert_not_reached();
    }

    if (flags & CMDF_NO_BM) {
        qpci_config_writew(dev, PCI_COMMAND,
                           PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
    }

    /* Select device 0 */
    qpci_io_writeb(dev, ide_bar, reg_device, 0 | LBA);

    /* Stop any running transfer, clear any pending interrupt */
    qpci_io_writeb(dev, bmdma_bar, bmreg_cmd, 0);
    qpci_io_writeb(dev, bmdma_bar, bmreg_status, BM_STS_INTR);

    /* Setup PRDT */
    len = sizeof(*prdt) * prdt_entries;
    guest_prdt = guest_alloc(guest_malloc, len);
    memwrite(guest_prdt, prdt, len);
    qpci_io_writel(dev, bmdma_bar, bmreg_prdt, guest_prdt);

    /* ATA DMA command */
    if (cmd == CMD_PACKET) {
        /* Enables ATAPI DMA; otherwise PIO is attempted */
        qpci_io_writeb(dev, ide_bar, reg_feature, 0x01);
    } else {
        qpci_io_writeb(dev, ide_bar, reg_nsectors, nb_sectors);
        qpci_io_writeb(dev, ide_bar, reg_lba_low,    sector & 0xff);
        qpci_io_writeb(dev, ide_bar, reg_lba_middle, (sector >> 8) & 0xff);
        qpci_io_writeb(dev, ide_bar, reg_lba_high,   (sector >> 16) & 0xff);
    }

    qpci_io_writeb(dev, ide_bar, reg_command, cmd);

    if (post_exec) {
        post_exec(dev, ide_bar, sector, nb_sectors);
    }

    /* Start DMA transfer */
    qpci_io_writeb(dev, bmdma_bar, bmreg_cmd,
                   BM_CMD_START | (from_dev ? BM_CMD_WRITE : 0));

    if (flags & CMDF_ABORT) {
        qpci_io_writeb(dev, bmdma_bar, bmreg_cmd, 0);
    }

    /* Wait for the DMA transfer to complete */
    do {
        status = qpci_io_readb(dev, bmdma_bar, bmreg_status);
    } while ((status & (BM_STS_ACTIVE | BM_STS_INTR)) == BM_STS_ACTIVE);

    g_assert_cmpint(get_irq(IDE_PRIMARY_IRQ), ==, !!(status & BM_STS_INTR));

    /* Check IDE status code */
    assert_bit_set(qpci_io_readb(dev, ide_bar, reg_status), DRDY);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), BSY | DRQ);

    /* Reading the status register clears the IRQ */
    g_assert(!get_irq(IDE_PRIMARY_IRQ));

    /* Stop DMA transfer if still active */
    if (status & BM_STS_ACTIVE) {
        qpci_io_writeb(dev, bmdma_bar, bmreg_cmd, 0);
    }

    free_pci_device(dev);

    return status;
}

static void test_bmdma_simple_rw(void)
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t status;
    uint8_t *buf;
    uint8_t *cmpbuf;
    size_t len = 512;
    uintptr_t guest_buf = guest_alloc(guest_malloc, len);

    PrdtEntry prdt[] = {
        {
            .addr = cpu_to_le32(guest_buf),
            .size = cpu_to_le32(len | PRDT_EOT),
        },
    };

    dev = get_pci_device(&bmdma_bar, &ide_bar);

    buf = g_malloc(len);
    cmpbuf = g_malloc(len);

    /* Write 0x55 pattern to sector 0 */
    memset(buf, 0x55, len);
    memwrite(guest_buf, buf, len);

    status = send_dma_request(CMD_WRITE_DMA, 0, 1, prdt,
                              ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

    /* Write 0xaa pattern to sector 1 */
    memset(buf, 0xaa, len);
    memwrite(guest_buf, buf, len);

    status = send_dma_request(CMD_WRITE_DMA, 1, 1, prdt,
                              ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

    /* Read and verify 0x55 pattern in sector 0 */
    memset(cmpbuf, 0x55, len);

    status = send_dma_request(CMD_READ_DMA, 0, 1, prdt, ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

    memread(guest_buf, buf, len);
    g_assert(memcmp(buf, cmpbuf, len) == 0);

    /* Read and verify 0xaa pattern in sector 1 */
    memset(cmpbuf, 0xaa, len);

    status = send_dma_request(CMD_READ_DMA, 1, 1, prdt, ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

    memread(guest_buf, buf, len);
    g_assert(memcmp(buf, cmpbuf, len) == 0);


    free_pci_device(dev);
    g_free(buf);
    g_free(cmpbuf);
}

static void test_bmdma_short_prdt(void)
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t status;

    PrdtEntry prdt[] = {
        {
            .addr = 0,
            .size = cpu_to_le32(0x10 | PRDT_EOT),
        },
    };

    dev = get_pci_device(&bmdma_bar, &ide_bar);

    /* Normal request */
    status = send_dma_request(CMD_READ_DMA, 0, 1,
                              prdt, ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, 0);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

    /* Abort the request before it completes */
    status = send_dma_request(CMD_READ_DMA | CMDF_ABORT, 0, 1,
                              prdt, ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, 0);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);
    free_pci_device(dev);
}

static void test_bmdma_one_sector_short_prdt(void)
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t status;

    /* Read 2 sectors but only give 1 sector in PRDT */
    PrdtEntry prdt[] = {
        {
            .addr = 0,
            .size = cpu_to_le32(0x200 | PRDT_EOT),
        },
    };

    dev = get_pci_device(&bmdma_bar, &ide_bar);

    /* Normal request */
    status = send_dma_request(CMD_READ_DMA, 0, 2,
                              prdt, ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, 0);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

    /* Abort the request before it completes */
    status = send_dma_request(CMD_READ_DMA | CMDF_ABORT, 0, 2,
                              prdt, ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, 0);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);
    free_pci_device(dev);
}

static void test_bmdma_long_prdt(void)
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t status;

    PrdtEntry prdt[] = {
        {
            .addr = 0,
            .size = cpu_to_le32(0x1000 | PRDT_EOT),
        },
    };

    dev = get_pci_device(&bmdma_bar, &ide_bar);

    /* Normal request */
    status = send_dma_request(CMD_READ_DMA, 0, 1,
                              prdt, ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, BM_STS_ACTIVE | BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

    /* Abort the request before it completes */
    status = send_dma_request(CMD_READ_DMA | CMDF_ABORT, 0, 1,
                              prdt, ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);
    free_pci_device(dev);
}

static void test_bmdma_no_busmaster(void)
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t status;

    dev = get_pci_device(&bmdma_bar, &ide_bar);

    /* No PRDT_EOT, each entry addr 0/size 64k, and in theory qemu shouldn't be
     * able to access it anyway because the Bus Master bit in the PCI command
     * register isn't set. This is complete nonsense, but it used to be pretty
     * good at confusing and occasionally crashing qemu. */
    PrdtEntry prdt[4096] = { };

    status = send_dma_request(CMD_READ_DMA | CMDF_NO_BM, 0, 512,
                              prdt, ARRAY_SIZE(prdt), NULL);

    /* Not entirely clear what the expected result is, but this is what we get
     * in practice. At least we want to be aware of any changes. */
    g_assert_cmphex(status, ==, BM_STS_ACTIVE | BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);
    free_pci_device(dev);
}

static void test_bmdma_setup(void)
{
    ide_test_start(
        "-drive file=%s,if=ide,serial=%s,cache=writeback,format=raw "
        "-global ide-hd.ver=%s",
        tmp_path, "testdisk", "version");
    qtest_irq_intercept_in(global_qtest, "ioapic");
}

static void test_bmdma_teardown(void)
{
    ide_test_quit();
}

static void string_cpu_to_be16(uint16_t *s, size_t bytes)
{
    g_assert((bytes & 1) == 0);
    bytes /= 2;

    while (bytes--) {
        *s = cpu_to_be16(*s);
        s++;
    }
}

static void test_identify(void)
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t data;
    uint16_t buf[256];
    int i;
    int ret;

    ide_test_start(
        "-drive file=%s,if=ide,serial=%s,cache=writeback,format=raw "
        "-global ide-hd.ver=%s",
        tmp_path, "testdisk", "version");

    dev = get_pci_device(&bmdma_bar, &ide_bar);

    /* IDENTIFY command on device 0*/
    qpci_io_writeb(dev, ide_bar, reg_device, 0);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_IDENTIFY);

    /* Read in the IDENTIFY buffer and check registers */
    data = qpci_io_readb(dev, ide_bar, reg_device);
    g_assert_cmpint(data & DEV, ==, 0);

    for (i = 0; i < 256; i++) {
        data = qpci_io_readb(dev, ide_bar, reg_status);
        assert_bit_set(data, DRDY | DRQ);
        assert_bit_clear(data, BSY | DF | ERR);

        buf[i] = qpci_io_readw(dev, ide_bar, reg_data);
    }

    data = qpci_io_readb(dev, ide_bar, reg_status);
    assert_bit_set(data, DRDY);
    assert_bit_clear(data, BSY | DF | ERR | DRQ);

    /* Check serial number/version in the buffer */
    string_cpu_to_be16(&buf[10], 20);
    ret = memcmp(&buf[10], "testdisk            ", 20);
    g_assert(ret == 0);

    string_cpu_to_be16(&buf[23], 8);
    ret = memcmp(&buf[23], "version ", 8);
    g_assert(ret == 0);

    /* Write cache enabled bit */
    assert_bit_set(buf[85], 0x20);

    ide_test_quit();
    free_pci_device(dev);
}

/*
 * Write sector 1 with random data to make IDE storage dirty
 * Needed for flush tests so that flushes actually go though the block layer
 */
static void make_dirty(uint8_t device)
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t status;
    size_t len = 512;
    uintptr_t guest_buf;
    void* buf;

    dev = get_pci_device(&bmdma_bar, &ide_bar);

    guest_buf = guest_alloc(guest_malloc, len);
    buf = g_malloc(len);
    memset(buf, rand() % 255 + 1, len);
    g_assert(guest_buf);
    g_assert(buf);

    memwrite(guest_buf, buf, len);

    PrdtEntry prdt[] = {
        {
            .addr = cpu_to_le32(guest_buf),
            .size = cpu_to_le32(len | PRDT_EOT),
        },
    };

    status = send_dma_request(CMD_WRITE_DMA, 1, 1, prdt,
                              ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

    g_free(buf);
    free_pci_device(dev);
}

static void test_flush(void)
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t data;

    ide_test_start(
        "-drive file=blkdebug::%s,if=ide,cache=writeback,format=raw",
        tmp_path);

    dev = get_pci_device(&bmdma_bar, &ide_bar);

    qtest_irq_intercept_in(global_qtest, "ioapic");

    /* Dirty media so that CMD_FLUSH_CACHE will actually go to disk */
    make_dirty(0);

    /* Delay the completion of the flush request until we explicitly do it */
    g_free(hmp("qemu-io ide0-hd0 \"break flush_to_os A\""));

    /* FLUSH CACHE command on device 0*/
    qpci_io_writeb(dev, ide_bar, reg_device, 0);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_FLUSH_CACHE);

    /* Check status while request is in flight*/
    data = qpci_io_readb(dev, ide_bar, reg_status);
    assert_bit_set(data, BSY | DRDY);
    assert_bit_clear(data, DF | ERR | DRQ);

    /* Complete the command */
    g_free(hmp("qemu-io ide0-hd0 \"resume A\""));

    /* Check registers */
    data = qpci_io_readb(dev, ide_bar, reg_device);
    g_assert_cmpint(data & DEV, ==, 0);

    do {
        data = qpci_io_readb(dev, ide_bar, reg_status);
    } while (data & BSY);

    assert_bit_set(data, DRDY);
    assert_bit_clear(data, BSY | DF | ERR | DRQ);

    ide_test_quit();
    free_pci_device(dev);
}

static void test_retry_flush(const char *machine)
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t data;
    const char *s;

    prepare_blkdebug_script(debug_path, "flush_to_disk");

    ide_test_start(
        "-drive file=blkdebug:%s:%s,if=ide,cache=writeback,format=raw,"
        "rerror=stop,werror=stop",
        debug_path, tmp_path);

    dev = get_pci_device(&bmdma_bar, &ide_bar);

    qtest_irq_intercept_in(global_qtest, "ioapic");

    /* Dirty media so that CMD_FLUSH_CACHE will actually go to disk */
    make_dirty(0);

    /* FLUSH CACHE command on device 0*/
    qpci_io_writeb(dev, ide_bar, reg_device, 0);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_FLUSH_CACHE);

    /* Check status while request is in flight*/
    data = qpci_io_readb(dev, ide_bar, reg_status);
    assert_bit_set(data, BSY | DRDY);
    assert_bit_clear(data, DF | ERR | DRQ);

    qmp_eventwait("STOP");

    /* Complete the command */
    s = "{'execute':'cont' }";
    qmp_discard_response(s);

    /* Check registers */
    data = qpci_io_readb(dev, ide_bar, reg_device);
    g_assert_cmpint(data & DEV, ==, 0);

    do {
        data = qpci_io_readb(dev, ide_bar, reg_status);
    } while (data & BSY);

    assert_bit_set(data, DRDY);
    assert_bit_clear(data, BSY | DF | ERR | DRQ);

    ide_test_quit();
    free_pci_device(dev);
}

static void test_flush_nodev(void)
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;

    ide_test_start("");

    dev = get_pci_device(&bmdma_bar, &ide_bar);

    /* FLUSH CACHE command on device 0*/
    qpci_io_writeb(dev, ide_bar, reg_device, 0);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_FLUSH_CACHE);

    /* Just testing that qemu doesn't crash... */

    free_pci_device(dev);
    ide_test_quit();
}

static void test_flush_empty_drive(void)
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;

    ide_test_start("-device ide-cd,bus=ide.0");
    dev = get_pci_device(&bmdma_bar, &ide_bar);

    /* FLUSH CACHE command on device 0 */
    qpci_io_writeb(dev, ide_bar, reg_device, 0);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_FLUSH_CACHE);

    /* Just testing that qemu doesn't crash... */

    free_pci_device(dev);
    ide_test_quit();
}

static void test_pci_retry_flush(void)
{
    test_retry_flush("pc");
}

static void test_isa_retry_flush(void)
{
    test_retry_flush("isapc");
}

typedef struct Read10CDB {
    uint8_t opcode;
    uint8_t flags;
    uint32_t lba;
    uint8_t reserved;
    uint16_t nblocks;
    uint8_t control;
    uint16_t padding;
} __attribute__((__packed__)) Read10CDB;

static void send_scsi_cdb_read10(QPCIDevice *dev, QPCIBar ide_bar,
                                 uint64_t lba, int nblocks)
{
    Read10CDB pkt = { .padding = 0 };
    int i;

    g_assert_cmpint(lba, <=, UINT32_MAX);
    g_assert_cmpint(nblocks, <=, UINT16_MAX);
    g_assert_cmpint(nblocks, >=, 0);

    /* Construct SCSI CDB packet */
    pkt.opcode = 0x28;
    pkt.lba = cpu_to_be32(lba);
    pkt.nblocks = cpu_to_be16(nblocks);

    /* Send Packet */
    for (i = 0; i < sizeof(Read10CDB)/2; i++) {
        qpci_io_writew(dev, ide_bar, reg_data,
                       le16_to_cpu(((uint16_t *)&pkt)[i]));
    }
}

static void nsleep(int64_t nsecs)
{
    const struct timespec val = { .tv_nsec = nsecs };
    nanosleep(&val, NULL);
    clock_set(nsecs);
}

static uint8_t ide_wait_clear(uint8_t flag)
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t data;
    time_t st;

    dev = get_pci_device(&bmdma_bar, &ide_bar);

    /* Wait with a 5 second timeout */
    time(&st);
    while (true) {
        data = qpci_io_readb(dev, ide_bar, reg_status);
        if (!(data & flag)) {
            free_pci_device(dev);
            return data;
        }
        if (difftime(time(NULL), st) > 5.0) {
            break;
        }
        nsleep(400);
    }
    g_assert_not_reached();
}

static void ide_wait_intr(int irq)
{
    time_t st;
    bool intr;

    time(&st);
    while (true) {
        intr = get_irq(irq);
        if (intr) {
            return;
        }
        if (difftime(time(NULL), st) > 5.0) {
            break;
        }
        nsleep(400);
    }

    g_assert_not_reached();
}

static void cdrom_pio_impl(int nblocks)
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    FILE *fh;
    int patt_blocks = MAX(16, nblocks);
    size_t patt_len = ATAPI_BLOCK_SIZE * patt_blocks;
    char *pattern = g_malloc(patt_len);
    size_t rxsize = ATAPI_BLOCK_SIZE * nblocks;
    uint16_t *rx = g_malloc0(rxsize);
    int i, j;
    uint8_t data;
    uint16_t limit;
    size_t ret;

    /* Prepopulate the CDROM with an interesting pattern */
    generate_pattern(pattern, patt_len, ATAPI_BLOCK_SIZE);
    fh = fopen(tmp_path, "w+");
    ret = fwrite(pattern, ATAPI_BLOCK_SIZE, patt_blocks, fh);
    g_assert_cmpint(ret, ==, patt_blocks);
    fclose(fh);

    ide_test_start("-drive if=none,file=%s,media=cdrom,format=raw,id=sr0,index=0 "
                   "-device ide-cd,drive=sr0,bus=ide.0", tmp_path);
    dev = get_pci_device(&bmdma_bar, &ide_bar);
    qtest_irq_intercept_in(global_qtest, "ioapic");

    /* PACKET command on device 0 */
    qpci_io_writeb(dev, ide_bar, reg_device, 0);
    qpci_io_writeb(dev, ide_bar, reg_lba_middle, BYTE_COUNT_LIMIT & 0xFF);
    qpci_io_writeb(dev, ide_bar, reg_lba_high, (BYTE_COUNT_LIMIT >> 8 & 0xFF));
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_PACKET);
    /* HP0: Check_Status_A State */
    nsleep(400);
    data = ide_wait_clear(BSY);
    /* HP1: Send_Packet State */
    assert_bit_set(data, DRQ | DRDY);
    assert_bit_clear(data, ERR | DF | BSY);

    /* SCSI CDB (READ10) -- read n*2048 bytes from block 0 */
    send_scsi_cdb_read10(dev, ide_bar, 0, nblocks);

    /* Read data back: occurs in bursts of 'BYTE_COUNT_LIMIT' bytes.
     * If BYTE_COUNT_LIMIT is odd, we transfer BYTE_COUNT_LIMIT - 1 bytes.
     * We allow an odd limit only when the remaining transfer size is
     * less than BYTE_COUNT_LIMIT. However, SCSI's read10 command can only
     * request n blocks, so our request size is always even.
     * For this reason, we assume there is never a hanging byte to fetch. */
    g_assert(!(rxsize & 1));
    limit = BYTE_COUNT_LIMIT & ~1;
    for (i = 0; i < DIV_ROUND_UP(rxsize, limit); i++) {
        size_t offset = i * (limit / 2);
        size_t rem = (rxsize / 2) - offset;

        /* HP3: INTRQ_Wait */
        ide_wait_intr(IDE_PRIMARY_IRQ);

        /* HP2: Check_Status_B (and clear IRQ) */
        data = ide_wait_clear(BSY);
        assert_bit_set(data, DRQ | DRDY);
        assert_bit_clear(data, ERR | DF | BSY);

        /* HP4: Transfer_Data */
        for (j = 0; j < MIN((limit / 2), rem); j++) {
            rx[offset + j] = cpu_to_le16(qpci_io_readw(dev, ide_bar,
                                                       reg_data));
        }
    }

    /* Check for final completion IRQ */
    ide_wait_intr(IDE_PRIMARY_IRQ);

    /* Sanity check final state */
    data = ide_wait_clear(DRQ);
    assert_bit_set(data, DRDY);
    assert_bit_clear(data, DRQ | ERR | DF | BSY);

    g_assert_cmpint(memcmp(pattern, rx, rxsize), ==, 0);
    g_free(pattern);
    g_free(rx);
    test_bmdma_teardown();
    free_pci_device(dev);
}

static void test_cdrom_pio(void)
{
    cdrom_pio_impl(1);
}

static void test_cdrom_pio_large(void)
{
    /* Test a few loops of the PIO DRQ mechanism. */
    cdrom_pio_impl(BYTE_COUNT_LIMIT * 4 / ATAPI_BLOCK_SIZE);
}


static void test_cdrom_dma(void)
{
    static const size_t len = ATAPI_BLOCK_SIZE;
    size_t ret;
    char *pattern = g_malloc(ATAPI_BLOCK_SIZE * 16);
    char *rx = g_malloc0(len);
    uintptr_t guest_buf;
    PrdtEntry prdt[1];
    FILE *fh;

    ide_test_start("-drive if=none,file=%s,media=cdrom,format=raw,id=sr0,index=0 "
                   "-device ide-cd,drive=sr0,bus=ide.0", tmp_path);
    qtest_irq_intercept_in(global_qtest, "ioapic");

    guest_buf = guest_alloc(guest_malloc, len);
    prdt[0].addr = cpu_to_le32(guest_buf);
    prdt[0].size = cpu_to_le32(len | PRDT_EOT);

    generate_pattern(pattern, ATAPI_BLOCK_SIZE * 16, ATAPI_BLOCK_SIZE);
    fh = fopen(tmp_path, "w+");
    ret = fwrite(pattern, ATAPI_BLOCK_SIZE, 16, fh);
    g_assert_cmpint(ret, ==, 16);
    fclose(fh);

    send_dma_request(CMD_PACKET, 0, 1, prdt, 1, send_scsi_cdb_read10);

    /* Read back data from guest memory into local qtest memory */
    memread(guest_buf, rx, len);
    g_assert_cmpint(memcmp(pattern, rx, len), ==, 0);

    g_free(pattern);
    g_free(rx);
    test_bmdma_teardown();
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();
    int fd;
    int ret;

    /* Check architecture */
    if (strcmp(arch, "i386") && strcmp(arch, "x86_64")) {
        g_test_message("Skipping test for non-x86\n");
        return 0;
    }

    /* Create temporary blkdebug instructions */
    fd = mkstemp(debug_path);
    g_assert(fd >= 0);
    close(fd);

    /* Create a temporary raw image */
    fd = mkstemp(tmp_path);
    g_assert(fd >= 0);
    ret = ftruncate(fd, TEST_IMAGE_SIZE);
    g_assert(ret == 0);
    close(fd);

    /* Run the tests */
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/ide/identify", test_identify);

    qtest_add_func("/ide/bmdma/setup", test_bmdma_setup);
    qtest_add_func("/ide/bmdma/simple_rw", test_bmdma_simple_rw);
    qtest_add_func("/ide/bmdma/short_prdt", test_bmdma_short_prdt);
    qtest_add_func("/ide/bmdma/one_sector_short_prdt",
                   test_bmdma_one_sector_short_prdt);
    qtest_add_func("/ide/bmdma/long_prdt", test_bmdma_long_prdt);
    qtest_add_func("/ide/bmdma/no_busmaster", test_bmdma_no_busmaster);
    qtest_add_func("/ide/bmdma/teardown", test_bmdma_teardown);

    qtest_add_func("/ide/flush", test_flush);
    qtest_add_func("/ide/flush/nodev", test_flush_nodev);
    qtest_add_func("/ide/flush/empty_drive", test_flush_empty_drive);
    qtest_add_func("/ide/flush/retry_pci", test_pci_retry_flush);
    qtest_add_func("/ide/flush/retry_isa", test_isa_retry_flush);

    qtest_add_func("/ide/cdrom/pio", test_cdrom_pio);
    qtest_add_func("/ide/cdrom/pio_large", test_cdrom_pio_large);
    qtest_add_func("/ide/cdrom/dma", test_cdrom_dma);

    ret = g_test_run();

    /* Cleanup */
    unlink(tmp_path);
    unlink(debug_path);

    return ret;
}
