@echo off
rem Build a test tool: build_tool.bat <source.c> <exeName>
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
if errorlevel 1 (
  echo [test-build] vcvars64 init failed
  exit /b 1
)
if not exist build mkdir build
cl /nologo /utf-8 /W4 /MT /Fe:build\%2 tools\%1 user32.lib
if errorlevel 1 (
  echo [test-build] FAILED
  exit /b 1
)
echo [test-build] OK: build\%2
