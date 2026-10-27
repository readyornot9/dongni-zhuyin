// 打包懂你注音 Windows 版：dist/package/懂你注音/ → dist/DongNiZhuyin-Windows.zip
// 先跑 windows\build.cmd 編好 DLL。Node.js 直接用這台電腦安裝的 node.exe（免安裝版一起打包）
// 用法：node tools/build-package.js
'use strict';
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const root = path.join(__dirname, '..');
const out = path.join(root, 'dist', 'package');
const top = path.join(out, '懂你注音');
const app = path.join(top, 'app');
const copy = (from, to) => { fs.mkdirSync(path.dirname(to), { recursive: true }); fs.copyFileSync(from, to); };

fs.rmSync(out, { recursive: true, force: true });

for (const f of ['安裝.cmd', '移除.cmd', '說明.txt', '授權與來源.txt']) copy(path.join(root, 'windows', 'package', f), path.join(top, f));
copy(path.join(root, 'windows', 'package', 'install.ps1'), path.join(app, 'setup', 'install.ps1'));
copy(path.join(root, 'windows', 'package', 'uninstall.ps1'), path.join(app, 'setup', 'uninstall.ps1'));
for (const a of ['x64', 'x86']) copy(path.join(root, 'windows', 'build', `ShunDaTip-${a}.dll`), path.join(app, 'bin', `ShunDaTip-${a}.dll`));
for (const f of ['ime-server.js', 'nn-thread.js']) copy(path.join(root, 'windows', 'server', f), path.join(app, 'windows', 'server', f));
for (const f of ['engine.js', 'dict.js', 'lm.js', 'seed.js', 'nn.js', 'nnlm.wasm']) copy(path.join(root, 'web', f), path.join(app, 'web', f));
copy(process.execPath, path.join(app, 'node', 'node.exe'));
fs.writeFileSync(path.join(app, 'node', 'LICENSE.txt'), `Node.js ${process.version}
Copyright Node.js contributors. All rights reserved.
Released under the MIT License, with third-party components under their own licenses.
Full text: https://github.com/nodejs/node/blob/${process.version}/LICENSE
`);

const zip = path.join(root, 'dist', 'DongNiZhuyin-Windows.zip');
fs.rmSync(zip, { force: true });
execFileSync('powershell', ['-NoProfile', '-Command',
  `Add-Type -AssemblyName System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::CreateFromDirectory('${out}', '${zip}', [IO.Compression.CompressionLevel]::Optimal, $false, [Text.Encoding]::UTF8)`]);
const mb = (n) => (n / 1048576).toFixed(1) + ' MB';
const size = (d) => fs.readdirSync(d, { withFileTypes: true }).reduce((s, e) => s + (e.isDirectory() ? size(path.join(d, e.name)) : fs.statSync(path.join(d, e.name)).size), 0);
console.log(`完成：dist/DongNiZhuyin-Windows.zip（壓縮後 ${mb(fs.statSync(zip).size)}，解開 ${mb(size(top))}）`);
