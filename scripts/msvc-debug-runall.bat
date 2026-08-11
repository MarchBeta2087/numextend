@echo off
rem MSVC Debug test runner: runs all unit tests, logs to ..\dbg_<test>.log.
rem Each test enables CRT debug heap checks (_CrtSetDbgFlag); check logs for
rem "Detected memory leaks" or heap corruption reports.
rem Usage: cd build-dbg\tests && runall.bat
setlocal enabledelayedexpansion
cd /d C:\Users\msgoogle\numextend\build-dbg\tests
for %%t in (test_bigint_bin test_bigint_dec test_bigfrac test_bigfloat test_bigfloat_fuzz test_rounding_tie test_bigdecimal test_bigcomplex_float test_bigcomplex_decimal test_convert test_oom) do (
    %%~t.exe > ..\dbg_%%t.log 2>&1
    echo %%t RC=!errorlevel!
)
