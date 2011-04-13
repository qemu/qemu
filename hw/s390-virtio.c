/*
 * QEMU S390 virtio target
 *
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw.h"
#include "block.h"
#include "blockdev.h"
#include "sysemu.h"
#include "net.h"
#include "boards.h"
#include "monitor.h"
#include "loader.h"
#include "elf.h"
#include "hw/virtio.h"
#include "hw/sysbus.h"
#include "kvm.h"

#include "hw/s390-virtio-bus.h"

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

#define KERN_IMAGE_START                0x010000UL
#define KERN_PARM_AREA                  0x010480UL
#define INITRD_START                    0x800000UL
#define INITRD_PARM_START               0x010408UL
#define INITRD_PARM_SIZE                0x010410UL
#define PARMFILE_START                  0x001000UL

#define ZIPL_START			0x009000UL
#define ZIPL_LOAD_ADDR			0x009000UL
#define ZIPL_FILENAME			"s390-zipl.rom"

#define MAX_BLK_DEVS                    10

static VirtIOS390Bus *s390_bus;
static CPUState **ipi_states;

void irq_info(Monitor *mon);
void pic_info(Monitor *mon);

void irq_info(Monitor *mon)
{
}

void pic_info(Monitor *mon)
{
}

CPUState *s390_cpu_addr2state(uint16_t cpu_addr)
{
    if (cpu_addr >= smp_cpus) {
        return NULL;
    }

    return ipi_states[cpu_addr];
}

int s390_virtio_hypercall(CPUState *env)
{
    int r = 0, i;
    target_ulong mem = env->regs[2];

    dprintf("KVM hypercall: %ld\n", env->regs[1]);
    switch (env->regs[1]) {
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
        s390_virtio_device_sync(dev);
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

    env->regs[2] = r;
    return 0;
}

/* PC hardware initialisation */
static void s390_init(ram_addr_t ram_size,
                      const char *boot_device,
                      const char *kernel_filename,
                      const char *kernel_cmdline,
                      const char *initrd_filename,
                      const char *cpu_model)
{
    CPUState *env = NULL;
    ram_addr_t ram_addr;
    ram_addr_t kernel_size = 0;
    ram_addr_t initrd_offset;
    ram_addr_t initrd_size = 0;
    int i;

    /* XXX we only work on KVM for now */

    if (!kvm_enabled()) {
        fprintf(stderr, "The S390 target only works with KVM enabled\n");
        exit(1);
    }

    /* get a BUS */
    s390_bus = s390_virtio_bus_init(&ram_size);

    /* allocate RAM */
    ram_addr = qemu_ram_alloc(NULL, "s390.ram", ram_size);
    cpu_register_physical_memory(0, ram_size, ram_addr);

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = "host";
    }

    ipi_states = qemu_malloc(sizeof(CPUState *) * smp_cpus);

    for (i = 0; i < smp_cpus; i++) {
        CPUState *tmp_env;

        tmp_env = cpu_init(cpu_model);
        if (!env) {
            env = tmp_env;
        }
        ipi_states[i] = tmp_env;
        tmp_env->halted = 1;
        tmp_env->exception_index = EXCP_HLT;
    }

    env->halted = 0;
    env->exception_index = 0;

    if (kernel_filename) {
        kernel_size = load_image(kernel_filename, qemu_get_ram_ptr(0));

        if (lduw_phys(KERN_IMAGE_START) != 0x0dd0) {
            fprintf(stderr, "Specified image is not an s390 boot image\n");
            exit(1);
        }

        env->psw.addr = KERN_IMAGE_START;
        env->psw.mask = 0x0000000180000000ULL;
    } else {
        ram_addr_t bios_size = 0;
        char *bios_filename;

        /* Load zipl bootloader */
        if (bios_name == NULL) {
            bios_name = ZIPL_FILENAME;
        }

        bios_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
        bios_size = load_image(bios_filename, qemu_get_ram_ptr(ZIPL_LOAD_ADDR));
        qemu_free(bios_filename);

        if ((long)bios_size < 0) {
            hw_error("could not load bootloader '%s'\n", bios_name);
        }

        if (bios_size > 4096) {
            hw_error("stage1 bootloader is > 4k\n");
        }

        env->psw.addr = ZIPL_START;
        env->psw.mask = 0x0000000180000000ULL;
    }

    if (initrd_filename) {
        initrd_offset = INITRD_START;
        while (kernel_size + 0x100000 > initrd_offset) {
            initrd_offset += 0x100000;
        }
        initrd_size = load_image(initrd_filename, qemu_get_ram_ptr(initrd_offset));

        stq_phys(INITRD_PARM_START, initrd_offset);
        stq_phys(INITRD_PARM_SIZE, initrd_size);
    }

    if (kernel_cmdline) {
        cpu_physical_memory_write(KERN_PARM_AREA, kernel_cmdline,
                                  strlen(kernel_cmdline));
    }

    /* Create VirtIO network adapters */
    for(i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];
        DeviceState *dev;

        if (!nd->model) {
            nd->model = qemu_strdup("virtio");
        }

        if (strcmp(nd->model, "virtio")) {
            fprintf(stderr, "S390 only supports VirtIO nics\n");
            exit(1);
        }

        dev = qdev_create((BusState *)s390_bus, "virtio-net-s390");
        qdev_set_nic_properties(dev, nd);
        qdev_init_nofail(dev);
    }

    /* Create VirtIO disk drives */
    for(i = 0; i < MAX_BLK_DEVS; i++) {
        DriveInfo *dinfo;
        DeviceState *dev;

        dinfo = drive_get(IF_IDE, 0, i);
        if (!dinfo) {
            continue;
        }

        dev = qdev_create((BusState *)s390_bus, "virtio-blk-s390");
        qdev_prop_set_drive_nofail(dev, "drive", dinfo->bdrv);
        qdev_init_nofail(dev);
    }
}

static QEMUMachine s390_machine = {
    .name = "s390-virtio",
    .alias = "s390",
    .desc = "VirtIO based S390 machine",
    .init = s390_init,
    .no_serial = 1,
    .no_parallel = 1,
    .use_virtcon = 1,
    .no_vga = 1,
    .max_cpus = 255,
    .is_default = 1,
};

static void s390_machine_init(void)
{
    qemu_register_machine(&s390_machine);
}

machine_init(s390_machine_init);
