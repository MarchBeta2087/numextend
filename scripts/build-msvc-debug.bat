@echo off
REM MSVC Debug 构建（CRT 调试堆 + /RTC1 栈检查）
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\Users\msgoogle\numextend
if exist build-dbg rd /s /q build-dbg
mkdir build-dbg
cd build-dbg
cmake -S .. -B . -G "NMake Makefiles" -DCMAKE_C_COMPILER=cl -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="/utf-8 /Zi" > cfg.log 2>&1
nmake > build.log 2>&1
echo NMAKE_RC=%errorlevel%
set DBGCRT=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Redist\MSVC\14.29.30133\debug_nonredist\x64\Microsoft.VC142.DebugCRT
copy /y "%DBGCRT%\vcruntime140d.dll" tests\ >nul 2>&1
copy /y "%DBGCRT%\vcruntime140_1d.dll" tests\ >nul 2>&1
copy /y C:\Windows\System32\ucrtbased.dll tests\ >nul 2>&1
copy /y C:\Users\msgoogle\numextend\scripts\msvc-debug-runall.bat tests\runall.bat >nul
echo BUILD_DONE
