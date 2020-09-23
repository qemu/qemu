/*
 * hw/arm/linux-boot-if.h : interface for devices which need to behave
 * specially for direct boot of an ARM Linux kernel
 */

#ifndef HW_ARM_LINUX_BOOT_IF_H
#define HW_ARM_LINUX_BOOT_IF_H

#include "qom/object.h"

#define TYPE_ARM_LINUX_BOOT_IF "arm-linux-boot-if"
typedef struct ARMLinuxBootIfClass ARMLinuxBootIfClass;
DECLARE_CLASS_CHECKERS(ARMLinuxBootIfClass, ARM_LINUX_BOOT_IF,
                       TYPE_ARM_LINUX_BOOT_IF)
#define ARM_LINUX_BOOT_IF(obj) \
    INTERFACE_CHECK(ARMLinuxBootIf, (obj), TYPE_ARM_LINUX_BOOT_IF)

typedef struct ARMLinuxBootIf ARMLinuxBootIf;

struct ARMLinuxBootIfClass {
    /*< private >*/
    InterfaceClass parent_class;

    /*< public >*/
    /** arm_linux_init: configure the device for a direct boot
     * of an ARM Linux kernel (so that device reset puts it into
     * the state the kernel expects after firmware initialization,
     * rather than the true hardware reset state). This callback is
     * called once after machine construction is complete (before the
     * first system reset).
     *
     * @obj: the object implementing this interface
     * @secure_boot: true if we are booting Secure, false for NonSecure
     * (or for a CPU which doesn't support TrustZone)
     */
    void (*arm_linux_init)(ARMLinuxBootIf *obj, bool secure_boot);
};

#endif
