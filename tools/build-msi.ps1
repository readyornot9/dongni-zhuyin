# 產生懂你注音的 MSI：全部裝到 Program Files\DongNiZhuyin（輸入法外殼 DLL＋Node 引擎＋詞庫／模型）
# 新式程式（Chrome、Teams、新版記事本）只載入放在受保護位置的輸入法，所以一定要裝在 Program Files
# 不用另外下載工具：直接用 Windows 內建的 Windows Installer COM（msi.dll）建資料庫，makecab 壓縮檔案
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-msi.ps1
# 先跑 windows\build.cmd 編好 DLL；Node.js 用這台電腦的 node.exe；輸出 dist\DongNiZhuyin.msi
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$build = Join-Path $root 'windows\build'
$dist = Join-Path $root 'dist'
$work = Join-Path $dist 'msi-work'
$msi = Join-Path $dist 'DongNiZhuyin.msi'
$version = '0.3.5'  # 每次發布都要遞增，新版才會自動取代舊版
$upgradeCode = '{6B1F3A52-0D44-4C1E-9E1B-4A7C2D8F5E31}'  # 固定：之後的版本靠它認出舊版

Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $work | Out-Null
Remove-Item $msi -ErrorAction SilentlyContinue

# 要裝的檔案：每個檔案一個元件。Dir 是目的資料夾的代號（見下方 Directory 表）
$web = Join-Path $root 'web'
$server = Join-Path $root 'windows\server'
$node = (Get-Command node).Source
$nodeVersion = & $node --version
$nodeLicense = Join-Path $work 'LICENSE.txt'
# makecab 只吃英數路徑：中文檔名的先複製一份
$licenses = Join-Path $work 'licenses.txt'
Copy-Item (Join-Path $root 'windows\package\授權與來源.txt') $licenses
Set-Content $nodeLicense -Encoding UTF8 -Value @(
  "Node.js $nodeVersion",
  'Copyright Node.js contributors. All rights reserved.',
  'Released under the MIT License, with third-party components under their own licenses.',
  "Full text: https://github.com/nodejs/node/blob/$nodeVersion/LICENSE")
$files = @(
  @{ Src = Join-Path $build 'ShunDaTip-x64.dll'; Name = 'ShunDaTip-x64.dll'; Dir = 'INSTALLDIR'; Key = 'TipX64'; SelfReg = $true },
  @{ Src = Join-Path $build 'ShunDaTip-x86.dll'; Name = 'ShunDaTip-x86.dll'; Dir = 'INSTALLDIR'; Key = 'TipX86'; Is64 = $false; SelfReg = $true },
  @{ Src = $licenses; Name = 'licenses.txt'; Dir = 'INSTALLDIR' },
  @{ Src = $node; Name = 'node.exe'; Dir = 'DirNode' },
  @{ Src = $nodeLicense; Name = 'LICENSE.txt'; Dir = 'DirNode' },
  @{ Src = Join-Path $server 'ime-server.js'; Name = 'ime-server.js'; Dir = 'DirServer' },
  @{ Src = Join-Path $server 'nn-thread.js'; Name = 'nn-thread.js'; Dir = 'DirServer' }
)
foreach ($f in 'engine.js', 'dict.js', 'lm.js', 'seed.js', 'nn.js', 'nnlm.wasm') { $files += @{ Src = Join-Path $web $f; Name = $f; Dir = 'DirWeb' } }
$n = 0
foreach ($f in $files) {
  $n++
  if (-not $f.Key) { $f.Key = 'F{0:D3}' -f $n }
  $f.Comp = 'C_' + $f.Key
  if ($null -eq $f.Is64) { $f.Is64 = $true }
  # MSI 檔名格式是「8.3 短檔名|長檔名」
  $ext = [IO.Path]::GetExtension($f.Name).TrimStart('.')
  $short = ('F{0:D5}' -f $n) + $(if ($ext) { '.' + $ext.Substring(0, [Math]::Min(3, $ext.Length)).ToUpper() } else { '' })
  $f.MsiName = "$short|$($f.Name)"
  if (-not (Test-Path $f.Src)) { throw "找不到 $($f.Src)" }
}

# ── 壓縮檔（makecab，Windows 內建） ──
$ddf = Join-Path $work 'product.ddf'
$lines = @('.Set CabinetNameTemplate=product.cab', ".Set DiskDirectoryTemplate=$work", '.Set Cabinet=on', '.Set Compress=on',
           '.Set CompressionType=MSZIP', '.Set MaxDiskSize=0', '.Set RptFileName=nul', '.Set InfFileName=nul')
