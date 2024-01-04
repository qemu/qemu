
#include "stdafx.hxx"
#include "vss.h"
#include "vswriter.h"
#include "vsbackup.h"
#include <debug.h>
#include <cwriter.h>
#include <lmshare.h>
#include <lmaccess.h>


LPCWSTR GetStringFromFailureType(HRESULT hrStatus)
{
    LPCWSTR pwszFailureType = L"";

    switch (hrStatus)
	{
	case VSS_E_WRITERERROR_INCONSISTENTSNAPSHOT:        pwszFailureType = L"VSS_E_WRITERERROR_INCONSISTENTSNAPSHOT";    break;
	case VSS_E_WRITERERROR_OUTOFRESOURCES:              pwszFailureType = L"VSS_E_WRITERERROR_OUTOFRESOURCES";          break;
	case VSS_E_WRITERERROR_TIMEOUT:                     pwszFailureType = L"VSS_E_WRITERERROR_TIMEOUT";                 break;
	case VSS_E_WRITERERROR_NONRETRYABLE:                pwszFailureType = L"VSS_E_WRITERERROR_NONRETRYABLE";            break;
	case VSS_E_WRITERERROR_RETRYABLE:                   pwszFailureType = L"VSS_E_WRITERERROR_RETRYABLE";               break;
	case VSS_E_BAD_STATE:                               pwszFailureType = L"VSS_E_BAD_STATE";                           break;
	case VSS_E_PROVIDER_ALREADY_REGISTERED:             pwszFailureType = L"VSS_E_PROVIDER_ALREADY_REGISTERED";         break;
	case VSS_E_PROVIDER_NOT_REGISTERED:                 pwszFailureType = L"VSS_E_PROVIDER_NOT_REGISTERED";             break;
	case VSS_E_PROVIDER_VETO:                           pwszFailureType = L"VSS_E_PROVIDER_VETO";                       break;
	case VSS_E_PROVIDER_IN_USE:				            pwszFailureType = L"VSS_E_PROVIDER_IN_USE";                     break;
	case VSS_E_OBJECT_NOT_FOUND:						pwszFailureType = L"VSS_E_OBJECT_NOT_FOUND";                    break;						
	case VSS_S_ASYNC_PENDING:							pwszFailureType = L"VSS_S_ASYNC_PENDING";                       break;
	case VSS_S_ASYNC_FINISHED:						    pwszFailureType = L"VSS_S_ASYNC_FINISHED";                      break;
	case VSS_S_ASYNC_CANCELLED:						    pwszFailureType = L"VSS_S_ASYNC_CANCELLED";                     break;
	case VSS_E_VOLUME_NOT_SUPPORTED:					pwszFailureType = L"VSS_E_VOLUME_NOT_SUPPORTED";                break;
	case VSS_E_VOLUME_NOT_SUPPORTED_BY_PROVIDER:		pwszFailureType = L"VSS_E_VOLUME_NOT_SUPPORTED_BY_PROVIDER";    break;
	case VSS_E_OBJECT_ALREADY_EXISTS:					pwszFailureType = L"VSS_E_OBJECT_ALREADY_EXISTS";               break;
	case VSS_E_UNEXPECTED_PROVIDER_ERROR:				pwszFailureType = L"VSS_E_UNEXPECTED_PROVIDER_ERROR";           break;
	case VSS_E_CORRUPT_XML_DOCUMENT:				    pwszFailureType = L"VSS_E_CORRUPT_XML_DOCUMENT";                break;
	case VSS_E_INVALID_XML_DOCUMENT:					pwszFailureType = L"VSS_E_INVALID_XML_DOCUMENT";                break;
	case VSS_E_MAXIMUM_NUMBER_OF_VOLUMES_REACHED:       pwszFailureType = L"VSS_E_MAXIMUM_NUMBER_OF_VOLUMES_REACHED";   break;
	case VSS_E_FLUSH_WRITES_TIMEOUT:                    pwszFailureType = L"VSS_E_FLUSH_WRITES_TIMEOUT";                break;
	case VSS_E_HOLD_WRITES_TIMEOUT:                     pwszFailureType = L"VSS_E_HOLD_WRITES_TIMEOUT";                 break;
	case VSS_E_UNEXPECTED_WRITER_ERROR:                 pwszFailureType = L"VSS_E_UNEXPECTED_WRITER_ERROR";             break;
	case VSS_E_SNAPSHOT_SET_IN_PROGRESS:                pwszFailureType = L"VSS_E_SNAPSHOT_SET_IN_PROGRESS";            break;
	case VSS_E_MAXIMUM_NUMBER_OF_SNAPSHOTS_REACHED:     pwszFailureType = L"VSS_E_MAXIMUM_NUMBER_OF_SNAPSHOTS_REACHED"; break;
	case VSS_E_WRITER_INFRASTRUCTURE:	 		        pwszFailureType = L"VSS_E_WRITER_INFRASTRUCTURE";               break;
	case VSS_E_WRITER_NOT_RESPONDING:			        pwszFailureType = L"VSS_E_WRITER_NOT_RESPONDING";               break;
    case VSS_E_WRITER_ALREADY_SUBSCRIBED:		        pwszFailureType = L"VSS_E_WRITER_ALREADY_SUBSCRIBED";           break;
	
	case NOERROR:
	default:
	    break;
	}

    return (pwszFailureType);
}


