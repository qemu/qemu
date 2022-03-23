#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "cpu.h"
#include "sysemu/reset.h"
#include "sysemu/sysemu.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/dcr_mpic.h"
#include "hw/char/pl011.h"
#include "hw/ssi/pl022.h"
#include "hw/ssi/ssi.h"
#include "hw/net/greth.h"
#include "hw/sd/keyasic_sd.h"
#include "hw/irq.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"

typedef struct {
    MachineState parent;

    PowerPCCPU *cpu;

    MpicState mpic;

    PL011State uart0;

    struct PL022State spi0;

    DeviceState *lsif0_mgpio[11];

    DeviceState *lsif1_gpio[2];
    DeviceState *lsif1_mgpio[5];

    GRETHState greth[3];
    GRETHState gb_greth[2];

    KeyasicSdState sdio;
} MM7705MachineState;

#define TYPE_MM7705_MACHINE MACHINE_TYPE_NAME("mm7705")
#define MM7705_MACHINE(obj) \
    OBJECT_CHECK(MM7705MachineState, obj, TYPE_MM7705_MACHINE)

/* DCR registers */
static int dcr_read_error(int dcrn)
{
    printf("DCR: error reading register with address 0x%x\n", dcrn);
    return -1;
}

static int dcr_write_error(int dcrn)
{
    printf("DCR: error writing register with address 0x%x\n", dcrn);
    return -1;
}

static uint32_t plb4arb8m_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void plb4arb8m_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_plb4arb8m_register(CPUPPCState *env, uint32_t base)
{
    ppc_dcr_register(env, base + 0x2, NULL, plb4arb8m_dcr_read, plb4arb8m_dcr_write);
    ppc_dcr_register(env, base + 0x3, NULL, plb4arb8m_dcr_read, plb4arb8m_dcr_write);
    ppc_dcr_register(env, base + 0x4, NULL, plb4arb8m_dcr_read, plb4arb8m_dcr_write);
    ppc_dcr_register(env, base + 0x6, NULL, plb4arb8m_dcr_read, plb4arb8m_dcr_write);
    ppc_dcr_register(env, base + 0x7, NULL, plb4arb8m_dcr_read, plb4arb8m_dcr_write);
}

static uint32_t itrace_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void itrace_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_itrace_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0; i <= 0xb; i++) {
        ppc_dcr_register(env, base + i, NULL, itrace_dcr_read, itrace_dcr_write);
    }
}

static uint32_t ltrace_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void ltrace_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_ltrace_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0; i <= 0x15; i++) {
        ppc_dcr_register(env, base + i, NULL, ltrace_dcr_read, ltrace_dcr_write);
    }
}

static uint32_t dmaplb6_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void dmaplb6_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_dmaplb6_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0; i <= 0x4b; i++) {
        ppc_dcr_register(env, base + i, NULL, dmaplb6_dcr_read, dmaplb6_dcr_write);
    }
}

static uint32_t p6bc_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void p6bc_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_p6bc_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0; i <= 0x11; i++) {
        ppc_dcr_register(env, base + i, NULL, p6bc_dcr_read, p6bc_dcr_write);
    }
}

static uint32_t dcrarb_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void dcrarb_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_dcrarb_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0; i <= 0x7; i++) {
        ppc_dcr_register(env, base + i, NULL, dcrarb_dcr_read, dcrarb_dcr_write);
    }
}

static uint32_t ddr_graif_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void ddr_graif_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_ddr_graif_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i <= 0xfb; i++) {
        ppc_dcr_register(env, base + i, NULL, ddr_graif_dcr_read, ddr_graif_dcr_write);
    }
}

static uint32_t ddr_aximcif2_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void ddr_aximcif2_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_ddr_aximcif2_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i <= 0x20; i++) {
        ppc_dcr_register(env, base + i, NULL, ddr_aximcif2_dcr_read, ddr_aximcif2_dcr_write);
    }
}

static uint32_t ddr_mclfir_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void ddr_mclfir_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_ddr_mclfir_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i <= 0x35; i++) {
        ppc_dcr_register(env, base + i, NULL, ddr_mclfir_dcr_read, ddr_mclfir_dcr_write);
    }
}

static uint32_t ddr_plb6mcif2_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void ddr_plb6mcif2_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_ddr_plb6mcif2_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i <= 0x3f; i++) {
        ppc_dcr_register(env, base + i, NULL, ddr_plb6mcif2_dcr_read, ddr_plb6mcif2_dcr_write);
    }
}

