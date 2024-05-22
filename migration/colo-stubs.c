#include "qemu/osdep.h"
#include "qemu/notify.h"
#include "net/colo-compare.h"
#include "migration/colo.h"
#include "qemu/error-report.h"
#include "qapi/qapi-commands-migration.h"

void colo_shutdown(void)
{
}

void coroutine_fn colo_incoming_co(void)
{
}

void colo_checkpoint_delay_set(void)
{
}

void migrate_start_colo_process(MigrationState *s)
{
    error_report("Impossible happened: trying to start COLO when COLO "
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
