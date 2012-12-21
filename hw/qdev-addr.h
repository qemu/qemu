#ifndef HW_QDEV_ADDR_H
#define HW_QDEV_ADDR_H 1

#define DEFINE_PROP_TADDR(_n, _s, _f, _d)                               \
    DEFINE_PROP_DEFAULT(_n, _s, _f, _d, qdev_prop_taddr, hwaddr)

extern PropertyInfo qdev_prop_taddr;
void qdev_prop_set_taddr(DeviceState *dev, const char *name, hwaddr value);

#endif
