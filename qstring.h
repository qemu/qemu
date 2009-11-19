#ifndef QSTRING_H
#define QSTRING_H

#include <stdint.h>
#include "qobject.h"

typedef struct QString {
    QObject_HEAD;
    char *string;
    size_t length;
    size_t capacity;
} QString;

QString *qstring_new(void);
QString *qstring_from_str(const char *str);
const char *qstring_get_str(const QString *qstring);
void qstring_append_int(QString *qstring, int64_t value);
void qstring_append(QString *qstring, const char *str);
void qstring_append_chr(QString *qstring, int c);
QString *qobject_to_qstring(const QObject *obj);

#endif /* QSTRING_H */
