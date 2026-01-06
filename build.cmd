@echo off
echo ========================================
echo   Building Better Autosprint...
echo ========================================

if not exist "src\bin" mkdir "src\bin"

windres src/Resources/resource.rc -o src/Resources/resource.o
g++ -std=c++17 src/main.cpp src/Resources/resource.o -o src/bin/Sprint.exe -luser32 -Os -s -static -static-libgcc -static-libstdc++ -fno-rtti -fno-exceptions -ffunction-sections -fdata-sections -Wl,--gc-sections

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [!] Build FAILED!
    pause
    exit /b %ERRORLEVEL%
)

strip --strip-all src/bin/Sprint.exe

ping -n 2 127.0.0.1 >nul
powershell -Command "(Get-Item 'src\bin\Sprint.exe').LastWriteTime = '01/01/2023 12:00:00'; (Get-Item 'src\bin\Sprint.exe').CreationTime = '01/01/2023 12:00:00'"

echo.
echo [OK] Build Successful: src\bin\Sprint.exe
ping -n 4 127.0.0.1 >nul
