@echo off
setlocal
cd /d %~dp0

echo ==========================================
echo   StudySense One-Click Start
echo ==========================================
echo.

REM ============ 1. MySQL (port 3306) ============
echo [1/4] Ensure MySQL (port 3306) is running ...
docker inspect studysense-mysql >nul 2>&1
if errorlevel 1 (
    echo   MySQL container not found, creating it ...
    docker run -d --name studysense-mysql --restart unless-stopped ^
        -e MYSQL_ROOT_PASSWORD=root123456 ^
        -e MYSQL_DATABASE=studysense ^
        -e MYSQL_USER=studysense ^
        -e MYSQL_PASSWORD=studysense123 ^
        -e TZ=Asia/Shanghai ^
        -p 3306:3306 ^
        mysql:8.0 ^
        --character-set-server=utf8mb4 ^
        --collation-server=utf8mb4_unicode_ci ^
        --default-time-zone=+08:00
    if errorlevel 1 (
        echo.
        echo FAILED to start MySQL! Is Docker Desktop running?
        pause
        exit /b 1
    )
    echo   MySQL container created.
) else (
    docker start studysense-mysql >nul 2>&1
    echo   MySQL container exists, ensuring it is started.
)
call :wait_tcp 3306
echo   MySQL ready.

REM ============ 2. EMQX / MQTT (port 1883) ============
REM NOTE: port 8083 is reserved by Windows Hyper-V / WinNAT, so it is skipped.
echo.
echo [2/4] Ensure EMQX / MQTT (port 1883) is running ...
docker inspect studysense-emqx >nul 2>&1
if errorlevel 1 (
    echo   EMQX container not found, creating it ...
    docker run -d --name studysense-emqx --restart unless-stopped ^
        -e TZ=Asia/Shanghai ^
        -p 1883:1883 ^
        -p 18083:18083 ^
        emqx/emqx:5.8.3
    if errorlevel 1 (
        echo.
        echo FAILED to start EMQX! Is Docker Desktop running?
        pause
        exit /b 1
    )
    echo   EMQX container created.
) else (
    docker start studysense-emqx >nul 2>&1
    echo   EMQX container exists, ensuring it is started.
)
call :wait_tcp 1883
echo   EMQX ready.

REM ============ 3. FastAPI backend ============
echo.
echo [3/4] Starting FastAPI backend (http://localhost:8800) ...
start "StudySense-Backend" /D "%CD%\backend" cmd /k ".\.venv\Scripts\python.exe run.py"

REM ============ 4. Frontend Vite ============
echo.
echo [4/4] Starting frontend dev server (http://localhost:5173) ...
start "StudySense-Frontend" /D "%CD%\frontend" cmd /k "npm run dev"

echo.
echo ==========================================
echo   All up and running!
echo     Frontend : http://localhost:5173
echo     API docs : http://localhost:8800/docs
echo     EMQX UI  : http://localhost:18083  (admin/public)
echo   (Close each terminal window to stop it)
echo ==========================================
pause
exit /b 0

REM ============ helper: wait until a TCP port accepts connections ============
:wait_tcp
set WP=%1
set /a WC=0
:wait_tcp_loop
set /a WC+=1
if %WC% gtr 90 (
    echo   TIMEOUT: port %WP% did not open within 3 minutes.
    exit /b 1
)
powershell -NoProfile -Command "try{$c=New-Object Net.Sockets.TcpClient;$c.Connect('127.0.0.1',%WP%);$c.Close();exit 0}catch{exit 1}" >nul 2>&1
if errorlevel 1 (
    timeout /t 2 /nobreak >nul
    goto wait_tcp_loop
)
exit /b 0