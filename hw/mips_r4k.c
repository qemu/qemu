/*
 * mips_r4k.c
 */

#include "vl.h"
#include "hw/ar7.h"	/* ar7_init */

#define BIOS_FILENAME "mips_bios.bin"

#define KERNEL_LOAD_ADDR 0x80010000
//~ #define KERNEL_LOAD_ADDR 0x80040000
#define INITRD_LOAD_ADDR 0x80800000

#define VIRT_TO_PHYS_ADDEND (-0x80000000LL)

//~ #define MIPS_CPS (100 * 1000 * 1000)
#define MIPS_CPS (150 * 1000 * 1000 / 2)

extern FILE *logfile;

static PITState *pit;

static int bigendian;

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

static void cpu_mips_irqctrl_init (void)
{
}

/* XXX: do not use a global */
uint32_t cpu_mips_get_random (CPUState *env)
{
    static uint32_t seed = 0;
    uint32_t idx;
    seed = seed * 314159 + 1;
    idx = (seed >> 16) % (MIPS_TLB_NB - env->CP0_Wired) + env->CP0_Wired;
    return idx;
}

/* MIPS R4K timer */
uint32_t cpu_mips_get_count (CPUState *env)
{
    return env->CP0_Count +
        (uint32_t)muldiv64(qemu_get_clock(vm_clock),
                           MIPS_CPS, ticks_per_sec);
}

static void cpu_mips_update_count (CPUState *env, uint32_t count,
                                   uint32_t compare)
{
    uint64_t now, next;
    uint32_t tmp;
    
    tmp = count;
    if (count == compare)
        tmp++;
    now = qemu_get_clock(vm_clock);
    next = now + muldiv64(compare - tmp, ticks_per_sec, MIPS_CPS);
    if (next == now)
        next++;
#if 1
    if (logfile) {
        fprintf(logfile, "%s: 0x%08" PRIx64 " %08x %08x => 0x%08" PRIx64 "\n",
                __func__, now, count, compare, next - now);
    }
#endif
    /* Store new count and compare registers */
    env->CP0_Compare = compare;
    env->CP0_Count =
        count - (uint32_t)muldiv64(now, MIPS_CPS, ticks_per_sec);
    /* Adjust timer */
    qemu_mod_timer(env->timer, next);
}

void cpu_mips_store_count (CPUState *env, uint32_t value)
{
    cpu_mips_update_count(env, value, env->CP0_Compare);
}

void cpu_mips_store_compare (CPUState *env, uint32_t value)
{
    cpu_mips_update_count(env, cpu_mips_get_count(env), value);
    env->CP0_Cause &= ~0x00008000;
    cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
}

static void mips_timer_cb (void *opaque)
{
    CPUState *env;

    env = opaque;
#if 1
    if (logfile) {
        fprintf(logfile, "%s\n", __func__);
    }
#endif
    cpu_mips_update_count(env, cpu_mips_get_count(env), env->CP0_Compare);
    env->CP0_Cause |= 0x00008000;
    cpu_interrupt(env, CPU_INTERRUPT_HARD);
}

void cpu_mips_clock_init (CPUState *env)
{
    env->timer = qemu_new_timer(vm_clock, &mips_timer_cb, env);
    env->CP0_Compare = 0;
    cpu_mips_update_count(env, 1, 0);
}

static void io_writeb (void *opaque, target_phys_addr_t addr, uint32_t value)
{
#if 0
    if (logfile)
        fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, value);
#endif
    cpu_outb(NULL, addr & 0xffff, value);
}

static uint32_t io_readb (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret = cpu_inb(NULL, addr & 0xffff);
#if 0
    if (logfile)
        fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, ret);
#endif
    return ret;
}

static void io_writew (void *opaque, target_phys_addr_t addr, uint32_t value)
{
#if 0
    if (logfile)
        fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, value);
#endif
    if (bigendian) {
        value = bswap16(value);
    }
    cpu_outw(NULL, addr & 0xffff, value);
}

static uint32_t io_readw (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret = cpu_inw(NULL, addr & 0xffff);
    if (bigendian) {
        ret = bswap16(ret);
    }
#if 0
    if (logfile)
        fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, ret);
#endif
    return ret;
}

static void io_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
#if 0
    if (logfile)
        fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, value);
#endif
    if (bigendian) {
        value = bswap32(value);
    }
    cpu_outl(NULL, addr & 0xffff, value);
}

static uint32_t io_readl (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret = cpu_inl(NULL, addr & 0xffff);

    if (bigendian) {
        ret = bswap32(ret);
    }
#if 0
    if (logfile)
        fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, ret);
#endif
    return ret;
}

static CPUWriteMemoryFunc * const io_write[] = {
    io_writeb,
    io_writew,
    io_writel,
};

static CPUReadMemoryFunc * const io_read[] = {
    io_readb,
    io_readw,
    io_readl,
};

static int bios_load(const char *filename, unsigned long bios_offset, unsigned long address)
{
    char buf[1024];
    int ret;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, filename);
    ret = load_image(buf, phys_ram_base + bios_offset);
    printf("%s: load BIOS '%s' size %d\n", __func__, buf, ret);
    if (ret > 0) {
        const unsigned blocksize = 0x10000;
        pflash_t *pf = pflash_cfi01_register(address, bios_offset,
                0,
                blocksize, ret / blocksize, 2,
                0x4a, 0x49, 0x33, 0x44);
    } else {
        ret = 0;
    }
    return ret;
}

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);
}

