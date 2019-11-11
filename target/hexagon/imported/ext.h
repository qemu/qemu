/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#ifndef EXT_H
#define EXT_H 1

enum {
#define DEF_EXT(NAME,START,NUM) EXT_IDX_##NAME = START, EXT_IDX_##NAME##_AFTER = (START+NUM),
#include "ext.def"
#undef DEF_EXT
	XX_LAST_EXT_IDX
};

enum {
#define DEF_EXT(NAME,START,NUM) EXTOPSTAB_IDX_##NAME,
#include "ext.def"
#undef DEF_EXT
	XX_LAST_EXTOPSTAB_IDX
};

#endif
