/*
 * Calypso SoC "high" machine for OsmocomBB highram firmware
 * QEMU 9.2 — QOM-based peripheral instantiation
 *
 * Peripherals are now separate QOM SysBusDevices:
 *   - calypso-inth   (hw/intc/calypso_inth.c)
 *   - calypso-timer   (hw/timer/calypso_timer.c)  ×2
 *   - calypso-uart    (hw/char/calypso_uart.c)    ×2
 *   - calypso-spi     (hw/ssi/calypso_spi.c)
 *   - calypso_trx.c   (DSP/TPU/TRX bridge, already modular)
 *
 * Usage:
 *   qemu-system-arm -M calypso-high -cpu arm946 \
 *     -kernel loader.highram.elf -serial pty -monitor stdio -nographic -s -S
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/sysbus.h"

#include "sysemu/sysemu.h"
#include "sysemu/blockdev.h"
#include "sysemu/block-backend.h"
#include "hw/qdev-properties.h"
#include "hw/loader.h"
#include "elf.h"
#include "hw/block/flash.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"
#include "hw/char/serial.h"
#include "chardev/char-fe.h"
#include "qemu/timer.h"
#include "hw/irq.h"

/* QOM device headers (all in hw/arm/calypso/) */
#include "calypso_inth.h"
#include "calypso_timer.h"
#include "calypso_uart.h"
#include "calypso_spi.h"
#include "calypso_trx.h"

/* ========================================================================
 * Memory Map
 * ======================================================================== */

/* RAM: internal 256K + external 8M */
#define CALYPSO_IRAM_BASE     0x00800000
#define CALYPSO_IRAM_SIZE     (256 * 1024)
#define CALYPSO_XRAM_BASE     0x01000000
#define CALYPSO_XRAM_SIZE     (8 * 1024 * 1024)

/* Flash */
#define CALYPSO_FLASH_BASE    0x02000000
#define CALYPSO_FLASH_SIZE    (4 * 1024 * 1024)

/* Peripheral base addresses */
#define CALYPSO_MMIO_18XX     0xFFFE1800
#define CALYPSO_SPI_BASE      0xFFFE3000
#define CALYPSO_TIMER1_BASE   0xFFFE3800
#define CALYPSO_KEYPAD_BASE   0xFFFE4800
#define CALYPSO_TIMER2_BASE   0xFFFE6800
#define CALYPSO_MMIO_80XX     0xFFFE8000
#define CALYPSO_MMIO_F0XX     0xFFFEF000
#define CALYPSO_UART_MODEM    0xFFFF5000
#define CALYPSO_UART_IRDA     0xFFFF5800
#define CALYPSO_MMIO_98XX     0xFFFF9800
#define CALYPSO_MMIO_F9XX     0xFFFFF900
#define CALYPSO_INTH_BASE     0xFFFFFA00
#define CALYPSO_SYSTEM_FB     0xFFFFFB00
#define CALYPSO_MMIO_FCXX     0xFFFFFC00
#define CALYPSO_SYSTEM_FD     0xFFFFFD00
#define CALYPSO_MMIO_FFXX     0xFFFFFF00

#define CALYPSO_PERIPH_SIZE   256

/* ========================================================================
 * IRQ numbers (must match calypso_trx.h / OsmocomBB calypso/irq.h)
 * ======================================================================== */

#define IRQ_TIMER1        1
#define IRQ_TIMER2        2
#define IRQ_UART_MODEM    7
#define IRQ_KEYPAD        8
#define IRQ_SPI          13
#define IRQ_UART_IRDA    18

/* ========================================================================
 * Keypad controller stub (simple enough to keep inline)
 * ======================================================================== */

static uint64_t calypso_keypad_read(void *opaque, hwaddr offset, unsigned size)
{
    return 0x0000;
}

static void calypso_keypad_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size)
{
}

static const MemoryRegionOps calypso_keypad_ops = {
    .read = calypso_keypad_read,
    .write = calypso_keypad_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = { .min_access_size = 2, .max_access_size = 2 },
};

/* ========================================================================
 * Generic MMIO stubs
 * ======================================================================== */

static uint64_t calypso_mmio8_read(void *opaque, hwaddr offset, unsigned size)
{
    return 0xFF;
}

static void calypso_mmio8_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size)
{
}

static const MemoryRegionOps calypso_mmio8_ops = {
    .read = calypso_mmio8_read,
    .write = calypso_mmio8_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 1 },
};

static uint64_t calypso_mmio16_read(void *opaque, hwaddr offset, unsigned size)
{
    return 0;
}

static void calypso_mmio16_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size)
{
}

static const MemoryRegionOps calypso_mmio16_ops = {
    .read = calypso_mmio16_read,
    .write = calypso_mmio16_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = { .min_access_size = 2, .max_access_size = 2 },
};

/* ========================================================================
 * Machine State
 * ======================================================================== */

