

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 6.00.0366 */
/* Compiler settings for vsmgmt.idl:
    Oicf, W1, Zp8, env=Win32 (32b run)
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
//@@MIDL_FILE_HEADING(  )

#pragma warning( disable: 4049 )  /* more than 64k source lines */


/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif // __RPCNDR_H_VERSION__

#ifndef COM_NO_WINDOWS_H
#include "windows.h"
#include "ole2.h"
#endif /*COM_NO_WINDOWS_H*/

#ifndef __vsmgmt_h__
#define __vsmgmt_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __IVssSnapshotMgmt_FWD_DEFINED__
#define __IVssSnapshotMgmt_FWD_DEFINED__
typedef interface IVssSnapshotMgmt IVssSnapshotMgmt;
#endif 	/* __IVssSnapshotMgmt_FWD_DEFINED__ */


#ifndef __IVssSnapshotMgmt2_FWD_DEFINED__
#define __IVssSnapshotMgmt2_FWD_DEFINED__
typedef interface IVssSnapshotMgmt2 IVssSnapshotMgmt2;
#endif 	/* __IVssSnapshotMgmt2_FWD_DEFINED__ */


#ifndef __IVssDifferentialSoftwareSnapshotMgmt_FWD_DEFINED__
#define __IVssDifferentialSoftwareSnapshotMgmt_FWD_DEFINED__
typedef interface IVssDifferentialSoftwareSnapshotMgmt IVssDifferentialSoftwareSnapshotMgmt;
#endif 	/* __IVssDifferentialSoftwareSnapshotMgmt_FWD_DEFINED__ */


#ifndef __IVssEnumMgmtObject_FWD_DEFINED__
#define __IVssEnumMgmtObject_FWD_DEFINED__
typedef interface IVssEnumMgmtObject IVssEnumMgmtObject;
#endif 	/* __IVssEnumMgmtObject_FWD_DEFINED__ */


#ifndef __VssSnapshotMgmt_FWD_DEFINED__
#define __VssSnapshotMgmt_FWD_DEFINED__

#ifdef __cplusplus
typedef class VssSnapshotMgmt VssSnapshotMgmt;
#else
typedef struct VssSnapshotMgmt VssSnapshotMgmt;
#endif /* __cplusplus */

#endif 	/* __VssSnapshotMgmt_FWD_DEFINED__ */


/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"
#include "vss.h"

