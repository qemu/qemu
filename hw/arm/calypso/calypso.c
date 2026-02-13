/*
 * Calypso SoC (TI ARM946E-S based) machine emulation for QEMU 9.2
 * Minimal version with basic peripherals
 */
#include "qemu/osdep.h"          /* MUST be first — always */
#include "qapi/error.h"
#include "cpu.h"
#include "hw/boards.h"            /* <-- add here, AFTER osdep.h */
#include "hw/sysbus.h"

#include "sysemu/sysemu.h"
#include "sysemu/blockdev.h"      /* drive_get, IF_PFLASH */
#include "sysemu/block-backend.h" /* blk_by_legacy_dinfo */
#include "hw/qdev-properties.h"
#include "hw/loader.h"
#include "elf.h"
#include "hw/block/flash.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"
#include "hw/char/serial.h"
#include "chardev/char-fe.h"

/* Memory Map */
#define CALYPSO_RAM_BASE      0x00800000
#define CALYPSO_RAM_SIZE      (256 * 1024)
#define CALYPSO_FLASH_BASE    0x02000000
#define CALYPSO_FLASH_SIZE    (4 * 1024 * 1024)

/* MMIO Peripherals */
#define CALYPSO_MMIO_18XX     0xFFFE1800
#define CALYPSO_MMIO_28XX     0xFFFE2800
#define CALYPSO_SPI_BASE      0xFFFE3000
#define CALYPSO_TIMER1_BASE   0xFFFE3800
#define CALYPSO_MMIO_48XX     0xFFFE4800
#define CALYPSO_MMIO_68XX     0xFFFE6800
#define CALYPSO_MMIO_80XX     0xFFFE8000
#define CALYPSO_MMIO_F0XX     0xFFFEF000
#define CALYPSO_MMIO_50XX     0xFFFF5000
#define CALYPSO_UART_BASE     0xFFFF5800
#define CALYPSO_MMIO_98XX     0xFFFF9800
#define CALYPSO_MMIO_F9XX     0xFFFFF900
#define CALYPSO_MMIO_FAXX     0xFFFFFA00
#define CALYPSO_SYSTEM_FB     0xFFFFFB00
#define CALYPSO_MMIO_FCXX     0xFFFFFC00
#define CALYPSO_SYSTEM_FD     0xFFFFFD00
#define CALYPSO_MMIO_FFXX     0xFFFFFF00

#define CALYPSO_PERIPH_SIZE   256

typedef struct CalypsoState {
    SysBusDevice parent_obj;
    MemoryRegion ram;
    MemoryRegion flash_mem;
    MemoryRegion ram_alias0;
    MemoryRegion high_vectors;
    ARMCPU *cpu;
} CalypsoState;

#define TYPE_CALYPSO_MACHINE "calypso-min"
OBJECT_DECLARE_SIMPLE_TYPE(CalypsoState, CALYPSO_MACHINE)

/* UART stub - logs character output */
static uint64_t calypso_uart_read(void *opaque, hwaddr offset, unsigned size)
{
    switch (offset) {
        case 0x04: /* STATUS register (low byte) */
            return 0x60; /* TX ready, RX ready */
        case 0x05: /* STATUS register (high byte) */
            return 0xFF; /* All ready bits set */
        case 0x00: /* RX data */
            return 0x00; /* No data */
        default:
            return 0xFF; /* Return all bits set for unknown registers */
    }
}

static void calypso_uart_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    if (offset == 0x00) { /* TX register */
        char c = (char)(value & 0xFF);
        printf("[calypso-uart] '%c' (0x%02x)\n", 
               (c >= 32 && c < 127) ? c : '.', (unsigned)value);
        fflush(stdout);
    }
}