typedef struct CalypsoHighState {
    ARMCPU *cpu;

    /* Memory regions */
    MemoryRegion iram;
    MemoryRegion xram;
    MemoryRegion ram_alias0;
    MemoryRegion high_vectors;

    /* QOM devices */
    CalypsoINTHState  *inth;
    CalypsoTimerState *timer1;
    CalypsoTimerState *timer2;
    CalypsoUARTState  *uart_modem;
    CalypsoUARTState  *uart_irda;
    CalypsoSPIState   *spi;
} CalypsoHighState;

/* ========================================================================
 * Helpers
 * ======================================================================== */

static void calypso_create_mmio(MemoryRegion *sysmem, const char *name,
                                hwaddr base, const MemoryRegionOps *ops,
                                void *opaque, uint64_t sz)
{
    MemoryRegion *mr = g_new(MemoryRegion, 1);
    memory_region_init_io(mr, NULL, ops, opaque, name, sz);
    memory_region_add_subregion(sysmem, base, mr);
}

/* ========================================================================
 * Machine init
 * ======================================================================== */

static void calypso_high_init(MachineState *machine)
{
    CalypsoHighState *s = g_new0(CalypsoHighState, 1);
    MemoryRegion *sysmem = get_system_memory();
    Object *cpuobj;
    Error *err = NULL;

    /* ---- CPU ---- */
    cpuobj = object_new(machine->cpu_type);
    s->cpu = ARM_CPU(cpuobj);
    if (!qdev_realize(DEVICE(cpuobj), NULL, &err)) {
        error_report_err(err);
        exit(1);
    }

    /* ---- Memory ---- */

    memory_region_init_ram(&s->iram, NULL, "calypso.iram",
                           CALYPSO_IRAM_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, CALYPSO_IRAM_BASE, &s->iram);

    memory_region_init_ram(&s->xram, NULL, "calypso.xram",
                           CALYPSO_XRAM_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, CALYPSO_XRAM_BASE, &s->xram);

    memory_region_init_alias(&s->ram_alias0, NULL, "calypso.ram_alias0",
                             &s->iram, 0, 128 * 1024);
    memory_region_add_subregion_overlap(sysmem, 0x00000000, &s->ram_alias0, 1);

    memory_region_init_alias(&s->high_vectors, NULL, "calypso.high_vectors",
                             &s->iram, 0, 64 * 1024);
    memory_region_add_subregion(sysmem, 0xFFFF0000, &s->high_vectors);

    /* ---- Flash ---- */
    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);
    pflash_cfi01_register(CALYPSO_FLASH_BASE, "calypso.flash",
                          CALYPSO_FLASH_SIZE,
                          dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                          64 * 1024, 1, 0x0089, 0x0018, 0x0000, 0x0000, 0);

    /* ---- INTH (QOM) ---- */
    {
        DeviceState *dev = qdev_new(TYPE_CALYPSO_INTH);
        SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

        sysbus_realize_and_unref(sbd, &error_fatal);
        sysbus_mmio_map(sbd, 0, CALYPSO_INTH_BASE);
        sysbus_connect_irq(sbd, 0,
                           qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));
        sysbus_connect_irq(sbd, 1,
                           qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_FIQ));

        s->inth = CALYPSO_INTH(dev);
    }

    #define INTH_IRQ(n) qdev_get_gpio_in(DEVICE(s->inth), (n))

    /* ---- Timer 1 (IRQ 1) ---- */
    {
        DeviceState *dev = qdev_new(TYPE_CALYPSO_TIMER);
        SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
        sysbus_realize_and_unref(sbd, &error_fatal);
        sysbus_mmio_map(sbd, 0, CALYPSO_TIMER1_BASE);
        sysbus_connect_irq(sbd, 0, INTH_IRQ(IRQ_TIMER1));
        s->timer1 = CALYPSO_TIMER(dev);
    }

    /* ---- Timer 2 (IRQ 2) ---- */
    {
        DeviceState *dev = qdev_new(TYPE_CALYPSO_TIMER);
        SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
        sysbus_realize_and_unref(sbd, &error_fatal);
        sysbus_mmio_map(sbd, 0, CALYPSO_TIMER2_BASE);
        sysbus_connect_irq(sbd, 0, INTH_IRQ(IRQ_TIMER2));
        s->timer2 = CALYPSO_TIMER(dev);
    }

    /* ---- SPI / TWL3025 ABB (IRQ 13) ---- */
    {
        DeviceState *dev = qdev_new(TYPE_CALYPSO_SPI);
        SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
        sysbus_realize_and_unref(sbd, &error_fatal);
        sysbus_mmio_map(sbd, 0, CALYPSO_SPI_BASE);
        sysbus_connect_irq(sbd, 0, INTH_IRQ(IRQ_SPI));
        s->spi = CALYPSO_SPI(dev);
    }

    /* ---- UART Modem (IRQ 7) — no chardev ---- */
    {
        DeviceState *dev = qdev_new(TYPE_CALYPSO_UART);
        qdev_prop_set_string(dev, "label", "modem");

        SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
        sysbus_realize_and_unref(sbd, &error_fatal);
        sysbus_mmio_map(sbd, 0, CALYPSO_UART_MODEM);
        sysbus_connect_irq(sbd, 0, INTH_IRQ(IRQ_UART_MODEM));
        s->uart_modem = CALYPSO_UART(dev);
    }

    /* ---- UART IrDA (IRQ 18) — serial0 for osmocon ---- */
    {
        DeviceState *dev = qdev_new(TYPE_CALYPSO_UART);
        qdev_prop_set_string(dev, "label", "irda");

        Chardev *chr = serial_hd(0);
        if (chr) {
            qdev_prop_set_chr(dev, "chardev", chr);
        }

        SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
        sysbus_realize_and_unref(sbd, &error_fatal);
        sysbus_mmio_map(sbd, 0, CALYPSO_UART_IRDA);
        sysbus_connect_irq(sbd, 0, INTH_IRQ(IRQ_UART_IRDA));
        s->uart_irda = CALYPSO_UART(dev);
    }

    /* ---- Keypad (IRQ 8) — inline stub ---- */
    {
        MemoryRegion *mr = g_new(MemoryRegion, 1);
        memory_region_init_io(mr, NULL, &calypso_keypad_ops, NULL,
                              "calypso.keypad", CALYPSO_PERIPH_SIZE);
        memory_region_add_subregion(sysmem, CALYPSO_KEYPAD_BASE, mr);
    }

    /* ---- MMIO stubs ---- */
    calypso_create_mmio(sysmem, "calypso.mmio_18xx",
                        CALYPSO_MMIO_18XX, &calypso_mmio8_ops, NULL,
                        CALYPSO_PERIPH_SIZE);
    calypso_create_mmio(sysmem, "calypso.mmio_80xx",
                        CALYPSO_MMIO_80XX, &calypso_mmio8_ops, NULL,
                        CALYPSO_PERIPH_SIZE);
    calypso_create_mmio(sysmem, "calypso.mmio_f0xx",
                        CALYPSO_MMIO_F0XX, &calypso_mmio16_ops, NULL,
                        CALYPSO_PERIPH_SIZE);
    calypso_create_mmio(sysmem, "calypso.mmio_98xx",
                        CALYPSO_MMIO_98XX, &calypso_mmio16_ops, NULL,
                        CALYPSO_PERIPH_SIZE);
    calypso_create_mmio(sysmem, "calypso.mmio_f9xx",
                        CALYPSO_MMIO_F9XX, &calypso_mmio16_ops, NULL,
                        CALYPSO_PERIPH_SIZE);
    calypso_create_mmio(sysmem, "calypso.system_fb",
                        CALYPSO_SYSTEM_FB, &calypso_mmio16_ops, NULL,
                        CALYPSO_PERIPH_SIZE);
    calypso_create_mmio(sysmem, "calypso.mmio_fcxx",
                        CALYPSO_MMIO_FCXX, &calypso_mmio16_ops, NULL,
                        CALYPSO_PERIPH_SIZE);
    calypso_create_mmio(sysmem, "calypso.system_fd",
                        CALYPSO_SYSTEM_FD, &calypso_mmio16_ops, NULL,
                        CALYPSO_PERIPH_SIZE);
    calypso_create_mmio(sysmem, "calypso.mmio_ffxx",
                        CALYPSO_MMIO_FFXX, &calypso_mmio8_ops, NULL,
                        CALYPSO_PERIPH_SIZE);
    
    /* ---- TRX bridge ---- */
    {
        qemu_irq *irqs = g_new0(qemu_irq, CALYPSO_NUM_IRQS);
        for (int i = 0; i < CALYPSO_NUM_IRQS; i++) {
            irqs[i] = INTH_IRQ(i);
        }
        calypso_trx_init(sysmem, irqs, 4729);
    }

    #undef INTH_IRQ

    /* ---- Load firmware ---- */
    if (machine->kernel_filename) {
        uint64_t entry;
        if (load_elf(machine->kernel_filename, NULL, NULL, NULL,
                     &entry, NULL, NULL, NULL,
                     0, EM_ARM, 1, 0) < 0) {
            error_report("Could not load ELF: %s", machine->kernel_filename);
            exit(1);
        }
        cpu_set_pc(CPU(s->cpu), entry);
    }
}

/* ========================================================================
 * Machine class
 * ======================================================================== */

static void calypso_high_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Calypso SoC (highram) with INTH, timers, UART, SPI/ABB, TRX";
    mc->init = calypso_high_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm946");
}

static const TypeInfo calypso_high_type = {
    .name = MACHINE_TYPE_NAME("calypso-high"),
    .parent = TYPE_MACHINE,
    .class_init = calypso_high_class_init,
};

static void calypso_high_register_types(void)
{
    type_register_static(&calypso_high_type);
}

type_init(calypso_high_register_types)