foreach ($f in $files) { $lines += "`"$($f.Src)`" $($f.Key)" }
Set-Content $ddf $lines -Encoding ASCII
& makecab.exe /F $ddf | Out-Null
if ($LASTEXITCODE) { throw 'makecab 失敗' }

# ── MSI 資料庫 ──
$installer = New-Object -ComObject WindowsInstaller.Installer
function Call($obj, $name, $argList) {
  try { $obj.GetType().InvokeMember($name, 'InvokeMethod', $null, $obj, (Plain $argList)) }
  catch { throw "呼叫 $name 失敗（$($argList -join ', ')）：$($_.Exception.InnerException.Message)" }
}
function Plain($argList) { [object[]]@($argList | ForEach-Object { if ($_ -is [psobject]) { $_.psobject.BaseObject } else { $_ } }) }
function Prop($obj, $name, $argList) { $obj.GetType().InvokeMember($name, 'GetProperty', $null, $obj, (Plain $argList)) }
function SetProp($obj, $name, $argList) { $obj.GetType().InvokeMember($name, 'SetProperty', $null, $obj, (Plain $argList)) | Out-Null }
$db = Call $installer 'OpenDatabase' @($msi, 3)  # msiOpenDatabaseModeCreate
function Sql($q) { $v = Call $db 'OpenView' @($q); Call $v 'Execute' @() | Out-Null; Call $v 'Close' @() | Out-Null }
# 用記錄寫入（MSI 的 INSERT 語法要列欄位名，用記錄比較簡單）；$null 代表空欄位
function Row($table, [object[]]$vals) {
  $v = Call $db 'OpenView' @("SELECT * FROM ``$table``")
  Call $v 'Execute' @() | Out-Null
  $r = Call $installer 'CreateRecord' @($vals.Count)
  for ($i = 0; $i -lt $vals.Count; $i++) {
    if ($null -eq $vals[$i]) { continue }
    if ($vals[$i] -is [int]) { SetProp $r 'IntegerData' @(($i + 1), $vals[$i]) } else { SetProp $r 'StringData' @(($i + 1), [string]$vals[$i]) }
  }
  Call $v 'Modify' @(1, $r) | Out-Null  # msiViewModifyInsert
  Call $v 'Close' @() | Out-Null
}

Sql 'CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` LONGCHAR NOT NULL LOCALIZABLE PRIMARY KEY `Property`)'
Sql 'CREATE TABLE `Directory` (`Directory` CHAR(72) NOT NULL, `Directory_Parent` CHAR(72), `DefaultDir` CHAR(255) NOT NULL LOCALIZABLE PRIMARY KEY `Directory`)'
Sql 'CREATE TABLE `Component` (`Component` CHAR(72) NOT NULL, `ComponentId` CHAR(38), `Directory_` CHAR(72) NOT NULL, `Attributes` SHORT NOT NULL, `Condition` CHAR(255), `KeyPath` CHAR(72) PRIMARY KEY `Component`)'
Sql 'CREATE TABLE `Feature` (`Feature` CHAR(38) NOT NULL, `Feature_Parent` CHAR(38), `Title` CHAR(64) LOCALIZABLE, `Description` CHAR(255) LOCALIZABLE, `Display` SHORT, `Level` SHORT NOT NULL, `Directory_` CHAR(72), `Attributes` SHORT NOT NULL PRIMARY KEY `Feature`)'
Sql 'CREATE TABLE `FeatureComponents` (`Feature_` CHAR(38) NOT NULL, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Feature_`, `Component_`)'
Sql 'CREATE TABLE `File` (`File` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL LOCALIZABLE, `FileSize` LONG NOT NULL, `Version` CHAR(72), `Language` CHAR(20), `Attributes` SHORT, `Sequence` LONG NOT NULL PRIMARY KEY `File`)'
Sql 'CREATE TABLE `Media` (`DiskId` SHORT NOT NULL, `LastSequence` LONG NOT NULL, `DiskPrompt` CHAR(64) LOCALIZABLE, `Cabinet` CHAR(255), `VolumeLabel` CHAR(32), `Source` CHAR(72) PRIMARY KEY `DiskId`)'
Sql 'CREATE TABLE `SelfReg` (`File_` CHAR(72) NOT NULL, `Cost` SHORT PRIMARY KEY `File_`)'
Sql 'CREATE TABLE `Registry` (`Registry` CHAR(72) NOT NULL, `Root` SHORT NOT NULL, `Key` CHAR(255) NOT NULL LOCALIZABLE, `Name` CHAR(255) LOCALIZABLE, `Value` LONGCHAR LOCALIZABLE, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Registry`)'
Sql 'CREATE TABLE `RemoveRegistry` (`RemoveRegistry` CHAR(72) NOT NULL, `Root` SHORT NOT NULL, `Key` CHAR(255) NOT NULL LOCALIZABLE, `Name` CHAR(255) LOCALIZABLE, `Component_` CHAR(72) NOT NULL PRIMARY KEY `RemoveRegistry`)'
Sql 'CREATE TABLE `CustomAction` (`Action` CHAR(72) NOT NULL, `Type` SHORT NOT NULL, `Source` CHAR(72), `Target` CHAR(255), `ExtendedType` LONG PRIMARY KEY `Action`)'
Sql 'CREATE TABLE `Binary` (`Name` CHAR(72) NOT NULL, `Data` OBJECT NOT NULL PRIMARY KEY `Name`)'
Sql 'CREATE TABLE `Upgrade` (`UpgradeCode` CHAR(38) NOT NULL, `VersionMin` CHAR(20), `VersionMax` CHAR(20), `Language` CHAR(255), `Attributes` LONG NOT NULL, `Remove` CHAR(255), `ActionProperty` CHAR(72) NOT NULL PRIMARY KEY `UpgradeCode`, `VersionMin`, `VersionMax`, `Language`, `Attributes`)'
foreach ($t in 'InstallExecuteSequence', 'InstallUISequence') {
  Sql "CREATE TABLE ``$t`` (``Action`` CHAR(72) NOT NULL, ``Condition`` CHAR(255), ``Sequence`` SHORT PRIMARY KEY ``Action``)"
}

