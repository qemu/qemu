/*
 * QEMU/MIPS pseudo-board
 *
 * emulates a simple machine with ISA-like bus.
 * ISA IO space mapped to the 0x14000000 (PHYS) and
 * ISA memory at the 0x10000000 (PHYS, 16Mb in size).
 * All peripherial devices are attached to this "bus" with
 * the standard PC ISA addresses.
*/
#include "vl.h"

#ifdef TARGET_WORDS_BIGENDIAN
#define BIOS_FILENAME "mips_bios.bin"
#else
#define BIOS_FILENAME "mipsel_bios.bin"
#endif

#ifdef MIPS_HAS_MIPS64
#define INITRD_LOAD_ADDR (int64_t)(int32_t)0x80800000
#else
#define INITRD_LOAD_ADDR (int32_t)0x80800000
#endif

#define VIRT_TO_PHYS_ADDEND (-((int64_t)(int32_t)0x80000000))

static const int ide_iobase[2] = { 0x1f0, 0x170 };
static const int ide_iobase2[2] = { 0x3f6, 0x376 };
static const int ide_irq[2] = { 14, 15 };

static int serial_io[MAX_SERIAL_PORTS] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
static int serial_irq[MAX_SERIAL_PORTS] = { 4, 3, 4, 3 };

extern FILE *logfile;

static PITState *pit; /* PIT i8254 */

/*i8254 PIT is attached to the IRQ0 at PIC i8259 */
/*The PIC is attached to the MIPS CPU INT0 pin */
static void pic_irq_request(void *opaque, int level)
{
    cpu_mips_irq_request(opaque, 2, level);
}

static void mips_qemu_writel (void *opaque, target_phys_addr_t addr,
			      uint32_t val)
{
    if ((addr & 0xffff) == 0 && val == 42)
        qemu_system_reset_request ();
    else if ((addr & 0xffff) == 4 && val == 42)
        qemu_system_shutdown_request ();
}

static uint32_t mips_qemu_readl (void *opaque, target_phys_addr_t addr)
{
    return 0;
}

static CPUWriteMemoryFunc *mips_qemu_write[] = {
    &mips_qemu_writel,
    &mips_qemu_writel,
    &mips_qemu_writel,
};

static CPUReadMemoryFunc *mips_qemu_read[] = {
    &mips_qemu_readl,
    &mips_qemu_readl,
    &mips_qemu_readl,
};

static int mips_qemu_iomemtype = 0;

void load_kernel (CPUState *env, int ram_size, const char *kernel_filename,
		  const char *kernel_cmdline,
		  const char *initrd_filename)
{
    int64_t entry = 0;
    long kernel_size, initrd_size;

    kernel_size = load_elf(kernel_filename, VIRT_TO_PHYS_ADDEND, &entry);
    if (kernel_size >= 0) {
        if ((entry & ~0x7fffffffULL) == 0x80000000)
            entry = (int32_t)entry;
        env->PC = entry;
    } else {
        fprintf(stderr, "qemu: could not load kernel '%s'\n",
                kernel_filename);
        exit(1);
    }

    /* load initrd */
    initrd_size = 0;
    if (initrd_filename) {
        initrd_size = load_image(initrd_filename,
                                 phys_ram_base + INITRD_LOAD_ADDR + VIRT_TO_PHYS_ADDEND);
        if (initrd_size == (target_ulong) -1) {
            fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                    initrd_filename);
            exit(1);
        }
    }

    /* Store command line.  */
    if (initrd_size > 0) {
        int ret;
        ret = sprintf(phys_ram_base + (16 << 20) - 256,
                      "rd_start=0x" TLSZ " rd_size=%li ",
                      INITRD_LOAD_ADDR,
                      initrd_size);
        strcpy (phys_ram_base + (16 << 20) - 256 + ret, kernel_cmdline);
    }
    else {
        strcpy (phys_ram_base + (16 << 20) - 256, kernel_cmdline);
    }

    *(int32_t *)(phys_ram_base + (16 << 20) - 260) = tswap32 (0x12345678);
    *(int32_t *)(phys_ram_base + (16 << 20) - 264) = tswap32 (ram_size);
}

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);

    if (env->kernel_filename)
        load_kernel (env, env->ram_size, env->kernel_filename,
                     env->kernel_cmdline, env->initrd_filename);
}

void mips_r4k_init (int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename)
{
    char buf[1024];
    unsigned long bios_offset;
    int bios_size;
    CPUState *env;
    static RTCState *rtc_state;
    int i;

    env = cpu_init();
    register_savevm("cpu", 0, 3, cpu_save, cpu_load, env);
    qemu_register_reset(main_cpu_reset, env);

    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, IO_MEM_RAM);

    if (!mips_qemu_iomemtype) {
        mips_qemu_iomemtype = cpu_register_io_memory(0, mips_qemu_read,
						     mips_qemu_write, NULL);
    }
    cpu_register_physical_memory(0x1fbf0000, 0x10000, mips_qemu_iomemtype);

    /* Try to load a BIOS image. If this fails, we continue regardless,
       but initialize the hardware ourselves. When a kernel gets
       preloaded we also initialize the hardware, since the BIOS wasn't
       run. */
    bios_offset = ram_size + vga_ram_size;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
    bios_size = load_image(buf, phys_ram_base + bios_offset);
    if ((bios_size > 0) && (bios_size <= BIOS_SIZE)) {
	cpu_register_physical_memory(0x1fc00000,
				     BIOS_SIZE, bios_offset | IO_MEM_ROM);
    } else {
	/* not fatal */
        fprintf(stderr, "qemu: Warning, could not load MIPS bios '%s'\n",
		buf);
    }

    if (kernel_filename) {
        load_kernel (env, ram_size, kernel_filename, kernel_cmdline,
		     initrd_filename);
	env->ram_size = ram_size;
	env->kernel_filename = kernel_filename;
	env->kernel_cmdline = kernel_cmdline;
	env->initrd_filename = initrd_filename;
    }

    /* Init CPU internal devices */
    cpu_mips_clock_init(env);
    cpu_mips_irqctrl_init();

    rtc_state = rtc_init(0x70, 8);

    /* Register 64 KB of ISA IO space at 0x14000000 */
    isa_mmio_init(0x14000000, 0x00010000);
    isa_mem_base = 0x10000000;

    isa_pic = pic_init(pic_irq_request, env);
    pit = pit_init(0x40, 0);

    for(i = 0; i < MAX_SERIAL_PORTS; i++) {
        if (serial_hds[i]) {
            serial_init(&pic_set_irq_new, isa_pic,
                        serial_io[i], serial_irq[i], serial_hds[i]);
        }
    }

    isa_vga_init(ds, phys_ram_base + ram_size, ram_size, 
                 vga_ram_size);

    if (nd_table[0].vlan) {
        if (nd_table[0].model == NULL
            || strcmp(nd_table[0].model, "ne2k_isa") == 0) {
            isa_ne2000_init(0x300, 9, &nd_table[0]);
        } else {
            fprintf(stderr, "qemu: Unsupported NIC: %s\n", nd_table[0].model);
            exit (1);
        }
    }

    for(i = 0; i < 2; i++)
        isa_ide_init(ide_iobase[i], ide_iobase2[i], ide_irq[i],
                     bs_table[2 * i], bs_table[2 * i + 1]);
}

QEMUMachine mips_machine = {
    "mips",
    "mips r4k platform",
    mips_r4k_init,
};
