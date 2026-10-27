// 合併兩份前後文統計：維基百科（data/work/lm-full.js）＋口語（data/work/lm-colloquial.js）
// 口語語料少但貼近日常打字，所以次數乘上倍數 K 再相加
// 用法：node tools/merge-lm.js K   → 輸出 data/work/lm-merged.js（格式同 lm-full.js），再用 prune-lm.js 精簡
'use strict';
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
const K = Number(process.argv[2] || 50);

function load(file) {
  delete globalThis.ZHUYIN_LM;
  require(file);
  const LM = globalThis.ZHUYIN_LM;
  const map = new Map();
  for (const line of LM.data.split('\n')) {
    const [p, f, k, list] = line.split('\t');
    const next = new Map();
    for (const x of list.split(',')) { const i = x.lastIndexOf(':'); next.set(x.slice(0, i), Number(x.slice(i + 1))); }
    map.set(p, { f: Number(f), k: Number(k), next });
  }
  return { tokens: LM.tokens, map };
}

const wiki = load(path.join(root, 'data', 'work', 'lm-full.js'));
const col = load(path.join(root, 'data', 'work', 'lm-colloquial.js'));
for (const [p, e] of col.map) {
  let w = wiki.map.get(p);
  if (!w) wiki.map.set(p, (w = { f: 0, k: 0, next: new Map() }));
  w.f += K * e.f;
  for (const [n, c] of e.next) {
    if (!w.next.has(n)) w.k++;
    w.next.set(n, (w.next.get(n) || 0) + K * c);
  }
}
const lines = [];
for (const [p, e] of wiki.map) lines.push(p + '\t' + e.f + '\t' + e.k + '\t' + [...e.next].map(([n, c]) => n + ':' + c).join(','));
fs.writeFileSync(path.join(root, 'data', 'work', 'lm-merged.js'),
  'globalThis.ZHUYIN_LM = { tokens: ' + (wiki.tokens + K * col.tokens) + ', min: 5, data: ' + JSON.stringify(lines.join('\n')) + ' };\n');
console.log(`合併完成（口語 ×${K}）：${wiki.map.size} 個前詞 → data/work/lm-merged.js`);
