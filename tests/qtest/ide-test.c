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
#include "qobject/qdict.h"
#include "qemu/bswap.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"

/* Specified by ATA (physical) CHS geometry for ~64 MiB device.  */
#define TEST_IMAGE_SIZE ((130 * 16 * 63) * 512)

#define IDE_PCI_DEV     1
#define IDE_PCI_FUNC    1

#define IDE_BASE 0x1f0
#define IDE_BASE2 0x3f6
#define IDE_PRIMARY_IRQ 14

#define IDE_CTRL_RESET 0x04

#define ATAPI_BLOCK_SIZE 2048

/* Raw READ CD sector: 12 sync + 4 header + 2048 data + 288 EDC/ECC. */
#define ATAPI_RAW_SIZE   2352
#define ATAPI_RAW_DATA   16

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
    CMD_READ        = 0x20,  /* READ SECTOR(S) */
    CMD_WRITE       = 0x30,  /* WRITE SECTOR(S) */
    CMD_DIAGNOSE    = 0x90,
    CMD_INIT_DP     = 0x91,  /* INITIALIZE DEVICE PARAMETERS */
    CMD_READ_DMA    = 0xc8,
    CMD_WRITE_DMA   = 0xca,
    CMD_FLUSH_CACHE = 0xe7,
    CMD_IDENTIFY    = 0xec,
    CMD_PACKET      = 0xa0,
    CMD_READ_NATIVE = 0xf8,  /* READ NATIVE MAX ADDRESS */

    CMDF_ABORT      = 0x100,
    CMDF_NO_BM      = 0x200,
    CMDF_NO_WAIT    = 0x400,
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

G_GNUC_PRINTF(1, 2)
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

static uint8_t wait_dma_completion(QTestState *qts, QPCIDevice *dev,
                                   QPCIBar bmdma_bar, QPCIBar ide_bar)
{
    uint8_t status;

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

    return status;
}

static int send_dma_request_dev(QTestState *qts, QPCIDevice *dev,
                                QPCIBar bmdma_bar, QPCIBar ide_bar, int cmd,
                                uint64_t sector, int nb_sectors,
                                PrdtEntry *prdt, int prdt_entries,
                                void(*post_exec)(QPCIDevice *dev,
                                                 QPCIBar ide_bar,
                                                 uint64_t sector,
                                                 int nb_sectors))
{
    uintptr_t guest_prdt;
    size_t len;
    bool from_dev;
    uint8_t status;
    int flags;

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

    if (flags & CMDF_NO_WAIT) {
        return 0;
    }

    status = wait_dma_completion(qts, dev, bmdma_bar, ide_bar);

    return status;
}

static int send_dma_request(QTestState *qts, int cmd, uint64_t sector,
                            int nb_sectors, PrdtEntry *prdt, int prdt_entries,
                            void(*post_exec)(QPCIDevice *dev, QPCIBar ide_bar,
                                             uint64_t sector, int nb_sectors))
{
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint8_t status;

    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);
    status = send_dma_request_dev(qts, dev, bmdma_bar, ide_bar,
                                  cmd, sector, nb_sectors, prdt, prdt_entries,
                                  post_exec);
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

static void test_bmdma_trim_reset(void)
{
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar, ide_bar2;
    uint8_t status;
    const uint64_t trim_range[] = {
        trim_range_le(0, 2),
        trim_range_le(6, 8),
    };
    size_t len = 512;
    uint8_t *buf;
    uintptr_t guest_buf;
    PrdtEntry prdt[1];

    qts = ide_test_start(
        "-blockdev file,filename=%s,node-name=img "
        "-blockdev blkdebug,image=img,node-name=dbg,discard=unmap,"
        "inject-error.0.event=none,inject-error.0.iotype=discard,"
        "inject-error.0.errno=0,inject-error.0.delay-ns=1000000 "
        "-device ide-hd,drive=dbg,bus=ide.0",
        tmp_path[0]);
    qtest_irq_intercept_in(qts, "ioapic");

    guest_buf = guest_alloc(&guest_malloc, len);
    prdt[0].addr = cpu_to_le32(guest_buf),
    prdt[0].size = cpu_to_le32(len | PRDT_EOT),

    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);
    ide_bar2 = qpci_legacy_iomap(dev, IDE_BASE2);

    buf = g_malloc(len);

    /* TRIM request with two segments */
    *((uint64_t *)buf) = trim_range[0];
    *((uint64_t *)buf + 1) = trim_range[1];

    qtest_memwrite(qts, guest_buf, buf, 2 * sizeof(uint64_t));

    send_dma_request_dev(qts, dev, bmdma_bar, ide_bar, CMD_DSM | CMDF_NO_WAIT, 0, 1, prdt,
                     ARRAY_SIZE(prdt), NULL);

    /* Reset the device while the first segment is in flight */
    qpci_io_writeb(dev, ide_bar2, 0, IDE_CTRL_RESET);

    status = wait_dma_completion(qts, dev, bmdma_bar, ide_bar);
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), DF | ERR);

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

