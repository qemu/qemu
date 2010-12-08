/*
 * QEMU/MIPS pseudo-board
 *
 * emulates a simple machine with ISA-like bus.
 * ISA IO space mapped to the 0x14000000 (PHYS) and
 * ISA memory at the 0x10000000 (PHYS, 16Mb in size).
 * All peripherial devices are attached to this "bus" with
 * the standard PC ISA addresses.
*/
#include "hw.h"
#include "mips.h"
#include "mips_cpudevs.h"
#include "pc.h"
#include "isa.h"
#include "net.h"
#include "sysemu.h"
#include "boards.h"
#include "flash.h"
#include "qemu-log.h"
#include "mips-bios.h"
#include "ide.h"
#include "loader.h"
#include "elf.h"
#include "mc146818rtc.h"
#include "blockdev.h"

#define MAX_IDE_BUS 2

static const int ide_iobase[2] = { 0x1f0, 0x170 };
static const int ide_iobase2[2] = { 0x3f6, 0x376 };
static const int ide_irq[2] = { 14, 15 };

static PITState *pit; /* PIT i8254 */

/* i8254 PIT is attached to the IRQ0 at PIC i8259 */

static struct _loaderparams {
    int ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
} loaderparams;

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

static CPUWriteMemoryFunc * const mips_qemu_write[] = {
    &mips_qemu_writel,
    &mips_qemu_writel,
    &mips_qemu_writel,
};

static CPUReadMemoryFunc * const mips_qemu_read[] = {
    &mips_qemu_readl,
    &mips_qemu_readl,
    &mips_qemu_readl,
};

static int mips_qemu_iomemtype = 0;

typedef struct ResetData {
    CPUState *env;
    uint64_t vector;
} ResetData;

static int64_t load_kernel(void)
{
    int64_t entry, kernel_high;
    long kernel_size, initrd_size, params_size;
    ram_addr_t initrd_offset;
    uint32_t *params_buf;
    int big_endian;

#ifdef TARGET_WORDS_BIGENDIAN
    big_endian = 1;
#else
    big_endian = 0;
#endif
    kernel_size = load_elf(loaderparams.kernel_filename, cpu_mips_kseg0_to_phys,
                           NULL, (uint64_t *)&entry, NULL,
                           (uint64_t *)&kernel_high, big_endian,
                           ELF_MACHINE, 1);
    if (kernel_size >= 0) {
        if ((entry & ~0x7fffffffULL) == 0x80000000)
            entry = (int32_t)entry;
    } else {
        fprintf(stderr, "qemu: could not load kernel '%s'\n",
                loaderparams.kernel_filename);
        exit(1);
    }

    /* load initrd */
    initrd_size = 0;
    initrd_offset = 0;
    if (loaderparams.initrd_filename) {
        initrd_size = get_image_size (loaderparams.initrd_filename);
        if (initrd_size > 0) {
            initrd_offset = (kernel_high + ~TARGET_PAGE_MASK) & TARGET_PAGE_MASK;
            if (initrd_offset + initrd_size > ram_size) {
                fprintf(stderr,
                        "qemu: memory too small for initial ram disk '%s'\n",
                        loaderparams.initrd_filename);
                exit(1);
            }
            initrd_size = load_image_targphys(loaderparams.initrd_filename,
                                              initrd_offset,
                                              ram_size - initrd_offset);
        }
        if (initrd_size == (target_ulong) -1) {
            fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                    loaderparams.initrd_filename);
            exit(1);
        }
    }

    /* Store command line.  */
    params_size = 264;
    params_buf = qemu_malloc(params_size);

    params_buf[0] = tswap32(ram_size);
    params_buf[1] = tswap32(0x12345678);

    if (initrd_size > 0) {
        snprintf((char *)params_buf + 8, 256, "rd_start=0x%" PRIx64 " rd_size=%li %s",
                 cpu_mips_phys_to_kseg0(NULL, initrd_offset),
                 initrd_size, loaderparams.kernel_cmdline);
    } else {
        snprintf((char *)params_buf + 8, 256, "%s", loaderparams.kernel_cmdline);
    }

    rom_add_blob_fixed("params", params_buf, params_size,
                       (16 << 20) - 264);

    return entry;
}

static void main_cpu_reset(void *opaque)
{
    ResetData *s = (ResetData *)opaque;
    CPUState *env = s->env;

    cpu_reset(env);
    env->active_tc.PC = s->vector;
}

