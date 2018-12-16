#ifndef PR_MANAGER_H
#define PR_MANAGER_H

#include "qom/object.h"
#include "qapi/visitor.h"
#include "qom/object_interfaces.h"
#include "block/aio.h"
#include "qemu/coroutine.h"

#define TYPE_PR_MANAGER "pr-manager"

#define PR_MANAGER_CLASS(klass) \
     OBJECT_CLASS_CHECK(PRManagerClass, (klass), TYPE_PR_MANAGER)
#define PR_MANAGER_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PRManagerClass, (obj), TYPE_PR_MANAGER)
#define PR_MANAGER(obj) \
     OBJECT_CHECK(PRManager, (obj), TYPE_PR_MANAGER)

struct sg_io_hdr;

typedef struct PRManager {
    /* <private> */
    Object parent;
} PRManager;

/**
 * PRManagerClass:
 * @parent_class: the base class
 * @run: callback invoked in thread pool context
 */
typedef struct PRManagerClass {
    /* <private> */
    ObjectClass parent_class;

    /* <public> */
    int (*run)(PRManager *pr_mgr, int fd, struct sg_io_hdr *hdr);
    bool (*is_connected)(PRManager *pr_mgr);
} PRManagerClass;

bool pr_manager_is_connected(PRManager *pr_mgr);
int coroutine_fn pr_manager_execute(PRManager *pr_mgr, AioContext *ctx, int fd,
                                    struct sg_io_hdr *hdr);

PRManager *pr_manager_lookup(const char *id, Error **errp);

#endif
