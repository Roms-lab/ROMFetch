@echo off
setlocal
cd /d "%~dp0"
cmake --build build --config Release %*
exit /b %ERRORLEVEL%
