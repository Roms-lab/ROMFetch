@echo off
echo Updating ROMFetch...
tasklist | findstr /I "ROMFetch.exe" >nul
if %errorlevel%==0 (
    timeout /t 1 >nul
    goto waitloop
)
echo Downloading update...
wget -q -L -O ROMFetch.exe https://raw.githubusercontent.com/Roms-lab/NotRoms/main/Update/ROMFetch.exe
echo Launching updated version...
start "" ROMFetch.exe
echo Creating cleanup script...
(
echo @echo off
echo timeout /t 1 ^>nul
echo rmdir /s /q "%~dp0"
echo del "%%~f0"
) > "%temp%\cleanup_romfetch.bat"
start "" "%temp%\cleanup_romfetch.bat"
exit
