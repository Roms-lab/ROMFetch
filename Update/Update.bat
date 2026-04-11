@echo off
echo Updating ROMFetch...
tasklist | findstr /I "ROMFetch.exe" >nul
if %errorlevel%==0 (
    timeout /t 1 >nul
    goto waitloop
)
del ROMFetch.exe
wget -q -L -O ROMFetch.exe https://raw.githubusercontent.com/Roms-lab/NotRoms/main/Update/ROMFetch.exe
start ROMFetch.exe
exit
