#ifndef SYSEMU_IOMMUFD_H
#define SYSEMU_IOMMUFD_H

#include "qom/object.h"
#include "exec/hwaddr.h"
#include "exec/cpu-common.h"

#define TYPE_IOMMUFD_BACKEND "iommufd"
OBJECT_DECLARE_TYPE(IOMMUFDBackend, IOMMUFDBackendClass, IOMMUFD_BACKEND)

struct IOMMUFDBackendClass {
    ObjectClass parent_class;
};

struct IOMMUFDBackend {
    Object parent;

    /*< protected >*/
    int fd;            /* /dev/iommu file descriptor */
    bool owned;        /* is the /dev/iommu opened internally */
    uint32_t users;

    /*< public >*/
};

int iommufd_backend_connect(IOMMUFDBackend *be, Error **errp);
void iommufd_backend_disconnect(IOMMUFDBackend *be);

int iommufd_backend_alloc_ioas(IOMMUFDBackend *be, uint32_t *ioas_id,
                               Error **errp);
void iommufd_backend_free_id(IOMMUFDBackend *be, uint32_t id);
int iommufd_backend_map_dma(IOMMUFDBackend *be, uint32_t ioas_id, hwaddr iova,
                            ram_addr_t size, void *vaddr, bool readonly);
int iommufd_backend_unmap_dma(IOMMUFDBackend *be, uint32_t ioas_id,
                              hwaddr iova, ram_addr_t size);
#endif
