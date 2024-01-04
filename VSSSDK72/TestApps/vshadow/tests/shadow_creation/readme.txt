
Usage: shadow_test_server.cmd [test-type] [drive letter] [expose as letter]

test-type: takes value as "1" or "2".
"1" refers to tests executed using software provider or volsnap.
"2" refers to hardware-provider specific tests.

drive letter: Value for this parameter is either target volume name or drive configured using hardware provider.
If tests pertaining to volsnap needs to be executed, then this drive letter should be an NTFS volume which is not system volume.
If hardware-provider specific tests need to be executed, then this drive letter should be drive configured using hardware provider.

expose as letter: Provide a drive letter not is not in use. It will be used to expose shadow copy locally.

Assumption: 
1) System drive is C:
2) There is some NTFS volume with drive letter C:


These tests test for various options available using vshadow.exe. These include tests relating to shadow copy creation, deletion and query with or without writers involved. Hardware provider tests specifically test for break and transportable snapshot features.