/*
**++
**
** Copyright (c) 2000-2001  Microsoft Corporation
**
**
** Module Name:
**
**	    main.h
**
**
** Abstract:
**
**	Test program to to register a Writer with various properties
**
** Author:
**
**	Reuven Lax      [reuvenl]       04-June-2002
**
**
**
** Revision History:
**
**--
*/

#ifndef _MAIN_H_
#define _MAIN_H_

extern "C" __cdecl wmain(int argc, wchar_t ** argv);
void loadFile(wchar_t* fileName);
BOOL WINAPI handler(DWORD dwCtrlType);

#endif

