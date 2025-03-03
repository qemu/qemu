#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine.h"

#ifdef CONFIG_FDT
void qmp_dumpdtb(const char *filename, Error **errp)
{
    ERRP_GUARD();

    error_setg(errp, "This machine doesn't have an FDT");
    error_append_hint(errp, "(this machine type definitely doesn't use FDT)\n");
}
#endif
