@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
  echo VCVARS_FAILED
  exit /b 1
)
cmake --build out\build\windows-release --target gto_cli
exit /b %errorlevel%
