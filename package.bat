@echo off
setlocal

set SCRIPT_DIR=%~dp0
set SCRIPT_PATH=%SCRIPT_DIR%package_release.ps1

if not exist "%SCRIPT_PATH%" (
	echo package_release.ps1 was not found.
	exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_PATH%" %*
set EXIT_CODE=%ERRORLEVEL%

if not "%EXIT_CODE%"=="0" (
	echo.
	echo Packaging failed with exit code %EXIT_CODE%.
)

exit /b %EXIT_CODE%