#ifdef __cplusplus
extern "C"{
#endif 

void * __RPC_USER MIDL_user_allocate(size_t);
void __RPC_USER MIDL_user_free( void * ); 

/* interface __MIDL_itf_vsmgmt_0000 */
/* [local] */ 

typedef 
enum _VSS_MGMT_OBJECT_TYPE
    {	VSS_MGMT_OBJECT_UNKNOWN	= 0,
	VSS_MGMT_OBJECT_VOLUME	= VSS_MGMT_OBJECT_UNKNOWN + 1,
	VSS_MGMT_OBJECT_DIFF_VOLUME	= VSS_MGMT_OBJECT_VOLUME + 1,
	VSS_MGMT_OBJECT_DIFF_AREA	= VSS_MGMT_OBJECT_DIFF_VOLUME + 1
    } 	VSS_MGMT_OBJECT_TYPE;

#define	VSS_ASSOC_NO_MAX_SPACE	( -1 )

#define	VSS_ASSOC_REMOVE	( 0 )

typedef struct _VSS_VOLUME_PROP
    {
    VSS_PWSZ m_pwszVolumeName;
    VSS_PWSZ m_pwszVolumeDisplayName;
    } 	VSS_VOLUME_PROP;

typedef struct _VSS_VOLUME_PROP *PVSS_VOLUME_PROP;

typedef struct _VSS_DIFF_VOLUME_PROP
    {
    VSS_PWSZ m_pwszVolumeName;
    VSS_PWSZ m_pwszVolumeDisplayName;
    LONGLONG m_llVolumeFreeSpace;
    LONGLONG m_llVolumeTotalSpace;
    } 	VSS_DIFF_VOLUME_PROP;

typedef struct _VSS_DIFF_VOLUME_PROP *PVSS_DIFF_VOLUME_PROP;

typedef struct _VSS_DIFF_AREA_PROP
    {
    VSS_PWSZ m_pwszVolumeName;
    VSS_PWSZ m_pwszDiffAreaVolumeName;
    LONGLONG m_llMaximumDiffSpace;
    LONGLONG m_llAllocatedDiffSpace;
    LONGLONG m_llUsedDiffSpace;
    } 	VSS_DIFF_AREA_PROP;

typedef struct _VSS_DIFF_AREA_PROP *PVSS_DIFF_AREA_PROP;

typedef /* [public][public][public][public][switch_type] */ union __MIDL___MIDL_itf_vsmgmt_0000_0001
    {
    /* [case()] */ VSS_VOLUME_PROP Vol;
    /* [case()] */ VSS_DIFF_VOLUME_PROP DiffVol;
    /* [case()] */ VSS_DIFF_AREA_PROP DiffArea;
    /* [default] */  /* Empty union arm */ 
    } 	VSS_MGMT_OBJECT_UNION;

typedef struct _VSS_MGMT_OBJECT_PROP
    {
    VSS_MGMT_OBJECT_TYPE Type;
    /* [switch_is] */ VSS_MGMT_OBJECT_UNION Obj;
    } 	VSS_MGMT_OBJECT_PROP;

typedef struct _VSS_MGMT_OBJECT_PROP *PVSS_MGMT_OBJECT_PROP;






extern RPC_IF_HANDLE __MIDL_itf_vsmgmt_0000_v0_0_c_ifspec;
extern RPC_IF_HANDLE __MIDL_itf_vsmgmt_0000_v0_0_s_ifspec;

#ifndef __IVssSnapshotMgmt_INTERFACE_DEFINED__
#define __IVssSnapshotMgmt_INTERFACE_DEFINED__

/* interface IVssSnapshotMgmt */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IVssSnapshotMgmt;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("FA7DF749-66E7-4986-A27F-E2F04AE53772")
    IVssSnapshotMgmt : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetProviderMgmtInterface( 
            /* [in] */ VSS_ID ProviderId,
            /* [in] */ REFIID InterfaceId,
            /* [iid_is][out] */ IUnknown **ppItf) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE QueryVolumesSupportedForSnapshots( 
            /* [in] */ VSS_ID ProviderId,
            /* [in] */ LONG lContext,
            /* [out] */ IVssEnumMgmtObject **ppEnum) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE QuerySnapshotsByVolume( 
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [in] */ VSS_ID ProviderId,
            /* [out] */ IVssEnumObject **ppEnum) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVssSnapshotMgmtVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVssSnapshotMgmt * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVssSnapshotMgmt * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVssSnapshotMgmt * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetProviderMgmtInterface )( 
            IVssSnapshotMgmt * This,
            /* [in] */ VSS_ID ProviderId,
            /* [in] */ REFIID InterfaceId,
            /* [iid_is][out] */ IUnknown **ppItf);
        
        HRESULT ( STDMETHODCALLTYPE *QueryVolumesSupportedForSnapshots )( 
            IVssSnapshotMgmt * This,
            /* [in] */ VSS_ID ProviderId,
            /* [in] */ LONG lContext,
            /* [out] */ IVssEnumMgmtObject **ppEnum);
        
        HRESULT ( STDMETHODCALLTYPE *QuerySnapshotsByVolume )( 
            IVssSnapshotMgmt * This,
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [in] */ VSS_ID ProviderId,
            /* [out] */ IVssEnumObject **ppEnum);
        
        END_INTERFACE
    } IVssSnapshotMgmtVtbl;

    interface IVssSnapshotMgmt
    {
        CONST_VTBL struct IVssSnapshotMgmtVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVssSnapshotMgmt_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVssSnapshotMgmt_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVssSnapshotMgmt_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVssSnapshotMgmt_GetProviderMgmtInterface(This,ProviderId,InterfaceId,ppItf)	\
    (This)->lpVtbl -> GetProviderMgmtInterface(This,ProviderId,InterfaceId,ppItf)

#define IVssSnapshotMgmt_QueryVolumesSupportedForSnapshots(This,ProviderId,lContext,ppEnum)	\
    (This)->lpVtbl -> QueryVolumesSupportedForSnapshots(This,ProviderId,lContext,ppEnum)

#define IVssSnapshotMgmt_QuerySnapshotsByVolume(This,pwszVolumeName,ProviderId,ppEnum)	\
    (This)->lpVtbl -> QuerySnapshotsByVolume(This,pwszVolumeName,ProviderId,ppEnum)

#endif /* COBJMACROS */


#endif 	/* C style interface */



HRESULT STDMETHODCALLTYPE IVssSnapshotMgmt_GetProviderMgmtInterface_Proxy( 
    IVssSnapshotMgmt * This,
    /* [in] */ VSS_ID ProviderId,
    /* [in] */ REFIID InterfaceId,
    /* [iid_is][out] */ IUnknown **ppItf);


void __RPC_STUB IVssSnapshotMgmt_GetProviderMgmtInterface_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


HRESULT STDMETHODCALLTYPE IVssSnapshotMgmt_QueryVolumesSupportedForSnapshots_Proxy( 
    IVssSnapshotMgmt * This,
    /* [in] */ VSS_ID ProviderId,
    /* [in] */ LONG lContext,
    /* [out] */ IVssEnumMgmtObject **ppEnum);


void __RPC_STUB IVssSnapshotMgmt_QueryVolumesSupportedForSnapshots_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


HRESULT STDMETHODCALLTYPE IVssSnapshotMgmt_QuerySnapshotsByVolume_Proxy( 
    IVssSnapshotMgmt * This,
    /* [in] */ VSS_PWSZ pwszVolumeName,
    /* [in] */ VSS_ID ProviderId,
    /* [out] */ IVssEnumObject **ppEnum);


void __RPC_STUB IVssSnapshotMgmt_QuerySnapshotsByVolume_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVssSnapshotMgmt_INTERFACE_DEFINED__ */


#ifndef __IVssSnapshotMgmt2_INTERFACE_DEFINED__
#define __IVssSnapshotMgmt2_INTERFACE_DEFINED__

/* interface IVssSnapshotMgmt2 */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IVssSnapshotMgmt2;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("0f61ec39-fe82-45f2-a3f0-768b5d427102")
    IVssSnapshotMgmt2 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetMinDiffAreaSize( 
            /* [out] */ LONGLONG *pllMinDiffAreaSize) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVssSnapshotMgmt2Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVssSnapshotMgmt2 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVssSnapshotMgmt2 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVssSnapshotMgmt2 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetMinDiffAreaSize )( 
            IVssSnapshotMgmt2 * This,
            /* [out] */ LONGLONG *pllMinDiffAreaSize);
        
        END_INTERFACE
    } IVssSnapshotMgmt2Vtbl;

    interface IVssSnapshotMgmt2
    {
        CONST_VTBL struct IVssSnapshotMgmt2Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVssSnapshotMgmt2_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVssSnapshotMgmt2_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVssSnapshotMgmt2_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVssSnapshotMgmt2_GetMinDiffAreaSize(This,pllMinDiffAreaSize)	\
    (This)->lpVtbl -> GetMinDiffAreaSize(This,pllMinDiffAreaSize)

#endif /* COBJMACROS */


#endif 	/* C style interface */



HRESULT STDMETHODCALLTYPE IVssSnapshotMgmt2_GetMinDiffAreaSize_Proxy( 
    IVssSnapshotMgmt2 * This,
    /* [out] */ LONGLONG *pllMinDiffAreaSize);


void __RPC_STUB IVssSnapshotMgmt2_GetMinDiffAreaSize_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVssSnapshotMgmt2_INTERFACE_DEFINED__ */


#ifndef __IVssDifferentialSoftwareSnapshotMgmt_INTERFACE_DEFINED__
#define __IVssDifferentialSoftwareSnapshotMgmt_INTERFACE_DEFINED__

/* interface IVssDifferentialSoftwareSnapshotMgmt */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IVssDifferentialSoftwareSnapshotMgmt;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("214A0F28-B737-4026-B847-4F9E37D79529")
    IVssDifferentialSoftwareSnapshotMgmt : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE AddDiffArea( 
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [in] */ VSS_PWSZ pwszDiffAreaVolumeName,
            /* [in] */ LONGLONG llMaximumDiffSpace) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE ChangeDiffAreaMaximumSize( 
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [in] */ VSS_PWSZ pwszDiffAreaVolumeName,
            /* [in] */ LONGLONG llMaximumDiffSpace) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE QueryVolumesSupportedForDiffAreas( 
            /* [in] */ VSS_PWSZ pwszOriginalVolumeName,
            /* [out] */ IVssEnumMgmtObject **ppEnum) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE QueryDiffAreasForVolume( 
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [out] */ IVssEnumMgmtObject **ppEnum) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE QueryDiffAreasOnVolume( 
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [out] */ IVssEnumMgmtObject **ppEnum) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE QueryDiffAreasForSnapshot( 
            /* [in] */ VSS_ID SnapshotId,
            /* [out] */ IVssEnumMgmtObject **ppEnum) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVssDifferentialSoftwareSnapshotMgmtVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVssDifferentialSoftwareSnapshotMgmt * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVssDifferentialSoftwareSnapshotMgmt * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVssDifferentialSoftwareSnapshotMgmt * This);
        
        HRESULT ( STDMETHODCALLTYPE *AddDiffArea )( 
            IVssDifferentialSoftwareSnapshotMgmt * This,
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [in] */ VSS_PWSZ pwszDiffAreaVolumeName,
            /* [in] */ LONGLONG llMaximumDiffSpace);
        
        HRESULT ( STDMETHODCALLTYPE *ChangeDiffAreaMaximumSize )( 
            IVssDifferentialSoftwareSnapshotMgmt * This,
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [in] */ VSS_PWSZ pwszDiffAreaVolumeName,
            /* [in] */ LONGLONG llMaximumDiffSpace);
        
        HRESULT ( STDMETHODCALLTYPE *QueryVolumesSupportedForDiffAreas )( 
            IVssDifferentialSoftwareSnapshotMgmt * This,
            /* [in] */ VSS_PWSZ pwszOriginalVolumeName,
            /* [out] */ IVssEnumMgmtObject **ppEnum);
        
        HRESULT ( STDMETHODCALLTYPE *QueryDiffAreasForVolume )( 
            IVssDifferentialSoftwareSnapshotMgmt * This,
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [out] */ IVssEnumMgmtObject **ppEnum);
        
        HRESULT ( STDMETHODCALLTYPE *QueryDiffAreasOnVolume )( 
            IVssDifferentialSoftwareSnapshotMgmt * This,
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [out] */ IVssEnumMgmtObject **ppEnum);
        
        HRESULT ( STDMETHODCALLTYPE *QueryDiffAreasForSnapshot )( 
            IVssDifferentialSoftwareSnapshotMgmt * This,
            /* [in] */ VSS_ID SnapshotId,
            /* [out] */ IVssEnumMgmtObject **ppEnum);
        
        END_INTERFACE
    } IVssDifferentialSoftwareSnapshotMgmtVtbl;

    interface IVssDifferentialSoftwareSnapshotMgmt
    {
        CONST_VTBL struct IVssDifferentialSoftwareSnapshotMgmtVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVssDifferentialSoftwareSnapshotMgmt_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVssDifferentialSoftwareSnapshotMgmt_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVssDifferentialSoftwareSnapshotMgmt_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVssDifferentialSoftwareSnapshotMgmt_AddDiffArea(This,pwszVolumeName,pwszDiffAreaVolumeName,llMaximumDiffSpace)	\
    (This)->lpVtbl -> AddDiffArea(This,pwszVolumeName,pwszDiffAreaVolumeName,llMaximumDiffSpace)

#define IVssDifferentialSoftwareSnapshotMgmt_ChangeDiffAreaMaximumSize(This,pwszVolumeName,pwszDiffAreaVolumeName,llMaximumDiffSpace)	\
    (This)->lpVtbl -> ChangeDiffAreaMaximumSize(This,pwszVolumeName,pwszDiffAreaVolumeName,llMaximumDiffSpace)

#define IVssDifferentialSoftwareSnapshotMgmt_QueryVolumesSupportedForDiffAreas(This,pwszOriginalVolumeName,ppEnum)	\
    (This)->lpVtbl -> QueryVolumesSupportedForDiffAreas(This,pwszOriginalVolumeName,ppEnum)

#define IVssDifferentialSoftwareSnapshotMgmt_QueryDiffAreasForVolume(This,pwszVolumeName,ppEnum)	\
    (This)->lpVtbl -> QueryDiffAreasForVolume(This,pwszVolumeName,ppEnum)

#define IVssDifferentialSoftwareSnapshotMgmt_QueryDiffAreasOnVolume(This,pwszVolumeName,ppEnum)	\
    (This)->lpVtbl -> QueryDiffAreasOnVolume(This,pwszVolumeName,ppEnum)

#define IVssDifferentialSoftwareSnapshotMgmt_QueryDiffAreasForSnapshot(This,SnapshotId,ppEnum)	\
    (This)->lpVtbl -> QueryDiffAreasForSnapshot(This,SnapshotId,ppEnum)

#endif /* COBJMACROS */


#endif 	/* C style interface */



HRESULT STDMETHODCALLTYPE IVssDifferentialSoftwareSnapshotMgmt_AddDiffArea_Proxy( 
    IVssDifferentialSoftwareSnapshotMgmt * This,
    /* [in] */ VSS_PWSZ pwszVolumeName,
    /* [in] */ VSS_PWSZ pwszDiffAreaVolumeName,
    /* [in] */ LONGLONG llMaximumDiffSpace);


void __RPC_STUB IVssDifferentialSoftwareSnapshotMgmt_AddDiffArea_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


HRESULT STDMETHODCALLTYPE IVssDifferentialSoftwareSnapshotMgmt_ChangeDiffAreaMaximumSize_Proxy( 
    IVssDifferentialSoftwareSnapshotMgmt * This,
    /* [in] */ VSS_PWSZ pwszVolumeName,
    /* [in] */ VSS_PWSZ pwszDiffAreaVolumeName,
    /* [in] */ LONGLONG llMaximumDiffSpace);


void __RPC_STUB IVssDifferentialSoftwareSnapshotMgmt_ChangeDiffAreaMaximumSize_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


HRESULT STDMETHODCALLTYPE IVssDifferentialSoftwareSnapshotMgmt_QueryVolumesSupportedForDiffAreas_Proxy( 
    IVssDifferentialSoftwareSnapshotMgmt * This,
    /* [in] */ VSS_PWSZ pwszOriginalVolumeName,
    /* [out] */ IVssEnumMgmtObject **ppEnum);


void __RPC_STUB IVssDifferentialSoftwareSnapshotMgmt_QueryVolumesSupportedForDiffAreas_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


HRESULT STDMETHODCALLTYPE IVssDifferentialSoftwareSnapshotMgmt_QueryDiffAreasForVolume_Proxy( 
    IVssDifferentialSoftwareSnapshotMgmt * This,
    /* [in] */ VSS_PWSZ pwszVolumeName,
    /* [out] */ IVssEnumMgmtObject **ppEnum);


void __RPC_STUB IVssDifferentialSoftwareSnapshotMgmt_QueryDiffAreasForVolume_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


HRESULT STDMETHODCALLTYPE IVssDifferentialSoftwareSnapshotMgmt_QueryDiffAreasOnVolume_Proxy( 
    IVssDifferentialSoftwareSnapshotMgmt * This,
    /* [in] */ VSS_PWSZ pwszVolumeName,
    /* [out] */ IVssEnumMgmtObject **ppEnum);


void __RPC_STUB IVssDifferentialSoftwareSnapshotMgmt_QueryDiffAreasOnVolume_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


HRESULT STDMETHODCALLTYPE IVssDifferentialSoftwareSnapshotMgmt_QueryDiffAreasForSnapshot_Proxy( 
    IVssDifferentialSoftwareSnapshotMgmt * This,
    /* [in] */ VSS_ID SnapshotId,
    /* [out] */ IVssEnumMgmtObject **ppEnum);


void __RPC_STUB IVssDifferentialSoftwareSnapshotMgmt_QueryDiffAreasForSnapshot_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVssDifferentialSoftwareSnapshotMgmt_INTERFACE_DEFINED__ */


#ifndef __IVssEnumMgmtObject_INTERFACE_DEFINED__
#define __IVssEnumMgmtObject_INTERFACE_DEFINED__

/* interface IVssEnumMgmtObject */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IVssEnumMgmtObject;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("01954E6B-9254-4e6e-808C-C9E05D007696")
    IVssEnumMgmtObject : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE Next( 
            /* [in] */ ULONG celt,
            /* [length_is][size_is][out] */ VSS_MGMT_OBJECT_PROP *rgelt,
            /* [out] */ ULONG *pceltFetched) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Skip( 
            /* [in] */ ULONG celt) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Reset( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Clone( 
            /* [out][in] */ IVssEnumMgmtObject **ppenum) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVssEnumMgmtObjectVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVssEnumMgmtObject * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVssEnumMgmtObject * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVssEnumMgmtObject * This);
        
        HRESULT ( STDMETHODCALLTYPE *Next )( 
            IVssEnumMgmtObject * This,
            /* [in] */ ULONG celt,
            /* [length_is][size_is][out] */ VSS_MGMT_OBJECT_PROP *rgelt,
            /* [out] */ ULONG *pceltFetched);
        
        HRESULT ( STDMETHODCALLTYPE *Skip )( 
            IVssEnumMgmtObject * This,
            /* [in] */ ULONG celt);
        
        HRESULT ( STDMETHODCALLTYPE *Reset )( 
            IVssEnumMgmtObject * This);
        
        HRESULT ( STDMETHODCALLTYPE *Clone )( 
            IVssEnumMgmtObject * This,
            /* [out][in] */ IVssEnumMgmtObject **ppenum);
        
        END_INTERFACE
    } IVssEnumMgmtObjectVtbl;

    interface IVssEnumMgmtObject
    {
        CONST_VTBL struct IVssEnumMgmtObjectVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVssEnumMgmtObject_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVssEnumMgmtObject_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVssEnumMgmtObject_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVssEnumMgmtObject_Next(This,celt,rgelt,pceltFetched)	\
    (This)->lpVtbl -> Next(This,celt,rgelt,pceltFetched)

#define IVssEnumMgmtObject_Skip(This,celt)	\
    (This)->lpVtbl -> Skip(This,celt)

#define IVssEnumMgmtObject_Reset(This)	\
    (This)->lpVtbl -> Reset(This)

#define IVssEnumMgmtObject_Clone(This,ppenum)	\
    (This)->lpVtbl -> Clone(This,ppenum)

#endif /* COBJMACROS */


#endif 	/* C style interface */



HRESULT STDMETHODCALLTYPE IVssEnumMgmtObject_Next_Proxy( 
    IVssEnumMgmtObject * This,
    /* [in] */ ULONG celt,
    /* [length_is][size_is][out] */ VSS_MGMT_OBJECT_PROP *rgelt,
    /* [out] */ ULONG *pceltFetched);


void __RPC_STUB IVssEnumMgmtObject_Next_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


HRESULT STDMETHODCALLTYPE IVssEnumMgmtObject_Skip_Proxy( 
    IVssEnumMgmtObject * This,
    /* [in] */ ULONG celt);


void __RPC_STUB IVssEnumMgmtObject_Skip_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


HRESULT STDMETHODCALLTYPE IVssEnumMgmtObject_Reset_Proxy( 
    IVssEnumMgmtObject * This);


void __RPC_STUB IVssEnumMgmtObject_Reset_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


HRESULT STDMETHODCALLTYPE IVssEnumMgmtObject_Clone_Proxy( 
    IVssEnumMgmtObject * This,
    /* [out][in] */ IVssEnumMgmtObject **ppenum);


void __RPC_STUB IVssEnumMgmtObject_Clone_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVssEnumMgmtObject_INTERFACE_DEFINED__ */



#ifndef __VSMGMT_LIBRARY_DEFINED__
#define __VSMGMT_LIBRARY_DEFINED__

/* library VSMGMT */
/* [helpstring][version][uuid] */ 


EXTERN_C const IID LIBID_VSMGMT;

EXTERN_C const CLSID CLSID_VssSnapshotMgmt;

#ifdef __cplusplus

class DECLSPEC_UUID("0B5A2C52-3EB9-470a-96E2-6C6D4570E40F")
VssSnapshotMgmt;
#endif
#endif /* __VSMGMT_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


