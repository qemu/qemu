/////////////////////////////////////////////////////////////////////////
// Copyright © 2004 Microsoft Corporation. All rights reserved.
// 
//  This file may contain preliminary information or inaccuracies, 
//  and may not correctly represent any associated Microsoft 
//  Product as commercially released. All Materials are provided entirely 
//  “AS IS.” To the extent permitted by law, MICROSOFT MAKES NO 
//  WARRANTY OF ANY KIND, DISCLAIMS ALL EXPRESS, IMPLIED AND STATUTORY 
//  WARRANTIES, AND ASSUMES NO LIABILITY TO YOU FOR ANY DAMAGES OF 
//  ANY TYPE IN CONNECTION WITH THESE MATERIALS OR ANY INTELLECTUAL PROPERTY IN THEM. 
// 


#pragma once


// VSS includes
#include "vss.h"
#include "vswriter.h"
#include "vsbackup.h"

// VDS includes
#include "vds.h"

// Our includes
#include "tracing.h"
#include "util.h"
#include "writer.h"
#include "vssclient.h"




/////////////////////////////////////////////////////////////////////////
//  The main command line parser
//

class CommandLineParser
{
public:

    CommandLineParser();

    ~CommandLineParser();

    // Main routine 
    int MainRoutine(vector<wstring> arguments);

    

private:

    // Returns the final context based on whether the shapshot is persistent 
    // and whether the creation is with/without writers
    DWORD UpdateFinalContext(DWORD dwContext);


    // Implementation of various options
    void PrintUsage();

    //
    //  Parsing utilities
    //


    // Returns TRUE if the argument is in the following formats
    //  -xxxx
    //  /xxxx
    // where xxxx is the option pattern
    bool MatchArgument(wstring arg, wstring optionPattern);

    // Returns TRUE if the argument is in the following formats
    //  -xxxx=yyyy
    //  /xxxx=yyyy
    // where xxxx is the option pattern and yyyy the additional parameter (eventually enclosed in ' or ")
    bool MatchArgument(wstring argument, wstring optionPattern, wstring & additionalParameter);


    //
    //  Private data members
    //

    // The VSS client
    VssClient   m_vssClient;
    
    // Flags to indicate whether the snapshot is persistent 
    // and whether the creation is with/without writers
    bool        m_bPersistent;
    bool        m_bWithWriters;

    // interactivity option
    bool        m_bWaitForFinish;
};






