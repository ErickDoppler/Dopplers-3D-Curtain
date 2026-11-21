@echo off
rem Doppler's 3D Curtain build: uses the toolchain in c:\workenv (same as Dopplers Bakery)
rem   dist\Dopplers3DCurtain.exe   the window
rem   dist\d3c.exe                 the command line (console program)
set PATH=c:\workenv\w64devkit\bin;c:\workenv\ninja;c:\workenv\cmake-4.4.3-windows-x86_64\bin;%PATH%
cd /d %~dp0
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ || exit /b 1
cmake --build build %* || exit /b 1
if not exist dist mkdir dist
copy /y build\Dopplers3DCurtain.exe dist\ >nul
copy /y build\d3c.exe dist\ >nul
echo built dist\Dopplers3DCurtain.exe and dist\d3c.exe
