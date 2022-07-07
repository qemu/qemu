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
#include "qapi/qmp/qdict.h"
#include "qemu/bswap.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"

/* TODO actually test the results and get rid of this */
#define qmp_discard_response(q, ...) qobject_unref(qtest_qmp(q, __VA_ARGS__))

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
    reg_error       = 0x1,
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

/* Error field */
enum {
    ABRT    = 0x04,
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
    CMD_DSM         = 0x06,
    CMD_DIAGNOSE    = 0x90,
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
static QGuestAllocator guest_malloc;

static char *tmp_path[2];
static char *debug_path;

static QTestState *ide_test_start(const char *cmdline_fmt, ...)
{
    QTestState *qts;
    g_autofree char *full_fmt = g_strdup_printf("-machine pc %s", cmdline_fmt);
    va_list ap;

    va_start(ap, cmdline_fmt);
    qts = qtest_vinitf(full_fmt, ap);
    va_end(ap);

    pc_alloc_init(&guest_malloc, qts, 0);

    return qts;
}

static void ide_test_quit(QTestState *qts)
{
    if (pcibus) {
        qpci_free_pc(pcibus);
        pcibus = NULL;
    }
    alloc_destroy(&guest_malloc);
    qtest_quit(qts);
}

static QPCIDevice *get_pci_device(QTestState *qts, QPCIBar *bmdma_bar,
                                  QPCIBar *ide_bar)
{
    QPCIDevice *dev;
    uint16_t vendor_id, device_id;

    if (!pcibus) {
        pcibus = qpci_new_pc(qts, NULL);
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

static uint64_t trim_range_le(uint64_t sector, uint16_t count)
{
    /* 2-byte range, 6-byte LBA */
    return cpu_to_le64(((uint64_t)count << 48) + sector);
}

static int send_dma_request(QTestState *qts, int cmd, uint64_t sector,
                            int nb_sectors, PrdtEntry *prdt, int prdt_entries,
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

    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

    flags = cmd & ~0xff;
    cmd &= 0xff;

    switch (cmd) {
    case CMD_READ_DMA:
    case CMD_PACKET:
        /* Assuming we only test data reads w/ ATAPI, otherwise we need to know
         * the SCSI command being sent in the packet, too. */
        from_dev = true;
        break;
    case CMD_DSM:
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
    guest_prdt = guest_alloc(&guest_malloc, len);
    qtest_memwrite(qts, guest_prdt, prdt, len);
    qpci_io_writel(dev, bmdma_bar, bmreg_prdt, guest_prdt);

    /* ATA DMA command */
    if (cmd == CMD_PACKET) {
        /* Enables ATAPI DMA; otherwise PIO is attempted */
        qpci_io_writeb(dev, ide_bar, reg_feature, 0x01);
    } else {
        if (cmd == CMD_DSM) {
            /* trim bit */
            qpci_io_writeb(dev, ide_bar, reg_feature, 0x01);
        }
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

    g_assert_cmpint(qtest_get_irq(qts, IDE_PRIMARY_IRQ), ==,
                    !!(status & BM_STS_INTR));

    /* Check IDE status code */
    assert_bit_set(qpci_io_readb(dev, ide_bar, reg_status), DRDY);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), BSY | DRQ);

    /* Reading the status register clears the IRQ */
    g_assert(!qtest_get_irq(qts, IDE_PRIMARY_IRQ));

    /* Stop DMA transfer if still active */
    if (status & BM_STS_ACTIVE) {
        qpci_io_writeb(dev, bmdma_bar, bmreg_cmd, 0);
    }

    free_pci_device(dev);

    return status;
}

static QTestState *test_bmdma_setup(void)
{
    QTestState *qts;

    qts = ide_test_start(
        "-drive file=%s,if=ide,cache=writeback,format=raw "
        "-global ide-hd.serial=%s -global ide-hd.ver=%s",
        tmp_path[0], "testdisk", "version");
    qtest_irq_intercept_in(qts, "ioapic");

    return qts;
}

static void test_bmdma_teardown(QTestState *qts)
{
    ide_test_quit(qts);
}

static void test_bmdma_simple_rw(void)
{
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t status;
    uint8_t *buf;
    uint8_t *cmpbuf;
    size_t len = 512;
    uintptr_t guest_buf;
    PrdtEntry prdt[1];

    qts = test_bmdma_setup();

    guest_buf  = guest_alloc(&guest_malloc, len);
    prdt[0].addr = cpu_to_le32(guest_buf);
    prdt[0].size = cpu_to_le32(len | PRDT_EOT);

    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

    buf = g_malloc(len);
    cmpbuf = g_malloc(len);

    /* Write 0x55 pattern to sector 0 */
    memset(buf, 0x55, len);
    qtest_memwrite(qts, guest_buf, buf, len);

    status = send_dma_request(qts, CMD_WRITE_DMA, 0, 1, prdt,
                              ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

    /* Write 0xaa pattern to sector 1 */
    memset(buf, 0xaa, len);
    qtest_memwrite(qts, guest_buf, buf, len);

    status = send_dma_request(qts, CMD_WRITE_DMA, 1, 1, prdt,
                              ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

    /* Read and verify 0x55 pattern in sector 0 */
    memset(cmpbuf, 0x55, len);

    status = send_dma_request(qts, CMD_READ_DMA, 0, 1, prdt, ARRAY_SIZE(prdt),
                              NULL);
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

    qtest_memread(qts, guest_buf, buf, len);
    g_assert(memcmp(buf, cmpbuf, len) == 0);

    /* Read and verify 0xaa pattern in sector 1 */
    memset(cmpbuf, 0xaa, len);

    status = send_dma_request(qts, CMD_READ_DMA, 1, 1, prdt, ARRAY_SIZE(prdt),
                              NULL);
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

    qtest_memread(qts, guest_buf, buf, len);
    g_assert(memcmp(buf, cmpbuf, len) == 0);

    free_pci_device(dev);
    g_free(buf);
    g_free(cmpbuf);

    test_bmdma_teardown(qts);
}

static void test_bmdma_trim(void)
{
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t status;
    const uint64_t trim_range[] = { trim_range_le(0, 2),
                                    trim_range_le(6, 8),
                                    trim_range_le(10, 1),
                                  };
    const uint64_t bad_range = trim_range_le(TEST_IMAGE_SIZE / 512 - 1, 2);
    size_t len = 512;
    uint8_t *buf;
    uintptr_t guest_buf;
    PrdtEntry prdt[1];

    qts = test_bmdma_setup();

    guest_buf = guest_alloc(&guest_malloc, len);
    prdt[0].addr = cpu_to_le32(guest_buf),
    prdt[0].size = cpu_to_le32(len | PRDT_EOT),

    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

    buf = g_malloc(len);

    /* Normal request */
    *((uint64_t *)buf) = trim_range[0];
    *((uint64_t *)buf + 1) = trim_range[1];

    qtest_memwrite(qts, guest_buf, buf, 2 * sizeof(uint64_t));

    status = send_dma_request(qts, CMD_DSM, 0, 1, prdt,
                              ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

    /* Request contains invalid range */
    *((uint64_t *)buf) = trim_range[2];
    *((uint64_t *)buf + 1) = bad_range;

    qtest_memwrite(qts, guest_buf, buf, 2 * sizeof(uint64_t));

    status = send_dma_request(qts, CMD_DSM, 0, 1, prdt,
                              ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_set(qpci_io_readb(dev, ide_bar, reg_status), ERR);
    assert_bit_set(qpci_io_readb(dev, ide_bar, reg_error), ABRT);

    free_pci_device(dev);
    g_free(buf);
    test_bmdma_teardown(qts);
}

/*
 * This test is developed according to the Programming Interface for
 * Bus Master IDE Controller (Revision 1.0 5/16/94)
 */
static void test_bmdma_various_prdts(void)
{
    int sectors = 0;
    uint32_t size = 0;

    for (sectors = 1; sectors <= 256; sectors *= 2) {
        QTestState *qts = NULL;
        QPCIDevice *dev = NULL;
        QPCIBar bmdma_bar, ide_bar;

        qts = test_bmdma_setup();
        dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

        for (size = 0; size < 65536; size += 256) {
            uint32_t req_size = sectors * 512;
            uint32_t prd_size = size & 0xfffe; /* bit 0 is always set to 0 */
            uint8_t ret = 0;
            uint8_t req_status = 0;
            uint8_t abort_req_status = 0;
            PrdtEntry prdt[] = {
                {
                    .addr = 0,
                    .size = cpu_to_le32(size | PRDT_EOT),
                },
            };

            /* A value of zero in PRD size indicates 64K */
            if (prd_size == 0) {
                prd_size = 65536;
            }

            /*
             * 1. If PRDs specified a smaller size than the IDE transfer
             * size, then the Interrupt and Active bits in the Controller
             * status register are not set (Error Condition).
             *
             * 2. If the size of the physical memory regions was equal to
             * the IDE device transfer size, the Interrupt bit in the
             * Controller status register is set to 1, Active bit is set to 0.
             *
             * 3. If PRDs specified a larger size than the IDE transfer size,
             * the Interrupt and Active bits in the Controller status register
             * are both set to 1.
             */
            if (prd_size < req_size) {
                req_status = 0;
                abort_req_status = 0;
            } else if (prd_size == req_size) {
                req_status = BM_STS_INTR;
                abort_req_status = BM_STS_INTR;
            } else {
                req_status = BM_STS_ACTIVE | BM_STS_INTR;
                abort_req_status = BM_STS_INTR;
            }

            /* Test the request */
            ret = send_dma_request(qts, CMD_READ_DMA, 0, sectors,
                                   prdt, ARRAY_SIZE(prdt), NULL);
            g_assert_cmphex(ret, ==, req_status);
            assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

            /* Now test aborting the same request */
            ret = send_dma_request(qts, CMD_READ_DMA | CMDF_ABORT, 0,
                                   sectors, prdt, ARRAY_SIZE(prdt), NULL);
            g_assert_cmphex(ret, ==, abort_req_status);
            assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);
        }

        free_pci_device(dev);
        test_bmdma_teardown(qts);
    }
}

static void test_bmdma_no_busmaster(void)
{
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t status;

    qts = test_bmdma_setup();

    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

    /* No PRDT_EOT, each entry addr 0/size 64k, and in theory qemu shouldn't be
     * able to access it anyway because the Bus Master bit in the PCI command
     * register isn't set. This is complete nonsense, but it used to be pretty
     * good at confusing and occasionally crashing qemu. */
    PrdtEntry prdt[4096] = { };

    status = send_dma_request(qts, CMD_READ_DMA | CMDF_NO_BM, 0, 512,
                              prdt, ARRAY_SIZE(prdt), NULL);

    /* Not entirely clear what the expected result is, but this is what we get
     * in practice. At least we want to be aware of any changes. */
    g_assert_cmphex(status, ==, BM_STS_ACTIVE | BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);
    free_pci_device(dev);
    test_bmdma_teardown(qts);
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
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t data;
    uint16_t buf[256];
    int i;
    int ret;

    qts = ide_test_start(
        "-drive file=%s,if=ide,cache=writeback,format=raw "
        "-global ide-hd.serial=%s -global ide-hd.ver=%s",
        tmp_path[0], "testdisk", "version");

    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

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

    ide_test_quit(qts);
    free_pci_device(dev);
}

static void test_diagnostic(void)
{
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t data;

    qts = ide_test_start(
        "-blockdev driver=file,node-name=hda,filename=%s "
        "-blockdev driver=file,node-name=hdb,filename=%s "
        "-device ide-hd,drive=hda,bus=ide.0,unit=0 "
        "-device ide-hd,drive=hdb,bus=ide.0,unit=1 ",
        tmp_path[0], tmp_path[1]);

    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

    /* DIAGNOSE command on device 1 */
    qpci_io_writeb(dev, ide_bar, reg_device, DEV);
    data = qpci_io_readb(dev, ide_bar, reg_device);
    g_assert_cmphex(data & DEV, ==, DEV);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_DIAGNOSE);

    /* Verify that DEVICE is now 0 */
    data = qpci_io_readb(dev, ide_bar, reg_device);
    g_assert_cmphex(data & DEV, ==, 0);

    ide_test_quit(qts);
    free_pci_device(dev);
}

/*
 * Write sector 1 with random data to make IDE storage dirty
 * Needed for flush tests so that flushes actually go though the block layer
 */
static void make_dirty(QTestState *qts, uint8_t device)
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t status;
    size_t len = 512;
    uintptr_t guest_buf;
    void* buf;

    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

    guest_buf = guest_alloc(&guest_malloc, len);
    buf = g_malloc(len);
    memset(buf, rand() % 255 + 1, len);
    g_assert(guest_buf);
    g_assert(buf);

    qtest_memwrite(qts, guest_buf, buf, len);

    PrdtEntry prdt[] = {
        {
            .addr = cpu_to_le32(guest_buf),
            .size = cpu_to_le32(len | PRDT_EOT),
        },
    };

    status = send_dma_request(qts, CMD_WRITE_DMA, 1, 1, prdt,
                              ARRAY_SIZE(prdt), NULL);
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

    g_free(buf);
    free_pci_device(dev);
}

static void test_flush(void)
{
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t data;

    qts = ide_test_start(
        "-drive file=blkdebug::%s,if=ide,cache=writeback,format=raw",
        tmp_path[0]);

    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

    qtest_irq_intercept_in(qts, "ioapic");

    /* Dirty media so that CMD_FLUSH_CACHE will actually go to disk */
    make_dirty(qts, 0);

    /* Delay the completion of the flush request until we explicitly do it */
    g_free(qtest_hmp(qts, "qemu-io ide0-hd0 \"break flush_to_os A\""));

    /* FLUSH CACHE command on device 0*/
    qpci_io_writeb(dev, ide_bar, reg_device, 0);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_FLUSH_CACHE);

    /* Check status while request is in flight*/
    data = qpci_io_readb(dev, ide_bar, reg_status);
    assert_bit_set(data, BSY | DRDY);
    assert_bit_clear(data, DF | ERR | DRQ);

    /* Complete the command */
    g_free(qtest_hmp(qts, "qemu-io ide0-hd0 \"resume A\""));

    /* Check registers */
    data = qpci_io_readb(dev, ide_bar, reg_device);
    g_assert_cmpint(data & DEV, ==, 0);

    do {
        data = qpci_io_readb(dev, ide_bar, reg_status);
    } while (data & BSY);

    assert_bit_set(data, DRDY);
    assert_bit_clear(data, BSY | DF | ERR | DRQ);

    ide_test_quit(qts);
    free_pci_device(dev);
}

static void test_pci_retry_flush(void)
{
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t data;

    prepare_blkdebug_script(debug_path, "flush_to_disk");

    qts = ide_test_start(
        "-drive file=blkdebug:%s:%s,if=ide,cache=writeback,format=raw,"
        "rerror=stop,werror=stop",
        debug_path, tmp_path[0]);

    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

    qtest_irq_intercept_in(qts, "ioapic");

    /* Dirty media so that CMD_FLUSH_CACHE will actually go to disk */
    make_dirty(qts, 0);

    /* FLUSH CACHE command on device 0*/
    qpci_io_writeb(dev, ide_bar, reg_device, 0);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_FLUSH_CACHE);

    /* Check status while request is in flight*/
    data = qpci_io_readb(dev, ide_bar, reg_status);
    assert_bit_set(data, BSY | DRDY);
    assert_bit_clear(data, DF | ERR | DRQ);

    qtest_qmp_eventwait(qts, "STOP");

    /* Complete the command */
    qmp_discard_response(qts, "{'execute':'cont' }");

    /* Check registers */
    data = qpci_io_readb(dev, ide_bar, reg_device);
    g_assert_cmpint(data & DEV, ==, 0);

    do {
        data = qpci_io_readb(dev, ide_bar, reg_status);
    } while (data & BSY);

    assert_bit_set(data, DRDY);
    assert_bit_clear(data, BSY | DF | ERR | DRQ);

    ide_test_quit(qts);
    free_pci_device(dev);
}

static void test_flush_nodev(void)
{
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;

    qts = ide_test_start("");

    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

    /* FLUSH CACHE command on device 0*/
    qpci_io_writeb(dev, ide_bar, reg_device, 0);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_FLUSH_CACHE);

    /* Just testing that qemu doesn't crash... */

    free_pci_device(dev);
    ide_test_quit(qts);
}

static void test_flush_empty_drive(void)
{
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;

    qts = ide_test_start("-device ide-cd,bus=ide.0");
    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

    /* FLUSH CACHE command on device 0 */
    qpci_io_writeb(dev, ide_bar, reg_device, 0);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_FLUSH_CACHE);

