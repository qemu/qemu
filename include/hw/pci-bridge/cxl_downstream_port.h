/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CXL_DOWNSTREAM_PORT_H
#define CXL_DOWNSTREAM_PORT_H
#include "include/hw/cxl/cxl_port.h"

typedef struct CXLDownstreamPort CXLDownstreamPort;
CXLPhyPortPerst *cxl_dsp_get_perst(CXLDownstreamPort *dsp);

#endif