static void test_specify(void)
{
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint16_t cyls;
    uint8_t heads, spt;

    qts = ide_test_start(
        "-blockdev driver=file,node-name=hda,filename=%s "
        "-device ide-hd,drive=hda,bus=ide.0,unit=0 ",
        tmp_path[0]);

    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

    /* Initialize drive with zero sectors per track and one head.  */
    qpci_io_writeb(dev, ide_bar, reg_nsectors, 0);
    qpci_io_writeb(dev, ide_bar, reg_device, 0);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_INIT_DP);

    /* READ NATIVE MAX ADDRESS (CHS mode).  */
    qpci_io_writeb(dev, ide_bar, reg_device, 0xa0);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_READ_NATIVE);

    heads = qpci_io_readb(dev, ide_bar, reg_device) & 0xf;
    ++heads;
    g_assert_cmpint(heads, ==, 16);

    cyls = qpci_io_readb(dev, ide_bar, reg_lba_high) << 8;
    cyls |= qpci_io_readb(dev, ide_bar, reg_lba_middle);
    ++cyls;
    g_assert_cmpint(cyls, ==, 130);

    spt = qpci_io_readb(dev, ide_bar, reg_lba_low);
    g_assert_cmpint(spt, ==, 63);

    ide_test_quit(qts);
    free_pci_device(dev);
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
    qtest_qmp_assert_success(qts, "{'execute':'cont' }");

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

    qts = ide_test_start("%s", "");

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

typedef struct ReadCDCDB {
    uint8_t opcode;
    uint8_t sector_type;
    uint32_t lba;
    uint8_t length[3];
    uint8_t main_channel;
    uint8_t sub_channel;
    uint8_t control;
} __attribute__((__packed__)) ReadCDCDB;

