// 把擁有者上傳的預設學習（data/seed.json）打包成 web/seed.js，讓所有人一打開就有
// data/seed.json 由 Claude 從網頁版資料庫（seed/learning）讀下來存檔
// 用法：node tools/build-seed.js
'use strict';
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const src = path.join(root, 'data', 'seed.json');
let seed = { learning: {}, tests: [] };
if (fs.existsSync(src)) {
  const raw = JSON.parse(fs.readFileSync(src, 'utf8'));
  // 資料庫裡是 { json: "...", updatedAt }，也接受直接放內容
  seed = typeof raw.json === 'string' ? JSON.parse(raw.json) : raw;
  seed.updatedAt = raw.updatedAt || seed.updatedAt || null;
}
const learning = seed.learning || {};
const count = Object.values(learning).reduce((s, v) => s + (v && typeof v === 'object' ? Object.keys(v).length : 0), 0);
fs.writeFileSync(path.join(root, 'web', 'seed.js'),
  '// 自動產生：擁有者設定的預設學習與測試題（來源 data/seed.json）\n' +
  'globalThis.ZHUYIN_SEED = ' + JSON.stringify(seed) + ';\n');
console.log(`完成：預設學習 ${count} 筆、預設測試題 ${(seed.tests || []).length} 題 → web/seed.js`);
