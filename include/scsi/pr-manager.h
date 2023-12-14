#ifndef PR_MANAGER_H
#define PR_MANAGER_H

#include "qom/object.h"
#include "qapi/visitor.h"
#include "qom/object_interfaces.h"
#include "block/aio.h"

#define TYPE_PR_MANAGER "pr-manager"

OBJECT_DECLARE_TYPE(PRManager, PRManagerClass,
                    PR_MANAGER)

struct sg_io_hdr;

struct PRManager {
    /* <private> */
    Object parent;
};

/**
 * PRManagerClass:
 * @parent_class: the base class
 * @run: callback invoked in thread pool context
 */
struct PRManagerClass {
    /* <private> */
    ObjectClass parent_class;

    /* <public> */
    int (*run)(PRManager *pr_mgr, int fd, struct sg_io_hdr *hdr);
    bool (*is_connected)(PRManager *pr_mgr);
};

bool pr_manager_is_connected(PRManager *pr_mgr);
int coroutine_fn pr_manager_execute(PRManager *pr_mgr, AioContext *ctx, int fd,
                                    struct sg_io_hdr *hdr);

PRManager *pr_manager_lookup(const char *id, Error **errp);

#endif