static void mips_init (int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename)
{
    char buf[1024];
    int64_t entry = 0;
    unsigned long bios_offset;
    int io_memory;
    int ret;
    CPUState *env;
    long kernel_size;

    env = cpu_init();
    env->bigendian = bigendian;
    printf("%s: setting endianness %d\n", __func__, bigendian);

    register_savevm("cpu", 0, 3, cpu_save, cpu_load, env);
    qemu_register_reset(main_cpu_reset, env);

    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, IO_MEM_RAM);

    /* Try to load a BIOS image. If this fails, we continue regardless,
       but initialize the hardware ourselves. When a kernel gets
       preloaded we also initialize the hardware, since the BIOS wasn't
       run. */
    bios_offset = ram_size + vga_ram_size;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
    printf("%s: ram_base = %p, ram_size = 0x%08x, bios_offset = 0x%08lx\n",
        __func__, phys_ram_base, ram_size, bios_offset);
    ret = load_image(buf, phys_ram_base + bios_offset);
    if ((ret > 0) && (ret <= BIOS_SIZE)) {
        printf("%s: load BIOS '%s' size %d\n", __func__, buf, ret);
        cpu_register_physical_memory((uint32_t)(0x1fc00000),
                                     ret, bios_offset | IO_MEM_ROM);
    } else {
        /* not fatal */
        fprintf(stderr, "qemu: Warning, could not load MIPS bios '%s'\n",
                buf);
    }

    kernel_size = 0;
    if (kernel_filename) {
        kernel_size = load_elf(kernel_filename, VIRT_TO_PHYS_ADDEND, &entry);
        if (kernel_size >= 0) {
            fprintf(stderr, "qemu: elf kernel '%s' with start address 0x%08lx\n",
                        kernel_filename, (unsigned long)entry);
            env->PC = entry;
        } else {
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

#if 0
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
#endif

        /* Store command line. */
        if (kernel_cmdline && *kernel_cmdline) {
            // code is buggy (wrong address)!!!
            strcpy (phys_ram_base + (16 << 20) - 256, kernel_cmdline);
            /* FIXME: little endian support */
            *(int *)(phys_ram_base + (16 << 20) - 260) = tswap32 (0x12345678);
            *(int *)(phys_ram_base + (16 << 20) - 264) = tswap32 (ram_size);
        }
    }

    /* Init internal devices */
    cpu_mips_clock_init(env);
    cpu_mips_irqctrl_init();

    /* Register 64 KB of ISA IO space at 0x14000000 */
    io_memory = cpu_register_io_memory(0, io_read, io_write, NULL);
    cpu_register_physical_memory(0x14000000, 0x00010000, io_memory);
    isa_mem_base = 0x10000000;

    isa_pic = pic_init(pic_irq_request, env);
    pit = pit_init(0x40, 0);
#if 1
    serial_16450_init(&pic_set_irq_new, isa_pic, 0x3f8, 4, serial_hds[0]);
#else
    serial_init(&pic_set_irq_new, isa_pic, 0x3f8, 4, serial_hds[0]);
    isa_vga_init(ds, phys_ram_base + ram_size, ram_size, 
                 vga_ram_size);
#endif

    if (nd_table[0].vlan) {
        if (nd_table[0].model == NULL
            || strcmp(nd_table[0].model, "ne2k_isa") == 0) {
            isa_ne2000_init(0x300, 9, &nd_table[0]);
        } else {
            fprintf(stderr, "qemu: Unsupported NIC: %s\n", nd_table[0].model);
            exit (1);
        }
    }
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

static void mips_ar7_init (int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename)
{
    char buf[1024];
    int64_t entry = 0;
    unsigned long bios_offset;
    int ret;
    CPUState *env;
    long kernel_size;

    /* This is an embedded device without VGA. */
    vga_ram_size = 0;

    env = cpu_init();
    /* Typical AR7 systems run in little endian mode. */
    bigendian = env->bigendian = 0;
    printf("%s: setting endianness %d\n", __func__, 0);

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
    bios_offset = ram_size + vga_ram_size;
    bios_offset += bios_load("flashimage.bin", bios_offset, 0x10000000);
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
    printf("%s: ram_base = %p, ram_size = 0x%08x, bios_offset = 0x%08lx\n",
        __func__, phys_ram_base, ram_size, bios_offset);
    ret = load_image(buf, phys_ram_base + bios_offset);
    if ((ret > 0) && (ret <= BIOS_SIZE)) {
        printf("%s: load BIOS '%s' size %d\n", __func__, buf, ret);
        cpu_register_physical_memory((uint32_t)(0x1fc00000),
                                     ret, bios_offset | IO_MEM_ROM);
    } else {
        /* not fatal */
        fprintf(stderr, "qemu: Warning, could not load MIPS bios '%s'\n",
                buf);
    }

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
        env->gpr[4] = 1;
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
    cpu_mips_irqctrl_init();

    ar7_init(env);
#if defined(CONFIG_SDL) && 0 // no VGA for embedded device
    vga_initialize(NULL, ds, phys_ram_base + ram_size, ram_size, 
                   vga_ram_size, 0, 0);
#endif
}

static QEMUMachine mips_machine[] = {
  {
    "mips",
    "mips r4k platform",
    mips_r4k_init,
  },
  {
    "mipsel",
    "mips r4k platform (little endian)",
    mipsel_r4k_init,
  },
  {
    "ar7",
    "mips ar7 platform",
    mips_ar7_init,
  }
};

int qemu_register_mips_machines(void)
{
    size_t i;
    for (i = 0; i < sizeof(mips_machine) / sizeof(*mips_machine); i++) {
        qemu_register_machine(&mips_machine[i]);
    }
    return 0;
}
