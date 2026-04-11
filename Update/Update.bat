@echo off
echo Updating ROMFetch...
tasklist | findstr /I "ROMFetch.exe" >nul
if %errorlevel%==0 (
    timeout /t 1 >nul
    goto waitloop
)
del ROMFetch.exe
wget -O ROMFetch.exe https://raw.githubusercontent.com/Roms-lab/NotRoms/refs/heads/main/Update/ROMFetch.exe
start ROMFetch.exe
exit
