/*
 * QEMU Malta board support
 *
 * Copyright (c) 2006 Aurelien Jarno
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

#define BIOS_FILENAME           "mips_bios.bin"
#ifdef MIPS_HAS_MIPS64
#define INITRD_LOAD_ADDR 	(uint64_t)0x80800000
#define ENVP_ADDR        	(uint64_t)0x80002000
#else
#define INITRD_LOAD_ADDR 	(uint32_t)0x80800000
#define ENVP_ADDR        	(uint32_t)0x80002000
#endif

#define VIRT_TO_PHYS_ADDEND 	(-((uint64_t)(uint32_t)0x80000000))

#define ENVP_NB_ENTRIES	 	16
#define ENVP_ENTRY_SIZE	 	256


extern FILE *logfile;

typedef struct {
    uint32_t leds;
    uint32_t brk;
    uint32_t gpout;
    uint32_t i2coe;
    uint32_t i2cout;
    uint32_t i2csel;
} MaltaFPGAState;

static PITState *pit;

static void pic_irq_request(void *opaque, int level)
{
    CPUState *env = first_cpu;
    if (level) {
        env->CP0_Cause |= 0x00000400;
        cpu_interrupt(env, CPU_INTERRUPT_HARD);
    } else {
	env->CP0_Cause &= ~0x00000400;
        cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
    }
}

/* MIPS ASCII display */
#define ASCII_DISPLAY_POS_BASE 0x1f000418
static char mips_display_text[8];
static CharDriverState *mips_display;
static void malta_display_writel(target_phys_addr_t addr, uint32_t val)
{
    if (mips_display == 0) {
        mips_display = qemu_chr_open("vc");
        qemu_chr_printf(mips_display, "MIPS Display\r\n");
        qemu_chr_printf(mips_display, "+----------+\r\n");
    }
    if (addr >= ASCII_DISPLAY_POS_BASE && addr < ASCII_DISPLAY_POS_BASE + 4 * 2 * 8) {
        unsigned index = (addr - ASCII_DISPLAY_POS_BASE) / 4 / 2;
        mips_display_text[index] = (char)val;
        qemu_chr_printf(mips_display, "\r| %-8.8s |", mips_display_text);
    }
}

/* Malta FPGA */
static uint32_t malta_fpga_readl(void *opaque, target_phys_addr_t addr)
{
    MaltaFPGAState *s = opaque;
    uint32_t val = 0;

    switch (addr & 0xffffff) {

    /* SWITCH Register */
    case 0x000200:
        val = 0x00000000;		/* All switches closed */
	break;

    /* STATUS Register */
    case 0x000208:
#ifdef TARGET_WORDS_BIGENDIAN
	val = 0x00000012;
#else
	val = 0x00000010;
#endif
        break;

    /* JMPRS Register */
    case 0x000210:
        val = 0x00;
	break;

    /* LEDBAR Register */
    case 0x000408:
        val = s->leds;
	break;

    /* BRKRES Register */
    case 0x000508:
        val = s->brk;
	break;

    /* GPOUT Register */
    case 0x000a00:
        val = s->gpout;
	break;

    /* XXX: implement a real I2C controller */

    /* GPINP Register */
    case 0x000a08:
	/* IN = OUT until a real I2C control is implemented */
	if (s->i2csel)
            val = s->i2cout;
        else
            val = 0x00;
	break;

    /* I2CINP Register */
    case 0x000b00:
        val = 0x00000003;
	break;

    /* I2COE Register */
    case 0x000b08:
        val = s->i2coe;
	break;

    /* I2COUT Register */
    case 0x000b10:
        val = s->i2cout;
	break;

    /* I2CSEL Register */
    case 0x000b18:
        val = s->i2cout;
	break;

    default:
#if 0
        printf ("malta_fpga_read: Bad register offset 0x%x\n", (int)addr);
#endif
	break;
    }
    return val;
}

