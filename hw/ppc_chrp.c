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
#define VGABIOS_FILENAME "video.x"
#define NVRAM_SIZE        0x2000

#define KERNEL_LOAD_ADDR 0x01000000
#define INITRD_LOAD_ADDR 0x01800000

/* MacIO devices (mapped inside the MacIO address space): CUDA, DBDMA,
   NVRAM */

static int dbdma_mem_index;
static int cuda_mem_index;
static int ide0_mem_index = -1;
static int ide1_mem_index = -1;
static int openpic_mem_index = -1;
static int heathrow_pic_mem_index = -1;
static int macio_nvram_mem_index = -1;

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

/* macio style NVRAM device */
typedef struct MacIONVRAMState {
    uint8_t data[0x2000];
} MacIONVRAMState;

static void macio_nvram_writeb (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    MacIONVRAMState *s = opaque;
    addr = (addr >> 4) & 0x1fff;
    s->data[addr] = value;
    //    printf("macio_nvram_writeb %04x = %02x\n", addr, value);
}

static uint32_t macio_nvram_readb (void *opaque, target_phys_addr_t addr)
{
    MacIONVRAMState *s = opaque;
    uint32_t value;

    addr = (addr >> 4) & 0x1fff;
    value = s->data[addr];
    //    printf("macio_nvram_readb %04x = %02x\n", addr, value);
    return value;
}

static CPUWriteMemoryFunc *macio_nvram_write[] = {
    &macio_nvram_writeb,
    &macio_nvram_writeb,
    &macio_nvram_writeb,
};

static CPUReadMemoryFunc *macio_nvram_read[] = {
    &macio_nvram_readb,
    &macio_nvram_readb,
    &macio_nvram_readb,
};

static MacIONVRAMState *macio_nvram_init(void)
{
    MacIONVRAMState *s;
    s = qemu_mallocz(sizeof(MacIONVRAMState));
    if (!s)
        return NULL;
    macio_nvram_mem_index = cpu_register_io_memory(0, macio_nvram_read, 
                                                   macio_nvram_write, s);
    return s;
}

static void macio_map(PCIDevice *pci_dev, int region_num, 
                      uint32_t addr, uint32_t size, int type)
{
    if (heathrow_pic_mem_index >= 0) {
        cpu_register_physical_memory(addr + 0x00000, 0x1000, 
                                     heathrow_pic_mem_index);
    }
    cpu_register_physical_memory(addr + 0x08000, 0x1000, dbdma_mem_index);
    cpu_register_physical_memory(addr + 0x16000, 0x2000, cuda_mem_index);
    if (ide0_mem_index >= 0)
        cpu_register_physical_memory(addr + 0x1f000, 0x1000, ide0_mem_index);
    if (ide1_mem_index >= 0)
        cpu_register_physical_memory(addr + 0x20000, 0x1000, ide1_mem_index);
    if (openpic_mem_index >= 0) {
        cpu_register_physical_memory(addr + 0x40000, 0x40000, 
                                     openpic_mem_index);
    }
    if (macio_nvram_mem_index >= 0)
        cpu_register_physical_memory(addr + 0x60000, 0x20000, macio_nvram_mem_index);
}

static void macio_init(PCIBus *bus, int device_id)
{
    PCIDevice *d;

    d = pci_register_device(bus, "macio", sizeof(PCIDevice),
                            -1, NULL, NULL);
    /* Note: this code is strongly inspirated from the corresponding code
       in PearPC */
    d->config[0x00] = 0x6b; // vendor_id
    d->config[0x01] = 0x10;
    d->config[0x02] = device_id;
    d->config[0x03] = device_id >> 8;

    d->config[0x0a] = 0x00; // class_sub = pci2pci
    d->config[0x0b] = 0xff; // class_base = bridge
    d->config[0x0e] = 0x00; // header_type

    d->config[0x3d] = 0x01; // interrupt on pin 1
    
    dbdma_mem_index = cpu_register_io_memory(0, dbdma_read, dbdma_write, NULL);

    pci_register_io_region(d, 0, 0x80000, 
                           PCI_ADDRESS_SPACE_MEM, macio_map);
}

/* UniN device */
static void unin_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
}

static uint32_t unin_readl (void *opaque, target_phys_addr_t addr)
{
    return 0;
}

