@echo off
chcp 65001 >nul 2>&1  # 解决中文乱码
echo ==============================================
echo 正在启动所有工具，请稍候...
echo ==============================================

:: -------------------------- 1. 启动 MySQL--------------------------------
echo [1/7] 启动 MySQL...
net start mysql  
if %errorlevel% equ 0 (
    echo MySQL 启动成功！
) else (
    echo 注意：MySQL 启动失败，可能已启动或服务名错误！
)
timeout /t 2 /nobreak >nul  # 等待 2 秒，确保服务启动稳定


:: -------------------------- 2. 启动 Redis-----------------------------
echo [2/7] 启动 Redis...
set "REDIS_PATH=D:\Redis\Redis-x64-5.0.14.1\redis-server.exe"  
set "REDIS_CONF=D:\Redis\Redis-x64-5.0.14.1\redis.windows.conf" 
start "Redis" "%REDIS_PATH%" "%REDIS_CONF%"  # start 后加空引号避免路径含空格报错
if %errorlevel% equ 0 (
    echo Redis 启动成功！
) else (
    echo 注意：Redis 启动失败，路径可能错误！
)
timeout /t 2 /nobreak >nul


:: -------------------------- 3. 启动 Qt Creator----------------------------------
echo [3/7] 启动 Qt Creator...
set "QT_PATH=D:\QT\Tools\QtCreator\bin\qtcreator.exe" 
start "" "%QT_PATH%"
if %errorlevel% equ 0 (
    echo Qt Creator 启动成功！
) else (
    echo 注意：Qt Creator 启动失败，路径可能错误！
)
timeout /t 2 /nobreak >nul


:: -------------------------- 4. 启动 4 个 Visual Studio 2022---------------------
echo [4/7] 启动第 1 个 VS2022 并打开项目...
set "VS_PATH=D:\visual studio\Common7\IDE\devenv.exe"  # 替换为你的VS路径
set "VS_PROJECT1=D:\CPP\network\Chat\wChat_server_gate\wChat_server_gate.sln"  # 第1个项目的.sln或.vcxproj路径
start "" "%VS_PATH%" "%VS_PROJECT1%"
if %errorlevel% equ 0 (echo 第 1 个 VS2022 及项目启动成功！) else (echo 第 1 个 VS2022 启动失败！)

echo [5/7] 启动第 2 个 VS2022 并打开项目...
set "VS_PROJECT2=D:\CPP\network\Chat\wChat_StatusServer\StatusServer.sln" # 第2个项目路径
start "" "%VS_PATH%" "%VS_PROJECT2%"
if %errorlevel% equ 0 (echo 第 2 个 VS2022 及项目启动成功！) else (echo 第 2 个 VS2022 启动失败！)

echo [6/7] 启动第 3 个 VS2022 并打开项目...
set "VS_PROJECT3=D:\CPP\network\Chat\wChat_server_tcp_1\wChat_server_tcp.sln"  # 第3个项目路径
start "" "%VS_PATH%" "%VS_PROJECT3%"
if %errorlevel% equ 0 (echo 第 3 个 VS2022 及项目启动成功！) else (echo 第 3 个 VS2022 启动失败！)

echo [7/7] 启动第 4 个 VS2022 并打开项目...
set "VS_PROJECT4=D:\CPP\network\Chat\wChat_server_tcp_2\wChat_server_tcp.sln" # 第4个项目路径
start "" "%VS_PATH%" "%VS_PROJECT4%"
if %errorlevel% equ 0 (echo 第 4 个 VS2022 及项目启动成功！) else (echo 第 4 个 VS2022 启动失败！)




:: 保持窗口
pause