static const MemoryRegionOps calypso_uart_ops = {
    .read = calypso_uart_read,
    .write = calypso_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* SPI stub - always ready */
static uint64_t calypso_spi_read(void *opaque, hwaddr offset, unsigned size)
{
    if (offset == 0x00) { /* STATUS */
        return 0x0003; /* TX_READY | RX_READY */
    }
    return 0;
}

static void calypso_spi_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    /* Stub: accept writes */
}

static const MemoryRegionOps calypso_spi_ops = {
    .read = calypso_spi_read,
    .write = calypso_spi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* Timer stub - counter auto-increments */
static uint32_t timer_counter = 0;

static uint64_t calypso_timer_read(void *opaque, hwaddr offset, unsigned size)
{
    if (offset == 0x00) { /* CNT register */
        return timer_counter++;
    }
    return 0;
}

static void calypso_timer_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    /* Stub: accept writes */
}

static const MemoryRegionOps calypso_timer_ops = {
    .read = calypso_timer_read,
    .write = calypso_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* Generic MMIO stubs - 8-bit */
static uint64_t calypso_mmio8_read(void *opaque, hwaddr offset, unsigned size)
{
    return 0xFF;  /* Always return "ready" status */
}

static void calypso_mmio8_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
}

static const MemoryRegionOps calypso_mmio8_ops = {
    .read = calypso_mmio8_read,
    .write = calypso_mmio8_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 1 },
};

/* Generic MMIO stubs - 16-bit */
static uint64_t calypso_mmio16_read(void *opaque, hwaddr offset, unsigned size)
{
    hwaddr base = (hwaddr)(uintptr_t)opaque;
    if (base == CALYPSO_MMIO_48XX && offset == 0x0A) {
        return 0xFFFF;
    }
    return 0;
}

static void calypso_mmio16_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
}

static const MemoryRegionOps calypso_mmio16_ops = {
    .read = calypso_mmio16_read,
    .write = calypso_mmio16_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = { .min_access_size = 2, .max_access_size = 2 },
};

/* 68XX: mixed 8/16-bit */
static uint64_t calypso_mmio_68xx_read(void *opaque, hwaddr offset, unsigned size)
{
    return 0;
}

static void calypso_mmio_68xx_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
}

static const MemoryRegionOps calypso_mmio_68xx_ops = {
    .read = calypso_mmio_68xx_read,
    .write = calypso_mmio_68xx_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
};

/* 80XX and FFXX: logged 8-bit */
static uint64_t calypso_mmio8_logged_read(void *opaque, hwaddr offset, unsigned size)
{
    return 0xFF;
}

static void calypso_mmio8_logged_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
}

static const MemoryRegionOps calypso_mmio8_logged_ops = {
    .read = calypso_mmio8_logged_read,
    .write = calypso_mmio8_logged_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 1 },
};

static void calypso_create_mmio_region(MemoryRegion *sysmem, const char *name,
                                       hwaddr base, const MemoryRegionOps *ops,
                                       void *opaque)
{
    MemoryRegion *mr = g_new(MemoryRegion, 1);
    memory_region_init_io(mr, NULL, ops, opaque, name, CALYPSO_PERIPH_SIZE);
    memory_region_add_subregion(sysmem, base, mr);
}

/* Patch firmware pointer at specific address */
static void calypso_patch_flash_pointer(AddressSpace *as)
{
    uint32_t flash_ptr = cpu_to_le32(CALYPSO_FLASH_BASE);
    address_space_write(as, 0x008147e8, MEMTXATTRS_UNSPECIFIED,
                        (uint8_t *)&flash_ptr, sizeof(flash_ptr));
}

