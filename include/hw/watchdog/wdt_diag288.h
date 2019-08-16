#ifndef WDT_DIAG288_H
#define WDT_DIAG288_H

#include "hw/qdev-core.h"

#define TYPE_WDT_DIAG288 "diag288"
#define DIAG288(obj) \
    OBJECT_CHECK(DIAG288State, (obj), TYPE_WDT_DIAG288)
#define DIAG288_CLASS(klass) \
    OBJECT_CLASS_CHECK(DIAG288Class, (klass), TYPE_WDT_DIAG288)
#define DIAG288_GET_CLASS(obj) \
    OBJECT_GET_CLASS(DIAG288Class, (obj), TYPE_WDT_DIAG288)

#define WDT_DIAG288_INIT      0
#define WDT_DIAG288_CHANGE    1
#define WDT_DIAG288_CANCEL    2

typedef struct DIAG288State {
    /*< private >*/
    DeviceState parent_obj;
    QEMUTimer *timer;
    bool enabled;

    /*< public >*/
} DIAG288State;

typedef struct DIAG288Class {
    /*< private >*/
    DeviceClass parent_class;

    /*< public >*/
    int (*handle_timer)(DIAG288State *dev,
                        uint64_t func, uint64_t timeout);
} DIAG288Class;

#endif /* WDT_DIAG288_H */
