/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2020 Huacai Chen <chenhc@lemote.com>
 * Copyright (c) 2020 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 */

#ifndef LOONGSON_LIOINTC_H
#define LOONGSON_LIOINTC_H

#include "qemu/units.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_LOONGSON_LIOINTC "loongson.liointc"
DECLARE_INSTANCE_CHECKER(struct loongson_liointc, LOONGSON_LIOINTC,
                         TYPE_LOONGSON_LIOINTC)

#endif /* LOONGSON_LIOINTC_H */
