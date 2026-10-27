// 從中文句子反查注音（給測試題用），再用引擎跑一次看會不會選對
// 用法：node tools/text-to-reading.js 句子1 句子2 ...
// 或：  node tools/text-to-reading.js --file docs/練習文章.txt
// 注意：破音字取詞庫最常見的讀音，產生後請人工確認
'use strict';
const fs = require('fs');
const path = require('path');
require(path.join(__dirname, '..', 'web', 'dict.js'));
const S = require(path.join(__dirname, '..', 'web', 'engine.js'));

// 一般人打「不」「一」都打本調（ㄅㄨˋ、ㄧ），不會打變調
function natural(w, r) {
  const syl = r.split(' ');
  return [...w].every((ch, i) => (ch !== '不' || syl[i] === 'ㄅㄨˋ') && (ch !== '一' || syl[i] === 'ㄧ'));
}
const best = new Map(); // 詞 → 最常見的注音
for (const line of globalThis.ZHUYIN_DICT.split('\n')) {
  const [w, r] = line.split('\t');
  // 已依分數排序，第一個就是最常見；但優先選本調的讀音
  if (!best.has(w) || (!natural(w, best.get(w)) && natural(w, r))) best.set(w, r);
}

function toReading(text) {
  const out = [];
  let i = 0;
  const chars = [...text];
  while (i < chars.length) {
    let found = null;
    for (let len = Math.min(9, chars.length - i); len >= 1; len--) {
      const w = chars.slice(i, i + len).join('');
      if (best.has(w)) { found = [w, best.get(w)]; break; }
    }
    if (!found) return null;
    out.push(found[1]);
    i += [...found[0]].length;
  }
  return out.join(' ');
}

let args = process.argv.slice(2);
if (args[0] === '--file') {
  args = fs.readFileSync(args[1], 'utf8').split(/[\r\n]+/);
}
// 依標點切成一段一段（打字時打到標點就會送出）
const parts = args.flatMap((s) => s.split(/[，。？！、：；「」『』（）\s#…—]+/)).filter((s) => /[一-鿿]/.test(s));

const engine = new S.Engine(new S.Dictionary(globalThis.ZHUYIN_DICT));
let ok = 0;
const rows = [];
for (const p of parts) {
  const reading = toReading(p);
  if (!reading) { console.log(`?  ${p}（有字查不到注音）`); continue; }
  const got = engine.convert(S.parseReading(reading)).text;
  if (got === p) ok++;
  console.log(`${got === p ? '✔' : '✘'} ${p}${got === p ? '' : '  → ' + got}\n   ${reading}`);
  rows.push({ expect: p, reading });
}
console.log(`\n${ok}/${parts.length} 正確`);
if (process.env.JSON) console.log(JSON.stringify(rows, null, 1));
