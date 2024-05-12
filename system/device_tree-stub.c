#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine.h"

#ifdef CONFIG_FDT
void qmp_dumpdtb(const char *filename, Error **errp)
{
    error_setg(errp, "This machine doesn't have a FDT");
}
#endif
