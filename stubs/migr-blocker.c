#include "qemu/osdep.h"
#include "migration/blocker.h"
#include "qapi/error.h"

int migrate_add_blocker(Error **reasonp, Error **errp)
{
    return 0;
}

int migrate_add_blocker_normal(Error **reasonp, Error **errp)
{
    return 0;
}

int migrate_add_blocker_modes(Error **reasonp, unsigned modes, Error **errp)
{
    return 0;
}

void migrate_del_blocker(Error **reasonp)
{
    /*
     * migrate_add_blocker*() is a no-op here, so nobody owns the Error;
     * free it to avoid a leak (e.g. "qemu-img info foo.vmdk").
     */
    if (reasonp && *reasonp) {
        error_free(*reasonp);
        *reasonp = NULL;
    }
}
