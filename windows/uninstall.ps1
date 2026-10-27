# 移除懂你注音：從鍵盤清單拿掉、解除註冊、停掉引擎服務。學習紀錄（%APPDATA%\ShunDa）會保留。
$ErrorActionPreference = 'Stop'
$tip = '0404:{EFD18DC6-22A2-41AE-B1DD-EC32380B91E9}{E3DBE072-9942-41DE-AB4F-29373C1BC196}'

$list = Get-WinUserLanguageList
foreach ($l in $list) { if ($l.InputMethodTips -contains $tip) { $l.InputMethodTips.Remove($tip) | Out-Null } }
Set-WinUserLanguageList $list -Force

$bin = Join-Path $env:LOCALAPPDATA 'ShunDa\bin'
$dll64 = Get-ChildItem $bin -Filter 'ShunDaTip-x64-*.dll' -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
$dll86 = Get-ChildItem $bin -Filter 'ShunDaTip-x86-*.dll' -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
$parts = @()
if ($dll64) { $parts += "& `"$env:windir\System32\regsvr32.exe`" /u /s `"$($dll64.FullName)`"" }
if ($dll86) { $parts += "& `"$env:windir\SysWOW64\regsvr32.exe`" /u /s `"$($dll86.FullName)`"" }
if ($parts) { Start-Process powershell -Verb RunAs -Wait -WindowStyle Hidden -ArgumentList '-NoProfile', '-Command', ($parts -join '; ') }

Get-CimInstance Win32_Process -Filter "Name='node.exe'" | Where-Object { $_.CommandLine -match 'ime-server\.js' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Remove-Item 'HKCU:\Software\ShunDa' -Recurse -ErrorAction SilentlyContinue
Write-Host '已移除。DLL 檔還被程式載入著的話，重新開機後可以刪掉 %LOCALAPPDATA%\ShunDa\bin。'
