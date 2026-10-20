@echo off
setlocal
cd /d "%~dp0"
:menu
cls
echo.
echo   ZAPPO 0.3.5  -  for the Anbernic RG35XX (original model) on GarlicOS
echo   ====================================================================
echo   (The same jobs with a window: "Zappo Setup.bat".)
echo   Put the GarlicOS card in this PC first (the partition with CFW, BIOS and Roms).
echo.
echo   1  Install / update Zappo on the card
echo   2  Add films and music from Downloads to the card
echo   3  Unpack downloaded video collections (zips in Downloads), then 2
echo   4  Box art, film posters and music covers
echo   5  Check the card (reads it all back; changes nothing)
echo   B  Boot logo and charging animation (needs the small boot partition showing)
echo   C  Extra cores pack from Downloads (newer emulator builds)
echo.
echo   U  Undo: GarlicOS's own cores back        V  Undo: GarlicOS's video player back
echo   X  Undo: boot logo and charging animation  R  Remove Zappo from the card
echo   0  Quit
echo.
set /p c=  Pick one:
set PS=powershell -NoProfile -ExecutionPolicy Bypass -File
if "%c%"=="1" (%PS% "%~dp0tools\install.ps1" & pause & goto menu)
if "%c%"=="2" (%PS% "%~dp0tools\add-media.ps1" & pause & goto menu)
if "%c%"=="3" (%PS% "%~dp0tools\unpack-videos.ps1" & pause & goto menu)
if "%c%"=="4" (%PS% "%~dp0tools\get-game-art.ps1" & %PS% "%~dp0tools\get-cover-art.ps1" & pause & goto menu)
if "%c%"=="5" (%PS% "%~dp0tools\check-card.ps1" & pause & goto menu)
if /i "%c%"=="B" (%PS% "%~dp0tools\boot-art.ps1" & pause & goto menu)
if /i "%c%"=="C" (%PS% "%~dp0tools\extra-cores.ps1" & pause & goto menu)
if /i "%c%"=="U" (%PS% "%~dp0tools\extra-cores.ps1" -Revert & pause & goto menu)
if /i "%c%"=="V" (%PS% "%~dp0tools\player-fix.ps1" -Revert & pause & goto menu)
if /i "%c%"=="X" (%PS% "%~dp0tools\boot-art.ps1" -Revert & pause & goto menu)
if /i "%c%"=="R" (%PS% "%~dp0tools\install.ps1" -Remove & pause & goto menu)
if "%c%"=="0" exit /b
goto menu
