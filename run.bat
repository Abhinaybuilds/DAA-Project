@echo off
title Smart Ambulance Route Finder - DAA Project
cd /d "%~dp0"

echo =====================================================================
echo          SMART AMBULANCE ROUTE FINDER - DAA MINI PROJECT
echo =====================================================================
echo Stack: Lightweight Backend (Python / Node.js / C) + Vanilla Frontend
echo Features: Dijkstra O(V^2), Rendezvous Scan, Physical Animation, Live Timer
echo =====================================================================
echo.

set SERVER_STARTED=0

:: Check if Python is available
where python >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [1/2] Starting Python backend server on http://localhost:8080 ...
    start "" http://localhost:8080
    python server.py 8080
    exit /b
)

:: Check if Node.js is available
where node >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [1/2] Starting Node.js backend server on http://localhost:8080 ...
    start "" http://localhost:8080
    node server.js
    exit /b
)

:: Check if compiled C server exists or can be compiled
if not exist "server.exe" (
    where gcc >nul 2>nul
    if %ERRORLEVEL% EQU 0 (
        echo Compiling native backend server with GCC...
        gcc -Wall -O2 server.c -o server.exe -lws2_32
    ) else (
        if exist "C:\MinGW\bin\gcc.exe" (
            echo Compiling native backend server with MinGW GCC...
            C:\MinGW\bin\gcc.exe -Wall -O2 server.c -o server.exe -lws2_32
        )
    )
)

if exist "server.exe" (
    echo [1/2] Launching native backend server on http://localhost:8080 ...
    start "" http://localhost:8080
    server.exe
    exit /b
)

:: Fallback directly to index.html with client-side engine
echo [Fallback] Opening dashboard in standalone client mode...
start "" index.html
