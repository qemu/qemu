#include "hw/hw.h"

/* Stub functions for binaries that never call qemu_devices_reset(),
 * and don't need to keep track of the reset handler list.
 */

void qemu_register_reset(QEMUResetHandler *func, void *opaque)
{
}

void qemu_unregister_reset(QEMUResetHandler *func, void *opaque)
{
}
