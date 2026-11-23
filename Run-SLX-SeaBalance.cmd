@echo off
rem Silicon LogiX / SLX SeaBalance
rem Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
rem Author: Marco Pezzullo
rem License: Silicon LogiX Evaluation License 1.0. See LICENSE.

setlocal
if exist "%~dp0bin\SLX-SeaBalance.exe" (
  start "" "%~dp0bin\SLX-SeaBalance.exe" %*
  exit /b 0
)
if exist "%~dp0build\windows\bin\SLX-SeaBalance.exe" (
  start "" "%~dp0build\windows\bin\SLX-SeaBalance.exe" %*
  exit /b 0
)
echo Build SLX SeaBalance with scripts\build-windows.ps1 or use the Windows evaluation package.
exit /b 1
