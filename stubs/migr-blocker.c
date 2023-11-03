#include "qemu/osdep.h"
#include "migration/blocker.h"

int migrate_add_blocker(Error **reasonp, Error **errp)
{
    return 0;
}

int migrate_add_blocker_normal(Error **reasonp, Error **errp)
{
    return 0;
}

int migrate_add_blocker_modes(Error **reasonp, Error **errp, MigMode mode, ...)
{
    return 0;
}

void migrate_del_blocker(Error **reasonp)
{
}
