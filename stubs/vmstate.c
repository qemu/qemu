#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "qapi/qapi-types-migration.h"
#include "migration/client-options.h"

int vmstate_register_with_alias_id(VMStateIf *obj,
                                   uint32_t instance_id,
                                   const VMStateDescription *vmsd,
                                   void *base, int alias_id,
                                   int required_for_version,
                                   Error **errp)
{
    return 0;
}

void vmstate_unregister(VMStateIf *obj,
                        const VMStateDescription *vmsd,
                        void *opaque)
{
}

bool vmstate_check_only_migratable(const VMStateDescription *vmsd)
{
    return true;
}

MigMode migrate_mode(void)
{
    return MIG_MODE_NORMAL;
}
