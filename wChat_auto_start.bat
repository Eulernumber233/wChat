
 
@echo off
chcp 65001 >nul
echo ==============================================
echo 开始执行：清库→编译服务→编译客户端→启动程序
echo ==============================================
timeout /t 2 /nobreak >nul
 
:: -------------------------- 1. 清空Redis数据库 --------------------------
echo 正在清空数据库...
:: 清空Redis（自定义：清空所有key，默认端口6380）
redis-cli -p 6380 -a 123456 FLUSHALL
if %errorlevel% equ 0 (echo Redis数据清空成功) else (echo Redis操作失败，请检查Redis是否启动！& pause & exit)
 
:: -------------------------- 2. 编译VS2022的4个服务端 --------------------------
echo 正在编译4个服务端...
:: 自定义VS2022路径（Community版默认路径）
set VS_MSBuild="D:\visual studio\MSBuild\Current\Bin\MSBuild.exe"
:: 自定义4个服务的解决方案路径（按实际项目调整）
set Server1_Sln="D:\CPP\network\Chat\wChat_server_gate\wChat_server_gate.sln"
set Server2_Sln="D:\CPP\network\Chat\wChat_server_tcp_1\wChat_server_tcp.sln"
set Server3_Sln="D:\CPP\network\Chat\wChat_server_tcp_2\wChat_server_tcp.sln"
set Server4_Sln="D:\CPP\network\Chat\wChat_StatusServer\wChat_StatusServer.sln"
:: 编译配置（自定义为Debug，可改为Release）
set Config=Debug
 
:: 批量编译4个服务
%VS_MSBuild% %Server1_Sln% /t:Build /p:Configuration=%Config% /nologo
%VS_MSBuild% %Server2_Sln% /t:Build /p:Configuration=%Config% /nologo
%VS_MSBuild% %Server3_Sln% /t:Build /p:Configuration=%Config% /nologo
%VS_MSBuild% %Server4_Sln% /t:Build /p:Configuration=%Config% /nologo
if %errorlevel% equ 0 (echo 4个服务端编译成功) else (echo 服务端编译失败！& pause & exit)
 
:: -------------------------- 3. 编译Qt客户端 --------------------------
echo 正在编译Qt客户端...
:: 自定义Qt的qmake路径（Qt 6.5.0 MSVC2019 64位默认路径）
set Qt_QMake="D:\QT\6.9.1\mingw_64\bin\qmake6.exe"
:: 自定义客户端的.pro文件路径
set Client_Pro="D:\CPP\network\Chat\wChat_client\wChat_client.pro"
:: 编译输出目录（避免文件混乱）
set Client_Build="D:\CPP\network\Chat\wChat_client\build"
 
:: 编译客户端
md %Client_Build% >nul 2>nul
cd /d %Client_Build%
%Qt_QMake% %Client1_Pro% -r -spec win32-msvc CONFIG+=%Config%
nmake >nul 2>nul
 
cd /d %~dp0
if %errorlevel% equ 0 (echo Qt客户端编译成功) else (echo 客户端编译失败，请检查Qt路径！& pause & exit)
 
:: -------------------------- 4. 启动所有服务和客户端 --------------------------
echo.
echo 正在启动所有程序...
:: 启动服务端（Debug版默认输出路径，按实际调整）
start "wChat_server_gate" "D:\CPP\network\Chat\wChat_server_gate\x64\Debug\wChat_server_gate.exe"
start "wChat_server_tcp_1" "D:\CPP\network\Chat\wChat_server_tcp_1\x64\Debug\wChat_server_tcp_1.exe"
start "wChat_server_tcp_2" "D:\CPP\network\Chat\wChat_server_tcp_2\x64\Debug\wChat_server_tcp_2.exe"
start "wChat_server_Status" "D:\CPP\network\Chat\wChat_StatusServer\x64\Debug\StatusServer.exe"
:: 启动客户端（Debug版默认输出路径）
start "" "D:\ChatProject\Client1\build\debug\Client1.exe"
start "" "D:\ChatProject\Client2\build\debug\Client2.exe"
 
echo.
echo ==============================================
echo 所有操作完成！已启动4个服务端 + 客户端
echo ==============================================
:: 保持窗口
pause
 
