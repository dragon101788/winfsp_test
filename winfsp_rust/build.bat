@echo off
:: 编译脚本
cd /d "%~dp0"
cargo build --release
if %ERRORLEVEL% equ 0 (
    echo [OK] 编译成功: target\release\hello_fs.exe
) else (
    echo [ERROR] 编译失败
    exit /b 1
)
