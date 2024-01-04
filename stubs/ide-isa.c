#include <qemu/osdep.h>
#include <hw/ide.h>
#include <stdlib.h>

ISADevice *isa_ide_init(ISABus *bus, int iobase, int iobase2, int isairq,
                        DriveInfo *hd0, DriveInfo *hd1)
{
    /*
     * In theory the real isa_ide_init() function can return NULL, but no
     * caller actually checks for that. Make sure we go out with a clear bang.
     */
    abort();
}