    /* Just testing that qemu doesn't crash... */

    free_pci_device(dev);
    ide_test_quit(qts);
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

static void nsleep(QTestState *qts, int64_t nsecs)
{
    const struct timespec val = { .tv_nsec = nsecs };
    nanosleep(&val, NULL);
    qtest_clock_set(qts, nsecs);
}

static uint8_t ide_wait_clear(QTestState *qts, uint8_t flag)
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t data;
    time_t st;

    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

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
        nsleep(qts, 400);
    }
    g_assert_not_reached();
}

static void ide_wait_intr(QTestState *qts, int irq)
{
    time_t st;
    bool intr;

    time(&st);
    while (true) {
        intr = qtest_get_irq(qts, irq);
        if (intr) {
            return;
        }
        if (difftime(time(NULL), st) > 5.0) {
            break;
        }
        nsleep(qts, 400);
    }

    g_assert_not_reached();
}

static void cdrom_pio_impl(int nblocks)
{
    QTestState *qts;
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
    fh = fopen(tmp_path[0], "wb+");
    ret = fwrite(pattern, ATAPI_BLOCK_SIZE, patt_blocks, fh);
    g_assert_cmpint(ret, ==, patt_blocks);
    fclose(fh);

    qts = ide_test_start(
            "-drive if=none,file=%s,media=cdrom,format=raw,id=sr0,index=0 "
            "-device ide-cd,drive=sr0,bus=ide.0", tmp_path[0]);
    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);
    qtest_irq_intercept_in(qts, "ioapic");

    /* PACKET command on device 0 */
    qpci_io_writeb(dev, ide_bar, reg_device, 0);
    qpci_io_writeb(dev, ide_bar, reg_lba_middle, BYTE_COUNT_LIMIT & 0xFF);
    qpci_io_writeb(dev, ide_bar, reg_lba_high, (BYTE_COUNT_LIMIT >> 8 & 0xFF));
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_PACKET);
    /* HP0: Check_Status_A State */
    nsleep(qts, 400);
    data = ide_wait_clear(qts, BSY);
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
        ide_wait_intr(qts, IDE_PRIMARY_IRQ);

        /* HP2: Check_Status_B (and clear IRQ) */
        data = ide_wait_clear(qts, BSY);
        assert_bit_set(data, DRQ | DRDY);
        assert_bit_clear(data, ERR | DF | BSY);

        /* HP4: Transfer_Data */
        for (j = 0; j < MIN((limit / 2), rem); j++) {
            rx[offset + j] = cpu_to_le16(qpci_io_readw(dev, ide_bar,
                                                       reg_data));
        }
    }

    /* Check for final completion IRQ */
    ide_wait_intr(qts, IDE_PRIMARY_IRQ);

    /* Sanity check final state */
    data = ide_wait_clear(qts, DRQ);
    assert_bit_set(data, DRDY);
    assert_bit_clear(data, DRQ | ERR | DF | BSY);

    g_assert_cmpint(memcmp(pattern, rx, rxsize), ==, 0);
    g_free(pattern);
    g_free(rx);
    test_bmdma_teardown(qts);
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
    QTestState *qts;
    static const size_t len = ATAPI_BLOCK_SIZE;
    size_t ret;
    char *pattern = g_malloc(ATAPI_BLOCK_SIZE * 16);
    char *rx = g_malloc0(len);
    uintptr_t guest_buf;
    PrdtEntry prdt[1];
    FILE *fh;

    qts = ide_test_start(
            "-drive if=none,file=%s,media=cdrom,format=raw,id=sr0,index=0 "
            "-device ide-cd,drive=sr0,bus=ide.0", tmp_path[0]);
    qtest_irq_intercept_in(qts, "ioapic");

    guest_buf = guest_alloc(&guest_malloc, len);
    prdt[0].addr = cpu_to_le32(guest_buf);
    prdt[0].size = cpu_to_le32(len | PRDT_EOT);

    generate_pattern(pattern, ATAPI_BLOCK_SIZE * 16, ATAPI_BLOCK_SIZE);
    fh = fopen(tmp_path[0], "wb+");
    ret = fwrite(pattern, ATAPI_BLOCK_SIZE, 16, fh);
    g_assert_cmpint(ret, ==, 16);
    fclose(fh);

    send_dma_request(qts, CMD_PACKET, 0, 1, prdt, 1, send_scsi_cdb_read10);

    /* Read back data from guest memory into local qtest memory */
    qtest_memread(qts, guest_buf, rx, len);
    g_assert_cmpint(memcmp(pattern, rx, len), ==, 0);

    g_free(pattern);
    g_free(rx);
    test_bmdma_teardown(qts);
}

