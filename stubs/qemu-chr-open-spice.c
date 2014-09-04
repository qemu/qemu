#include "qemu-common.h"
#include "ui/qemu-spice.h"

CharDriverState *qemu_chr_open_spice_vmc(const char *type)
{
    return NULL;
}

#if SPICE_SERVER_VERSION >= 0x000c02
CharDriverState *qemu_chr_open_spice_port(const char *name)
{
    return NULL;
}
#endif
