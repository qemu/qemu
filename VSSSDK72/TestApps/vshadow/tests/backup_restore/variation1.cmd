setlocal 
set VARIATION=1
call cleanup_test
call install_db

call osql_db prepare_variation1.sql %DB%
call check_db 1
call backup_db
call osql_db variation1.sql %DB%
call check_db 2
call compare_db 1 2 check_difference
call restore_db
call check_db 3
call compare_db 1 3
