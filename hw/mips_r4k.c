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
#include "ar7.h"                /* ar7_init */
#include "mips_display.h"       /* mips_display_init */
#include "pflash.h"             /* pflash_amd_register, ... */

#define BIOS_FILENAME "mips_bios.bin"

#define KERNEL_LOAD_ADDR 0x80010000
//~ #define KERNEL_LOAD_ADDR 0x80040000
#define INITRD_LOAD_ADDR 0x80800000

#define VIRT_TO_PHYS_ADDEND (-0x80000000LL)

//~ #define MIPS_CPS (100 * 1000 * 1000)
#define MIPS_CPS (150 * 1000 * 1000 / 2)

static const int ide_iobase[2] = { 0x1f0, 0x170 };
static const int ide_iobase2[2] = { 0x3f6, 0x376 };
static const int ide_irq[2] = { 14, 15 };

extern FILE *logfile;

static PITState *pit; /* PIT i8254 */

static int bigendian;

/*i8254 PIT is attached to the IRQ0 at PIC i8259 */
/*The PIC is attached to the MIPS CPU INT0 pin */
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

/*------------------------------------------*/

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
    if (kernel_size >= 0)
        env->PC = entry;
    else {
        kernel_size = load_image(kernel_filename,
                                 phys_ram_base + KERNEL_LOAD_ADDR + VIRT_TO_PHYS_ADDEND);
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
        env->PC = KERNEL_LOAD_ADDR;
    }

    /* Set SP (needed for some kernels) - normally set by bootloader. */
    env->gpr[29] = (env->PC + (kernel_size & 0xfffffffc)) + 0x1000;

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
                      "rd_start=0x%08x rd_size=%li ",
                      INITRD_LOAD_ADDR,
                      initrd_size);
        strcpy (phys_ram_base + (16 << 20) - 256 + ret, kernel_cmdline);
    }
    else {
        strcpy (phys_ram_base + (16 << 20) - 256, kernel_cmdline);
    }

    *(int *)(phys_ram_base + (16 << 20) - 260) = tswap32 (0x12345678);
    *(int *)(phys_ram_base + (16 << 20) - 264) = tswap32 (ram_size);
}

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);

    if (env->kernel_filename)
        load_kernel (env, env->ram_size, env->kernel_filename,
                     env->kernel_cmdline, env->initrd_filename);
}