static void send_scsi_cdb_read_cd(QPCIDevice *dev, QPCIBar ide_bar,
                                  uint64_t lba, int nblocks)
{
    ReadCDCDB pkt = { };
    int i;

    g_assert_cmpint(lba, <=, UINT32_MAX);
    g_assert_cmpint(nblocks, >=, 0);
    g_assert_cmpint(nblocks, <=, 0xffffff);

    /* Construct SCSI CDB packet */
    pkt.opcode = 0xbe;
    pkt.lba = cpu_to_be32(lba);
    pkt.length[0] = (nblocks >> 16) & 0xff;
    pkt.length[1] = (nblocks >> 8) & 0xff;
    pkt.length[2] = nblocks & 0xff;
    pkt.main_channel = 0xf8; /* sync + headers + user data + EDC/ECC: 2352 */

    /* Send Packet */
    for (i = 0; i < sizeof(ReadCDCDB) / 2; i++) {
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

#define CDROM_PIO 0
#define CDROM_DMA (1 << 0)
#define CDROM_RAW (1 << 1)

static void cdrom_read_impl(int nblocks, unsigned flags)
{
    bool dma = flags & CDROM_DMA;
    bool raw = flags & CDROM_RAW;
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    FILE *fh;
    int patt_blocks = MAX(16, nblocks);
    size_t patt_len = ATAPI_BLOCK_SIZE * patt_blocks;
    char *pattern = g_malloc(patt_len);
    unsigned xfer = raw ? ATAPI_RAW_SIZE : ATAPI_BLOCK_SIZE;
    size_t rxsize = xfer * nblocks;
    uint16_t *rx = g_malloc0(rxsize);
    void (*send_cdb)(QPCIDevice *, QPCIBar, uint64_t, int) =
        raw ? send_scsi_cdb_read_cd : send_scsi_cdb_read10;
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

    if (dma) {
        uintptr_t guest_buf = guest_alloc(&guest_malloc, rxsize);
        PrdtEntry prdt[1];

        prdt[0].addr = cpu_to_le32(guest_buf);
        prdt[0].size = cpu_to_le32(rxsize | PRDT_EOT);

        send_dma_request_dev(qts, dev, bmdma_bar, ide_bar, CMD_PACKET, 0,
                             nblocks, prdt, ARRAY_SIZE(prdt), send_cdb);

        qtest_memread(qts, guest_buf, rx, rxsize);
    } else {
        /* PACKET command on device 0 */
        qpci_io_writeb(dev, ide_bar, reg_device, 0);
        qpci_io_writeb(dev, ide_bar, reg_lba_middle, BYTE_COUNT_LIMIT & 0xFF);
        qpci_io_writeb(dev, ide_bar, reg_lba_high,
                       (BYTE_COUNT_LIMIT >> 8 & 0xFF));
        qpci_io_writeb(dev, ide_bar, reg_command, CMD_PACKET);
        /* HP0: Check_Status_A State */
        nsleep(qts, 400);
        data = ide_wait_clear(qts, BSY);
        /* HP1: Send_Packet State */
        assert_bit_set(data, DRQ | DRDY);
        assert_bit_clear(data, ERR | DF | BSY);

        send_cdb(dev, ide_bar, 0, nblocks);

        /*
         * Read data back: occurs in bursts of 'BYTE_COUNT_LIMIT' bytes.
         * If BYTE_COUNT_LIMIT is odd, we transfer BYTE_COUNT_LIMIT - 1 bytes.
         * We allow an odd limit only when the remaining transfer size is
         * less than BYTE_COUNT_LIMIT. However, SCSI's read10 command can only
         * request n blocks, so our request size is always even.
         * For this reason, we assume there is never a hanging byte to fetch.
         */
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
    }

    if (raw) {
        /* The 2048-byte payload of each raw sector sits past its header. */
        for (i = 0; i < nblocks; i++) {
            uint8_t *sec = (uint8_t *)rx + i * ATAPI_RAW_SIZE + ATAPI_RAW_DATA;

            g_assert_cmpint(memcmp(sec, pattern + i * ATAPI_BLOCK_SIZE,
                                   ATAPI_BLOCK_SIZE), ==, 0);
        }
    } else {
        g_assert_cmpint(memcmp(pattern, rx, rxsize), ==, 0);
    }

    g_free(pattern);
    g_free(rx);
    test_bmdma_teardown(qts);
    free_pci_device(dev);
}

static void ide_identify_words(QPCIDevice *dev, QPCIBar ide_bar,
                               uint16_t buf[256])
{
    int i;

    qpci_io_writeb(dev, ide_bar, reg_device, 0);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_IDENTIFY);
    for (i = 0; i < 256; i++) {
        buf[i] = qpci_io_readw(dev, ide_bar, reg_data);
    }
}

/* Zero sectors per track has to abort (ATA-5 8.16.6), not divide by zero */
static void test_specify_zero_sectors(void)
{
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint16_t buf[256];
    uint8_t data;
    int i;

    qts = ide_test_start(
        "-blockdev driver=file,node-name=hda,filename=%s "
        "-device ide-hd,drive=hda,bus=ide.0,unit=0 ",
        tmp_path[0]);

    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

    qpci_io_writeb(dev, ide_bar, reg_nsectors, 0);
    qpci_io_writeb(dev, ide_bar, reg_device, 0);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_INIT_DP);

    assert_bit_set(qpci_io_readb(dev, ide_bar, reg_status), ERR);
    assert_bit_set(qpci_io_readb(dev, ide_bar, reg_error), ABRT);

    /* The refused request has to leave the default translation in effect */
    ide_identify_words(dev, ide_bar, buf);
    g_assert_cmpint(buf[55], ==, 16);
    g_assert_cmpint(buf[56], ==, 63);

    /* READ SECTOR(S) of CHS 0/0/1, which used to crash QEMU */
    qpci_io_writeb(dev, ide_bar, reg_nsectors, 1);
    qpci_io_writeb(dev, ide_bar, reg_lba_low, 1);
    qpci_io_writeb(dev, ide_bar, reg_lba_middle, 0);
    qpci_io_writeb(dev, ide_bar, reg_lba_high, 0);
    qpci_io_writeb(dev, ide_bar, reg_device, 0);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_READ);

    data = ide_wait_clear(qts, BSY);
    assert_bit_set(data, DRQ);
    assert_bit_clear(data, ERR | DF);
    for (i = 0; i < 256; i++) {
        buf[i] = qpci_io_readw(dev, ide_bar, reg_data);
    }
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), ERR | DF | DRQ);

    /* A supported translation is still accepted */
    qpci_io_writeb(dev, ide_bar, reg_nsectors, 32);
    qpci_io_writeb(dev, ide_bar, reg_device, 7);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_INIT_DP);

    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), ERR);

    ide_test_quit(qts);
    free_pci_device(dev);
}

