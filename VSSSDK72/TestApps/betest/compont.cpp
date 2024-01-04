#include "stdafx.hxx"
#include "vs_idl.hxx"
#include "vswriter.h"
#include "vsbackup.h"
#include "compont.h"
#include <debug.h>
#include <cwriter.h>
#include <lmshare.h>
#include <lmaccess.h>
#include <time.h>

//
//  CWriterComponentsSelection class
//

CWriterComponentsSelection::CWriterComponentsSelection()
{
    m_WriterId = GUID_NULL;
    m_uNumComponents = 0;
    m_uNumSubcomponents = 0;
    m_ppwszComponentLogicalPaths = NULL;
    m_ppwszSubcomponentLogicalPaths = NULL;    
}

CWriterComponentsSelection::~CWriterComponentsSelection()
{
    if ((m_uNumComponents > 0) && (m_ppwszComponentLogicalPaths != NULL))
        {
        for (UINT i=0; i<m_uNumComponents; i++)
            {
            if (m_ppwszComponentLogicalPaths[i] != NULL)
                {
                free(m_ppwszComponentLogicalPaths[i]);
                m_ppwszComponentLogicalPaths[i] = NULL;
                }
            }

        free(m_ppwszComponentLogicalPaths);
        m_ppwszComponentLogicalPaths = NULL;
        m_uNumComponents = 0;
        }

    if ((m_uNumSubcomponents > 0) && (m_ppwszSubcomponentLogicalPaths != NULL))
        {
        for (UINT i=0; i<m_uNumSubcomponents; i++)
            {
            if (m_ppwszSubcomponentLogicalPaths[i] != NULL)
                {
                free(m_ppwszSubcomponentLogicalPaths[i]);
                m_ppwszSubcomponentLogicalPaths[i] = NULL;
                }
            }

        free(m_ppwszSubcomponentLogicalPaths);
        m_ppwszSubcomponentLogicalPaths = NULL;
        m_uNumSubcomponents = 0;
        }    

    for (int x = 0; x < m_newTargets.GetSize(); x++)
        {
        NewTarget* pCurrent = m_newTargets.GetValueAt(x);
        while (pCurrent)
            {
            NewTarget* pNext = pCurrent->m_pNext;
            delete pCurrent;
            pCurrent = pNext;
            }         
        }        
}



void CWriterComponentsSelection::SetWriter
    (
    IN VSS_ID WriterId
    )
{
    m_WriterId = WriterId;
}

HRESULT CWriterComponentsSelection::AddSelectedComponent
    (
    IN WCHAR* pwszComponentLogicalPath
    )
{
    return AddSelected(pwszComponentLogicalPath, m_ppwszComponentLogicalPaths, m_uNumComponents);
}

HRESULT CWriterComponentsSelection::AddSelectedSubcomponent
    (
    IN WCHAR* pwszSubcomponentLogicalPath
    )
{
    return AddSelected(pwszSubcomponentLogicalPath, m_ppwszSubcomponentLogicalPaths, m_uNumSubcomponents);
}

HRESULT CWriterComponentsSelection::AddSelected
    (
    IN WCHAR* pwszLogicalPath, 
    WCHAR**& pwszLogicalPaths, 
    UINT& uSize
    )
{
    if (m_WriterId == GUID_NULL)
        {
        // Don't allow adding components to NULL writer...
        return E_UNEXPECTED;
        }

    if (pwszLogicalPath == NULL)
        {
        return E_INVALIDARG;
        }

    // A more clever implementation would allocate memory in chuncks, but this is just a test program...
    PWCHAR *ppwzTemp = (PWCHAR *) realloc(pwszLogicalPaths, (uSize+1) * sizeof (PWCHAR));
    if (ppwzTemp != NULL)
        {
        pwszLogicalPaths = ppwzTemp;
        pwszLogicalPaths[uSize] = NULL;
        }
    else
        {
        return E_OUTOFMEMORY;
        }

    pwszLogicalPaths[uSize] = (PWCHAR) malloc((wcslen(pwszLogicalPath) + 1) * sizeof (WCHAR));
    if (pwszLogicalPaths[uSize] != NULL)
        {
        wcscpy(pwszLogicalPaths[uSize], pwszLogicalPath);
        uSize++;
        }
    else
        {
        return E_OUTOFMEMORY;
        }

    return S_OK;
}

