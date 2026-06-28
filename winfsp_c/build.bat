@echo off
setlocal enabledelayedexpansion

echo === WinFSP HelloFS Build ===
echo.

set WINFSP_INC=C:\Program Files (x86)\WinFsp\inc
set WINFSP_LIB=C:\Program Files (x86)\WinFsp\lib

if not exist "%WINFSP_INC%\winfsp\winfsp.h" (
    echo [ERROR] WinFSP headers not found
    exit /b 1
)

:: Try MSVC 2022 Community
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist "!VCVARS!" (
    echo [MSVC] Setting up environment...
    call "!VCVARS!" x64 >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        echo [MSVC] Compiling...
        cl /nologo /W3 /O2 ^
           /I"%WINFSP_INC%" ^
           hello_fs.c ^
           /link /LIBPATH:"%WINFSP_LIB%" winfsp-x64.lib DelayImp.lib ^
           /DELAYLOAD:winfsp-x64.dll ^
           /SUBSYSTEM:CONSOLE /OUT:hello_fs.exe
        if !ERRORLEVEL! equ 0 (
            if exist hello_fs.exe (
                echo [OK] Build successful: hello_fs.exe
                exit /b 0
            )
        )
        echo [ERROR] MSVC compile failed
        exit /b 1
    )
)

:: Try MinGW gcc in PATH
where gcc >nul 2>&1
if %ERRORLEVEL% equ 0 goto :mingw_build

:: Try MinGW in msys2
if exist "C:\msys64\mingw64\bin\gcc.exe" (
    set "PATH=C:\msys64\mingw64\bin;%PATH%"
    goto :mingw_build
)

:: Try MinGW in Git for Windows
if exist "C:\Program Files\Git\mingw64\bin\gcc.exe" (
    set "PATH=C:\Program Files\Git\mingw64\bin;%PATH%"
    goto :mingw_build
)

echo [ERROR] No compiler found (MSVC 2022 or MinGW-w64)
exit /b 1

:mingw_build
echo [MinGW] Compiling...
gcc -O2 ^
    -I"%WINFSP_INC%" ^
    hello_fs.c ^
    -L"%WINFSP_LIB%" -lwinfsp-x64 ^
    -o hello_fs.exe ^
    -municode

if exist hello_fs.exe (
    echo [OK] Build successful: hello_fs.exe
) else (
    echo [ERROR] MinGW build failed
    exit /b 1
)
