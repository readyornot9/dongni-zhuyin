// 產生公開的打字練習網站（dist/typing.html）：把題庫塞進 site/typing.html
// 題庫：日常口語的開發、評估、練習、盲測句子，4～20 字
// 用法：node tools/build-typing-site.js
'use strict';
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const docs = path.join(root, 'docs');
const files = fs.readdirSync(docs).filter((f) => /^(盲測句子\d*|開發句子|評估句子|練習句子)\.txt$/.test(f));
const all = files.flatMap((f) => fs.readFileSync(path.join(docs, f), 'utf8').split(/\r?\n/)).map((s) => s.trim());
const pool = [...new Set(all)].filter((s) => /^[一-鿿，、]+$/.test(s) && [...s].length >= 4 && [...s].length <= 20);

const html = fs.readFileSync(path.join(root, 'site', 'typing.html'), 'utf8').replace('/*SENTENCES*/[]', JSON.stringify(pool));
fs.writeFileSync(path.join(root, 'dist', 'typing.html'), html);
console.log(`題庫 ${pool.length} 句 → dist/typing.html（${Math.round(html.length / 1024)} KB）`);

// GitHub Pages 版：完整的 HTML 文件，有 site/firebase-config.json 就開排行榜
const cfgFile = path.join(root, 'site', 'firebase-config.json');
const cfg = fs.existsSync(cfgFile) ? fs.readFileSync(cfgFile, 'utf8').trim() : '';
const wrap = (body, desc, head = '') => [
  '<!doctype html>',
  '<html lang="zh-Hant-TW">',
  '<head>',
  '<meta charset="utf-8">',
  '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">',
  `<meta name="description" content="${desc}">`,
  '<style>:root{padding-top:env(safe-area-inset-top,0px);padding-bottom:env(safe-area-inset-bottom,0px)}body{margin:0}[hidden]{display:none!important}</style>',
  head,
  '</head>',
  '<body>',
  body,
  '</body>',
  '</html>',
  '',
].join('\n');
fs.mkdirSync(path.join(root, 'pages'), { recursive: true });
fs.writeFileSync(path.join(root, 'pages', 'index.html'), wrap(html, '用你平常的注音輸入法照題打字，測每分鐘字數和正確率。每天一組新題目，還有排行榜。', cfg ? `<script>window.FIREBASE_CONFIG = ${cfg};</script>` : ''));
fs.writeFileSync(path.join(root, 'pages', 'support.html'), wrap(fs.readFileSync(path.join(root, 'site', 'support.html'), 'utf8'), '注音打字挑戰免費、沒有廣告，歡迎隨意贊助支持開發。'));
// 程式碼簽章政策（SignPath 要求首頁和下載處都要連得到）
fs.copyFileSync(path.join(root, 'site', 'code-signing.html'), path.join(root, 'pages', 'code-signing.html'));
console.log(`GitHub Pages 版 → pages/index.html、support.html（排行榜：${cfg ? '開' : '還沒設定 Firebase'}）`);

// 線上試用：網頁版輸入法（web/ 的測試台）複製到 pages/try/，標題改成「線上試用」並加上回首頁和下載連結
const tryDir = path.join(root, 'pages', 'try');
fs.rmSync(tryDir, { recursive: true, force: true });
fs.mkdirSync(tryDir, { recursive: true });
for (const f of ['style.css', 'app.js', 'engine.js', 'dict.js', 'lm.js', 'nn.js', 'nn-worker.js', 'nnlm.wasm', 'seed.js', 'testcases.js', 'practice.js', 'practice-data.js']) {
  fs.copyFileSync(path.join(root, 'web', f), path.join(tryDir, f));
}
const tryHtml = fs.readFileSync(path.join(root, 'web', 'index.html'), 'utf8')
  .replace(/懂你注音 測試台/g, '懂你注音 線上試用')
  .replace('<p class="sub">在下面直接打字。選字、學習、測試都在這一頁。', '<p class="sub">在下面直接打字試試看（先把 Windows 輸入法切到英文）。喜歡的話 <a href="../download/DongNiZhuyin.msi" download>下載 Windows 版</a>・<a href="../">回打字挑戰</a>');
if (!tryHtml.includes('../download/')) throw new Error('試用頁的標題列格式變了，請更新 build-typing-site.js');
fs.writeFileSync(path.join(tryDir, 'index.html'), tryHtml);

// 下載檔：MSI 安裝檔（node tools/build-msi.ps1 產生）放進 pages/download/，舊的 zip 安裝包拿掉
const msi = path.join(root, 'dist', 'DongNiZhuyin.msi');
const dl = path.join(root, 'pages', 'download');
if (fs.existsSync(msi)) {
  fs.mkdirSync(dl, { recursive: true });
  fs.copyFileSync(msi, path.join(dl, 'DongNiZhuyin.msi'));
  fs.rmSync(path.join(dl, 'DongNiZhuyin-Windows.zip'), { force: true });
}
console.log(`線上試用 → pages/try/；下載檔 → ${fs.existsSync(msi) ? 'pages/download/DongNiZhuyin.msi' : '還沒打包（先跑 tools\build-msi.ps1）'}`);
