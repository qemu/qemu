#ifndef QSTRING_H
#define QSTRING_H

#include "qobject.h"

typedef struct QString {
    QObject_HEAD;
    char *string;
} QString;

QString *qstring_from_str(const char *str);
const char *qstring_get_str(const QString *qstring);
QString *qobject_to_qstring(const QObject *obj);

#endif /* QSTRING_H */
