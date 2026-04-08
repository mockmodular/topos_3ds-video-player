@echo off
chcp 65001 >nul
set "FFMPEG=C:\software\ffmpeg\bin\ffmpeg.exe"
if not exist "%FFMPEG%" set "FFMPEG=%~dp0ffmpeg.exe"
if not exist "%FFMPEG%" (
    echo [ERROR] ffmpeg not found. Set FFMPEG path at top of this BAT or put ffmpeg.exe here.
    pause
    exit /b 1
)
cd /d "%~dp0"
title Video to MKV Converter
color 0A
echo ============================================
echo     Drag video file here, press Enter
echo     Output: same folder as this BAT
echo     Type q to quit
echo ============================================
echo.
:loop
echo --------------------------------------------
set "input="
set /p "input=Drag file here + Enter: "
if /i "%input%"=="q" goto :eof
if "%input%"=="" goto loop
set "input=%input:"=%"
if not exist "%input%" (
    echo [ERROR] File not found: "%input%"
    echo.
    goto loop
)
for %%F in ("%input%") do set "filename=%%~nF"
set "FILE_IN=%input%"
set "FILE_OUT=%~dp0%filename%_h264.mkv"
echo.
echo [START] "%FILE_IN%"
echo [OUT]   "%FILE_OUT%"
echo.
if exist "%FILE_OUT%" del /f /q "%FILE_OUT%"
"%FFMPEG%" -y -stats -stats_period 1 -i "%FILE_IN%" -filter_complex "[0:v]scale=iw*sar:ih,setsar=1,split[L][R];[L]crop=iw/2:ih:0:0[Lh];[R]crop=iw/2:ih:iw/2:0[Rh];[Lh]crop=min(iw\,ih*400/240):ih[Lc];[Rh]crop=min(iw\,ih*400/240):ih[Rc];[Lc][Rc]hstack[Vs];[Vs]format=yuv420p16le,zscale=w=800:h=240:m=bt709:min=bt709:filter=spline36,format=yuv420p,setsar=1[V]" -map "[V]" -map "0:a?" -sws_dither ed -c:v libx264 -preset slow -crf 14 -profile:v high -level 3.1 -fps_mode cfr -x264-params "aq-mode=3:aq-strength=1.0:qcomp=0.65:ref=4:bframes=4:no-fast-pskip=1" -color_primaries bt709 -color_trc bt709 -colorspace bt709 -c:a aac -b:a 128k -ac 2 -ar 48000 "%FILE_OUT%"
if %errorlevel%==0 (
    echo.
    echo [DONE] Success!
) else (
    echo.
    echo [FAIL] Check file or ffmpeg installation
)
echo.
goto loop