#ifndef VFIO_API_H
#define VFIO_API_H

bool vfio_eeh_as_ok(AddressSpace *as);
int vfio_eeh_as_op(AddressSpace *as, uint32_t op);

#endif
