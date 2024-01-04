/*
**++
**
** Copyright (c) 2002  Microsoft Corporation
**
**
** Module Name:
**
**	assert.h
**
**
** Abstract:
**
**	Defines my assert function since I can't use the built-in one
**
** Author:
**
**	Reuven Lax      [reuvenl]       04-June-2002
**
**
** Revision History:
**
**--
*/

#ifndef _ASSERT_H_
#define _ASSERT_H_

#include <stdio.h>

#ifdef _DEBUG
#define _ASSERTE(x) { if (!(x)) FailAssertion(__FILE__, __LINE__, #x ); }
#define assert(x) _ASSERTE(x)
#define verify(x) _ASSERTE(x)
#else
#define _ASSERTE(x) 
#define assert(x)
#define verify(x) x
#endif

void FailAssertion(const char* fileName, unsigned int lineNumber, const char* condition);

#endif

