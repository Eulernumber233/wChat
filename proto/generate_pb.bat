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

REM =====================================================================
REM Also regenerate Python gRPC stubs for AgentServer (M2+).
REM Uses the venv's grpc_tools so it picks up a matching grpcio / protobuf.
REM Skipped gracefully if the venv doesn't exist yet (dev hasn't set up
REM AgentServer).
REM =====================================================================

set AGENT_PY=..\wChat_AgentServer\.venv\Scripts\python.exe
set AGENT_GEN_DIR=..\wChat_AgentServer\app\rpc\gen

if not exist "%AGENT_PY%" (
    echo.
    echo [skip] AgentServer venv not found at %AGENT_PY%
    echo        Skipping Python stub generation.
    goto :done
)

if not exist "%AGENT_GEN_DIR%" (
    mkdir "%AGENT_GEN_DIR%"
)

echo Generating protobuf + gRPC Python files ...
"%AGENT_PY%" -m grpc_tools.protoc ^
    -I. ^
    --python_out="%AGENT_GEN_DIR%" ^
    --grpc_python_out="%AGENT_GEN_DIR%" ^
    message.proto
if errorlevel 1 (
    echo [ERROR] python grpc_tools.protoc failed.
    pause
    exit /b 1
)

REM Ensure the gen directory is an importable package. Some tools also
REM need a guard so the generated message_pb2_grpc.py can `import message_pb2`
REM locally — we fix that by writing a tiny __init__.py that extends sys.path.
> "%AGENT_GEN_DIR%\__init__.py" echo import os, sys
>> "%AGENT_GEN_DIR%\__init__.py" echo sys.path.insert(0, os.path.dirname(__file__))

:done
echo.
echo Done. Generated files:
dir /b message.pb.* message.grpc.pb.*
if exist "%AGENT_GEN_DIR%\message_pb2.py" (
    echo [python] %AGENT_GEN_DIR%\message_pb2.py
    echo [python] %AGENT_GEN_DIR%\message_pb2_grpc.py
)
echo.
pause
endlocal