static void mips_init (int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename)
{
    char buf[1024];
    unsigned long bios_offset;
    int ret;
    CPUState *env;
    static RTCState *rtc_state;
    int i;

    env = cpu_init();
    env->bigendian = bigendian;
    fprintf(stderr, "%s: setting %s endian mode\n",
            __func__, bigendian ? "big" : "little");

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
    fprintf(stderr, "%s: ram_base = %p, ram_size = 0x%08x, bios_offset = 0x%08lx\n",
        __func__, phys_ram_base, ram_size, bios_offset);
    ret = load_image(buf, phys_ram_base + bios_offset);
    if ((ret > 0) && (ret <= BIOS_SIZE)) {
        fprintf(stderr, "%s: load BIOS '%s', size %d\n", __func__, buf, ret);
        cpu_register_physical_memory((uint32_t)(0x1fc00000),
                                     ret, bios_offset | IO_MEM_ROM);
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

    serial_init(&pic_set_irq_new, isa_pic, 0x3f8, 4, serial_hds[0]);
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

    mips_display_init(env, "vc");
}

static void mips_r4k_init (int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename)
{
    /* Run MIPS system in big endian mode. */
    bigendian = 1;
    mips_init(ram_size, vga_ram_size,
        boot_device, ds, fd_filename, snapshot,
        kernel_filename, kernel_cmdline, initrd_filename);
}

static void mipsel_r4k_init (int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename)
{
    /* Run MIPS system in little endian mode. */
    bigendian = 0;
    mips_init(ram_size, vga_ram_size,
        boot_device, ds, fd_filename, snapshot,
        kernel_filename, kernel_cmdline, initrd_filename);
}

static void mips_ar7_common_init (int ram_size,
                    uint16_t flash_manufacturer, uint16_t flash_type,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename)
{
    char buf[1024];
    int64_t entry = 0;
    unsigned long bios_offset;
    int ret;
    CPUState *env;
    long kernel_size;

    env = cpu_init();
    /* Typical AR7 systems run in little endian mode. */
    bigendian = env->bigendian = 0;
    fprintf(stderr, "%s: setting endianness %d\n", __func__, 0);

    register_savevm("cpu", 0, 3, cpu_save, cpu_load, env);
    qemu_register_reset(main_cpu_reset, env);

    /* Allocate RAM. */

    /* The AR7 processor has 4 KiB internal RAM at physical address 0x00000000. */
    cpu_register_physical_memory(0, 4 * KiB, IO_MEM_RAM);

    /* 16 MiB external RAM at physical address 0x14000000.
       More memory can be selected with command line option -m. */
    if (ram_size > 100 * MiB) {
            ram_size = 16 * MiB;
    }
    cpu_register_physical_memory(0x14000000, ram_size, (4 * KiB) | IO_MEM_RAM);

    /* Try to load a BIOS image. If this fails, we continue regardless,
       but initialize the hardware ourselves. When a kernel gets
       preloaded we also initialize the hardware, since the BIOS wasn't
       run. */
    bios_offset = ram_size;
    
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, "flashimage.bin");
    ret = load_image(buf, phys_ram_base + bios_offset);
    fprintf(stderr, "%s: load BIOS '%s', size %d\n", __func__, buf, ret);
    if (ret > 0) {
        const uint32_t address = 0x10000000;
        const unsigned blocksize = 0x10000;
        pflash_t *pf;
        switch (flash_manufacturer) {
            case MANUFACTURER_AMD:
            case MANUFACTURER_MACRONIX:
            case 0x4a:  /* Which manufacturer is this? */
                pf = pflash_amd_register(address, bios_offset,
                        0,
                        blocksize, ret / blocksize, 2,
                        /* AMD Am29LV160DB */
                        /* ES29LV160D */
                        flash_manufacturer, flash_type, 0x33, 0x44);
                break;
            case MANUFACTURER_INTEL:
                pf = pflash_cfi01_register(address, bios_offset,
                        0,
                        blocksize, ret / blocksize, 2,
                        flash_manufacturer, flash_type, 0x33, 0x44);
                break;
            default:
                pf = pflash_cfi02_register(address, bios_offset,
                        0,
                        blocksize, ret / blocksize, 2,
                        flash_manufacturer, flash_type, 0x33, 0x44);
        }
        bios_offset += ret;
    } else {
        ret = 0;
    }
    
    /* The AR7 processor has 4 KiB internal ROM at physical address 0x1fc00000. */
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
    fprintf(stderr, "%s: ram_base = %p, ram_size = 0x%08x, bios_offset = 0x%08lx\n",
        __func__, phys_ram_base, ram_size, bios_offset);
    ret = load_image(buf, phys_ram_base + bios_offset);
    if ((ret > 0) && (ret <= BIOS_SIZE)) {
        fprintf(stderr, "%s: load BIOS '%s', size %d\n", __func__, buf, ret);
    } else {
        /* Not fatal, write a jump to address 0xb0000000 into memory. */
        static const uint8_t jump[] = {
            /* lui t9,0xb000; jr t9 */
            0x00, 0xb0, 0x19, 0x3c, 0x08, 0x00, 0x20, 0x03
        };
        fprintf(stderr, "QEMU: Warning, could not load MIPS bios '%s'.\n"
                "QEMU added a jump instruction to flash start.\n", buf);
        memcpy (phys_ram_base + bios_offset, jump, sizeof(jump));
        ret = 4 * KiB;
    }
    cpu_register_physical_memory((uint32_t)(0x1fc00000),
                                 ret, bios_offset | IO_MEM_ROM);

    kernel_size = 0;
    if (kernel_filename) {
        kernel_size = load_elf(kernel_filename, VIRT_TO_PHYS_ADDEND, &entry);
        if (kernel_size >= 0) {
            fprintf(stderr, "qemu: elf kernel '%s' with start address 0x%08lx\n",
                        kernel_filename, (unsigned long)entry);
            env->PC = entry;
        } else {
            kernel_size = load_image(kernel_filename,
                                phys_ram_base + 4 * KiB);
            if (kernel_size < 0) {
                fprintf(stderr, "qemu: could not load kernel '%s'\n",
                        kernel_filename);
                exit(1);
            }
            env->PC = 0x94000000;
        }

        /* a0 = argc, a1 = argv, a2 = envp */
        env->gpr[4] = 0;
        env->gpr[5] = 0;
        env->gpr[6] = 0;

        /* Set SP (needed for some kernels) - normally set by bootloader. */
        env->gpr[29] = (env->PC + (kernel_size & 0xfffffffc)) + 0x1000;

#if 0 /* disable buggy code */
        /* load initrd */
        if (initrd_filename) {
            // code is buggy (wrong address)!!!
            target_ulong initrd_base = INITRD_LOAD_ADDR;
            target_ulong initrd_size = load_image(initrd_filename,
                                     phys_ram_base + initrd_base);
            if (initrd_size == (target_ulong) -1) {
            if (load_image(initrd_filename,
                           phys_ram_base + INITRD_LOAD_ADDR + VIRT_TO_PHYS_ADDEND)
                == (target_ulong) -1) {
                fprintf(stderr, "qemu: could not load initial ram disk '%s'\n", 
                        initrd_filename);
                exit(1);
            }
        }

        /* Store command line. */
        if (kernel_cmdline && *kernel_cmdline) {
            // code is buggy (wrong address)!!!
            strcpy (phys_ram_base + (16 << 20) - 256, kernel_cmdline);
            /* FIXME: little endian support */
            *(int *)(phys_ram_base + (16 << 20) - 260) = tswap32 (0x12345678);
            *(int *)(phys_ram_base + (16 << 20) - 264) = tswap32 (ram_size);
        }
#endif
    }

    /* Init internal devices */
    cpu_mips_clock_init(env);
    //~ cpu_mips_irqctrl_init();

    ar7_init(env);
    mips_display_init(env, "vc");
}

static void mips_ar7_init(int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename)
{
    mips_ar7_common_init (ram_size, MANUFACTURER_ST, 0x2249,
                          kernel_filename, kernel_cmdline, initrd_filename);
}

static void fbox4_init(int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename)
{
    mips_ar7_common_init (32 * MiB, MANUFACTURER_MACRONIX, MX29LV320CT,
                          kernel_filename, kernel_cmdline, initrd_filename);
}

static void fbox8_init(int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename)
{
    mips_ar7_common_init (32 * MiB, MANUFACTURER_MACRONIX, MX29LV640BT,
                          kernel_filename, kernel_cmdline, initrd_filename);
}

static void ar7_amd_init(int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename)
{
    mips_ar7_common_init (ram_size, MANUFACTURER_AMD, AM29LV160DB,
                          kernel_filename, kernel_cmdline, initrd_filename);
}

static void sinus_3_init(int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename)
{
    mips_ar7_common_init (16 * MiB, MANUFACTURER_004A, ES29LV160DB,
                          kernel_filename, kernel_cmdline, initrd_filename);
}

static void sinus_se_init(int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename)
{
    mips_ar7_common_init (16 * MiB, MANUFACTURER_INTEL, I28F160C3B,
                          kernel_filename, kernel_cmdline, initrd_filename);
}

static QEMUMachine mips_machines[] = {
  {
    "mips",
    "MIPS r4k platform",
    mips_r4k_init,
  },
  {
    "mipsel",
    "MIPS r4k platform (little endian)",
    mipsel_r4k_init,
  },
  {
    "ar7",
    "MIPS 4KEc / AR7 platform",
    mips_ar7_init,
  },
  {
    "fbox-4mb",
    "FBox 4 MiB flash (AR7 platform)",
    fbox4_init,
  },
  {
    "fbox-8mb",
    "FBox 8 MiB flash (AR7 platform)",
    fbox8_init,
  },
  {
    "ar7-amd",
    "MIPS AR7 with AMD flash",
    ar7_amd_init,
  },
  {
    "sinus-se",
    "Sinus DSL SE, Sinus DSL Basic SE (AR7 platform)",
    sinus_se_init,
  },
  {
    "sinus-3",
    "Sinus DSL Basic 3 (AR7 platform)",
    sinus_3_init,
  },
};

int qemu_register_mips_machines(void)
{
    size_t i;
    for (i = 0; i < sizeof(mips_machines) / sizeof(*mips_machines); i++) {
        qemu_register_machine(&mips_machines[i]);
    }
    return 0;
}
