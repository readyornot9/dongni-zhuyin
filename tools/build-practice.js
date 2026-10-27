// 產生打字練習題庫（web/practice-data.js）
// 新題：docs/練習句子.txt（引擎沒修過的句子）；舊題：開發、評估、盲測 1～7
// 用法：node tools/build-practice.js
'use strict';
const fs = require('fs');
const path = require('path');

const docs = path.join(__dirname, '..', 'docs');
const read = (f) => fs.readFileSync(path.join(docs, f), 'utf8').split(/\r?\n/).map((s) => s.trim()).filter(Boolean);
const oldFiles = ['開發句子.txt', '評估句子.txt', ...fs.readdirSync(docs).filter((f) => /^盲測句子\d*\.txt$/.test(f)).sort()];
const old = [...new Set(oldFiles.flatMap(read))];
const known = new Set(fs.readdirSync(docs).filter((f) => f.endsWith('.txt') && f !== '練習句子.txt').flatMap(read));
const fresh = [...new Set(read('練習句子.txt'))].filter((s) => !known.has(s));

const out = `// 自動產生：node tools/build-practice.js\nglobalThis.PRACTICE_SETS = ${JSON.stringify({ fresh, old })};\n`;
fs.writeFileSync(path.join(__dirname, '..', 'web', 'practice-data.js'), out);
console.log(`新題 ${fresh.length} 句，舊題 ${old.length} 句 → web/practice-data.js`);
