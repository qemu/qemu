setlocal

call setvar.cmd


del %FINAL_TEST_OUTPUT%

rem ==== Install MSDE, databases =====


call install_db

rem ==== Run tests =====

@echo.
@echo =========================
@echo == Starting test...  ====
@echo =========================
@echo.

call variation1

type %FINAL_TEST_OUTPUT%

