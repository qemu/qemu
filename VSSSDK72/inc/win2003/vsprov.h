

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 6.00.0366 */
/* Compiler settings for vsprov.idl:
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

#ifndef __vsprov_h__
#define __vsprov_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __IVssSoftwareSnapshotProvider_FWD_DEFINED__
#define __IVssSoftwareSnapshotProvider_FWD_DEFINED__
typedef interface IVssSoftwareSnapshotProvider IVssSoftwareSnapshotProvider;
#endif 	/* __IVssSoftwareSnapshotProvider_FWD_DEFINED__ */


#ifndef __IVssProviderCreateSnapshotSet_FWD_DEFINED__
#define __IVssProviderCreateSnapshotSet_FWD_DEFINED__
typedef interface IVssProviderCreateSnapshotSet IVssProviderCreateSnapshotSet;
#endif 	/* __IVssProviderCreateSnapshotSet_FWD_DEFINED__ */


#ifndef __IVssProviderNotifications_FWD_DEFINED__
#define __IVssProviderNotifications_FWD_DEFINED__
typedef interface IVssProviderNotifications IVssProviderNotifications;
#endif 	/* __IVssProviderNotifications_FWD_DEFINED__ */


#ifndef __IVssHardwareSnapshotProvider_FWD_DEFINED__
#define __IVssHardwareSnapshotProvider_FWD_DEFINED__
typedef interface IVssHardwareSnapshotProvider IVssHardwareSnapshotProvider;
#endif 	/* __IVssHardwareSnapshotProvider_FWD_DEFINED__ */


/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"
#include "vss.h"
#include "vdslun.h"

