#ifndef _COMPONTH_
#define _COMPONTH_

#include "vs_hash.hxx"

struct NewTarget
{
    NewTarget(WCHAR* wszSourcePath, WCHAR* wszSourceFilespec, bool bRecursive, WCHAR* wszTarget, NewTarget* pNext) : 
                                                                                                                     m_bstrSourcePath(wszSourcePath),
                                                                                                                     m_bstrSourceFilespec(wszSourceFilespec),
                                                                                                                     m_bRecursive(bRecursive),
                                                                                                                     m_bstrTarget(wszTarget),
                                                                                                                     m_pNext(pNext),
                                                                                                                     m_cTargets(1)
        {
        if (m_pNext)
            m_cTargets = m_pNext->m_cTargets + 1;
        }
    CComBSTR m_bstrSourcePath;
    CComBSTR m_bstrSourceFilespec;    
    CComBSTR m_bstrTarget;
    bool m_bRecursive;
    NewTarget* m_pNext;
    unsigned m_cTargets;
};

class CWriterComponentsSelection
{
public:
	// Construction Destruction
	CWriterComponentsSelection();
	~CWriterComponentsSelection();

	// methods
	void SetWriter
		(
		IN VSS_ID WriterId
		);
	
	HRESULT AddSelectedComponent
		(
		IN WCHAR* pwszComponentLogicalPath
		);

       HRESULT AddSelectedSubcomponent
            (
            IN WCHAR* pwszSubcomponentLogicalPath
            );

       HRESULT AddNewTarget
            (
            IN WCHAR* pwszComponent,
            IN WCHAR* pwszSource,
            IN WCHAR* pwszTarget
            );

       NewTarget* GetNewTargets
              (
              IN LPCWSTR pwszComponentLogicalPath,
              IN LPCWSTR pwszComponentName
              );
       
	   
	BOOL IsComponentSelected
		(
		IN WCHAR* pwszComponentLogicalPath,
		IN WCHAR* pwszComponentName
		);

        BOOL IsSubcomponentSelected
            (
            IN WCHAR* pwszSubcomponentLogicalPath,
            IN WCHAR* pwszSubcomponentName
            );

        UINT GetComponentsCount()
            { return m_uNumComponents; }

        UINT GetSubcomponentsCount()
            { return m_uNumSubcomponents; }        

        const WCHAR* const * GetComponents()
            { return m_ppwszComponentLogicalPaths; }
        
        const WCHAR* const * GetSubcomponents()
            { return m_ppwszSubcomponentLogicalPaths; }            
private:        
    HRESULT AddSelected(IN WCHAR* pwszLogicalPath, WCHAR**& ppwszLogicalPaths, UINT& uSize);
    BOOL IsSelected(IN WCHAR* pwszLogicalPath, IN WCHAR* pwszName, IN WCHAR** pwszLogicalPaths,
                             IN  UINT uSize);
    
    VSS_ID            m_WriterId;
    UINT                m_uNumComponents;
    WCHAR**         m_ppwszComponentLogicalPaths;
    UINT                m_uNumSubcomponents;
    WCHAR**         m_ppwszSubcomponentLogicalPaths;
    CVssSimpleMap<CComBSTR, NewTarget*> m_newTargets;		
};


class CWritersSelection :
	public IUnknown            // Must be the FIRST base class since we use CComPtr<CVssSnapshotSetObject>

{
protected:
	// Construction Destruction
	CWritersSelection();
	~CWritersSelection();
	
public:
	// Creation
	static CWritersSelection* CreateInstance();

	// Chosen writers & components management
	STDMETHOD(BuildChosenComponents)
		(
		WCHAR *pwszComponentsFileName
		);

	BOOL IsWriterSelected
		(
		IN VSS_ID WriterId
		);
	
	BOOL IsComponentSelected
		(
		IN VSS_ID WriterId,
		IN WCHAR* pwszComponentLogicalPath,
		IN WCHAR* pwszComponentName
		);

	BOOL IsSubcomponentSelected
		(
		IN VSS_ID WriterId,
		IN WCHAR* pwszComponentLogicalPath,
		IN WCHAR* pwszComponentName
		);

    const WCHAR* const * GetComponents
            (
            IN VSS_ID WriterId
            );
    const WCHAR* const * GetSubcomponents
            (
            IN VSS_ID WriterId
            );

    const UINT GetComponentsCount
            (
            IN VSS_ID WriterId
            );
    const UINT GetSubcomponentsCount
            (
            IN VSS_ID WriterId
            );

    NewTarget* GetNewTargets
        (
        IN VSS_ID WriterId,
        IN LPCWSTR wszLogicalPath,
        IN LPCWSTR wszName
        );
    
    const UINT GetWritersCount()    {
        return m_WritersMap.GetSize();
     }
    
    const VSS_ID GetWriter(IN UINT index) {
        return m_WritersMap.GetKeyAt(index);
    }
        
	// IUnknown
	STDMETHOD(QueryInterface)(REFIID iid, void** pp);
	STDMETHOD_(ULONG, AddRef)();
	STDMETHOD_(ULONG, Release)();
	
private:
	// Chosen writers
	CVssSimpleMap<VSS_ID, CWriterComponentsSelection*> m_WritersMap;
	
    // For life management
	LONG 	m_lRef;
};

#endif	// _COMPONTH_
