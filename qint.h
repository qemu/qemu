#ifndef QINT_H
#define QINT_H

#include <stdint.h>
#include "qobject.h"

typedef struct QInt {
    QObject_HEAD;
    int64_t value;
} QInt;

QInt *qint_from_int(int64_t value);
int64_t qint_get_int(const QInt *qi);
QInt *qobject_to_qint(const QObject *obj);

#endif /* QINT_H */
