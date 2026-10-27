// 從維基百科繁體句子統計「前一個詞 → 下一個詞」的次數，產生前後文模型 web/lm.js
// 用法：node --max-old-space-size=12000 tools/build-lm.js data/work/wiki-trad.txt [最少次數=5]
// 前置：node tools/build-dict.js、python tools/wiki-extract.py
'use strict';
const fs = require('fs');
const path = require('path');
const readline = require('readline');

const root = path.join(__dirname, '..');
require(path.join(root, 'web', 'dict.js'));
const input = process.argv[2] || path.join(root, 'data', 'work', 'wiki-trad.txt');
const MIN = Number(process.argv[3] || 5);
const SEG = -3; // 跟引擎的 SEGMENT_COST 一樣，切詞偏好長詞

// ── 詞表：詞 → id、分數（同一個詞有多種讀音取最高分） ──
const ids = new Map();
const words = [];
const scores = [];
let maxLen = 1;
for (const line of globalThis.ZHUYIN_DICT.split('\n')) {
  const [w, , s] = line.split('\t');
  if (!w) continue;
  if (!ids.has(w)) { ids.set(w, words.length); words.push(w); scores.push(Number(s)); maxLen = Math.max(maxLen, w.length); }
}
maxLen = Math.min(maxLen, 9);
const V = words.length;
console.log(`詞表 ${V} 個詞`);

// ── 切詞（動態規劃，選總分最高的切法） ──
function segment(text) {
  const n = text.length;
  const best = new Float64Array(n + 1).fill(-Infinity);
  const back = new Int32Array(n + 1);
  const wid = new Int32Array(n + 1);
  best[0] = 0;
  for (let i = 0; i < n; i++) {
    if (best[i] === -Infinity) continue;
    let any = false;
    for (let len = 1; len <= maxLen && i + len <= n; len++) {
      const id = ids.get(text.substr(i, len));
      if (id === undefined) continue;
      any = true;
      const sc = best[i] + scores[id] + SEG;
      if (sc > best[i + len]) { best[i + len] = sc; back[i + len] = i; wid[i + len] = id; }
    }
    if (!any && best[i] - 30 > best[i + 1]) { best[i + 1] = best[i] - 30; back[i + 1] = i; wid[i + 1] = -1; }
  }
  const out = [];
  for (let j = n; j > 0; j = back[j]) out.push(wid[j]);
  return out.reverse();
}

// ── 計數 ──
const uni = new Float64Array(V);
const SHARDS = 16; // Map 有筆數上限，分片存放
const bi = Array.from({ length: SHARDS }, () => new Map());
let tokens = 0, lines = 0;
const t0 = Date.now();

(async () => {
  const rl = readline.createInterface({ input: fs.createReadStream(input, { encoding: 'utf8' }), crlfDelay: Infinity });
  for await (const line of rl) {
    if (!line) continue;
    const seq = segment(line);
    let prev = -1;
    for (const id of seq) {
      if (id < 0) { prev = -1; continue; }
      uni[id]++;
      tokens++;
      if (prev >= 0) {
        const m = bi[prev % SHARDS];
        const key = prev * 262144 + id;
        m.set(key, (m.get(key) || 0) + 1);
      }
      prev = id;
    }
    if (++lines % 2000000 === 0) {
      const size = bi.reduce((s, m) => s + m.size, 0);
      console.log(`${lines} 行、${tokens} 個詞、${size} 種組合，${((Date.now() - t0) / 1000).toFixed(0)} 秒`);
    }
  }

  // 每個前詞：總次數、接過幾種不同的詞（算退回機率用，要在刪減前算）
  const follow = new Float64Array(V);
  const kinds = new Float64Array(V);
  for (const m of bi) for (const [key, c] of m) { const p = Math.floor(key / 262144); follow[p] += c; kinds[p]++; }

  // 只留常見組合
  const byPrev = new Map();
  let kept = 0;
  for (const m of bi) {
    for (const [key, c] of m) {
      if (c < MIN) continue;
      const p = Math.floor(key / 262144), w = key % 262144;
      let a = byPrev.get(p);
      if (!a) byPrev.set(p, (a = []));
      a.push(words[w] + ':' + c);
      kept++;
    }
  }
  const linesOut = [];
  for (const [p, a] of byPrev) linesOut.push(words[p] + '\t' + follow[p] + '\t' + kinds[p] + '\t' + a.join(','));
  const body = linesOut.join('\n');
  const out = '// 自動產生：前後文模型（詞與詞相鄰出現的次數）\n' +
    '// 統計來源：中文維基百科（繁體句子），CC BY-SA 4.0，https://zh.wikipedia.org/\n' +
    'globalThis.ZHUYIN_LM = { tokens: ' + tokens + ', min: ' + MIN + ', data: ' + JSON.stringify(body) + ' };\n';
  fs.writeFileSync(path.join(root, 'web', 'lm.js'), out);
  console.log(`完成：${lines} 行、${tokens} 個詞；保留 ${kept} 種組合（出現 ≥ ${MIN} 次），${byPrev.size} 個前詞；` +
    `${(Buffer.byteLength(out) / 1024 / 1024).toFixed(1)} MB → web/lm.js，${((Date.now() - t0) / 1000).toFixed(0)} 秒`);

  // 另存完整計數摘要，方便之後調整門檻不用重跑
  fs.writeFileSync(path.join(root, 'data', 'work', 'lm-stats.json'), JSON.stringify({ lines, tokens, kept, prevs: byPrev.size, min: MIN }));
})();
