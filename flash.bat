@echo off
REM Flash SD-WIFI board over USB
REM Usage: flash.bat COM5 [path\to\firmware.bin]
REM Board prep: switch to USB2UART, hold FLSH while plugging USB, release.

if "%~1"=="" (
  echo Usage: flash.bat COMx [firmware.bin]
  exit /b 1
)

set PORT=%~1
set BIN=%~2
if "%BIN%"=="" set BIN=%~dp0firmware\firmware.bin

python -m esptool --chip esp8266 --port %PORT% --baud 460800 write_flash --flash_mode dout --flash_size 1MB 0x0 "%BIN%"
if errorlevel 1 (
  echo.
  echo If esptool is missing:  python -m pip install esptool
  echo If flashing fails at 460800, try editing this file to use --baud 115200
)