$productCode = '{' + [guid]::NewGuid().ToString().ToUpper() + '}'
foreach ($p in @(
  @('ProductName', 'DongNi Zhuyin'), @('ProductCode', $productCode), @('ProductVersion', $version),
  @('ProductLanguage', '1033'), @('Manufacturer', 'DongNi Zhuyin'), @('UpgradeCode', $upgradeCode),
  @('ALLUSERS', '1'), @('ARPNOMODIFY', '1'), @('SecureCustomProperties', 'OLDERFOUND'))) { Row 'Property' $p }

Row 'Directory' @('TARGETDIR', $null, 'SourceDir')
Row 'Directory' @('ProgramFiles64Folder', 'TARGETDIR', '.')
Row 'Directory' @('INSTALLDIR', 'ProgramFiles64Folder', 'DongNi|DongNiZhuyin')
Row 'Directory' @('DirNode', 'INSTALLDIR', 'node')
Row 'Directory' @('DirWeb', 'INSTALLDIR', 'web')
Row 'Directory' @('DirWin', 'INSTALLDIR', 'windows')
Row 'Directory' @('DirServer', 'DirWin', 'server')

Row 'Feature' @('Main', $null, 'DongNi Zhuyin', $null, 1, 1, 'INSTALLDIR', 0)
$seq = 0
foreach ($f in $files) {
  $seq++
  $compId = '{' + [guid]::NewGuid().ToString().ToUpper() + '}'
  Row 'Component' @($f.Comp, $compId, $f.Dir, $(if ($f.Is64) { 256 } else { 0 }), $null, $f.Key)
  Row 'FeatureComponents' @('Main', $f.Comp)
  $size = [int](Get-Item $f.Src).Length
  Row 'File' @($f.Key, $f.Comp, $f.MsiName, $size, $null, $null, 16384, $seq)  # 16384 = msidbFileAttributesCompressed
  if ($f.SelfReg) { Row 'SelfReg' @($f.Key, $null) }
}
# 登入 Windows 時在背景啟動引擎（所有使用者）
Row 'Registry' @('RunEngine', 2, 'Software\Microsoft\Windows\CurrentVersion\Run', 'DongNiZhuyin',
  '"[%windir]\System32\rundll32.exe" "[INSTALLDIR]ShunDaTip-x64.dll",RunServerW', 'C_TipX64')
