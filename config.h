
#include "config-host.h"
#include "config-target.h"

/* We want to include different config files for specific targets
   And for the common library.  They need a different name because
   we don't want to rely in paths */

#if defined(NEED_CPU_H)
#include "config-devices.h"
#else
#include "config-all-devices.h"
#endif
