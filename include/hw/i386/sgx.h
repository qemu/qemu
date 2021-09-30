#ifndef QEMU_SGX_H
#define QEMU_SGX_H

#include "qom/object.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qapi/qapi-types-misc-target.h"

SGXInfo *sgx_get_info(Error **errp);
SGXInfo *sgx_get_capabilities(Error **errp);

#endif
