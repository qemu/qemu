/*
 * QEMU PowerMac emulation shared definitions and prototypes
 *
 * Copyright (c) 2004-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
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
#if !defined(__PPC_MAC_H__)
#define __PPC_MAC_H__

/* SMP is not enabled, for now */
#define MAX_CPUS 1

#define BIOS_FILENAME "ppc_rom.bin"
#define VGABIOS_FILENAME "video.x"
#define NVRAM_SIZE        0x2000
#define PROM_FILENAME    "openbios-ppc"
#define PROM_ADDR         0xfff00000

#define KERNEL_LOAD_ADDR 0x01000000
#define INITRD_LOAD_ADDR 0x01800000

/* DBDMA */
void dbdma_init (int *dbdma_mem_index);

/* Cuda */
void cuda_init (int *cuda_mem_index, qemu_irq irq);

/* MacIO */
void macio_init (PCIBus *bus, int device_id, int is_oldworld, int pic_mem_index,
                 int dbdma_mem_index, int cuda_mem_index, void *nvram,
                 int nb_ide, int *ide_mem_index);

/* NewWorld PowerMac IDE */
int pmac_ide_init (BlockDriverState **hd_table, qemu_irq irq);

/* Heathrow PIC */
qemu_irq *heathrow_pic_init(int *pmem_index,
                            int nb_cpus, qemu_irq **irqs);

/* Grackle PCI */
PCIBus *pci_grackle_init(uint32_t base, qemu_irq *pic);

/* UniNorth PCI */
PCIBus *pci_pmac_init(qemu_irq *pic);

/* Mac NVRAM */
typedef struct MacIONVRAMState MacIONVRAMState;

MacIONVRAMState *macio_nvram_init (int *mem_index, target_phys_addr_t size);
void macio_nvram_map (void *opaque, target_phys_addr_t mem_base);
void pmac_format_nvram_partition (MacIONVRAMState *nvr, int len);
uint32_t macio_nvram_read (void *opaque, uint32_t addr);
void macio_nvram_write (void *opaque, uint32_t addr, uint32_t val);

/* adb.c */

#define MAX_ADB_DEVICES 16

#define ADB_MAX_OUT_LEN 16

typedef struct ADBDevice ADBDevice;

/* buf = NULL means polling */
typedef int ADBDeviceRequest(ADBDevice *d, uint8_t *buf_out,
                              const uint8_t *buf, int len);
typedef int ADBDeviceReset(ADBDevice *d);

struct ADBDevice {
    struct ADBBusState *bus;
    int devaddr;
    int handler;
    ADBDeviceRequest *devreq;
    ADBDeviceReset *devreset;
    void *opaque;
};

typedef struct ADBBusState {
    ADBDevice devices[MAX_ADB_DEVICES];
    int nb_devices;
    int poll_index;
} ADBBusState;

int adb_request(ADBBusState *s, uint8_t *buf_out,
                const uint8_t *buf, int len);
int adb_poll(ADBBusState *s, uint8_t *buf_out);

ADBDevice *adb_register_device(ADBBusState *s, int devaddr,
                               ADBDeviceRequest *devreq,
                               ADBDeviceReset *devreset,
                               void *opaque);
void adb_kbd_init(ADBBusState *bus);
void adb_mouse_init(ADBBusState *bus);

extern ADBBusState adb_bus;

/* openpic.c */
/* OpenPIC have 5 outputs per CPU connected and one IRQ out single output */
enum {
    OPENPIC_OUTPUT_INT = 0, /* IRQ                       */
    OPENPIC_OUTPUT_CINT,    /* critical IRQ              */
    OPENPIC_OUTPUT_MCK,     /* Machine check event       */
    OPENPIC_OUTPUT_DEBUG,   /* Inconditional debug event */
    OPENPIC_OUTPUT_RESET,   /* Core reset event          */
    OPENPIC_OUTPUT_NB,
};
qemu_irq *openpic_init (PCIBus *bus, int *pmem_index, int nb_cpus,
                        qemu_irq **irqs, qemu_irq irq_out);

#endif /* !defined(__PPC_MAC_H__) */