/* Addressed by LBA, so no translation can influence where it lands */
static void ide_write_marker(QTestState *qts, QPCIDevice *dev, QPCIBar ide_bar,
                             uint32_t lba, const char *marker)
{
    uint16_t buf[256];
    uint8_t data;
    int i;

    memset(buf, 0, sizeof(buf));
    memcpy(buf, marker, strlen(marker));

    qpci_io_writeb(dev, ide_bar, reg_nsectors, 1);
    qpci_io_writeb(dev, ide_bar, reg_lba_low, lba & 0xff);
    qpci_io_writeb(dev, ide_bar, reg_lba_middle, (lba >> 8) & 0xff);
    qpci_io_writeb(dev, ide_bar, reg_lba_high, (lba >> 16) & 0xff);
    qpci_io_writeb(dev, ide_bar, reg_device, LBA | ((lba >> 24) & 0xf));
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_WRITE);

    data = ide_wait_clear(qts, BSY);
    assert_bit_set(data, DRQ);
    for (i = 0; i < 256; i++) {
        qpci_io_writew(dev, ide_bar, reg_data, buf[i]);
    }
    data = ide_wait_clear(qts, BSY);
    assert_bit_clear(data, ERR | DF | DRQ);

    qpci_io_writeb(dev, ide_bar, reg_command, CMD_FLUSH_CACHE);
    data = ide_wait_clear(qts, BSY);
    assert_bit_clear(data, ERR | DF);
}

/* The marker read back names the sector the translation selected */
static void ide_read_chs_marker(QTestState *qts, QPCIDevice *dev,
                                QPCIBar ide_bar, uint8_t cyl_lo, uint8_t head,
                                uint8_t sector, char out[9])
{
    uint16_t buf[256];
    uint8_t data;
    int i;

    qpci_io_writeb(dev, ide_bar, reg_nsectors, 1);
    qpci_io_writeb(dev, ide_bar, reg_lba_low, sector);
    qpci_io_writeb(dev, ide_bar, reg_lba_middle, cyl_lo);
    qpci_io_writeb(dev, ide_bar, reg_lba_high, 0);
    qpci_io_writeb(dev, ide_bar, reg_device, head & 0xf);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_READ);

    data = ide_wait_clear(qts, BSY);
    assert_bit_set(data, DRQ);
    assert_bit_clear(data, ERR | DF);
    for (i = 0; i < 256; i++) {
        buf[i] = qpci_io_readw(dev, ide_bar, reg_data);
    }
    data = ide_wait_clear(qts, BSY);
    assert_bit_clear(data, ERR | DF | DRQ);

    memcpy(out, buf, 8);
    out[8] = '\0';
}

