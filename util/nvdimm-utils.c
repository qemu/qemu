#include "qemu/osdep.h"
#include "qemu/nvdimm-utils.h"
#include "hw/mem/nvdimm.h"

static int nvdimm_device_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_NVDIMM)) {
        *list = g_slist_append(*list, DEVICE(obj));
    }

    object_child_foreach(obj, nvdimm_device_list, opaque);
    return 0;
}

/*
 * inquire NVDIMM devices and link them into the list which is
 * returned to the caller.
 *
 * Note: it is the caller's responsibility to free the list to avoid
 * memory leak.
 */
GSList *nvdimm_get_device_list(void)
{
    GSList *list = NULL;

    object_child_foreach(qdev_get_machine(), nvdimm_device_list, &list);
    return list;
}
