#include "hw/hw.h"
#include "hw/boards.h"

void register_machines(void)
{
    qemu_register_machine(&mcf5208evb_machine);
    qemu_register_machine(&an5206_machine);
    qemu_register_machine(&dummy_m68k_machine);
}
