/*++

Copyright (c) 1999  Microsoft Corporation

Module Name:

    stdafx.hxx

Abstract:

    Include file for standard system include files.

Author:

    Adi Oltean   [aoltean]      07/02/1999

Revision History:


    Name    Date            Comments

    aoltean 07/02/1999      Created
    aoltean 09/11/1999      Disabling the C4290 warning

--*/

#define NOCRYPT

#ifndef __VSS_STDAFX_HXX__
#define __VSS_STDAFX_HXX__

#if _MSC_VER > 1000
#pragma once
#endif

// Disable warning: 'identifier' : identifier was truncated to 'number' characters in the debug information
//#pragma warning(disable:4786)

//
// C4290: C++ Exception Specification ignored
//
#pragma warning(disable:4290)


// #define _ATL_STATIC_REGISTRY
//
//  ATL debugging support turned on at debug version
//

#include <wtypes.h>
#pragma warning( disable: 4201 )    // C4201: nonstandard extension used : nameless struct/union
#include <devioctl.h>
#include <ntddstor.h>
#include <winioctl.h>
#pragma warning( default: 4201 )	// C4201: nonstandard extension used : nameless struct/union
#include <winbase.h>
#include <wchar.h>
#include <string.h>
#include <iostream.h>
#include <fstream.h>
#include <stdio.h>
#include <process.h>
#include <stdlib.h>
#include <errno.h>

#include <oleauto.h>
#include <stddef.h>

#define STRSAFE_NO_DEPRECATE
#include <strsafe.h>

#include "vs_assert.hxx"

#pragma warning( disable: 4127 )    // warning C4127: conditional expression is constant
#include <atlconv.h>
#include <atlbase.h>
extern CComModule _Module;
#include <atlcom.h>

#endif // __VSS_STDAFX_HXX__
