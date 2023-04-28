#include "qemu/osdep.h"
#include "qemu/notify.h"
#include "net/colo-compare.h"
#include "migration/colo.h"
#include "migration/migration.h"
#include "qemu/error-report.h"
#include "qapi/qapi-commands-migration.h"

void colo_shutdown(void)
{
}

void *colo_process_incoming_thread(void *opaque)
{
    error_report("Impossible happend: trying to start COLO thread when COLO "
                 "module is not built in");
    abort();
}

void colo_checkpoint_delay_set(void)
{
}

void migrate_start_colo_process(MigrationState *s)
{
    error_report("Impossible happend: trying to start COLO when COLO "
                 "module is not built in");
    abort();
}

bool migration_in_colo_state(void)
{
    return false;
}

bool migration_incoming_in_colo_state(void)
{
    return false;
}
