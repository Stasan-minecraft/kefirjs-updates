@echo off
setlocal DisableDelayedExpansion
set "ps=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "script=%~dp0config\kefiriz\standalone\Install.ps1"
set "action=Artwork"
if not "%~3"=="" goto usage
if "%~1"=="" goto prompt
if /i "%~1"=="undo" goto undo
if /i "%~1"=="undo-artwork" goto undo
if /i "%~1"=="artwork" goto card
if not "%~2"=="" goto usage
"%ps%" -NoProfile -ExecutionPolicy Bypass -File "%script%" -Action Artwork -SdRoot "%~f1\."
exit /b %errorlevel%
:undo
set "action=Undo-Artwork"
:card
if "%~2"=="" goto prompt
"%ps%" -NoProfile -ExecutionPolicy Bypass -File "%script%" -Action %action% -SdRoot "%~f2\."
exit /b %errorlevel%
:prompt
"%ps%" -NoProfile -ExecutionPolicy Bypass -File "%script%" -Action %action%
set "result=%errorlevel%"
if "%~1"=="" pause
exit /b %result%
:usage
echo Usage: INSTALL.bat [artwork or undo-artwork] [microSD path]
exit /b 2
