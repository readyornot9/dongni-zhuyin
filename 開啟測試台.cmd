@echo off
chcp 65001 >nul
cd /d "%~dp0"
if not exist "web\dict.js" (
  echo 第一次使用，正在產生詞庫…
  node tools\build-dict.js || (echo 需要先安裝 Node.js & pause & exit /b 1)
)
start "" "%~dp0web\index.html"