static void ide_set_translation(QPCIDevice *dev, QPCIBar ide_bar,
                                uint8_t heads, uint8_t sectors)
{
    qpci_io_writeb(dev, ide_bar, reg_nsectors, sectors);
    qpci_io_writeb(dev, ide_bar, reg_device, heads - 1);
    qpci_io_writeb(dev, ide_bar, reg_command, CMD_INIT_DP);
    assert_bit_clear(qpci_io_readb(dev, ide_bar, reg_status), ERR);
}

/* CHS 0/1/1 is LBA 32 under 8/32, and LBA 63 under the drive's own 16/63 */
#define CHS_MARKER_CUSTOM  "CUSTOM__"
#define CHS_MARKER_DEFAULT "DEFAULT_"

static void ide_prepare_markers(QTestState *qts, QPCIDevice *dev,
                                QPCIBar ide_bar)
{
    ide_write_marker(qts, dev, ide_bar, 32, CHS_MARKER_CUSTOM);
    ide_write_marker(qts, dev, ide_bar, 63, CHS_MARKER_DEFAULT);
}

static void ide_hmp_quiet(QTestState *qts, const char *command)
{
    g_autofree char *out = qtest_hmp(qts, "%s", command);

    g_assert_cmpstr(out, ==, "");
}

static char *ide_migration_status(QTestState *qts)
{
    QDict *ret;
    char *status;

    ret = qtest_qmp_assert_success_ref(qts, "{ 'execute': 'query-migrate' }");
    g_assert(qdict_haskey(ret, "status"));
    status = g_strdup(qdict_get_str(ret, "status"));
    qobject_unref(ret);

    return status;
}

/* Waiting for the other side's event would hang if it refuses the stream */
static void ide_migration_wait(QTestState *qts, const char *expected)
{
    while (true) {
        g_autofree char *status = ide_migration_status(qts);

        if (g_str_equal(status, expected)) {
            return;
        }
        if (!g_str_equal(status, "setup") && !g_str_equal(status, "active") &&
            !g_str_equal(status, "device")) {
            fprintf(stderr, "Migration status is %s, expected %s\n",
                    status, expected);
            g_assert_not_reached();
        }
        g_usleep(5000);
    }
}

static void ide_migrate(QTestState *src, QTestState *dst, const char *uri)
{
    qtest_qmp_assert_success(src, "{ 'execute': 'migrate',"
                             " 'arguments': { 'uri': %s } }", uri);
    qtest_qmp_eventwait(src, "STOP");
    ide_migration_wait(src, "completed");
    qtest_qmp_eventwait(dst, "RESUME");
}

/* A translation the guest selected has to survive migration */
static void test_migrate_chs_translation(void)
{
    QTestState *src, *dst;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    g_autofree char *mig_path = NULL;
    g_autofree char *uri = NULL;
    g_autofree char *dst_args = NULL;
    char marker[9];
    int fd;

    fd = g_file_open_tmp("qtest-ide-migration.XXXXXX", &mig_path, NULL);
    g_assert(fd >= 0);
    close(fd);
    uri = g_strdup_printf("unix:%s", mig_path);

    src = ide_test_start(
        "-blockdev driver=file,node-name=hda,filename=%s,locking=off "
        "-device ide-hd,drive=hda,bus=ide.0,unit=0 ",
        tmp_path[0]);
    dev = get_pci_device(src, &bmdma_bar, &ide_bar);

    ide_prepare_markers(src, dev, ide_bar);
    ide_set_translation(dev, ide_bar, 8, 32);
    ide_read_chs_marker(src, dev, ide_bar, 0, 1, 1, marker);
    g_assert_cmpstr(marker, ==, CHS_MARKER_CUSTOM);

    dst_args = g_strdup_printf(
        "-machine pc "
        "-blockdev driver=file,node-name=hda,filename=%s,locking=off "
        "-device ide-hd,drive=hda,bus=ide.0,unit=0 -incoming %s",
        tmp_path[0], uri);
    dst = qtest_init(dst_args);

    ide_migrate(src, dst, uri);

    /* Talk to the destination instead of the source */
    qpci_free_pc(pcibus);
    pcibus = NULL;
    free_pci_device(dev);
    dev = get_pci_device(dst, &bmdma_bar, &ide_bar);

    ide_read_chs_marker(dst, dev, ide_bar, 0, 1, 1, marker);
    g_assert_cmpstr(marker, ==, CHS_MARKER_CUSTOM);

    free_pci_device(dev);
    qtest_quit(dst);
    ide_test_quit(src);
    unlink(mig_path);
}