# 自訂動作（寫在輸入法 DLL 裡）：StopEngine 裝之前先停掉正在跑的引擎，檔案才換得掉
# （用內嵌的 DLL，第一次安裝時檔案還不在；類型 65 = 內嵌 DLL＋失敗不中斷）
# 鍵盤清單、清舊設定要用使用者自己的身分，MSI 裡做不可靠，改由登入時的 RunServerW（HKLM Run）處理，所以裝完要登出再登入
Row 'CustomAction' @('StopEngine', 65, 'TipBin', 'MsiStopEngine', $null)
# StartEngine：安裝畫面結束時啟動引擎；引擎起來後會用使用者身分做使用者設定（類型 81 = 已安裝的 DLL＋失敗不中斷）。
# 沒有畫面的安裝（/qb、/qn）不會跑，引擎會在下次登入時啟動
Row 'CustomAction' @('StartEngine', 81, 'TipX64', 'MsiStartEngine', $null)
Row 'Media' @(1, $seq, $null, '#product.cab', $null, $null)
# 裝新版時先移除舊版（之後的版本號要遞增）
Row 'Upgrade' @($upgradeCode, $null, $version, $null, 256, $null, 'OLDERFOUND')  # 256 = 包含較小版本（不含相同版本）

$execSeq = @(
  @('FindRelatedProducts', 25), @('LaunchConditions', 100), @('ValidateProductID', 700), @('CostInitialize', 800),
  @('FileCost', 900), @('CostFinalize', 1000), @('InstallValidate', 1400), @('RemoveExistingProducts', 1450),
  @('InstallInitialize', 1500), @('ProcessComponents', 1600), @('UnpublishFeatures', 1800), @('SelfUnregModules', 2200),
  @('RemoveFiles', 3500), @('InstallFiles', 4000), @('RegisterUser', 6000), @('RegisterProduct', 6100),
  @('PublishFeatures', 6300), @('PublishProduct', 6400), @('SelfRegModules', 6500), @('InstallFinalize', 6600),
  @('RemoveRegistryValues', 2600), @('RemoveFolders', 3600), @('CreateFolders', 3700), @('WriteRegistryValues', 5000))
foreach ($a in $execSeq) { Row 'InstallExecuteSequence' @($a[0], $null, $a[1]) }
# 只有真的要換檔案時才停引擎（第一次裝、升級、修復、移除）；已經裝好再執行一次同版本不要停掉
Row 'InstallExecuteSequence' @('StopEngine', 'NOT Installed OR REINSTALL OR REMOVE~="ALL"', 1390)
Row 'InstallUISequence' @('StartEngine', 'NOT REMOVE~="ALL"', 1400)
foreach ($a in @(@('FindRelatedProducts', 25), @('LaunchConditions', 100), @('CostInitialize', 800), @('FileCost', 900),
                 @('CostFinalize', 1000), @('ExecuteAction', 1300))) { Row 'InstallUISequence' @($a[0], $null, $a[1]) }

# 壓縮檔、自訂動作用的 DLL 放進 MSI 裡
function Stream($table, $name, $path) {
  $view = Call $db 'OpenView' @("SELECT ``Name``, ``Data`` FROM ``$table``")
  Call $view 'Execute' @() | Out-Null
  $rec = Call $installer 'CreateRecord' @(2)
  SetProp $rec 'StringData' @(1, $name)
  Call $rec 'SetStream' @(2, $path) | Out-Null
  Call $view 'Modify' @(1, $rec) | Out-Null  # msiViewModifyInsert
  Call $view 'Close' @() | Out-Null
}
Stream '_Streams' 'product.cab' (Join-Path $work 'product.cab')
Stream 'Binary' 'TipBin' (Join-Path $build 'ShunDaTip-x64.dll')

# 摘要資訊：x64 套件、需要提權、壓縮檔案
$si = Prop $db 'SummaryInformation' @(20)
SetProp $si 'Property' @(2, 'Installation Database')
SetProp $si 'Property' @(3, 'DongNi Zhuyin')
SetProp $si 'Property' @(4, 'DongNi Zhuyin')
SetProp $si 'Property' @(7, 'x64;1033')
SetProp $si 'Property' @(9, ('{' + [guid]::NewGuid().ToString().ToUpper() + '}'))
SetProp $si 'Property' @(14, 200)
SetProp $si 'Property' @(15, 2)
Call $si 'Persist' @() | Out-Null
Call $db 'Commit' @() | Out-Null
[void][Runtime.InteropServices.Marshal]::ReleaseComObject($db)
Remove-Item $work -Recurse -Force
Write-Host "完成：$msi（$([math]::Round((Get-Item $msi).Length / 1KB)) KB）"
