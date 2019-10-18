/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

/*
 *
 * This file defines the architectural  type information
 *
 * 
 *
 */

#ifndef _ARCH_TYPES_H_
#define _ARCH_TYPES_H_

#include "global_types.h"

/* Data types */
typedef size4u_t Reg_t;
typedef size4s_t reg_t;
typedef size4u_t VA_t;
typedef size8u_t PA_t;

typedef union {
	size2u_t i;
#ifndef SLOWLARIS
	struct {
		size2u_t mant:10;
		size2u_t exp:5;
		size2u_t sign:1;
	} x;
#else
	struct {
		size2u_t sign:1;
		size2u_t exp:5;
		size2u_t mant:10;
	} x;
#endif
} hf_t;

#endif							/* #ifndef _ARCH_TYPES_H_ */
