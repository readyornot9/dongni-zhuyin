@echo off
chcp 65001 >nul
title 移除懂你注音
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0app\setup\uninstall.ps1"
echo.
pause
