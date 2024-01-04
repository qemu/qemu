Volume Shadow Copy Service SDK, v7.2
====================================


SDK Contents and Directory Structure
------------------------------------

* INC & LIB - The headers, IDLs, and LIBs included in these directories 
       are necessary to build a VSS component.  This includes versions for 
       both Windows Server 2003 and Windows XP.

* TESTAPPS - Test and sample applications are included in this directory.

     * BETest - Console test requestor that can be used to test a writer.
            BETest is able to perform most of the operations that a VSS 
            requestor will perform.

     * HWProvST - Sample hardware provider and test suite for testing a 
            VSS hardware provider.

     * TestWriter - A test writer that can be driven to emulate any writer 
            functionality.  This is particularly useful for testing a 
            requestor.  The test writer is driven through an XML file 
            that specifies how the test writer should behave.

     * VShadow - Sample requestor application geared towards hardware 
            shadow copy scenarios.  It can import hardware shadow copies, 
            interact with writers, and offers minimal backup & restore 
            functionality.  VShadow was formerly called VSnap.

* Tools - The Tools directory contains tools and diagnostics related to VSS.

     * VSSReports - Diagnostic tracing tool for logging VSS interactions 
            with other components.


Release Notes for v7.2
----------------------

* There are four new files added to the INC directory.  These files 
  provide the ability to use the management interfaces for the Microsoft 
  System Provider.  The online documentation has been updated to include 
  these interfaces.

* The header vswriter.h has been updated with a new VSS_RT_ORIGINAL_LOCATION
  item added to the VSS_RESTORE_TARGET enumeration.  The online
  documentation has been updated for this change.

* A new diagnostic tool is included in this SDK called VSSReports.  
  VSSReports tool is the standard tool used by Microsoft engineers for 
  diagnosing VSS issues.

* Improvements have been made to the VShadow tool.


Release Notes for v7.1
----------------------

* The documentation has been removed from the SDK and can now be found 
  on MSDN at http://msdn.microsoft.com.

* VSnap.exe has been renamed to VShadow.exe.  The tool has also been 
  significantly changed to better reflect the VSS and VDS operations 
  in an environment with a hardware provider and provide better code 
  samples.  It also replaces the VSReq sample requestor.

* A hardware provider test suite has been added to the SDK.  The tests 
  can be used to verify functionality and stress a hardware provider.

* This SDK includes the headers and libs necessary to build both Windows 
  Server 2003 and Windows XP VSS components.  Note that Windows XP 
  contains a limited set of VSS functionality.
