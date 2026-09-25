@echo off
rem Build & run a self-test driver under tools (default: test_import).
rem Usage: tools\build_test.bat test_import
setlocal enabledelayedexpansion
cd /d "%~dp0.."

rem ---- Locate vcvars64.bat automatically ----
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if not defined VCVARS if exist "D:\exe\VS\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=D:\exe\VS\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS ( echo [test-build] MSVC toolchain not found & exit /b 1 )
call "%VCVARS%" x64 >nul 2>&1

set NAME=%1
if "%NAME%"=="" set NAME=test_import
rem Extra main-project objs can be passed as 2nd arg, e.g.:
rem   tools\build_test.bat test_extract "build\text_extract.obj"
set EXTRA=%2

if not exist build\tests mkdir build\tests

cl /nologo /utf-8 /W2 /MT /DUNICODE /D_UNICODE /Isrc /Ithird_party\cJSON ^
   /Fe:build\tests\%NAME%.exe /Fo:build\tests\ ^
   tools\%NAME%.c build\cJSON.obj %EXTRA% user32.lib gdi32.lib shell32.lib
if errorlevel 1 exit /b 1
build\tests\%NAME%.exe