HRESULT CWriterComponentsSelection::AddNewTarget
    (
    IN WCHAR* pwszComponent,
    IN WCHAR* pwszSource,
    IN WCHAR* pwszTarget
    )
    {
        BS_ASSERT(IsSelected(pwszComponent, NULL, 
                    m_ppwszComponentLogicalPaths, m_uNumComponents));

        NewTarget* pTarget = m_newTargets.Lookup(pwszComponent);

        WCHAR* lastWhack = wcsrchr(pwszSource, L'\\');
        if (lastWhack == NULL || lastWhack == pwszSource || lastWhack == pwszSource + wcslen(pwszSource) - 1)
            return E_INVALIDARG;
        *lastWhack = L'\0';

        WCHAR* recursiveIndicator = wcsstr(lastWhack + 1, L"...");
        if (recursiveIndicator != NULL)
            {
            if (recursiveIndicator != lastWhack + wcslen(lastWhack + 1) - 2)
                return E_INVALIDARG;
            *recursiveIndicator = L'\0';
            }
        
        NewTarget* pNewTarget = new NewTarget
                                                                (
                                                                pwszSource, 
                                                                lastWhack + 1,
                                                                recursiveIndicator != NULL,             
                                                                pwszTarget, 
                                                                pTarget);

        *lastWhack = L'\\';
        if (recursiveIndicator)
            *recursiveIndicator = L'.';
        
        if (pNewTarget == NULL)
            return E_OUTOFMEMORY;

        if (pTarget == NULL) 
            {
            if (!m_newTargets.Add(pwszComponent, pNewTarget))
                {
                delete pNewTarget;
                return E_OUTOFMEMORY;
                }
            }
        else
            m_newTargets.SetAt(pwszComponent, pNewTarget);

        return S_OK;
     }

NewTarget* CWriterComponentsSelection::GetNewTargets
    (
    IN LPCWSTR pwszComponentLogicalPath,
    IN LPCWSTR pwszComponentName
    )
    {
    CComBSTR bstrComponent = pwszComponentLogicalPath;
    if (bstrComponent.Length() > 0 && bstrComponent[bstrComponent.Length() - 1] != L'\\')
        bstrComponent += L"\\";
    bstrComponent += pwszComponentName;

    return m_newTargets.Lookup(bstrComponent);
    }

BOOL CWriterComponentsSelection::IsComponentSelected
        (
        IN WCHAR* pwszComponentLogicalPath,
        IN WCHAR* pwszComponentName
        )
    {
        return IsSelected(pwszComponentLogicalPath, pwszComponentName, 
                    m_ppwszComponentLogicalPaths, m_uNumComponents);
    }

BOOL CWriterComponentsSelection::IsSubcomponentSelected
    (
    IN WCHAR* pwszSubcomponentLogicalPath,
    IN WCHAR* pwszSubcomponentName
    )
{
    return IsSelected(pwszSubcomponentLogicalPath, pwszSubcomponentName, 
                m_ppwszSubcomponentLogicalPaths, m_uNumSubcomponents);
}

BOOL CWriterComponentsSelection::IsSelected(IN WCHAR* pwszLogicalPath, IN WCHAR* pwszName, 
                        IN WCHAR** pwszLogicalPaths, IN  UINT uSize)
{
   if (m_WriterId == GUID_NULL)
        {
        // Don't allow query for NULL writer...
        return FALSE;
        }
    if (uSize <= 0)
        {
        return FALSE;
        }

    // A component matches if:
    //  1. The selection criteria is on the logical-path of the leaf component  OR
    //  2. The selection criteria is <full-logical-path>\<component-name>
    //  3. The selction criteria is component-name (only if logical-path is NULL)

    for (UINT i=0; i<uSize; i++)
        {
        DWORD dwLen;

        if (pwszLogicalPaths[i] == NULL)
            {
            continue;
            }

        dwLen = (DWORD)wcslen(pwszLogicalPaths[i]);

        if (pwszLogicalPath != NULL)
            {
            // Case 1.
            if (_wcsicmp(pwszLogicalPaths[i], pwszLogicalPath) == 0 &&
                 pwszName == NULL)
                {
                return TRUE;
                }

            // Case 2.
            if (pwszName == NULL)
                {
                continue;
                }
            WCHAR* pwszTemp = wcsrchr(pwszLogicalPaths[i], L'\\');
            if (pwszTemp == NULL)
                {
                continue;
                }
            if ((pwszTemp != pwszLogicalPaths[i]) && (*(pwszTemp+1) != L'\0'))
                {
                dwLen = (DWORD)(pwszTemp - pwszLogicalPaths[i]);
                if ( (dwLen == wcslen(pwszLogicalPath)) &&
                     (_wcsnicmp(pwszLogicalPaths[i], pwszLogicalPath, dwLen) == 0) &&
                     (_wcsicmp(pwszTemp+1, pwszName) == 0) )
                    {
                    return TRUE;
                    }
                }
            }
        else
            {
            // Case 3.
            if (pwszName == NULL)
                {
                continue;
                }
            if (_wcsicmp(pwszLogicalPaths[i], pwszName) == 0)
                {
                return TRUE;
                }
            }
        }

    return FALSE;
}

