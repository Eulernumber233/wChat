@echo off
REM =====================================================================
REM wChat ChatServer cluster launcher
REM
REM Launches chatserver1 (TCP 8090 / gRPC 50055)
REM      and chatserver2 (TCP 8091 / gRPC 50056)
REM in two separate windows, using the same exe + different config files.
REM
REM Child windows use "cmd /k" so that if the exe crashes, the window
REM stays open and you can see the error. Press any key to close.
REM
REM To add a 3rd / Nth instance:
REM   1) add configs/chatserverN.ini (edit SelfServer / PeerServer)
REM   2) append one more "start" line at the bottom
REM =====================================================================

setlocal

REM cd into the directory this script lives in
cd /d "%~dp0"

REM Path to the built exe. Change to x64\Release\... if you build Release.
REM Current project output name is wChat_server_tcp_1.exe (legacy from vcxproj
REM ProjectName). If you later rename ProjectName to wChat_server_tcp, drop the _1.
set EXE=x64\Debug\wChat_server_tcp_1.exe

if not exist "%EXE%" (
    echo [ERROR] %EXE% not found. Build the project in Visual Studio first.
    pause
    exit /b 1
)

if not exist "configs\chatserver1.ini" (
    echo [ERROR] configs\chatserver1.ini not found.
    pause
    exit /b 1
)

if not exist "configs\chatserver2.ini" (
    echo [ERROR] configs\chatserver2.ini not found.
    pause
    exit /b 1
)

echo Launching chatserver1 ...
start "chatserver1" /D "%CD%" cmd /k "%EXE% configs\chatserver1.ini"

REM Small delay so the two startup logs do not interleave
timeout /t 2 /nobreak >nul

echo Launching chatserver2 ...
start "chatserver2" /D "%CD%" cmd /k "%EXE% configs\chatserver2.ini"

echo.
echo Both instances launched in separate windows.
echo Close the windows (or Ctrl+C inside them) to stop each instance.
echo.
pause
endlocal