static CPUWriteMemoryFunc *unin_write[] = {
    &unin_writel,
    &unin_writel,
    &unin_writel,
};

static CPUReadMemoryFunc *unin_read[] = {
    &unin_readl,
    &unin_readl,
    &unin_readl,
};

/* temporary frame buffer OSI calls for the video.x driver. The right
   solution is to modify the driver to use VGA PCI I/Os */
static int vga_osi_call(CPUState *env)
{
    static int vga_vbl_enabled;
    int linesize;
    
    //    printf("osi_call R5=%d\n", env->gpr[5]);

    /* same handler as PearPC, coming from the original MOL video
       driver. */
    switch(env->gpr[5]) {
    case 4:
        break;
    case 28: /* set_vmode */
        if (env->gpr[6] != 1 || env->gpr[7] != 0)
            env->gpr[3] = 1;
        else
            env->gpr[3] = 0;
        break;
    case 29: /* get_vmode_info */
        if (env->gpr[6] != 0) {
            if (env->gpr[6] != 1 || env->gpr[7] != 0) {
                env->gpr[3] = 1;
                break;
            }
        }
        env->gpr[3] = 0; 
        env->gpr[4] = (1 << 16) | 1; /* num_vmodes, cur_vmode */
        env->gpr[5] = (1 << 16) | 0; /* num_depths, cur_depth_mode */
        env->gpr[6] = (graphic_width << 16) | graphic_height; /* w, h */
        env->gpr[7] = 85 << 16; /* refresh rate */
        env->gpr[8] = (graphic_depth + 7) & ~7; /* depth (round to byte) */
        linesize = ((graphic_depth + 7) >> 3) * graphic_width;
        linesize = (linesize + 3) & ~3;
        env->gpr[9] = (linesize << 16) | 0; /* row_bytes, offset */
        break;
    case 31: /* set_video power */
        env->gpr[3] = 0;
        break;
    case 39: /* video_ctrl */
        if (env->gpr[6] == 0 || env->gpr[6] == 1)
            vga_vbl_enabled = env->gpr[6];
        env->gpr[3] = 0;
        break;
    case 47:
        break;
    case 59: /* set_color */
        /* R6 = index, R7 = RGB */
        env->gpr[3] = 0;
        break;
    case 64: /* get color */
        /* R6 = index */
        env->gpr[3] = 0; 
        break;
    case 116: /* set hwcursor */
        /* R6 = x, R7 = y, R8 = visible, R9 = data */
        break;
    default:
        fprintf(stderr, "unsupported OSI call R5=%08x\n", env->gpr[5]);
        break;
    }
    return 1; /* osi_call handled */
}

/* XXX: suppress that */
static void pic_irq_request(void *opaque, int level)
{
}

static uint8_t nvram_chksum(const uint8_t *buf, int n)
{
    int sum, i;
    sum = 0;
    for(i = 0; i < n; i++)
        sum += buf[i];
    return (sum & 0xff) + (sum >> 8);
}

/* set a free Mac OS NVRAM partition */
void pmac_format_nvram_partition(uint8_t *buf, int len)
{
    char partition_name[12] = "wwwwwwwwwwww";
    
    buf[0] = 0x7f; /* free partition magic */
    buf[1] = 0; /* checksum */
    buf[2] = len >> 8;
    buf[3] = len;
    memcpy(buf + 4, partition_name, 12);
    buf[1] = nvram_chksum(buf, 16);
}    