/* Machine init */
static void create_initial_mapping(CPUPPCState *env)
{
    ppcemb_tlb_t *tlb = &env->tlb.tlbe[0xf0 + 3 * env->tlb_per_way];

    tlb->attr = 0;
    tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_WRITE | PAGE_EXEC) << 4);
    tlb->size = 4*KiB;
    tlb->EPN = 0xfffff000 & TARGET_PAGE_MASK;
    tlb->RPN = 0x3fffffff000;
    tlb->PID = 0;
}

static void cpu_reset_temp(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));

    /* Create mapping */
    create_initial_mapping(&cpu->env);
}

static void mm7705_init(MachineState *machine)
{
    MM7705MachineState *s = MM7705_MACHINE(machine);
    SysBusDevice *busdev;

    /* init CPUs */
    s->cpu = POWERPC_CPU(cpu_create(machine->cpu_type));
    ppc_booke_timers_init(s->cpu, 800000000, 0);

    CPUPPCState *env = &s->cpu->env;
    ppc_dcr_init(env, dcr_read_error, dcr_write_error);

    dcr_plb4arb8m_register(env, 0x00000010);
    dcr_plb4arb8m_register(env, 0x00000020);
    dcr_plb4arb8m_register(env, 0x00000060);
    dcr_plb4arb8m_register(env, 0x00000070);
    dcr_plb4arb8m_register(env, 0x00000080);
    dcr_plb4arb8m_register(env, 0x00000090);
    dcr_plb4arb8m_register(env, 0x000000a0);
    dcr_itrace_register(env, 0x80000900);
    dcr_itrace_register(env, 0x80000a00);
    dcr_ltrace_register(env, 0x80000b00);
    dcr_ltrace_register(env, 0x80000c00);
    dcr_dmaplb6_register(env, 0x80000100);
    dcr_dmaplb6_register(env, 0x80000d00);
    dcr_p6bc_register(env, 0x80000200);
    dcr_dcrarb_register(env, 0x80000800);

    dcr_ddr_plb6mcif2_register(env, 0x80010000);
    dcr_ddr_aximcif2_register(env, 0x80020000);
    dcr_ddr_mclfir_register(env, 0x80030000);
    dcr_ddr_graif_register(env, 0x80040000);
    dcr_ddr_graif_register(env, 0x80050000);

    dcr_ddr_plb6mcif2_register(env, 0x80100000);
    dcr_ddr_aximcif2_register(env, 0x80110000);
    dcr_ddr_mclfir_register(env, 0x80120000);
    dcr_ddr_graif_register(env, 0x80130000);
    dcr_ddr_graif_register(env, 0x80140000);

    dcr_ddr_plb6mcif2_register(env, 0x80160000);
    dcr_ddr_plb6mcif2_register(env, 0x80180000);

    object_initialize_child(OBJECT(s), "mpic", &s->mpic, TYPE_MPIC);
    object_property_set_int(OBJECT(&s->mpic), "baseaddr", 0xffc00000, &error_fatal);
    object_property_set_link(OBJECT(&s->mpic), "cpu-state", OBJECT(s->cpu), &error_fatal);
    qdev_realize(DEVICE(&s->mpic), NULL, &error_fatal);
    qdev_connect_gpio_out_named(DEVICE(&s->mpic), "non_crit_int", 0,
                                ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_INT]);
    qdev_connect_gpio_out_named(DEVICE(&s->mpic), "crit_int", 0,
                                ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_CINT]);


    /* Board has separated AXI bus for all peripherial devices */
    MemoryRegion *axi_mem = g_new(MemoryRegion, 1);
    AddressSpace *axi_addr_space = g_new(AddressSpace, 1);
    memory_region_init(axi_mem, NULL, "axi_mem", ~0u);
    address_space_init(axi_addr_space, axi_mem, "axi_addr_space");


    MemoryRegion *EM0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(EM0, NULL, "EM0", 0x200000000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x0, EM0);

    MemoryRegion *EM0_alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(EM0_alias, NULL, "EM0_alias", EM0, 0, 0x40000000);
    memory_region_add_subregion(axi_mem, 0x40000000, EM0_alias);

    MemoryRegion *EM1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(EM1, NULL, "EM1", 0x200000000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x200000000, EM1);

    MemoryRegion *EM1_alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(EM1_alias, NULL, "EM1_alias", EM1, 0, 0x80000000);
    memory_region_add_subregion(axi_mem, 0x80000000, EM1_alias);

    MemoryRegion *EM2 = g_new(MemoryRegion, 1);
    memory_region_init_alias(EM2, NULL, "EM2", EM0, 0, 0x200000000);
    memory_region_add_subregion(get_system_memory(), 0x400000000, EM2);

    MemoryRegion *EM3 = g_new(MemoryRegion, 1);
    memory_region_init_alias(EM3, NULL, "EM3", EM1, 0, 0x200000000);
    memory_region_add_subregion(get_system_memory(), 0x600000000, EM3);


    MemoryRegion *IM0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(IM0, NULL, "IM0", 0x40000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1000040000, IM0);

    MemoryRegion *IM0_alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(IM0_alias, NULL, "IM0_alias", IM0, 0, 0x40000);
    memory_region_add_subregion(axi_mem, 0x0, IM0_alias);


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
    // make this memory as fallback
    memory_region_add_subregion_overlap(get_system_memory(), 0x103c000000,
                                IFSYS1, -10);

    if (serial_hd(0)) {
        object_initialize_child(OBJECT(s), "uart0", &s->uart0, TYPE_PL011);
        qdev_prop_set_chr(DEVICE(&s->uart0), "chardev", serial_hd(0));
        sysbus_realize(SYS_BUS_DEVICE(&s->uart0), &error_fatal);
        SysBusDevice *busdev = SYS_BUS_DEVICE(&s->uart0);
        memory_region_add_subregion(get_system_memory(), 0x103c05d000,
                                    sysbus_mmio_get_region(busdev, 0));
    }

    {
        object_initialize_child(OBJECT(s), "eth0", &s->greth[0], TYPE_GRETH);
        greth_change_address_space(&s->greth[0], axi_addr_space, &error_fatal);
        sysbus_realize(SYS_BUS_DEVICE(&s->greth[0]), &error_fatal);
        SysBusDevice *busdev = SYS_BUS_DEVICE(&s->greth[0]);
        memory_region_add_subregion(get_system_memory(), 0x103c035000,
                                    sysbus_mmio_get_region(busdev, 0));

        object_initialize_child(OBJECT(s), "eth1", &s->greth[1], TYPE_GRETH);
        greth_change_address_space(&s->greth[1], axi_addr_space, &error_fatal);
        sysbus_realize(SYS_BUS_DEVICE(&s->greth[1]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->greth[1]);
        memory_region_add_subregion(get_system_memory(), 0x103c036000,
                                    sysbus_mmio_get_region(busdev, 0));

        object_initialize_child(OBJECT(s), "eth2", &s->greth[2], TYPE_GRETH);
        // if (nd_table[0].used) {
        //     qemu_check_nic_model(&nd_table[0], TYPE_GRETH);
        //     qdev_set_nic_properties(DEVICE(&s->greth[2]), &nd_table[0]);
        // }
        greth_change_address_space(&s->greth[2], axi_addr_space, &error_fatal);
        sysbus_realize(SYS_BUS_DEVICE(&s->greth[2]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->greth[2]);
        memory_region_add_subregion(get_system_memory(), 0x103c037000,
                                    sysbus_mmio_get_region(busdev, 0));

        object_initialize_child(OBJECT(s), "gbit_eth0", &s->gb_greth[0], TYPE_GRETH);
        greth_change_address_space(&s->gb_greth[0], axi_addr_space, &error_fatal);
        sysbus_realize(SYS_BUS_DEVICE(&s->gb_greth[0]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->gb_greth[0]);
        memory_region_add_subregion(get_system_memory(), 0x103c033000,
                                    sysbus_mmio_get_region(busdev, 0));

        object_initialize_child(OBJECT(s), "gbit_eth1", &s->gb_greth[1], TYPE_GRETH);
        if (nd_table[0].used) {
            qemu_check_nic_model(&nd_table[0], TYPE_GRETH);
            qdev_set_nic_properties(DEVICE(&s->gb_greth[1]), &nd_table[0]);
        }
        greth_change_address_space(&s->gb_greth[1], axi_addr_space, &error_fatal);
        sysbus_realize(SYS_BUS_DEVICE(&s->gb_greth[1]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->gb_greth[1]);
        memory_region_add_subregion(get_system_memory(), 0x103c034000,
                                    sysbus_mmio_get_region(busdev, 0));
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(&s->mpic), 108));
    }

    {
        object_initialize_child(OBJECT(s), "sdio", &s->sdio, TYPE_KEYASIC_SD);
        sysbus_realize(SYS_BUS_DEVICE(&s->sdio), &error_fatal);
        SysBusDevice *busdev = SYS_BUS_DEVICE(&s->sdio);
        memory_region_add_subregion(get_system_memory(), 0x103c064000,
                                    sysbus_mmio_get_region(busdev, 0));
    }

    {
        // FIXME: connect gpio IRQs to corresponding MPIC IRQ lines
        for (int i = 0; i < ARRAY_SIZE(s->lsif0_mgpio); i++) {
            s->lsif0_mgpio[i] = sysbus_create_simple("pl061", 0x103c040000 + 0x1000*i, NULL);
        }
        
        for (int i = 0; i < ARRAY_SIZE(s->lsif1_gpio); i++) {
            s->lsif1_gpio[i] = sysbus_create_simple("pl061", 0x103c065000 + 0x1000*i, NULL);
        }

        for (int i = 0; i < ARRAY_SIZE(s->lsif1_mgpio); i++) {
            s->lsif1_mgpio[i] = sysbus_create_simple("pl061", 0x103c067000 + 0x1000*i, NULL);
        }
    }

    {
        object_initialize_child(OBJECT(s), "spi0", &s->spi0, TYPE_PL022);
        sysbus_realize(SYS_BUS_DEVICE(&s->spi0), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->spi0);
        memory_region_add_subregion(get_system_memory(), 0x103c061000,
                                    sysbus_mmio_get_region(busdev, 0));

        DeviceState *flash_dev = qdev_new("m25p32");
        DriveInfo *dinfo = drive_get_next(IF_MTD);
        if (dinfo) {
            struct BlockBackend *blk = blk_by_legacy_dinfo(dinfo);
            qdev_prop_set_drive_err(flash_dev, "drive",
                                    blk,
                                    &error_fatal);
        }
        // Our flash has 1 dummy cycle (or at least with this value it works)
        // So we take default value and set dummy cycles to 1
        object_property_set_int(OBJECT(flash_dev), "nonvolatile-cfg", 0x1fff, &error_fatal);
        qdev_realize(flash_dev, BUS(s->spi0.ssi), &error_fatal);

        // Connect spi_flash chip select (cs pin) to 2nd pin of gpio1
        qdev_connect_gpio_out(s->lsif1_gpio[1], 2,
                              qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0));
    }

    MemoryRegion *BOOT_ROM_1 = g_new(MemoryRegion, 1);
    memory_region_init_rom(BOOT_ROM_1, NULL, "BOOT_ROM_1", 0x40000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1100000000, BOOT_ROM_1);

    MemoryRegion *BOOT_ROM_1_alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(BOOT_ROM_1_alias, NULL, "BOOT_ROM_1_alias", BOOT_ROM_1, 0, 0x40000);
    memory_region_add_subregion(axi_mem, 0x40000, BOOT_ROM_1_alias);


    MemoryRegion *XHSIF0 = g_new(MemoryRegion, 1);
    memory_region_init_rom(XHSIF0, NULL, "XHSIF0", 0x100000000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1200000000, XHSIF0);

    MemoryRegion *XHSIF1 = g_new(MemoryRegion, 1);
    memory_region_init_rom(XHSIF1, NULL, "XHSIF1", 0x100000000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1300000000, XHSIF1);


    MemoryRegion *BOOT_ROM = g_new(MemoryRegion, 1);
    memory_region_init_rom(BOOT_ROM, NULL, "BOOT_ROM", 0x40000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x3fffffc0000, BOOT_ROM);

    qemu_register_reset(cpu_reset_temp, s->cpu);
}

static void mm7705_reset(MachineState *machine)
{
    MM7705MachineState *s = MM7705_MACHINE(machine);

    // default action
    qemu_devices_reset();

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

    // STCL
    uint8_t boot_cfg = 0x16;
    if (address_space_write(&address_space_memory, 0x1038000000 + 0x3,
            MEMTXATTRS_UNSPECIFIED, &boot_cfg, 1) != 0) {
        printf("shit!!1\n");
    }

    uint8_t pll_state = 0x3f;
    if (address_space_write(&address_space_memory, 0x1038000004  + 0x3,
            MEMTXATTRS_UNSPECIFIED, &pll_state, 1) != 0) {
        printf("shit!!2\n");
    }

    // Set GPIO0 pins
    for (int i = 0; boot_cfg; i++, boot_cfg >>= 1) {
        if (boot_cfg & (1<<i)) {
            qemu_irq_raise(qdev_get_gpio_in(s->lsif1_gpio[0], i));
        }
    }

    // Disables SD card presence (1st pin of gpio1)
    qemu_irq_raise(qdev_get_gpio_in(s->lsif1_gpio[1], 1));
}

static void mm7705_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "MM7705 board";
    mc->init = mm7705_init;
    mc->reset = mm7705_reset;
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