/* A translation selected after the snapshot must not outlive loading it */
static void test_migrate_chs_snapshot(void)
{
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    g_autofree char *img = NULL;
    char marker[9];
    int fd;

    if (!have_qemu_img()) {
        g_test_skip("QTEST_QEMU_IMG not set, snapshots need a qcow2 image");
        return;
    }

    fd = g_file_open_tmp("qtest-ide-snapshot.XXXXXX", &img, NULL);
    g_assert(fd >= 0);
    close(fd);
    g_assert(mkimg(img, "qcow2", TEST_IMAGE_SIZE / (1024 * 1024)));

    qts = ide_test_start(
        "-blockdev driver=qcow2,node-name=hda,file.driver=file,"
        "file.filename=%s "
        "-device ide-hd,drive=hda,bus=ide.0,unit=0 ", img);
    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

    ide_prepare_markers(qts, dev, ide_bar);

    /* Snapshot taken while the default translation is in effect */
    ide_read_chs_marker(qts, dev, ide_bar, 0, 1, 1, marker);
    g_assert_cmpstr(marker, ==, CHS_MARKER_DEFAULT);
    ide_hmp_quiet(qts, "savevm s0");

    ide_set_translation(dev, ide_bar, 8, 32);
    ide_read_chs_marker(qts, dev, ide_bar, 0, 1, 1, marker);
    g_assert_cmpstr(marker, ==, CHS_MARKER_CUSTOM);

    ide_hmp_quiet(qts, "loadvm s0");

    ide_read_chs_marker(qts, dev, ide_bar, 0, 1, 1, marker);
    g_assert_cmpstr(marker, ==, CHS_MARKER_DEFAULT);

    free_pci_device(dev);
    ide_test_quit(qts);
    unlink(img);
}

/* A migration stream holds NUL bytes, so this cannot be a string search */
static char *ide_stream_find(char *stream, gsize len, const char *name)
{
    gsize name_len = strlen(name);
    gsize i;

    if (len < name_len) {
        return NULL;
    }
    for (i = 0; i <= len - name_len; i++) {
        if (memcmp(stream + i, name, name_len) == 0) {
            return stream + i;
        }
    }

    return NULL;
}

