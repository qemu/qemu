/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef HELPER__H
#define HELPER__H

#include "exec/helper-proto-common.h"
#include "exec/helper-gen-common.h"

#define HELPER_H "tcg/helper-defs.h"
#include "exec/helper-proto.h.inc"
#include "exec/helper-gen.h.inc"
#undef HELPER_H

#endif /* HELPER__H */
