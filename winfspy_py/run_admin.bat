@echo off
cd /d "%~dp0"

echo [*] Cleaning up stale mount point...
if exist "D:\test" (
    rmdir "D:\test" 2>nul
    echo [*] Removed D:\test
)

echo [*] Starting winfspy memory filesystem...
echo [*] Mount point: D:\test
echo.

python hello_fs.py

echo.
echo [*] Exited
pause
