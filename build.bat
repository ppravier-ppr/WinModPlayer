@echo off
rem Builds WinModPlayer.exe (Release) using VS 2019 Build Tools + CMake/Ninja.
rem Usage: build.bat [target]     e.g. build.bat openmpt
setlocal
set ROOT=%~dp0
set CMAKE_DIR=%LOCALAPPDATA%\Android\Sdk\cmake\3.22.1\bin
set PATH=%CMAKE_DIR%;%PATH%

set VCVARS=
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat
if not defined VCVARS ( echo Visual C++ build tools not found & exit /b 1 )
call "%VCVARS%" >nul || exit /b 1

if not exist "%ROOT%build\build.ninja" (
  cmake -S "%ROOT%." -B "%ROOT%build" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
)
set TARGET=%1
if "%TARGET%"=="" set TARGET=all
cmake --build "%ROOT%build" --target %TARGET% || exit /b 1
if exist "%ROOT%build\WinModPlayer.exe" copy /y "%ROOT%build\WinModPlayer.exe" "%ROOT%WinModPlayer.exe" >nul
echo BUILD OK
