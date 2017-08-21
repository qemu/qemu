#ifndef PR_MANAGER_H
#define PR_MANAGER_H

#include "qom/object.h"
#include "qapi/qmp/qdict.h"
#include "qapi/visitor.h"
#include "qom/object_interfaces.h"
#include "block/aio.h"

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
} PRManagerClass;

BlockAIOCB *pr_manager_execute(PRManager *pr_mgr,
                               AioContext *ctx, int fd,
                               struct sg_io_hdr *hdr,
                               BlockCompletionFunc *complete,
                               void *opaque);

#ifdef CONFIG_LINUX
PRManager *pr_manager_lookup(const char *id, Error **errp);
#else
static inline PRManager *pr_manager_lookup(const char *id, Error **errp)
{
    /* The classes do not exist at all!  */
    error_setg(errp, "No persistent reservation manager with id '%s'", id);
    return NULL;
}
#endif

#endif
