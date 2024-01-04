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



///////////////////////////////////////////////////////////////////
//
//  Main routine 
//
//  Return values:
//      0 - Success
//      1 - Object not found
//      2 - Runtime Error 
//      3 - Memory allocation error
//
extern "C" __cdecl wmain(int argc, WCHAR ** argv)
{
    FunctionTracer ft(DBG_INFO);
    CommandLineParser obj;

    try
    {
        ft.WriteLine(
            L"\n"
            L"VSHADOW.EXE 2.2 - Volume Shadow Copy sample client\n"
            L"Copyright (C) 2005 Microsoft Corporation. All rights reserved.\n"
            L"\n"
            );
        
        // Build the argument vector 
        vector<wstring> arguments;
        for(int i = 1; i < argc; i++)
            arguments.push_back(argv[i]);

        // Process the arguments and execute the main options
        return obj.MainRoutine(arguments);
    }
    catch(bad_alloc ex)
    {
        // Generic STL allocation error
        ft.WriteLine(L"ERROR: Memory allocation error");
        return 3;
    }
    catch(exception ex)
    {
        // We should never get here (unless we have a bug)
        _ASSERTE(false);
        ft.WriteLine(L"ERROR: STL Exception catched: %S", ex.what());
        return 2;
    }
    catch(HRESULT hr)
    {
        ft.Trace(DBG_INFO, L"HRESULT Error catched: 0x%08lx", hr);
        return 2;
    }
}


