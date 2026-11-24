@echo off
setlocal DisableDelayedExpansion
echo KEFIRIZ OS - Boot tools
echo 1. Check boot files on the microSD (read only)
echo 2. Return to Hekate after a normal Linux restart
echo 3. Exit
choice /c 123 /n /m "Select 1, 2 or 3: "
if errorlevel 3 exit /b 0
if errorlevel 2 (set "operation=BootMenu") else (set "operation=Diagnose")
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" -Action "%operation%"
set "result=%ERRORLEVEL%"
pause
exit /b %result%
