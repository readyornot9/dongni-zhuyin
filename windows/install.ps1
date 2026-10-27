# 安裝懂你注音（給目前的使用者）
# 1. 把編好的外殼 DLL 複製到 %LOCALAPPDATA%\ShunDa\bin（每次用新檔名，舊的還被程式載入著也能更新）
# 2. 記下 Node 和引擎服務的位置（外殼找不到服務時會自己啟動）
# 3. 向 Windows 註冊輸入法（需要系統管理員權限，會跳出「是否允許變更」）
# 4. 加進「中文（台灣）」的鍵盤清單
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSCommandPath
$tip = '0404:{EFD18DC6-22A2-41AE-B1DD-EC32380B91E9}{E3DBE072-9942-41DE-AB4F-29373C1BC196}'

$node = (Get-Command node -ErrorAction Stop).Source
$script = Join-Path $root 'server\ime-server.js'
foreach ($a in 'x64', 'x86') {
  if (-not (Test-Path (Join-Path $root "build\ShunDaTip-$a.dll"))) { throw "找不到 build\ShunDaTip-$a.dll，請先執行 build.cmd" }
}

$bin = Join-Path $env:LOCALAPPDATA 'ShunDa\bin'
New-Item -ItemType Directory -Force $bin | Out-Null
$stamp = Get-Date -Format 'yyyyMMddHHmmss'
$dll64 = Join-Path $bin "ShunDaTip-x64-$stamp.dll"
$dll86 = Join-Path $bin "ShunDaTip-x86-$stamp.dll"
Copy-Item (Join-Path $root 'build\ShunDaTip-x64.dll') $dll64
Copy-Item (Join-Path $root 'build\ShunDaTip-x86.dll') $dll86

New-Item -Force 'HKCU:\Software\ShunDa' | Out-Null
Set-ItemProperty 'HKCU:\Software\ShunDa' NodePath $node
Set-ItemProperty 'HKCU:\Software\ShunDa' ServerScript $script

# 註冊（一次提權做完 64 和 32 位元）
$cmd = "& `"$env:windir\System32\regsvr32.exe`" /s `"$dll64`"; if (`$LASTEXITCODE) { exit 1 }; & `"$env:windir\SysWOW64\regsvr32.exe`" /s `"$dll86`"; exit `$LASTEXITCODE"
$p = Start-Process powershell -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ArgumentList '-NoProfile', '-Command', $cmd
if ($p.ExitCode) { throw "註冊失敗（代碼 $($p.ExitCode)）" }

# 加進鍵盤清單
$list = Get-WinUserLanguageList
$zh = $list | Where-Object { $_.LanguageTag -eq 'zh-Hant-TW' -or $_.LanguageTag -eq 'zh-TW' } | Select-Object -First 1
if (-not $zh) {
  $list.Add('zh-Hant-TW')
  $zh = $list | Where-Object { $_.LanguageTag -eq 'zh-Hant-TW' } | Select-Object -First 1
}
if (-not ($zh.InputMethodTips -contains $tip)) { $zh.InputMethodTips.Add($tip) }
Set-WinUserLanguageList $list -Force

# 清掉以前版本的 DLL（還被程式載入著的刪不掉，下次再清）
Get-ChildItem $bin -Filter 'ShunDaTip-*.dll' | Where-Object { $_.FullName -ne $dll64 -and $_.FullName -ne $dll86 } | ForEach-Object { Remove-Item $_.FullName -ErrorAction SilentlyContinue }

Write-Host "安裝完成：$dll64"
Write-Host '按 Win＋空白鍵 切換到「懂你注音」就能用。已經開著的程式要重開才會載入新版。'