//
//  Main routine
//  - First, parse the command-line options
//  - Then execute the appropriate client calls
//
//  WARNING: 
//  - The routine does not check for mutually exclusive flags/options
//
//  On error, this function throws
//
int CommandLineParser::MainRoutine(vector<wstring> arguments)
{
    FunctionTracer ft(DBG_INFO);

    // Context for the VSS operation
    DWORD dwContext = VSS_CTX_BACKUP;

    // List of selected writers during the shadow copy creation process
    vector<wstring> excludedWriterList;

    // List of selected writers during the shadow copy creation process
    vector<wstring> includedWriterList;

    // the volume for which to delete the oldest shadow copy
    wstring stringVolume;

    // The script file prefix 
    // Non-empty if scripts have to be generated 
    wstring stringFileName;

    // Script to execute between snapshot creation and backup complete
    wstring execCommand;

    // The backup components document
    wstring xmlBackupComponentsDoc;

    // Enumerate each argument
    for(unsigned argIndex = 0; argIndex < arguments.size(); argIndex++)
    {
        //
        //  Flags 
        //

#ifdef VSS_SERVER

        // Check for the persistent flag
        if (MatchArgument(arguments[argIndex], L"p"))
        {
            ft.WriteLine(L"(Option: Persistent shadow copy)");
            m_bPersistent = true;

            // The final dwContext needs to be updated based on m_bPersistent and m_bWithWriters
            continue;
        }

        // Check for the no-writers flag
        if (MatchArgument(arguments[argIndex], L"nw"))
        {
            ft.WriteLine(L"(Option: No-writers option detected)");
            m_bWithWriters = false;

            // The final dwContext needs to be updated based on m_bPersistent and m_bWithWriters
            continue;
        }

        // Check for the transportability option
        if (MatchArgument(arguments[argIndex], L"t", xmlBackupComponentsDoc))
        {
            ft.WriteLine(L"(Option: Transportable shadow set. Saving xml to file '%s')", xmlBackupComponentsDoc.c_str());
            dwContext |= VSS_VOLSNAP_ATTR_TRANSPORTABLE;
            continue;
        }

        // Check for the backup components document option
        if (MatchArgument(arguments[argIndex], L"bc", xmlBackupComponentsDoc))
        {
            ft.WriteLine(L"(Option: Saving xml to file '%s')", xmlBackupComponentsDoc.c_str());
            continue;
        }

        // Check for the differential flag option
        if (MatchArgument(arguments[argIndex], L"ad"))
        {
            ft.WriteLine(L"(Option: Creating differential HW shadow copies)");
            dwContext |= VSS_VOLSNAP_ATTR_DIFFERENTIAL;
            continue;
        }

        // Check for the plex flag option
        if (MatchArgument(arguments[argIndex], L"ap"))
        {
            ft.WriteLine(L"(Option: Creating plex HW shadow copies)");
            dwContext |= VSS_VOLSNAP_ATTR_PLEX;
            continue;
        }

        // Check for the Shadow Copy for Shared Folders (i.e. Client Accessible) flag option
        if (MatchArgument(arguments[argIndex], L"scsf"))
        {
            ft.WriteLine(L"(Option: Creating Shadow Copies for Shared Folders - Client Accessible)");
            dwContext = VSS_CTX_CLIENT_ACCESSIBLE;
            continue;
        }

#endif

        // Check for the writer exclusion flag
        wstring excludedWriter;
        if (MatchArgument(arguments[argIndex], L"wx", excludedWriter))
        {
            ft.WriteLine(L"(Option: Excluding writer/component '%s')", excludedWriter.c_str());
            excludedWriterList.push_back(excludedWriter);
            continue;
        }

        // Check for the writer inclusion flag
        wstring includedWriter;
        if (MatchArgument(arguments[argIndex], L"wi", includedWriter))
        {
            ft.WriteLine(L"(Option: Verifying inclusion of writer/component '%s')", includedWriter.c_str());
            includedWriterList.push_back(includedWriter);
            continue;
        }

        // Check for the "wait" flag
        if (MatchArgument(arguments[argIndex], L"wait"))
        {
            ft.WriteLine(L"(Option: Wait on finish)");
            m_bWaitForFinish = true;
            continue;
        }

        // Check for the script generation option
        if (MatchArgument(arguments[argIndex], L"script", stringFileName))
        {
            ft.WriteLine(L"(Option: Generate SETVAR script '%s')", stringFileName.c_str());
            continue;
        }

        // Check for the command execution option
        if (MatchArgument(arguments[argIndex], L"exec", execCommand))
        {
            ft.WriteLine(L"(Option: Execute binary/script after shadow creation '%s')", execCommand.c_str());

            // Check if the command is a valid CMD/EXE file
            DWORD dwAttributes = GetFileAttributes((LPWSTR)execCommand.c_str());
            if ((dwAttributes == INVALID_FILE_ATTRIBUTES) || ((dwAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0))
            {
                ft.WriteLine(L"ERROR: the parameter '%s' must be an existing file!", execCommand.c_str());
                ft.WriteLine(L"- Note: the -exec command cannot have parameters!");
                throw(E_INVALIDARG);
            }

            continue;
        }

        // Check for the tracing option
        if (MatchArgument(arguments[argIndex], L"tracing"))
        {
            ft.WriteLine(L"(Option: Enable tracing)");
            FunctionTracer::EnableTracingMode();
            continue;
        }

        //
        //  Operations 
        //

        // Check for /? or -?
        if (MatchArgument(arguments[argIndex], L"?"))
            break;

        // Query all shadow copies in the set 
        if (MatchArgument(arguments[argIndex], L"q"))
        {
            ft.WriteLine(L"(Option: Query all shadow copies)");
            
            // Initialize the VSS client
            m_vssClient.Initialize(VSS_CTX_ALL);

            // List all shadow copies in the system
            m_vssClient.QuerySnapshotSet(GUID_NULL);

            return 0;
        }

        // Query all shadow copies in the set 
        wstring id;
        if (MatchArgument(arguments[argIndex], L"qx", id))
        {
            ft.WriteLine(L"(Option: Query shadow copy set)");
            
            GUID queryingSnapshotSetID = WString2Guid(id);

            // Initialize the VSS client
            m_vssClient.Initialize(VSS_CTX_ALL);

            // List all shadow copies in the set
            m_vssClient.QuerySnapshotSet(queryingSnapshotSetID);

            return 0;
        }

        // Query the specified shadow copy 
        if (MatchArgument(arguments[argIndex], L"s", id))
        {
            ft.WriteLine(L"(Option: Query shadow copy)");
            
            VSS_ID queryingSnapshotID = WString2Guid(id);

            // Initialize the VSS client
            m_vssClient.Initialize(VSS_CTX_ALL);

            // List all shadow copies in the set
            m_vssClient.GetSnapshotProperties(queryingSnapshotID);

            return 0;
        }

        // Delete all shadow copies in the system
        if (MatchArgument(arguments[argIndex], L"da"))
        {
            ft.WriteLine(L"(Option: Delete all shadow copies)");

            // Test if the user agrees
            string response;
            cout << "This will delete all shadow copies in the system. Are you sure? [Y/N] ";
            cin >> response;
            if ((response.length() != 1) || ((response[0] != 'Y') && (response[0] != 'y')))
                return 0;
            cout << "\n";

            // Initialize the VSS client
            m_vssClient.Initialize(VSS_CTX_ALL);

            // Gather writer metadata
            m_vssClient.DeleteAllSnapshots();

            return 0;
        }

        // Delete a certain shadow copy set
        if (MatchArgument(arguments[argIndex], L"dx", id))
        {
            ft.WriteLine(L"(Option: Delete a shadow copy set)");

            VSS_ID deletingSnapshotSetID = WString2Guid(id);

            // Initialize the VSS client
            m_vssClient.Initialize(VSS_CTX_ALL);

            // Gather writer metadata
            m_vssClient.DeleteSnapshotSet(deletingSnapshotSetID);

            return 0;
        }

        // Delete a certain shadow copy 
        if (MatchArgument(arguments[argIndex], L"ds", id))
        {
            ft.WriteLine(L"(Option: Delete a shadow copy)");

            VSS_ID deletingSnapshotID = WString2Guid(id);

            // Initialize the VSS client
            m_vssClient.Initialize(VSS_CTX_ALL);

            // Gather writer metadata
            m_vssClient.DeleteSnapshot(deletingSnapshotID);

            return 0;
        }

        // List the summary writer metadata
        if (MatchArgument(arguments[argIndex], L"wm"))
        {
            ft.WriteLine(L"(Option: List writer metadata)");
            
            // Initialize the VSS client
            dwContext = UpdateFinalContext(dwContext);
            m_vssClient.Initialize(dwContext);

            // Gather writer metadata
            m_vssClient.GatherWriterMetadata();

            // List summary writer metadata
            m_vssClient.ListWriterMetadata(false);

            return 0;
        }

        // List the extended writer metadata
        if (MatchArgument(arguments[argIndex], L"wm2"))
        {
            ft.WriteLine(L"(Option: List extended writer metadata)");
            
            // Initialize the VSS client
            dwContext = UpdateFinalContext(dwContext);
            m_vssClient.Initialize(dwContext);

            // Gather writer metadata
            m_vssClient.GatherWriterMetadata();

            // List writer metadata
            m_vssClient.ListWriterMetadata(true);

            return 0;
        }

        // List the writer state
        if (MatchArgument(arguments[argIndex], L"ws"))
        {
            ft.WriteLine(L"(Option: List writer status)");
            
            // Initialize the VSS client
            dwContext = UpdateFinalContext(dwContext);
            m_vssClient.Initialize(dwContext);

            // Gather writer metadata
            m_vssClient.GatherWriterMetadata();

            // Gather writer status
            m_vssClient.GatherWriterStatus();

            // List writer status
            m_vssClient.ListWriterStatus();

            return 0;
        }

#ifdef VSS_SERVER
        // Delete the oldest shadow copy for a volume
        if (MatchArgument(arguments[argIndex], L"do", stringVolume))
        {
            ft.WriteLine(L"(Option: Delete the oldest shadow copy for %s)", stringVolume.c_str());

            m_vssClient.Initialize(VSS_CTX_ALL);

            m_vssClient.DeleteOldestSnapshot(stringVolume);

            return 0;
        }

		// Revert a volume to the specified shadow copy
        if (MatchArgument(arguments[argIndex], L"revert", id))
        {
            ft.WriteLine(L"(revert a shadow copy)");

            VSS_ID revertingShadowId = WString2Guid(id);

            m_vssClient.Initialize(VSS_CTX_ALL);

            m_vssClient.RevertToSnapshot(revertingShadowId);

            return 0;
        }

        // Break this shadow copy set to readonly
        if (MatchArgument(arguments[argIndex], L"b", id))
        {
            ft.WriteLine(L"(Option: Break shadow copy set)");
            
            VSS_ID breakingSnapshotSetID = WString2Guid(id);

            // Initialize the VSS client
            m_vssClient.Initialize(VSS_CTX_ALL);

            // List all shadow copies in the set
            m_vssClient.BreakSnapshotSet(breakingSnapshotSetID, false);

            return 0;
        }

        // Break this shadow copy set to writable volumes
        if (MatchArgument(arguments[argIndex], L"bw", id))
        {
            ft.WriteLine(L"(Option: Break shadow copy set as writable)");
            
            VSS_ID breakingSnapshotSetID = WString2Guid(id);

            // Initialize the VSS client
            m_vssClient.Initialize(VSS_CTX_ALL);

            // Break all shadow copies in the set
            if (m_bWaitForFinish || execCommand.length()>0)
            {
                vector<wstring> volumeList;
                m_vssClient.BreakSnapshotSet(breakingSnapshotSetID, true, &volumeList);

                // Executing the custom command if needed
                if (execCommand.length()>0)
                    ExecCommand(execCommand);

                // Wait if needed
                if (m_bWaitForFinish)
                {
                    ft.WriteLine(L"\nPress <ENTER> to make the volumes writable...");
                    getchar();
                    m_bWaitForFinish = false;
                }

                // Make the volumes read-write
                ft.WriteLine(L"- Making shadow copy devices from " WSTR_GUID_FMT L" read-write...", 
                    GUID_PRINTF_ARG(breakingSnapshotSetID));
                
                m_vssClient.MakeVolumesReadWrite(volumeList);
            }
            else
            {
                // Break and make them read-write
                m_vssClient.BreakSnapshotSet(breakingSnapshotSetID, true);
            }

            return 0;
        }

        // Expose the shadow copy locally
        wstring exposeArgs;
        if (MatchArgument(arguments[argIndex], L"el", exposeArgs))
        {
            ft.WriteLine(L"(Option: Expose a shadow copy)");

            vector<wstring> exposeArgsArray = SplitWString(exposeArgs, L',');
            if (exposeArgsArray.size() != 2)
            {
                ft.WriteLine(L"ERROR: the -el arguments must contain a GUID and a local path separated by a comma.");
                throw(E_INVALIDARG);
            }

            VSS_ID snapshotID = WString2Guid(exposeArgsArray[0]);

            // Initialize the VSS client
            m_vssClient.Initialize(VSS_CTX_ALL);

            // Expose locally this shadow copy
            m_vssClient.ExposeSnapshotLocally(snapshotID, exposeArgsArray[1]);

            return 0;
        }

        // Expose the shadow copy remotely
        if (MatchArgument(arguments[argIndex], L"er", exposeArgs))
        {
            ft.WriteLine(L"(Option: Expose a shadow copy)");

            vector<wstring> exposeArgsArray = SplitWString(exposeArgs, L',');
            if ((exposeArgsArray.size() != 2) && (exposeArgsArray.size() != 3))
            {
                ft.WriteLine(L"ERROR: the -er arguments must contain a GUID, a share name and an optional local path separated by a comma.");
                throw(E_INVALIDARG);
            }

            VSS_ID snapshotID = WString2Guid(exposeArgsArray[0]);

            // Initialize the VSS client
            m_vssClient.Initialize(VSS_CTX_ALL);

            // Expose this shadow copy as a share
            m_vssClient.ExposeSnapshotRemotely(snapshotID, 
                exposeArgsArray[1], 
                exposeArgsArray.size() == 3? exposeArgsArray[2]: L"");

            return 0;
        }

        // Import with the given XML file
        if (MatchArgument(arguments[argIndex], L"i", xmlBackupComponentsDoc))
        {
            ft.WriteLine(L"(Option: Import shadow copy set from file '%s')", xmlBackupComponentsDoc.c_str());

            // Reading the backup components document
            wstring xmlDoc = ReadFileContents(xmlBackupComponentsDoc);
            ft.Trace(DBG_INFO, L"XML document: '%s'", xmlDoc.c_str());

            // Initialize the VSS client
            m_vssClient.Initialize(VSS_CTX_ALL, xmlDoc);

            // List all shadow copies in the system
            m_vssClient.ImportSnapshotSet();

            // Executing the custom command if needed
            if (execCommand.length() > 0)
                ExecCommand(execCommand);

            return 0;
        }
#endif

        // Perform a restore
        if (MatchArgument(arguments[argIndex], L"r", xmlBackupComponentsDoc))
        {
            ft.WriteLine(L"(Option: Perform a restore)");
            
            // Reading the backup components document
            wstring xmlDoc = ReadFileContents(xmlBackupComponentsDoc);
            ft.Trace(DBG_INFO, L"XML document: '%s'", xmlDoc.c_str());

            // Initialize the VSS client
            m_vssClient.Initialize(VSS_CTX_ALL, xmlDoc, true);

            // Gather writer metadata
            m_vssClient.GatherWriterMetadata();

            // Gather writer status
            m_vssClient.GatherWriterStatus();

            // List writer status
            m_vssClient.ListWriterStatus();

            // Initialize the list of writers and components for restore
            m_vssClient.InitializeWriterComponentsForRestore();

            // Select required components for restore
            m_vssClient.SelectComponentsForRestore(excludedWriterList, includedWriterList);

            // Issue a PreRestore event to the writers
            m_vssClient.PreRestore();

            // Execute the optional custom command between PreRestore and PostRestore
            try
            {
                // Check selected writer status
                m_vssClient.CheckSelectedWriterStatus();

                // Executing the custom command if needed
                if (execCommand.length() > 0)
                    ExecCommand(execCommand);

            }
            catch(HRESULT)
            {
                // Notify writers about a failed restore
                m_vssClient.SetFileRestoreStatus(false);

                // Issue a PostRestore event to the writers
                m_vssClient.PostRestore();

                throw;
            }

            // Notify writers about a succesful restore
            m_vssClient.SetFileRestoreStatus(true);

            // Issue a PostRestore event to the writers
            m_vssClient.PostRestore();

            // Check selected writer status
            m_vssClient.CheckSelectedWriterStatus();

            ft.WriteLine(L"\nRestore done.");
            
            return 0;
        }

        // Perform a simulated restore
        if (MatchArgument(arguments[argIndex], L"rs", xmlBackupComponentsDoc))
        {
            ft.WriteLine(L"(Option: Perform a Simulated restore)");
            
            // Reading the backup components document
            wstring xmlDoc = ReadFileContents(xmlBackupComponentsDoc);
            ft.Trace(DBG_INFO, L"XML document: '%s'", xmlDoc.c_str());

            // Initialize the VSS client
            m_vssClient.Initialize(VSS_CTX_ALL, xmlDoc, true);

            // Gather writer metadata
            m_vssClient.GatherWriterMetadata();

            // Gather writer status
            m_vssClient.GatherWriterStatus();

            // List writer status
            m_vssClient.ListWriterStatus();

            // Initialize the list of writers and components for restore
            m_vssClient.InitializeWriterComponentsForRestore();

            // Select required components for restore
            m_vssClient.SelectComponentsForRestore(excludedWriterList, includedWriterList);

            ft.WriteLine(L"\nRestore simulation done.");
            
            return 0;
        }


        // Check if the arguments are volumes. If yes, try to create the shadow copy set 
        if (IsVolume(arguments[argIndex]))
        {
            ft.WriteLine(L"(Option: Create shadow copy set)");
            
            ft.Trace(DBG_INFO, L"\nAttempting to create a shadow copy set... (volume %s was added as parameter)", arguments[argIndex].c_str());
            
            // Make sure that all the arguments are volumes
            vector<wstring> volumeList; 
            volumeList.push_back(GetUniqueVolumeNameForPath(arguments[argIndex]));

            // Process the rest of the arguments
            for(unsigned i = argIndex + 1; i < arguments.size(); i++)
            {
                if (!IsVolume(arguments[i]))
                {
                    // No match. Print an error and the usage
                    ft.WriteLine(L"\nERROR: invalid parameters %s", GetCommandLine());
                    ft.WriteLine(L"- Parameter %s is expected to be a volume!  (shadow copy creation is assumed)", arguments[i].c_str());
                    ft.WriteLine(L"- Example: VSHADOW C:");
                    PrintUsage();
                    return 1;
                }

                // Add the volume to the list
                volumeList.push_back(GetUniqueVolumeNameForPath(arguments[i]));
            }
            
            // Initialize the VSS client
            dwContext = UpdateFinalContext(dwContext);
            m_vssClient.Initialize(dwContext);

            // Create the shadow copy set
            m_vssClient.CreateSnapshotSet(
                volumeList, 
                xmlBackupComponentsDoc, 
                excludedWriterList,
                includedWriterList
                );

            try
            {
                // Generate management scripts if needed
                if (stringFileName.length() > 0)
                    m_vssClient.GenerateSetvarScript(stringFileName);

                // Executing the custom command if needed
                if (execCommand.length() > 0)
                    ExecCommand(execCommand);

            }
            catch(HRESULT)
            {
                // Mark backup failure and exit
                if ((dwContext & VSS_VOLSNAP_ATTR_NO_WRITERS) == 0)
                    m_vssClient.BackupComplete(false);

                throw;
            }

            // Complete the backup
            // Note that this will notify writers that the backup is succesful! 
            // (which means eventually log truncation)
            if ((dwContext & VSS_VOLSNAP_ATTR_NO_WRITERS) == 0)
                m_vssClient.BackupComplete(true);

            ft.WriteLine(L"\nSnapshot creation done.");
            
            return 0;
        }

        // No match. Print an error and the usage
        ft.WriteLine(L"\nERROR: invalid parameter '%s'\n", arguments[argIndex].c_str());
        PrintUsage();
        return 1;
    }

    PrintUsage();
    return 0;
}



// Returns TRUE if the argument is in the following formats
//  -xxxx
//  /xxxx
// where xxxx is the option pattern
bool CommandLineParser::MatchArgument(wstring argument, wstring optionPattern)
{
    FunctionTracer ft(DBG_INFO);
    
    ft.Trace(DBG_INFO, L"Matching Arg: '%s' with '%s'\n", argument.c_str(), optionPattern.c_str());
    
    bool retVal = (IsEqual(argument, wstring(L"/") + optionPattern) || IsEqual(argument, wstring(L"-") + optionPattern) );
    
    ft.Trace(DBG_INFO, L"Return: %s\n", BOOL2TXT(retVal));
    return retVal;
}


// Returns TRUE if the argument is in the following formats
//  -xxxx=yyyy
//  /xxxx=yyyy
// where xxxx is the option pattern and yyyy the additional parameter (eventually enclosed in ' or ")
bool CommandLineParser::MatchArgument(wstring argument, wstring optionPattern, wstring & additionalParameter)
{
    FunctionTracer ft(DBG_INFO);
    
    ft.Trace(DBG_INFO, L"Matching Arg: '%s' with '%s'", argument.c_str(), optionPattern.c_str());

    _ASSERTE(argument.length() > 0);
    
    if ((argument[0] != L'/') && (argument[0] != L'-'))
        return false;

    // Find the '=' separator between the option and the additional parameter
    size_t equalSignPos = argument.find(L'=');
    if ((equalSignPos == wstring::npos) || (equalSignPos == 0))
        return false;

    ft.Trace(DBG_INFO, L"%s %d", argument.substr(1, equalSignPos - 1).c_str(), equalSignPos);
    
    // Check to see if this is our option
    if (!IsEqual(argument.substr(1, equalSignPos - 1), optionPattern))
        return false;

    // Isolate the additional parameter
    additionalParameter = argument.substr(equalSignPos + 1);

    ft.Trace(DBG_INFO, L"- Additional Param: [%s]", additionalParameter.c_str());

    // We cannot have an empty additional parameter
    if (additionalParameter.length() == 0)
        return false;

    // Eliminate the enclosing quotes, if any
    size_t lastPos = additionalParameter.length() - 1;
    if ((additionalParameter[0] == L'"') && (additionalParameter[lastPos] == L'"'))
        additionalParameter = additionalParameter.substr(1, additionalParameter.length() - 2);
    
    ft.Trace(DBG_INFO, L"Return true; (additional param = %s)", additionalParameter.c_str());
    
    return true;
}


// 
//  Prints the command line options
//
void CommandLineParser::PrintUsage()
{
    FunctionTracer ft(DBG_INFO);

#ifdef VSS_SERVER
    ft.WriteLine(
        L"Usage:\n"
        L"   VSHADOW [optional flags] [commands]\n"
        L"\n"
        L"List of optional flags:\n"
        L"  -?                 - Displays the usage screen\n"
        L"  -p                 - Manages persistent shadow copies\n"
        L"  -nw                - Manages no-writer shadow copies\n"
        L"  -ad                - Creates differential HW shadow copies\n"
        L"  -ap                - Creates plex HW shadow copies\n"
        L"  -scsf              - Creates Shadow Copies for Shared Folders (Client Accessible)\n"
        L"  -t={file.xml}      - Transportable shadow set. Generates also the backup components doc.\n"
        L"  -bc={file.xml}     - Generates the backup components doc for non-transportable shadow set.\n"
        L"  -wi={Writer Name}  - Verify that a writer/component is included\n"
        L"  -wx={Writer Name}  - Exclude a writer/component from set creation or restore\n"
        L"  -script={file.cmd} - SETVAR script creation\n"
        L"  -exec={command}    - Custom command executed after shadow creation, import or between break and make-it-write\n"
        L"  -wait              - Wait before program termination or between shadow set break and make-it-write\n"
        L"  -tracing           - Runs VSHADOW.EXE with enhanced diagnostics\n"
        L"\n"
        L"List of commands:\n"
        L"  {volume list}      - Creates a shadow set on these volumes\n"
        L"  -ws                - List writer status\n"
        L"  -wm                - List writer summary metadata\n"
        L"  -wm2               - List writer detailed metadata\n"
        L"  -q                 - List all shadow copies in the system\n"
        L"  -qx={SnapSetID}    - List all shadow copies in this set\n"
        L"  -s={SnapID}        - List the shadow copy with the given ID\n"
        L"  -da                - Deletes all shadow copies in the system\n"
        L"  -do={volume}       - Deletes the oldest shadow of the specified volume\n"
        L"  -dx={SnapSetID}    - Deletes all shadow copies in this set\n"
        L"  -ds={SnapID}       - Deletes this shadow copy\n"
        L"  -i={file.xml}      - Transportable shadow copy import\n"
        L"  -b={SnapSetID}     - Break the given shadow set into read-only volumes\n"
        L"  -bw={SnapSetID}    - Break the shadow set into writable volumes\n"
        L"  -el={SnapID},dir   - Expose the shadow copy as a mount point\n"
        L"  -el={SnapID},drive - Expose the shadow copy as a drive letter\n"
        L"  -er={SnapID},share - Expose the shadow copy as a network share\n"
        L"  -er={SnapID},share,path - Expose a child directory from the shadow copy as a share\n"
        L"  -r={file.xml}      - Restore based on a previously-generated Backup Components document\n"
        L"  -rs={file.xml}     - Simulated restore based on a previously-generated Backup Components doc\n"\
        L"  -revert={SnapID}   - Revert a volume to the specified shadow copy\n"
        L"\n"
        L"Examples:\n"
        L"\n"
        L" - Non-persistent shadow copy creation on C: and D:\n"
        L"     VSHADOW C: E:\n"
        L"\n"
        L" - Persistent shadow copy creation on C: (with no writers)\n"
        L"     VSHADOW -p -nw C:\n"
        L"\n"
        L" - Transportable shadow copy creation on X:\n"
        L"     VSHADOW -t=file1.xml X:\n"
        L"\n"
        L" - Transportable shadow copy import\n"
        L"     VSHADOW -i=file1.xml\n"
        L"\n"
        L" - List all shadow copies in the system:\n"
        L"     VSHADOW -q\n"
        L"\n"
        L"Please see the README.DOC file for more details.\n"
        L"\n"
        L"\n"
        );
#else
    ft.WriteLine(
        L"Usage:\n"
        L"   VSHADOW [optional flags] [commands]\n"
        L"\n"
        L"List of optional flags:\n"
        L"  -?                 - Displays the usage screen\n"
        L"  -wi={Writer Name}  - Verify that a writer/component is included\n"
        L"  -wx={Writer Name}  - Exclude a writer/component from set creation or restore\n"
        L"  -bc={file.xml}     - Generates the backup components document during shadow creation.\n"
        L"  -script={file.cmd} - SETVAR script creation\n"
        L"  -exec={command}    - Custom command executed after shadow creation\n"
        L"  -wait              - Wait before program termination \n"
        L"  -tracing           - Runs VSHADOW.EXE with enhanced diagnostics\n"
        L"\n"
        L"List of commands:\n"
        L"  {volume list}      - Creates a shadow set on these volumes\n"
        L"  -ws                - List writer status\n"
        L"  -wm                - List writer summary metadata\n"
        L"  -wm2               - List writer detailed metadata\n"
        L"  -q                 - List all shadow copies in the system\n"
        L"  -qx={SnapSetID}    - List all shadow copies in this set\n"
        L"  -s={SnapID}        - List the shadow copy with the given ID\n"
        L"  -da                - Deletes all shadow copies in the system\n"
        L"  -dx={SnapSetID}    - Deletes all shadow copies in this set\n"
        L"  -ds={SnapID}       - Deletes this shadow copy\n"
        L"  -r={file.xml}      - Restore based on a previously-generated Backup Components doc\n"
        L"  -rs={file.xml}     - Simulated restore based on a previously-generated Backup Components doc\n"
        L"\n"
        L"Examples:\n"
        L"\n"
        L" - Non-persistent shadow copy creation on C: and D:\n"
        L"     VSHADOW C: E:\n"
        L"\n"
        L" - List all shadow copies in the system:\n"
        L"     VSHADOW -q\n"
        L"\n"
        L"Please see the README.DOC file for more details.\n"
        L"\n"
        L"\n"
        );
#endif
}



DWORD CommandLineParser::UpdateFinalContext(DWORD dwContext)
{
#ifdef VSS_SERVER
    if (m_bPersistent)
    {
        if (m_bWithWriters) 
            dwContext |= VSS_CTX_APP_ROLLBACK;
        else
            dwContext |= VSS_CTX_NAS_ROLLBACK;
    }
    else
    {
        if (m_bWithWriters) 
            dwContext |= VSS_CTX_BACKUP;
        else
            dwContext |= VSS_CTX_FILE_SHARE_BACKUP;
    }
#endif

    return dwContext;
}



CommandLineParser::CommandLineParser()
{
    // true if it is a is persistent snapshot
    m_bPersistent = false;

    // false if the snapshot creation is without writers
    m_bWithWriters = true;

    // true if the user wants to wait for termination
    m_bWaitForFinish = false;
}



// Destructor
CommandLineParser::~CommandLineParser()
{
    FunctionTracer ft(DBG_INFO);

    if (m_bWaitForFinish)
    {
        ft.WriteLine(L"\nPress <ENTER> to continue...");
        getchar();
    }
}
