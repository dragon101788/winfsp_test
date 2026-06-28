@echo off
:: 以管理员身份运行 hello_fs.exe
:: 要求 D:\test 目录不存在或为空
:: 日志写入 hello_fs.log（与 exe 同目录）
echo [*] Starting WinFSP Rust hello_fs...
echo [*] Make sure D:\test does NOT exist
echo.
cd /d "%~dp0"
target\debug\hello_fs.exe
pause
