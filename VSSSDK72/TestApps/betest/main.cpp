
#include "stdafx.hxx"
//#include "vs_idl.hxx"
#include "vss.h"
#include "vswriter.h"
#include "vsbackup.h"
#include "vs_trace.hxx"
#include "vscoordint.h"
//#include "vs_debug.hxx"
#include "compont.h"
#include <debug.h>
#include <cwriter.h>
#include <lmshare.h>
#include <lmaccess.h>
#include <time.h>


// Globals
BOOL g_bDebug = TRUE;
BOOL g_bComponentBackup = TRUE;
BOOL g_bBackupOnly = FALSE;
BOOL g_bRestoreOnly = FALSE;
BOOL g_bExcludeTestWriter = TRUE;
BOOL g_bAsrRestoreMode = FALSE;

WCHAR g_wszBackupDocumentFileName[MAX_PATH];
WCHAR g_wszPreviousBackupDocumentFileName[MAX_PATH];
WCHAR g_wszComponentsFileName[MAX_PATH];
WCHAR g_wszSavedFilesDirectory[MAX_PATH];
LONG g_lWriterWait = 0;
bool g_bRestoreTest = false;
bool g_lRestoreTestOptions = 0;
VSS_BACKUP_TYPE g_BackupType = VSS_BT_FULL;
bool g_bBootableSystemState = false;
bool g_bTestNewInterfaces = false;
bool g_bAddFullUNCPath = true;
bool g_bPersistantSnapshot  = false;
bool g_bRollbackRecovery = false;
bool g_bAuthRestore = false;
bool g_bStopOnFirstFailure = false;
bool g_bNoDelete = false;
bool g_bAdditionalRestores = false;
bool g_bVerbose = false;
WCHAR g_wszJustSnapshot[MAX_PATH];

VSS_ID g_idRevertToSnapshot = GUID_NULL;

CComPtr<CWritersSelection>  g_pWriterSelection;

void TestSnapshotXML();
void EnumVolumes();

// forward declarations
void CheckStatus(IVssBackupComponents *pvbc, LPCWSTR wszWhen, 
            CSimpleMap<VSS_ID, HRESULT>* failedWriters = NULL);
HRESULT ParseCommandLine (int argc, WCHAR **argv);
BOOL SaveBackupDocument(CComBSTR &bstr);
void LoadMetadataFile
    (
    VSS_ID idInstance,
    IVssExamineWriterMetadata **ppMetadataSaved
    );

void DoCopyFile(LPCWSTR, LPCWSTR);
void RestoreFiles(IVssBackupComponents *pvbc, const CSimpleMap<VSS_ID, HRESULT>& failedWriters);
void SetSubcomponentsSelectedForRestore
    (
    IVssBackupComponents *pvbc,
    VSS_ID idInstance,
    IVssComponent *pComponent
    );


void SaveFiles(IVssBackupComponents *pvbc, VSS_ID *rgSnapshotId, UINT cSnapshots);

bool AddDependencies
       (
       IVssWMComponent* pComponent,
       PVSSCOMPONENTINFO pInfo,
       IVssBackupComponents *pvbc,
       IVssBackupComponents* pPreviousBackup,
       DWORD dwWriterSchema,
       LPWSTR wszVolumes
       );

bool FindComponent
    (
    IVssExamineWriterMetadata *pMetadata,
    LPCWSTR wszLogicalPath,
    LPCWSTR wszComponentName,
    IVssWMComponent **ppComponent
    );

BOOL IsUNCPrefixLen(
    IN      LPCWSTR      wszUNCPath,
    OUT     DWORD       &dwPrefixLen
    );

bool FindComponentInDoc
    (
    IVssBackupComponents *pvbc,
    VSS_ID idWriter,
    LPCWSTR wszLogicalPath,
    LPCWSTR wszComponentName,
    IVssComponent **ppComponent,
    VSS_ID *pidInstance = NULL
    );

bool needsBackingUp(DWORD dwMask);
bool needsSnapshot(DWORD dwMask);
VSS_BACKUP_TYPE ToBackupType(WCHAR* type)
{
    if (wcscmp(type, L"COPY") == 0)
        return VSS_BT_COPY;
    else if (wcscmp(type, L"LOG") == 0)
        return VSS_BT_LOG;
    else if (wcscmp(type, L"INCREMENTAL") == 0)
        return VSS_BT_INCREMENTAL;
    else if (wcscmp(type, L"DIFFERENTIAL") == 0)
        return VSS_BT_DIFFERENTIAL;
    
    return VSS_BT_FULL;
}

bool MustAddComponent(IVssExamineWriterMetadata* pMetadata, LPCWSTR wszLogicalPath, LPCWSTR wszComponentName)
{
    HRESULT hr = S_OK;
    UINT cIncludes, cExcludes, cComponents;
    CHECK_SUCCESS(pMetadata->GetFileCounts(&cIncludes, &cExcludes, &cComponents));

    CComBSTR bstrQualifiedTarget = (wszLogicalPath) ? wszLogicalPath : L"";
    if (bstrQualifiedTarget.Length() > 0)
        bstrQualifiedTarget += L"\\";
    bstrQualifiedTarget += wszComponentName;
    bstrQualifiedTarget += L"\\";
    
    for (UINT x = 0; x < cComponents; x++)
        {
        CComPtr<IVssWMComponent> pComponent;
        CHECK_SUCCESS(pMetadata->GetComponent(x, &pComponent));
        PVSSCOMPONENTINFO pInfo;
        CHECK_SUCCESS(pComponent->GetComponentInfo(&pInfo));

        CComBSTR bstrParent = (pInfo->bstrLogicalPath) ? pInfo->bstrLogicalPath : L"";
        if (bstrParent.Length() > 0)
            bstrParent += L"\\";
        bstrParent += pInfo->bstrComponentName;
        bstrParent += L"\\";
            
        if (wcsstr(bstrQualifiedTarget, bstrParent) == bstrQualifiedTarget &&
             pInfo->bSelectable)
            {
            pComponent->FreeComponentInfo(pInfo);
             return false;
            }
        
        pComponent->FreeComponentInfo(pInfo);
        }

    return true;
}
    
BOOL AssertPrivilege( LPCWSTR privName )
    {
    HANDLE  tokenHandle;
    BOOL    stat = FALSE;

    if ( OpenProcessToken (GetCurrentProcess(),
               TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,
               &tokenHandle))
    {
    LUID value;

    if ( LookupPrivilegeValue( NULL, privName, &value ) )
        {
        TOKEN_PRIVILEGES newState;
        DWORD            error;

        newState.PrivilegeCount           = 1;
        newState.Privileges[0].Luid       = value;
        newState.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED_BY_DEFAULT|SE_PRIVILEGE_ENABLED;

        /*
         * We will always call GetLastError below, so clear
         * any prior error values on this thread.
         */
        SetLastError( ERROR_SUCCESS );

        stat = AdjustTokenPrivileges (tokenHandle,
                      FALSE,
                      &newState,
                      (DWORD)0,
                      NULL,
                      NULL );

        /*
         * Supposedly, AdjustTokenPriveleges always returns TRUE
         * (even when it fails). So, call GetLastError to be
         * extra sure everything's cool.
         */
        if ( (error = GetLastError()) != ERROR_SUCCESS )
        {
        stat = FALSE;
        }

        if ( !stat )
        {
        wprintf( L"AdjustTokenPrivileges for %s failed with %d",
             privName,
             error );
        }
        }

    DWORD cbTokens;
    GetTokenInformation (tokenHandle,
                 TokenPrivileges,
                 NULL,
                 0,
                 &cbTokens);

    TOKEN_PRIVILEGES *pTokens = (TOKEN_PRIVILEGES *) new BYTE[cbTokens];
    GetTokenInformation (tokenHandle,
                 TokenPrivileges,
                 pTokens,
                 cbTokens,
                 &cbTokens);

    delete pTokens;
    CloseHandle( tokenHandle );
    }


    return stat;
    }


LPCWSTR GetStringFromUsageType (VSS_USAGE_TYPE eUsageType)
    {
    LPCWSTR pwszRetString = L"UNDEFINED";

    switch (eUsageType)
    {
    case VSS_UT_BOOTABLESYSTEMSTATE: pwszRetString = L"BootableSystemState"; break;
    case VSS_UT_SYSTEMSERVICE:       pwszRetString = L"SystemService";       break;
    case VSS_UT_USERDATA:            pwszRetString = L"UserData";            break;
    case VSS_UT_OTHER:               pwszRetString = L"Other";               break;

    default:
        break;
    }


    return (pwszRetString);
    }


LPCWSTR GetStringFromSourceType (VSS_SOURCE_TYPE eSourceType)
    {
    LPCWSTR pwszRetString = L"UNDEFINED";

    switch (eSourceType)
    {
    case VSS_ST_TRANSACTEDDB:    pwszRetString = L"TransactionDb";    break;
    case VSS_ST_NONTRANSACTEDDB: pwszRetString = L"NonTransactionDb"; break;
    case VSS_ST_OTHER:           pwszRetString = L"Other";            break;

    default:
        break;
    }


    return (pwszRetString);
    }


LPCWSTR GetStringFromRestoreMethod (VSS_RESTOREMETHOD_ENUM eRestoreMethod)
    {
    LPCWSTR pwszRetString = L"UNDEFINED";

    switch (eRestoreMethod)
    {
    case VSS_RME_RESTORE_IF_NOT_THERE:          pwszRetString = L"RestoreIfNotThere";          break;
    case VSS_RME_RESTORE_IF_CAN_REPLACE:        pwszRetString = L"RestoreIfCanReplace";        break;
    case VSS_RME_STOP_RESTORE_START:            pwszRetString = L"StopRestoreStart";           break;
    case VSS_RME_RESTORE_TO_ALTERNATE_LOCATION: pwszRetString = L"RestoreToAlternateLocation"; break;
    case VSS_RME_RESTORE_AT_REBOOT:             pwszRetString = L"RestoreAtReboot";            break;
    case VSS_RME_RESTORE_AT_REBOOT_IF_CANNOT_REPLACE: pwszRetString = L"RestoreAtRebootIfCannotReplace"; break;
    case VSS_RME_CUSTOM:                        pwszRetString = L"Custom";                     break;

    default:
        break;
    }


    return (pwszRetString);
    }


LPCWSTR GetStringFromWriterRestoreMethod (VSS_WRITERRESTORE_ENUM eWriterRestoreMethod)
    {
    LPCWSTR pwszRetString = L"UNDEFINED";

    switch (eWriterRestoreMethod)
    {
    case VSS_WRE_NEVER:            pwszRetString = L"RestoreNever";           break;
    case VSS_WRE_IF_REPLACE_FAILS: pwszRetString = L"RestoreIfReplaceFails";  break;
    case VSS_WRE_ALWAYS:           pwszRetString = L"RestoreAlways";          break;

    default:
        break;
    }


    return (pwszRetString);
    }


LPCWSTR GetStringFromComponentType (VSS_COMPONENT_TYPE eComponentType)
    {
    LPCWSTR pwszRetString = L"UNDEFINED";

    switch (eComponentType)
    {
    case VSS_CT_DATABASE:  pwszRetString = L"Database";  break;
    case VSS_CT_FILEGROUP: pwszRetString = L"FileGroup"; break;

    default:
        break;
    }


    return (pwszRetString);
    }


bool needsBackingUp(DWORD dwMask)
{
    switch(g_BackupType)    {
        case VSS_BT_FULL:
            return (dwMask & VSS_FSBT_FULL_BACKUP_REQUIRED) != 0;
        case VSS_BT_DIFFERENTIAL:
            return (dwMask & VSS_FSBT_DIFFERENTIAL_BACKUP_REQUIRED) != 0;
        case VSS_BT_INCREMENTAL:
            return (dwMask & VSS_FSBT_INCREMENTAL_BACKUP_REQUIRED) != 0;
        case VSS_BT_LOG:
            return (dwMask & VSS_FSBT_LOG_BACKUP_REQUIRED) != 0;
        default:
            return true;
    }
}

bool needsSnapshot(DWORD dwMask)
{
    switch(g_BackupType)    {
        case VSS_BT_FULL:
            return (dwMask & VSS_FSBT_FULL_SNAPSHOT_REQUIRED) != 0;
        case VSS_BT_DIFFERENTIAL:
            return (dwMask & VSS_FSBT_DIFFERENTIAL_SNAPSHOT_REQUIRED) != 0;
        case VSS_BT_INCREMENTAL:
            return (dwMask & VSS_FSBT_INCREMENTAL_SNAPSHOT_REQUIRED) != 0;
        case VSS_BT_LOG:
            return (dwMask & VSS_FSBT_LOG_SNAPSHOT_REQUIRED) != 0;
        default:
            return true;
    }
}