static const int sector_len = 32 * 1024;
static
void mips_r4k_init (ram_addr_t ram_size,
                    const char *boot_device,
                    const char *kernel_filename, const char *kernel_cmdline,
                    const char *initrd_filename, const char *cpu_model)
{
    char *filename;
    ram_addr_t ram_offset;
    ram_addr_t bios_offset;
    int bios_size;
    CPUState *env;
    ResetData *reset_info;
    int i;
    qemu_irq *i8259;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    DriveInfo *dinfo;
    int be;

    /* init CPUs */
    if (cpu_model == NULL) {
#ifdef TARGET_MIPS64
        cpu_model = "R4000";
#else
        cpu_model = "24Kf";
#endif
    }
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    reset_info = qemu_mallocz(sizeof(ResetData));
    reset_info->env = env;
    reset_info->vector = env->active_tc.PC;
    qemu_register_reset(main_cpu_reset, reset_info);

    /* allocate RAM */
    if (ram_size > (256 << 20)) {
        fprintf(stderr,
                "qemu: Too much memory for this machine: %d MB, maximum 256 MB\n",
                ((unsigned int)ram_size / (1 << 20)));
        exit(1);
    }
    ram_offset = qemu_ram_alloc(NULL, "mips_r4k.ram", ram_size);

    cpu_register_physical_memory(0, ram_size, ram_offset | IO_MEM_RAM);

    if (!mips_qemu_iomemtype) {
        mips_qemu_iomemtype = cpu_register_io_memory(mips_qemu_read,
                                                     mips_qemu_write, NULL,
                                                     DEVICE_NATIVE_ENDIAN);
    }
    cpu_register_physical_memory(0x1fbf0000, 0x10000, mips_qemu_iomemtype);

    /* Try to load a BIOS image. If this fails, we continue regardless,
       but initialize the hardware ourselves. When a kernel gets
       preloaded we also initialize the hardware, since the BIOS wasn't
       run. */
    if (bios_name == NULL)
        bios_name = BIOS_FILENAME;
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (filename) {
        bios_size = get_image_size(filename);
    } else {
        bios_size = -1;
    }
#ifdef TARGET_WORDS_BIGENDIAN
    be = 1;
#else
    be = 0;
#endif
    if ((bios_size > 0) && (bios_size <= BIOS_SIZE)) {
        bios_offset = qemu_ram_alloc(NULL, "mips_r4k.bios", BIOS_SIZE);
	cpu_register_physical_memory(0x1fc00000, BIOS_SIZE,
                                     bios_offset | IO_MEM_ROM);

        load_image_targphys(filename, 0x1fc00000, BIOS_SIZE);
    } else if ((dinfo = drive_get(IF_PFLASH, 0, 0)) != NULL) {
        uint32_t mips_rom = 0x00400000;
        bios_offset = qemu_ram_alloc(NULL, "mips_r4k.bios", mips_rom);
        if (!pflash_cfi01_register(0x1fc00000, bios_offset,
                                   dinfo->bdrv, sector_len,
                                   mips_rom / sector_len,
                                   4, 0, 0, 0, 0, be)) {
            fprintf(stderr, "qemu: Error registering flash memory.\n");
	}
    }
    else {
	/* not fatal */
        fprintf(stderr, "qemu: Warning, could not load MIPS bios '%s'\n",
		bios_name);
    }
    if (filename) {
        qemu_free(filename);
    }

    if (kernel_filename) {
        loaderparams.ram_size = ram_size;
        loaderparams.kernel_filename = kernel_filename;
        loaderparams.kernel_cmdline = kernel_cmdline;
        loaderparams.initrd_filename = initrd_filename;
        reset_info->vector = load_kernel();
    }

    /* Init CPU internal devices */
    cpu_mips_irq_init_cpu(env);
    cpu_mips_clock_init(env);

    /* The PIC is attached to the MIPS CPU INT0 pin */
    i8259 = i8259_init(env->irq[2]);
    isa_bus_new(NULL);
    isa_bus_irqs(i8259);

    rtc_init(2000, NULL);

    /* Register 64 KB of ISA IO space at 0x14000000 */
    isa_mmio_init(0x14000000, 0x00010000);
    isa_mem_base = 0x10000000;

    pit = pit_init(0x40, i8259[0]);

    for(i = 0; i < MAX_SERIAL_PORTS; i++) {
        if (serial_hds[i]) {
            serial_isa_init(i, serial_hds[i]);
        }
    }

    isa_vga_init();

    if (nd_table[0].vlan)
        isa_ne2000_init(0x300, 9, &nd_table[0]);

    if (drive_get_max_bus(IF_IDE) >= MAX_IDE_BUS) {
        fprintf(stderr, "qemu: too many IDE bus\n");
        exit(1);
    }

    for(i = 0; i < MAX_IDE_BUS * MAX_IDE_DEVS; i++) {
        hd[i] = drive_get(IF_IDE, i / MAX_IDE_DEVS, i % MAX_IDE_DEVS);
    }

    for(i = 0; i < MAX_IDE_BUS; i++)
        isa_ide_init(ide_iobase[i], ide_iobase2[i], ide_irq[i],
                     hd[MAX_IDE_DEVS * i],
		     hd[MAX_IDE_DEVS * i + 1]);

    isa_create_simple("i8042");
}

static QEMUMachine mips_machine = {
    .name = "mips",
    .desc = "mips r4k platform",
    .init = mips_r4k_init,
};

static void mips_machine_init(void)
{
    qemu_register_machine(&mips_machine);
}

machine_init(mips_machine_init);
