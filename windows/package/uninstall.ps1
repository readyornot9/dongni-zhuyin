# 懂你注音移除程式（安裝包版，由「移除.cmd」呼叫）：從鍵盤清單拿掉、解除註冊、停掉引擎服務、刪掉程式
# 學習紀錄（%APPDATA%\ShunDa）會保留，重新安裝後還在；想一起清掉就手動刪那個資料夾
$ErrorActionPreference = 'Stop'
$tip = '0404:{EFD18DC6-22A2-41AE-B1DD-EC32380B91E9}{E3DBE072-9942-41DE-AB4F-29373C1BC196}'
$base = Join-Path $env:LOCALAPPDATA 'ShunDa'

$list = Get-WinUserLanguageList
foreach ($l in $list) { if ($l.InputMethodTips -contains $tip) { $l.InputMethodTips.Remove($tip) | Out-Null } }
Set-WinUserLanguageList $list -Force -WarningAction SilentlyContinue

$bin = Join-Path $base 'bin'
$dll64 = Get-ChildItem $bin -Filter 'ShunDaTip-x64-*.dll' -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
$dll86 = Get-ChildItem $bin -Filter 'ShunDaTip-x86-*.dll' -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
$parts = @()
if ($dll64) { $parts += "& `"$env:windir\System32\regsvr32.exe`" /u /s `"$($dll64.FullName)`"" }
if ($dll86) { $parts += "& `"$env:windir\SysWOW64\regsvr32.exe`" /u /s `"$($dll86.FullName)`"" }
if ($parts) {
  Write-Host '接下來 Windows 會詢問「是否允許變更」，請按「是」。'
  Start-Process powershell -Verb RunAs -Wait -WindowStyle Hidden -ArgumentList '-NoProfile', '-Command', ($parts -join '; ')
}

Get-CimInstance Win32_Process -Filter "Name='node.exe'" | Where-Object { $_.CommandLine -match 'ime-server\.js' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Start-Sleep -Milliseconds 500
Remove-Item (Join-Path $base 'app') -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item 'HKCU:\Software\ShunDa' -Recurse -ErrorAction SilentlyContinue
Remove-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' 'DongNiZhuyin' -ErrorAction SilentlyContinue

Write-Host ''
Write-Host '已移除懂你注音。'
Write-Host '還開著的程式可能仍載入著輸入法檔案，重新開機後會完全清掉。'