void PrintFiledesc(IVssWMFiledesc *pFiledesc, LPCWSTR wszDescription)
    {
    CComBSTR bstrPath;
    CComBSTR bstrFilespec;
    CComBSTR bstrAlternate;
    CComBSTR bstrDestination;
    bool bRecursive;
    DWORD dwTypeMask;
    HRESULT hr;

    CHECK_SUCCESS(pFiledesc->GetPath(&bstrPath));
    CHECK_SUCCESS(pFiledesc->GetFilespec(&bstrFilespec));
    CHECK_NOFAIL(pFiledesc->GetRecursive(&bRecursive));
    CHECK_NOFAIL(pFiledesc->GetAlternateLocation(&bstrAlternate));
    CHECK_NOFAIL(pFiledesc->GetBackupTypeMask(&dwTypeMask));
	
    wprintf (L"%s\n            Path = %s, Filespec = %s, Recursive = %s, BackupTypeMask = 0x%x\n",
         wszDescription,
         bstrPath,
         bstrFilespec,
         bRecursive ? L"yes" : L"no",
         dwTypeMask);

    if (bstrAlternate && wcslen(bstrAlternate) > 0)
    wprintf(L"            Alternate Location = %s\n", bstrAlternate);
    }



// wait a maximum number of seconds before cancelling the operation
void LoopWait
    (
    IVssAsync *pAsync,
    LONG seconds,
    LPCWSTR wszOperation,
    bool trackPercentDone = false
    )
    {
    // if debugging, allow one hour before cancelling operation
    if (g_bDebug)
        seconds = 3600;

    if (trackPercentDone)
        wprintf(L"waiting for operation %s to complete: ", wszOperation);
    
    clock_t start = clock();
    HRESULT hr, hrStatus;
    INT percentDone = 0;
    while(TRUE)
        {
        Sleep(500);
        INT oldPercentDone = percentDone;
        CHECK_SUCCESS(pAsync->QueryStatus(&hrStatus, &percentDone));
        if (hrStatus != VSS_S_ASYNC_PENDING)
            break;

        for (int x = 0; trackPercentDone && x < percentDone - oldPercentDone; x++)
            {
            wprintf(L"*");
            }
        
        if (((clock() - start)/CLOCKS_PER_SEC) >= seconds)
            break;
        }

    wprintf(L"\n");
    if (hrStatus == VSS_S_ASYNC_PENDING)
        {
        CHECK_NOFAIL(pAsync->Cancel());
        wprintf(L"Called cancelled for %s.\n", wszOperation);
        }

    CHECK_SUCCESS(pAsync->QueryStatus(&hrStatus, NULL));
    CHECK_NOFAIL(hrStatus);
    }


void  UpdatePartialFileRanges(IVssComponent* pComponent, IVssBackupComponents* pvbc,
	                                   VSS_ID id, VSS_COMPONENT_TYPE ct, BSTR bstrLogicalPath, BSTR bstrName)
    {
    UINT cPartialFiles;
    HRESULT hr;

    CHECK_SUCCESS(pComponent->GetPartialFileCount(&cPartialFiles));

    for(UINT iFile = 0; iFile < cPartialFiles; iFile++)
        {
        CComBSTR bstrPath;
        CComBSTR bstrFilename;
        CComBSTR bstrRanges;
        CComBSTR bstrMetadata;

        CHECK_SUCCESS(pComponent->GetPartialFile
					(
					iFile,
					&bstrPath,
					&bstrFilename,
					&bstrRanges,
					&bstrMetadata
					));

        // always call this function to see what it does if there is no ranges file
        CHECK_SUCCESS(pvbc->SetRangesFilePath(id, ct, bstrLogicalPath, bstrName, iFile,bstrRanges ));
        }
    }

void DoPrepareBackup(IVssBackupComponents *pvbc)
    {
    CComPtr<IVssAsync> pAsync;
    INT nPercentDone;
    HRESULT hrResult;
    HRESULT hr;


    CHECK_SUCCESS(pvbc->PrepareForBackup(&pAsync));
    LoopWait(pAsync, 5, L"PrepareForBackup");
    CHECK_SUCCESS(pAsync->QueryStatus(&hrResult, &nPercentDone));
    CHECK_NOFAIL(hrResult);
    }


void DoSnapshotSet(IVssBackupComponents *pvbc, HRESULT &hrResult)
    {
    CComPtr<IVssAsync> pAsync;
    INT nPercentDone;
    HRESULT hr;

    CHECK_SUCCESS(pvbc->DoSnapshotSet (&pAsync));

    CHECK_SUCCESS(pAsync->Wait());
    CHECK_SUCCESS(pAsync->QueryStatus(&hrResult, &nPercentDone));
    }




void DoBackupComplete(IVssBackupComponents *pvbc)
    {
    CComPtr<IVssAsync> pAsync;
    HRESULT hr;

    CHECK_SUCCESS(pvbc->BackupComplete(&pAsync));
    LoopWait(pAsync, 5, L"BackupComplete");
    }

void AddNewTargets(VSS_ID idWriter, VSS_COMPONENT_TYPE ct, IVssBackupComponents* pvbc, WCHAR* wszLogicalPath, WCHAR* wszComponentName)
{
    HRESULT hr = S_OK;
    
    CComPtr<IVssComponent> pComponent;
    FindComponentInDoc(pvbc, idWriter, wszLogicalPath, wszComponentName, &pComponent, NULL);

    NewTarget* pTarget = g_pWriterSelection->GetNewTargets(idWriter, wszLogicalPath, wszComponentName);
    while (pTarget != NULL)
        {
        CHECK_SUCCESS(pvbc->AddNewTarget
                                        (
                                        idWriter,
                                        ct,
                                        wszLogicalPath,
                                        wszComponentName,
                                        pTarget->m_bstrSourcePath,
                                        pTarget->m_bstrSourceFilespec,
                                        pTarget->m_bRecursive,
                                        pTarget->m_bstrTarget
                                        ));
        
        pTarget = pTarget->m_pNext;
        }
}


