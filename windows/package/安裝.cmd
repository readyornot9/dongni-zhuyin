@echo off
chcp 65001 >nul
title 安裝懂你注音
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0app\setup\install.ps1"
if errorlevel 1 (echo. & echo 安裝沒有完成，請看上面的訊息。)
echo.
pause