static void malta_fpga_writel(void *opaque, target_phys_addr_t addr,
                             uint32_t val)
{
    MaltaFPGAState *s = opaque;

    switch (addr & 0xffffff) {

    /* SWITCH Register */
    case 0x000200:
        break;

    /* JMPRS Register */
    case 0x000210:
        break;

    /* LEDBAR Register */
    /* XXX: implement a 8-LED array */
    case 0x000408:
        s->leds = val & 0xff;
	break;

    /* ASCIIWORD, ASCIIPOS0 to ASCIIPOS7 Registers */
    /* XXX: implement a 8-character ASCII display */
    case 0x000410:
    case 0x000418:
    case 0x000420:
    case 0x000428:
    case 0x000430:
    case 0x000438:
    case 0x000440:
    case 0x000448:
    case 0x000450:
        malta_display_writel(addr, val);
        break;

    /* SOFTRES Register */
    case 0x000500:
	if (val == 0x42)
            qemu_system_reset_request ();
	break;

    /* BRKRES Register */
    case 0x000508:
        s->brk = val & 0xff;
	break;

    /* GPOUT Register */
    case 0x000a00:
        s->gpout = val & 0xff;
	break;

    /* I2COE Register */
    case 0x000b08:
        s->i2coe = val & 0x03;
	break;

    /* I2COUT Register */
    case 0x000b10:
        s->i2cout = val & 0x03;
	break;

    /* I2CSEL Register */
    case 0x000b18:
        s->i2cout = val & 0x01;
	break;

    default:
#if 0
        printf ("malta_fpga_write: Bad register offset 0x%x\n", (int)addr);
#endif
        break;
    }
}

static CPUReadMemoryFunc *malta_fpga_read[] = {
   malta_fpga_readl,
   malta_fpga_readl,
   malta_fpga_readl
};

static CPUWriteMemoryFunc *malta_fpga_write[] = {
   malta_fpga_writel,
   malta_fpga_writel,
   malta_fpga_writel
};

void malta_fpga_reset(void *opaque)
{
    MaltaFPGAState *s = opaque;

    s->leds   = 0x00;
    s->brk    = 0x0a;
    s->gpout  = 0x00;
    s->i2coe  = 0x0;
    s->i2cout = 0x3;
    s->i2csel = 0x1;
}

MaltaFPGAState *malta_fpga_init(target_phys_addr_t base)
{
    MaltaFPGAState *s;
    int malta;

    s = (MaltaFPGAState *)qemu_mallocz(sizeof(MaltaFPGAState));
    malta_fpga_reset(s);

    malta = cpu_register_io_memory(0, malta_fpga_read,
                                   malta_fpga_write, s);
    cpu_register_physical_memory(base, 0xc0000, malta);

    qemu_register_reset(malta_fpga_reset, s);

    return s;
}

/* Audio support */
#ifdef HAS_AUDIO
static void audio_init (PCIBus *pci_bus)
{
    struct soundhw *c;
    int audio_enabled = 0;

    for (c = soundhw; !audio_enabled && c->name; ++c) {
        audio_enabled = c->enabled;
    }

    if (audio_enabled) {
        AudioState *s;

        s = AUD_init ();
        if (s) {
            for (c = soundhw; c->name; ++c) {
                if (c->enabled) {
                    if (c->isa) {
                        fprintf(stderr, "qemu: Unsupported Sound Card: %s\n", c->name);
                        exit(1);
                    }
                    else {
                        if (pci_bus) {
                            c->init.init_pci (pci_bus, s);
                        }
                    }
                }
            }
        }
    }
}
#endif

/* Network support */
static void network_init (PCIBus *pci_bus)
{
    int i;
    NICInfo *nd;

    for(i = 0; i < nb_nics; i++) {
        nd = &nd_table[i];
        if (!nd->model) {
            nd->model = "pcnet";
        }
        if (i == 0  && strcmp(nd->model, "pcnet") == 0) {
            /* The malta board has a PCNet card using PCI SLOT 11 */
            pci_nic_init(pci_bus, nd, 88);
	} else {
	    pci_nic_init(pci_bus, nd, -1);
        }
    }
}

