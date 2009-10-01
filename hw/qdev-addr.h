#define DEFINE_PROP_TADDR(_n, _s, _f, _d)                               \
    DEFINE_PROP_DEFAULT(_n, _s, _f, _d, qdev_prop_taddr, target_phys_addr_t)

extern PropertyInfo qdev_prop_taddr;
void qdev_prop_set_taddr(DeviceState *dev, const char *name, target_phys_addr_t value);