static void calypso_init(MachineState *machine)
{
    CalypsoState *s = g_new0(CalypsoState, 1);
    MemoryRegion *sysmem = get_system_memory();
    Object *cpuobj;
    Error *err = NULL;

    /* CPU: ARM946E-S */
    cpuobj = object_new(machine->cpu_type);
    s->cpu = ARM_CPU(cpuobj);
    
    if (!qdev_realize(DEVICE(cpuobj), NULL, &err)) {
        error_report_err(err);
        exit(1);
    }

    /* RAM: 256 KiB at 0x00800000 */
    memory_region_init_ram(&s->ram, NULL, "calypso.ram", 
                          CALYPSO_RAM_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, CALYPSO_RAM_BASE, &s->ram);

    /* RAM alias at 0x00000000 (priority 1) */
    memory_region_init_alias(&s->ram_alias0, NULL, "calypso.ram_alias0",
                            &s->ram, 0, 128 * 1024);
    memory_region_add_subregion_overlap(sysmem, 0x00000000, &s->ram_alias0, 1);

    /* High vectors alias at 0xFFFF0000 */
    memory_region_init_alias(&s->high_vectors, NULL, "calypso.high_vectors",
                            &s->ram, 0, 64 * 1024);
    memory_region_add_subregion(sysmem, 0xFFFF0000, &s->high_vectors);

    /* Flash: 4 MiB NOR at 0x02000000 */
    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);
    pflash_cfi01_register(CALYPSO_FLASH_BASE, "calypso.flash",
                         CALYPSO_FLASH_SIZE,
                         dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                         64 * 1024, 1, 0x0089, 0x0018, 0x0000, 0x0000, 0);

    /* MMIO Peripherals (8-bit) */
    calypso_create_mmio_region(sysmem, "calypso.mmio_18xx", 
                               CALYPSO_MMIO_18XX, &calypso_mmio8_ops, NULL);
    calypso_create_mmio_region(sysmem, "calypso.mmio_28xx",
                               CALYPSO_MMIO_28XX, &calypso_mmio8_ops, NULL);
    calypso_create_mmio_region(sysmem, "calypso.mmio_50xx",
                               CALYPSO_MMIO_50XX, &calypso_mmio8_ops, NULL);

    /* SPI Controller */
    calypso_create_mmio_region(sysmem, "calypso.spi",
                               CALYPSO_SPI_BASE, &calypso_spi_ops, NULL);

    /* Timer */
    calypso_create_mmio_region(sysmem, "calypso.timer1",
                               CALYPSO_TIMER1_BASE, &calypso_timer_ops, NULL);

    /* UART */
    calypso_create_mmio_region(sysmem, "calypso.uart",
                               CALYPSO_UART_BASE, &calypso_uart_ops, NULL);

    /* MMIO (16-bit) */
    calypso_create_mmio_region(sysmem, "calypso.mmio_48xx",
                               CALYPSO_MMIO_48XX, &calypso_mmio16_ops,
                               (void *)(uintptr_t)CALYPSO_MMIO_48XX);
    calypso_create_mmio_region(sysmem, "calypso.mmio_f0xx",
                               CALYPSO_MMIO_F0XX, &calypso_mmio16_ops, NULL);
    calypso_create_mmio_region(sysmem, "calypso.mmio_98xx",
                               CALYPSO_MMIO_98XX, &calypso_mmio16_ops, NULL);
    calypso_create_mmio_region(sysmem, "calypso.mmio_f9xx",
                               CALYPSO_MMIO_F9XX, &calypso_mmio16_ops, NULL);
    calypso_create_mmio_region(sysmem, "calypso.mmio_faxx",
                               CALYPSO_MMIO_FAXX, &calypso_mmio16_ops, NULL);
    calypso_create_mmio_region(sysmem, "calypso.system_fb",
                               CALYPSO_SYSTEM_FB, &calypso_mmio16_ops, NULL);
    calypso_create_mmio_region(sysmem, "calypso.mmio_fcxx",
                               CALYPSO_MMIO_FCXX, &calypso_mmio16_ops, NULL);
    calypso_create_mmio_region(sysmem, "calypso.system_fd",
                               CALYPSO_SYSTEM_FD, &calypso_mmio16_ops, NULL);

    /* 68XX: mixed 8/16-bit */
    calypso_create_mmio_region(sysmem, "calypso.mmio_68xx",
                               CALYPSO_MMIO_68XX, &calypso_mmio_68xx_ops, NULL);

    /* 80XX and FFXX: logged 8-bit */
    calypso_create_mmio_region(sysmem, "calypso.mmio_80xx",
                               CALYPSO_MMIO_80XX, &calypso_mmio8_logged_ops,
                               (void *)(uintptr_t)CALYPSO_MMIO_80XX);
    calypso_create_mmio_region(sysmem, "calypso.mmio_ffxx",
                               CALYPSO_MMIO_FFXX, &calypso_mmio8_logged_ops,
                               (void *)(uintptr_t)CALYPSO_MMIO_FFXX);

    /* Create more aliases with different priorities */
    MemoryRegion *uart_alias0 = g_new(MemoryRegion, 1);
    MemoryRegion *mr_uart = memory_region_find(sysmem, CALYPSO_UART_BASE, 1).mr;
    if (mr_uart) {
        memory_region_init_alias(uart_alias0, NULL, "calypso.uart_alias0",
                                mr_uart, 0, CALYPSO_PERIPH_SIZE);
        memory_region_add_subregion_overlap(sysmem, 0x00000000, uart_alias0, -2);
    }

    MemoryRegion *faxx_alias0 = g_new(MemoryRegion, 1);
    MemoryRegion *mr_faxx = memory_region_find(sysmem, CALYPSO_MMIO_FAXX, 1).mr;
    if (mr_faxx) {
        memory_region_init_alias(faxx_alias0, NULL, "calypso.faxx_alias0",
                                mr_faxx, 0, CALYPSO_PERIPH_SIZE);
        memory_region_add_subregion_overlap(sysmem, 0x00000000, faxx_alias0, -3);
    }

    MemoryRegion *fcxx_alias0 = g_new(MemoryRegion, 1);
    MemoryRegion *mr_fcxx = memory_region_find(sysmem, CALYPSO_MMIO_FCXX, 1).mr;
    if (mr_fcxx) {
        memory_region_init_alias(fcxx_alias0, NULL, "calypso.fcxx_alias0",
                                mr_fcxx, 0, CALYPSO_PERIPH_SIZE);
        memory_region_add_subregion_overlap(sysmem, 0x00000000, fcxx_alias0, -4);
    }

    MemoryRegion *f9xx_alias0100 = g_new(MemoryRegion, 1);
    MemoryRegion *mr_f9xx = memory_region_find(sysmem, CALYPSO_MMIO_F9XX, 1).mr;
    if (mr_f9xx) {
        memory_region_init_alias(f9xx_alias0100, NULL, "calypso.f9xx_alias0100",
                                mr_f9xx, 0, CALYPSO_PERIPH_SIZE);
        memory_region_add_subregion_overlap(sysmem, 0x00000100, f9xx_alias0100, -5);
    }

    /* Load firmware (bare-metal ELF, not Linux) */
    if (machine->kernel_filename) {
        uint64_t entry;
        if (load_elf(machine->kernel_filename, NULL, NULL, NULL,
                     &entry, NULL, NULL, NULL,
                     0, EM_ARM, 1, 0) < 0) {
            error_report("Could not load ELF: %s", machine->kernel_filename);
            exit(1);
        }
        cpu_set_pc(CPU(s->cpu), entry);

        /* Patch flash pointer */
        calypso_patch_flash_pointer(cpu_get_address_space(CPU(s->cpu), 0));
    }
}

static void calypso_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    
    mc->desc = "Calypso SoC minimal machine";
    mc->init = calypso_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm946");
}

static const TypeInfo calypso_machine_type = {
    .name = MACHINE_TYPE_NAME("calypso-min"),
    .parent = TYPE_MACHINE,
    .class_init = calypso_machine_class_init,
};

static void calypso_register_types(void)
{
    type_register_static(&calypso_machine_type);
}

type_init(calypso_register_types)
