#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "net/net.h"

#include "target/csky/cpu.h"
#include "hw/sysbus.h"
#include "hw/boards.h"
#include "hw/csky/csky.h"
#include "hw/csky/cskydev.h"
#include "hw/char/csky_uart.h"

static struct csky_boot_info virt_binfo = {
        .loader_start   = 0,
        .dtb_addr       = 0x8f000000,
        .magic          = 0x20150401,
        .freq           = 50000000ll,
};

static void virt_init(MachineState *machine)
{
        ObjectClass     *cpu_oc;
        Object          *cpuobj;
        CSKYCPU         *cpu;

        DeviceState     *intc;

        /*
         * Prepare RAM.
         */
        MemoryRegion *sysmem = get_system_memory();
        MemoryRegion *ram = g_new(MemoryRegion, 1);

        memory_region_allocate_system_memory(ram, NULL, "ram", 0x1f400000);
        memory_region_add_subregion(sysmem, 0, ram);

        /*
         * Prepare CPU
         */
#ifdef TARGET_CSKYV2
        machine->cpu_model = "ck810f";
#else
        machine->cpu_model = "ck610ef";
#endif

        cpu_oc = cpu_class_by_name(TYPE_CSKY_CPU, machine->cpu_model);
        if (!cpu_oc) {
                fprintf(stderr, "Unable to find CPU definition\n");
                exit(1);
        }

        cpuobj = object_new(object_class_get_name(cpu_oc));
        object_property_set_bool(cpuobj, true, "realized", &error_fatal);

        cpu = CSKY_CPU(cpuobj);

        /*
         * use C-SKY interrupt controller
         */
        intc = sysbus_create_simple(
                        "csky_intc",
                        0x1ffff000,
                        *csky_intc_init_cpu(&cpu->env));

        /*
         * use dw-apb-timer
         */
        csky_timer_set_freq(virt_binfo.freq);
        sysbus_create_varargs(
                        "csky_timer",
                        0x1fffd000,
                        qdev_get_gpio_in(intc, 1),
                        qdev_get_gpio_in(intc, 2),
                        NULL);

        /*
         * use 16650a uart.
         */
        csky_uart_create(
                        0x1fffe000,
                        qdev_get_gpio_in(intc, 3),
                        serial_hds[0]);

        /*
         * for qemu exit, use cmd poweroff.
         */
        sysbus_create_simple("csky_exit", 0x1fffc000, NULL);

        /*
         * add net, io-len is 2K.
         */
        csky_mac_v2_create(&nd_table[0], 0x1fffa000, qdev_get_gpio_in(intc, 4));

        /*
         * boot up kernel with unaligned_access and mmu on.
         */
#ifdef TARGET_CSKYV2
        cpu->env.features |= UNALIGNED_ACCESS;
#endif
        cpu->env.mmu_default = 1;

        virt_binfo.kernel_filename = machine->kernel_filename;
        csky_load_kernel(cpu, &virt_binfo);
}

static void virt_class_init(ObjectClass *oc, void *data)
{
        MACHINE_CLASS(oc)->desc = "C-SKY QEMU virt machine";
        MACHINE_CLASS(oc)->init = virt_init;
}

static const TypeInfo virt_type = {
        .name           = MACHINE_TYPE_NAME("virt"),
        .parent         = TYPE_MACHINE,
        .class_init     = virt_class_init,
};

static void virt_machine_init(void)
{
        type_register_static(&virt_type);
}

type_init(virt_machine_init)

