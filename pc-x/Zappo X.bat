@echo off
setlocal
cd /d "%~dp0"
:menu
cls
echo.
echo   ZAPPO X  -  for the Anbernic RG40XX H (and the RG35XX / RG40XX family) on Knulli
echo   ================================================================================
echo   Put the Knulli card in this PC first (its BATOCERA partition shows as a drive).
echo.
echo   1  Install / update Zappo X on the card
echo   2  Add the test pack (songs and films to try things with)
echo   3  Check the card (reads it back, changes nothing) and show what the handheld did
echo.
echo   R  Take Zappo X off (Knulli's own menu at the next start)
echo   0  Quit
echo.
set /p c=  Pick one:
set PS=powershell -NoProfile -ExecutionPolicy Bypass -File
if "%c%"=="1" (%PS% "%~dp0tools\install.ps1" & pause & goto menu)
if "%c%"=="2" (%PS% "%~dp0tools\install.ps1" -OnlyTestPack & pause & goto menu)
if "%c%"=="3" (%PS% "%~dp0tools\check.ps1" & pause & goto menu)
if /i "%c%"=="R" (%PS% "%~dp0tools\install.ps1" -Remove & pause & goto menu)
if "%c%"=="0" exit /b
goto menu