void DoRestore(IVssBackupComponents *pvbc)
    {
    CComPtr<IVssAsync> pAsync;
    HRESULT hr;

    if (g_bTestNewInterfaces)
        pvbc->SetRestoreState(VSS_RTYPE_OTHER);
    
    pvbc->GatherWriterMetadata(&pAsync);
    LoopWait(pAsync, 60, L"GetherWriterMetadata");
    pAsync = NULL;
    UINT cWriters, iWriter;

    CVssSimpleMap<VSS_ID, DWORD> schemas;
    CVssSimpleMap<VSS_ID, CComPtr<IVssExamineWriterMetadata> > currentMetadatas;    
    CHECK_SUCCESS(pvbc->GetWriterMetadataCount(&cWriters));
    for(iWriter = 0; iWriter < cWriters; iWriter++)
        {
        CComPtr<IVssExamineWriterMetadata> pMetadata;
        VSS_ID idInstance, idWriter;
        CComBSTR bstrName;
        VSS_USAGE_TYPE usage;
        VSS_SOURCE_TYPE source;
        
        CHECK_SUCCESS(pvbc->GetWriterMetadata(iWriter, &idInstance, &pMetadata));

        CHECK_SUCCESS(pMetadata->GetIdentity(&idInstance, &idWriter, &bstrName, &usage, &source));
        
        DWORD schema = 0; 
        CHECK_SUCCESS(pMetadata->GetBackupSchema(&schema));
        schemas.Add(idWriter, schema);    

        currentMetadatas.Add(idInstance, pMetadata);            
        }

    UINT cWriterComponents;
    CHECK_SUCCESS(pvbc->GetWriterComponentsCount(&cWriterComponents));
    for(UINT iWriterComponent = 0; iWriterComponent < cWriterComponents; iWriterComponent++)
        {
        CComPtr<IVssWriterComponentsExt> pWriter;

        CHECK_SUCCESS(pvbc->GetWriterComponents(iWriterComponent, &pWriter));
        VSS_ID idInstance;
        VSS_ID idWriter;
        UINT cComponents;
        CHECK_SUCCESS(pWriter->GetComponentCount(&cComponents));
        CHECK_SUCCESS(pWriter->GetWriterInfo(&idInstance, &idWriter));

        CComPtr<IVssExamineWriterMetadata> pStoredMetadata;
        if (g_wszSavedFilesDirectory[0] != L'\0')
            LoadMetadataFile(idInstance, &pStoredMetadata);

        
        for(UINT iComponent = 0; iComponent < cComponents; iComponent++)
            {
            CComPtr<IVssComponent> pComponent;

            CHECK_SUCCESS(pWriter->GetComponent(iComponent, &pComponent));

            CComBSTR bstrLogicalPath;
            CComBSTR bstrComponentName;
            CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
            CHECK_SUCCESS(pComponent->GetComponentName(&bstrComponentName));

            // For RestoreOnly case, we check if the user provided a component selection
            BOOL bSelected = TRUE;
            if (g_bRestoreOnly && g_pWriterSelection)
                {
                // User provided a valid selection file
                bSelected = g_pWriterSelection->IsComponentSelected(idWriter, bstrLogicalPath, bstrComponentName);
                if (bSelected)
                    {
                    wprintf (L"\n        Component \"%s\" is selected for Restore\n", bstrComponentName);
                    }
                else
                    {
                    wprintf (L"\n        Component \"%s\" is NOT selected for Restore\n", bstrComponentName);
                    }
                }

            // TODO: this requires some more thought
            if (!bSelected)
                bSelected = MustAddComponent(pStoredMetadata, bstrLogicalPath, bstrComponentName);
            
            // get the matching component from the writer metadata document
            CComPtr<IVssWMComponent> pWriterComponent;
            PVSSCOMPONENTINFO pInfo = NULL;
            bool bSelectable = true, bSelectableForRestore = false;
            if (g_wszSavedFilesDirectory[0] != L'\0')
                {
                BS_VERIFY(FindComponent(pStoredMetadata, bstrLogicalPath, bstrComponentName, &pWriterComponent));
                CHECK_SUCCESS(pWriterComponent->GetComponentInfo(&pInfo));

//                BS_ASSERT(!bSelected || (pInfo->bSelectable || pInfo->bSelectableForRestore));

                bSelectable = pInfo->bSelectable;
                bSelectableForRestore = pInfo->bSelectableForRestore;
                }


            // get the component type
            VSS_COMPONENT_TYPE ct;
            CHECK_SUCCESS(pComponent->GetComponentType(&ct));
            
            if (bSelected)
                {
                IVssExamineWriterMetadata* pMeta = (g_wszSavedFilesDirectory[0] != L'\0') ? 
                                                                                pStoredMetadata :
                                                                                currentMetadatas.Lookup(idInstance);
                BS_ASSERT(pMeta);
                
                VSS_WRITERRESTORE_ENUM writerRestore;    
                VSS_RESTOREMETHOD_ENUM method;
                CComBSTR foo1, foo2;
                bool reboot;
                UINT mappings;
                CHECK_SUCCESS(pMeta->GetRestoreMethod(&method, &foo1, &foo2, &writerRestore, 
                                                                                                     &reboot, &mappings));

                if (writerRestore != VSS_WRE_NEVER)
                    {
                    hr = pvbc->SetSelectedForRestore
                                            (
                                            idWriter,
                                            ct,
                                            bstrLogicalPath,
                                            bstrComponentName,
                                            true
                                            );
                    if (SUCCEEDED(hr) && g_bAdditionalRestores)
                      {
                      hr = pvbc->SetAdditionalRestores
                                            (
                                            idWriter,
                                            ct,
                                            bstrLogicalPath,
                                            bstrComponentName,
                                            true
                                            );
                      }
                    }
                if (hr == VSS_E_OBJECT_NOT_FOUND || writerRestore == VSS_WRE_NEVER)
                    {
                // BUGBUG: huge hack to fix the AD case.   We eventually need to 
                // BUGBUG: do something better here, but this is easiest for now.
                CHECK_SUCCESS(pvbc->SetRestoreOptions(idWriter,
                                                                                    ct,
                                                                                    bstrLogicalPath,
                                                                                    bstrComponentName,
                                                                                    L"RESTORE"
                                                                                    ));
                }
                else if (g_pWriterSelection)
                    {
                    AddNewTargets(idWriter, ct, pvbc, bstrLogicalPath, bstrComponentName);
                    }
                CHECK_SUCCESS(hr);
                    
//                SetSubcomponentsSelectedForRestore(pvbc, idInstance, pComponent);                
                }


        if (g_wszSavedFilesDirectory[0] != L'\0')
            {
            pWriterComponent->FreeComponentInfo(pInfo);
            pInfo = NULL;
            }
        }

    UINT nSubcomponents = 0;
    const WCHAR* const * ppwszSubcomponents = NULL;
    if (g_bRestoreOnly && g_pWriterSelection)
        {
            nSubcomponents = g_pWriterSelection->GetSubcomponentsCount(idWriter);
            ppwszSubcomponents = g_pWriterSelection->GetSubcomponents(idWriter);
        };
    
    for (UINT iSubcomponent = 0; g_wszSavedFilesDirectory[0] != L'\0' && 
                            iSubcomponent < nSubcomponents; iSubcomponent++)
        {
                // pull apart the logical path and component name
                CComBSTR bstrLogicalPath, bstrComponentName;        
                WCHAR* lastSlash = wcsrchr(ppwszSubcomponents[iSubcomponent], L'\\');
                if (lastSlash != NULL)
                    {
                    *lastSlash = L'\0';
                    bstrLogicalPath = ppwszSubcomponents[iSubcomponent];
                    bstrComponentName = lastSlash + 1;
                    *lastSlash = L'\\';
                    }
                else
                    {
                    bstrComponentName = ppwszSubcomponents[iSubcomponent];
                    }
                
                // look for the closest parent component that has been backed up
               CComBSTR bstrLogicalPathParent;
               CComBSTR bstrComponentNameParent;
               CComPtr<IVssComponent> pCurrentParent;
               unsigned int maxLength = 0;
                for(UINT iParentComponent = 0; iParentComponent  < cComponents; iParentComponent++)
                    {
                    CComPtr<IVssComponent> pParentComponent ;

                    CComBSTR bstrCurrentLPath;
                    CComBSTR bstrCurrentCName;
                    
                    CHECK_SUCCESS(pWriter->GetComponent(iParentComponent, &pParentComponent));

                    CHECK_NOFAIL(pParentComponent->GetLogicalPath(&bstrCurrentLPath));
                    CHECK_SUCCESS(pParentComponent->GetComponentName(&bstrCurrentCName));

                    CComBSTR bstrFullPath = bstrCurrentLPath;
                    if (bstrFullPath.Length() > 0)
                        bstrFullPath += L"\\";
                    bstrFullPath += bstrCurrentCName;
                    if (!bstrFullPath)
                        Error(E_OUTOFMEMORY, L"Ran out of memory");

                    // check to see if we've found a parent component that's larger
                    unsigned int currentLength = bstrFullPath.Length();
                    if (bstrLogicalPath && wcsstr(bstrLogicalPath, bstrFullPath) == bstrLogicalPath &&
                        currentLength > maxLength)
                        {
                        bstrLogicalPathParent = bstrCurrentLPath;
                        bstrComponentNameParent = bstrCurrentCName;
                        maxLength = currentLength;
                        pCurrentParent = pParentComponent;
                        }
                    }

                // if maxLength is zero, we're trying to restore a subcomponent for a component
                // that wasn't backed up.
                BS_ASSERT(maxLength > 0);

                wprintf (L"\n        SubComponent \"%s\" is selected for Restore\n", ppwszSubcomponents[iSubcomponent]);
                
                VSS_COMPONENT_TYPE ct;
                CHECK_SUCCESS(pCurrentParent->GetComponentType(&ct));
                
                // the parent component must be selected for restore
                hr = pvbc->SetSelectedForRestore
                                    (
                                     idWriter,
                                     ct,
                                     bstrLogicalPathParent,
                                     bstrComponentNameParent,
                                     true
                                     );
                if (hr != VSS_E_OBJECT_NOT_FOUND)
                    CHECK_SUCCESS(hr);

                
                // BUGBUG: Should check bSelectableForRestore first
                CHECK_SUCCESS(pvbc->AddRestoreSubcomponent
                                        (
                                        idWriter,
                                        ct,
                                        bstrLogicalPathParent,
                                        bstrComponentNameParent,
                                        bstrLogicalPath,
                                        bstrComponentName,
                                        false
                                        ));            
        }
    }
    CSimpleMap<VSS_ID, HRESULT> failedWriters;        

    CHECK_SUCCESS(pvbc->PreRestore(&pAsync));
    LoopWait(pAsync, 600, L"PreRestore");
    pAsync = NULL;
    
    CheckStatus(pvbc, L"After PreRestore", &failedWriters);

    for(UINT iWriterComponent = 0; iWriterComponent < cWriterComponents; iWriterComponent++)
        {
        CComPtr<IVssWriterComponentsExt> pWriter;
        VSS_ID idWriter;
        VSS_ID idInstance;

        CHECK_SUCCESS(pvbc->GetWriterComponents(iWriterComponent, &pWriter));
        UINT cComponents;
        CHECK_SUCCESS(pWriter->GetComponentCount(&cComponents));
        CHECK_SUCCESS(pWriter->GetWriterInfo(&idInstance, &idWriter));

        for(UINT iComponent = 0; iComponent < cComponents; iComponent++)
            {
            CComPtr<IVssComponent> pComponent;
            CComBSTR bstrLogicalPath;
            CComBSTR bstrComponentName;
            CComBSTR bstrFailureMsg;

            CHECK_SUCCESS(pWriter->GetComponent(iComponent, &pComponent));
            VSS_COMPONENT_TYPE ct;
            CHECK_SUCCESS(pComponent->GetComponentType(&ct));
            CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
            CHECK_SUCCESS(pComponent->GetComponentName(&bstrComponentName));
            CHECK_NOFAIL(pComponent->GetPreRestoreFailureMsg(&bstrFailureMsg));

            VSS_RESTORE_TARGET rt;
            CHECK_SUCCESS(pComponent->GetRestoreTarget(&rt));

            if (bstrFailureMsg || rt != VSS_RT_ORIGINAL)
                {
                wprintf(L"\nComponent Path=%s Name=%s\n",
                        bstrLogicalPath ? bstrLogicalPath : L"",
                        bstrComponentName);

                if (bstrFailureMsg)
                    wprintf(L"\nPreRestoreFailureMsg=%s\n", bstrFailureMsg);

                wprintf(L"restore target = %s\n", WszFromRestoreTarget(rt));
                if (rt == VSS_RT_DIRECTED)
                    PrintDirectedTargets(pComponent);

                wprintf(L"\n");
                }

            // we start off by saying that no files were restored.  we will reset this attribute later
            CHECK_SUCCESS(pvbc->SetFileRestoreStatus(idWriter, ct, bstrLogicalPath, bstrComponentName, VSS_RS_NONE));
            
            if (g_bTestNewInterfaces)
                {
                UpdatePartialFileRanges(pComponent, pvbc, idWriter, ct, bstrLogicalPath, bstrComponentName);
                PrintPartialFiles(pComponent);
                PrintDifferencedFiles(pComponent);
                }
            }

        wprintf(L"\n");
        }

    if (g_wszSavedFilesDirectory[0] != L'\0')
        RestoreFiles(pvbc, failedWriters);

    CHECK_SUCCESS(pvbc->PostRestore(&pAsync));
    LoopWait(pAsync, 600, L"PostRestore");
    pAsync = NULL;

    CheckStatus(pvbc, L"After PostRestore");

    for(UINT iWriterComponent = 0; iWriterComponent < cWriterComponents; iWriterComponent++)
        {
        CComPtr<IVssWriterComponentsExt> pWriter;

        CHECK_SUCCESS(pvbc->GetWriterComponents(iWriterComponent, &pWriter));
        UINT cComponents;
        CHECK_SUCCESS(pWriter->GetComponentCount(&cComponents));

        for(UINT iComponent = 0; iComponent < cComponents; iComponent++)
            {
            CComPtr<IVssComponent> pComponent;
            CComBSTR bstrLogicalPath;
            CComBSTR bstrComponentName;
            CComBSTR bstrFailureMsg;

            CHECK_SUCCESS(pWriter->GetComponent(iComponent, &pComponent));
            VSS_COMPONENT_TYPE ct;
            CHECK_SUCCESS(pComponent->GetComponentType(&ct));
            CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
            CHECK_SUCCESS(pComponent->GetComponentName(&bstrComponentName));
            CHECK_NOFAIL(pComponent->GetPostRestoreFailureMsg(&bstrFailureMsg));
            if (bstrFailureMsg)
                {
                wprintf(L"\nComponent Path=%s Name=%s\n",
                        bstrLogicalPath ? bstrLogicalPath : L"",
                        bstrComponentName);

                if (bstrFailureMsg)
                    wprintf(L"\nPostRestoreFailureMsg=%s\n", bstrFailureMsg);

                wprintf(L"\n");
                }
            }
        }

    wprintf(L"\n");
    }


// Perform the ASR restore
void DoAsrRestore(IVssBackupComponents *pvbc)
    {
    CComPtr<IVssAsync> pAsync;
    HRESULT hr;

    CHECK_SUCCESS(pvbc->PreRestore(&pAsync));
    LoopWait(pAsync, 600, L"PreRestore");
    pAsync = NULL;
    
    CHECK_SUCCESS(pvbc->PostRestore(&pAsync));
    LoopWait(pAsync, 600, L"PostRestore");
    pAsync = NULL;

    wprintf(L"\n");    
    }



void DoAddToVolumeList
    (
    IN BSTR bstrPath,
    IN LPWSTR wszVolumes    
    )
    {
    PWCHAR  pwszPath           = NULL;
    PWCHAR  pwszMountPointName = NULL;
    WCHAR   wszLocalVolumeName [50];
    WCHAR   *pwszVolumeName = NULL;
    ULONG   ulPathLength;
    ULONG   ulMountpointBufferLength;


    ulPathLength = ExpandEnvironmentStringsW (bstrPath, NULL, 0);

    pwszPath = (PWCHAR) malloc (ulPathLength * sizeof (WCHAR));

    ulPathLength = ExpandEnvironmentStringsW (bstrPath, pwszPath, ulPathLength);


    ulMountpointBufferLength = GetFullPathName (pwszPath, 0, NULL, NULL);

    pwszMountPointName = (PWCHAR) malloc (ulMountpointBufferLength * sizeof (WCHAR));
    bool fSuccess = false;
    if (GetVolumePathNameW (pwszPath, pwszMountPointName, ulMountpointBufferLength))
        fSuccess = true;
    else
        {
        BS_ASSERT(FALSE);
        printf("GetVolumePathNameW failed with error %d\nfor path %s.\n", GetLastError(), pwszPath);
        }

    if (fSuccess)
        {
        pwszVolumeName = wszLocalVolumeName;
        if (!GetVolumeNameForVolumeMountPointW (pwszMountPointName, wszLocalVolumeName, sizeof (wszLocalVolumeName) / sizeof (WCHAR)))
            {
            wprintf(L"\nGetVolumeNameForVolumeMountPointW failed with err %d for <%s>\n", GetLastError(), pwszMountPointName);

            // Check whether it looks like a UNC path
            DWORD dwLen;
            if (IsUNCPrefixLen(pwszMountPointName, dwLen))
                {
                // UNC Path - check whether we need to add the whole path or just the share-volume root
                wprintf(L"Path is a share, assume remote snapshot is required\n");
                
                if (g_bAddFullUNCPath)
                    pwszVolumeName = (LPWSTR)bstrPath;
                else
                    // BUGBUG: This doesn't work for the DFS case - need to resolve DFS to a share first
                    // - this code will add the DFS root instead of the share-volume root
                    pwszVolumeName = pwszMountPointName;
                } 
            else 
                {
                fSuccess = false;
                }
            }
        
//            wprintf(L"EXTRADBG: Volume <%s> <%s> is required for snapshot\n", wszVolumeName, pwszMountPointName);
        if (fSuccess)
            {
            if (NULL == wcsstr (wszVolumes, pwszVolumeName))
                {
                if (L'\0' != wszVolumes [0])
                    wcscat (wszVolumes, L";");

                wcscat (wszVolumes, pwszVolumeName);
                }
            }
        }

    if (NULL != pwszPath)           free (pwszPath);
    if (NULL != pwszMountPointName) free (pwszMountPointName);
    }

void UpdateSnapshotSet
    (
    IN IVssBackupComponents *pvbc,
    IN LPWSTR wszVolumes,
    VSS_ID *rgpSnapshotId,
    UINT *pcSnapshot
    )
    {
    HRESULT hr = S_OK;
    
    LPWSTR pCurrent = wszVolumes;
    while (wcslen(pCurrent) > 0)
        {
        LPWSTR pDelim = wcschr(pCurrent, L';');        
        if (pDelim)
            *pDelim = L'\0';

        CHECK_SUCCESS
            (
                pvbc->AddToSnapshotSet
                    (
                    pCurrent,
                    GUID_NULL,
                    &rgpSnapshotId[*pcSnapshot]
                    )
                    );

        *pcSnapshot += 1;
        
        wprintf(L"Volume <%s>\n", pCurrent);
        wprintf(L"is added to the snapshot set\n\n");

        
        if (pDelim)
            *pDelim = ';';

        pCurrent = (pDelim) ? pDelim + 1 : pCurrent + wcslen(pCurrent);
        }                   
    }

