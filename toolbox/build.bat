@echo off
setlocal
set "MINGW=C:\Program Files (x86)\Dev-Cpp\MinGW64\bin"
set "PATH=%MINGW%;%PATH%"
cd /d "%~dp0"

if not exist build mkdir build
if not exist dist mkdir dist

set "FLAGS=-std=c++11 -O2 -DUNICODE -D_UNICODE -Wall -Wno-unused-parameter -IF:\projects\toolbox\src"
set "LDFLAGS=-municode -mwindows -static -static-libgcc -static-libstdc++"
set "LIBS=-lgdiplus -luser32 -lcomctl32 -lgdi32 -lole32 -lshlwapi -lshell32 -lwinspool -ladvapi32 -lws2_32"

set "SRCS=src\main.cpp src\ui.cpp src\launcher.cpp src\theme.cpp src\stats.cpp src\proxy.cpp src\main_window.cpp"

echo [1/3] Compiling resources...
windres -I src -i src\app.rc -o build\app_res.o
if errorlevel 1 goto :fail

echo [2/3] Compiling sources...
for %%F in (%SRCS%) do (
    set "OBJ=build\%%~nF.o"
    echo   %%F
    g++ %FLAGS% -c "%%F" -o "build\%%~nF.o"
    if errorlevel 1 goto :fail
)

echo [3/3] Linking...
g++ -o dist\Toolkit.exe ^
    build\main.o build\ui.o build\launcher.o build\theme.o build\stats.o build\proxy.o build\main_window.o build\app_res.o ^
    %LDFLAGS% %LIBS%
if errorlevel 1 goto :fail

echo.
echo Build OK: dist\Toolkit.exe
exit /b 0

:fail
echo.
echo Build FAILED
exit /b 1
