#ifndef VFIO_API_H
#define VFIO_API_H

#include "qemu/typedefs.h"

extern int vfio_container_ioctl(AddressSpace *as, int32_t groupid,
                                int req, void *param);
bool vfio_eeh_as_ok(AddressSpace *as);
int vfio_eeh_as_op(AddressSpace *as, uint32_t op);

#endif
