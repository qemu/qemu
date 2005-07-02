#include "vl.h"

#define DEBUG_IRQ_COUNT

#define BIOS_FILENAME "mips_bios.bin"
//#define BIOS_FILENAME "system.bin"
#define KERNEL_LOAD_ADDR 0x80010000
#define INITRD_LOAD_ADDR 0x80800000

/* MIPS R4K IRQ controler */
#if defined(DEBUG_IRQ_COUNT)
static uint64_t irq_count[16];
#endif

extern FILE *logfile;

void mips_set_irq (int n_IRQ, int level)
{
    uint32_t mask;

    if (n_IRQ < 0 || n_IRQ >= 8)
        return;
    mask = 0x100 << n_IRQ;
    if (level != 0) {
#if 1
        if (logfile) {
            fprintf(logfile, "%s n %d l %d mask %08x %08x\n",
                    __func__, n_IRQ, level, mask, cpu_single_env->CP0_Status);
        }
#endif
        cpu_single_env->CP0_Cause |= mask;
        if ((cpu_single_env->CP0_Status & 0x00000001) &&
            (cpu_single_env->CP0_Status & mask)) {
#if defined(DEBUG_IRQ_COUNT)
            irq_count[n_IRQ]++;
#endif
#if 1
            if (logfile)
                fprintf(logfile, "%s raise IRQ\n", __func__);
#endif
            cpu_interrupt(cpu_single_env, CPU_INTERRUPT_HARD);
        }
    } else {
        cpu_single_env->CP0_Cause &= ~mask;
    }
}

void pic_set_irq (int n_IRQ, int level)
{
    mips_set_irq(n_IRQ + 2, level);
}

void pic_info (void)
{
    term_printf("IRQ asserted: %02x mask: %02x\n",
                (cpu_single_env->CP0_Cause >> 8) & 0xFF,
                (cpu_single_env->CP0_Status >> 8) & 0xFF);
}

void irq_info (void)
{
#if !defined(DEBUG_IRQ_COUNT)
    term_printf("irq statistic code not compiled.\n");
#else
    int i;
    int64_t count;

    term_printf("IRQ statistics:\n");
    for (i = 0; i < 8; i++) {
        count = irq_count[i];
        if (count > 0)
            term_printf("%2d: %lld\n", i, count);
    }
#endif
}

void cpu_mips_irqctrl_init (void)
{
}

uint32_t cpu_mips_get_random (CPUState *env)
{
    uint32_t now = qemu_get_clock(vm_clock);

    return now % (MIPS_TLB_NB - env->CP0_Wired) + env->CP0_Wired;
}

/* MIPS R4K timer */
uint32_t cpu_mips_get_count (CPUState *env)
{
    return env->CP0_Count +
        (uint32_t)muldiv64(qemu_get_clock(vm_clock),
                           100 * 1000 * 1000, ticks_per_sec);
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
    next = now + muldiv64(compare - tmp, ticks_per_sec, 100 * 1000 * 1000);
    if (next == now)
	next++;
#if 1
    if (logfile) {
        fprintf(logfile, "%s: 0x%08llx %08x %08x => 0x%08llx\n",
                __func__, now, count, compare, next - now);
    }
#endif
    /* Store new count and compare registers */
    env->CP0_Compare = compare;
    env->CP0_Count =
        count - (uint32_t)muldiv64(now, 100 * 1000 * 1000, ticks_per_sec);
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
    pic_set_irq(5, 0);
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
    pic_set_irq(5, 1);
}

void cpu_mips_clock_init (CPUState *env)
{
    env->timer = qemu_new_timer(vm_clock, &mips_timer_cb, env);
    env->CP0_Compare = 0;
    cpu_mips_update_count(env, 1, 0);
}

static void io_writeb (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    if (logfile)
        fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, value);
    cpu_outb(NULL, addr & 0xffff, value);
}

static uint32_t io_readb (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret = cpu_inb(NULL, addr & 0xffff);
    if (logfile)
        fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, ret);
    return ret;
}

static void io_writew (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    if (logfile)
        fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, value);
#ifdef TARGET_WORDS_BIGENDIAN
    value = bswap16(value);
