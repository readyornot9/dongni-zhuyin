@echo off
rem 編譯輸入法外殼：64 位元（一般程式）和 32 位元（舊程式）各一份，輸出到 windows\build\
setlocal
chcp 65001 >nul
set ROOT=%~dp0
set OUT=%ROOT%build
if not exist "%OUT%" mkdir "%OUT%"
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VS=%%i
if not defined VS (echo 找不到 Visual Studio Build Tools & exit /b 1)

for %%A in (x64 x86) do (
  call :build %%A || exit /b 1
)
echo 完成
exit /b 0

:build
setlocal
chcp 65001 >nul
call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" %1 >nul || exit /b 1
set OBJ=%OUT%\obj-%1
if not exist "%OBJ%" mkdir "%OBJ%"
rc /nologo /fo "%OBJ%\ShunDaTip.res" "%ROOT%tip\ShunDaTip.rc" || exit /b 1
cl /nologo /utf-8 /std:c++17 /O2 /MT /EHsc /W3 /DUNICODE /D_UNICODE /Fo"%OBJ%\\" ^
  "%ROOT%tip\Dll.cpp" "%ROOT%tip\TextService.cpp" "%ROOT%tip\CandWnd.cpp" "%ROOT%tip\Pipe.cpp" "%ROOT%tip\Util.cpp" "%ROOT%tip\LangBar.cpp" ^
  "%OBJ%\ShunDaTip.res" /LD /Fe"%OUT%\ShunDaTip-%1.dll" ^
  /link /DEF:"%ROOT%tip\ShunDaTip.def" user32.lib gdi32.lib advapi32.lib ole32.lib uuid.lib winmm.lib oleaut32.lib || exit /b 1
endlocal
exit /b 0
