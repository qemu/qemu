#ifndef VFIO_API_H
#define VFIO_API_H

#include "qemu/typedefs.h"

extern int vfio_container_ioctl(AddressSpace *as, int32_t groupid,
                                int req, void *param);

#endif
