/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef HELPER_SVE_H
#define HELPER_SVE_H

#include "exec/helper-proto-common.h"
#include "exec/helper-gen-common.h"

#define HELPER_H "tcg/helper-sve-defs.h"
#include "exec/helper-proto.h.inc"
#include "exec/helper-gen.h.inc"
#undef HELPER_H

#endif /* HELPER_SVE_H */
