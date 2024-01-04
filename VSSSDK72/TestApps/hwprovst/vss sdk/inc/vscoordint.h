

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 6.00.0366 */
/* Compiler settings for vscoordint.idl:
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

#ifndef __vscoordint_h__
#define __vscoordint_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __IVssCoordinator_FWD_DEFINED__
#define __IVssCoordinator_FWD_DEFINED__
typedef interface IVssCoordinator IVssCoordinator;
#endif 	/* __IVssCoordinator_FWD_DEFINED__ */


#ifndef __IVssRemoteCoordinator_FWD_DEFINED__
#define __IVssRemoteCoordinator_FWD_DEFINED__
typedef interface IVssRemoteCoordinator IVssRemoteCoordinator;
#endif 	/* __IVssRemoteCoordinator_FWD_DEFINED__ */


#ifndef __IVssRemsnapCoordinatorInternal_FWD_DEFINED__
#define __IVssRemsnapCoordinatorInternal_FWD_DEFINED__
typedef interface IVssRemsnapCoordinatorInternal IVssRemsnapCoordinatorInternal;
#endif 	/* __IVssRemsnapCoordinatorInternal_FWD_DEFINED__ */


#ifndef __IVssShim_FWD_DEFINED__
#define __IVssShim_FWD_DEFINED__
typedef interface IVssShim IVssShim;
#endif 	/* __IVssShim_FWD_DEFINED__ */


#ifndef __IVssAdmin_FWD_DEFINED__
#define __IVssAdmin_FWD_DEFINED__
typedef interface IVssAdmin IVssAdmin;
#endif 	/* __IVssAdmin_FWD_DEFINED__ */


#ifndef __VSSCoordinator_FWD_DEFINED__
#define __VSSCoordinator_FWD_DEFINED__

#ifdef __cplusplus
typedef class VSSCoordinator VSSCoordinator;
#else
typedef struct VSSCoordinator VSSCoordinator;
#endif /* __cplusplus */

#endif 	/* __VSSCoordinator_FWD_DEFINED__ */


#ifndef __VSSRemoteCoordinator_FWD_DEFINED__
#define __VSSRemoteCoordinator_FWD_DEFINED__

#ifdef __cplusplus
typedef class VSSRemoteCoordinator VSSRemoteCoordinator;
#else
typedef struct VSSRemoteCoordinator VSSRemoteCoordinator;
#endif /* __cplusplus */

#endif 	/* __VSSRemoteCoordinator_FWD_DEFINED__ */


/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"
#include "vss.h"