static LPCWSTR s_rgwszStates[] =
    {
    NULL,
    L"STABLE",
    L"WAIT_FOR_FREEZE",
    L"WAIT_FOR_THAW",
    L"WAIT_FOR_POST_SNAPSHOT",
    L"WAIT_FOR_BACKUP_COMPLETE",
    L"FAILED_AT_IDENTIFY",
    L"FAILED_AT_PREPARE_BACKUP",
    L"FAILED_AT_PREPARE_SNAPSHOT",
    L"FAILED_AT_FREEZE",
    L"FAILED_AT_THAW",
    L"FAILED_AT_POST_SNAPSHOT",
    L"FAILED_AT_BACKUP_COMPLETE",
    L"FAILED_AT_PRE_RESTORE",
    L"FAILED_AT_POST_RESTORE"
    };


void CheckStatus(IVssBackupComponents *pvbc, LPCWSTR wszWhen, 
            CSimpleMap<VSS_ID, HRESULT>* failedWriters)
    {
    unsigned cWriters;
    CComPtr<IVssAsync> pAsync;
    HRESULT hr;

    CHECK_NOFAIL(pvbc->GatherWriterStatus(&pAsync));
    CHECK_NOFAIL(pAsync->Wait());
    CHECK_NOFAIL(pvbc->GetWriterStatusCount(&cWriters));


    wprintf(L"\n\nstatus %s (%d writers)\n\n", wszWhen, cWriters);

    for(unsigned iWriter = 0; iWriter < cWriters; iWriter++)
    {
    VSS_ID idInstance;
    VSS_ID idWriter;
    VSS_WRITER_STATE status;
    CComBSTR bstrWriter;
    HRESULT hrWriterFailure;

    CHECK_SUCCESS(pvbc->GetWriterStatus (iWriter,
                         &idInstance,
                         &idWriter,
                         &bstrWriter,
                         &status,
                         &hrWriterFailure));

    wprintf (L"Status for writer %s: %s(0x%08lx%s%s)\n",
         bstrWriter,
         s_rgwszStates[status],
         hrWriterFailure,
         SUCCEEDED (hrWriterFailure) ? L"" : L" - ",
         GetStringFromFailureType (hrWriterFailure));

    if (FAILED(hrWriterFailure))   
        {
        if (g_bStopOnFirstFailure)
            CHECK_SUCCESS(hrWriterFailure);
        if (failedWriters)
            failedWriters->Add(idInstance, hrWriterFailure);
        }            
    }

    
    pvbc->FreeWriterStatus();
    }

void PrintDifferencedFilesForComponents(IVssBackupComponents* pvbc)
    {
    HRESULT hr;
    UINT cWriterComponents;

    CHECK_SUCCESS(pvbc->GetWriterComponentsCount(&cWriterComponents));
    for(UINT iWriterComponent = 0; iWriterComponent < cWriterComponents; iWriterComponent++)
        {
        CComPtr<IVssWriterComponentsExt> pWriter;

        CHECK_SUCCESS(pvbc->GetWriterComponents(iWriterComponent, &pWriter));
        UINT cComponents;
        CHECK_SUCCESS(pWriter->GetComponentCount(&cComponents));

        for(UINT iComponent = 0; iComponent < cComponents; iComponent++)
            {
            CComPtr<IVssComponent> pComponent;

            CHECK_SUCCESS(pWriter->GetComponent(iComponent, &pComponent));

            CComBSTR bstrLogicalPath;
            CComBSTR bstrComponentName;

            CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
            CHECK_SUCCESS(pComponent->GetComponentName(&bstrComponentName));
            UINT cDifferencedFiles;

            CHECK_SUCCESS(pComponent->GetDifferencedFilesCount(&cDifferencedFiles));
            if (cDifferencedFiles > 0)
                {
                wprintf(L"\nDifferenced  files for Component Path=%s Name=%s\n",
                        bstrLogicalPath ? bstrLogicalPath : L"",
                        bstrComponentName);

                PrintDifferencedFiles(pComponent);
                }
        	}
    	}
    }

void PrintPartialFilesForComponents(IVssBackupComponents *pvbc)
    {
    HRESULT hr;
    UINT cWriterComponents;

    CHECK_SUCCESS(pvbc->GetWriterComponentsCount(&cWriterComponents));
    for(UINT iWriterComponent = 0; iWriterComponent < cWriterComponents; iWriterComponent++)
        {
        CComPtr<IVssWriterComponentsExt> pWriter;

        CHECK_SUCCESS(pvbc->GetWriterComponents(iWriterComponent, &pWriter));
        UINT cComponents;
        CHECK_SUCCESS(pWriter->GetComponentCount(&cComponents));

        for(UINT iComponent = 0; iComponent < cComponents; iComponent++)
            {
            CComPtr<IVssComponent> pComponent;

            CHECK_SUCCESS(pWriter->GetComponent(iComponent, &pComponent));

            CComBSTR bstrLogicalPath;
            CComBSTR bstrComponentName;

            CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
            CHECK_SUCCESS(pComponent->GetComponentName(&bstrComponentName));
            UINT cPartialFiles;

            CHECK_SUCCESS(pComponent->GetPartialFileCount(&cPartialFiles));
            if (cPartialFiles > 0)
                {
                wprintf(L"\nPartial files for Component Path=%s Name=%s\n",
                        bstrLogicalPath ? bstrLogicalPath : L"",
                        bstrComponentName);

                PrintPartialFiles(pComponent);
                }
            }
        }
    }

