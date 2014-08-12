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

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <glib.h>

#include "libqtest.h"
#include "libqos/pci-pc.h"
#include "libqos/malloc-pc.h"

#include "qemu-common.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"

#define TEST_IMAGE_SIZE 64 * 1024 * 1024

#define IDE_PCI_DEV     1
#define IDE_PCI_FUNC    1

#define IDE_BASE 0x1f0
#define IDE_PRIMARY_IRQ 14

enum {
    reg_data        = 0x0,
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
    qtest_irq_intercept_in(global_qtest, "ioapic");
    guest_malloc = pc_alloc_init();

    g_free(cmdline);
}

static void ide_test_quit(void)
{
    qtest_end();
}

static QPCIDevice *get_pci_device(uint16_t *bmdma_base)
{
    QPCIDevice *dev;
    uint16_t vendor_id, device_id;

    if (!pcibus) {
        pcibus = qpci_init_pc();
    }

    /* Find PCI device and verify it's the right one */
    dev = qpci_device_find(pcibus, QPCI_DEVFN(IDE_PCI_DEV, IDE_PCI_FUNC));
    g_assert(dev != NULL);

    vendor_id = qpci_config_readw(dev, PCI_VENDOR_ID);
    device_id = qpci_config_readw(dev, PCI_DEVICE_ID);
    g_assert(vendor_id == PCI_VENDOR_ID_INTEL);
    g_assert(device_id == PCI_DEVICE_ID_INTEL_82371SB_1);

    /* Map bmdma BAR */
    *bmdma_base = (uint16_t)(uintptr_t) qpci_iomap(dev, 4, NULL);

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
                            PrdtEntry *prdt, int prdt_entries)
{
    QPCIDevice *dev;
    uint16_t bmdma_base;
    uintptr_t guest_prdt;
    size_t len;
    bool from_dev;
    uint8_t status;
    int flags;

    dev = get_pci_device(&bmdma_base);

    flags = cmd & ~0xff;
    cmd &= 0xff;

    switch (cmd) {
    case CMD_READ_DMA:
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
    outb(IDE_BASE + reg_device, 0 | LBA);

    /* Stop any running transfer, clear any pending interrupt */
    outb(bmdma_base + bmreg_cmd, 0);
    outb(bmdma_base + bmreg_status, BM_STS_INTR);

    /* Setup PRDT */
    len = sizeof(*prdt) * prdt_entries;
    guest_prdt = guest_alloc(guest_malloc, len);
    memwrite(guest_prdt, prdt, len);
    outl(bmdma_base + bmreg_prdt, guest_prdt);

    /* ATA DMA command */
    outb(IDE_BASE + reg_nsectors, nb_sectors);

    outb(IDE_BASE + reg_lba_low,    sector & 0xff);
    outb(IDE_BASE + reg_lba_middle, (sector >> 8) & 0xff);
    outb(IDE_BASE + reg_lba_high,   (sector >> 16) & 0xff);

    outb(IDE_BASE + reg_command, cmd);

    /* Start DMA transfer */
    outb(bmdma_base + bmreg_cmd, BM_CMD_START | (from_dev ? BM_CMD_WRITE : 0));

    if (flags & CMDF_ABORT) {
        outb(bmdma_base + bmreg_cmd, 0);
    }

    /* Wait for the DMA transfer to complete */
    do {
        status = inb(bmdma_base + bmreg_status);
    } while ((status & (BM_STS_ACTIVE | BM_STS_INTR)) == BM_STS_ACTIVE);

    g_assert_cmpint(get_irq(IDE_PRIMARY_IRQ), ==, !!(status & BM_STS_INTR));

    /* Check IDE status code */
    assert_bit_set(inb(IDE_BASE + reg_status), DRDY);
    assert_bit_clear(inb(IDE_BASE + reg_status), BSY | DRQ);

    /* Reading the status register clears the IRQ */
    g_assert(!get_irq(IDE_PRIMARY_IRQ));

    /* Stop DMA transfer if still active */
    if (status & BM_STS_ACTIVE) {
        outb(bmdma_base + bmreg_cmd, 0);
    }

    free_pci_device(dev);

    return status;
}

static void test_bmdma_simple_rw(void)
{
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

    buf = g_malloc(len);
    cmpbuf = g_malloc(len);

    /* Write 0x55 pattern to sector 0 */
    memset(buf, 0x55, len);
    memwrite(guest_buf, buf, len);

    status = send_dma_request(CMD_WRITE_DMA, 0, 1, prdt, ARRAY_SIZE(prdt));
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(inb(IDE_BASE + reg_status), DF | ERR);

    /* Write 0xaa pattern to sector 1 */
    memset(buf, 0xaa, len);
    memwrite(guest_buf, buf, len);

    status = send_dma_request(CMD_WRITE_DMA, 1, 1, prdt, ARRAY_SIZE(prdt));
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(inb(IDE_BASE + reg_status), DF | ERR);

    /* Read and verify 0x55 pattern in sector 0 */
    memset(cmpbuf, 0x55, len);

    status = send_dma_request(CMD_READ_DMA, 0, 1, prdt, ARRAY_SIZE(prdt));
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(inb(IDE_BASE + reg_status), DF | ERR);

    memread(guest_buf, buf, len);
    g_assert(memcmp(buf, cmpbuf, len) == 0);

    /* Read and verify 0xaa pattern in sector 1 */
    memset(cmpbuf, 0xaa, len);

    status = send_dma_request(CMD_READ_DMA, 1, 1, prdt, ARRAY_SIZE(prdt));
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(inb(IDE_BASE + reg_status), DF | ERR);

    memread(guest_buf, buf, len);
    g_assert(memcmp(buf, cmpbuf, len) == 0);


    g_free(buf);
    g_free(cmpbuf);
}

static void test_bmdma_short_prdt(void)
{
    uint8_t status;

    PrdtEntry prdt[] = {
        {
            .addr = 0,
            .size = cpu_to_le32(0x10 | PRDT_EOT),
        },
    };

    /* Normal request */
    status = send_dma_request(CMD_READ_DMA, 0, 1,
                              prdt, ARRAY_SIZE(prdt));
    g_assert_cmphex(status, ==, 0);
    assert_bit_clear(inb(IDE_BASE + reg_status), DF | ERR);

    /* Abort the request before it completes */
    status = send_dma_request(CMD_READ_DMA | CMDF_ABORT, 0, 1,
                              prdt, ARRAY_SIZE(prdt));
    g_assert_cmphex(status, ==, 0);
    assert_bit_clear(inb(IDE_BASE + reg_status), DF | ERR);
}

static void test_bmdma_long_prdt(void)
{
    uint8_t status;

    PrdtEntry prdt[] = {
        {
            .addr = 0,
            .size = cpu_to_le32(0x1000 | PRDT_EOT),
        },
    };

    /* Normal request */
    status = send_dma_request(CMD_READ_DMA, 0, 1,
                              prdt, ARRAY_SIZE(prdt));
    g_assert_cmphex(status, ==, BM_STS_ACTIVE | BM_STS_INTR);
    assert_bit_clear(inb(IDE_BASE + reg_status), DF | ERR);

    /* Abort the request before it completes */
    status = send_dma_request(CMD_READ_DMA | CMDF_ABORT, 0, 1,
                              prdt, ARRAY_SIZE(prdt));
    g_assert_cmphex(status, ==, BM_STS_INTR);
    assert_bit_clear(inb(IDE_BASE + reg_status), DF | ERR);
}

static void test_bmdma_no_busmaster(void)
{
    uint8_t status;

    /* No PRDT_EOT, each entry addr 0/size 64k, and in theory qemu shouldn't be
     * able to access it anyway because the Bus Master bit in the PCI command
     * register isn't set. This is complete nonsense, but it used to be pretty
     * good at confusing and occasionally crashing qemu. */
    PrdtEntry prdt[4096] = { };

    status = send_dma_request(CMD_READ_DMA | CMDF_NO_BM, 0, 512,
                              prdt, ARRAY_SIZE(prdt));

    /* Not entirely clear what the expected result is, but this is what we get
     * in practice. At least we want to be aware of any changes. */
    g_assert_cmphex(status, ==, BM_STS_ACTIVE | BM_STS_INTR);
    assert_bit_clear(inb(IDE_BASE + reg_status), DF | ERR);
}

static void test_bmdma_setup(void)
{
    ide_test_start(
        "-drive file=%s,if=ide,serial=%s,cache=writeback "
        "-global ide-hd.ver=%s",
        tmp_path, "testdisk", "version");
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
    uint8_t data;
    uint16_t buf[256];
    int i;
    int ret;

    ide_test_start(
        "-drive file=%s,if=ide,serial=%s,cache=writeback "
        "-global ide-hd.ver=%s",
        tmp_path, "testdisk", "version");

    /* IDENTIFY command on device 0*/
    outb(IDE_BASE + reg_device, 0);
    outb(IDE_BASE + reg_command, CMD_IDENTIFY);

    /* Read in the IDENTIFY buffer and check registers */
    data = inb(IDE_BASE + reg_device);
    g_assert_cmpint(data & DEV, ==, 0);

    for (i = 0; i < 256; i++) {
        data = inb(IDE_BASE + reg_status);
        assert_bit_set(data, DRDY | DRQ);
        assert_bit_clear(data, BSY | DF | ERR);

        ((uint16_t*) buf)[i] = inw(IDE_BASE + reg_data);
    }

    data = inb(IDE_BASE + reg_status);
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
}

static void test_flush(void)
{
    uint8_t data;

    ide_test_start(
        "-drive file=blkdebug::%s,if=ide,cache=writeback",
        tmp_path);

    /* Delay the completion of the flush request until we explicitly do it */
    qmp_discard_response("{'execute':'human-monitor-command', 'arguments': {"
                         " 'command-line':"
                         " 'qemu-io ide0-hd0 \"break flush_to_os A\"'} }");

    /* FLUSH CACHE command on device 0*/
    outb(IDE_BASE + reg_device, 0);
    outb(IDE_BASE + reg_command, CMD_FLUSH_CACHE);

    /* Check status while request is in flight*/
    data = inb(IDE_BASE + reg_status);
    assert_bit_set(data, BSY | DRDY);
    assert_bit_clear(data, DF | ERR | DRQ);

    /* Complete the command */
    qmp_discard_response("{'execute':'human-monitor-command', 'arguments': {"
                         " 'command-line':"
                         " 'qemu-io ide0-hd0 \"resume A\"'} }");

    /* Check registers */
    data = inb(IDE_BASE + reg_device);
    g_assert_cmpint(data & DEV, ==, 0);

    do {
        data = inb(IDE_BASE + reg_status);
    } while (data & BSY);

    assert_bit_set(data, DRDY);
    assert_bit_clear(data, BSY | DF | ERR | DRQ);

    ide_test_quit();
}

static void prepare_blkdebug_script(const char *debug_fn, const char *event)
{
    FILE *debug_file = fopen(debug_fn, "w");
    int ret;

    fprintf(debug_file, "[inject-error]\n");
    fprintf(debug_file, "event = \"%s\"\n", event);
    fprintf(debug_file, "errno = \"5\"\n");
    fprintf(debug_file, "state = \"1\"\n");
    fprintf(debug_file, "immediately = \"off\"\n");
    fprintf(debug_file, "once = \"on\"\n");

    fprintf(debug_file, "[set-state]\n");
    fprintf(debug_file, "event = \"%s\"\n", event);
    fprintf(debug_file, "new_state = \"2\"\n");
    fflush(debug_file);
    g_assert(!ferror(debug_file));

    ret = fclose(debug_file);
    g_assert(ret == 0);
}

static void test_retry_flush(void)
{
    uint8_t data;
    const char *s;
    QDict *response;

    prepare_blkdebug_script(debug_path, "flush_to_disk");

    ide_test_start(
        "-vnc none "
        "-drive file=blkdebug:%s:%s,if=ide,cache=writeback,rerror=stop,werror=stop",
        debug_path, tmp_path);

    /* FLUSH CACHE command on device 0*/
    outb(IDE_BASE + reg_device, 0);
    outb(IDE_BASE + reg_command, CMD_FLUSH_CACHE);

    /* Check status while request is in flight*/
    data = inb(IDE_BASE + reg_status);
    assert_bit_set(data, BSY | DRDY);
    assert_bit_clear(data, DF | ERR | DRQ);

    for (;; response = NULL) {
        response = qmp_receive();
        if ((qdict_haskey(response, "event")) &&
            (strcmp(qdict_get_str(response, "event"), "STOP") == 0)) {
            QDECREF(response);
            break;
        }
        QDECREF(response);
    }

    /* Complete the command */
    s = "{'execute':'cont' }";
    qmp_discard_response(s);

    /* Check registers */
    data = inb(IDE_BASE + reg_device);
    g_assert_cmpint(data & DEV, ==, 0);

    do {
        data = inb(IDE_BASE + reg_status);
    } while (data & BSY);

    assert_bit_set(data, DRDY);
    assert_bit_clear(data, BSY | DF | ERR | DRQ);

    ide_test_quit();
}

static void test_flush_nodev(void)
{
    ide_test_start("");

    /* FLUSH CACHE command on device 0*/
    outb(IDE_BASE + reg_device, 0);
    outb(IDE_BASE + reg_command, CMD_FLUSH_CACHE);

    /* Just testing that qemu doesn't crash... */

    ide_test_quit();
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
    qtest_add_func("/ide/bmdma/long_prdt", test_bmdma_long_prdt);
    qtest_add_func("/ide/bmdma/no_busmaster", test_bmdma_no_busmaster);
    qtest_add_func("/ide/bmdma/teardown", test_bmdma_teardown);

    qtest_add_func("/ide/flush", test_flush);
    qtest_add_func("/ide/flush_nodev", test_flush_nodev);

    qtest_add_func("/ide/retry/flush", test_retry_flush);

    ret = g_test_run();

    /* Cleanup */
    unlink(tmp_path);
    unlink(debug_path);

    return ret;
}
