@echo off
setlocal
set "LinkScript="
if /i "%~1"=="TestBeautyTransfer" set "LinkScript=TestBeautyTransfer"
if /i "%~1"=="BuildUnreal" set "LinkScript=BuildUnreal"
if /i "%~1"=="BuildNative" set "LinkScript=BuildNative"
if /i "%~1"=="TestEditorCamera" set "LinkScript=TestEditorCamera"
if /i "%~1"=="OpenTestProject" set "LinkScript=OpenTestProject"
if not defined LinkScript (
  echo Usage: Run.cmd BuildUnreal^|BuildNative^|TestEditorCamera^|OpenTestProject [options]
  exit /b 2
)
set "LinkPwsh="
for /f "delims=" %%P in ('where pwsh.exe 2^>nul') do if not defined LinkPwsh set "LinkPwsh=%%P"
if not defined LinkPwsh if exist "%ProgramFiles%\PowerShell\7\pwsh.exe" set "LinkPwsh=%ProgramFiles%\PowerShell\7\pwsh.exe"
rem This machine has PowerShell 7 bundled with Codex, outside the user's PATH.
if not defined LinkPwsh if exist "%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe" set "LinkPwsh=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe"
if not defined LinkPwsh (
  echo PowerShell 7 was not found. Install PowerShell 7 or add pwsh.exe to PATH.
  exit /b 1
)
"%LinkPwsh%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0%LinkScript%.ps1" %2 %3 %4 %5 %6 %7 %8 %9
exit /b %errorlevel%