// This function displays the formatted message at the console and throws
// The passed return code will be returned by vsreq.exe
void Error(
    IN  INT nReturnCode,
    IN  const WCHAR* pwszMsgFormat,
    IN  ...
    )
{
    va_list marker;
    va_start( marker, pwszMsgFormat );
    vwprintf( pwszMsgFormat, marker );
    va_end( marker );

	BS_ASSERT(FALSE);
    // throw that return code.
    throw(nReturnCode);
}

// convert VSS_RESTORE_TARGET to string
LPCWSTR WszFromRestoreTarget
	(
	IN VSS_RESTORE_TARGET rt
	)
	{
	switch(rt)
		{
        default:
			return L"Undefined";

        case VSS_RT_ORIGINAL:
			return L"Original";

		case VSS_RT_ALTERNATE:
            return L"Alternate";

        case VSS_RT_DIRECTED:
			return L"Directed";
        }
	}

// convert VSS_FILE_RESTORE_STATUS to string
LPCWSTR WszFromFileRestoreStatus
	(
	IN VSS_FILE_RESTORE_STATUS rs
	)
	{
	switch(rs)
		{
		default:
			return L"Undefined";

        case VSS_RS_NONE:
			return L"None";

		case VSS_RS_ALL:
            return L"All";

		case VSS_RS_FAILED:
            return L"Failed";
        }
	}

void PrintPartialFiles(IVssComponent *pComponent)
	{
	UINT cPartialFiles;
	HRESULT hr;

	CHECK_SUCCESS(pComponent->GetPartialFileCount(&cPartialFiles));
	if (cPartialFiles > 0)
		wprintf(L"\n%d Partial Files:\n\n", cPartialFiles);

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

        wprintf(L"Path=%s, Name=%s\nRanges=%s\nMetadata=%s\n",
				bstrPath, bstrFilename, bstrRanges, bstrMetadata);


        WCHAR wszPathName[MAX_PATH];
        WCHAR wszVolumeName[MAX_PATH];
        if (bstrPath[bstrPath.Length() - 1] != L'\\')
            bstrPath += L"\\";
        
        if (!::GetVolumePathName(bstrPath, wszPathName, MAX_PATH))
            CHECK_SUCCESS(HRESULT_FROM_WIN32(::GetLastError()));
        if (!::GetVolumeNameForVolumeMountPoint(wszPathName, wszVolumeName, MAX_PATH))
            CHECK_SUCCESS(HRESULT_FROM_WIN32(::GetLastError()));

        wprintf(L"resident on volume %s\n\n", wszVolumeName);

        }
	}