#ifdef __cplusplus
extern "C"{
#endif 

void * __RPC_USER MIDL_user_allocate(size_t);
void __RPC_USER MIDL_user_free( void * ); 

/* interface __MIDL_itf_vsprov_0000 */
/* [local] */ 



typedef VSS_PWSZ *PVSS_PWSZ;



extern RPC_IF_HANDLE __MIDL_itf_vsprov_0000_v0_0_c_ifspec;
extern RPC_IF_HANDLE __MIDL_itf_vsprov_0000_v0_0_s_ifspec;

#ifndef __IVssSoftwareSnapshotProvider_INTERFACE_DEFINED__
#define __IVssSoftwareSnapshotProvider_INTERFACE_DEFINED__

/* interface IVssSoftwareSnapshotProvider */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IVssSoftwareSnapshotProvider;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("609e123e-2c5a-44d3-8f01-0b1d9a47d1ff")
    IVssSoftwareSnapshotProvider : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetContext( 
            /* [in] */ LONG lContext) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetSnapshotProperties( 
            /* [in] */ VSS_ID SnapshotId,
            /* [out] */ VSS_SNAPSHOT_PROP *pProp) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Query( 
            /* [in] */ VSS_ID QueriedObjectId,
            /* [in] */ VSS_OBJECT_TYPE eQueriedObjectType,
            /* [in] */ VSS_OBJECT_TYPE eReturnedObjectsType,
            /* [out] */ IVssEnumObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE DeleteSnapshots( 
            /* [in] */ VSS_ID SourceObjectId,
            /* [in] */ VSS_OBJECT_TYPE eSourceObjectType,
            /* [in] */ BOOL bForceDelete,
            /* [out] */ LONG *plDeletedSnapshots,
            /* [out] */ VSS_ID *pNondeletedSnapshotID) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE BeginPrepareSnapshot( 
            /* [in] */ VSS_ID SnapshotSetId,
            /* [in] */ VSS_ID SnapshotId,
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [in] */ LONG lNewContext) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE IsVolumeSupported( 
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [out] */ BOOL *pbSupportedByThisProvider) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE IsVolumeSnapshotted( 
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [out] */ BOOL *pbSnapshotsPresent,
            /* [out] */ LONG *plSnapshotCompatibility) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetSnapshotProperty( 
            /* [in] */ VSS_ID SnapshotId,
            /* [in] */ VSS_SNAPSHOT_PROPERTY_ID eSnapshotPropertyId,
            /* [in] */ VARIANT vProperty) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE RevertToSnapshot( 
            /* [in] */ VSS_ID SnapshotId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryRevertStatus( 
            /* [in] */ VSS_PWSZ pwszVolume,
            /* [out] */ IVssAsync **ppAsync) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVssSoftwareSnapshotProviderVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVssSoftwareSnapshotProvider * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVssSoftwareSnapshotProvider * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVssSoftwareSnapshotProvider * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetContext )( 
            IVssSoftwareSnapshotProvider * This,
            /* [in] */ LONG lContext);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetSnapshotProperties )( 
            IVssSoftwareSnapshotProvider * This,
            /* [in] */ VSS_ID SnapshotId,
            /* [out] */ VSS_SNAPSHOT_PROP *pProp);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Query )( 
            IVssSoftwareSnapshotProvider * This,
            /* [in] */ VSS_ID QueriedObjectId,
            /* [in] */ VSS_OBJECT_TYPE eQueriedObjectType,
            /* [in] */ VSS_OBJECT_TYPE eReturnedObjectsType,
            /* [out] */ IVssEnumObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *DeleteSnapshots )( 
            IVssSoftwareSnapshotProvider * This,
            /* [in] */ VSS_ID SourceObjectId,
            /* [in] */ VSS_OBJECT_TYPE eSourceObjectType,
            /* [in] */ BOOL bForceDelete,
            /* [out] */ LONG *plDeletedSnapshots,
            /* [out] */ VSS_ID *pNondeletedSnapshotID);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *BeginPrepareSnapshot )( 
            IVssSoftwareSnapshotProvider * This,
            /* [in] */ VSS_ID SnapshotSetId,
            /* [in] */ VSS_ID SnapshotId,
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [in] */ LONG lNewContext);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *IsVolumeSupported )( 
            IVssSoftwareSnapshotProvider * This,
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [out] */ BOOL *pbSupportedByThisProvider);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *IsVolumeSnapshotted )( 
            IVssSoftwareSnapshotProvider * This,
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [out] */ BOOL *pbSnapshotsPresent,
            /* [out] */ LONG *plSnapshotCompatibility);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetSnapshotProperty )( 
            IVssSoftwareSnapshotProvider * This,
            /* [in] */ VSS_ID SnapshotId,
            /* [in] */ VSS_SNAPSHOT_PROPERTY_ID eSnapshotPropertyId,
            /* [in] */ VARIANT vProperty);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *RevertToSnapshot )( 
            IVssSoftwareSnapshotProvider * This,
            /* [in] */ VSS_ID SnapshotId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryRevertStatus )( 
            IVssSoftwareSnapshotProvider * This,
            /* [in] */ VSS_PWSZ pwszVolume,
            /* [out] */ IVssAsync **ppAsync);
        
        END_INTERFACE
    } IVssSoftwareSnapshotProviderVtbl;

    interface IVssSoftwareSnapshotProvider
    {
        CONST_VTBL struct IVssSoftwareSnapshotProviderVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVssSoftwareSnapshotProvider_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVssSoftwareSnapshotProvider_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVssSoftwareSnapshotProvider_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVssSoftwareSnapshotProvider_SetContext(This,lContext)	\
    (This)->lpVtbl -> SetContext(This,lContext)

#define IVssSoftwareSnapshotProvider_GetSnapshotProperties(This,SnapshotId,pProp)	\
    (This)->lpVtbl -> GetSnapshotProperties(This,SnapshotId,pProp)

#define IVssSoftwareSnapshotProvider_Query(This,QueriedObjectId,eQueriedObjectType,eReturnedObjectsType,ppEnum)	\
    (This)->lpVtbl -> Query(This,QueriedObjectId,eQueriedObjectType,eReturnedObjectsType,ppEnum)

#define IVssSoftwareSnapshotProvider_DeleteSnapshots(This,SourceObjectId,eSourceObjectType,bForceDelete,plDeletedSnapshots,pNondeletedSnapshotID)	\
    (This)->lpVtbl -> DeleteSnapshots(This,SourceObjectId,eSourceObjectType,bForceDelete,plDeletedSnapshots,pNondeletedSnapshotID)

#define IVssSoftwareSnapshotProvider_BeginPrepareSnapshot(This,SnapshotSetId,SnapshotId,pwszVolumeName,lNewContext)	\
    (This)->lpVtbl -> BeginPrepareSnapshot(This,SnapshotSetId,SnapshotId,pwszVolumeName,lNewContext)

#define IVssSoftwareSnapshotProvider_IsVolumeSupported(This,pwszVolumeName,pbSupportedByThisProvider)	\
    (This)->lpVtbl -> IsVolumeSupported(This,pwszVolumeName,pbSupportedByThisProvider)

#define IVssSoftwareSnapshotProvider_IsVolumeSnapshotted(This,pwszVolumeName,pbSnapshotsPresent,plSnapshotCompatibility)	\
    (This)->lpVtbl -> IsVolumeSnapshotted(This,pwszVolumeName,pbSnapshotsPresent,plSnapshotCompatibility)

#define IVssSoftwareSnapshotProvider_SetSnapshotProperty(This,SnapshotId,eSnapshotPropertyId,vProperty)	\
    (This)->lpVtbl -> SetSnapshotProperty(This,SnapshotId,eSnapshotPropertyId,vProperty)

#define IVssSoftwareSnapshotProvider_RevertToSnapshot(This,SnapshotId)	\
    (This)->lpVtbl -> RevertToSnapshot(This,SnapshotId)

#define IVssSoftwareSnapshotProvider_QueryRevertStatus(This,pwszVolume,ppAsync)	\
    (This)->lpVtbl -> QueryRevertStatus(This,pwszVolume,ppAsync)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssSoftwareSnapshotProvider_SetContext_Proxy( 
    IVssSoftwareSnapshotProvider * This,
    /* [in] */ LONG lContext);


void __RPC_STUB IVssSoftwareSnapshotProvider_SetContext_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssSoftwareSnapshotProvider_GetSnapshotProperties_Proxy( 
    IVssSoftwareSnapshotProvider * This,
    /* [in] */ VSS_ID SnapshotId,
    /* [out] */ VSS_SNAPSHOT_PROP *pProp);


void __RPC_STUB IVssSoftwareSnapshotProvider_GetSnapshotProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssSoftwareSnapshotProvider_Query_Proxy( 
    IVssSoftwareSnapshotProvider * This,
    /* [in] */ VSS_ID QueriedObjectId,
    /* [in] */ VSS_OBJECT_TYPE eQueriedObjectType,
    /* [in] */ VSS_OBJECT_TYPE eReturnedObjectsType,
    /* [out] */ IVssEnumObject **ppEnum);


void __RPC_STUB IVssSoftwareSnapshotProvider_Query_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssSoftwareSnapshotProvider_DeleteSnapshots_Proxy( 
    IVssSoftwareSnapshotProvider * This,
    /* [in] */ VSS_ID SourceObjectId,
    /* [in] */ VSS_OBJECT_TYPE eSourceObjectType,
    /* [in] */ BOOL bForceDelete,
    /* [out] */ LONG *plDeletedSnapshots,
    /* [out] */ VSS_ID *pNondeletedSnapshotID);


void __RPC_STUB IVssSoftwareSnapshotProvider_DeleteSnapshots_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssSoftwareSnapshotProvider_BeginPrepareSnapshot_Proxy( 
    IVssSoftwareSnapshotProvider * This,
    /* [in] */ VSS_ID SnapshotSetId,
    /* [in] */ VSS_ID SnapshotId,
    /* [in] */ VSS_PWSZ pwszVolumeName,
    /* [in] */ LONG lNewContext);


void __RPC_STUB IVssSoftwareSnapshotProvider_BeginPrepareSnapshot_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssSoftwareSnapshotProvider_IsVolumeSupported_Proxy( 
    IVssSoftwareSnapshotProvider * This,
    /* [in] */ VSS_PWSZ pwszVolumeName,
    /* [out] */ BOOL *pbSupportedByThisProvider);


void __RPC_STUB IVssSoftwareSnapshotProvider_IsVolumeSupported_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssSoftwareSnapshotProvider_IsVolumeSnapshotted_Proxy( 
    IVssSoftwareSnapshotProvider * This,
    /* [in] */ VSS_PWSZ pwszVolumeName,
    /* [out] */ BOOL *pbSnapshotsPresent,
    /* [out] */ LONG *plSnapshotCompatibility);


void __RPC_STUB IVssSoftwareSnapshotProvider_IsVolumeSnapshotted_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssSoftwareSnapshotProvider_SetSnapshotProperty_Proxy( 
    IVssSoftwareSnapshotProvider * This,
    /* [in] */ VSS_ID SnapshotId,
    /* [in] */ VSS_SNAPSHOT_PROPERTY_ID eSnapshotPropertyId,
    /* [in] */ VARIANT vProperty);


void __RPC_STUB IVssSoftwareSnapshotProvider_SetSnapshotProperty_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssSoftwareSnapshotProvider_RevertToSnapshot_Proxy( 
    IVssSoftwareSnapshotProvider * This,
    /* [in] */ VSS_ID SnapshotId);


void __RPC_STUB IVssSoftwareSnapshotProvider_RevertToSnapshot_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssSoftwareSnapshotProvider_QueryRevertStatus_Proxy( 
    IVssSoftwareSnapshotProvider * This,
    /* [in] */ VSS_PWSZ pwszVolume,
    /* [out] */ IVssAsync **ppAsync);


void __RPC_STUB IVssSoftwareSnapshotProvider_QueryRevertStatus_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVssSoftwareSnapshotProvider_INTERFACE_DEFINED__ */


#ifndef __IVssProviderCreateSnapshotSet_INTERFACE_DEFINED__
#define __IVssProviderCreateSnapshotSet_INTERFACE_DEFINED__

/* interface IVssProviderCreateSnapshotSet */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IVssProviderCreateSnapshotSet;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("5F894E5B-1E39-4778-8E23-9ABAD9F0E08C")
    IVssProviderCreateSnapshotSet : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE EndPrepareSnapshots( 
            /* [in] */ VSS_ID SnapshotSetId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE PreCommitSnapshots( 
            /* [in] */ VSS_ID SnapshotSetId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE CommitSnapshots( 
            /* [in] */ VSS_ID SnapshotSetId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE PostCommitSnapshots( 
            /* [in] */ VSS_ID SnapshotSetId,
            /* [in] */ LONG lSnapshotsCount) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE PreFinalCommitSnapshots( 
            /* [in] */ VSS_ID SnapshotSetId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE PostFinalCommitSnapshots( 
            /* [in] */ VSS_ID SnapshotSetId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE AbortSnapshots( 
            /* [in] */ VSS_ID SnapshotSetId) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVssProviderCreateSnapshotSetVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVssProviderCreateSnapshotSet * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVssProviderCreateSnapshotSet * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVssProviderCreateSnapshotSet * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *EndPrepareSnapshots )( 
            IVssProviderCreateSnapshotSet * This,
            /* [in] */ VSS_ID SnapshotSetId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *PreCommitSnapshots )( 
            IVssProviderCreateSnapshotSet * This,
            /* [in] */ VSS_ID SnapshotSetId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *CommitSnapshots )( 
            IVssProviderCreateSnapshotSet * This,
            /* [in] */ VSS_ID SnapshotSetId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *PostCommitSnapshots )( 
            IVssProviderCreateSnapshotSet * This,
            /* [in] */ VSS_ID SnapshotSetId,
            /* [in] */ LONG lSnapshotsCount);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *PreFinalCommitSnapshots )( 
            IVssProviderCreateSnapshotSet * This,
            /* [in] */ VSS_ID SnapshotSetId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *PostFinalCommitSnapshots )( 
            IVssProviderCreateSnapshotSet * This,
            /* [in] */ VSS_ID SnapshotSetId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *AbortSnapshots )( 
            IVssProviderCreateSnapshotSet * This,
            /* [in] */ VSS_ID SnapshotSetId);
        
        END_INTERFACE
    } IVssProviderCreateSnapshotSetVtbl;

    interface IVssProviderCreateSnapshotSet
    {
        CONST_VTBL struct IVssProviderCreateSnapshotSetVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVssProviderCreateSnapshotSet_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVssProviderCreateSnapshotSet_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVssProviderCreateSnapshotSet_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVssProviderCreateSnapshotSet_EndPrepareSnapshots(This,SnapshotSetId)	\
    (This)->lpVtbl -> EndPrepareSnapshots(This,SnapshotSetId)

#define IVssProviderCreateSnapshotSet_PreCommitSnapshots(This,SnapshotSetId)	\
    (This)->lpVtbl -> PreCommitSnapshots(This,SnapshotSetId)

#define IVssProviderCreateSnapshotSet_CommitSnapshots(This,SnapshotSetId)	\
    (This)->lpVtbl -> CommitSnapshots(This,SnapshotSetId)

#define IVssProviderCreateSnapshotSet_PostCommitSnapshots(This,SnapshotSetId,lSnapshotsCount)	\
    (This)->lpVtbl -> PostCommitSnapshots(This,SnapshotSetId,lSnapshotsCount)

#define IVssProviderCreateSnapshotSet_PreFinalCommitSnapshots(This,SnapshotSetId)	\
    (This)->lpVtbl -> PreFinalCommitSnapshots(This,SnapshotSetId)

#define IVssProviderCreateSnapshotSet_PostFinalCommitSnapshots(This,SnapshotSetId)	\
    (This)->lpVtbl -> PostFinalCommitSnapshots(This,SnapshotSetId)

#define IVssProviderCreateSnapshotSet_AbortSnapshots(This,SnapshotSetId)	\
    (This)->lpVtbl -> AbortSnapshots(This,SnapshotSetId)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssProviderCreateSnapshotSet_EndPrepareSnapshots_Proxy( 
    IVssProviderCreateSnapshotSet * This,
    /* [in] */ VSS_ID SnapshotSetId);


void __RPC_STUB IVssProviderCreateSnapshotSet_EndPrepareSnapshots_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssProviderCreateSnapshotSet_PreCommitSnapshots_Proxy( 
    IVssProviderCreateSnapshotSet * This,
    /* [in] */ VSS_ID SnapshotSetId);


void __RPC_STUB IVssProviderCreateSnapshotSet_PreCommitSnapshots_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssProviderCreateSnapshotSet_CommitSnapshots_Proxy( 
    IVssProviderCreateSnapshotSet * This,
    /* [in] */ VSS_ID SnapshotSetId);


void __RPC_STUB IVssProviderCreateSnapshotSet_CommitSnapshots_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssProviderCreateSnapshotSet_PostCommitSnapshots_Proxy( 
    IVssProviderCreateSnapshotSet * This,
    /* [in] */ VSS_ID SnapshotSetId,
    /* [in] */ LONG lSnapshotsCount);


void __RPC_STUB IVssProviderCreateSnapshotSet_PostCommitSnapshots_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssProviderCreateSnapshotSet_PreFinalCommitSnapshots_Proxy( 
    IVssProviderCreateSnapshotSet * This,
    /* [in] */ VSS_ID SnapshotSetId);


void __RPC_STUB IVssProviderCreateSnapshotSet_PreFinalCommitSnapshots_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssProviderCreateSnapshotSet_PostFinalCommitSnapshots_Proxy( 
    IVssProviderCreateSnapshotSet * This,
    /* [in] */ VSS_ID SnapshotSetId);


void __RPC_STUB IVssProviderCreateSnapshotSet_PostFinalCommitSnapshots_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssProviderCreateSnapshotSet_AbortSnapshots_Proxy( 
    IVssProviderCreateSnapshotSet * This,
    /* [in] */ VSS_ID SnapshotSetId);


void __RPC_STUB IVssProviderCreateSnapshotSet_AbortSnapshots_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVssProviderCreateSnapshotSet_INTERFACE_DEFINED__ */


#ifndef __IVssProviderNotifications_INTERFACE_DEFINED__
#define __IVssProviderNotifications_INTERFACE_DEFINED__

/* interface IVssProviderNotifications */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IVssProviderNotifications;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("E561901F-03A5-4afe-86D0-72BAEECE7004")
    IVssProviderNotifications : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE OnLoad( 
            /* [unique][in] */ IUnknown *pCallback) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE OnUnload( 
            /* [in] */ BOOL bForceUnload) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVssProviderNotificationsVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVssProviderNotifications * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVssProviderNotifications * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVssProviderNotifications * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *OnLoad )( 
            IVssProviderNotifications * This,
            /* [unique][in] */ IUnknown *pCallback);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *OnUnload )( 
            IVssProviderNotifications * This,
            /* [in] */ BOOL bForceUnload);
        
        END_INTERFACE
    } IVssProviderNotificationsVtbl;

    interface IVssProviderNotifications
    {
        CONST_VTBL struct IVssProviderNotificationsVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVssProviderNotifications_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVssProviderNotifications_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVssProviderNotifications_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVssProviderNotifications_OnLoad(This,pCallback)	\
    (This)->lpVtbl -> OnLoad(This,pCallback)

#define IVssProviderNotifications_OnUnload(This,bForceUnload)	\
    (This)->lpVtbl -> OnUnload(This,bForceUnload)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssProviderNotifications_OnLoad_Proxy( 
    IVssProviderNotifications * This,
    /* [unique][in] */ IUnknown *pCallback);


void __RPC_STUB IVssProviderNotifications_OnLoad_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssProviderNotifications_OnUnload_Proxy( 
    IVssProviderNotifications * This,
    /* [in] */ BOOL bForceUnload);


void __RPC_STUB IVssProviderNotifications_OnUnload_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVssProviderNotifications_INTERFACE_DEFINED__ */


#ifndef __IVssHardwareSnapshotProvider_INTERFACE_DEFINED__
#define __IVssHardwareSnapshotProvider_INTERFACE_DEFINED__

/* interface IVssHardwareSnapshotProvider */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IVssHardwareSnapshotProvider;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("9593A157-44E9-4344-BBEB-44FBF9B06B10")
    IVssHardwareSnapshotProvider : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE AreLunsSupported( 
            /* [in] */ LONG lLunCount,
            /* [in] */ LONG lContext,
            /* [size_is][unique][in] */ VSS_PWSZ *rgwszDevices,
            /* [size_is][out][in] */ VDS_LUN_INFORMATION *pLunInformation,
            /* [out] */ BOOL *pbIsSupported) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE FillInLunInfo( 
            /* [in] */ VSS_PWSZ wszDeviceName,
            /* [out][in] */ VDS_LUN_INFORMATION *pLunInfo,
            /* [out] */ BOOL *pbIsSupported) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE BeginPrepareSnapshot( 
            /* [in] */ VSS_ID SnapshotSetId,
            /* [in] */ VSS_ID SnapshotId,
            /* [in] */ LONG lContext,
            /* [in] */ LONG lLunCount,
            /* [size_is][unique][in] */ VSS_PWSZ *rgDeviceNames,
            /* [size_is][out][in] */ VDS_LUN_INFORMATION *rgLunInformation) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetTargetLuns( 
            /* [in] */ LONG lLunCount,
            /* [size_is][unique][in] */ VSS_PWSZ *rgDeviceNames,
            /* [size_is][unique][in] */ VDS_LUN_INFORMATION *rgSourceLuns,
            /* [size_is][out][in] */ VDS_LUN_INFORMATION *rgDestinationLuns) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE LocateLuns( 
            /* [in] */ LONG lLunCount,
            /* [size_is][unique][in] */ VDS_LUN_INFORMATION *rgSourceLuns) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE OnLunEmpty( 
            /* [unique][in] */ VSS_PWSZ wszDeviceName,
            /* [unique][in] */ VDS_LUN_INFORMATION *pInformation) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVssHardwareSnapshotProviderVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVssHardwareSnapshotProvider * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVssHardwareSnapshotProvider * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVssHardwareSnapshotProvider * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *AreLunsSupported )( 
            IVssHardwareSnapshotProvider * This,
            /* [in] */ LONG lLunCount,
            /* [in] */ LONG lContext,
            /* [size_is][unique][in] */ VSS_PWSZ *rgwszDevices,
            /* [size_is][out][in] */ VDS_LUN_INFORMATION *pLunInformation,
            /* [out] */ BOOL *pbIsSupported);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *FillInLunInfo )( 
            IVssHardwareSnapshotProvider * This,
            /* [in] */ VSS_PWSZ wszDeviceName,
            /* [out][in] */ VDS_LUN_INFORMATION *pLunInfo,
            /* [out] */ BOOL *pbIsSupported);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *BeginPrepareSnapshot )( 
            IVssHardwareSnapshotProvider * This,
            /* [in] */ VSS_ID SnapshotSetId,
            /* [in] */ VSS_ID SnapshotId,
            /* [in] */ LONG lContext,
            /* [in] */ LONG lLunCount,
            /* [size_is][unique][in] */ VSS_PWSZ *rgDeviceNames,
            /* [size_is][out][in] */ VDS_LUN_INFORMATION *rgLunInformation);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetTargetLuns )( 
            IVssHardwareSnapshotProvider * This,
            /* [in] */ LONG lLunCount,
            /* [size_is][unique][in] */ VSS_PWSZ *rgDeviceNames,
            /* [size_is][unique][in] */ VDS_LUN_INFORMATION *rgSourceLuns,
            /* [size_is][out][in] */ VDS_LUN_INFORMATION *rgDestinationLuns);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *LocateLuns )( 
            IVssHardwareSnapshotProvider * This,
            /* [in] */ LONG lLunCount,
            /* [size_is][unique][in] */ VDS_LUN_INFORMATION *rgSourceLuns);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *OnLunEmpty )( 
            IVssHardwareSnapshotProvider * This,
            /* [unique][in] */ VSS_PWSZ wszDeviceName,
            /* [unique][in] */ VDS_LUN_INFORMATION *pInformation);
        
        END_INTERFACE
    } IVssHardwareSnapshotProviderVtbl;

    interface IVssHardwareSnapshotProvider
    {
        CONST_VTBL struct IVssHardwareSnapshotProviderVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVssHardwareSnapshotProvider_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVssHardwareSnapshotProvider_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVssHardwareSnapshotProvider_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVssHardwareSnapshotProvider_AreLunsSupported(This,lLunCount,lContext,rgwszDevices,pLunInformation,pbIsSupported)	\
    (This)->lpVtbl -> AreLunsSupported(This,lLunCount,lContext,rgwszDevices,pLunInformation,pbIsSupported)

#define IVssHardwareSnapshotProvider_FillInLunInfo(This,wszDeviceName,pLunInfo,pbIsSupported)	\
    (This)->lpVtbl -> FillInLunInfo(This,wszDeviceName,pLunInfo,pbIsSupported)

#define IVssHardwareSnapshotProvider_BeginPrepareSnapshot(This,SnapshotSetId,SnapshotId,lContext,lLunCount,rgDeviceNames,rgLunInformation)	\
    (This)->lpVtbl -> BeginPrepareSnapshot(This,SnapshotSetId,SnapshotId,lContext,lLunCount,rgDeviceNames,rgLunInformation)

#define IVssHardwareSnapshotProvider_GetTargetLuns(This,lLunCount,rgDeviceNames,rgSourceLuns,rgDestinationLuns)	\
    (This)->lpVtbl -> GetTargetLuns(This,lLunCount,rgDeviceNames,rgSourceLuns,rgDestinationLuns)

#define IVssHardwareSnapshotProvider_LocateLuns(This,lLunCount,rgSourceLuns)	\
    (This)->lpVtbl -> LocateLuns(This,lLunCount,rgSourceLuns)

#define IVssHardwareSnapshotProvider_OnLunEmpty(This,wszDeviceName,pInformation)	\
    (This)->lpVtbl -> OnLunEmpty(This,wszDeviceName,pInformation)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssHardwareSnapshotProvider_AreLunsSupported_Proxy( 
    IVssHardwareSnapshotProvider * This,
    /* [in] */ LONG lLunCount,
    /* [in] */ LONG lContext,
    /* [size_is][unique][in] */ VSS_PWSZ *rgwszDevices,
    /* [size_is][out][in] */ VDS_LUN_INFORMATION *pLunInformation,
    /* [out] */ BOOL *pbIsSupported);


void __RPC_STUB IVssHardwareSnapshotProvider_AreLunsSupported_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssHardwareSnapshotProvider_FillInLunInfo_Proxy( 
    IVssHardwareSnapshotProvider * This,
    /* [in] */ VSS_PWSZ wszDeviceName,
    /* [out][in] */ VDS_LUN_INFORMATION *pLunInfo,
    /* [out] */ BOOL *pbIsSupported);


void __RPC_STUB IVssHardwareSnapshotProvider_FillInLunInfo_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssHardwareSnapshotProvider_BeginPrepareSnapshot_Proxy( 
    IVssHardwareSnapshotProvider * This,
    /* [in] */ VSS_ID SnapshotSetId,
    /* [in] */ VSS_ID SnapshotId,
    /* [in] */ LONG lContext,
    /* [in] */ LONG lLunCount,
    /* [size_is][unique][in] */ VSS_PWSZ *rgDeviceNames,
    /* [size_is][out][in] */ VDS_LUN_INFORMATION *rgLunInformation);


void __RPC_STUB IVssHardwareSnapshotProvider_BeginPrepareSnapshot_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssHardwareSnapshotProvider_GetTargetLuns_Proxy( 
    IVssHardwareSnapshotProvider * This,
    /* [in] */ LONG lLunCount,
    /* [size_is][unique][in] */ VSS_PWSZ *rgDeviceNames,
    /* [size_is][unique][in] */ VDS_LUN_INFORMATION *rgSourceLuns,
    /* [size_is][out][in] */ VDS_LUN_INFORMATION *rgDestinationLuns);


void __RPC_STUB IVssHardwareSnapshotProvider_GetTargetLuns_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssHardwareSnapshotProvider_LocateLuns_Proxy( 
    IVssHardwareSnapshotProvider * This,
    /* [in] */ LONG lLunCount,
    /* [size_is][unique][in] */ VDS_LUN_INFORMATION *rgSourceLuns);


void __RPC_STUB IVssHardwareSnapshotProvider_LocateLuns_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssHardwareSnapshotProvider_OnLunEmpty_Proxy( 
    IVssHardwareSnapshotProvider * This,
    /* [unique][in] */ VSS_PWSZ wszDeviceName,
    /* [unique][in] */ VDS_LUN_INFORMATION *pInformation);


void __RPC_STUB IVssHardwareSnapshotProvider_OnLunEmpty_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVssHardwareSnapshotProvider_INTERFACE_DEFINED__ */



#ifndef __VSSProvider_LIBRARY_DEFINED__
#define __VSSProvider_LIBRARY_DEFINED__

/* library VSSProvider */
/* [helpstring][version][uuid] */ 


EXTERN_C const IID LIBID_VSSProvider;
#endif /* __VSSProvider_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

unsigned long             __RPC_USER  VARIANT_UserSize(     unsigned long *, unsigned long            , VARIANT * ); 
unsigned char * __RPC_USER  VARIANT_UserMarshal(  unsigned long *, unsigned char *, VARIANT * ); 
unsigned char * __RPC_USER  VARIANT_UserUnmarshal(unsigned long *, unsigned char *, VARIANT * ); 
void                      __RPC_USER  VARIANT_UserFree(     unsigned long *, VARIANT * ); 

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