/* A translation no command could have selected has to be refused on load */
static void test_migrate_chs_rejected(void)
{
    const char *name = "ide_drive/chs_translation";
    QTestState *src, *dst;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    g_autofree char *dst_args = NULL;
    g_autofree char *stream = NULL;
    char *subsection;
    gsize len;
    int fd;

    fd = g_file_open_tmp("qtest-ide-stream.XXXXXX", &path, NULL);
    g_assert(fd >= 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = ide_test_start(
        "-blockdev driver=file,node-name=hda,filename=%s "
        "-device ide-hd,drive=hda,bus=ide.0,unit=0 ",
        tmp_path[0]);
    dev = get_pci_device(src, &bmdma_bar, &ide_bar);

    ide_set_translation(dev, ide_bar, 8, 32);
    qtest_qmp_assert_success(src, "{ 'execute': 'migrate',"
                             " 'arguments': { 'uri': %s } }", uri);
    qtest_qmp_eventwait(src, "STOP");
    ide_migration_wait(src, "completed");
    free_pci_device(dev);
    ide_test_quit(src);

    /*
     * Behind the name come version, heads and sectors, each big endian 32 bit.
     * The name recurs in the description at the end of the stream, so the
     * first match is the one carrying data.
     */
    g_assert(g_file_get_contents(path, &stream, &len, NULL));
    subsection = ide_stream_find(stream, len, name);
    g_assert(subsection);
    g_assert_cmpint(subsection - stream + strlen(name) + 12, <=, len);
    memset(subsection + strlen(name) + 8, 0, 4);
    g_assert(g_file_set_contents(path, stream, len, NULL));

    dst_args = g_strdup_printf(
        "-machine pc "
        "-blockdev driver=file,node-name=hda,filename=%s "
        "-device ide-hd,drive=hda,bus=ide.0,unit=0 -incoming defer",
        tmp_path[0]);
    dst = qtest_init(dst_args);

    qtest_qmp_assert_success(dst, "{ 'execute': 'migrate-incoming',"
                             " 'arguments': { 'uri': %s,"
                             " 'exit-on-error': false } }", uri);
    ide_migration_wait(dst, "failed");

    qtest_quit(dst);
    unlink(path);
}

/* Words 54 to 58 follow the translation even when the data was cached first */
static void test_specify_identify(void)
{
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint16_t buf[256];
    unsigned int cyls;

    qts = ide_test_start(
        "-blockdev driver=file,node-name=hda,filename=%s "
        "-device ide-hd,drive=hda,bus=ide.0,unit=0 ",
        tmp_path[0]);
    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

    /* Have the data built while the default translation is still in effect */
    ide_identify_words(dev, ide_bar, buf);
    cyls = buf[1];
    g_assert_cmpint(buf[3], ==, 16);
    g_assert_cmpint(buf[6], ==, 63);
    g_assert_cmpint(buf[53] & 1, ==, 1);
    g_assert_cmpint(buf[55], ==, 16);
    g_assert_cmpint(buf[56], ==, 63);
    g_assert_cmpint(buf[57] | (buf[58] << 16), ==, cyls * 16 * 63);

    ide_set_translation(dev, ide_bar, 8, 32);

    ide_identify_words(dev, ide_bar, buf);
    g_assert_cmpint(buf[1], ==, cyls);
    g_assert_cmpint(buf[3], ==, 16);
    g_assert_cmpint(buf[4], ==, 512 * 63);
    g_assert_cmpint(buf[6], ==, 63);
    g_assert_cmpint(buf[55], ==, 8);
    g_assert_cmpint(buf[56], ==, 32);
    g_assert_cmpint(buf[57] | (buf[58] << 16), ==, cyls * 8 * 32);

    free_pci_device(dev);
    ide_test_quit(qts);
}

/* Words 3 and 6 keep the drive's own geometry even if built after a change */
static void test_specify_identify_default(void)
{
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar;
    uint16_t buf[256];

    qts = ide_test_start(
        "-blockdev driver=file,node-name=hda,filename=%s "
        "-device ide-hd,drive=hda,bus=ide.0,unit=0 ",
        tmp_path[0]);
    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);

    /* No IDENTIFY DEVICE before this one, so nothing was cached yet */
    ide_set_translation(dev, ide_bar, 8, 32);
    ide_identify_words(dev, ide_bar, buf);
    g_assert_cmpint(buf[3], ==, 16);
    g_assert_cmpint(buf[4], ==, 512 * 63);
    g_assert_cmpint(buf[6], ==, 63);
    g_assert_cmpint(buf[55], ==, 8);
    g_assert_cmpint(buf[56], ==, 32);
    g_assert_cmpint(buf[57] | (buf[58] << 16), ==, buf[1] * 8 * 32);

    free_pci_device(dev);
    ide_test_quit(qts);
}

