@echo off
REM Dump the full 1MB flash of an SD-WIFI board (use on boards still running
REM STOCK firmware, before flashing turbo - keeps a restorable original).
REM Board prep: switch to USB2UART, hold FLSH while plugging USB, release.
REM Usage: backup_stock.bat COM7 [output.bin]

if "%~1"=="" (
  echo Usage: backup_stock.bat COMx [output.bin]
  exit /b 1
)
set PORT=%~1
set OUT=%~2
if "%OUT%"=="" set OUT=%~dp0firmware\stock_backup_1MB.bin

python -m esptool --chip esp8266 --port %PORT% --baud 460800 read_flash 0x0 0x100000 "%OUT%"
echo.
echo Restore any board to stock later with:
echo   python -m esptool --chip esp8266 --port COMx --baud 460800 write_flash --flash_mode dout 0x0 "%OUT%"
