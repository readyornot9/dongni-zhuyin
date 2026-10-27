// 口語詞頻：把 data/work/colloquial.txt（Tatoeba＋Common Voice）用詞庫斷詞後計數 → data/work/colloq-unigram.tsv
// 只打聲母（簡拼）多半是在聊天，書面語詞頻（驚濤駭浪、維修作業）不適合，引擎在簡拼時會混用這份口語詞頻
// 用法：node tools/colloquial-unigram.js
'use strict';
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
require(path.join(root, 'web', 'dict.js'));

const best = new Map(); // 詞 → 書面語分數（斷詞用）
for (const line of globalThis.ZHUYIN_DICT.split('\n')) {
  const [w, , s] = line.split('\t');
  if (w && (!best.has(w) || best.get(w) < Number(s))) best.set(w, Number(s));
}
const MAX = 6;
const counts = new Map();
let total = 0;
const seen = new Set();
for (const raw of fs.readFileSync(path.join(root, 'data', 'work', 'colloquial.txt'), 'utf8').split(/\r?\n/)) {
  const line = raw.trim();
  if (!line || seen.has(line)) continue; // 同一句只算一次（語料裡口語句有重複放大）
  seen.add(line);
  for (const run of line.match(/[一-鿿]+/g) || []) {
    // 最大機率斷詞（每切一段扣 3，跟引擎的 SEGMENT_COST 相同，偏好長詞）
    const chars = [...run];
    const n = chars.length;
    const dp = new Array(n + 1).fill(-Infinity), back = new Array(n + 1).fill(0);
    dp[0] = 0;
    for (let j = 1; j <= n; j++) {
      for (let i = Math.max(0, j - MAX); i < j; i++) {
        const w = chars.slice(i, j).join('');
        const s = best.has(w) ? best.get(w) : (j - i === 1 ? -20 : null);
        if (s === null) continue;
        if (dp[i] + s - 3 > dp[j]) { dp[j] = dp[i] + s - 3; back[j] = i; }
      }
    }
    for (let j = n; j > 0; j = back[j]) {
      const w = chars.slice(back[j], j).join('');
      counts.set(w, (counts.get(w) || 0) + 1);
      total++;
    }
  }
}
const out = [`#total\t${total}`, ...[...counts].sort((a, b) => b[1] - a[1]).map(([w, c]) => `${w}\t${c}`)];
fs.writeFileSync(path.join(root, 'data', 'work', 'colloq-unigram.tsv'), out.join('\n'));
console.log(`完成：${counts.size} 個詞，${total} 次 → data/work/colloq-unigram.tsv`);
