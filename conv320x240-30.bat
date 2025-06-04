@echo off
setlocal enableextensions

set outdir=%~dp0\test_videos\
mkdir "%outdir%"

:loop
ffmpeg -y -i "%1" -c:v mjpeg -q:v 2 -vf "scale=iw*sar:ih,scale=320:240:force_original_aspect_ratio=decrease,pad=320:240:-1:-1" -r 30 -c:a pcm_s16le -ac 1 -ar 44100 "%outdir%%~n1.avi"
shift
if not "%~n1" == "" goto :loop

pause