BOOL SaveBackupDocument(CComBSTR &bstr)
    {
    HANDLE  hFile = INVALID_HANDLE_VALUE;
    DWORD dwByteToWrite = (bstr.Length() + 1) * sizeof(WCHAR);
    DWORD dwBytesWritten;

    // Create the file (override if exists)
    hFile = CreateFile(g_wszBackupDocumentFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        {
        return FALSE;
        }

    // Write the XML string
    if (! WriteFile(hFile, (LPVOID)(BSTR)bstr, dwByteToWrite, &dwBytesWritten, NULL))
        {
        CloseHandle(hFile);
        return FALSE;
        }

    CloseHandle(hFile);
    return TRUE;
    }

BOOL LoadBackupDocument(const WCHAR* wszFileName, CComBSTR &bstr)
    {
    HANDLE  hFile = INVALID_HANDLE_VALUE;
    DWORD dwBytesToRead = 0;
    DWORD dwBytesRead;

    // Create the file (must exist)
    hFile = CreateFile(wszFileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        {
        return FALSE;
        }

    if ((dwBytesToRead = GetFileSize(hFile, NULL)) <= 0)
        {
        CloseHandle(hFile);
        return FALSE;
        }

    WCHAR *pwszBuffer = NULL;
    DWORD dwNofChars = 0;

    if ((dwBytesToRead % sizeof(WCHAR)) != 0)
        {
        CloseHandle(hFile);
        wprintf(L"Invalid file lenght %lu for backup document file\n", dwBytesToRead);
        return FALSE;
        }
    else
        {
        dwNofChars = dwBytesToRead / sizeof(WCHAR);
        }

    pwszBuffer = (PWCHAR) malloc (dwNofChars * sizeof (WCHAR));
    if (! pwszBuffer)
        {
        CloseHandle(hFile);
        wprintf(L"Failed to allocate memory for backup document buffer\n");
        return FALSE;
        }

    // Read the XML string
    if (! ReadFile(hFile, (LPVOID)pwszBuffer, dwBytesToRead, &dwBytesRead, NULL))
        {
        CloseHandle(hFile);
        free (pwszBuffer);
        return FALSE;
        }

    CloseHandle(hFile);

    if (dwBytesToRead != dwBytesRead)
        {
        free (pwszBuffer);
        wprintf(L"Backup document file is supposed to have %lu bytes but only %lu bytes are read\n", dwBytesToRead, dwBytesRead);
        return FALSE;
        }

    // Copy to output bstr
    bstr.Empty();
    if (bstr.Append(pwszBuffer, dwNofChars) != S_OK)     // don't copy the NULL
        {
        free (pwszBuffer);
        wprintf(L"Failed to copy from temporary buffer into Backup Document XML string\n");
        return FALSE;
        }

    return TRUE;
    }

HRESULT ParseCommandLine (int argc, WCHAR **argv)
    {
    HRESULT hr = S_OK;
    int iArg;

    g_wszBackupDocumentFileName[0] = L'\0';
    g_wszComponentsFileName[0] = L'\0';
    g_wszSavedFilesDirectory[0] = L'\0';
    g_wszPreviousBackupDocumentFileName[0] = L'\0';
    g_wszJustSnapshot[0] = L'\0';
    
    try
        {
        for (iArg=1; iArg<argc; iArg++)
            {
            if ((_wcsicmp(argv[iArg], L"/W") == 0) || (_wcsicmp(argv[iArg], L"-W") == 0))
                {
                iArg++;

                if (iArg >= argc)
                    {
                    wprintf(L"/W switch missing wait-time argument\n");
                    throw(E_INVALIDARG);
                    }
                if (argv[iArg][0] >= L'0' && argv[iArg][0] <= L'9'||
                    argv[iArg][0] >= L'a' && argv[iArg][0] <= L'f')
                    {
                    if (argv[iArg][0] >= L'0' && argv[iArg][0] <= L'9')
                         g_lWriterWait = argv[iArg][0] - L'0';
                     else
                         g_lWriterWait = argv[iArg][0] - L'a' + 10;

                     wprintf(L"Writer wait parameter=%ld.\n", g_lWriterWait);
                    }
                else
                    {
                    wprintf(L"/W switch is followed by invalid wait-time argument\n");
                    throw(E_INVALIDARG);
                    }
                }
            else if ((_wcsicmp(argv[iArg], L"/ASR") == 0) || (_wcsicmp(argv[iArg], L"-ASR") == 0))
                {
                g_bAsrRestoreMode = TRUE;
                g_bRestoreOnly = TRUE;
                g_bExcludeTestWriter = TRUE;
                wprintf(L"Asked to do ASR Restore\n");
                }
            else if ((_wcsicmp(argv[iArg], L"/B") == 0) || (_wcsicmp(argv[iArg], L"-B") == 0))
                {
                g_bBackupOnly = TRUE;
                wprintf(L"Asked to do Backup only\n");
                }
            else if ((_wcsicmp(argv[iArg], L"/R") == 0) || (_wcsicmp(argv[iArg], L"-R") == 0))
                {
                g_bRestoreOnly = TRUE;
                wprintf(L"Asked to do Restore only\n");
                }

            else if ((_wcsicmp(argv[iArg], L"/E") == 0) || (_wcsicmp(argv[iArg], L"-E") == 0))
                {
                g_bExcludeTestWriter = TRUE;
                wprintf(L"Asked to exclude BETEST test writer\n");
                }

            else if ((_wcsicmp(argv[iArg], L"/O") == 0) || (_wcsicmp(argv[iArg], L"-O") == 0))
                {
                g_bBootableSystemState = true;
                wprintf(L"Asked to specify BootableSystemState backup\n");
                }

            else if ((_wcsicmp(argv[iArg], L"/T") == 0) || (_wcsicmp(argv[iArg], L"-T") == 0))
                {
                iArg++;

                if (iArg >= argc)
                    {
                    wprintf(L"/T switch missing backup-type parameter\n");
                    throw(E_INVALIDARG);
                    }

                g_BackupType = ToBackupType(argv[iArg]);

                wprintf(L"backup-type to use is %s\n", argv[iArg]);
                }

            else if ((_wcsicmp(argv[iArg], L"/Pre" )== 0) || (_wcsicmp(argv[iArg], L"-Pre") == 0))
                {
                iArg++;
                
                if (iArg >= argc)
                    {
                    wprintf(L"/S switch missing file-name to load backup document for previous backup \n");
                    throw(E_INVALIDARG);
                    }
                if (wcslen(argv[iArg]) >= MAX_PATH - 1)
                    {
                    wprintf(L"Path for file-name to load backup document is limited to %d\n", MAX_PATH - 2);
                    throw(E_INVALIDARG);
                    }
                wcscpy(g_wszPreviousBackupDocumentFileName, argv[iArg]);
                wprintf(L"File name to load Backup Document for previous backup is \"%s\"\n", g_wszPreviousBackupDocumentFileName);
                }

            else if ((_wcsicmp(argv[iArg], L"/S") == 0) || (_wcsicmp(argv[iArg], L"-S") == 0))
                {
                iArg++;

                if (iArg >= argc)
                    {
                    wprintf(L"/S switch missing file-name to save/load backup document\n");
                    throw(E_INVALIDARG);
                    }
                if (wcslen(argv[iArg]) >= MAX_PATH - 1)
                    {
                    wprintf(L"Path for file-name to save/load backup document is limited to %d\n", MAX_PATH - 2);
                    throw(E_INVALIDARG);
                    }
                wcscpy(g_wszBackupDocumentFileName, argv[iArg]);
                wprintf(L"File name to save/load Backup Document is \"%s\"\n", g_wszBackupDocumentFileName);
                }

            else if ((_wcsicmp(argv[iArg], L"/D") == 0) || (_wcsicmp(argv[iArg], L"-D") == 0))
                {
                iArg++;

                if (iArg >= argc)
                    {
                    wprintf(L"/D switch missing directory path to save/load backup document\n");
                    throw(E_INVALIDARG);
                    }
                if (wcslen(argv[iArg]) >= MAX_PATH - 2)
                    {
                    wprintf(L"Path to save/restore backup files is limited to %d\n", MAX_PATH - 2);
                    throw(E_INVALIDARG);
                    }
                wcscpy(g_wszSavedFilesDirectory, argv[iArg]);
                if (g_wszSavedFilesDirectory[wcslen(g_wszSavedFilesDirectory)-1] != L'\\')
                    wcscat(g_wszSavedFilesDirectory, L"\\");

                wprintf(L"Directory to save/restore backup files is \"%s\"\n", g_wszSavedFilesDirectory);
                DoCopyFile(NULL, g_wszSavedFilesDirectory);

                // replace test writer so that it tests restore options
                g_bRestoreTest = true;
                }


            else if ((_wcsicmp(argv[iArg], L"/C") == 0) || (_wcsicmp(argv[iArg], L"-C") == 0))
                {
                iArg++;

                if (iArg >= argc)
                    {
                    wprintf(L"/C switch missing file-name to load components selection from\n");
                    throw(E_INVALIDARG);
                    }
                if (wcslen(argv[iArg]) >= MAX_PATH)
                    {
                    wprintf(L"Path for file-name to load components selection is limited to %d\n", MAX_PATH);
                    throw(E_INVALIDARG);
                    }
                wcscpy(g_wszComponentsFileName, argv[iArg]);
                wprintf(L"File name for Components Selection is \"%s\"\n", g_wszComponentsFileName);
                }
            else if ((_wcsicmp(argv[iArg], L"/N") == 0) || (_wcsicmp(argv[iArg], L"-N") == 0))
                {
                g_bTestNewInterfaces = true;
                wprintf(L"Asked to test new interfaces\n");
                }
            else if  ((_wcsicmp(argv[iArg], L"/Auth") == 0) || (_wcsicmp(argv[iArg], L"-Auth") == 0))
                {
                g_bAuthRestore = true;
                wprintf(L"asked to do an authoratitive restore\n");
                }
            else if ((_wcsicmp(argv[iArg], L"/U") == 0) || (_wcsicmp(argv[iArg], L"-U") == 0))
                {
                g_bAddFullUNCPath = false;
                wprintf(L"Asked to add share root for remote snapshots rather than full UNC path\n");
            }
            else if ((_wcsicmp(argv[iArg], L"/P") == 0) || (_wcsicmp(argv[iArg], L"-P") == 0))
                {
                g_bPersistantSnapshot = true;
                }
            else if ((_wcsicmp(argv[iArg], L"/Revert") == 0) || (_wcsicmp(argv[iArg], L"-Revert") == 0))
                {
                 iArg++;

                 if (iArg >= argc)
                    {
                    wprintf(L"/Revert switch misssing id of snapshot to revert to");
                    throw(E_INVALIDARG);
                    }

                 CHECK_SUCCESS(CLSIDFromString(argv[iArg], &g_idRevertToSnapshot));
                }
            else if ((_wcsicmp(argv[iArg], L"/Snapshot") == 0) || (_wcsicmp(argv[iArg], L"-Snapshot") == 0))
                {
                iArg++;
                    
                if (iArg >= argc)
                    {
                    wprintf(L"/Snapshot switch misssing volume to snapshot");
                    throw(E_INVALIDARG);
                    }

                wcscpy(g_wszJustSnapshot, argv[iArg]);
                }
            else if ((_wcsicmp(argv[iArg], L"/StopError") == 0) || (_wcsicmp(argv[iArg], L"-StopError") == 0))
                {
                g_bStopOnFirstFailure = true;
                }
            else if ((_wcsicmp(argv[iArg], L"/NoDelete") == 0) || (_wcsicmp(argv[iArg], L"-NoDelete") == 0))
                {
                g_bNoDelete = true;
                }
            else if ((_wcsicmp(argv[iArg], L"/AdditionalRestores") == 0) || (_wcsicmp(argv[iArg], L"-AdditionalRestores") == 0))
                {
                g_bAdditionalRestores = true;
                }
            else if ((_wcsicmp(argv[iArg], L"/Rollback") == 0) || (_wcsicmp(argv[iArg], L"-Rollback") == 0))
                {
                g_bRollbackRecovery = true;
                }
            else if ((_wcsicmp(argv[iArg], L"/V") == 0) || (_wcsicmp(argv[iArg], L"-V") == 0))
                {
                g_bVerbose = true;
                }
             
            else if ((_wcsicmp(argv[iArg], L"/?") == 0) || (_wcsicmp(argv[iArg], L"-?") == 0))
                {
                // Print help
                wprintf(L"BETEST [/V] [/B] [/R] [/E] [/P] [/U/ [/Auth] [/T backup-type] [/S filename] [/C filename] [/D path]\n\n");
                wprintf(L"/V\t\t Print verbose output\n");
                wprintf(L"/B\t\t Performs backup only\n");
                wprintf(L"/R\t\t Performs restore only\n");
                wprintf(L"\t\t Restore-only must be used with /S for a backup document file\n\n");
                wprintf(L"/E\t\t Excludes BETEST test writer\n");
                wprintf(L"/P\t\t Create a persistant snapshot\n");
                wprintf(L"/O\t\t Specifies BootableSystemState backup\n");
                wprintf(L"/T\t\t Chooses backup type (FULL, LOG, COPY, INCREMENTAL, DIFFERENTIAL)\n");
                wprintf(L"/Pre filename\t In case of incremental/differential backup, set backup document for previous full or incremental\n");
                wprintf(L"/S filename\t In case of backup, saves the backup document to file\n");
                wprintf(L"\t\t In case of restore-only, loads the backup document from file\n\n");
                wprintf(L"/D path\t In case of backup, saves the files to be backed up to this location.\n");
                wprintf(L"\t\t In case of restore, restores the backed up files from this location.\n\n");
                wprintf(L"/Auth perform an authorative restore for AD or ADAM\n");                
                wprintf(L"/N Test new backup infrastructure interfaces.\n\n");
                wprintf(L"/Revert <snapshot-id> revert a volume to the specific snapshot\n\n");
                wprintf(L"/Snapshot <volume-name>:  take a snapshot without backing up files\n\n");
                wprintf(L"/StopError stop BETest the first time a writer error is encountered\n\n");
                wprintf(L"/Rollback creates a snapshot that can be used for application rollback\n\n");
                wprintf(L"/U Remote Snapshots: Adds to the set the share rather than the writer path\n\n");
                wprintf(L"/C filename\t Selects which components to backup/restore based on the file\n\n");
                wprintf(L"Components selection file format:\n");
                wprintf(L"\"<writer-id>\": \"<component-logical-path>\" {\"target\" # \"new target\", ...}, ...\"<component-logical-path>\" : '\"<subcomponent-logical-path>,...\";\n\n");
                wprintf(L"\t\twhere several writers may be specified, each one with its own components and subcomponents\n");
                wprintf(L"\t\t<writer-id> is in standard GUID format\n");
                wprintf(L"\t\t<component-logical-path> is either logical-path, logical-path\\component-name\n");
                wprintf(L"\t\tor component-name-only (if there's no logical path)\n\n");
                wprintf(L"\t\tnew-target specifications are optional, and can redirect restore of a specific filespec to a different location\n");
                wprintf(L"\t\tappending '...' on the source of a new-target specification indicates a recursive specification\n");                  
                wprintf(L"For example:\n");
                wprintf(L"\t\t\"{c0577ae6-d741-452a-8cba-99d744008c04}\": \"\\mydatabases\", \"\\mylogfiles\";\n");
                wprintf(L"\t\t\"{f2436e37-09f5-41af-9b2a-4ca2435dbfd5}\" : \"Registry\"  ;\n\n");
                wprintf(L"If no argument is specified, BETEST performs a backup followed by a restore\n");
                wprintf(L"choosing all components reported by all writers\n\n");

                // Set hr such that program terminates
                hr = S_FALSE;
                }
            else
                {
                wprintf(L"Invalid switch\n");
                throw(E_INVALIDARG);
                }
            }

        // Check for invalid combinations
        if (g_bBackupOnly && g_bRestoreOnly)
            {
                wprintf(L"Cannot backup-only and restore-only at the same time...\n");
                throw(E_INVALIDARG);
            }
        if (g_bRestoreOnly && (wcslen(g_wszBackupDocumentFileName) == 0))
            {
                wprintf(L"Cannot restore-only with no backup-document to use.\nUse the /S switch for specifying a file name with backup document from a previous BETEST backup");
                throw(E_INVALIDARG);
            }
        if ((g_BackupType == VSS_BT_DIFFERENTIAL || g_BackupType == VSS_BT_INCREMENTAL) &&
            wcslen(g_wszPreviousBackupDocumentFileName) == 0)
            {
            wprintf(L"Cannot perform an incremental of differeintal backup without a backup document for a previous full or incremental");
            throw (E_INVALIDARG);
            }
        }
    catch (HRESULT hrParse)
        {
        hr = hrParse;
        }

    return hr;
    }

bool IsWriterPath(LPCWSTR wszPath)
    {
    if (wszPath[0] == L'{')
        {
        LPCWSTR wszNext = wcschr(wszPath + 1, L'}');
        if (wszNext == NULL)
            return false;

        if (wszNext - wszPath != 37)
            return false;

        WCHAR buf[39];
        memcpy(buf, wszPath, 38*sizeof(WCHAR));
        buf[38] = L'\0';
        VSS_ID id;

        if (FAILED(CLSIDFromString(buf, &id)))
            return false;

        return wcslen(wszNext) >= 3 &&
               wszNext[1] == L':' &&
               wszNext[2] == L'\\';

        }

    return false;
    }


typedef VSS_ID *PVSS_ID;

bool DoAddComponent
    (
    IVssBackupComponents *pvbc,
    IVssBackupComponents* pPreviousBackup,
    IVssExamineWriterMetadata *pMetadata,
    DWORD dwWriterSchema,    
    VSS_ID idInstance,
    VSS_ID idWriter,
    LPCWSTR wszLogicalPath,
    LPCWSTR wszComponentName,
    LPWSTR wszVolumes
    );


// add a child component to the backup components document
bool AddDependentComponent
    (
    IVssBackupComponents *pvbc,
    IVssBackupComponents *pPreviousBackup,
    DWORD dwWriterSchema,    
    VSS_ID id,
    CComBSTR bstrLogicalPath,
    CComBSTR bstrComponentName,
    LPWSTR wszVolumes
    )
    {
    HRESULT hr;

    UINT cWriters, iWriter;
    CHECK_SUCCESS(pvbc->GetWriterMetadataCount(&cWriters));
    CComPtr<IVssExamineWriterMetadata> pMetadata;
    VSS_ID idInstance = GUID_NULL;

    for(iWriter = 0; iWriter < cWriters; iWriter++)
        {
        CHECK_SUCCESS(pvbc->GetWriterMetadata(iWriter, &idInstance, &pMetadata));

        VSS_ID idInstanceT;
        VSS_ID idWriter;
        CComBSTR bstrWriterName;
        VSS_USAGE_TYPE usage;
        VSS_SOURCE_TYPE source;

        CHECK_SUCCESS(pMetadata->GetIdentity
                            (
                            &idInstanceT,
                            &idWriter,
                            &bstrWriterName,
                            &usage,
                            &source
                            ));
        if (idWriter == id)
            break;

        pMetadata = NULL;
        }

    if (iWriter > cWriters)
        {
        wprintf(L"Cannot backup component: %s\\%s\nWriter doesn't exist.\n\n", bstrLogicalPath, bstrComponentName);
        return false;
        }


    wprintf(L"Backing up subcomponent: %s\\%s.\n\n", bstrLogicalPath, bstrComponentName);
    return DoAddComponent
                (
                pvbc,
                pPreviousBackup,
                pMetadata,
                dwWriterSchema,
                idInstance,
                id,
                bstrLogicalPath,
                bstrComponentName,
                wszVolumes
                );
    }


bool FindComponent
    (
    IVssExamineWriterMetadata *pMetadata,
    LPCWSTR wszLogicalPath,
    LPCWSTR wszComponentName,
    IVssWMComponent **ppComponent
    )
    {
    HRESULT hr;

    UINT cIncludeFiles, cExcludeFiles, cComponents;
    CHECK_SUCCESS(pMetadata->GetFileCounts(
                            &cIncludeFiles,
                            &cExcludeFiles,
                            &cComponents));

    for(UINT iComponent = 0; iComponent < cComponents; iComponent++)
        {
        CComPtr<IVssWMComponent> pComponent;
        CHECK_SUCCESS(pMetadata->GetComponent(iComponent, &pComponent));
        PVSSCOMPONENTINFO pInfo;
        CHECK_SUCCESS(pComponent->GetComponentInfo(&pInfo));
        if (_wcsicmp(wszComponentName, pInfo->bstrComponentName) == 0 &&
            ((wszLogicalPath == NULL &&
              (pInfo->bstrLogicalPath == NULL ||
               wcslen(pInfo->bstrLogicalPath) == 0)) ||
             (wszLogicalPath != NULL &&
              pInfo->bstrLogicalPath != NULL &&
              _wcsicmp(wszLogicalPath, pInfo->bstrLogicalPath) == 0)))
            {
            pComponent->FreeComponentInfo(pInfo);
            break;
            }

        pComponent->FreeComponentInfo(pInfo);
        }

    if (iComponent < cComponents)
        {
        CHECK_SUCCESS(pMetadata->GetComponent(iComponent, ppComponent));
        return true;
        }

    return false;
    }


bool UpdateVolumeList
    (
    IVssWMComponent* pComponent, 
    PVSSCOMPONENTINFO pInfo, 
    LPWSTR wszVolumes
    )
    {
    HRESULT hr = S_OK;
    
    bool bOneSelected = false;
    for(UINT i = 0; i < pInfo->cFileCount; i++)
       {
       CComPtr<IVssWMFiledesc> pFiledesc;
       CHECK_SUCCESS(pComponent->GetFile(i, &pFiledesc));

       DWORD dwMask;
       CHECK_SUCCESS(pFiledesc->GetBackupTypeMask(&dwMask));

       bOneSelected = bOneSelected || needsBackingUp(dwMask);
       
       if (needsBackingUp(dwMask) && needsSnapshot(dwMask))
            {
            CComBSTR bstrPath;
            CHECK_SUCCESS(pFiledesc->GetPath(&bstrPath));
            
            DoAddToVolumeList(bstrPath, wszVolumes);
            }
       }

    for(UINT i = 0; i < pInfo->cDatabases; i++)
        {
        CComPtr<IVssWMFiledesc> pFiledesc;
        CHECK_SUCCESS(pComponent->GetDatabaseFile(i, &pFiledesc));

       DWORD dwMask;
       CHECK_SUCCESS(pFiledesc->GetBackupTypeMask(&dwMask));

       bOneSelected = bOneSelected || needsBackingUp(dwMask);

       if (needsBackingUp(dwMask) && needsSnapshot(dwMask))
            {
            CComBSTR bstrPath;
            CHECK_SUCCESS(pFiledesc->GetPath(&bstrPath));

            DoAddToVolumeList(bstrPath, wszVolumes);
            }
        }

    for(UINT i = 0; i < pInfo->cLogFiles; i++)
        {
        CComPtr<IVssWMFiledesc> pFiledesc;
        CHECK_SUCCESS(pComponent->GetDatabaseLogFile(i, &pFiledesc));

       DWORD dwMask;
       CHECK_SUCCESS(pFiledesc->GetBackupTypeMask(&dwMask));
       
       bOneSelected = bOneSelected || needsBackingUp(dwMask);
       
       if (needsBackingUp(dwMask) && needsSnapshot(dwMask))
           {
           CComBSTR bstrPath;
           CHECK_SUCCESS(pFiledesc->GetPath(&bstrPath));

           DoAddToVolumeList(bstrPath, wszVolumes);
           }
        }  

    return bOneSelected;
    }

bool DoAddComponent
    (
    IVssBackupComponents *pvbc,
    IVssBackupComponents* pPreviousBackup,        
    IVssExamineWriterMetadata *pMetadata,
    DWORD dwWriterSchema,    
    VSS_ID idInstance,
    VSS_ID idWriter,
    LPCWSTR wszLogicalPath,
    LPCWSTR wszComponentName,
    LPWSTR wszVolumes
    )
    {
    HRESULT hr;

    // was at least one file selected
    bool bAtLeastOneSelected = false;

    CComPtr<IVssWMComponent> pComponent;
    if (!FindComponent
            (
            pMetadata,
            wszLogicalPath,
            wszComponentName,
            &pComponent
            ))
        {
        wprintf(L"Component is not found: " WSTR_GUID_FMT L":\\%s\\%s",
                GUID_PRINTF_ARG(idWriter),
                wszLogicalPath ? wszLogicalPath : L"",
                wszComponentName);

        return false;
        }

    PVSSCOMPONENTINFO pInfo;
    CHECK_SUCCESS(pComponent->GetComponentInfo(&pInfo));
    hr = pvbc->AddComponent
            (
            idInstance,
            idWriter,
            pInfo->type,
            pInfo->bstrLogicalPath,
            pInfo->bstrComponentName
            );

    if (hr == VSS_E_OBJECT_ALREADY_EXISTS)
        return false;

    CHECK_SUCCESS(hr);

    // if we're basing this off of a previous backup, set the previous backup stamp
    if (pPreviousBackup != NULL && dwWriterSchema & VSS_BS_TIMESTAMPED)
        {
        BS_ASSERT(g_BackupType == VSS_BT_DIFFERENTIAL || g_BackupType == VSS_BT_INCREMENTAL);
        
        CComPtr<IVssComponent> pPreviousComponent;
        if (FindComponentInDoc(pPreviousBackup, idWriter, wszLogicalPath, wszComponentName, &pPreviousComponent))
            {
            CComBSTR stamp;
            CHECK_SUCCESS(pPreviousComponent->GetBackupStamp(&stamp));
            CHECK_SUCCESS(pvbc->SetPreviousBackupStamp
                                            (
                                            idWriter, 
                                            pInfo->type,             
                                            pInfo->bstrLogicalPath,
                                            pInfo->bstrComponentName,
                                            stamp
                                            ));
            }
        }
    

    if (pInfo->type == VSS_CT_DATABASE &&
        pInfo->bstrLogicalPath &&
        wcscmp(pInfo->bstrLogicalPath, L"\\mydatabases") == 0 &&
        wcscmp(pInfo->bstrComponentName, L"db1") == 0)
        {
        CHECK_SUCCESS(pvbc->SetPreviousBackupStamp
                            (
                            idWriter,
                            pInfo->type,
                            pInfo->bstrLogicalPath,
                            pInfo->bstrComponentName,
                            L"LASTFULLBACKUP"
                            ));

        CHECK_SUCCESS(pvbc->SetBackupOptions
                            (
                            idWriter,
                            pInfo->type,
                            pInfo->bstrLogicalPath,
                            pInfo->bstrComponentName,
                            L"DOFASTINCREMENAL"
                            ));
        }


    // add volumes to the current snapshot set
   bAtLeastOneSelected = UpdateVolumeList(pComponent, pInfo, wszVolumes);

    // add volumes to the current snapshot set for all implicitly-selected components
    CComBSTR bstrFullPath = wszLogicalPath;
    if (bstrFullPath.Length() > 0)
        bstrFullPath += L"\\";
    bstrFullPath += wszComponentName;
    if (!bstrFullPath)
        Error(E_OUTOFMEMORY, L"Ran out of memory");

    bAtLeastOneSelected = 
         AddDependencies(pComponent, pInfo, pvbc, pPreviousBackup, dwWriterSchema, wszVolumes) 
         || bAtLeastOneSelected;
    
    UINT cIncludeFiles = 0, cExcludeFiles = 0, cComponents = 0;
    CHECK_SUCCESS(pMetadata->GetFileCounts(&cIncludeFiles, &cExcludeFiles, &cComponents));
    for (UINT iComponent = 0; iComponent < cComponents; iComponent++)
        {
        CComPtr<IVssWMComponent> pCurrent;
        CHECK_SUCCESS(pMetadata->GetComponent(iComponent, &pCurrent));

        PVSSCOMPONENTINFO pCurrentInfo = NULL;
        CHECK_SUCCESS(pCurrent->GetComponentInfo(&pCurrentInfo));
        
        if (pCurrentInfo->bstrLogicalPath &&
            wcsstr(pCurrentInfo->bstrLogicalPath, bstrFullPath) == pCurrentInfo->bstrLogicalPath)
            {
            wprintf(L"Component %s\\%s implicitly selected for backup\n", pCurrentInfo->bstrLogicalPath,
                                                                                                       pCurrentInfo->bstrComponentName);
            
            bAtLeastOneSelected = UpdateVolumeList(pCurrent, pCurrentInfo, wszVolumes)
                   || AddDependencies(pComponent, pInfo, pvbc, pPreviousBackup, dwWriterSchema, wszVolumes) 
                   || bAtLeastOneSelected;
            }

        pCurrent->FreeComponentInfo(pCurrentInfo);
        }

    pComponent->FreeComponentInfo(pInfo);
    return bAtLeastOneSelected;
    }

bool AddDependencies
       (
       IVssWMComponent* pComponent,
       PVSSCOMPONENTINFO pInfo,
       IVssBackupComponents *pvbc,
       IVssBackupComponents* pPreviousBackup,
       DWORD dwWriterSchema,
       LPWSTR wszVolumes
       )
{
    HRESULT hr = S_OK;
    
    bool bAtLeastOneSelected = false;
    for (unsigned i = 0; i < pInfo->cDependencies; i++)
        {
        CComPtr<IVssWMDependency> pDependency;
        CHECK_SUCCESS(pComponent->GetDependency(i, &pDependency));

        VSS_ID writerId;
        CComBSTR logicalPath, componentName;
        CHECK_SUCCESS(pDependency->GetWriterId(&writerId));
        CHECK_NOFAIL(pDependency->GetLogicalPath(&logicalPath));
        CHECK_SUCCESS(pDependency->GetComponentName(&componentName));

        if (AddDependentComponent(pvbc, pPreviousBackup, dwWriterSchema, writerId, logicalPath, componentName, wszVolumes))
            bAtLeastOneSelected = true;
        }

    return bAtLeastOneSelected;
}

// find component in the backup components document
bool FindComponentInDoc
    (
    IVssBackupComponents *pvbc,
    VSS_ID idWriter,
    LPCWSTR wszLogicalPath,
    LPCWSTR wszComponentName,
    IVssComponent **ppComponent,
    VSS_ID *pidInstance
    )
    {
    HRESULT hr;

    UINT cWriterComponents;
    CHECK_SUCCESS(pvbc->GetWriterComponentsCount(&cWriterComponents));
    for(UINT iWriterComponent = 0; iWriterComponent < cWriterComponents; iWriterComponent++)
        {
        CComPtr<IVssWriterComponentsExt> pWriter;

        CHECK_SUCCESS(pvbc->GetWriterComponents(iWriterComponent, &pWriter));
        VSS_ID idInstanceT;
        VSS_ID idWriterT;
        CHECK_SUCCESS(pWriter->GetWriterInfo(&idInstanceT, &idWriterT));
        if (idWriter == idWriterT)
            {
            UINT cComponents;

            CHECK_SUCCESS(pWriter->GetComponentCount(&cComponents));
            for(UINT iComponent = 0; iComponent < cComponents; iComponent++)
                {
                CComPtr<IVssComponent> pComponent;

                CHECK_SUCCESS(pWriter->GetComponent(iComponent, &pComponent));

                CComBSTR bstrLogicalPath;
                CComBSTR bstrComponentName;
                CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
                CHECK_SUCCESS(pComponent->GetComponentName(&bstrComponentName));
                if (_wcsicmp(bstrComponentName, wszComponentName) == 0 &&
                    ((!wszLogicalPath && !bstrLogicalPath) ||
                     (wszLogicalPath && bstrLogicalPath &&
                      _wcsicmp(wszLogicalPath, bstrLogicalPath) == 0)))
                    {
                    CHECK_SUCCESS(pWriter->GetComponent(iComponent, ppComponent));
                    if (pidInstance)
                        *pidInstance = idInstanceT;
                    return true;
                    }
                }
            }
        }

    return false;
    }



void SetSubcomponentSelectedForRestore
    (
    IVssBackupComponents *pvbc,
    LPCWSTR wszComponentPath,
    LPCWSTR wszComponentName
    )
    {
    HRESULT hr;

    BS_ASSERT(IsWriterPath(wszComponentPath));

    VSS_ID id;
    WCHAR buf[39];

    memcpy(buf, wszComponentPath, 38*sizeof(WCHAR));
    buf[38] = L'\0';
    CLSIDFromString(buf, &id);
    LPCWSTR wszLogicalPath = NULL;
    if (wcslen(wszComponentPath) > 40)
        wszLogicalPath = wszComponentPath + 40;

    CComPtr<IVssComponent> pComponent;
    VSS_ID idInstance;
    if (!FindComponentInDoc
            (
            pvbc,
            id,
            wszLogicalPath,
            wszComponentName,
            &pComponent,
            &idInstance
            ))
        {
        wprintf(L"Subcomponent %s\\%s was not found.\n\n", wszComponentPath, wszComponentName);
        BS_ASSERT(FALSE);
        throw E_UNEXPECTED;
        }

    bool bSelectedForRestore;
    CHECK_SUCCESS(pComponent->IsSelectedForRestore(&bSelectedForRestore))

    // if component is already selected for restore, then do nothing.
    if (!bSelectedForRestore)
        {
        VSS_COMPONENT_TYPE ct;
        CComBSTR bstrLogicalPath;
        CComBSTR bstrComponentName;

        CHECK_SUCCESS(pComponent->GetComponentType(&ct));
        CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
        CHECK_NOFAIL(pComponent->GetComponentName(&bstrComponentName));
        CHECK_SUCCESS(pvbc->SetSelectedForRestore(
                            id,
                            ct,
                            bstrLogicalPath,
                            bstrComponentName,
                            true
                            ));

        SetSubcomponentsSelectedForRestore(pvbc, idInstance, pComponent);
        }
    }

// determine if any subcomponents of a component selected for restore
// should also be selected for restore
void SetSubcomponentsSelectedForRestore
    (
    IVssBackupComponents *pvbc,
    VSS_ID idInstance,
    IVssComponent *pComponent
    )
    {
    HRESULT hr;

    CComPtr<IVssExamineWriterMetadata> pWriterMetadata;
    if (g_wszSavedFilesDirectory[0] == L'\0')
        return;

    LoadMetadataFile(idInstance, &pWriterMetadata);

    CComBSTR bstrLogicalPath;
    CComBSTR bstrComponentName;
    CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
    CHECK_SUCCESS(pComponent->GetComponentName(&bstrComponentName));

    CComPtr<IVssWMComponent> pWMComponent;
    if (!FindComponent
            (
            pWriterMetadata,
            bstrLogicalPath,
            bstrComponentName,
            &pWMComponent
            ))
        {
        wprintf(L"Component %s\\%s cannot be found.\n", bstrLogicalPath, bstrComponentName);
        BS_ASSERT(FALSE);
        throw E_UNEXPECTED;
        }

    PVSSCOMPONENTINFO pInfo;
    CHECK_SUCCESS(pWMComponent->GetComponentInfo(&pInfo));

    unsigned i;
    for(i = 0; i < pInfo->cFileCount; i++)
        {
        CComPtr<IVssWMFiledesc> pFiledesc;
        CHECK_SUCCESS(pWMComponent->GetFile(i, &pFiledesc));

        CComBSTR bstrPath;
        CHECK_SUCCESS(pFiledesc->GetPath(&bstrPath));
        if (IsWriterPath(bstrPath))
            {
            CComBSTR bstrComponentName;
            CHECK_SUCCESS(pFiledesc->GetFilespec(&bstrComponentName));
            SetSubcomponentSelectedForRestore(pvbc, bstrPath, bstrComponentName);
            }
        }

    for(i = 0; i < pInfo->cDatabases; i++)
        {
        CComPtr<IVssWMFiledesc> pFiledesc;
        CHECK_SUCCESS(pWMComponent->GetDatabaseFile(i, &pFiledesc));

        CComBSTR bstrPath;
        CHECK_SUCCESS(pFiledesc->GetPath(&bstrPath));
        if (IsWriterPath(bstrPath))
            {
            CComBSTR bstrComponentName;
            CHECK_SUCCESS(pFiledesc->GetFilespec(&bstrComponentName));
            SetSubcomponentSelectedForRestore(pvbc, bstrPath, bstrComponentName);
            }
        }

    for(i = 0; i < pInfo->cLogFiles; i++)
        {
        CComPtr<IVssWMFiledesc> pFiledesc;
        CHECK_SUCCESS(pWMComponent->GetDatabaseLogFile(i, &pFiledesc));

        CComBSTR bstrPath;
        CHECK_SUCCESS(pFiledesc->GetPath(&bstrPath));
        if (IsWriterPath(bstrPath))
            {
            CComBSTR bstrComponentName;
            CHECK_SUCCESS(pFiledesc->GetFilespec(&bstrComponentName));
            SetSubcomponentSelectedForRestore(pvbc, bstrPath, bstrComponentName);
            }
        }

    pWMComponent->FreeComponentInfo(pInfo);
    }


void RevertToSnapshot()
    {
   HRESULT hr = S_OK;
    
   CComPtr<IVssBackupComponents> pComp;
   CHECK_SUCCESS(::CreateVssBackupComponents(&pComp));
   CHECK_SUCCESS(pComp->InitializeForBackup());
   CHECK_SUCCESS(pComp->SetContext(VSS_CTX_ALL));
   
    VSS_SNAPSHOT_PROP prop;
    CHECK_SUCCESS(pComp->GetSnapshotProperties(g_idRevertToSnapshot, &prop));
    CHECK_SUCCESS(pComp->RevertToSnapshot(g_idRevertToSnapshot, true));
    CComPtr<IVssAsync> pAsync;
    hr = pComp->QueryRevertStatus(prop.m_pwszOriginalVolumeName, &pAsync);
    if (hr == VSS_E_OBJECT_NOT_FOUND)
        {
        wprintf(L"finished real fast!\n");
        return;
        }
    else
        CHECK_SUCCESS(hr);
    
    LoopWait(pAsync, 6000, L"RevertToSnapshot", true);    
    }

extern "C" __cdecl wmain(int argc, WCHAR **argv)
    {
    WCHAR wszVolumes[2048];
    wszVolumes[0] = L'\0';

    UINT cSnapshot = 0;
    VSS_ID rgpSnapshotId[64];

    CTestVssWriter *pInstance = NULL;
    bool bCreated = false;
    bool bSubscribed = false;
    HRESULT hrMain = S_OK;
    bool bCoInitializeSucceeded = false;


    try
    {
    HRESULT hr = S_OK;
    CComBSTR bstrXML;
    BOOL bXMLSaved = FALSE;

    // Parse command line arguments
    if (ParseCommandLine(argc, argv) != S_OK)
        {
        // Don't throw since we want to avoid assertions here - we can return safely
        return (3);
        }

    CHECK_SUCCESS(CoInitializeEx(NULL, COINIT_MULTITHREADED));

    // Initialize COM security
    CHECK_SUCCESS
        (
        CoInitializeSecurity
            (
            NULL,                                //  IN PSECURITY_DESCRIPTOR         pSecDesc,
            -1,                                  //  IN LONG                         cAuthSvc,
            NULL,                                //  IN SOLE_AUTHENTICATION_SERVICE *asAuthSvc,
            NULL,                                //  IN void                        *pReserved1,
            RPC_C_AUTHN_LEVEL_CONNECT,           //  IN DWORD                        dwAuthnLevel,
            RPC_C_IMP_LEVEL_IMPERSONATE,         //  IN DWORD                        dwImpLevel,
            NULL,                                //  IN void                        *pAuthList,
            EOAC_NONE,                           //  IN DWORD                        dwCapabilities,
            NULL                                 //  IN void                        *pReserved3
            )
        );

    bCoInitializeSucceeded = true;

    if ( !AssertPrivilege( SE_BACKUP_NAME ) )
        {
        wprintf( L"AssertPrivilege returned error, rc:%d\n", GetLastError() );
        return 2;
        }

    if (g_idRevertToSnapshot != GUID_NULL)
        {
        RevertToSnapshot();
        return 0;
        }

    if (wcslen(g_wszJustSnapshot))
        {
        CComPtr<IVssAsync> pAsync;
        CComPtr<IVssBackupComponents> pvbc;
        CHECK_SUCCESS(CreateVssBackupComponents(&pvbc));
        CHECK_SUCCESS(pvbc->InitializeForBackup());
        CHECK_SUCCESS(pvbc->SetBackupState (true, g_bBootableSystemState,g_BackupType,true));

//        CHECK_SUCCESS(pvbc->SetContext(VSS_CTX_FILE_SHARE_BACKUP));
        
        CHECK_NOFAIL(pvbc->GatherWriterMetadata(&pAsync));
        LoopWait(pAsync, 30, L"GatherWriterMetadata");

        VSS_ID idSet, idSnap;
        CHECK_SUCCESS(pvbc->StartSnapshotSet(&idSet));
        CHECK_SUCCESS(pvbc->AddToSnapshotSet(g_wszJustSnapshot, GUID_NULL, &idSnap));
        DoPrepareBackup(pvbc);
        CheckStatus(pvbc, L"After Prepare Backup");

        HRESULT hrResult;
        DoSnapshotSet(pvbc, hrResult);
        CheckStatus(pvbc, L"After DoSnapshotSet");

        return 0;
    }
    
    // Get chosen components for backup and/or restore
    if (wcslen(g_wszComponentsFileName) > 0)
        {
        g_pWriterSelection = CWritersSelection::CreateInstance();
        if (g_pWriterSelection == NULL)
            {
            wprintf(L"allocation failure\n");
            DebugBreak();
            }

        if (g_pWriterSelection->BuildChosenComponents(g_wszComponentsFileName) != S_OK)
            {
            wprintf(L"Component selection in %s is ignored due to a failure in processing the file\n", g_wszComponentsFileName);
            g_pWriterSelection = 0;
            }
        }

    if (! g_bExcludeTestWriter) {
        pInstance = new CTestVssWriter(g_bRestoreTest, g_bTestNewInterfaces, g_lWriterWait, g_lRestoreTestOptions);
        if (pInstance == NULL)
            {
            wprintf(L"allocation failure\n");
            DebugBreak();
            }

        bCreated = true;
        pInstance->Initialize();
        CHECK_SUCCESS(pInstance->Subscribe());
        bSubscribed = true;
    }

    if (! g_bRestoreOnly)
        {
        CComBSTR strSnapshotSetId = "12345678-1234-1234-1234-1234567890ab";

        CComPtr<IVssBackupComponents> pvbc;

        CHECK_SUCCESS(CreateVssBackupComponents(&pvbc));

        CComPtr<IVssBackupComponents> pPreviousBackup;
        if (wcslen(g_wszPreviousBackupDocumentFileName) > 0)
            {
            CHECK_SUCCESS(CreateVssBackupComponents(&pPreviousBackup));
            
            CComBSTR bstrXML;
            LoadBackupDocument(g_wszPreviousBackupDocumentFileName, bstrXML);
            CHECK_SUCCESS(pPreviousBackup->InitializeForBackup(bstrXML));
            }

        CHECK_SUCCESS(pvbc->InitializeForBackup());
        CHECK_SUCCESS(pvbc->SetBackupState (true,
                            g_bBootableSystemState,
                            g_BackupType,
                            true));


        unsigned cWriters;
        CComPtr<IVssAsync> pAsync;
        CHECK_NOFAIL(pvbc->GatherWriterMetadata(&pAsync));
        LoopWait(pAsync, 30, L"GatherWriterMetadata");
        CHECK_NOFAIL(pvbc->GetWriterMetadataCount(&cWriters));

        VSS_ID id;
        LONG lContext = (g_bPersistantSnapshot) ? VSS_CTX_APP_ROLLBACK :
                                                                               VSS_CTX_BACKUP;
        if (g_bRollbackRecovery)    
            lContext |= VSS_VOLSNAP_ATTR_ROLLBACK_RECOVERY;
        
        CHECK_SUCCESS(pvbc->SetContext(lContext));

        while(TRUE)
            {
            hr = pvbc->StartSnapshotSet(&id);
            if (hr == S_OK)
                break;

            if (hr == VSS_E_SNAPSHOT_SET_IN_PROGRESS)
                Sleep(1000);
            else
                CHECK_SUCCESS(hr);
            }

        BOOL bAtLeastOneSelected = FALSE;

        for(unsigned iWriter = 0; iWriter < cWriters; iWriter++)
            {
            CComPtr<IVssExamineWriterMetadata> pMetadata;
            VSS_ID idInstance;

            CHECK_SUCCESS(pvbc->GetWriterMetadata(iWriter, &idInstance, &pMetadata));
            VSS_ID idInstanceT;
            VSS_ID idWriter;
            CComBSTR bstrWriterName;
            VSS_USAGE_TYPE usage;
            VSS_SOURCE_TYPE source;

            CHECK_SUCCESS(pMetadata->GetIdentity
                    (
                    &idInstanceT,
                    &idWriter,
                    &bstrWriterName,
                    &usage,
                    &source
                    ));

            wprintf (L"\n\n");

            if (memcmp(&idInstance, &idInstanceT, sizeof(VSS_ID)) != 0)
                {
                wprintf(L"Instance id mismatch\n");
                DebugBreak();
                }

            WCHAR *pwszInstanceId;
            WCHAR *pwszWriterId;
            UuidToString(&idInstance, &pwszInstanceId);
            UuidToString(&idWriter, &pwszWriterId);
            wprintf (L"WriterName = %s\n\n"
                 L"    WriterId   = %s\n"
                 L"    InstanceId = %s\n"
                 L"    UsageType  = %d (%s)\n"
                 L"    SourceType = %d (%s)\n",
                 bstrWriterName,
                 pwszWriterId,
                 pwszInstanceId,
                 usage,
                 GetStringFromUsageType (usage),
                 source,
                 GetStringFromSourceType (source));

            RpcStringFree(&pwszInstanceId);
            RpcStringFree(&pwszWriterId);

            VSS_RESTOREMETHOD_ENUM method;
            CComBSTR bstrUserProcedure;
            CComBSTR bstrService;
            VSS_WRITERRESTORE_ENUM writerRestore;
            unsigned cMappings;
            bool bRebootRequired;

            CHECK_NOFAIL(pMetadata->GetRestoreMethod (&method,
                                  &bstrService,
                                  &bstrUserProcedure,
                                  &writerRestore,
                                  &bRebootRequired,
                                  &cMappings));


            wprintf (L"\n"
                 L"    Restore method = %d (%s)\n"
                 L"    Service        = %s\n"
                 L"    User Procedure = %s\n"
                 L"    WriterRestore  = %d (%s)\n"
                 L"    RebootRequired = %s\n",
                 method,
                 GetStringFromRestoreMethod (method),
                 bstrService,
                 bstrUserProcedure,
                 writerRestore,
                 GetStringFromWriterRestoreMethod (writerRestore),
                 bRebootRequired ? L"yes" : L"no");

                for(unsigned iMapping = 0; iMapping < cMappings; iMapping++)
                {
                CComPtr<IVssWMFiledesc> pFiledesc;

                CHECK_SUCCESS(pMetadata->GetAlternateLocationMapping(iMapping, &pFiledesc));

                PrintFiledesc(pFiledesc, L"AlternateMapping");
                }

            unsigned cIncludeFiles, cExcludeFiles, cComponents;
            CHECK_SUCCESS(pMetadata->GetFileCounts (&cIncludeFiles,
                                &cExcludeFiles,
                                &cComponents));

            CComBSTR bstrPath;
            CComBSTR bstrFilespec;
            CComBSTR bstrAlternate;
            CComBSTR bstrDestination;
            unsigned i;

            for(i = 0; i < cIncludeFiles; i++)
                {
                CComPtr<IVssWMFiledesc> pFiledesc;
                CHECK_SUCCESS(pMetadata->GetIncludeFile(i, &pFiledesc));

                PrintFiledesc(pFiledesc, L"\n    Include File");
                }

            for(i = 0; i < cExcludeFiles; i++)
                {
                CComPtr<IVssWMFiledesc> pFiledesc;
                CHECK_SUCCESS(pMetadata->GetExcludeFile(i, &pFiledesc));
                PrintFiledesc(pFiledesc, L"\n    Exclude File");
                }

            DWORD schema = 0;
            CHECK_SUCCESS(pMetadata->GetBackupSchema(&schema));
            wprintf(L"        BackupSchema        = 0x%x\n", schema);

            if (g_pWriterSelection && !g_pWriterSelection->IsWriterSelected(idWriter))
                continue;

            for(unsigned iComponent = 0; iComponent < cComponents; iComponent++)
                {
                CComPtr<IVssWMComponent> pComponent;
                PVSSCOMPONENTINFO pInfo;
                CHECK_SUCCESS(pMetadata->GetComponent(iComponent, &pComponent));
                CHECK_SUCCESS(pComponent->GetComponentInfo(&pInfo));
                wprintf (L"\n"
                         L"    Component %d, type = %d (%s)\n"
                         L"        LogicalPath = %s\n"
                         L"        Name        = %s\n"
                         L"        Caption     = %s\n",
                         iComponent,
                         pInfo->type,
                         GetStringFromComponentType (pInfo->type),
                         pInfo->bstrLogicalPath,
                         pInfo->bstrComponentName,
                         pInfo->bstrCaption);


                 wprintf (L"        RestoreMetadata        = %s\n"
                          L"        NotifyOnBackupComplete = %s\n"
                          L"        Selectable             = %s\n"
                          L"        SelectableForRestore = %s\n"
                          L"        ComponentFlags        = 0x%x\n",
                          pInfo->bRestoreMetadata        ? L"yes" : L"no",
                          pInfo->bNotifyOnBackupComplete ? L"yes" : L"no",
                          pInfo->bSelectable             ? L"yes" : L"no",
                          pInfo->bSelectableForRestore ? L"yes" : L"no",
                          pInfo->dwComponentFlags);

                for (unsigned iDependency = 0; iDependency < pInfo->cDependencies; iDependency++)
                    {
                    CComPtr<IVssWMDependency> pDependency;
                    CHECK_NOFAIL(pComponent->GetDependency(iDependency, &pDependency));

                    VSS_ID writerId;
                    CComBSTR logicalPath, componentName;
                    CHECK_SUCCESS(pDependency->GetWriterId(&writerId));
                    CHECK_NOFAIL(pDependency->GetLogicalPath(&logicalPath));
                    CHECK_SUCCESS(pDependency->GetComponentName(&componentName));

                    wprintf (L"        (Dependent Component):              WriterId " WSTR_GUID_FMT L"\n"
                                L"                                                           Logical Path %s\n"
                                L"                                                           Name %s\n",
                                GUID_PRINTF_ARG(writerId),
                                logicalPath,
                                componentName);
                    }

                // print out all the file descriptions
                for (UINT i= 0; i < pInfo->cFileCount; i++)
                    {
                    CComPtr<IVssWMFiledesc> pFiledesc;
                    CHECK_SUCCESS(pComponent->GetFile(i, &pFiledesc));
                    PrintFiledesc(pFiledesc, L"        FileGroupFile");
                    }
                for(UINT i = 0; i < pInfo->cDatabases; i++)
                   {
                   CComPtr<IVssWMFiledesc> pFiledesc;
                   CHECK_SUCCESS(pComponent->GetDatabaseFile(i, &pFiledesc));
                   PrintFiledesc(pFiledesc, L"        DatabaseFile");
                   }
                for(UINT i = 0; i < pInfo->cLogFiles; i++)
                   {
                   CComPtr<IVssWMFiledesc> pFiledesc;                   
                   CHECK_SUCCESS(pComponent->GetDatabaseLogFile(i, &pFiledesc));
                   PrintFiledesc(pFiledesc, L"        DatabaseLogFile");
                   }
                
                BOOL bSelected = TRUE;
                if (g_pWriterSelection)
                    {
                    // User provided a valid selection file
                    bSelected = g_pWriterSelection->IsComponentSelected(idWriter, pInfo->bstrLogicalPath, pInfo->bstrComponentName);
                    if (bSelected)
                        {
//                        if (!pInfo->bSelectable && !pInfo->bSelectableForRestore)
//                            {
//                            Error(E_UNEXPECTED, L"\na completely non-selectable component was selected!\n");
//                            }
                        
                        wprintf (L"\n        Component \"%s\" IS selected for Backup\n\n", pInfo->bstrComponentName);
                        }
                    else
                        {
                        wprintf (L"\n        Component \"%s\" is NOT selected for Backup\n\n", pInfo->bstrComponentName);
                        }
                    }

                // only add selectable components to the document or non-selectable components that have no selectable ancestor
                bSelected = bSelected || MustAddComponent(pMetadata, pInfo->bstrLogicalPath, pInfo->bstrComponentName);
                if (bSelected)
                    {
                    if (DoAddComponent
                            (
                            pvbc,
                            pPreviousBackup, 
                            pMetadata,
                            schema, 
                            idInstance,
                            idWriter,
                            pInfo->bstrLogicalPath,
                            pInfo->bstrComponentName,
                            wszVolumes
                            ))
                        bAtLeastOneSelected = true;
                    }

                pComponent->FreeComponentInfo(pInfo);
                }            
            }

            PVSS_ID rgSnapshotIds = rgpSnapshotId;
            UpdateSnapshotSet(pvbc, wszVolumes, rgSnapshotIds, &cSnapshot);

        //
        // Proceed with backup only if at least one component and one volume was selected for backup
        //
        if (bAtLeastOneSelected)
            {

            DoPrepareBackup(pvbc);

            CheckStatus(pvbc, L"After Prepare Backup");

            HRESULT hrResult;
            DoSnapshotSet(pvbc, hrResult);

            if (FAILED(hrResult))
                {
                wprintf(L"Creating the snapshot failed.  hr = 0x%08lx\n", hrResult);
                CheckStatus(pvbc, L"After Do Snapshot");
                }
            else
                {
                CheckStatus(pvbc, L"After Do Snapshot");

                PrintPartialFilesForComponents(pvbc);
                PrintDifferencedFilesForComponents(pvbc);
                
                SaveFiles(pvbc, rgpSnapshotId, cSnapshot);

                DoBackupComplete(pvbc);
                CheckStatus(pvbc, L"After Backup Complete");

                // Save backup document in a string
                CHECK_SUCCESS(pvbc->SaveAsXML(&bstrXML));
                bXMLSaved = TRUE;

                // Save backup document (XML string) in a file
                if (wcslen(g_wszBackupDocumentFileName) > 0)
                    {
                    if (SaveBackupDocument(bstrXML))
                        {
                        wprintf(L"Backup document saved successfully in %s\n", g_wszBackupDocumentFileName);
                        }
                    else
                        {
                        wprintf(L"Failed to save backup document: SaveBackupDocument returned error %d\n", GetLastError());
                        }
                    }

                // Delete the snapshot set
                if (!g_bNoDelete)
                    {
                    LONG lSnapshotsNotDeleted;
                    VSS_ID rgSnapshotsNotDeleted[10];

                    hr  = pvbc->DeleteSnapshots (id,
                                 VSS_OBJECT_SNAPSHOT_SET,
                                 false,
                                 &lSnapshotsNotDeleted,
                                 rgSnapshotsNotDeleted);

                    if (FAILED(hr))
                        wprintf(L"Deletion of Snapshots failed.  hr = 0x%08lx\n", hr);
                    }
                }
            }
        else
            {
            wprintf(L"\nBackup test is aborted since no component is selected, therefore, there are no volumes added to the snapshot set\n\n");
            }

        CHECK_SUCCESS(pvbc->FreeWriterMetadata());
        }




    // Restore is done if
    //  1. User did not ask backup-only AND
    //  2. User asked restore-only OR user asked both, and backup succeeded OR user selected ASR mode
    if (! g_bBackupOnly)
        {
        if (g_bRestoreOnly || bXMLSaved)
            {
            BOOL bXMLLoaded = FALSE;

            // Load XML string only in Restore-only case
            if (g_bRestoreOnly)
                {
                if (LoadBackupDocument(g_wszBackupDocumentFileName, bstrXML))
                    {
                    bXMLLoaded = TRUE;
                    wprintf(L"Backup document was loaded from %s\n", g_wszBackupDocumentFileName);
                    }
                else
                    {
                    wprintf(L"Failed to load backup document: LoadBackupDocument returned error %d\n", GetLastError());
                    }
                }

            // If we have a backup document from current backup or loaded successfully froma previous backup
            if (bXMLSaved || bXMLLoaded)
                {
                // Prepare for restore
                CComPtr<IVssBackupComponents> pvbcRestore;

                CHECK_SUCCESS(CreateVssBackupComponents(&pvbcRestore));
                CHECK_SUCCESS(pvbcRestore->InitializeForRestore(bstrXML));
                wprintf(L"InitializeForRestore succeeded.\n");

                // Do the restore
                if (g_bAsrRestoreMode)
                    DoAsrRestore(pvbcRestore);
                else
                    DoRestore(pvbcRestore);
                }
            }
        else
            {
            wprintf(L"\nRestore test is not done due to a failure in the preceding Backup test\n\n");
            }
        }
    }
    catch (HRESULT hr)
    {
        hrMain = hr;
    }
    

    if (bSubscribed)
        pInstance->Unsubscribe();

    if (bCreated)
        delete pInstance;

    if (FAILED(hrMain))
    wprintf(L"Failed with %08x.\n", hrMain);

    if (bCoInitializeSucceeded)
    CoUninitialize();

    return (SUCCEEDED(hrMain) ? 0 : 1);
    }

