@echo off
REM =====================================================================
REM Regenerate C++ protobuf / gRPC files from message.proto
REM
REM Run this script after editing message.proto.
REM Output: message.pb.h, message.pb.cc, message.grpc.pb.h, message.grpc.pb.cc
REM         (all in the same directory as this script)
REM
REM All 3 C++ services reference these generated files via relative paths
REM in their vcxproj, so no manual copying is needed.
REM =====================================================================

setlocal

cd /d "%~dp0"

set PROTOC=D:\library\grpc\visualpro\third_party\protobuf\Debug\protoc.exe
set GRPC_PLUGIN=D:\library\grpc\visualpro\Debug\grpc_cpp_plugin.exe

if not exist "%PROTOC%" (
    echo [ERROR] protoc not found at: %PROTOC%
    echo         Edit this script and set PROTOC to the correct path.
    pause
    exit /b 1
)

if not exist "%GRPC_PLUGIN%" (
    echo [ERROR] grpc_cpp_plugin not found at: %GRPC_PLUGIN%
    echo         Edit this script and set GRPC_PLUGIN to the correct path.
    pause
    exit /b 1
)

echo Generating protobuf C++ files ...
"%PROTOC%" --cpp_out=. "message.proto"
if errorlevel 1 (
    echo [ERROR] protoc --cpp_out failed.
    pause
    exit /b 1
)

echo Generating gRPC C++ files ...
"%PROTOC%" -I="." --grpc_out="." --plugin=protoc-gen-grpc="%GRPC_PLUGIN%" "message.proto"
if errorlevel 1 (
    echo [ERROR] protoc --grpc_out failed.
    pause
    exit /b 1
)

echo.
echo Done. Generated files:
dir /b message.pb.* message.grpc.pb.*
echo.
pause
endlocal
