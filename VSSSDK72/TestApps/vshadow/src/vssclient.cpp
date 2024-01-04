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


// Main header
#include "stdafx.h"



// Constructor
VssClient::VssClient()
{
    m_bCoInitializeCalled = false;
    m_dwContext = VSS_CTX_BACKUP;
    m_latestSnapshotSetID = GUID_NULL;
    m_bDuringRestore = false;
}


// Destructor
VssClient::~VssClient()
{
    // Release the IVssBackupComponents interface 
    // WARNING: this must be done BEFORE calling CoUninitialize()
    m_pVssObject = NULL;
    
    // Call CoUninitialize if the CoInitialize was performed sucesfully
    if (m_bCoInitializeCalled)
        CoUninitialize();
}


// Initialize the COM infrastructure and the internal pointers
void VssClient::Initialize(DWORD dwContext, wstring xmlDoc, bool bDuringRestore)
{
    FunctionTracer ft(DBG_INFO);

    // Initialize COM 
    CHECK_COM( CoInitialize(NULL) );
    m_bCoInitializeCalled = true;

    // Initialize COM security
    CHECK_COM( 
        CoInitializeSecurity(
            NULL,                           //  Allow *all* VSS writers to communicate back!
            -1,                             //  Default COM authentication service
            NULL,                           //  Default COM authorization service
            NULL,                           //  reserved parameter
            RPC_C_AUTHN_LEVEL_PKT_PRIVACY,  //  Strongest COM authentication level
            RPC_C_IMP_LEVEL_IDENTIFY,       //  Minimal impersonation abilities 
            NULL,                           //  Default COM authentication settings
            EOAC_NONE,                      //  No special options
            NULL                            //  Reserved parameter
            ) );

    // Create the internal backup components object
    CHECK_COM( CreateVssBackupComponents(&m_pVssObject) );
    
    // We are during restore now?
    m_bDuringRestore = bDuringRestore;

    // Call either Initialize for backup or for restore
    if (m_bDuringRestore)
    {
        CHECK_COM(m_pVssObject->InitializeForRestore(CComBSTR(xmlDoc.c_str())))
    }
    else
    {
        // Initialize for backup
        if (xmlDoc.length() == 0)
            CHECK_COM(m_pVssObject->InitializeForBackup())
        else
            CHECK_COM(m_pVssObject->InitializeForBackup(CComBSTR(xmlDoc.c_str())))

#ifdef VSS_SERVER

        // Set the context, if different than the default context
        if (dwContext != VSS_CTX_BACKUP)
        {
            ft.WriteLine(L"- Setting the VSS context to: 0x%08lx", dwContext);
            CHECK_COM(m_pVssObject->SetContext(dwContext) );
        }

#endif

    }

    // Keep the context
    m_dwContext = dwContext;

    // Set various properties per backup components instance
    CHECK_COM(m_pVssObject->SetBackupState(true, true, VSS_BT_FULL, false));
}



// Waits for the completion of the asynchronous operation
void VssClient::WaitAndCheckForAsyncOperation(IVssAsync* pAsync)
{
    FunctionTracer ft(DBG_INFO);

    ft.WriteLine(L"(Waiting for the asynchronous operation to finish...)");

    // Wait until the async operation finishes
    CHECK_COM(pAsync->Wait());

    // Check the result of the asynchronous operation
    HRESULT hrReturned = S_OK;
    CHECK_COM(pAsync->QueryStatus(&hrReturned, NULL));

    // Check if the async operation succeeded...
    if(FAILED(hrReturned))
    {
        ft.WriteLine(L"Error during the last asynchronous operation.");
        ft.WriteLine(L"- Returned HRESULT = 0x%08lx", hrReturned);
        ft.WriteLine(L"- Error text: %s", FunctionTracer::HResult2String(hrReturned).c_str());
        ft.WriteLine(L"- Please re-run VSHADOW.EXE with the /tracing option to get more details");
        throw(hrReturned);
    }
}





