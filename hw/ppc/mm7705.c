#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "cpu.h"
#include "sysemu/reset.h"
#include "sysemu/sysemu.h"
#include "hw/ppc/ppc.h"
#include "hw/char/pl011.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"

typedef struct {
    MachineState parent;

    PowerPCCPU *cpu;

    PL011State uart0;
} MM7705MachineState;

#define TYPE_MM7705_MACHINE MACHINE_TYPE_NAME("mm7705")
#define MM7705_MACHINE(obj) \
    OBJECT_CHECK(MM7705MachineState, obj, TYPE_MM7705_MACHINE)

static void create_initial_mapping(CPUPPCState *env)
{
    ppcemb_tlb_t *tlb = &env->tlb.tlbe[0];

    tlb->attr = 0;
    tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_WRITE | PAGE_EXEC) << 4);
    tlb->size = 4*KiB;
    tlb->EPN = 0xfffff000 & TARGET_PAGE_MASK;
    tlb->RPN = 0x3fffffff000 & TARGET_PAGE_MASK;
    tlb->PID = 0;
}

static void cpu_reset_temp(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));

    /* Create mapping */
    create_initial_mapping(&cpu->env);
}

static uint32_t dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void mm7705_init(MachineState *machine)
{
    MM7705MachineState *s = MM7705_MACHINE(machine);

    /* init CPUs */
    s->cpu = POWERPC_CPU(cpu_create(machine->cpu_type));
    ppc_booke_timers_init(s->cpu, 400000000, 0);


    ppc_dcr_init(&s->cpu->env, NULL, NULL);

    uint32_t i;
    for (i = 0x10; i < 0xd0; i++) {
        ppc_dcr_register(&s->cpu->env, i, NULL, dcr_read, dcr_write);
    }

    for (i = 0x80000100; i < 0x80001300; i++) {
        ppc_dcr_register(&s->cpu->env, i, NULL, dcr_read, dcr_write);
    }

    for (i = 0x80010000; i < 0x80070000; i++) {
        ppc_dcr_register(&s->cpu->env, i, NULL, dcr_read, dcr_write);
    }

    for (i = 0x80100000; i < 0x801a0000; i++) {
        ppc_dcr_register(&s->cpu->env, i, NULL, dcr_read, dcr_write);
    }

    for (i = 0x80300000; i < 0x80310000; i++) {
        ppc_dcr_register(&s->cpu->env, i, NULL, dcr_read, dcr_write);
    }

    for (i = 0xffc00000; i < 0xffc40000; i++) {
        ppc_dcr_register(&s->cpu->env, i, NULL, dcr_read, dcr_write);
    }


    MemoryRegion *EM0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(EM0, NULL, "EM0", 0x200000000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x0, EM0);

    MemoryRegion *EM1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(EM1, NULL, "EM1", 0x200000000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x200000000, EM1);

    MemoryRegion *EM2 = g_new(MemoryRegion, 1);
    memory_region_init_ram(EM2, NULL, "EM2", 0x200000000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x400000000, EM2);

    MemoryRegion *EM3 = g_new(MemoryRegion, 1);
    memory_region_init_ram(EM3, NULL, "EM3", 0x200000000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x600000000, EM3);


    MemoryRegion *IM0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(IM0, NULL, "IM0", 0x40000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1000040000, IM0);


    MemoryRegion *IFSYS0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(IFSYS0, NULL, "IFSYS0", 0x28000000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1010000000, IFSYS0);

    MemoryRegion *APB0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(APB0, NULL, "APB0", 0x10000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1038000000, APB0);

    MemoryRegion *APB1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(APB1, NULL, "APB1", 0x14000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1038010000, APB1);


    MemoryRegion *NIC301_A_CFG = g_new(MemoryRegion, 1);
    memory_region_init_ram(NIC301_A_CFG, NULL, "NIC301_A_CFG", 0x100000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1038100000, NIC301_A_CFG);

    MemoryRegion *NIC301_DSP0_CFG = g_new(MemoryRegion, 1);
    memory_region_init_ram(NIC301_DSP0_CFG, NULL, "NIC301_DSP0_CFG", 0x100000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1038200000, NIC301_DSP0_CFG);

    MemoryRegion *NIC301_DSP1_CFG = g_new(MemoryRegion, 1);
    memory_region_init_ram(NIC301_DSP1_CFG, NULL, "NIC301_DSP1_CFG", 0x100000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1038300000, NIC301_DSP1_CFG);


    MemoryRegion *DSP0_NM0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(DSP0_NM0, NULL, "DSP0_NM0", 0x20000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1039000000, DSP0_NM0);

    MemoryRegion *DSP0_NM1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(DSP0_NM1, NULL, "DSP0_NM1", 0x20000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1039020000, DSP0_NM1);

    MemoryRegion *DSP1_NM0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(DSP1_NM0, NULL, "DSP1_NM0", 0x20000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1039040000, DSP1_NM0);

    MemoryRegion *DSP1_NM1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(DSP1_NM1, NULL, "DSP1_NM1", 0x20000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1039060000, DSP1_NM1);

    MemoryRegion *I2S = g_new(MemoryRegion, 1);
    memory_region_init_ram(I2S, NULL, "I2S", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1039080000, I2S);

    MemoryRegion *SPDIF = g_new(MemoryRegion, 1);
    memory_region_init_ram(SPDIF, NULL, "SPDIF", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1039081000, SPDIF);


    MemoryRegion *IFSYS1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(IFSYS1, NULL, "IFSYS1", 0x4000000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x103c000000, IFSYS1);

    if (serial_hd(0)) {
        object_initialize_child(OBJECT(s), "uart0", &s->uart0, TYPE_PL011);
        qdev_prop_set_chr(DEVICE(&s->uart0), "chardev", serial_hd(0));
        sysbus_realize(SYS_BUS_DEVICE(&s->uart0), &error_fatal);
        SysBusDevice *busdev = SYS_BUS_DEVICE(&s->uart0);
        // overlap with higher priority (default is 0)
        memory_region_add_subregion_overlap(get_system_memory(), 0x103c05d000,
                                    sysbus_mmio_get_region(busdev, 0), 1);
    }


    MemoryRegion *BOOT_ROM_1 = g_new(MemoryRegion, 1);
    memory_region_init_rom(BOOT_ROM_1, NULL, "BOOT_ROM_1", 0x40000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1100000000, BOOT_ROM_1);


    MemoryRegion *XHSIF0 = g_new(MemoryRegion, 1);
    memory_region_init_rom(XHSIF0, NULL, "XHSIF0", 0x100000000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1200000000, XHSIF0);

    MemoryRegion *XHSIF1 = g_new(MemoryRegion, 1);
    memory_region_init_rom(XHSIF1, NULL, "XHSIF1", 0x100000000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1300000000, XHSIF1);


    MemoryRegion *BOOT_ROM = g_new(MemoryRegion, 1);
    memory_region_init_rom(BOOT_ROM, NULL, "BOOT_ROM", 0x40000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x3fffffc0000, BOOT_ROM);

    // FIXME: не надо ли как-то по-другому помещать прошивку в память?
    {
        uint32_t file_size = 256*KiB;
        uint8_t data[256*KiB];
        int fd = open(machine->firmware, O_RDONLY);

        if (fd == -1) {
            printf("No bios file '%s' found\n", machine->firmware);
            exit(-1);
        }

        if (read(fd, data, file_size) != file_size) {
            printf("File size is less then expected %u bytes\n", file_size);
        }

        close(fd);

        address_space_write_rom(&address_space_memory, 0x3fffffc0000,
            MEMTXATTRS_UNSPECIFIED, data, file_size);
    }

    qemu_register_reset(cpu_reset_temp, s->cpu);
}

static void mm7705_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "MM7705 board";
    mc->init = mm7705_init;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("476fp");
    // FIXME: используется ли данное имя и вообще к чему относится
    mc->default_ram_id = "mm7705.ram";
}

static const TypeInfo mm7705_info = {
    .name = TYPE_MM7705_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(MM7705MachineState),
    .class_init = mm7705_class_init,
};

static void mm7705_machines_init(void)
{
    type_register_static(&mm7705_info);
}

type_init(mm7705_machines_init)