/* PowerPC CHRP hardware initialisation */
static void ppc_chrp_init(int ram_size, int vga_ram_size, int boot_device,
                          DisplayState *ds, const char **fd_filename, 
                          int snapshot,
                          const char *kernel_filename, 
                          const char *kernel_cmdline,
                          const char *initrd_filename,
                          int is_heathrow)
{
    CPUState *env;
    char buf[1024];
    SetIRQFunc *set_irq;
    void *pic;
    m48t59_t *nvram;
    int unin_memory;
    int linux_boot, i;
    unsigned long bios_offset, vga_bios_offset;
    uint32_t kernel_base, kernel_size, initrd_base, initrd_size;
    ppc_def_t *def;
    PCIBus *pci_bus;
    const char *arch_name;
    int vga_bios_size, bios_size;

    linux_boot = (kernel_filename != NULL);

    /* init CPUs */
    env = cpu_init();
    register_savevm("cpu", 0, 3, cpu_save, cpu_load, env);

    /* Register CPU as a 74x/75x */
    /* XXX: CPU model (or PVR) should be provided on command line */
    //    ppc_find_by_name("750gx", &def); // Linux boot OK
    //    ppc_find_by_name("750fx", &def); // Linux boot OK
    /* Linux does not boot on 750cxe (and probably other 750cx based)
     * because it assumes it has 8 IBAT & DBAT pairs as it only have 4.
     */
    //    ppc_find_by_name("750cxe", &def);
    //    ppc_find_by_name("750p", &def);
    //    ppc_find_by_name("740p", &def);
    ppc_find_by_name("750", &def);
    //    ppc_find_by_name("740", &def);
    //    ppc_find_by_name("G3", &def);
    //    ppc_find_by_name("604r", &def);
    //    ppc_find_by_name("604e", &def);
    //    ppc_find_by_name("604", &def);
    if (def == NULL) {
        cpu_abort(env, "Unable to find PowerPC CPU definition\n");
    }
    cpu_ppc_register(env, def);

    /* Set time-base frequency to 100 Mhz */
    cpu_ppc_tb_init(env, 100UL * 1000UL * 1000UL);
    
    env->osi_call = vga_osi_call;

    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, IO_MEM_RAM);

    /* allocate and load BIOS */
    bios_offset = ram_size + vga_ram_size;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
    bios_size = load_image(buf, phys_ram_base + bios_offset);
    if (bios_size < 0 || bios_size > BIOS_SIZE) {
        fprintf(stderr, "qemu: could not load PowerPC bios '%s'\n", buf);
        exit(1);
    }
    bios_size = (bios_size + 0xfff) & ~0xfff;
    cpu_register_physical_memory((uint32_t)(-bios_size), 
                                 bios_size, bios_offset | IO_MEM_ROM);
    
    /* allocate and load VGA BIOS */
    vga_bios_offset = bios_offset + bios_size;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, VGABIOS_FILENAME);
    vga_bios_size = load_image(buf, phys_ram_base + vga_bios_offset + 8);
    if (vga_bios_size < 0) {
        /* if no bios is present, we can still work */
        fprintf(stderr, "qemu: warning: could not load VGA bios '%s'\n", buf);
        vga_bios_size = 0;
    } else {
        /* set a specific header (XXX: find real Apple format for NDRV
           drivers) */
        phys_ram_base[vga_bios_offset] = 'N';
        phys_ram_base[vga_bios_offset + 1] = 'D';
        phys_ram_base[vga_bios_offset + 2] = 'R';
        phys_ram_base[vga_bios_offset + 3] = 'V';
        cpu_to_be32w((uint32_t *)(phys_ram_base + vga_bios_offset + 4), 
                     vga_bios_size);
        vga_bios_size += 8;
    }
    vga_bios_size = (vga_bios_size + 0xfff) & ~0xfff;
    
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

    if (is_heathrow) {
        isa_mem_base = 0x80000000;
        
        /* Register 2 MB of ISA IO space */
        isa_mmio_init(0xfe000000, 0x00200000);

        /* init basic PC hardware */
        pic = heathrow_pic_init(&heathrow_pic_mem_index);
        set_irq = heathrow_pic_set_irq;
        pci_bus = pci_grackle_init(0xfec00000, pic);
        pci_vga_init(pci_bus, ds, phys_ram_base + ram_size, 
                     ram_size, vga_ram_size,
                     vga_bios_offset, vga_bios_size);

        /* XXX: suppress that */
        isa_pic = pic_init(pic_irq_request, NULL);
        
        /* XXX: use Mac Serial port */
        serial_init(&pic_set_irq_new, isa_pic, 0x3f8, 4, serial_hds[0]);
        
        for(i = 0; i < nb_nics; i++) {
            if (!nd_table[i].model)
                nd_table[i].model = "ne2k_pci";
            pci_nic_init(pci_bus, &nd_table[i], -1);
        }
        
        pci_cmd646_ide_init(pci_bus, &bs_table[0], 0);

        /* cuda also initialize ADB */
        cuda_mem_index = cuda_init(set_irq, pic, 0x12);
        
        adb_kbd_init(&adb_bus);
        adb_mouse_init(&adb_bus);
        
        {
            MacIONVRAMState *nvr;
            nvr = macio_nvram_init();
            pmac_format_nvram_partition(nvr->data, 0x2000);
        }

        macio_init(pci_bus, 0x0017);
        
        nvram = m48t59_init(8, 0xFFF04000, 0x0074, NVRAM_SIZE, 59);
        
        arch_name = "HEATHROW";
    } else {
        isa_mem_base = 0x80000000;
        
        /* Register 8 MB of ISA IO space */
        isa_mmio_init(0xf2000000, 0x00800000);
        
        /* UniN init */
        unin_memory = cpu_register_io_memory(0, unin_read, unin_write, NULL);
        cpu_register_physical_memory(0xf8000000, 0x00001000, unin_memory);

        pic = openpic_init(NULL, &openpic_mem_index, 1, &env);
        set_irq = openpic_set_irq;
        pci_bus = pci_pmac_init(pic);
        /* init basic PC hardware */
        pci_vga_init(pci_bus, ds, phys_ram_base + ram_size,
                     ram_size, vga_ram_size,
                     vga_bios_offset, vga_bios_size);

        /* XXX: suppress that */
        isa_pic = pic_init(pic_irq_request, NULL);
        
        /* XXX: use Mac Serial port */
        serial_init(&pic_set_irq_new, isa_pic, 0x3f8, 4, serial_hds[0]);
        
        for(i = 0; i < nb_nics; i++) {
            pci_ne2000_init(pci_bus, &nd_table[i], -1);
        }
        
#if 1
        ide0_mem_index = pmac_ide_init(&bs_table[0], set_irq, pic, 0x13);
        ide1_mem_index = pmac_ide_init(&bs_table[2], set_irq, pic, 0x14);
#else
        pci_cmd646_ide_init(pci_bus, &bs_table[0], 0);
#endif
        /* cuda also initialize ADB */
        cuda_mem_index = cuda_init(set_irq, pic, 0x19);
        
        adb_kbd_init(&adb_bus);
        adb_mouse_init(&adb_bus);
        
        macio_init(pci_bus, 0x0022);
        
        nvram = m48t59_init(8, 0xFFF04000, 0x0074, NVRAM_SIZE, 59);
        
        arch_name = "MAC99";
    }

    if (usb_enabled) {
        usb_ohci_init(pci_bus, 3, -1);
    }

    if (graphic_depth != 15 && graphic_depth != 32 && graphic_depth != 8)
        graphic_depth = 15;

    PPC_NVRAM_set_params(nvram, NVRAM_SIZE, arch_name, ram_size, boot_device,
                         kernel_base, kernel_size,
                         kernel_cmdline,
                         initrd_base, initrd_size,
                         /* XXX: need an option to load a NVRAM image */
                         0,
                         graphic_width, graphic_height, graphic_depth);
    /* No PCI init: the BIOS will do it */

    /* Special port to get debug messages from Open-Firmware */
    register_ioport_write(0x0F00, 4, 1, &PPC_debug_write, NULL);
}

static void ppc_core99_init(int ram_size, int vga_ram_size, int boot_device,
                            DisplayState *ds, const char **fd_filename, 
                            int snapshot,
                            const char *kernel_filename, 
                            const char *kernel_cmdline,
                            const char *initrd_filename)
{
    ppc_chrp_init(ram_size, vga_ram_size, boot_device,
                  ds, fd_filename, snapshot,
                  kernel_filename, kernel_cmdline,
                  initrd_filename, 0);
}
    
static void ppc_heathrow_init(int ram_size, int vga_ram_size, int boot_device,
                              DisplayState *ds, const char **fd_filename, 
                              int snapshot,
                              const char *kernel_filename, 
                              const char *kernel_cmdline,
                              const char *initrd_filename)
{
    ppc_chrp_init(ram_size, vga_ram_size, boot_device,
                  ds, fd_filename, snapshot,
                  kernel_filename, kernel_cmdline,
                  initrd_filename, 1);
}

QEMUMachine core99_machine = {
    "mac99",
    "Mac99 based PowerMAC",
    ppc_core99_init,
};

QEMUMachine heathrow_machine = {
    "g3bw",
    "Heathrow based PowerMAC",
    ppc_heathrow_init,
};