/* A hardware reset reverts the translation (ATA-5 9.1), SRST does not (9.2) */
static void test_specify_reset(void)
{
    QTestState *qts;
    QPCIDevice *dev;
    QPCIBar bmdma_bar, ide_bar, ide_bar2;
    uint16_t buf[256];
    char marker[9];

    qts = ide_test_start(
        "-blockdev driver=file,node-name=hda,filename=%s "
        "-device ide-hd,drive=hda,bus=ide.0,unit=0 ",
        tmp_path[0]);
    dev = get_pci_device(qts, &bmdma_bar, &ide_bar);
    ide_bar2 = qpci_legacy_iomap(dev, IDE_BASE2);

    ide_prepare_markers(qts, dev, ide_bar);
    ide_set_translation(dev, ide_bar, 8, 32);
    ide_read_chs_marker(qts, dev, ide_bar, 0, 1, 1, marker);
    g_assert_cmpstr(marker, ==, CHS_MARKER_CUSTOM);

    qpci_io_writeb(dev, ide_bar2, 0, IDE_CTRL_RESET);
    qpci_io_writeb(dev, ide_bar2, 0, 0);
    ide_wait_clear(qts, BSY);

    ide_identify_words(dev, ide_bar, buf);
    g_assert_cmpint(buf[55], ==, 8);
    g_assert_cmpint(buf[56], ==, 32);
    ide_read_chs_marker(qts, dev, ide_bar, 0, 1, 1, marker);
    g_assert_cmpstr(marker, ==, CHS_MARKER_CUSTOM);

    qtest_qmp_assert_success(qts, "{ 'execute': 'system_reset' }");
    qtest_qmp_eventwait(qts, "RESET");
    qpci_device_enable(dev);

    ide_identify_words(dev, ide_bar, buf);
    g_assert_cmpint(buf[55], ==, 16);
    g_assert_cmpint(buf[56], ==, 63);
    ide_read_chs_marker(qts, dev, ide_bar, 0, 1, 1, marker);
    g_assert_cmpstr(marker, ==, CHS_MARKER_DEFAULT);

    free_pci_device(dev);
    ide_test_quit(qts);
}

static void test_cdrom_pio(void)
{
    cdrom_read_impl(1, CDROM_PIO);
}

static void test_cdrom_pio_large(void)
{
    /* Test a few loops of the PIO DRQ mechanism. */
    cdrom_read_impl(BYTE_COUNT_LIMIT * 4 / ATAPI_BLOCK_SIZE, CDROM_PIO);
}

static void test_cdrom_dma(void)
{
    cdrom_read_impl(1, CDROM_DMA);
}

static void test_cdrom_dma_large(void)
{
    cdrom_read_impl(BYTE_COUNT_LIMIT * 4 / ATAPI_BLOCK_SIZE, CDROM_DMA);
}

static void test_cdrom_pio_raw(void)
{
    cdrom_read_impl(4, CDROM_RAW);
}

static void test_cdrom_dma_raw(void)
{
    cdrom_read_impl(4, CDROM_DMA | CDROM_RAW);
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

    qtest_add_func("/ide/read_native", test_specify);
    qtest_add_func("/ide/specify/zero_sectors", test_specify_zero_sectors);
    qtest_add_func("/ide/specify/identify", test_specify_identify);
    qtest_add_func("/ide/specify/identify_default",
                   test_specify_identify_default);
    qtest_add_func("/ide/specify/reset", test_specify_reset);
    qtest_add_func("/ide/migration/chs_translation",
                   test_migrate_chs_translation);
    qtest_add_func("/ide/migration/chs_snapshot", test_migrate_chs_snapshot);
    qtest_add_func("/ide/migration/chs_rejected", test_migrate_chs_rejected);

    qtest_add_func("/ide/identify", test_identify);

    qtest_add_func("/ide/diagnostic", test_diagnostic);

    qtest_add_func("/ide/bmdma/simple_rw", test_bmdma_simple_rw);
    qtest_add_func("/ide/bmdma/trim", test_bmdma_trim);
    qtest_add_func("/ide/bmdma/trim_reset", test_bmdma_trim_reset);
    qtest_add_func("/ide/bmdma/various_prdts", test_bmdma_various_prdts);
    qtest_add_func("/ide/bmdma/no_busmaster", test_bmdma_no_busmaster);

    qtest_add_func("/ide/flush", test_flush);
    qtest_add_func("/ide/flush/nodev", test_flush_nodev);
    qtest_add_func("/ide/flush/empty_drive", test_flush_empty_drive);
    qtest_add_func("/ide/flush/retry_pci", test_pci_retry_flush);

    qtest_add_func("/ide/cdrom/pio", test_cdrom_pio);
    qtest_add_func("/ide/cdrom/pio_large", test_cdrom_pio_large);
    qtest_add_func("/ide/cdrom/dma", test_cdrom_dma);
    qtest_add_func("/ide/cdrom/dma_large", test_cdrom_dma_large);
    qtest_add_func("/ide/cdrom/pio_raw", test_cdrom_pio_raw);
    qtest_add_func("/ide/cdrom/dma_raw", test_cdrom_dma_raw);

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
