/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */


#ifndef _GLOBAL_TYPES_H_
#define _GLOBAL_TYPES_H_ 1


typedef unsigned char size1u_t;
typedef char size1s_t;
typedef unsigned short int size2u_t;
typedef short size2s_t;
typedef unsigned int size4u_t;
typedef int size4s_t;
typedef unsigned long long int size8u_t;
typedef long long int size8s_t;
typedef size8u_t paddr_t;
typedef size4u_t vaddr_t;
typedef size8u_t pcycles_t;

typedef struct size16s {
    size8s_t hi;
    size8u_t lo;
} size16s_t;

#endif /* _GLOBAL_TYPES_H_ */
