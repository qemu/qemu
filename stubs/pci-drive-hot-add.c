#include <monitor/monitor.h>
#include <sysemu/sysemu.h>
#include <sysemu/blockdev.h>

int pci_drive_hot_add(Monitor *mon, const QDict *qdict, DriveInfo *dinfo)
{
    /* On non-x86 we don't do PCI hotplug */
    monitor_printf(mon, "Can't hot-add drive to type %d\n", dinfo->type);
    return -1;
}