void PrintDifferencedFiles(IVssComponent* pComponent)
	{
	UINT cDifferencedFiles;
	HRESULT hr;

	CHECK_SUCCESS(pComponent->GetDifferencedFilesCount(&cDifferencedFiles));
	if (cDifferencedFiles > 0)
		wprintf(L"\n%d Differenced Files:\n\n", cDifferencedFiles);

	for(UINT iDiff = 0; iDiff < cDifferencedFiles; iDiff++)
		{
		CComBSTR bstrPath;
		CComBSTR bstrFilename;
		BOOL bRecursive;
		CComBSTR bstrLSN;
		FILETIME ftLastModify;

		CHECK_SUCCESS(pComponent->GetDifferencedFile
							(
							iDiff,
							&bstrPath,
							&bstrFilename,
							&bRecursive,
							&bstrLSN,
							&ftLastModify
							));

        wprintf(L"Path=%s, Name=%s\nRecursive=%s,LSN=%s\nLastModifyHigh=%x\nLastModifyLow=%x\n\n",
				bstrPath, 
				bstrFilename, 
				(bRecursive) ? L"yes" : L"no",
				bstrLSN, 
				ftLastModify.dwHighDateTime,
				ftLastModify.dwLowDateTime);

        }
	
	}

void PrintNewTargets(IVssComponent *pComponent)
    {
    	UINT cTarget;
	HRESULT hr;

	CHECK_SUCCESS(pComponent->GetNewTargetCount(&cTarget));
	if (cTarget > 0)
		wprintf(L"\n%d New Targets:\n\n", cTarget);

	for(UINT iTarget = 0; iTarget < cTarget; iTarget++)
		{
		CComPtr<IVssWMFiledesc> pFiledesc;

		CHECK_SUCCESS(pComponent->GetNewTarget
			(
			iTarget,
			&pFiledesc
			));

		CComBSTR bstrSourcePath;
		CComBSTR bstrFilespec;
		CComBSTR bstrAlt;
		bool bRecursive = false;


		CHECK_SUCCESS(pFiledesc->GetPath(&bstrSourcePath));
		CHECK_SUCCESS(pFiledesc->GetFilespec(&bstrFilespec));
		CHECK_SUCCESS(pFiledesc->GetRecursive(&bRecursive));
		CHECK_SUCCESS(pFiledesc->GetAlternateLocation(&bstrAlt));

		wprintf(L"path = %s\nfilespec = %s\nrecursive = %s\nalternateLocation=%s\n",
			     bstrSourcePath,
			     bstrFilespec,
			     (bRecursive) ?  L"yes" : L"no",
			     bstrAlt
			     );		
        }
}

void PrintDirectedTargets(IVssComponent *pComponent)
	{
	UINT cTarget;
	HRESULT hr;

	CHECK_SUCCESS(pComponent->GetDirectedTargetCount(&cTarget));
	if (cTarget > 0)
		wprintf(L"\n%d Directed Targets:\n\n", cTarget);

	for(UINT iTarget = 0; iTarget < cTarget; iTarget++)
		{
		CComBSTR bstrSourcePath;
		CComBSTR bstrSourceFilespec;
		CComBSTR bstrSourceRanges;
		CComBSTR bstrTargetPath;
		CComBSTR bstrTargetFilespec;
		CComBSTR bstrTargetRanges;


		CHECK_SUCCESS(pComponent->GetDirectedTarget
			(
			iTarget,
			&bstrSourcePath,
			&bstrSourceFilespec,
			&bstrSourceRanges,
			&bstrTargetPath,
			&bstrTargetFilespec,
			&bstrTargetRanges
			));

		wprintf(L"Source Path=%s, Name=%s\nRanges=%s\nTarget Path=%s, Name=%s\nRanges=%s\n",
				bstrSourcePath,
				bstrSourceFilespec,
				bstrSourceRanges,
				bstrTargetPath,
				bstrTargetFilespec,
				bstrTargetRanges);
        }
	}


void PrintRestoreSubcomponents(IVssComponent *pComponent)
	{
	UINT cSub;
	HRESULT hr;

	CHECK_SUCCESS(pComponent->GetRestoreSubcomponentCount(&cSub));
	if (cSub > 0)
		wprintf(L"\n%d Restore Subcomponents:\n\n", cSub);

	for(UINT iSub = 0; iSub < cSub; iSub++)
		{
		CComBSTR bstrLogicalPath;
		CComBSTR bstrComponentName;
		bool bRepair;


		CHECK_SUCCESS(pComponent->GetRestoreSubcomponent
			(
			iSub,
			&bstrLogicalPath,
			&bstrComponentName,
			&bRepair
			));

		wprintf(L"Logical Path=%s, Name=%s, Repair=%s\n",
				bstrLogicalPath,
				bstrComponentName,
				bRepair ? L"Yes" : L"No");
        }
	}

