#include <linux/ioctl.h>
#include <linux/types.h>

#define MINIMIG_IOCTL_BASE 'M'

#define MINIMIG_IOC_WAIT_IRQ     _IOR(MINIMIG_IOCTL_BASE, 0, int)
