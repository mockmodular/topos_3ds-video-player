@echo off
setlocal
set "BASH=C:\devkitPro\msys2\usr\bin\bash.exe"
set "WINDIR=%~dp0"
echo Building %~dp0...
echo.

REM Regenerate banner.wav (44100Hz stereo, loop-friendly) before each full build
set "GENWAV=%~dp0scripts\gen_banner_wav.py"
if exist "%GENWAV%" (
  py -3 "%GENWAV%" 2>nul
  if errorlevel 1 python "%GENWAV%" 2>nul
  if errorlevel 1 echo NOTE: Python not in PATH. Install Python or run: python scripts\gen_banner_wav.py
)
echo.

"%BASH%" -lc "cd \"$(cygpath '%WINDIR:\=/%')\" && make clean && make 2>&1; exit $?"
if %errorlevel% == 0 (
    echo.
    echo BUILD SUCCESS.
    echo Output: .3dsx / .smdh / .elf in this folder ^(Makefile TARGET = folder name^)
    echo Note: `make all` also builds .cia when makerom/bannertool are installed ^(see Makefile^).
) else (
    echo.
    echo BUILD FAILED. See errors above.
)
pause
