/*++

Copyright (c) 2002  Microsoft Corporation

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

#ifndef __VSS_STDAFX_HXX__
#define __VSS_STDAFX_HXX__

#if _MSC_VER > 1000
#pragma once
#endif

#include <windows.h>

#include "vss.h"
#include <vswriter.h>
#include <vsbackup.h>

#include "sassert.h"
#include <stddef.h>
#include <atlbase.h>
#include <atlconv.h>
#include <new>
#include <Wincrypt.h>

#pragma warning(disable:4511)
#pragma warning(disable:4100)  // I don't like disabling this, but STL insists
#endif // __VSS_STDAFX_HXX__
