#include "qemu/osdep.h"
#include "qemu/yank.h"

bool yank_register_instance(const YankInstance *instance, Error **errp)
{
    return true;
}

void yank_unregister_instance(const YankInstance *instance)
{
}

void yank_register_function(const YankInstance *instance,
                            YankFn *func,
                            void *opaque)
{
}

void yank_unregister_function(const YankInstance *instance,
                              YankFn *func,
                              void *opaque)
{
}

void yank_generic_iochannel(void *opaque)
{
}


