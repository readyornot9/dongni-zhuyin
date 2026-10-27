// 把 docs/新增測試題.txt 轉成測試題（注音從詞庫反查，跟 tools/text-to-reading.js 相同規則）
// 用法：node tools/make-tests.js → 印出 testcases.js 用的行，並列出破音字讓人工確認
'use strict';
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
require(path.join(root, 'web', 'dict.js'));

// 一般人打「不」「一」都打本調（ㄅㄨˋ、ㄧ），「的、了、嗎、呢」打輕聲
function natural(w, r) {
  const syl = r.split(' ');
  return [...w].every((ch, i) => (ch !== '不' || syl[i] === 'ㄅㄨˋ') && (ch !== '一' || syl[i] === 'ㄧ'));
}
const best = new Map();
const readings = new Map(); // 單字 → 所有讀音（找破音字用）
for (const line of globalThis.ZHUYIN_DICT.split('\n')) {
  const [w, r] = line.split('\t');
  if (!best.has(w) || (!natural(w, best.get(w)) && natural(w, r))) best.set(w, r);
  if ([...w].length === 1) {
    if (!readings.has(w)) readings.set(w, new Set());
    readings.get(w).add(r);
  }
}
function toReading(text) {
  const out = [];
  const chars = [...text];
  for (let i = 0; i < chars.length;) {
    let found = null;
    for (let len = Math.min(9, chars.length - i); len >= 1; len--) {
      const w = chars.slice(i, i + len).join('');
      if (best.has(w)) { found = [len, best.get(w)]; break; }
    }
    if (!found) return null;
    out.push(...found[1].split(' '));
    i += found[0];
  }
  return out;
}

const lines = fs.readFileSync(path.join(root, 'docs', '新增測試題.txt'), 'utf8').split(/\r?\n/);
let group = '', mode = '';
const out = [], poly = [];
for (const l of lines) {
  if (!l.trim() || l.startsWith('#')) continue;
  const m = l.match(/^\[(.+?)(?:：(.+))?\]$/);
  if (m) { group = m[2] ? `${m[1]}（${m[2]}）` : m[1]; mode = m[2] || ''; continue; }
  const syl = toReading(l.trim());
  if (!syl) { console.error('查不到注音：' + l); continue; }
  let r;
  if (mode === '省略聲調') r = syl.map((s) => s.replace(/[ˊˇˋ˙]/g, '') + '*');
  else if (mode === '只打聲母') r = syl.map((s) => [...s][0] + '*');
  else r = syl.map((s) => s.replace(/^(.+)$/, '$1').replace(/ˉ/, ''));
  // 一聲在這套寫法裡不標
  out.push(`    { group: '${group}', expect: '${l.trim()}', reading: '${r.join(' ')}' },`);
  [...l.trim()].forEach((ch, i) => { if ((readings.get(ch) || new Set()).size > 1) poly.push(`${ch}=${syl[i]}`); });
}
console.log(out.join('\n'));
console.error('\n破音字（請確認）：' + [...new Set(poly)].join('、'));
