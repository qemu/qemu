/*
 * Misc ARM declarations
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 *
 */

#ifndef HW_ARM_H
#define HW_ARM_H

#include "exec/memory.h"
#include "target/arm/cpu-qom.h"
#include "hw/irq.h"
#include "qemu/notify.h"

typedef enum {
    ARM_ENDIANNESS_UNKNOWN = 0,
    ARM_ENDIANNESS_LE,
    ARM_ENDIANNESS_BE8,
    ARM_ENDIANNESS_BE32,
} arm_endianness;

/* armv7m.c */
DeviceState *armv7m_init(MemoryRegion *system_memory, int mem_size, int num_irq,
                      const char *kernel_filename, const char *cpu_model);
/**
 * armv7m_load_kernel:
 * @cpu: CPU
 * @kernel_filename: file to load
 * @mem_size: mem_size: maximum image size to load
 *
 * Load the guest image for an ARMv7M system. This must be called by
 * any ARMv7M board, either directly or via armv7m_init(). (This is
 * necessary to ensure that the CPU resets correctly on system reset,
 * as well as for kernel loading.)
 */
void armv7m_load_kernel(ARMCPU *cpu, const char *kernel_filename, int mem_size);

/*
 * struct used as a parameter of the arm_load_kernel machine init
 * done notifier
 */
typedef struct {
    Notifier notifier; /* actual notifier */
    ARMCPU *cpu; /* handle to the first cpu object */
} ArmLoadKernelNotifier;

/* arm_boot.c */
struct arm_boot_info {
    uint64_t ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
    const char *dtb_filename;
    hwaddr loader_start;
    /* multicore boards that use the default secondary core boot functions
     * need to put the address of the secondary boot code, the boot reg,
     * and the GIC address in the next 3 values, respectively. boards that
     * have their own boot functions can use these values as they want.
     */
    hwaddr smp_loader_start;
    hwaddr smp_bootreg_addr;
    hwaddr gic_cpu_if_addr;
    int nb_cpus;
    int board_id;
    /* ARM machines that support the ARM Security Extensions use this field to
     * control whether Linux is booted as secure(true) or non-secure(false).
     */
    bool secure_boot;
    int (*atag_board)(const struct arm_boot_info *info, void *p);
    /* multicore boards that use the default secondary core boot functions
     * can ignore these two function calls. If the default functions won't
     * work, then write_secondary_boot() should write a suitable blob of
     * code mimicking the secondary CPU startup process used by the board's
     * boot loader/boot ROM code, and secondary_cpu_reset_hook() should
     * perform any necessary CPU reset handling and set the PC for the
     * secondary CPUs to point at this boot blob.
     */
    void (*write_secondary_boot)(ARMCPU *cpu,
                                 const struct arm_boot_info *info);
    void (*secondary_cpu_reset_hook)(ARMCPU *cpu,
                                     const struct arm_boot_info *info);
    /* if a board is able to create a dtb without a dtb file then it
     * sets get_dtb. This will only be used if no dtb file is provided
     * by the user. On success, sets *size to the length of the created
     * dtb, and returns a pointer to it. (The caller must free this memory
     * with g_free() when it has finished with it.) On failure, returns NULL.
     */
    void *(*get_dtb)(const struct arm_boot_info *info, int *size);
    /* if a board needs to be able to modify a device tree provided by
     * the user it should implement this hook.
     */
    void (*modify_dtb)(const struct arm_boot_info *info, void *fdt);
    /* machine init done notifier executing arm_load_dtb */
    ArmLoadKernelNotifier load_kernel_notifier;
    /* Used internally by arm_boot.c */
    int is_linux;
    hwaddr initrd_start;
    hwaddr initrd_size;
    hwaddr entry;

    /* Boot firmware has been loaded, typically at address 0, with -bios or
     * -pflash. It also implies that fw_cfg_find() will succeed.
     */
    bool firmware_loaded;

    /* Address at which board specific loader/setup code exists. If enabled,
     * this code-blob will run before anything else. It must return to the
     * caller via the link register. There is no stack set up. Enabled by
     * defining write_board_setup, which is responsible for loading the blob
     * to the specified address.
     */
    hwaddr board_setup_addr;
    void (*write_board_setup)(ARMCPU *cpu,
                              const struct arm_boot_info *info);

    /* If set, the board specific loader/setup blob will be run from secure
     * mode, regardless of secure_boot. The blob becomes responsible for
     * changing to non-secure state if implementing a non-secure boot
     */
    bool secure_board_setup;

    arm_endianness endianness;
};

/**
 * arm_load_kernel - Loads memory with everything needed to boot
 *
 * @cpu: handle to the first CPU object
 * @info: handle to the boot info struct
 * Registers a machine init done notifier that copies to memory
 * everything needed to boot, depending on machine and user options:
 * kernel image, boot loaders, initrd, dtb. Also registers the CPU
 * reset handler.
 *
 * In case the machine file supports the platform bus device and its
 * dynamically instantiable sysbus devices, this function must be called
 * before sysbus-fdt arm_register_platform_bus_fdt_creator. Indeed the
 * machine init done notifiers are called in registration reverse order.
 */
void arm_load_kernel(ARMCPU *cpu, struct arm_boot_info *info);

/* Write a secure board setup routine with a dummy handler for SMCs */
void arm_write_secure_board_setup_dummy_smc(ARMCPU *cpu,
                                            const struct arm_boot_info *info,
                                            hwaddr mvbar_addr);

/* Multiplication factor to convert from system clock ticks to qemu timer
   ticks.  */
extern int system_clock_scale;

#endif /* HW_ARM_H */
