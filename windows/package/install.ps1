# 懂你注音安裝程式（安裝包版，由「安裝.cmd」呼叫）
# 1. 停掉舊的引擎服務，把程式複製到 %LOCALAPPDATA%\ShunDa\app（學習紀錄在 %APPDATA%\ShunDa，不會動到）
# 2. 外殼 DLL 每次用新檔名複製到 %LOCALAPPDATA%\ShunDa\bin（舊的還被程式載入著也能更新）
# 3. 向 Windows 註冊輸入法（需要系統管理員權限，會跳出「是否允許變更」）
# 4. 加進「中文（台灣）」的鍵盤清單
$ErrorActionPreference = 'Stop'
$app = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$tip = '0404:{EFD18DC6-22A2-41AE-B1DD-EC32380B91E9}{E3DBE072-9942-41DE-AB4F-29373C1BC196}'
$base = Join-Path $env:LOCALAPPDATA 'ShunDa'
$dest = Join-Path $base 'app'
$bin = Join-Path $base 'bin'

Write-Host '正在安裝懂你注音…'
Get-ChildItem $app -Recurse -File | Unblock-File
Get-CimInstance Win32_Process -Filter "Name='node.exe'" | Where-Object { $_.CommandLine -match 'ime-server\.js' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Start-Sleep -Milliseconds 500

New-Item -ItemType Directory -Force $dest, $bin | Out-Null
foreach ($d in 'node', 'web', 'windows') {
  $to = Join-Path $dest $d
  if (Test-Path $to) { Remove-Item $to -Recurse -Force }
  Copy-Item (Join-Path $app $d) $to -Recurse -Force
}

$stamp = Get-Date -Format 'yyyyMMddHHmmss'
$dll64 = Join-Path $bin "ShunDaTip-x64-$stamp.dll"
$dll86 = Join-Path $bin "ShunDaTip-x86-$stamp.dll"
Copy-Item (Join-Path $app 'bin\ShunDaTip-x64.dll') $dll64
Copy-Item (Join-Path $app 'bin\ShunDaTip-x86.dll') $dll86
# 新式程式（沙盒）也要讀得到輸入法檔案：開放 ALL APPLICATION PACKAGES／ALL RESTRICTED APPLICATION PACKAGES 讀取
& icacls $bin /grant '*S-1-15-2-1:(OI)(CI)(RX)' '*S-1-15-2-2:(OI)(CI)(RX)' /T /Q | Out-Null

New-Item -Force 'HKCU:\Software\ShunDa' | Out-Null
Set-ItemProperty 'HKCU:\Software\ShunDa' NodePath (Join-Path $dest 'node\node.exe')
Set-ItemProperty 'HKCU:\Software\ShunDa' ServerScript (Join-Path $dest 'windows\server\ime-server.js')

Write-Host '接下來 Windows 會詢問「是否允許變更」，請按「是」。'
$cmd = "& `"$env:windir\System32\regsvr32.exe`" /s `"$dll64`"; if (`$LASTEXITCODE) { exit 1 }; & `"$env:windir\SysWOW64\regsvr32.exe`" /s `"$dll86`"; exit `$LASTEXITCODE"
try {
  $p = Start-Process powershell -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ArgumentList '-NoProfile', '-Command', $cmd
} catch { throw '沒有取得系統管理員權限，安裝取消。請再執行一次，並在詢問時按「是」。' }
if ($p.ExitCode) { throw "註冊輸入法失敗（代碼 $($p.ExitCode)）" }

$list = Get-WinUserLanguageList
$zh = $list | Where-Object { $_.LanguageTag -eq 'zh-Hant-TW' -or $_.LanguageTag -eq 'zh-TW' } | Select-Object -First 1
if (-not $zh) {
  $list.Add('zh-Hant-TW')
  $zh = $list | Where-Object { $_.LanguageTag -eq 'zh-Hant-TW' } | Select-Object -First 1
}
if (-not ($zh.InputMethodTips -contains $tip)) { $zh.InputMethodTips.Add($tip) }
Set-WinUserLanguageList $list -Force -WarningAction SilentlyContinue

# 引擎服務：登入 Windows 時自動在背景啟動；現在也先啟動一次（服務會脫離這個安裝視窗獨立執行）
$run = "`"$env:windir\System32\rundll32.exe`" `"$dll64`",RunServerW"
Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' 'DongNiZhuyin' $run
Start-Process "$env:windir\System32\rundll32.exe" -ArgumentList "`"$dll64`",RunServerW"

Get-ChildItem $bin -Filter 'ShunDaTip-*.dll' | Where-Object { $_.FullName -ne $dll64 -and $_.FullName -ne $dll86 } | ForEach-Object { Remove-Item $_.FullName -ErrorAction SilentlyContinue }

Write-Host ''
Write-Host '安裝完成！'
Write-Host '按 Win＋空白鍵，切換到「懂你注音」就能打字。'
Write-Host '已經開著的程式（例如瀏覽器、LINE）要關掉重開才會出現。'
