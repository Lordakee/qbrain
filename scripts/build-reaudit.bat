@echo off
REM N0 re-audit build into a separate dir (does not touch build/ NMake cache)
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
set CM="C:\Program Files\CMake\bin\cmake.exe"
%CM% -S "D:\Projects\Qbrain" -B "D:\Projects\Qbrain\build\reaudit" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
%CM% --build "D:\Projects\Qbrain\build\reaudit" --target qbrain qbrain_tests
if errorlevel 1 exit /b 1
echo BUILD_OK