#endif
    cpu_outw(NULL, addr & 0xffff, value);
}

static uint32_t io_readw (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret = cpu_inw(NULL, addr & 0xffff);
#ifdef TARGET_WORDS_BIGENDIAN
    ret = bswap16(ret);
#endif
    if (logfile)
        fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, ret);
    return ret;
}

static void io_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    if (logfile)
        fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, value);
#ifdef TARGET_WORDS_BIGENDIAN
    value = bswap32(value);
#endif
    cpu_outl(NULL, addr & 0xffff, value);
}

static uint32_t io_readl (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret = cpu_inl(NULL, addr & 0xffff);

#ifdef TARGET_WORDS_BIGENDIAN
    ret = bswap32(ret);
#endif
    if (logfile)
        fprintf(logfile, "%s: addr %08x val %08x\n", __func__, addr, ret);
    return ret;
}

CPUWriteMemoryFunc *io_write[] = {
    &io_writeb,
    &io_writew,
    &io_writel,
};

CPUReadMemoryFunc *io_read[] = {
    &io_readb,
    &io_readw,
    &io_readl,
};

void mips_r4k_init (int ram_size, int vga_ram_size, int boot_device,
                    DisplayState *ds, const char **fd_filename, int snapshot,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename)
{
    char buf[1024];
    target_ulong kernel_base, kernel_size, initrd_base, initrd_size;
    unsigned long bios_offset;
    int io_memory;
    int linux_boot;
    int ret;

    printf("%s: start\n", __func__);
    linux_boot = (kernel_filename != NULL);
    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, IO_MEM_RAM);
    bios_offset = ram_size + vga_ram_size;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
    printf("%s: load BIOS '%s' size %d\n", __func__, buf, BIOS_SIZE);
    ret = load_image(buf, phys_ram_base + bios_offset);
    if (ret != BIOS_SIZE) {
        fprintf(stderr, "qemu: could not load MIPS bios '%s'\n", buf);
        exit(1);
    }
    cpu_register_physical_memory((uint32_t)(0x1fc00000),
                                 BIOS_SIZE, bios_offset | IO_MEM_ROM);
#if 0
    memcpy(phys_ram_base + 0x10000, phys_ram_base + bios_offset, BIOS_SIZE);
    cpu_single_env->PC = 0x80010004;
#else
    cpu_single_env->PC = 0xBFC00004;
#endif
    if (linux_boot) {
        kernel_base = KERNEL_LOAD_ADDR;
        /* now we can load the kernel */
        kernel_size = load_image(kernel_filename,
                                phys_ram_base + (kernel_base - 0x80000000));
        if (kernel_size == (target_ulong) -1) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n", 
                    kernel_filename);
            exit(1);
        }
        /* load initrd */
        if (initrd_filename) {
            initrd_base = INITRD_LOAD_ADDR;
            initrd_size = load_image(initrd_filename,
                                     phys_ram_base + initrd_base);
            if (initrd_size == (target_ulong) -1) {
                fprintf(stderr, "qemu: could not load initial ram disk '%s'\n", 
                        initrd_filename);
                exit(1);
            }
        } else {
            initrd_base = 0;
            initrd_size = 0;
        }
        cpu_single_env->PC = KERNEL_LOAD_ADDR;
    } else {
        kernel_base = 0;
        kernel_size = 0;
        initrd_base = 0;
        initrd_size = 0;
    }
    /* XXX: should not be ! */
    printf("%s: init VGA\n", __func__);
    vga_initialize(NULL, ds, phys_ram_base + ram_size, ram_size, 
                   vga_ram_size);


    /* Init internal devices */
    cpu_mips_clock_init(cpu_single_env);
    cpu_mips_irqctrl_init();

    isa_mem_base = 0x78000000;
    /* Register 64 KB of ISA IO space at random address */
    io_memory = cpu_register_io_memory(0, io_read, io_write, NULL);
    cpu_register_physical_memory(0x70000000, 0x00010000, io_memory);
    serial_init(0x3f8, 4, serial_hds[0]);
    printf("%s: done\n", __func__);
}

QEMUMachine mips_machine = {
    "mips",
    "mips r4k platform",
    mips_r4k_init,
};
