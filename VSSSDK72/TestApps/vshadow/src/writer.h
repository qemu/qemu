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


// The type of a file descriptor
typedef enum 
{
    VSS_FDT_UNDEFINED = 0,
    VSS_FDT_EXCLUDE_FILES,
    VSS_FDT_FILELIST,
    VSS_FDT_DATABASE,
    VSS_FDT_DATABASE_LOG,
} VSS_DESCRIPTOR_TYPE;


//////////////////////////////////////////////////////////////////////////////////////
// In-memory representation of a file descriptor
//

struct VssFileDescriptor
{
    VssFileDescriptor(): 
        isRecursive(false), 
        type(VSS_FDT_UNDEFINED)
        {};

    // Initialize from a IVssWMFiledesc
    void Initialize(
        IVssWMFiledesc * pFileDesc, 
        VSS_DESCRIPTOR_TYPE typeParam
        );

    // Print this file descriptor 
    void Print();

    // Get the string representation of the type
    wstring GetStringFromFileDescriptorType(VSS_DESCRIPTOR_TYPE eType);

    //
    //  Data members
    //

    wstring             path;
    wstring             filespec;
    wstring             alternatePath;
    bool                isRecursive;

    VSS_DESCRIPTOR_TYPE type;
    wstring             expandedPath;
    wstring             affectedVolume;
};



//////////////////////////////////////////////////////////////////////////////////////
// In-memory representation of a component dependency
//

#ifdef VSS_SERVER

struct VssDependency
{
    VssDependency() {};

    // Initialize from a IVssWMDependency
    void Initialize(
        IVssWMDependency * pDependency
        );

    // Print this dependency
    void Print();

    //
    //  Data members
    //

    wstring             writerId;
    wstring             logicalPath;
    wstring             componentName;
    wstring             fullPath;
};

#endif


//////////////////////////////////////////////////////////////////////////////////////
// In-memory representation of a component
//

struct VssComponent
{
    VssComponent(): 
        type(VSS_CT_UNDEFINED),
        isSelectable(false),
        notifyOnBackupComplete(false),
        isTopLevel(false),
        isExcluded(false),
        isExplicitlyIncluded(false)
        {};

    // Initialize from a IVssWMComponent
    void Initialize(wstring writerNameParam, IVssWMComponent * pComponent);

    // Initialize from a IVssComponent
    void Initialize(wstring writerNameParam, IVssComponent * pComponent);

    // Print summary/detalied information about this component
    void Print(bool bListDetailedInfo);

    // Convert a component type into a string
    wstring GetStringFromComponentType(VSS_COMPONENT_TYPE eComponentType);

    // Return TRUE if the current component is ancestor of the given component
    bool IsAncestorOf(VssComponent & child);

    // return TRUEif it can be explicitly included
    bool CanBeExplicitlyIncluded();

    //
    //  Data members
    //

    wstring             name;
    wstring             writerName;
    wstring             logicalPath;
    wstring             caption;
    VSS_COMPONENT_TYPE  type;
    bool                isSelectable;
    bool                notifyOnBackupComplete;

    wstring             fullPath;
    bool                isTopLevel;
    bool                isExcluded;
    bool                isExplicitlyIncluded;
    vector<wstring>     affectedPaths;
    vector<wstring>     affectedVolumes;
    vector<VssFileDescriptor> descriptors;

#ifdef VSS_SERVER
    vector<VssDependency> dependencies;
#endif
};


//////////////////////////////////////////////////////////////////////////////////////
// In-memory representation of a writer metadata
//

struct VssWriter
{
    VssWriter(): 
        isExcluded(false),
        supportsRestore(false),
        restoreMethod(VSS_RME_UNDEFINED),
        writerRestoreConditions(VSS_WRE_UNDEFINED),
        rebootRequiredAfterRestore(false)
        {};

    // Initialize from a IVssWMFiledesc
    void Initialize(IVssExamineWriterMetadata * pMetadata);

    // Initialize from a IVssWriterComponentsExt
    void InitializeComponentsForRestore(IVssWriterComponentsExt * pWriterComponents);

    // Print summary/detalied information about this writer
    void Print(bool bListDetailedInfo);

    wstring GetStringFromRestoreMethod(VSS_RESTOREMETHOD_ENUM eRestoreMethod);

    wstring GetStringFromRestoreConditions(VSS_WRITERRESTORE_ENUM eRestoreEnum);

    //
    //  Data members
    //

    wstring                     name;
    wstring                     id;
    wstring                     instanceId;
    vector<VssComponent>        components;
    vector<VssFileDescriptor>   excludedFiles;
    VSS_WRITERRESTORE_ENUM      writerRestoreConditions;
    bool                        supportsRestore;
    VSS_RESTOREMETHOD_ENUM      restoreMethod;
    bool                        rebootRequiredAfterRestore;

    bool                        isExcluded;
};