int main(int argc, char **argv)
{
    const char *base;
    int i;
    int fd;
    int ret;

    /*
     * "base" stores the starting point where we create temporary files.
     *
     * On Windows, this is set to the relative path of current working
     * directory, because the absolute path causes the blkdebug filename
     * parser fail to parse "blkdebug:path/to/config:path/to/image".
     */
#ifndef _WIN32
    base = g_get_tmp_dir();
#else
    base = ".";
#endif

    /* Create temporary blkdebug instructions */
    debug_path = g_strdup_printf("%s/qtest-blkdebug.XXXXXX", base);
    fd = g_mkstemp(debug_path);
    g_assert(fd >= 0);
    close(fd);

    /* Create a temporary raw image */
    for (i = 0; i < 2; ++i) {
        tmp_path[i] = g_strdup_printf("%s/qtest.XXXXXX", base);
        fd = g_mkstemp(tmp_path[i]);
        g_assert(fd >= 0);
        ret = ftruncate(fd, TEST_IMAGE_SIZE);
        g_assert(ret == 0);
        close(fd);
    }

    /* Run the tests */
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/ide/identify", test_identify);

    qtest_add_func("/ide/diagnostic", test_diagnostic);

    qtest_add_func("/ide/bmdma/simple_rw", test_bmdma_simple_rw);
    qtest_add_func("/ide/bmdma/trim", test_bmdma_trim);
    qtest_add_func("/ide/bmdma/various_prdts", test_bmdma_various_prdts);
    qtest_add_func("/ide/bmdma/no_busmaster", test_bmdma_no_busmaster);

    qtest_add_func("/ide/flush", test_flush);
    qtest_add_func("/ide/flush/nodev", test_flush_nodev);
    qtest_add_func("/ide/flush/empty_drive", test_flush_empty_drive);
    qtest_add_func("/ide/flush/retry_pci", test_pci_retry_flush);

    qtest_add_func("/ide/cdrom/pio", test_cdrom_pio);
    qtest_add_func("/ide/cdrom/pio_large", test_cdrom_pio_large);
    qtest_add_func("/ide/cdrom/dma", test_cdrom_dma);

    ret = g_test_run();

    /* Cleanup */
    for (i = 0; i < 2; ++i) {
        unlink(tmp_path[i]);
        g_free(tmp_path[i]);
    }
    unlink(debug_path);
    g_free(debug_path);

    return ret;
}
