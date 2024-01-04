@if not "%1"=="0" goto :ERROR

@ECHO.                       >> %FINAL_TEST_OUTPUT%
@ECHO * Variation: %VARIATION% >> %FINAL_TEST_OUTPUT%
@echo ====================   >> %FINAL_TEST_OUTPUT%
@echo === TEST PASSED! ===   >> %FINAL_TEST_OUTPUT%
@echo ====================   >> %FINAL_TEST_OUTPUT%
@ECHO.                       >> %FINAL_TEST_OUTPUT%

@ECHO.
@ECHO * Variation: %VARIATION%
@echo ====================
@echo === TEST PASSED! ===
@echo ====================
@ECHO.

@goto :EOF

:ERROR

@ECHO.                          >> %FINAL_TEST_OUTPUT%
@ECHO * Variation: %VARIATION%  >> %FINAL_TEST_OUTPUT%
@echo =======================   >> %FINAL_TEST_OUTPUT%
@echo === TEST FAILED! [%1] ===   >> %FINAL_TEST_OUTPUT%
@echo =======================   >> %FINAL_TEST_OUTPUT%
@ECHO.                          >> %FINAL_TEST_OUTPUT%

@ECHO.
@ECHO * Variation: %VARIATION%
@echo =======================
@echo === TEST FAILED! [%1] ===
@echo =======================
@ECHO.

@goto :EOF