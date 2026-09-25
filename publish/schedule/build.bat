@echo off
rem One-click build: MSVC x64 toolchain -> release\ScheduleWidget.exe
setlocal enabledelayedexpansion
cd /d "%~dp0"

rem ---- Locate vcvars64.bat automatically (Community/Pro/Enterprise/BuildTools) ----
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
  )
)
rem Common install paths fallback
if not defined VCVARS (
  for %%E in (Community Professional Enterprise BuildTools) do (
    if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if not defined VCVARS if exist "D:\exe\VS\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=D:\exe\VS\Community\VC\Auxiliary\Build\vcvars64.bat"

if not defined VCVARS (
  echo [build] MSVC toolchain vcvars64.bat not found.
  echo [build] Install Visual Studio with "Desktop development with C++" or Build Tools.
  echo [build] Or run this script from "x64 Native Tools Command Prompt for VS".
  exit /b 1
)
echo [build] using: %VCVARS%
call "%VCVARS%" x64 >nul 2>&1
if errorlevel 1 (
  echo [build] vcvars64 init failed
  exit /b 1
)

if not exist build mkdir build
if not exist release mkdir release

echo [build] compile cJSON (W0)...
cl /nologo /utf-8 /O2 /W0 /MT /Fobuild\ /c third_party\cJSON\cJSON.c
if errorlevel 1 goto fail

echo [build] compile sources (W4)...
cl /nologo /utf-8 /O2 /W4 /MT /DUNICODE /D_UNICODE /Ithird_party\cJSON /Fobuild\ /c src\*.c
if errorlevel 1 goto fail

echo [build] compile resources...
rc /nologo /i src /fo build\app.res src\app.rc
if errorlevel 1 goto fail

echo [build] link...
link /nologo /OUT:build\ScheduleWidget.exe build\*.obj build\app.res /SUBSYSTEM:WINDOWS ^
  user32.lib gdi32.lib gdiplus.lib shell32.lib advapi32.lib dwmapi.lib ^
  comctl32.lib winmm.lib shcore.lib comdlg32.lib ole32.lib msimg32.lib
if errorlevel 1 goto fail

copy /y build\ScheduleWidget.exe release\ScheduleWidget.exe >nul
echo [build] OK: release\ScheduleWidget.exe
exit /b 0

:fail
echo [build] FAILED
exit /b 1
