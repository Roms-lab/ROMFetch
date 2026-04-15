@echo off
setlocal
cd /d "%~dp0"
cmake -S . -B build
if errorlevel 1 exit /b 1
cmake --build build --config Release --target ROMFetchGUI -j 8 %*
exit /b %ERRORLEVEL%
