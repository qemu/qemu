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

#include "stdafx.h"
#include "sassert.h"

// really stupid assertion function...
void FailAssertion(const char* fileName, unsigned int lineNumber, const char* condition)
{
	fprintf(stderr, "Assertion failure: %s\nFile: %s\nLine: %u\n", condition, fileName, lineNumber);
	::DebugBreak();
}