//
//  CWritersSelection class
//

CWritersSelection::CWritersSelection()
{
    m_lRef = 0;
}

CWritersSelection::~CWritersSelection()
{
    // Cleanup the Map
    for(int nIndex = 0; nIndex < m_WritersMap.GetSize(); nIndex++)
        {
        CWriterComponentsSelection* pComponentsSelection = m_WritersMap.GetValueAt(nIndex);
        if (pComponentsSelection)
            {
            delete pComponentsSelection;
            }
        }

    m_WritersMap.RemoveAll();
}

CWritersSelection* CWritersSelection::CreateInstance()
{
    CWritersSelection* pObj = new CWritersSelection;

    return pObj;
}

STDMETHODIMP CWritersSelection::QueryInterface(
    IN  REFIID iid,
    OUT void** pp
    )
{
    if (pp == NULL)
        return E_INVALIDARG;
    if (iid != IID_IUnknown)
        return E_NOINTERFACE;

    AddRef();
    IUnknown** pUnk = reinterpret_cast<IUnknown**>(pp);
    (*pUnk) = static_cast<IUnknown*>(this);
    return S_OK;
}


ULONG CWritersSelection::AddRef()
{
    return ::InterlockedIncrement(&m_lRef);
}


ULONG CWritersSelection::Release()
{
    LONG l = ::InterlockedDecrement(&m_lRef);
    if (l == 0)
        delete this; // We assume that we always allocate this object on the heap!
    return l;
}


