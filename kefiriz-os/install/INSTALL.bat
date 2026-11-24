@echo off
setlocal
set "ps=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "operation=%~1"
if not defined operation set "operation=Install"
if "%~2"=="" (
  "%ps%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" -Action "%operation%"
) else (
  "%ps%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" -Action "%operation%" -Card "%~2"
)
set "result=%ERRORLEVEL%"
if "%~1"=="" pause
exit /b %result%
