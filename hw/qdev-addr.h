#define DEFINE_PROP_TADDR(_n, _s, _f, _d)                               \
    DEFINE_PROP_DEFAULT(_n, _s, _f, _d, qdev_prop_taddr, a_target_phys_addr)

extern PropertyInfo qdev_prop_taddr;
void qdev_prop_set_taddr(DeviceState *dev, const char *name, a_target_phys_addr value);