STDMETHODIMP CWritersSelection::BuildChosenComponents
    (
    WCHAR *pwszComponentsFileName
    )
{
    HRESULT hr = S_OK;
    HANDLE  hFile = INVALID_HANDLE_VALUE;
    DWORD dwBytesToRead = 0;
    DWORD dwBytesRead;

    // Create the file
    hFile = CreateFile(pwszComponentsFileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        {
        DWORD dwLastError = GetLastError();
        wprintf(L"Invalid components file, CreateFile returned = %lu\n", dwLastError);
        return HRESULT_FROM_WIN32(dwLastError);
        }

    if ((dwBytesToRead = GetFileSize(hFile, NULL)) <= 0)
        {
        CloseHandle(hFile);
        DWORD dwLastError = GetLastError();
        wprintf(L"Invalid components file, GetFileSize returned = %lu\n", dwLastError);
        return HRESULT_FROM_WIN32(dwLastError);
        }

    if (dwBytesToRead > 0x100000)
        {
        CloseHandle(hFile);
        wprintf(L"Invalid components file, Provide a file with a size of less than 1 MB\n");
        return E_FAIL;
        }

    char * pcBuffer = (PCHAR) malloc (dwBytesToRead);
    if (! pcBuffer)
        {
        CloseHandle(hFile);
        return E_OUTOFMEMORY;
        }

    // Read the components info
    if (! ReadFile(hFile, (LPVOID)pcBuffer, dwBytesToRead, &dwBytesRead, NULL))
        {
        DWORD dwLastError = GetLastError();
        CloseHandle(hFile);
        free (pcBuffer);
        wprintf(L"Invalid components file, ReadFile returned = %lu\n", dwLastError);
        return HRESULT_FROM_WIN32(dwLastError);
        }

    CloseHandle(hFile);

    if (dwBytesToRead != dwBytesRead)
        {
        free (pcBuffer);
        wprintf(L"Components selection file is supposed to have %lu bytes but only %lu bytes are read\n", dwBytesToRead, dwBytesRead);
        return E_FAIL;
        }

    // Allocate a buffer to work with
    WCHAR * pwcBuffer = (PWCHAR) malloc ((dwBytesToRead+1) * sizeof(WCHAR));
    if (! pwcBuffer)
        {
        free (pcBuffer);
        return E_OUTOFMEMORY;
        }

    // Simple pasring, assume ANSI, Format:
    // "writer1-id": "component1.1-name", "component1.2-name",...        ; "writer2-id": "component2.1-name", ...
    CWriterComponentsSelection* pWriterComponents = NULL;

    try
        {
        VSS_ID WriterId = GUID_NULL;
        BOOL bBeforeWriter = TRUE;
        BOOL bBeforeComponents = TRUE;
        BOOL bInString = FALSE;
        BOOL bInTarget = FALSE;
        BOOL bInTargetTarget = FALSE;        
        char* pcStart = NULL;
        WCHAR* pBufferCurrent = pwcBuffer;

        for (char* pcCurrent = pcBuffer; pcCurrent < (pcBuffer+dwBytesToRead); pcCurrent++)
            {
            switch (*pcCurrent)
                {
                case '{':
                    if (bInString)
                        break;
                        
                    if (!bBeforeWriter && bBeforeComponents && !bInTarget)
                        bInTarget = TRUE;
                    else
                        throw (E_FAIL);

                    break;
                case '}':
                    if (bInString)
                        break;

                    assert(bInTarget && !bInTargetTarget);
                    bInTarget = FALSE;

                    break;
                case '#':
                    if (bInString)
                        break;

                    if (!bInTarget)
                        throw(E_FAIL);

                    assert(bInTargetTarget);
                    break;
                case ':':
                    if (bBeforeWriter && !bInString)
                        {
                        bBeforeWriter = FALSE;
                        }
                    else if (bBeforeComponents && !bInString)
                        {
                        bBeforeComponents = FALSE;
                        bInTarget = bInTargetTarget = FALSE;                        
                        }
                    else if (!bInString)
                        {
                        throw(E_FAIL);
                        }
                    break;

                case ';':
                    if (bBeforeWriter || bInString)
                        {
                        throw(E_FAIL);
                        }
                    else
                        {
                        // If we have a valid writer - add it to the map
                        if ((pWriterComponents != NULL) && (WriterId != GUID_NULL))
                            {
                            if (!m_WritersMap.Add(WriterId, pWriterComponents)) 
                                {
                                delete pWriterComponents;
                                throw E_OUTOFMEMORY;
                                }

                            pWriterComponents = NULL;
                            WriterId = GUID_NULL;
                            }

                        bBeforeWriter = TRUE;
                        }
                    break;

                case ',':
                    if (bBeforeWriter || bInString)
                        {
                        throw(E_FAIL);
                        }
                    break;

                case '"':
                    if (! bInString)
                        {
                        // Mark string-start for later
                        pcStart = pcCurrent + 1;
                        }
                    else if (pcStart == pcCurrent)
                        {
                        // empty string - skip it
                        }
                    else
                        {
                        if (!bInTarget)
                            pBufferCurrent = pwcBuffer;

                        // String ends - convert to WCHAR and process
                        DWORD dwSize = (DWORD)mbstowcs(pBufferCurrent, pcStart, pcCurrent - pcStart);
                        pBufferCurrent[dwSize] = NULL;
                        if (dwSize <= 0)
                            {
                            throw(E_FAIL);
                            }

                        if (bBeforeWriter)
                            {
                            // If before-writer - must be a writer GUID
                            HRESULT hrConvert = CLSIDFromString(pBufferCurrent, &WriterId);
                            if ((! SUCCEEDED(hrConvert)) && (hrConvert != REGDB_E_WRITEREGDB))
                                {
                                wprintf(L"A writer id in the components selection file is in invalid GUID format\n");
                                throw(E_FAIL);
                                }

                            if (pWriterComponents != NULL)
                                {
                                // Previous writer info was not ended correctly
                                throw(E_FAIL);
                                }

                            pWriterComponents = new CWriterComponentsSelection;
                            if (pWriterComponents == NULL)
                                {
                                throw(E_OUTOFMEMORY);
                                }
                            pWriterComponents->SetWriter(WriterId);
                            }
                        else if (bBeforeComponents && !bInTarget)
                            {
                            // Must be a component logical-path , name or logical-path\name
                            if (pWriterComponents != NULL)
                                {
                                CHECK_SUCCESS(pWriterComponents->AddSelectedComponent(pBufferCurrent));
                                }

                            pBufferCurrent += wcslen(pBufferCurrent) + 1;
                            }
                        else if (bBeforeComponents && bInTarget)
                            {
                            if (!bInTargetTarget)
                                {
                                bInTargetTarget = TRUE;
                                pBufferCurrent += wcslen(pBufferCurrent) + 1;
                                }
                            else
                                {                                
                                CHECK_SUCCESS(pWriterComponents->AddNewTarget(pwcBuffer, pwcBuffer + wcslen(pwcBuffer) + 1, pBufferCurrent));
                                pBufferCurrent = pwcBuffer + wcslen(pwcBuffer) + 1;
                                bInTargetTarget = FALSE;
                                }                            
                            }
                        else 
                            {
                            // Must be a component logical-path , name or logical-path\name
                            if (pWriterComponents != NULL)
                                {
                                CHECK_SUCCESS(pWriterComponents->AddSelectedSubcomponent(pBufferCurrent));
                                }                            
                            }
                        }

                    // Flip in-string flag
                    bInString = (! bInString);

                    break;

                case ' ':
                    break;

                case '\n':
                case '\t':
                case '\r':
                    if (bInString)
                        {
                        throw(E_FAIL);
                        }

                    break;

                default:
                    if (! bInString)
                        {
                        throw(E_FAIL);
                        }

                    break;

                }
            }
         }

    catch (HRESULT hrParse)
        {
        hr = hrParse;

        if (hr == E_FAIL)
            {
            wprintf(L"Invalid format of components selection file\n");
            }

        if (pWriterComponents != NULL)
            {
            // Error int he middle of writer-components creation (not added to the map yet...)
            delete pWriterComponents;
            }
        }

    free (pcBuffer);
    free (pwcBuffer);

    return hr;
}
    
BOOL CWritersSelection::IsWriterSelected
        (
        IN VSS_ID WriterId
        )
{
    return m_WritersMap.Lookup(WriterId) != NULL;
}
    
BOOL CWritersSelection::IsComponentSelected
    (
    IN VSS_ID WriterId,
    IN WCHAR* pwszComponentLogicalPath,
    IN WCHAR* pwszComponentName
    )
{
    CWriterComponentsSelection* pWriterComponents = m_WritersMap.Lookup(WriterId);
    if (pWriterComponents == NULL)
        {
        // No component is selected for this writer
        return FALSE;
        }

    // There are components selected for this writer, check if this specific one is selected
    return pWriterComponents->IsComponentSelected(pwszComponentLogicalPath, pwszComponentName);
}

BOOL CWritersSelection::IsSubcomponentSelected
    (
    IN VSS_ID WriterId,
    IN WCHAR* pwszComponentLogicalPath,
    IN WCHAR* pwszComponentName
    )
{
    CWriterComponentsSelection* pWriterComponents = m_WritersMap.Lookup(WriterId);
    if (pWriterComponents == NULL)
        {
        // No component is selected for this writer
        return FALSE;
        }

    // There are subccomponents selected for this writer, check if this specific one is selected
    return pWriterComponents->IsSubcomponentSelected(pwszComponentLogicalPath, pwszComponentName);
}

const WCHAR* const * CWritersSelection::GetComponents
    (
    IN VSS_ID WriterId
    )
{
    CWriterComponentsSelection* pWriterComponents = m_WritersMap.Lookup(WriterId);
    if (pWriterComponents == NULL)
        {
        return NULL;
        }

    return pWriterComponents->GetComponents();
}

const WCHAR* const * CWritersSelection::GetSubcomponents
    (
    IN VSS_ID WriterId
    )
{
    CWriterComponentsSelection* pWriterComponents = m_WritersMap.Lookup(WriterId);
    if (pWriterComponents == NULL)
        {
        return NULL;
        }

    return pWriterComponents->GetSubcomponents();
}

NewTarget* CWritersSelection::GetNewTargets
        (
        IN VSS_ID WriterId,
        IN LPCWSTR wszLogicalPath,
        IN LPCWSTR wszName
        )
{
  CWriterComponentsSelection* pWriterComponents = m_WritersMap.Lookup(WriterId);
    if (pWriterComponents == NULL)
        {
        return NULL;
        }

    return pWriterComponents->GetNewTargets(wszLogicalPath, wszName);
}


const UINT CWritersSelection::GetComponentsCount
    (
    IN VSS_ID WriterId
    )
{
    CWriterComponentsSelection* pWriterComponents = m_WritersMap.Lookup(WriterId);
    if (pWriterComponents == NULL)
        {
        return NULL;
        }

    return pWriterComponents->GetComponentsCount();
}

const UINT CWritersSelection::GetSubcomponentsCount
    (
    IN VSS_ID WriterId
    )
{
    CWriterComponentsSelection* pWriterComponents = m_WritersMap.Lookup(WriterId);
    if (pWriterComponents == NULL)
        {
        return NULL;
        }

    return pWriterComponents->GetSubcomponentsCount();
}


