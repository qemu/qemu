

VSHADOW backup/restore functionality of SQL Server


The test demonstrates the VSHADOW backup and restore capabilities. 


Instructions for running SQL/MSDE tests against the default SQL Server instance:
1) Install SQL Server (default config - unnamed instance)
2) Copy the content of this folder to a new directory on the test machine. Ensure that the path does not include spaces.
3) Edit the "setvar.cmd" file and comment the "set MSDE_INSTANCE=TESTMSDE1" line. 
4) Copy VSHADOW.EXE in this directory
5) Run "run_test.cmd"
6) If you do not see TEST FAILED in the output, the test passed.

Instructions for running SQL/MSDE tests against a MSDE instance or a named SQL Server instance:
1) Install SQL Server or MSDE - named instance
2) Copy the content of this folder to a new directory on the test machine
3) Edit the "setvar.cmd" file and change the "set MSDE_INSTANCE=TESTMSDE1" line. Put there the SQL instance name.
4) Copy VSHADOW.EXE in this directory
5) Run "run_test.cmd"
6) If you do not see TEST FAILED in the output, the test passed.



The cleanup script is cleanup_test.cmd. This basically deletes all temporary files and directories 
(in the form _XXXXX)


Test implementation:
- Clean test 
- install db
- save DB state
- backup
- modify DB 
- verify modifications
- restore
- verify that the DB was restored


Notes:
- This test will not work in a clustered configuration.
- The test uses the PUBS database by default. If you want to use Northwind instead, edit the "setvar.cmd" file, by changing the DB name to Northwind: "set DB=Northwind".
- The tests assume the paths below. If these are not OK on your machine, you need to change them.
    set MSDE_SETUP_LOCATION=TODO - put your location to the MSDE setup.exe program!
    path %path%;"%ProgramFiles%\Microsoft SQL Server\80\Tools\Binn"

