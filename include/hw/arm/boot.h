/*
 * ARM kernel loader.
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 *
 */

#ifndef HW_ARM_BOOT_H
#define HW_ARM_BOOT_H

#include "target/arm/cpu-qom.h"
#include "qemu/notify.h"

typedef enum {
    ARM_ENDIANNESS_UNKNOWN = 0,
    ARM_ENDIANNESS_LE,
    ARM_ENDIANNESS_BE8,
    ARM_ENDIANNESS_BE32,
} arm_endianness;

/**
 * armv7m_load_kernel:
 * @cpu: CPU
 * @kernel_filename: file to load
 * @mem_base: base address to load image at (should be where the
 *            CPU expects to find its vector table on reset)
 * @mem_size: mem_size: maximum image size to load
 *
 * Load the guest image for an ARMv7M system. This must be called by
 * any ARMv7M board. (This is necessary to ensure that the CPU resets
 * correctly on system reset, as well as for kernel loading.)
 */
void armv7m_load_kernel(ARMCPU *cpu, const char *kernel_filename,
                        hwaddr mem_base, int mem_size);

/* arm_boot.c */
struct arm_boot_info {
    uint64_t ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
    const char *dtb_filename;
    hwaddr loader_start;
    hwaddr dtb_start;
    hwaddr dtb_limit;
    /* If set to True, arm_load_kernel() will not load DTB.
     * It allows board to load DTB manually later.
     * (default: False)
     */
    bool skip_dtb_autoload;
    /* multicore boards that use the default secondary core boot functions
     * need to put the address of the secondary boot code, the boot reg,
     * and the GIC address in the next 3 values, respectively. boards that
     * have their own boot functions can use these values as they want.
     */
    hwaddr smp_loader_start;
    hwaddr smp_bootreg_addr;
    hwaddr gic_cpu_if_addr;
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
     *
     * These hooks won't be called if secondary CPUs are booting via
     * emulated PSCI (see psci_conduit below).
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
    /*
     * If a board wants to use the QEMU emulated-firmware PSCI support,
     * it should set this to QEMU_PSCI_CONDUIT_HVC or QEMU_PSCI_CONDUIT_SMC
     * as appropriate. arm_load_kernel() will set the psci-conduit and
     * start-powered-off properties on the CPUs accordingly.
     * Note that if the guest image is started at the same exception level
     * as the conduit specifies calls should go to (eg guest firmware booted
     * to EL3) then PSCI will not be enabled.
     */
    int psci_conduit;
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

    /*
     * If set, the board specific loader/setup blob will be run from secure
     * mode, regardless of secure_boot. The blob becomes responsible for
     * changing to non-secure state if implementing a non-secure boot,
     * including setting up EL3/Secure registers such as the NSACR as
     * required by the Linux booting ABI before the switch to non-secure.
     */
    bool secure_board_setup;

    arm_endianness endianness;

    /* CPU having load the kernel and that should be the first to boot.  */
    ARMCPU *primary_cpu;
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
void arm_load_kernel(ARMCPU *cpu, MachineState *ms, struct arm_boot_info *info);

AddressSpace *arm_boot_address_space(ARMCPU *cpu,
                                     const struct arm_boot_info *info);

/**
 * arm_load_dtb() - load a device tree binary image into memory
 * @addr:       the address to load the image at
 * @binfo:      struct describing the boot environment
 * @addr_limit: upper limit of the available memory area at @addr
 * @as:         address space to load image to
 * @cpu:        ARM CPU object
 *
 * Load a device tree supplied by the machine or by the user  with the
 * '-dtb' command line option, and put it at offset @addr in target
 * memory.
 *
 * If @addr_limit contains a meaningful value (i.e., it is strictly greater
 * than @addr), the device tree is only loaded if its size does not exceed
 * the limit.
 *
 * Returns: the size of the device tree image on success,
 *          0 if the image size exceeds the limit,
 *          -1 on errors.
 *
 * Note: Must not be called unless have_dtb(binfo) is true.
 */
int arm_load_dtb(hwaddr addr, const struct arm_boot_info *binfo,
                 hwaddr addr_limit, AddressSpace *as, MachineState *ms,
                 ARMCPU *cpu);

/* Write a secure board setup routine with a dummy handler for SMCs */
void arm_write_secure_board_setup_dummy_smc(ARMCPU *cpu,
                                            const struct arm_boot_info *info,
                                            hwaddr mvbar_addr);

typedef enum {
    FIXUP_NONE = 0,     /* do nothing */
    FIXUP_TERMINATOR,   /* end of insns */
    FIXUP_BOARDID,      /* overwrite with board ID number */
    FIXUP_BOARD_SETUP,  /* overwrite with board specific setup code address */
    FIXUP_ARGPTR_LO,    /* overwrite with pointer to kernel args */
    FIXUP_ARGPTR_HI,    /* overwrite with pointer to kernel args (high half) */
    FIXUP_ENTRYPOINT_LO, /* overwrite with kernel entry point */
    FIXUP_ENTRYPOINT_HI, /* overwrite with kernel entry point (high half) */
    FIXUP_GIC_CPU_IF,   /* overwrite with GIC CPU interface address */
    FIXUP_BOOTREG,      /* overwrite with boot register address */
    FIXUP_DSB,          /* overwrite with correct DSB insn for cpu */
    FIXUP_MAX,
} FixupType;

typedef struct ARMInsnFixup {
    uint32_t insn;
    FixupType fixup;
} ARMInsnFixup;

/**
 * arm_write_bootloader - write a bootloader to guest memory
 * @name: name of the bootloader blob
 * @as: AddressSpace to write the bootloader
 * @addr: guest address to write it
 * @insns: the blob to be loaded
 * @fixupcontext: context to be used for any fixups in @insns
 *
 * Write a bootloader to guest memory at address @addr in the address
 * space @as. @name is the name to use for the resulting ROM blob, so
 * it should be unique in the system and reasonably identifiable for debugging.
 *
 * @insns must be an array of ARMInsnFixup structs, each of which has
 * one 32-bit value to be written to the guest memory, and a fixup to be
 * applied to the value. FIXUP_NONE (do nothing) is value 0, so effectively
 * the fixup is optional when writing a struct initializer.
 * The final entry in the array must be { 0, FIXUP_TERMINATOR }.
 *
 * All other supported fixup types have the semantics "ignore insn
 * and instead use the value from the array element @fixupcontext[fixup]".
 * The caller should therefore provide @fixupcontext as an array of
 * size FIXUP_MAX whose elements have been initialized for at least
 * the entries that @insns refers to.
 */
void arm_write_bootloader(const char *name,
                          AddressSpace *as, hwaddr addr,
                          const ARMInsnFixup *insns,
                          const uint32_t *fixupcontext);

#endif /* HW_ARM_BOOT_H */
