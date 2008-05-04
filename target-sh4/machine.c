#include "hw/hw.h"
#include "hw/boards.h"

void register_machines(void)
{
    qemu_register_machine(&shix_machine);
    qemu_register_machine(&r2d_machine);
}
