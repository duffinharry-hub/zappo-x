@echo off
rem Zappo Setup - the setup window. (Zappo.bat is the same kit as a text menu.)
start "" /b powershell -NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File "%~dp0Zappo Setup.ps1"
