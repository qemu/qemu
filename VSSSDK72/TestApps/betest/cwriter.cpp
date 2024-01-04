#include "stdafx.hxx"
#include "vs_idl.hxx"
#include "vswriter.h"
#include "vsbackup.h"
#include "vs_trace.hxx"
#include "compont.h"
#include <debug.h>
#include <cwriter.h>
#include <lmshare.h>
#include <lmaccess.h>
#include <time.h>


#define IID_PPV_ARG( Type, Expr ) IID_##Type, reinterpret_cast< void** >( static_cast< Type** >( Expr ) )
#define SafeQI( Type, Expr ) QueryInterface( IID_PPV_ARG( Type, Expr ) )

static BYTE x_rgbIcon[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
static unsigned x_cbIcon = 10;


static VSS_ID s_WRITERID =
    {
    0xc0577ae6, 0xd741, 0x452a,
    0x8c, 0xba, 0x99, 0xd7, 0x44, 0x00, 0x8c, 0x04
    };

static LPCWSTR s_WRITERNAME = L"BeTest Writer";

LPCWSTR GetStringFromRestoreType(_VSS_RESTORE_TYPE eRestoreType)
	{
	LPCWSTR pwszRetString = L"UNDEFINED";

	switch(eRestoreType)
		{
		case VSS_RTYPE_BY_COPY: pwszRetString = L"ByCopy"; break;
		case VSS_RTYPE_IMPORT: pwszRetString = L"Import"; break;
		case VSS_RTYPE_OTHER:pwszRetString = L"Other"; break;

		default:
		    break;
		}

	return (pwszRetString);
	}

void CTestVssWriter::Initialize()
    {
    HRESULT hr;

    CHECK_SUCCESS(CVssWriter::Initialize
                    (
                    s_WRITERID,
                    s_WRITERNAME,
                    VSS_UT_USERDATA,
                    VSS_ST_OTHER
                    ));
    }

bool STDMETHODCALLTYPE CTestVssWriter::OnIdentify(IN IVssCreateWriterMetadata *pMetadata)
    {
    HRESULT hr;

    wprintf(L"\n\n***OnIdentify***\n");
    if(m_bTestNewInterfaces)
	 return DoNewInterfacesTestIdentify(pMetadata);
    
    if (m_bRestoreTest)
        return DoRestoreTestIdentify(pMetadata);

    if (m_lWait & x_bitWaitIdentify)
        {
        wprintf(L"\nWaiting 30 seconds in OnIdentify.\n\n");
        Sleep(30000);
        }


    CHECK_SUCCESS(pMetadata->AddExcludeFiles
                        (
                        L"%systemroot%\\config",
                        L"*.tmp",
                        true
                        ));
    CHECK_SUCCESS(pMetadata->AddExcludeFiles
                        (
                        L"w:\\exclude",
                        L"*.tmp",
                        true
                        ));

    CHECK_SUCCESS(pMetadata->AddComponent
                        (
                        VSS_CT_DATABASE,
                        L"\\mydatabases",
                        L"db1",
                        L"this is my main database",
                        x_rgbIcon,
                        x_cbIcon,
                        true,
                        true,
                        true
                        ));

    CHECK_SUCCESS(pMetadata->AddDatabaseFiles
                    (
                    L"\\mydatabases",
                    L"db1",
                    L"w:\\databases",
                    L"foo1.db"
                    ));

    CHECK_SUCCESS(pMetadata->AddDatabaseFiles
                    (
                    L"\\mydatabases",
                    L"db1",
                    L"w:\\databases",
                    L"foo2.db"
                    ));


    CHECK_SUCCESS(pMetadata->AddDatabaseLogFiles
                    (
                    L"\\mydatabases",
                    L"db1",
                    L"w:\\logs",
                    L"foo.log"
                    ));

    CHECK_SUCCESS(pMetadata->SetRestoreMethod
                    (
                    VSS_RME_RESTORE_IF_NOT_THERE,
                    NULL,
                    NULL,
                    VSS_WRE_ALWAYS,
                    true
                    ));

    CHECK_SUCCESS(pMetadata->AddAlternateLocationMapping
                    (
                    L"c:\\databases",
                    L"*.db",
                    false,
                    L"w:\\databases\\restore"
                    ));

    CHECK_SUCCESS(pMetadata->AddAlternateLocationMapping
                    (
                    L"d:\\logs",
                    L"*.log",
                    false,
                    L"w:\\databases\\restore"
                    ));


    return true;
    }

bool DoPrepareBackupDatabase(IVssComponent* pComponent)
    {
    HRESULT hr;
    
    CComBSTR bstrLogicalPath;
    CComBSTR bstrComponentName;
    CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
    CHECK_SUCCESS(pComponent->GetComponentName(&bstrComponentName));

    wprintf
    (
    L"Backing up database %s\\%s.\n",
    bstrLogicalPath,
    bstrComponentName
    );
    
    WCHAR buf[100];
    wsprintf (buf, L"backupTime = %d\n", (INT) time(NULL));

    CHECK_SUCCESS(pComponent->SetBackupMetadata(buf));
    wprintf(L"\nBackupMetadata=%s\n", buf);

    CComBSTR bstrPreviousStamp;
    CHECK_NOFAIL(pComponent->GetPreviousBackupStamp(&bstrPreviousStamp));
    if (bstrPreviousStamp)
        wprintf(L"Previous stamp = %s\n", bstrPreviousStamp);

    CComBSTR bstrBackupOptions;
    CHECK_NOFAIL(pComponent->GetBackupOptions(&bstrBackupOptions));
    if (bstrBackupOptions)
        wprintf(L"Backup options = %s\n", bstrBackupOptions);

    WCHAR wszBackupStamp[32];
    swprintf(wszBackupStamp, L"B-%d-", clock());
    CHECK_SUCCESS(pComponent->SetBackupStamp(wszBackupStamp));
    wprintf(L"Backup stamp = %s\n\n", wszBackupStamp);

    return true;
    }

bool DoPrepareBackupFilegroup(IVssComponent* pComponent)
    {
    HRESULT hr;
    
    CComBSTR bstrLogicalPath;
    CComBSTR bstrComponentName;
    CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
    CHECK_SUCCESS(pComponent->GetComponentName(&bstrComponentName));

    wprintf
    (
    L"Backing up filegroup %s\\%s.\n",
    bstrLogicalPath,
    bstrComponentName
    );

    return true;
    }

bool STDMETHODCALLTYPE CTestVssWriter::OnPrepareBackup(IN IVssWriterComponents *pWriterComponents)
    {
    HRESULT hr;

    wprintf(L"\n\n***OnPrepareBackup***\n");
    if (m_bRestoreTest)
        return DoRestoreTestPrepareBackup(pWriterComponents);
    

    if (m_lWait & x_bitWaitPrepareForBackup)
        {
        wprintf(L"\nWaiting 10 seconds in PrepareForBackup.\n\n");
        Sleep(10000);
        }

    unsigned cComponents;
    LPCWSTR wszBackupType;
    switch(GetBackupType())
        {
        default:
            wszBackupType = L"undefined";
            break;

        case VSS_BT_FULL:
            wszBackupType = L"full";
            break;

        case VSS_BT_INCREMENTAL:
            wszBackupType = L"incremental";
            break;

        case VSS_BT_DIFFERENTIAL:
            wszBackupType = L"differential";
            break;

        case VSS_BT_LOG:
            wszBackupType = L"log";
            break;

        case VSS_BT_COPY:
            wszBackupType = L"copy";
            break;

        case VSS_BT_OTHER:
            wszBackupType = L"other";
            break;
        }

    wprintf(L"\n\n***OnPrepareBackup****\nBackup Type = %s\n", wszBackupType);

    wprintf
        (
        L"AreComponentsSelected = %s\n",
        AreComponentsSelected() ? L"yes" : L"no"
        );

    wprintf
        (
        L"BootableSystemStateBackup = %s\n\n",
        IsBootableSystemStateBackedUp() ? L"yes" : L"no"
        );

    CHECK_SUCCESS(pWriterComponents->GetComponentCount(&cComponents));
    for(unsigned iComponent = 0; iComponent < cComponents; iComponent++)
        {
        CComPtr<IVssComponent> pComponent;
        VSS_COMPONENT_TYPE ct;

        CHECK_SUCCESS(pWriterComponents->GetComponent(iComponent, &pComponent));
        CHECK_SUCCESS(pComponent->GetComponentType(&ct));

        wprintf(L"Current backup context is 0x%x\n", GetContext());

        if (ct == VSS_CT_DATABASE)
             DoPrepareBackupDatabase(pComponent);
	 else
	      DoPrepareBackupFilegroup(pComponent);
    	}

    
    return true;
    }

bool STDMETHODCALLTYPE CTestVssWriter::OnPrepareSnapshot()
    {
    wprintf(L"\n\n***OnPrepareSnapshot***\n");

    if (m_lWait & x_bitWaitPrepareSnapshot)
        {
        wprintf(L"\nWaiting 10 seconds in PrepareSnapshot.\n\n");
        Sleep(10000);
        }

    if (!m_bRestoreTest)
        IsPathAffected(L"e:\\foobar");

    wprintf(L"Current backup context is 0x%x\n", GetContext());

    return true;
    }


bool STDMETHODCALLTYPE CTestVssWriter::OnFreeze()
    {
    wprintf(L"\n\n***OnFreeze***\n");

    if (m_lWait & x_bitWaitFreeze)
        {
        wprintf(L"\nWaiting 10 seconds in Freeze.\n\n");
        Sleep(10000);
        }

    wprintf(L"Current backup context is 0x%x\n", GetContext());

    return true;
    }

bool STDMETHODCALLTYPE CTestVssWriter::OnThaw()
    {
    wprintf(L"\n\n***OnThaw***\n");

    if (m_lWait & x_bitWaitThaw)
        {
        wprintf(L"\nWaiting 10 seconds in PrepareThaw.\n\n");
        Sleep(10000);
        }

    wprintf(L"Current backup context is 0x%x\n", GetContext());

    return true;
    }

bool STDMETHODCALLTYPE CTestVssWriter::OnBackupComplete(IN IVssWriterComponents *pWriterComponents)
    {
    HRESULT hr;

    wprintf(L"\n\n***OnBackupComplete***\n");

    wprintf(L"Current backup context is 0x%x\n", GetContext());
    
    if (m_bRestoreTest)
        return true;

    if (m_lWait & x_bitWaitBackupComplete)
        {
        wprintf(L"\nWaiting 30 seconds in BackupComplete.\n\n");
        Sleep(30000);
        }

    unsigned cComponents;
    CHECK_SUCCESS(pWriterComponents->GetComponentCount(&cComponents));
    for(unsigned iComponent = 0; iComponent < cComponents; iComponent++)
        {
        CComPtr<IVssComponent> pComponent;
        VSS_COMPONENT_TYPE ct;
        CComBSTR bstrLogicalPath;
        CComBSTR bstrComponentName;
        bool bBackupSucceeded;

    
        CHECK_SUCCESS(pWriterComponents->GetComponent(iComponent, &pComponent));
        CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
        CHECK_SUCCESS(pComponent->GetComponentType(&ct));
        CHECK_SUCCESS(pComponent->GetComponentName(&bstrComponentName));
        CHECK_SUCCESS(pComponent->GetBackupSucceeded(&bBackupSucceeded));
        if (ct == VSS_CT_DATABASE)
        	wprintf(L"Database ");
        else
        	wprintf(L"FileGroup ");
        wprintf
            (
            L"%s\\%s backup %s.\n",
            bstrLogicalPath,
            bstrComponentName,
            bBackupSucceeded ? L"succeeded" : L"failed"
            );

        CComBSTR bstrMetadata;
        CHECK_NOFAIL(pComponent->GetBackupMetadata(&bstrMetadata));
        wprintf(L"backupMetadata=%s\n", bstrMetadata);
        }


    return true;
    }

bool STDMETHODCALLTYPE CTestVssWriter::OnBackupShutdown(IN VSS_ID SnapshotSetId)
  {
  if (!m_bTestNewInterfaces)
  	return true;

  wprintf(L"OnBackupShutdown called for snapshot-set id " WSTR_GUID_FMT L"\n", 
  	 GUID_PRINTF_ARG(SnapshotSetId));

  return true;
  }

bool STDMETHODCALLTYPE CTestVssWriter::OnPreRestore(IN IVssWriterComponents *pWriter)
    {
    if (m_bTestNewInterfaces)
    	{
    	VSS_RESTORE_TYPE type = GetRestoreType();
    	wprintf(L"\nRestore type is %s\n", GetStringFromRestoreType(type));
    	}
    
    wprintf(L"\n\n***OnPreRestore***\n");
    if (m_bRestoreTest)
        return DoRestoreTestPreRestore(pWriter);


    if (m_lWait & x_bitWaitPreRestore)
        {
        wprintf(L"\nWaiting 10 seconds in PreRestore.\n\n");
        Sleep(10000);
        }

    HRESULT hr;

    UINT cComponents;
    CHECK_SUCCESS(pWriter->GetComponentCount(&cComponents));
    for(UINT iComponent = 0; iComponent < cComponents; iComponent++)
        {
        CComPtr<IVssComponent> pComponent;

        CHECK_SUCCESS(pWriter->GetComponent(iComponent, &pComponent));

        PrintRestoreSubcomponents(pComponent);

        CComBSTR bstrBackupMetadata;
        CHECK_NOFAIL(pComponent->GetBackupMetadata(&bstrBackupMetadata));

        if (bstrBackupMetadata)
            wprintf(L"BackupMetadata=%s\n", bstrBackupMetadata);

        WCHAR buf[100];
        wsprintf (buf, L"restoreTime = %d", (INT) time(NULL));

        CHECK_SUCCESS(pComponent->SetRestoreMetadata(buf));
        wprintf(L"\nRestoreMetadata=%s\n", buf);

        CComBSTR bstrRestoreOptions;
        bool bAdditionalRestores;
        bool bSelectedForRestore;
        CHECK_SUCCESS(pComponent->GetAdditionalRestores(&bAdditionalRestores));
        CHECK_SUCCESS(pComponent->IsSelectedForRestore(&bSelectedForRestore));
        CHECK_NOFAIL(pComponent->GetRestoreOptions(&bstrRestoreOptions));
        wprintf(L"SelectedForRestore=%s\n", bSelectedForRestore ? L"Yes" : L"No");
        wprintf(L"Additional restores=%s\n", bAdditionalRestores ? L"Yes" : L"No");
        if (bstrRestoreOptions)
            wprintf(L"Restore options=%s\n", bstrRestoreOptions);

        ULONG type = clock() % 47;
        VSS_RESTORE_TARGET rt;

        if (type >= 15 && type < 30 && IsPartialFileSupportEnabled())
            rt = VSS_RT_DIRECTED;
        else if (type >= 30 && type < 40)
            rt = VSS_RT_ORIGINAL;
        else
            rt = VSS_RT_ALTERNATE;

        wprintf(L"restore target = %s\n", WszFromRestoreTarget(rt));
        CHECK_SUCCESS(pComponent->SetRestoreTarget(rt));
        if (rt == VSS_RT_DIRECTED)
            {
            CHECK_SUCCESS(pComponent->AddDirectedTarget
                                (
                                L"e:\\databases",
                                L"foo1.db",
                                L"0x8000:0x10000",
                                L"e:\\newdatabases",
                                L"copy1.db",
                                L"0x0000:0x10000"
                                ));

            CHECK_SUCCESS(pComponent->AddDirectedTarget
                                (
                                L"e:\\databases",
                                L"foo2.db",
                                L"0x4000:0x1000",
                                L"e:\\newdatabases",
                                L"copy1.db",
                                L"0x0000:0x1000"
                                ));

            PrintDirectedTargets(pComponent);

            if (m_bTestNewInterfaces)
                PrintNewTargets(pComponent);
            }

        wprintf(L"\n");

        CHECK_SUCCESS(pComponent->SetPreRestoreFailureMsg(L"PreRestore Successfully Completed."));
        }

    return true;
    }

bool STDMETHODCALLTYPE CTestVssWriter::OnPostRestore(IN IVssWriterComponents *pWriter)
    {
    wprintf(L"\n\n***OnPostRestore***\n");
    if (m_bRestoreTest)
        return DoRestoreTestPostRestore(pWriter);

    if (m_lWait & x_bitWaitPostRestore)
        {
        wprintf(L"\nWaiting 10 seconds in PostRestore.\n\n");
        Sleep(10000);
        }

    HRESULT hr;

    UINT cComponents;
    CHECK_SUCCESS(pWriter->GetComponentCount(&cComponents));
    for(UINT iComponent = 0; iComponent < cComponents; iComponent++)
        {
        CComPtr<IVssComponent> pComponent;

        CHECK_SUCCESS(pWriter->GetComponent(iComponent, &pComponent));

        VSS_RESTORE_TARGET rt;
        CHECK_SUCCESS(pComponent->GetRestoreTarget(&rt));
        wprintf(L"RestoreTarget = %s\n", WszFromRestoreTarget(rt));
        if (rt == VSS_RT_DIRECTED)
            PrintDirectedTargets(pComponent);

        VSS_FILE_RESTORE_STATUS rs;
        CHECK_SUCCESS(pComponent->GetFileRestoreStatus(&rs));
        wprintf(L"RestoreStatus = %s\n", WszFromFileRestoreStatus(rs));

        CComBSTR bstrRestoreMetadata;
        CComBSTR bstrBackupMetadata;
        CHECK_NOFAIL(pComponent->GetRestoreMetadata(&bstrRestoreMetadata));
        CHECK_NOFAIL(pComponent->GetBackupMetadata(&bstrBackupMetadata));
        if (bstrRestoreMetadata)
            wprintf(L"RestoreMetadata=%s\n", bstrRestoreMetadata);

        if (bstrBackupMetadata)
            wprintf(L"BackupMetadata=%s\n", bstrBackupMetadata);

        wprintf(L"\n");

        CHECK_SUCCESS(pComponent->SetPostRestoreFailureMsg(L"PostRestore Successfully Completed."));
        }

    return true;
    }

bool STDMETHODCALLTYPE CTestVssWriter::OnAbort()
    {
    wprintf(L"\n\n***OnAbort***\n\n");
    if (m_lWait & x_bitWaitAbort)
        {
        wprintf(L"\nWaiting 10 seconds in Abort.\n\n");
        Sleep(10000);
        }

    return true;
    }

bool STDMETHODCALLTYPE CTestVssWriter::OnPostSnapshot
    (
    IN IVssWriterComponents *pWriter
    )
    {
    wprintf(L"\n\n***OnPostSnapshot***\n\n");
    if (m_bRestoreTest)
        return true;

    if (m_lWait & x_bitWaitPostSnapshot)
        {
        wprintf(L"\nWaiting 10 seconds in PostSnapshot.\n\n");
        Sleep(10000);
        }

    HRESULT hr;



    if (IsPartialFileSupportEnabled() &&
        GetBackupType() == VSS_BT_DIFFERENTIAL)
        {
        UINT cComponents;

        CHECK_SUCCESS(pWriter->GetComponentCount(&cComponents));

        for(UINT iComponent = 0; iComponent < cComponents; iComponent++)
            {
            CComPtr<IVssComponent> pComponent;

            CHECK_SUCCESS(pWriter->GetComponent(iComponent, &pComponent));
            VSS_COMPONENT_TYPE ct;
            CHECK_SUCCESS(pComponent->GetComponentType(&ct));
            if (ct == VSS_CT_DATABASE)
                {
                CHECK_SUCCESS(pComponent->AddPartialFile
                                    (
                                    L"e:\\databases",
                                    L"foo1.db",
                                    L"0x8000:0x10000, 0x100000:0x2000",
                                    L"Length=0x200000"
                                    ));

                CHECK_SUCCESS(pComponent->AddPartialFile
                                (
                                L"e:\\databases",
                                L"foo2.db",
                                L"0x4000:0x1000",
                                L"Length=0x100000"
                                ));
                }

            PrintPartialFiles(pComponent);

	     if(m_bTestNewInterfaces)
	     	{
	     	::Sleep(1000);
	     	FILETIME time;
	     	CHECK_SUCCESS(CoFileTimeNow(&time));

	     	// add bogus files as differenced files
		CHECK_SUCCESS(pComponent->AddDifferencedFilesByLastModifyTime
			         (
			         L"C:\\",
			         L"Foo",
			         false,
			         time
			         ));

		CHECK_SUCCESS(pComponent->AddDifferencedFilesByLastModifyLSN
			         (
			         L"C:\\",
			         L"Bar",
			         true,
			         L"MYLSNFORMAT"
			         ));

	       if (GetSnapshotDeviceName(NULL, NULL) != E_NOTIMPL)
       	    Error(1, L"GetSnapshotDeviceName should return E_NOTIMPL");
	     }
            }

        }

    return true;
    }

void CTestVssWriter::CreateDirectoryName(LPWSTR buf)
    {
    UINT cwc = ExpandEnvironmentStrings(L"%SystemDrive%", buf, 1024);
    if (cwc == 0)
        {
        DWORD dwErr = GetLastError();
        Error(HRESULT_FROM_WIN32(dwErr), L"ExpandEnvironmentStrings failed with error %d.\n", dwErr);
        }

    wcscat(buf, L"\\BETESTWRITERFILES");
    }


bool CTestVssWriter::DoRestoreTestIdentify(IVssCreateWriterMetadata *pMetadata)
    {
    HRESULT hr;

    WCHAR buf[1024];
    CreateDirectoryName(buf);
    CreateDirectory(buf, NULL);

    CHECK_SUCCESS(pMetadata->SetRestoreMethod
                                (
                                (m_lRestoreTestOptions & x_RestoreTestOptions_RestoreIfNotThere) ?
                                    VSS_RME_RESTORE_IF_NOT_THERE : VSS_RME_RESTORE_IF_CAN_REPLACE,
                                NULL,
                                NULL,
                                VSS_WRE_ALWAYS,
                                false
                                ));

    DoAddComponent(pMetadata, L"a", buf, NULL, L"*.a", L"ALTA", true, true, 0);
    DoAddComponent(pMetadata, L"b", buf, L"b", L"*", L"ALTB", false, true, 0);
    DoAddComponent(pMetadata, L"c", buf, NULL, L"c.*", L"ALTC", true, true, 0);

    
    return true;
    }

bool CTestVssWriter::DoNewInterfacesTestIdentify(IVssCreateWriterMetadata* pMetadata)
    {
    HRESULT hr;

    WCHAR buf[1024];
    CreateDirectoryName(buf);
    CreateDirectory(buf, NULL);
	
    CHECK_SUCCESS(pMetadata->SetRestoreMethod
                                (
                                VSS_RME_RESTORE_IF_NOT_THERE,
                                NULL,
                                NULL,
                                VSS_WRE_ALWAYS,
                                false
                                ));

    CHECK_SUCCESS(pMetadata->SetBackupSchema
		 (VSS_BS_DIFFERENTIAL | VSS_BS_INCREMENTAL | VSS_BS_LOG |
		 VSS_BS_COPY | VSS_BS_TIMESTAMPED | VSS_BS_TIMESTAMPED |
		 VSS_BS_LAST_MODIFY | VSS_BS_LSN | VSS_BS_WRITER_SUPPORTS_NEW_TARGET
		 ));
				
    DoAddComponent(pMetadata, L"a", buf, NULL, L"*.a", L"ALTA", true, true, 0);
    DoAddComponent(pMetadata, L"b", buf, L"b", L"*", L"ALTB", false, true, 0);
    DoAddComponent(pMetadata, L"c", buf, NULL, L"c.*", L"ALTC", true, true, VSS_CF_BACKUP_RECOVERY);

   CHECK_SUCCESS(pMetadata->AddComponentDependency(NULL, L"a", s_WRITERID, NULL, L"b"));
   CHECK_SUCCESS(pMetadata->AddComponentDependency(NULL, L"c", s_WRITERID, NULL, L"a"));

   CHECK_SUCCESS(pMetadata->AddComponent
   	                        (
   	                        VSS_CT_DATABASE,
   	                        NULL,
   	                        L"db1",
   	                        NULL,
   	                        NULL,
   	                        0,
   	                        true,
   	                        true,
   	                        true,
   	                        true
   	                        ));
      CHECK_SUCCESS(pMetadata->AddComponent
   	                        (
   	                        VSS_CT_FILEGROUP,
   	                        NULL,
   	                        L"db2",
   	                        NULL,
   	                        NULL,
   	                        0,
   	                        true,
   	                        true,
   	                        true,
   	                        true
   	                        ));

   CHECK_SUCCESS(pMetadata->AddDatabaseFiles
   				   (
   				   NULL,
   				   L"db1",
 				   buf,
 				   L"*.db1",
 				   VSS_FSBT_FULL_BACKUP_REQUIRED
 				   ));
   CHECK_SUCCESS(pMetadata->AddDatabaseLogFiles
   				   (
   				   NULL,
   				   L"db1",
 				   buf,
 				   L"*.db2",
 				   VSS_FSBT_DIFFERENTIAL_BACKUP_REQUIRED
 				   ));
   CHECK_SUCCESS(pMetadata->AddFilesToFileGroup
      				   (
      				   NULL,
      				   L"db2",
    				   buf,
    				   L"*.db3",
    				   true,
    				   NULL,
    				   VSS_FSBT_INCREMENTAL_BACKUP_REQUIRED
    				   ));
   CHECK_SUCCESS(pMetadata->AddDatabaseFiles
      				   (
      				   NULL,
      				   L"db1",
    				   buf,
    				   L"*.db4",
    				   VSS_FSBT_LOG_BACKUP_REQUIRED
    				   ));
   CHECK_SUCCESS(pMetadata->AddDatabaseLogFiles
      				   (
      				   NULL,
      				   L"db1",
    				   buf,
    				   L"*.db5",
    				   VSS_FSBT_FULL_SNAPSHOT_REQUIRED
    				   ));
   CHECK_SUCCESS(pMetadata->AddFilesToFileGroup
      				   (
      				   NULL,
      				   L"db2",
    				   buf,
    				   L"*.db6",
    				   true,
    				   NULL,
    				   VSS_FSBT_DIFFERENTIAL_SNAPSHOT_REQUIRED
    				   ));
   CHECK_SUCCESS(pMetadata->AddDatabaseFiles
      				   (
      				   NULL,
      				   L"db1",
    				   buf,
    				   L"*.db7",
    				   VSS_FSBT_INCREMENTAL_SNAPSHOT_REQUIRED
    				   ));
   CHECK_SUCCESS(pMetadata->AddDatabaseLogFiles
      				   (
      				   NULL,
      				   L"db1",
    				   buf,
    				   L"*.db8",
    				   VSS_FSBT_LOG_SNAPSHOT_REQUIRED
    				   ));
   CHECK_SUCCESS(pMetadata->AddFilesToFileGroup
      				   (
      				   NULL,
      				   L"db2",
    				   buf,
    				   L"*.db9",
    				   true,
    				   NULL,
    				   VSS_FSBT_ALL_BACKUP_REQUIRED
    				   ));
   
   CHECK_SUCCESS(pMetadata->AddDatabaseFiles
      				   (
      				   NULL,
      				   L"db1",
    				   buf,
    				   L"*.db10",
    				   VSS_FSBT_ALL_SNAPSHOT_REQUIRED
    				   ));

    return true;   
    }

void CTestVssWriter::DoAddComponent
    (
    IVssCreateWriterMetadata *pMetadata,
    LPCWSTR wszComponentName,
    LPCWSTR wszRootDirectory,
    LPCWSTR wszSubdirectory,
    LPCWSTR wszFilespec,
    LPCWSTR wszAlternateDirectory,
    bool bSelectable,
    bool bSelectableForRestore,
    LONG lFlags
    )
    {
    HRESULT hr;
    CComBSTR bstrAlternate;
    bstrAlternate.Append(wszRootDirectory);
    bstrAlternate.Append(L"\\");
    bstrAlternate.Append(wszAlternateDirectory);

    CHECK_SUCCESS(pMetadata->AddComponent
                                (
                                VSS_CT_FILEGROUP,
                                NULL,
                                wszComponentName,
                                NULL,
                                NULL,
                                0,
                                true,
                                true,
                                bSelectable,
                                bSelectableForRestore,
                                lFlags
                                ));

    if (wszSubdirectory == NULL)
        {
        CHECK_SUCCESS(pMetadata->AddFilesToFileGroup
                                    (
                                    NULL,
                                    wszComponentName,
                                    wszRootDirectory,
                                    wszFilespec,
                                    false,
                                    NULL
                                    ));

        CHECK_SUCCESS(pMetadata->AddAlternateLocationMapping
                                        (
                                        wszRootDirectory,
                                        wszFilespec,
                                        false,
                                        bstrAlternate
                                        ));
        }
    else
        {
        CComBSTR bstr;
        bstr.Append(wszRootDirectory);
        bstr.Append(L"\\");
        bstr.Append(wszSubdirectory);
        CHECK_SUCCESS(pMetadata->AddFilesToFileGroup
                                    (
                                    NULL,
                                    wszComponentName,
                                    bstr,
                                    wszFilespec,
                                    true,
                                    NULL
                                    ));

        CHECK_SUCCESS(pMetadata->AddAlternateLocationMapping
                                    (
                                    bstr,
                                    wszFilespec,
                                    true,
                                    bstrAlternate
                                    ));
        }
    }



bool CTestVssWriter::DoRestoreTestPrepareBackup(IVssWriterComponents *pWriterComponents)
    {
    WCHAR buf[1024];
    HRESULT hr;
    unsigned cComponents;

    CreateDirectoryName(buf);
    CHECK_SUCCESS(pWriterComponents->GetComponentCount(&cComponents));
    for(unsigned iComponent = 0; iComponent < cComponents; iComponent++)
        {
        CComPtr<IVssComponent> pComponent;
        VSS_COMPONENT_TYPE ct;
        CComBSTR bstrLogicalPath;
        CComBSTR bstrComponentName;

        CHECK_SUCCESS(pWriterComponents->GetComponent(iComponent, &pComponent));
        CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
        CHECK_SUCCESS(pComponent->GetComponentType(&ct));
        CHECK_SUCCESS(pComponent->GetComponentName(&bstrComponentName));
        if (ct == VSS_CT_FILEGROUP && !bstrLogicalPath && wcslen(bstrComponentName) == 1)
            {
            if (bstrComponentName[0] == L'a')
                CreateComponentFilesA(buf, false);
            else if (bstrComponentName[0] == L'b')
                CreateComponentFilesB(buf, false);
            else if (bstrComponentName[0] == L'c')
                CreateComponentFilesC(buf, false);
            }
        }

    return true;
    }




void CTestVssWriter::CreateComponentFilesA(LPCWSTR buf, bool bKeepOpen)
    {
    DoCreateFile(buf, L"foo.a", 100, bKeepOpen);
    DoCreateFile(buf, L"bar.a", 1000, bKeepOpen);
    DoCreateFile(buf, L"xxx.a", 10000, bKeepOpen);
    }

void CTestVssWriter::VerifyComponentFilesA(LPCWSTR buf)
    {
    CComBSTR bstr;
    bstr.Append(buf);
    bstr.Append(L"\\ALTA");
    DoVerifyFile(bstr, L"foo.a", 100);
    DoVerifyFile(bstr, L"bar.a", 1000);
    DoVerifyFile(bstr, L"xxx.a", 10000);
    wprintf(L"Component a is verified.\n");
    }


void CTestVssWriter::CreateComponentFilesB(LPCWSTR buf, bool bKeepOpen)
    {
    CComBSTR bstr;
    bstr.Append(buf);
    bstr.Append(L"\\b");

    CreateDirectory(bstr, NULL);

    DoCreateFile(bstr, L"a.a", 1000, bKeepOpen);
    DoCreateFile(bstr, L"b.b", 1000, bKeepOpen);

    bstr.Append(L"\\a");
    CreateDirectory(bstr, NULL);
    DoCreateFile(bstr, L"a.a", 10000, bKeepOpen);
    DoCreateFile(bstr, L"b.b", 10000, bKeepOpen);

    bstr.Append(L"\\b");
    CreateDirectory(bstr, NULL);
    DoCreateFile(bstr, L"a.a", 100000, bKeepOpen);
    DoCreateFile(bstr, L"b.b", 100000, bKeepOpen);

    bstr[wcslen(bstr) - 1] = L'c';
    CreateDirectory(bstr, NULL);
    DoCreateFile(bstr, L"a.a", 10, bKeepOpen);
    DoCreateFile(bstr, L"b.b", 10, bKeepOpen);
    }

void CTestVssWriter::VerifyComponentFilesB(LPCWSTR buf)
    {
    CComBSTR bstr;
    bstr.Append(buf);
    bstr.Append(L"\\ALTB");

    DoVerifyFile(bstr, L"a.a", 1000);
    DoVerifyFile(bstr, L"b.b", 1000);

    bstr.Append(L"\\a");
    DoVerifyFile(bstr, L"a.a", 10000);
    DoVerifyFile(bstr, L"b.b", 10000);

    bstr.Append(L"\\b");
    CreateDirectory(bstr, NULL);
    DoVerifyFile(bstr, L"a.a", 100000);
    DoVerifyFile(bstr, L"b.b", 100000);

    bstr[wcslen(bstr) - 1] = L'c';
    DoVerifyFile(bstr, L"a.a", 10);
    DoVerifyFile(bstr, L"b.b", 10);
    wprintf(L"Component b is verified.\n");
    }


void CTestVssWriter::CreateComponentFilesC(LPCWSTR buf, bool bKeepOpen)
    {
    DoCreateFile(buf, L"c.x1", 100, bKeepOpen);
    DoCreateFile(buf, L"c.x2", 1000, bKeepOpen);
    DoCreateFile(buf, L"c.x3", 10000, bKeepOpen);
    }

void CTestVssWriter::VerifyComponentFilesC(LPCWSTR buf)
    {
    CComBSTR bstr;
    bstr.Append(buf);
    bstr.Append(L"\\ALTC");

    DoVerifyFile(bstr, L"c.x1", 100);
    DoVerifyFile(bstr, L"c.x2", 1000);
    DoVerifyFile(bstr, L"c.x3", 10000);
    wprintf(L"Component c is verified.\n");
    }

void CTestVssWriter::DoCreateFile
    (
    LPCWSTR wszPath,
    LPCWSTR wszFilename,
    DWORD length,
    bool bKeepOpen
    )
    {
    CComBSTR bstr;
    bstr.Append(wszPath);
    bstr.Append(L"\\");
    bstr.Append(wszFilename);
    if (bKeepOpen && m_chOpen == m_chOpenMax)
        {
        HANDLE *rghNew = new HANDLE[m_chOpen + 4];
        memcpy(rghNew, m_rghOpen, m_chOpen * sizeof(HANDLE));
        delete m_rghOpen;
        m_rghOpen = rghNew;
        m_chOpenMax = m_chOpen + 4;
        }

    BYTE *buf = new BYTE[length];
    if (buf == NULL)
        Error(E_OUTOFMEMORY, L"Out of memory.\n");

    UINT seed = length + wszFilename[0];
    for(UINT i = 0; i < length; i++, seed++)
        buf[i] = (BYTE) (seed & 0xff);


    HANDLE hFile = CreateFile
                        (
                        bstr,
                        GENERIC_READ|GENERIC_WRITE,
                        FILE_SHARE_READ,
                        NULL,
                        CREATE_ALWAYS,
                        0,
                        NULL
                        );

    if (hFile == INVALID_HANDLE_VALUE)
        {
        DWORD dwErr = GetLastError();
        delete buf;
        Error(HRESULT_FROM_WIN32(dwErr), L"CreateFile failed due to error %d.\n", dwErr);
        }

    DWORD dwWritten;
    if (!WriteFile(hFile, buf, length, &dwWritten, NULL) ||
        dwWritten != length)
        {
        DWORD dwErr = GetLastError();
        delete buf;
        CloseHandle(hFile);
        Error(HRESULT_FROM_WIN32(dwErr), L"Write file failed due to error %d.\n", dwErr);
        }

    delete buf;
    if (bKeepOpen)
        m_rghOpen[m_chOpen++] = hFile;
    else
        CloseHandle(hFile);
    }

void CTestVssWriter::DoVerifyFile
    (
    LPCWSTR wszPath,
    LPCWSTR wszFilename,
    DWORD length
    )
    {
    CComBSTR bstr;
    bstr.Append(wszPath);
    bstr.Append(L"\\");
    bstr.Append(wszFilename);

    HANDLE hFile = CreateFile
                        (
                        bstr,
                        GENERIC_READ|GENERIC_WRITE,
                        FILE_SHARE_READ,
                        NULL,
                        OPEN_EXISTING,
                        0,
                        NULL
                        );

    if (hFile == INVALID_HANDLE_VALUE)
        {
        DWORD dwErr = GetLastError();
        if (dwErr == ERROR_FILE_NOT_FOUND ||
            dwErr == ERROR_PATH_NOT_FOUND)
            Error(E_UNEXPECTED, L"%s was not restored.\n", bstr);
        }

    DWORD dwSize = GetFileSize(hFile, NULL);
    if (dwSize == 0xffffffff)
        {
        DWORD dwErr = GetLastError();
        CloseHandle(hFile);
        Error(HRESULT_FROM_WIN32(dwErr), L"GetFileSize failed due to error %d.\n", dwErr);
        }

    if (dwSize != length)
        {
        CloseHandle(hFile);
        Error(E_UNEXPECTED, L"Failed to restore file %s correctly.\n", bstr);
        }


    BYTE *buf = new BYTE[length];
    if (buf == NULL)
        {
        CloseHandle(hFile);
        Error(E_OUTOFMEMORY, L"Out of memory.\n");
        }

    DWORD dwRead;
    if (!ReadFile(hFile, buf, length, &dwRead, NULL))
        {
        DWORD dwErr = GetLastError();
        delete buf;
        CloseHandle(hFile);
        Error(HRESULT_FROM_WIN32(dwErr), L"Write file failed due to error %d.\n", dwErr);
        }

    CloseHandle(hFile);

    UINT seed = length + wszFilename[0];
    for(UINT i = 0; i < length; i++, seed++)
        {
        if (buf[i] != (BYTE) (seed & 0xff))
            {
            delete buf;
            Error(E_UNEXPECTED, L"Failed to restore file %s correctly.\n", bstr);
            }
        }

    delete buf;
    }


bool CTestVssWriter::DoRestoreTestPreRestore(IVssWriterComponents *pWriterComponents)
    {
    WCHAR buf[1024];
    HRESULT hr;
    unsigned cComponents;
    bool bKeepOpen = (m_lRestoreTestOptions & x_RestoreTestOptions_RestoreIfNotThere) ? false : true;

    CreateDirectoryName(buf);
    CHECK_SUCCESS(pWriterComponents->GetComponentCount(&cComponents));
    for(unsigned iComponent = 0; iComponent < cComponents; iComponent++)
        {
        CComPtr<IVssComponent> pComponent;
        VSS_COMPONENT_TYPE ct;
        CComBSTR bstrLogicalPath;
        CComBSTR bstrComponentName;
        bool bRestore;

        CHECK_SUCCESS(pWriterComponents->GetComponent(iComponent, &pComponent));
        CHECK_SUCCESS(pComponent->IsSelectedForRestore(&bRestore));
        if (!bRestore)
            continue;

        CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
        CHECK_SUCCESS(pComponent->GetComponentType(&ct));
        CHECK_SUCCESS(pComponent->GetComponentName(&bstrComponentName));
        if (ct == VSS_CT_FILEGROUP && !bstrLogicalPath && wcslen(bstrComponentName) == 1)
            {
            if (bstrComponentName[0] == L'a')
                CreateComponentFilesA(buf, bKeepOpen);
            else if (bstrComponentName[0] == L'b')
                CreateComponentFilesB(buf, bKeepOpen);
            else if (bstrComponentName[0] == L'c')
                CreateComponentFilesC(buf, bKeepOpen);
            }

        if (m_bTestNewInterfaces)
        	PrintNewTargets(pComponent);
        }

    return true;
    }


bool CTestVssWriter::DoRestoreTestPostRestore(IVssWriterComponents *pWriterComponents)
    {
    WCHAR buf[1024];
    HRESULT hr;
    unsigned cComponents;

    CreateDirectoryName(buf);
    CHECK_SUCCESS(pWriterComponents->GetComponentCount(&cComponents));
    for(unsigned iComponent = 0; iComponent < cComponents; iComponent++)
        {
        CComPtr<IVssComponent> pComponent;
        VSS_COMPONENT_TYPE ct;
        CComBSTR bstrLogicalPath;
        CComBSTR bstrComponentName;
        bool bRestore;

        CHECK_SUCCESS(pWriterComponents->GetComponent(iComponent, &pComponent));
        CHECK_SUCCESS(pComponent->IsSelectedForRestore(&bRestore));

        if (!bRestore)
            continue;

        CHECK_NOFAIL(pComponent->GetLogicalPath(&bstrLogicalPath));
        CHECK_SUCCESS(pComponent->GetComponentType(&ct));
        CHECK_SUCCESS(pComponent->GetComponentName(&bstrComponentName));
        if (ct == VSS_CT_FILEGROUP && !bstrLogicalPath && wcslen(bstrComponentName) == 1)
            {
            if (bstrComponentName[0] == L'a')
                VerifyComponentFilesA(buf);
            else if (bstrComponentName[0] == L'b')
                VerifyComponentFilesB(buf);
            else if (bstrComponentName[0] == L'c')
                VerifyComponentFilesC(buf);
            }
        }

    for(UINT ih = 0; ih < m_chOpen; ih++)
        CloseHandle(m_rghOpen[ih]);

    delete m_rghOpen;
    m_chOpen = 0;
    m_chOpenMax = 0;
    m_rghOpen = NULL;
    return true;
    }