/* ROM and pseudo bootloader

   The following code implements a very very simple bootloader. It first
   loads the registers a0 to a3 to the values expected by the OS, and 
   then jump at the kernel address.

   The bootloader should pass the locations of the kernel arguments and 
   environment variables tables. Those tables contain the 32-bit address
   of NULL terminated strings. The environment variables table should be
   terminated by a NULL address.

   For a simpler implementation, the number of kernel arguments is fixed
   to two (the name of the kernel and the command line), and the two 
   tables are actually the same one.

   The registers a0 to a3 should contain the following values:
     a0 - number of kernel arguments
     a1 - 32-bit address of the kernel arguments table
     a2 - 32-bit address of the environment variables table
     a3 - RAM size in bytes
*/

static void write_bootloader (CPUState *env, unsigned long bios_offset, int64_t kernel_addr)
{
    uint32_t *p;

    /* Small bootloader */
    p = (uint32_t *) (phys_ram_base + bios_offset);
    stl_raw(p++, 0x0bf00010);                               /* j 0x1fc00040 */
    stl_raw(p++, 0x00000000);                               /* nop */

    /* Second part of the bootloader */
    p = (uint32_t *) (phys_ram_base + bios_offset + 0x040);
    stl_raw(p++, 0x3c040000); 				    /* lui a0, 0 */ 
    stl_raw(p++, 0x34840002);                               /* ori a0, a0, 2 */
    stl_raw(p++, 0x3c050000 | ((ENVP_ADDR) >> 16));         /* lui a1, high(ENVP_ADDR) */ 
    stl_raw(p++, 0x34a50000 | ((ENVP_ADDR) & 0xffff));      /* ori a1, a0, low(ENVP_ADDR) */
    stl_raw(p++, 0x3c060000 | ((ENVP_ADDR + 8) >> 16));     /* lui a2, high(ENVP_ADDR + 8) */ 
    stl_raw(p++, 0x34c60000 | ((ENVP_ADDR + 8) & 0xffff));  /* ori a2, a2, low(ENVP_ADDR + 8) */
    stl_raw(p++, 0x3c070000 | ((env->ram_size) >> 16));     /* lui a3, high(env->ram_size) */ 
    stl_raw(p++, 0x34e70000 | ((env->ram_size) & 0xffff));  /* ori a3, a3, low(env->ram_size) */
    stl_raw(p++, 0x3c1f0000 | ((kernel_addr) >> 16));       /* lui ra, high(kernel_addr) */;
    stl_raw(p++, 0x37ff0000 | ((kernel_addr) & 0xffff));    /* ori ra, ra, low(kernel_addr) */
    stl_raw(p++, 0x03e00008);                               /* jr ra */
    stl_raw(p++, 0x00000000);                               /* nop */
}

static void prom_set(int index, const char *string, ...)
{
    va_list ap;
    uint32_t *p;
    uint32_t table_addr;
    char *s;

    if (index >= ENVP_NB_ENTRIES)
      return;

    p = (uint32_t *) (phys_ram_base + ENVP_ADDR + VIRT_TO_PHYS_ADDEND);
    p += index;

    if (string == NULL) {
      stl_raw(p, 0);	  
      return;
    }

    table_addr = ENVP_ADDR + sizeof(uint32_t) * ENVP_NB_ENTRIES + index * ENVP_ENTRY_SIZE;
    s = (char *) (phys_ram_base + VIRT_TO_PHYS_ADDEND + table_addr);

    stl_raw(p, table_addr);

    va_start(ap, string);
    vsnprintf (s, ENVP_ENTRY_SIZE, string, ap);
    va_end(ap);
}

/* Kernel */
static int64_t load_kernel (CPUState *env)
{
    int64_t kernel_addr = 0;
    int index = 0;
    long initrd_size;

    if (load_elf(env->kernel_filename, VIRT_TO_PHYS_ADDEND, &kernel_addr) < 0) {
      fprintf(stderr, "qemu: could not load kernel '%s'\n",
              env->kernel_filename);
      exit(1);
    }

    /* load initrd */
    initrd_size = 0;
    if (env->initrd_filename) {
        initrd_size = load_image(env->initrd_filename,
                                 phys_ram_base + INITRD_LOAD_ADDR + VIRT_TO_PHYS_ADDEND);
        if (initrd_size == (target_ulong) -1) {
            fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                    env->initrd_filename);
            exit(1);
        }
    }

    /* Store command line.  */
    prom_set(index++, env->kernel_filename);
    if (initrd_size > 0)
        prom_set(index++, "rd_start=0x%08x rd_size=%li %s", INITRD_LOAD_ADDR, initrd_size, env->kernel_cmdline);
    else
	prom_set(index++, env->kernel_cmdline);

    /* Setup minimum environment variables */
    prom_set(index++, "memsize");
    prom_set(index++, "%i", env->ram_size);
    prom_set(index++, "modetty0");
    prom_set(index++, "38400n8r");
    prom_set(index++, NULL);

    return kernel_addr;
}

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);

    /* The bootload does not need to be rewritten as it is located in a
       read only location. The kernel location and the arguments table
       location does not change. */
    if (env->kernel_filename)
        load_kernel (env);
}

