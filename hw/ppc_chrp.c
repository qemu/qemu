/*
 * QEMU PPC CHRP/PMAC hardware System Emulator
 * 
 * Copyright (c) 2004 Fabrice Bellard
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
#include "vl.h"

#define BIOS_FILENAME "ppc_rom.bin"
#define NVRAM_SIZE        0x2000

#define KERNEL_LOAD_ADDR 0x01000000
#define INITRD_LOAD_ADDR 0x01800000

/* MacIO devices (mapped inside the MacIO address space): CUDA, DBDMA,
   NVRAM (not implemented).  */

static int dbdma_mem_index;
static int cuda_mem_index;
static int ide0_mem_index;
static int ide1_mem_index;

/* DBDMA: currently no op - should suffice right now */

static void dbdma_writeb (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    printf("%s: 0x%08x <= 0x%08x\n", __func__, addr, value);
}

static void dbdma_writew (void *opaque, target_phys_addr_t addr, uint32_t value)
{
}

static void dbdma_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
}

static uint32_t dbdma_readb (void *opaque, target_phys_addr_t addr)
{
    printf("%s: 0x%08x => 0x00000000\n", __func__, addr);
    return 0;
}

static uint32_t dbdma_readw (void *opaque, target_phys_addr_t addr)
{
    return 0;
}

static uint32_t dbdma_readl (void *opaque, target_phys_addr_t addr)
{
    return 0;
}

static CPUWriteMemoryFunc *dbdma_write[] = {
    &dbdma_writeb,
    &dbdma_writew,
    &dbdma_writel,
};

static CPUReadMemoryFunc *dbdma_read[] = {
    &dbdma_readb,
    &dbdma_readw,
    &dbdma_readl,
};

static void macio_map(PCIDevice *pci_dev, int region_num, 
                      uint32_t addr, uint32_t size, int type)
{
    cpu_register_physical_memory(addr + 0x08000, 0x1000, dbdma_mem_index);
    cpu_register_physical_memory(addr + 0x16000, 0x2000, cuda_mem_index);
    cpu_register_physical_memory(addr + 0x1f000, 0x1000, ide0_mem_index);
    cpu_register_physical_memory(addr + 0x20000, 0x1000, ide1_mem_index);
}

static void macio_init(void)
{
    PCIDevice *d;

    d = pci_register_device("macio", sizeof(PCIDevice),
                            0, -1, 
                            NULL, NULL);
    /* Note: this code is strongly inspirated from the corresponding code
       in PearPC */
    d->config[0x00] = 0x6b; // vendor_id
    d->config[0x01] = 0x10;
    d->config[0x02] = 0x22;
    d->config[0x03] = 0x00;

    d->config[0x0a] = 0x00; // class_sub = pci2pci
    d->config[0x0b] = 0xff; // class_base = bridge
    d->config[0x0e] = 0x00; // header_type

    d->config[0x3d] = 0x01; // interrupt on pin 1
    
    dbdma_mem_index = cpu_register_io_memory(0, dbdma_read, dbdma_write, NULL);

    pci_register_io_region(d, 0, 0x80000, 
                           PCI_ADDRESS_SPACE_MEM, macio_map);
}

/* PowerPC PREP hardware initialisation */
void ppc_chrp_init(int ram_size, int vga_ram_size, int boot_device,
		   DisplayState *ds, const char **fd_filename, int snapshot,
		   const char *kernel_filename, const char *kernel_cmdline,
		   const char *initrd_filename)
{
    char buf[1024];
    openpic_t *openpic;
    m48t59_t *nvram;
    int PPC_io_memory;
    int ret, linux_boot, i, fd;
    unsigned long bios_offset;
    uint32_t kernel_base, kernel_size, initrd_base, initrd_size;
    
    linux_boot = (kernel_filename != NULL);

    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, IO_MEM_RAM);

    /* allocate and load BIOS */
    bios_offset = ram_size + vga_ram_size;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
    ret = load_image(buf, phys_ram_base + bios_offset);
    if (ret != BIOS_SIZE) {
        fprintf(stderr, "qemu: could not load PPC PREP bios '%s'\n", buf);
        exit(1);
    }
    cpu_register_physical_memory((uint32_t)(-BIOS_SIZE), 
                                 BIOS_SIZE, bios_offset | IO_MEM_ROM);
    cpu_single_env->nip = 0xfffffffc;

    if (linux_boot) {
        kernel_base = KERNEL_LOAD_ADDR;
        /* now we can load the kernel */
        kernel_size = load_image(kernel_filename, phys_ram_base + kernel_base);
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n", 
                    kernel_filename);
            exit(1);
        }
        /* load initrd */
        if (initrd_filename) {
            initrd_base = INITRD_LOAD_ADDR;
            initrd_size = load_image(initrd_filename,
                                     phys_ram_base + initrd_base);
            if (initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initial ram disk '%s'\n", 
                        initrd_filename);
                exit(1);
            }
        } else {
            initrd_base = 0;
            initrd_size = 0;
        }
        boot_device = 'm';
    } else {
        kernel_base = 0;
        kernel_size = 0;
        initrd_base = 0;
        initrd_size = 0;
    }
    /* Register CPU as a 74x/75x */
    cpu_ppc_register(cpu_single_env, 0x00080000);
    /* Set time-base frequency to 100 Mhz */
    cpu_ppc_tb_init(cpu_single_env, 100UL * 1000UL * 1000UL);

    isa_mem_base = 0x80000000;
    pci_pmac_init();

    /* Register 8 MB of ISA IO space */
    PPC_io_memory = cpu_register_io_memory(0, PPC_io_read, PPC_io_write, NULL);
    cpu_register_physical_memory(0xF2000000, 0x00800000, PPC_io_memory);

    /* init basic PC hardware */
    vga_initialize(ds, phys_ram_base + ram_size, ram_size, 
                   vga_ram_size, 1);
    openpic = openpic_init(0x00000000, 0xF0000000, 1);

    /* XXX: suppress that */
    pic_init();

    /* XXX: use Mac Serial port */
    fd = serial_open_device();
    serial_init(0x3f8, 4, fd);

    for(i = 0; i < nb_nics; i++) {
        pci_ne2000_init(&nd_table[i]);
    }

    ide0_mem_index = pmac_ide_init(&bs_table[0], openpic, 0x13);
    ide1_mem_index = pmac_ide_init(&bs_table[2], openpic, 0x13);

    /* cuda also initialize ADB */
    cuda_mem_index = cuda_init(openpic, 0x19);

    adb_kbd_init(&adb_bus);
    adb_mouse_init(&adb_bus);
    
    macio_init();

    nvram = m48t59_init(8, 0xFFF04000, 0x0074, NVRAM_SIZE);
    
    if (graphic_depth != 15 && graphic_depth != 32 && graphic_depth != 8)
        graphic_depth = 15;

    PPC_NVRAM_set_params(nvram, NVRAM_SIZE, "CHRP", ram_size, boot_device,
                         kernel_base, kernel_size,
                         kernel_cmdline,
                         initrd_base, initrd_size,
                         /* XXX: need an option to load a NVRAM image */
                         0,
                         graphic_width, graphic_height, graphic_depth);
    /* No PCI init: the BIOS will do it */
}
