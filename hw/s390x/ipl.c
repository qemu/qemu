/*
 * bootloader support
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Christian Borntraeger <borntraeger@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "sysemu/sysemu.h"
#include "cpu.h"
#include "elf.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/s390x/virtio-ccw.h"
#include "hw/s390x/css.h"

#define KERN_IMAGE_START                0x010000UL
#define KERN_PARM_AREA                  0x010480UL
#define INITRD_START                    0x800000UL
#define INITRD_PARM_START               0x010408UL
#define INITRD_PARM_SIZE                0x010410UL
#define PARMFILE_START                  0x001000UL
#define ZIPL_IMAGE_START                0x009000UL
#define IPL_PSW_MASK                    (PSW_MASK_32 | PSW_MASK_64)

#define TYPE_S390_IPL "s390-ipl"
#define S390_IPL(obj) \
    OBJECT_CHECK(S390IPLState, (obj), TYPE_S390_IPL)
#if 0
#define S390_IPL_CLASS(klass) \
    OBJECT_CLASS_CHECK(S390IPLState, (klass), TYPE_S390_IPL)
#define S390_IPL_GET_CLASS(obj) \
    OBJECT_GET_CLASS(S390IPLState, (obj), TYPE_S390_IPL)
#endif

typedef struct S390IPLClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/

    void (*parent_reset) (SysBusDevice *dev);
} S390IPLClass;

typedef struct S390IPLState {
    /*< private >*/
    SysBusDevice parent_obj;
    uint64_t start_addr;

    /*< public >*/
    char *kernel;
    char *initrd;
    char *cmdline;
    char *firmware;
} S390IPLState;


static int s390_ipl_init(SysBusDevice *dev)
{
    S390IPLState *ipl = S390_IPL(dev);
    ram_addr_t kernel_size = 0;

    if (!ipl->kernel) {
        ram_addr_t bios_size = 0;
        char *bios_filename;

        /* Load zipl bootloader */
        if (bios_name == NULL) {
            bios_name = ipl->firmware;
        }

        bios_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
        if (bios_filename == NULL) {
            hw_error("could not find stage1 bootloader\n");
        }

        bios_size = load_elf(bios_filename, NULL, NULL, &ipl->start_addr, NULL,
                             NULL, 1, ELF_MACHINE, 0);
        if (bios_size == -1UL) {
            bios_size = load_image_targphys(bios_filename, ZIPL_IMAGE_START,
                                            4096);
            ipl->start_addr = ZIPL_IMAGE_START;
            if (bios_size > 4096) {
                hw_error("stage1 bootloader is > 4k\n");
            }
        }
        g_free(bios_filename);

        if ((long)bios_size < 0) {
            hw_error("could not load bootloader '%s'\n", bios_name);
        }
        return 0;
    } else {
        kernel_size = load_elf(ipl->kernel, NULL, NULL, NULL, NULL,
                               NULL, 1, ELF_MACHINE, 0);
        if (kernel_size == -1UL) {
            kernel_size = load_image_targphys(ipl->kernel, 0, ram_size);
        }
        if (kernel_size == -1UL) {
            fprintf(stderr, "could not load kernel '%s'\n", ipl->kernel);
            return -1;
        }
        /* we have to overwrite values in the kernel image, which are "rom" */
        strcpy(rom_ptr(KERN_PARM_AREA), ipl->cmdline);

        /*
         * we can not rely on the ELF entry point, since up to 3.2 this
         * value was 0x800 (the SALIPL loader) and it wont work. For
         * all (Linux) cases 0x10000 (KERN_IMAGE_START) should be fine.
         */
        ipl->start_addr = KERN_IMAGE_START;
    }
    if (ipl->initrd) {
        ram_addr_t initrd_offset, initrd_size;

        initrd_offset = INITRD_START;
        while (kernel_size + 0x100000 > initrd_offset) {
            initrd_offset += 0x100000;
        }
        initrd_size = load_image_targphys(ipl->initrd, initrd_offset,
                                          ram_size - initrd_offset);
        if (initrd_size == -1UL) {
            fprintf(stderr, "qemu: could not load initrd '%s'\n", ipl->initrd);
            exit(1);
        }

        /* we have to overwrite values in the kernel image, which are "rom" */
        stq_p(rom_ptr(INITRD_PARM_START), initrd_offset);
        stq_p(rom_ptr(INITRD_PARM_SIZE), initrd_size);
    }

    return 0;
}

static Property s390_ipl_properties[] = {
    DEFINE_PROP_STRING("kernel", S390IPLState, kernel),
    DEFINE_PROP_STRING("initrd", S390IPLState, initrd),
    DEFINE_PROP_STRING("cmdline", S390IPLState, cmdline),
    DEFINE_PROP_STRING("firmware", S390IPLState, firmware),
    DEFINE_PROP_END_OF_LIST(),
};

static void s390_ipl_reset(DeviceState *dev)
{
    S390IPLState *ipl = S390_IPL(dev);
    S390CPU *cpu = S390_CPU(qemu_get_cpu(0));
    CPUS390XState *env = &cpu->env;

    env->psw.addr = ipl->start_addr;
    env->psw.mask = IPL_PSW_MASK;

    if (!ipl->kernel) {
        /* booting firmware, tell what device to boot from */
        DeviceState *dev_st = get_boot_device(0);
        VirtioCcwDevice *ccw_dev = (VirtioCcwDevice *) object_dynamic_cast(
                OBJECT(&(dev_st->parent_obj)), "virtio-blk-ccw");

        if (ccw_dev) {
            env->regs[7] = ccw_dev->sch->cssid << 24 |
                           ccw_dev->sch->ssid << 16 |
                           ccw_dev->sch->devno;
        } else {
            env->regs[7] = -1;
        }
    }

    s390_add_running_cpu(cpu);
}

static void s390_ipl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = s390_ipl_init;
    dc->props = s390_ipl_properties;
    dc->reset = s390_ipl_reset;
    dc->no_user = 1;
}

static const TypeInfo s390_ipl_info = {
    .class_init = s390_ipl_class_init,
    .parent = TYPE_SYS_BUS_DEVICE,
    .name  = "s390-ipl",
    .instance_size  = sizeof(S390IPLState),
};

static void s390_ipl_register_types(void)
{
    type_register_static(&s390_ipl_info);
}

type_init(s390_ipl_register_types)
