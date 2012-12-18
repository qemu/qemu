/*
 * QEMU S390 virtio target
 *
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 * Copyright IBM Corp 2012
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * Contributions after 2012-10-29 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 *
 * You should have received a copy of the GNU (Lesser) General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw.h"
#include "block/block.h"
#include "sysemu/blockdev.h"
#include "sysemu/sysemu.h"
#include "net/net.h"
#include "boards.h"
#include "monitor/monitor.h"
#include "loader.h"
#include "hw/virtio.h"
#include "hw/sysbus.h"
#include "sysemu/kvm.h"
#include "exec/address-spaces.h"

#include "hw/s390-virtio-bus.h"
#include "hw/s390x/sclp.h"

//#define DEBUG_S390

#ifdef DEBUG_S390
#define dprintf(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

#define KVM_S390_VIRTIO_NOTIFY          0
#define KVM_S390_VIRTIO_RESET           1
#define KVM_S390_VIRTIO_SET_STATUS      2

#define MAX_BLK_DEVS                    10

static VirtIOS390Bus *s390_bus;
static S390CPU **ipi_states;

S390CPU *s390_cpu_addr2state(uint16_t cpu_addr)
{
    if (cpu_addr >= smp_cpus) {
        return NULL;
    }

    return ipi_states[cpu_addr];
}

int s390_virtio_hypercall(CPUS390XState *env, uint64_t mem, uint64_t hypercall)
{
    int r = 0, i;

    dprintf("KVM hypercall: %ld\n", hypercall);
    switch (hypercall) {
    case KVM_S390_VIRTIO_NOTIFY:
        if (mem > ram_size) {
            VirtIOS390Device *dev = s390_virtio_bus_find_vring(s390_bus,
                                                               mem, &i);
            if (dev) {
                virtio_queue_notify(dev->vdev, i);
            } else {
                r = -EINVAL;
            }
        } else {
            /* Early printk */
        }
        break;
    case KVM_S390_VIRTIO_RESET:
    {
        VirtIOS390Device *dev;

        dev = s390_virtio_bus_find_mem(s390_bus, mem);
        virtio_reset(dev->vdev);
        stb_phys(dev->dev_offs + VIRTIO_DEV_OFFS_STATUS, 0);
        s390_virtio_device_sync(dev);
        s390_virtio_reset_idx(dev);
        break;
    }
    case KVM_S390_VIRTIO_SET_STATUS:
    {
        VirtIOS390Device *dev;

        dev = s390_virtio_bus_find_mem(s390_bus, mem);
        if (dev) {
            s390_virtio_device_update_status(dev);
        } else {
            r = -EINVAL;
        }
        break;
    }
    default:
        r = -EINVAL;
        break;
    }

    return r;
}

/*
 * The number of running CPUs. On s390 a shutdown is the state of all CPUs
 * being either stopped or disabled (for interrupts) waiting. We have to
 * track this number to call the shutdown sequence accordingly. This
 * number is modified either on startup or while holding the big qemu lock.
 */
static unsigned s390_running_cpus;

void s390_add_running_cpu(CPUS390XState *env)
{
    if (env->halted) {
        s390_running_cpus++;
        env->halted = 0;
        env->exception_index = -1;
    }
}

unsigned s390_del_running_cpu(CPUS390XState *env)
{
    if (env->halted == 0) {
        assert(s390_running_cpus >= 1);
        s390_running_cpus--;
        env->halted = 1;
        env->exception_index = EXCP_HLT;
    }
    return s390_running_cpus;
}

/* PC hardware initialisation */
static void s390_init(QEMUMachineInitArgs *args)
{
    ram_addr_t my_ram_size = args->ram_size;
    const char *cpu_model = args->cpu_model;
    CPUS390XState *env = NULL;
    DeviceState *dev;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    int shift = 0;
    uint8_t *storage_keys;
    void *virtio_region;
    hwaddr virtio_region_len;
    hwaddr virtio_region_start;
    int i;

    /* s390x ram size detection needs a 16bit multiplier + an increment. So
       guests > 64GB can be specified in 2MB steps etc. */
    while ((my_ram_size >> (20 + shift)) > 65535) {
        shift++;
    }
    my_ram_size = my_ram_size >> (20 + shift) << (20 + shift);

    /* lets propagate the changed ram size into the global variable. */
    ram_size = my_ram_size;

    /* get a BUS */
    s390_bus = s390_virtio_bus_init(&my_ram_size);
    s390_sclp_init();
    dev  = qdev_create(NULL, "s390-ipl");
    if (args->kernel_filename) {
        qdev_prop_set_string(dev, "kernel", args->kernel_filename);
    }
    if (args->initrd_filename) {
        qdev_prop_set_string(dev, "initrd", args->initrd_filename);
    }
    qdev_prop_set_string(dev, "cmdline", args->kernel_cmdline);
    qdev_init_nofail(dev);

    /* allocate RAM */
    memory_region_init_ram(ram, "s390.ram", my_ram_size);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(sysmem, 0, ram);

    /* clear virtio region */
    virtio_region_len = my_ram_size - ram_size;
    virtio_region_start = ram_size;
    virtio_region = cpu_physical_memory_map(virtio_region_start,
                                            &virtio_region_len, true);
    memset(virtio_region, 0, virtio_region_len);
    cpu_physical_memory_unmap(virtio_region, virtio_region_len, 1,
                              virtio_region_len);

    /* allocate storage keys */
    storage_keys = g_malloc0(my_ram_size / TARGET_PAGE_SIZE);

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = "host";
    }

    ipi_states = g_malloc(sizeof(S390CPU *) * smp_cpus);

    for (i = 0; i < smp_cpus; i++) {
        S390CPU *cpu;
        CPUS390XState *tmp_env;

        cpu = cpu_s390x_init(cpu_model);
        tmp_env = &cpu->env;
        if (!env) {
            env = tmp_env;
        }
        ipi_states[i] = cpu;
        tmp_env->halted = 1;
        tmp_env->exception_index = EXCP_HLT;
        tmp_env->storage_keys = storage_keys;
    }


    /* Create VirtIO network adapters */
    for(i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];
        DeviceState *dev;

        if (!nd->model) {
            nd->model = g_strdup("virtio");
        }

        if (strcmp(nd->model, "virtio")) {
            fprintf(stderr, "S390 only supports VirtIO nics\n");
            exit(1);
        }

        dev = qdev_create((BusState *)s390_bus, "virtio-net-s390");
        qdev_set_nic_properties(dev, nd);
        qdev_init_nofail(dev);
    }
}

static QEMUMachine s390_machine = {
    .name = "s390-virtio",
    .alias = "s390",
    .desc = "VirtIO based S390 machine",
    .init = s390_init,
    .block_default_type = IF_VIRTIO,
    .no_cdrom = 1,
    .no_floppy = 1,
    .no_serial = 1,
    .no_parallel = 1,
    .no_sdcard = 1,
    .use_virtcon = 1,
    .max_cpus = 255,
    .is_default = 1,
    DEFAULT_MACHINE_OPTIONS,
};

static void s390_machine_init(void)
{
    qemu_register_machine(&s390_machine);
}

machine_init(s390_machine_init);