#ifdef __cplusplus
extern "C"{
#endif 

void * __RPC_USER MIDL_user_allocate(size_t);
void __RPC_USER MIDL_user_free( void * ); 

#ifndef __IVssCoordinator_INTERFACE_DEFINED__
#define __IVssCoordinator_INTERFACE_DEFINED__

/* interface IVssCoordinator */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IVssCoordinator;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("da9f41d4-1a5d-41d0-a614-6dfd78df5d05")
    IVssCoordinator : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetContext( 
            /* [in] */ LONG lContext) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE StartSnapshotSet( 
            /* [out] */ VSS_ID *pSnapshotSetId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE AddToSnapshotSet( 
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [in] */ VSS_ID ProviderId,
            /* [out] */ VSS_ID *pSnapshotId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE DoSnapshotSet( 
            /* [in] */ IDispatch *pWriterCallback,
            /* [out] */ IVssAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetSnapshotProperties( 
            /* [in] */ VSS_ID SnapshotId,
            /* [out] */ VSS_SNAPSHOT_PROP *pProp) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE ExposeSnapshot( 
            /* [in] */ VSS_ID SnapshotId,
            /* [in] */ VSS_PWSZ wszPathFromRoot,
            /* [in] */ LONG lAttributes,
            /* [in] */ VSS_PWSZ wszExpose,
            /* [out] */ VSS_PWSZ *pwszExposed) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE ImportSnapshots( 
            /* [in] */ BSTR bstrXMLSnapshotSet,
            /* [out] */ IVssAsync **ppAsync) = 0;
        
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
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE BreakSnapshotSet( 
            /* [in] */ VSS_ID SnapshotSetId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE RevertToSnapshot( 
            /* [in] */ VSS_ID SnapshotId,
            /* [in] */ BOOL bForceDismount) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryRevertStatus( 
            /* [in] */ VSS_PWSZ pwszVolume,
            /* [out] */ IVssAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE IsVolumeSupported( 
            /* [in] */ VSS_ID ProviderId,
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [out] */ BOOL *pbSupportedByThisProvider) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE IsVolumeSnapshotted( 
            /* [in] */ VSS_ID ProviderId,
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [out] */ BOOL *pbSnapshotsPresent,
            /* [out] */ LONG *plSnapshotCompatibility) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetWriterInstances( 
            /* [in] */ LONG lWriterInstanceIdCount,
            /* [size_is][unique][in] */ VSS_ID *rgWriterInstanceId) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVssCoordinatorVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVssCoordinator * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVssCoordinator * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVssCoordinator * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetContext )( 
            IVssCoordinator * This,
            /* [in] */ LONG lContext);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *StartSnapshotSet )( 
            IVssCoordinator * This,
            /* [out] */ VSS_ID *pSnapshotSetId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *AddToSnapshotSet )( 
            IVssCoordinator * This,
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [in] */ VSS_ID ProviderId,
            /* [out] */ VSS_ID *pSnapshotId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *DoSnapshotSet )( 
            IVssCoordinator * This,
            /* [in] */ IDispatch *pWriterCallback,
            /* [out] */ IVssAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetSnapshotProperties )( 
            IVssCoordinator * This,
            /* [in] */ VSS_ID SnapshotId,
            /* [out] */ VSS_SNAPSHOT_PROP *pProp);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *ExposeSnapshot )( 
            IVssCoordinator * This,
            /* [in] */ VSS_ID SnapshotId,
            /* [in] */ VSS_PWSZ wszPathFromRoot,
            /* [in] */ LONG lAttributes,
            /* [in] */ VSS_PWSZ wszExpose,
            /* [out] */ VSS_PWSZ *pwszExposed);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *ImportSnapshots )( 
            IVssCoordinator * This,
            /* [in] */ BSTR bstrXMLSnapshotSet,
            /* [out] */ IVssAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Query )( 
            IVssCoordinator * This,
            /* [in] */ VSS_ID QueriedObjectId,
            /* [in] */ VSS_OBJECT_TYPE eQueriedObjectType,
            /* [in] */ VSS_OBJECT_TYPE eReturnedObjectsType,
            /* [out] */ IVssEnumObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *DeleteSnapshots )( 
            IVssCoordinator * This,
            /* [in] */ VSS_ID SourceObjectId,
            /* [in] */ VSS_OBJECT_TYPE eSourceObjectType,
            /* [in] */ BOOL bForceDelete,
            /* [out] */ LONG *plDeletedSnapshots,
            /* [out] */ VSS_ID *pNondeletedSnapshotID);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *BreakSnapshotSet )( 
            IVssCoordinator * This,
            /* [in] */ VSS_ID SnapshotSetId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *RevertToSnapshot )( 
            IVssCoordinator * This,
            /* [in] */ VSS_ID SnapshotId,
            /* [in] */ BOOL bForceDismount);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryRevertStatus )( 
            IVssCoordinator * This,
            /* [in] */ VSS_PWSZ pwszVolume,
            /* [out] */ IVssAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *IsVolumeSupported )( 
            IVssCoordinator * This,
            /* [in] */ VSS_ID ProviderId,
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [out] */ BOOL *pbSupportedByThisProvider);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *IsVolumeSnapshotted )( 
            IVssCoordinator * This,
            /* [in] */ VSS_ID ProviderId,
            /* [in] */ VSS_PWSZ pwszVolumeName,
            /* [out] */ BOOL *pbSnapshotsPresent,
            /* [out] */ LONG *plSnapshotCompatibility);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetWriterInstances )( 
            IVssCoordinator * This,
            /* [in] */ LONG lWriterInstanceIdCount,
            /* [size_is][unique][in] */ VSS_ID *rgWriterInstanceId);
        
        END_INTERFACE
    } IVssCoordinatorVtbl;

    interface IVssCoordinator
    {
        CONST_VTBL struct IVssCoordinatorVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVssCoordinator_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVssCoordinator_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVssCoordinator_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVssCoordinator_SetContext(This,lContext)	\
    (This)->lpVtbl -> SetContext(This,lContext)

#define IVssCoordinator_StartSnapshotSet(This,pSnapshotSetId)	\
    (This)->lpVtbl -> StartSnapshotSet(This,pSnapshotSetId)

#define IVssCoordinator_AddToSnapshotSet(This,pwszVolumeName,ProviderId,pSnapshotId)	\
    (This)->lpVtbl -> AddToSnapshotSet(This,pwszVolumeName,ProviderId,pSnapshotId)

#define IVssCoordinator_DoSnapshotSet(This,pWriterCallback,ppAsync)	\
    (This)->lpVtbl -> DoSnapshotSet(This,pWriterCallback,ppAsync)

#define IVssCoordinator_GetSnapshotProperties(This,SnapshotId,pProp)	\
    (This)->lpVtbl -> GetSnapshotProperties(This,SnapshotId,pProp)

#define IVssCoordinator_ExposeSnapshot(This,SnapshotId,wszPathFromRoot,lAttributes,wszExpose,pwszExposed)	\
    (This)->lpVtbl -> ExposeSnapshot(This,SnapshotId,wszPathFromRoot,lAttributes,wszExpose,pwszExposed)

#define IVssCoordinator_ImportSnapshots(This,bstrXMLSnapshotSet,ppAsync)	\
    (This)->lpVtbl -> ImportSnapshots(This,bstrXMLSnapshotSet,ppAsync)

#define IVssCoordinator_Query(This,QueriedObjectId,eQueriedObjectType,eReturnedObjectsType,ppEnum)	\
    (This)->lpVtbl -> Query(This,QueriedObjectId,eQueriedObjectType,eReturnedObjectsType,ppEnum)

#define IVssCoordinator_DeleteSnapshots(This,SourceObjectId,eSourceObjectType,bForceDelete,plDeletedSnapshots,pNondeletedSnapshotID)	\
    (This)->lpVtbl -> DeleteSnapshots(This,SourceObjectId,eSourceObjectType,bForceDelete,plDeletedSnapshots,pNondeletedSnapshotID)

#define IVssCoordinator_BreakSnapshotSet(This,SnapshotSetId)	\
    (This)->lpVtbl -> BreakSnapshotSet(This,SnapshotSetId)

#define IVssCoordinator_RevertToSnapshot(This,SnapshotId,bForceDismount)	\
    (This)->lpVtbl -> RevertToSnapshot(This,SnapshotId,bForceDismount)

#define IVssCoordinator_QueryRevertStatus(This,pwszVolume,ppAsync)	\
    (This)->lpVtbl -> QueryRevertStatus(This,pwszVolume,ppAsync)

#define IVssCoordinator_IsVolumeSupported(This,ProviderId,pwszVolumeName,pbSupportedByThisProvider)	\
    (This)->lpVtbl -> IsVolumeSupported(This,ProviderId,pwszVolumeName,pbSupportedByThisProvider)

#define IVssCoordinator_IsVolumeSnapshotted(This,ProviderId,pwszVolumeName,pbSnapshotsPresent,plSnapshotCompatibility)	\
    (This)->lpVtbl -> IsVolumeSnapshotted(This,ProviderId,pwszVolumeName,pbSnapshotsPresent,plSnapshotCompatibility)

#define IVssCoordinator_SetWriterInstances(This,lWriterInstanceIdCount,rgWriterInstanceId)	\
    (This)->lpVtbl -> SetWriterInstances(This,lWriterInstanceIdCount,rgWriterInstanceId)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssCoordinator_SetContext_Proxy( 
    IVssCoordinator * This,
    /* [in] */ LONG lContext);


void __RPC_STUB IVssCoordinator_SetContext_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssCoordinator_StartSnapshotSet_Proxy( 
    IVssCoordinator * This,
    /* [out] */ VSS_ID *pSnapshotSetId);


void __RPC_STUB IVssCoordinator_StartSnapshotSet_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssCoordinator_AddToSnapshotSet_Proxy( 
    IVssCoordinator * This,
    /* [in] */ VSS_PWSZ pwszVolumeName,
    /* [in] */ VSS_ID ProviderId,
    /* [out] */ VSS_ID *pSnapshotId);


void __RPC_STUB IVssCoordinator_AddToSnapshotSet_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssCoordinator_DoSnapshotSet_Proxy( 
    IVssCoordinator * This,
    /* [in] */ IDispatch *pWriterCallback,
    /* [out] */ IVssAsync **ppAsync);


void __RPC_STUB IVssCoordinator_DoSnapshotSet_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssCoordinator_GetSnapshotProperties_Proxy( 
    IVssCoordinator * This,
    /* [in] */ VSS_ID SnapshotId,
    /* [out] */ VSS_SNAPSHOT_PROP *pProp);


void __RPC_STUB IVssCoordinator_GetSnapshotProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssCoordinator_ExposeSnapshot_Proxy( 
    IVssCoordinator * This,
    /* [in] */ VSS_ID SnapshotId,
    /* [in] */ VSS_PWSZ wszPathFromRoot,
    /* [in] */ LONG lAttributes,
    /* [in] */ VSS_PWSZ wszExpose,
    /* [out] */ VSS_PWSZ *pwszExposed);


void __RPC_STUB IVssCoordinator_ExposeSnapshot_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssCoordinator_ImportSnapshots_Proxy( 
    IVssCoordinator * This,
    /* [in] */ BSTR bstrXMLSnapshotSet,
    /* [out] */ IVssAsync **ppAsync);


void __RPC_STUB IVssCoordinator_ImportSnapshots_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssCoordinator_Query_Proxy( 
    IVssCoordinator * This,
    /* [in] */ VSS_ID QueriedObjectId,
    /* [in] */ VSS_OBJECT_TYPE eQueriedObjectType,
    /* [in] */ VSS_OBJECT_TYPE eReturnedObjectsType,
    /* [out] */ IVssEnumObject **ppEnum);


void __RPC_STUB IVssCoordinator_Query_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssCoordinator_DeleteSnapshots_Proxy( 
    IVssCoordinator * This,
    /* [in] */ VSS_ID SourceObjectId,
    /* [in] */ VSS_OBJECT_TYPE eSourceObjectType,
    /* [in] */ BOOL bForceDelete,
    /* [out] */ LONG *plDeletedSnapshots,
    /* [out] */ VSS_ID *pNondeletedSnapshotID);


void __RPC_STUB IVssCoordinator_DeleteSnapshots_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssCoordinator_BreakSnapshotSet_Proxy( 
    IVssCoordinator * This,
    /* [in] */ VSS_ID SnapshotSetId);


void __RPC_STUB IVssCoordinator_BreakSnapshotSet_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssCoordinator_RevertToSnapshot_Proxy( 
    IVssCoordinator * This,
    /* [in] */ VSS_ID SnapshotId,
    /* [in] */ BOOL bForceDismount);


void __RPC_STUB IVssCoordinator_RevertToSnapshot_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssCoordinator_QueryRevertStatus_Proxy( 
    IVssCoordinator * This,
    /* [in] */ VSS_PWSZ pwszVolume,
    /* [out] */ IVssAsync **ppAsync);


void __RPC_STUB IVssCoordinator_QueryRevertStatus_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssCoordinator_IsVolumeSupported_Proxy( 
    IVssCoordinator * This,
    /* [in] */ VSS_ID ProviderId,
    /* [in] */ VSS_PWSZ pwszVolumeName,
    /* [out] */ BOOL *pbSupportedByThisProvider);


void __RPC_STUB IVssCoordinator_IsVolumeSupported_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssCoordinator_IsVolumeSnapshotted_Proxy( 
    IVssCoordinator * This,
    /* [in] */ VSS_ID ProviderId,
    /* [in] */ VSS_PWSZ pwszVolumeName,
    /* [out] */ BOOL *pbSnapshotsPresent,
    /* [out] */ LONG *plSnapshotCompatibility);


void __RPC_STUB IVssCoordinator_IsVolumeSnapshotted_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssCoordinator_SetWriterInstances_Proxy( 
    IVssCoordinator * This,
    /* [in] */ LONG lWriterInstanceIdCount,
    /* [size_is][unique][in] */ VSS_ID *rgWriterInstanceId);


void __RPC_STUB IVssCoordinator_SetWriterInstances_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVssCoordinator_INTERFACE_DEFINED__ */


#ifndef __IVssRemoteCoordinator_INTERFACE_DEFINED__
#define __IVssRemoteCoordinator_INTERFACE_DEFINED__

/* interface IVssRemoteCoordinator */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IVssRemoteCoordinator;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("BF5AEED7-92BA-4484-97BE-EF7DA4825680")
    IVssRemoteCoordinator : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SetContext( 
            /* [in] */ LONG lContext) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE StartSnapshotSet( 
            /* [in] */ VSS_PWSZ pwszMachineName,
            /* [in] */ VSS_ID SnapshotSetId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE AddToSnapshotSet( 
            /* [in] */ VSS_PWSZ pwszShareName,
            /* [in] */ VSS_ID ProviderId,
            /* [out] */ VSS_ID *pSnapshotId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE EndprepareAllSnapshots( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE DoSnapshotSet( 
            /* [in] */ LONG lTotalSnapshotsCount,
            /* [out] */ IVssAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetSnapshotProperties( 
            /* [in] */ VSS_ID SnapshotId,
            /* [out] */ VSS_SNAPSHOT_PROP *pProp) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE ExposeSnapshot( 
            /* [in] */ VSS_ID SnapshotId,
            /* [in] */ VSS_PWSZ wszPathFromRoot,
            /* [in] */ LONG lContext,
            /* [in] */ VSS_PWSZ wszExpose,
            /* [out] */ VSS_PWSZ *pwszExposed) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Query( 
            /* [in] */ VSS_PWSZ pwszObjectName,
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
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE BreakSnapshotSet( 
            /* [in] */ VSS_ID SnapshotSetId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE IsShareSupported( 
            /* [in] */ VSS_ID ProviderId,
            /* [in] */ VSS_PWSZ pwszShareName,
            /* [out] */ BOOL *pbSupportedByThisProvider) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE IsShareSnapshotted( 
            /* [in] */ VSS_ID ProviderId,
            /* [in] */ VSS_PWSZ pwszShareName,
            /* [out] */ BOOL *pbSnapshotsPresent,
            /* [out] */ LONG *plSnapshotCompatibility) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVssRemoteCoordinatorVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVssRemoteCoordinator * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVssRemoteCoordinator * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVssRemoteCoordinator * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SetContext )( 
            IVssRemoteCoordinator * This,
            /* [in] */ LONG lContext);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *StartSnapshotSet )( 
            IVssRemoteCoordinator * This,
            /* [in] */ VSS_PWSZ pwszMachineName,
            /* [in] */ VSS_ID SnapshotSetId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *AddToSnapshotSet )( 
            IVssRemoteCoordinator * This,
            /* [in] */ VSS_PWSZ pwszShareName,
            /* [in] */ VSS_ID ProviderId,
            /* [out] */ VSS_ID *pSnapshotId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *EndprepareAllSnapshots )( 
            IVssRemoteCoordinator * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *DoSnapshotSet )( 
            IVssRemoteCoordinator * This,
            /* [in] */ LONG lTotalSnapshotsCount,
            /* [out] */ IVssAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetSnapshotProperties )( 
            IVssRemoteCoordinator * This,
            /* [in] */ VSS_ID SnapshotId,
            /* [out] */ VSS_SNAPSHOT_PROP *pProp);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *ExposeSnapshot )( 
            IVssRemoteCoordinator * This,
            /* [in] */ VSS_ID SnapshotId,
            /* [in] */ VSS_PWSZ wszPathFromRoot,
            /* [in] */ LONG lContext,
            /* [in] */ VSS_PWSZ wszExpose,
            /* [out] */ VSS_PWSZ *pwszExposed);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Query )( 
            IVssRemoteCoordinator * This,
            /* [in] */ VSS_PWSZ pwszObjectName,
            /* [in] */ VSS_ID QueriedObjectId,
            /* [in] */ VSS_OBJECT_TYPE eQueriedObjectType,
            /* [in] */ VSS_OBJECT_TYPE eReturnedObjectsType,
            /* [out] */ IVssEnumObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *DeleteSnapshots )( 
            IVssRemoteCoordinator * This,
            /* [in] */ VSS_ID SourceObjectId,
            /* [in] */ VSS_OBJECT_TYPE eSourceObjectType,
            /* [in] */ BOOL bForceDelete,
            /* [out] */ LONG *plDeletedSnapshots,
            /* [out] */ VSS_ID *pNondeletedSnapshotID);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *BreakSnapshotSet )( 
            IVssRemoteCoordinator * This,
            /* [in] */ VSS_ID SnapshotSetId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *IsShareSupported )( 
            IVssRemoteCoordinator * This,
            /* [in] */ VSS_ID ProviderId,
            /* [in] */ VSS_PWSZ pwszShareName,
            /* [out] */ BOOL *pbSupportedByThisProvider);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *IsShareSnapshotted )( 
            IVssRemoteCoordinator * This,
            /* [in] */ VSS_ID ProviderId,
            /* [in] */ VSS_PWSZ pwszShareName,
            /* [out] */ BOOL *pbSnapshotsPresent,
            /* [out] */ LONG *plSnapshotCompatibility);
        
        END_INTERFACE
    } IVssRemoteCoordinatorVtbl;

    interface IVssRemoteCoordinator
    {
        CONST_VTBL struct IVssRemoteCoordinatorVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVssRemoteCoordinator_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVssRemoteCoordinator_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVssRemoteCoordinator_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVssRemoteCoordinator_SetContext(This,lContext)	\
    (This)->lpVtbl -> SetContext(This,lContext)

#define IVssRemoteCoordinator_StartSnapshotSet(This,pwszMachineName,SnapshotSetId)	\
    (This)->lpVtbl -> StartSnapshotSet(This,pwszMachineName,SnapshotSetId)

#define IVssRemoteCoordinator_AddToSnapshotSet(This,pwszShareName,ProviderId,pSnapshotId)	\
    (This)->lpVtbl -> AddToSnapshotSet(This,pwszShareName,ProviderId,pSnapshotId)

#define IVssRemoteCoordinator_EndprepareAllSnapshots(This)	\
    (This)->lpVtbl -> EndprepareAllSnapshots(This)

#define IVssRemoteCoordinator_DoSnapshotSet(This,lTotalSnapshotsCount,ppAsync)	\
    (This)->lpVtbl -> DoSnapshotSet(This,lTotalSnapshotsCount,ppAsync)

#define IVssRemoteCoordinator_GetSnapshotProperties(This,SnapshotId,pProp)	\
    (This)->lpVtbl -> GetSnapshotProperties(This,SnapshotId,pProp)

#define IVssRemoteCoordinator_ExposeSnapshot(This,SnapshotId,wszPathFromRoot,lContext,wszExpose,pwszExposed)	\
    (This)->lpVtbl -> ExposeSnapshot(This,SnapshotId,wszPathFromRoot,lContext,wszExpose,pwszExposed)

#define IVssRemoteCoordinator_Query(This,pwszObjectName,QueriedObjectId,eQueriedObjectType,eReturnedObjectsType,ppEnum)	\
    (This)->lpVtbl -> Query(This,pwszObjectName,QueriedObjectId,eQueriedObjectType,eReturnedObjectsType,ppEnum)

#define IVssRemoteCoordinator_DeleteSnapshots(This,SourceObjectId,eSourceObjectType,bForceDelete,plDeletedSnapshots,pNondeletedSnapshotID)	\
    (This)->lpVtbl -> DeleteSnapshots(This,SourceObjectId,eSourceObjectType,bForceDelete,plDeletedSnapshots,pNondeletedSnapshotID)

#define IVssRemoteCoordinator_BreakSnapshotSet(This,SnapshotSetId)	\
    (This)->lpVtbl -> BreakSnapshotSet(This,SnapshotSetId)

#define IVssRemoteCoordinator_IsShareSupported(This,ProviderId,pwszShareName,pbSupportedByThisProvider)	\
    (This)->lpVtbl -> IsShareSupported(This,ProviderId,pwszShareName,pbSupportedByThisProvider)

#define IVssRemoteCoordinator_IsShareSnapshotted(This,ProviderId,pwszShareName,pbSnapshotsPresent,plSnapshotCompatibility)	\
    (This)->lpVtbl -> IsShareSnapshotted(This,ProviderId,pwszShareName,pbSnapshotsPresent,plSnapshotCompatibility)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssRemoteCoordinator_SetContext_Proxy( 
    IVssRemoteCoordinator * This,
    /* [in] */ LONG lContext);


void __RPC_STUB IVssRemoteCoordinator_SetContext_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssRemoteCoordinator_StartSnapshotSet_Proxy( 
    IVssRemoteCoordinator * This,
    /* [in] */ VSS_PWSZ pwszMachineName,
    /* [in] */ VSS_ID SnapshotSetId);


void __RPC_STUB IVssRemoteCoordinator_StartSnapshotSet_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssRemoteCoordinator_AddToSnapshotSet_Proxy( 
    IVssRemoteCoordinator * This,
    /* [in] */ VSS_PWSZ pwszShareName,
    /* [in] */ VSS_ID ProviderId,
    /* [out] */ VSS_ID *pSnapshotId);


void __RPC_STUB IVssRemoteCoordinator_AddToSnapshotSet_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssRemoteCoordinator_EndprepareAllSnapshots_Proxy( 
    IVssRemoteCoordinator * This);


void __RPC_STUB IVssRemoteCoordinator_EndprepareAllSnapshots_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssRemoteCoordinator_DoSnapshotSet_Proxy( 
    IVssRemoteCoordinator * This,
    /* [in] */ LONG lTotalSnapshotsCount,
    /* [out] */ IVssAsync **ppAsync);


void __RPC_STUB IVssRemoteCoordinator_DoSnapshotSet_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssRemoteCoordinator_GetSnapshotProperties_Proxy( 
    IVssRemoteCoordinator * This,
    /* [in] */ VSS_ID SnapshotId,
    /* [out] */ VSS_SNAPSHOT_PROP *pProp);


void __RPC_STUB IVssRemoteCoordinator_GetSnapshotProperties_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssRemoteCoordinator_ExposeSnapshot_Proxy( 
    IVssRemoteCoordinator * This,
    /* [in] */ VSS_ID SnapshotId,
    /* [in] */ VSS_PWSZ wszPathFromRoot,
    /* [in] */ LONG lContext,
    /* [in] */ VSS_PWSZ wszExpose,
    /* [out] */ VSS_PWSZ *pwszExposed);


void __RPC_STUB IVssRemoteCoordinator_ExposeSnapshot_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssRemoteCoordinator_Query_Proxy( 
    IVssRemoteCoordinator * This,
    /* [in] */ VSS_PWSZ pwszObjectName,
    /* [in] */ VSS_ID QueriedObjectId,
    /* [in] */ VSS_OBJECT_TYPE eQueriedObjectType,
    /* [in] */ VSS_OBJECT_TYPE eReturnedObjectsType,
    /* [out] */ IVssEnumObject **ppEnum);


void __RPC_STUB IVssRemoteCoordinator_Query_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssRemoteCoordinator_DeleteSnapshots_Proxy( 
    IVssRemoteCoordinator * This,
    /* [in] */ VSS_ID SourceObjectId,
    /* [in] */ VSS_OBJECT_TYPE eSourceObjectType,
    /* [in] */ BOOL bForceDelete,
    /* [out] */ LONG *plDeletedSnapshots,
    /* [out] */ VSS_ID *pNondeletedSnapshotID);


void __RPC_STUB IVssRemoteCoordinator_DeleteSnapshots_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssRemoteCoordinator_BreakSnapshotSet_Proxy( 
    IVssRemoteCoordinator * This,
    /* [in] */ VSS_ID SnapshotSetId);


void __RPC_STUB IVssRemoteCoordinator_BreakSnapshotSet_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssRemoteCoordinator_IsShareSupported_Proxy( 
    IVssRemoteCoordinator * This,
    /* [in] */ VSS_ID ProviderId,
    /* [in] */ VSS_PWSZ pwszShareName,
    /* [out] */ BOOL *pbSupportedByThisProvider);


void __RPC_STUB IVssRemoteCoordinator_IsShareSupported_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssRemoteCoordinator_IsShareSnapshotted_Proxy( 
    IVssRemoteCoordinator * This,
    /* [in] */ VSS_ID ProviderId,
    /* [in] */ VSS_PWSZ pwszShareName,
    /* [out] */ BOOL *pbSnapshotsPresent,
    /* [out] */ LONG *plSnapshotCompatibility);


void __RPC_STUB IVssRemoteCoordinator_IsShareSnapshotted_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVssRemoteCoordinator_INTERFACE_DEFINED__ */


#ifndef __IVssRemsnapCoordinatorInternal_INTERFACE_DEFINED__
#define __IVssRemsnapCoordinatorInternal_INTERFACE_DEFINED__

/* interface IVssRemsnapCoordinatorInternal */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IVssRemsnapCoordinatorInternal;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("F2C2787D-95AB-40D4-942D-298F5F757874")
    IVssRemsnapCoordinatorInternal : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE OnSnapshotSetDone( void) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE OnSnapshotSetAbort( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVssRemsnapCoordinatorInternalVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVssRemsnapCoordinatorInternal * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVssRemsnapCoordinatorInternal * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVssRemsnapCoordinatorInternal * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *OnSnapshotSetDone )( 
            IVssRemsnapCoordinatorInternal * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *OnSnapshotSetAbort )( 
            IVssRemsnapCoordinatorInternal * This);
        
        END_INTERFACE
    } IVssRemsnapCoordinatorInternalVtbl;

    interface IVssRemsnapCoordinatorInternal
    {
        CONST_VTBL struct IVssRemsnapCoordinatorInternalVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVssRemsnapCoordinatorInternal_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVssRemsnapCoordinatorInternal_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVssRemsnapCoordinatorInternal_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVssRemsnapCoordinatorInternal_OnSnapshotSetDone(This)	\
    (This)->lpVtbl -> OnSnapshotSetDone(This)

#define IVssRemsnapCoordinatorInternal_OnSnapshotSetAbort(This)	\
    (This)->lpVtbl -> OnSnapshotSetAbort(This)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssRemsnapCoordinatorInternal_OnSnapshotSetDone_Proxy( 
    IVssRemsnapCoordinatorInternal * This);


void __RPC_STUB IVssRemsnapCoordinatorInternal_OnSnapshotSetDone_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssRemsnapCoordinatorInternal_OnSnapshotSetAbort_Proxy( 
    IVssRemsnapCoordinatorInternal * This);


void __RPC_STUB IVssRemsnapCoordinatorInternal_OnSnapshotSetAbort_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVssRemsnapCoordinatorInternal_INTERFACE_DEFINED__ */


#ifndef __IVssShim_INTERFACE_DEFINED__
#define __IVssShim_INTERFACE_DEFINED__

/* interface IVssShim */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IVssShim;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("D6222095-05C3-42f3-81D9-A4A0CEC05C26")
    IVssShim : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SimulateSnapshotFreeze( 
            /* [in] */ VSS_ID guidSnapshotSetId,
            /* [in] */ ULONG ulOptionFlags,
            /* [in] */ ULONG ulVolumeCount,
            /* [size_is][size_is][unique][in] */ VSS_PWSZ *ppwszVolumeNamesArray,
            /* [out] */ IVssAsync **ppAsync) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE SimulateSnapshotThaw( 
            /* [in] */ VSS_ID guidSnapshotSetId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE WaitForSubscribingCompletion( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVssShimVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVssShim * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVssShim * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVssShim * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SimulateSnapshotFreeze )( 
            IVssShim * This,
            /* [in] */ VSS_ID guidSnapshotSetId,
            /* [in] */ ULONG ulOptionFlags,
            /* [in] */ ULONG ulVolumeCount,
            /* [size_is][size_is][unique][in] */ VSS_PWSZ *ppwszVolumeNamesArray,
            /* [out] */ IVssAsync **ppAsync);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *SimulateSnapshotThaw )( 
            IVssShim * This,
            /* [in] */ VSS_ID guidSnapshotSetId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *WaitForSubscribingCompletion )( 
            IVssShim * This);
        
        END_INTERFACE
    } IVssShimVtbl;

    interface IVssShim
    {
        CONST_VTBL struct IVssShimVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVssShim_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVssShim_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVssShim_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVssShim_SimulateSnapshotFreeze(This,guidSnapshotSetId,ulOptionFlags,ulVolumeCount,ppwszVolumeNamesArray,ppAsync)	\
    (This)->lpVtbl -> SimulateSnapshotFreeze(This,guidSnapshotSetId,ulOptionFlags,ulVolumeCount,ppwszVolumeNamesArray,ppAsync)

#define IVssShim_SimulateSnapshotThaw(This,guidSnapshotSetId)	\
    (This)->lpVtbl -> SimulateSnapshotThaw(This,guidSnapshotSetId)

#define IVssShim_WaitForSubscribingCompletion(This)	\
    (This)->lpVtbl -> WaitForSubscribingCompletion(This)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssShim_SimulateSnapshotFreeze_Proxy( 
    IVssShim * This,
    /* [in] */ VSS_ID guidSnapshotSetId,
    /* [in] */ ULONG ulOptionFlags,
    /* [in] */ ULONG ulVolumeCount,
    /* [size_is][size_is][unique][in] */ VSS_PWSZ *ppwszVolumeNamesArray,
    /* [out] */ IVssAsync **ppAsync);


void __RPC_STUB IVssShim_SimulateSnapshotFreeze_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssShim_SimulateSnapshotThaw_Proxy( 
    IVssShim * This,
    /* [in] */ VSS_ID guidSnapshotSetId);


void __RPC_STUB IVssShim_SimulateSnapshotThaw_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssShim_WaitForSubscribingCompletion_Proxy( 
    IVssShim * This);


void __RPC_STUB IVssShim_WaitForSubscribingCompletion_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVssShim_INTERFACE_DEFINED__ */


#ifndef __IVssAdmin_INTERFACE_DEFINED__
#define __IVssAdmin_INTERFACE_DEFINED__

/* interface IVssAdmin */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IVssAdmin;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("77ED5996-2F63-11d3-8A39-00C04F72D8E3")
    IVssAdmin : public IUnknown
    {
    public:
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE RegisterProvider( 
            /* [in] */ VSS_ID pProviderId,
            /* [in] */ CLSID ClassId,
            /* [in] */ VSS_PWSZ pwszProviderName,
            /* [in] */ VSS_PROVIDER_TYPE eProviderType,
            /* [in] */ VSS_PWSZ pwszProviderVersion,
            /* [in] */ VSS_ID ProviderVersionId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE UnregisterProvider( 
            /* [in] */ VSS_ID ProviderId) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryProviders( 
            /* [out] */ IVssEnumObject **ppEnum) = 0;
        
        virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE AbortAllSnapshotsInProgress( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IVssAdminVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IVssAdmin * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IVssAdmin * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IVssAdmin * This);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *RegisterProvider )( 
            IVssAdmin * This,
            /* [in] */ VSS_ID pProviderId,
            /* [in] */ CLSID ClassId,
            /* [in] */ VSS_PWSZ pwszProviderName,
            /* [in] */ VSS_PROVIDER_TYPE eProviderType,
            /* [in] */ VSS_PWSZ pwszProviderVersion,
            /* [in] */ VSS_ID ProviderVersionId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *UnregisterProvider )( 
            IVssAdmin * This,
            /* [in] */ VSS_ID ProviderId);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *QueryProviders )( 
            IVssAdmin * This,
            /* [out] */ IVssEnumObject **ppEnum);
        
        /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *AbortAllSnapshotsInProgress )( 
            IVssAdmin * This);
        
        END_INTERFACE
    } IVssAdminVtbl;

    interface IVssAdmin
    {
        CONST_VTBL struct IVssAdminVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IVssAdmin_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IVssAdmin_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IVssAdmin_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IVssAdmin_RegisterProvider(This,pProviderId,ClassId,pwszProviderName,eProviderType,pwszProviderVersion,ProviderVersionId)	\
    (This)->lpVtbl -> RegisterProvider(This,pProviderId,ClassId,pwszProviderName,eProviderType,pwszProviderVersion,ProviderVersionId)

#define IVssAdmin_UnregisterProvider(This,ProviderId)	\
    (This)->lpVtbl -> UnregisterProvider(This,ProviderId)

#define IVssAdmin_QueryProviders(This,ppEnum)	\
    (This)->lpVtbl -> QueryProviders(This,ppEnum)

#define IVssAdmin_AbortAllSnapshotsInProgress(This)	\
    (This)->lpVtbl -> AbortAllSnapshotsInProgress(This)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssAdmin_RegisterProvider_Proxy( 
    IVssAdmin * This,
    /* [in] */ VSS_ID pProviderId,
    /* [in] */ CLSID ClassId,
    /* [in] */ VSS_PWSZ pwszProviderName,
    /* [in] */ VSS_PROVIDER_TYPE eProviderType,
    /* [in] */ VSS_PWSZ pwszProviderVersion,
    /* [in] */ VSS_ID ProviderVersionId);


void __RPC_STUB IVssAdmin_RegisterProvider_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssAdmin_UnregisterProvider_Proxy( 
    IVssAdmin * This,
    /* [in] */ VSS_ID ProviderId);


void __RPC_STUB IVssAdmin_UnregisterProvider_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssAdmin_QueryProviders_Proxy( 
    IVssAdmin * This,
    /* [out] */ IVssEnumObject **ppEnum);


void __RPC_STUB IVssAdmin_QueryProviders_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring] */ HRESULT STDMETHODCALLTYPE IVssAdmin_AbortAllSnapshotsInProgress_Proxy( 
    IVssAdmin * This);


