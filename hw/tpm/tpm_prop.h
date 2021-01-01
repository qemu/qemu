/*
 * TPM utility functions
 *
 *  Copyright (c) 2010 - 2015 IBM Corporation
 *  Authors:
 *    Stefan Berger <stefanb@us.ibm.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef HW_TPM_PROP_H
#define HW_TPM_PROP_H

#include "sysemu/tpm_backend.h"
#include "hw/qdev-properties.h"

extern const PropertyInfo qdev_prop_tpm;

#define DEFINE_PROP_TPMBE(_n, _s, _f)                     \
    DEFINE_PROP(_n, _s, _f, qdev_prop_tpm, TPMBackend *)

#endif /* HW_TPM_PROP_H */
