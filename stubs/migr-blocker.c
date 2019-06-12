#include "qemu/osdep.h"
#include "migration/blocker.h"

int migrate_add_blocker(Error *reason, Error **errp)
{
    return 0;
}

void migrate_del_blocker(Error *reason)
{
}