void __RPC_STUB IVssAdmin_AbortAllSnapshotsInProgress_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IVssAdmin_INTERFACE_DEFINED__ */



#ifndef __VSS_LIBRARY_DEFINED__
#define __VSS_LIBRARY_DEFINED__

/* library VSS */
/* [helpstring][version][uuid] */ 


EXTERN_C const IID LIBID_VSS;

EXTERN_C const CLSID CLSID_VSSCoordinator;

#ifdef __cplusplus

class DECLSPEC_UUID("E579AB5F-1CC4-44b4-BED9-DE0991FF0623")
VSSCoordinator;
#endif

EXTERN_C const CLSID CLSID_VSSRemoteCoordinator;

#ifdef __cplusplus

class DECLSPEC_UUID("95243A62-2F9B-4FDF-B437-40D965F6D17F")
VSSRemoteCoordinator;
#endif
#endif /* __VSS_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

unsigned long             __RPC_USER  BSTR_UserSize(     unsigned long *, unsigned long            , BSTR * ); 
unsigned char * __RPC_USER  BSTR_UserMarshal(  unsigned long *, unsigned char *, BSTR * ); 
unsigned char * __RPC_USER  BSTR_UserUnmarshal(unsigned long *, unsigned char *, BSTR * ); 
void                      __RPC_USER  BSTR_UserFree(     unsigned long *, BSTR * ); 

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


