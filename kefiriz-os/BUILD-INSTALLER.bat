@echo off
setlocal DisableDelayedExpansion
if /i "%~1"=="--help" goto help
if /i "%~1"=="/?" goto help
if not "%~3"=="" goto usage
set "KEFIRIZ_BUILD_OUTPUT=%~dp0dist\KEFIRIZ-OS-0.1.0"
if not "%~1"=="" set "KEFIRIZ_BUILD_OUTPUT=%~f1"
set "KEFIRIZ_BUILD_ARCHIVE=%LOCALAPPDATA%\KEFIRIZ\build-cache\Lakka-Switch-5f29696.tar"
if not "%~2"=="" set "KEFIRIZ_BUILD_ARCHIVE=%~f2"
set "WSLENV=KEFIRIZ_BUILD_OUTPUT/p:KEFIRIZ_BUILD_ARCHIVE/p:%WSLENV%"
pushd "%~dp0" || exit /b 1
wsl.exe --user root --exec python3 -B ./build/windows_installer.py
set "build_status=%errorlevel%"
popd
if "%build_status%"=="0" echo Open "%KEFIRIZ_BUILD_OUTPUT%\INSTALL.bat" to install.
if "%~1"=="" pause
exit /b %build_status%
:usage
echo Expected at most two paths. Run BUILD-INSTALLER.bat --help.
exit /b 2
:help
echo BUILD-INSTALLER.bat [new-output-folder] [upstream-cache.tar]
echo Default output is dist\KEFIRIZ-OS-0.1.0 in this kit.
echo Uses WSL 2 and its default Ubuntu distribution. Install its prerequisites with:
echo   sudo apt install python3 golang-go squashfs-tools gcc-aarch64-linux-gnu libc6-dev-arm64-cross ca-certificates
echo Use Go 1.21 or newer with automatic toolchain downloads enabled.
echo Reserve 12 GiB in WSL and 4 GiB on the Windows build drive.
echo Missing upstream files are downloaded from Switchroot and verified automatically.
exit /b 0
