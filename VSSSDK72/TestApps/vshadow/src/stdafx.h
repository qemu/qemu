// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once


// General includes
#include <windows.h>
#include <winbase.h>

// _ASSERTE declaration (used by ATL) and otehr macros
#include "macros.h"



#include <iostream>
#include <tchar.h>
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS  // some CString constructors will be explicit

// ATL includes
#pragma warning( disable: 4189 )    // disable local variable is initialized but not referenced
#include <atlbase.h>

// STL includes
#include <vector>
#include <algorithm>
#include <string>
#include <fstream>
using namespace std;   

// Used for safe string manipulation
#include <strsafe.h>

#include "shadow.h"