void mips_malta_init (int ram_size, int vga_ram_size, int boot_device,
                      DisplayState *ds, const char **fd_filename, int snapshot,
                      const char *kernel_filename, const char *kernel_cmdline,
                      const char *initrd_filename)
{
    char buf[1024];
    unsigned long bios_offset;
    int64_t kernel_addr;
    PCIBus *pci_bus;
    CPUState *env;
    RTCState *rtc_state;
    fdctrl_t *floppy_controller;
    MaltaFPGAState *malta_fpga;
    int ret;

    env = cpu_init();
    register_savevm("cpu", 0, 3, cpu_save, cpu_load, env);
    qemu_register_reset(main_cpu_reset, env);

    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, IO_MEM_RAM);

    /* Map the bios at two physical locations, as on the real board */
    bios_offset = ram_size + vga_ram_size;
    cpu_register_physical_memory(0x1e000000LL,
                                 BIOS_SIZE, bios_offset | IO_MEM_ROM);
    cpu_register_physical_memory(0x1fc00000LL,
                                 BIOS_SIZE, bios_offset | IO_MEM_ROM);

    /* Load a BIOS image except if a kernel image has been specified. In
       the later case, just write a small bootloader to the flash 
       location. */
    if (kernel_filename) {
	env->ram_size = ram_size;
	env->kernel_filename = kernel_filename;
	env->kernel_cmdline = kernel_cmdline;
	env->initrd_filename = initrd_filename;
	kernel_addr = load_kernel(env);
	write_bootloader(env, bios_offset, kernel_addr);
    } else {	    
        snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
        ret = load_image(buf, phys_ram_base + bios_offset);
        if ((ret <= 0) && (ret > BIOS_SIZE)) {
            fprintf(stderr, "qemu: Warning, could not load MIPS bios '%s'\n",
  	            buf);
            exit(1);
        }	   
    }

    /* Board ID = 0x420 (Malta Board with CoreLV)
       XXX: theoretically 0x1e000010 should map to flash and 0x1fc00010 should
       map to the board ID. */
    stl_raw(phys_ram_base + bios_offset + 0x10, 0x00000420);

    /* Init internal devices */
    cpu_mips_clock_init(env);
    cpu_mips_irqctrl_init();

    /* FPGA */
    malta_fpga = malta_fpga_init(0x1f000000LL);

    /* Interrupt controller */
    isa_pic = pic_init(pic_irq_request, env);

    /* Northbridge */
    pci_bus = pci_gt64120_init(isa_pic);

    /* Southbridge */
    piix3_init(pci_bus, 80);
    pci_piix3_ide_init(pci_bus, bs_table, 81);
    usb_uhci_init(pci_bus, 82);
    piix4_pm_init(pci_bus, 83);
    pit = pit_init(0x40, 0);
    DMA_init(0);

    /* Super I/O */
    kbd_init();
    rtc_state = rtc_init(0x70, 8);
    serial_init(&pic_set_irq_new, isa_pic, 0x3f8, 4, serial_hds[0]);
    parallel_init(0x378, 7, parallel_hds[0]);
    /* XXX: The floppy controller does not work correctly, something is
       probably wrong */
    floppy_controller = fdctrl_init(6, 2, 0, 0x3f0, fd_table); 

    /* Sound card */
#ifdef HAS_AUDIO
    audio_init(pci_bus);
#endif

    /* Network card */
    network_init(pci_bus);
}

QEMUMachine malta_machine = {
    "malta",
    "MIPS Malta Core LV",
    mips_malta_init,
};
