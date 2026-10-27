// 精簡前後文模型：從完整版（data/work/lm-full.js）只留出現次數夠多的組合，輸出 web/lm.js
// 用法：node tools/prune-lm.js 最少次數 [每個前詞最多保留幾個]
// 前詞的總次數、接過幾種詞保持原值（退回機率仍然正確）
'use strict';
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
// LM_SRC 可指定來源（例如合併版 data/work/lm-merged.js），預設是維基百科完整版
const full = process.env.LM_SRC ? path.resolve(process.env.LM_SRC) : path.join(root, 'data', 'work', 'lm-full.js');
if (!fs.existsSync(full)) fs.copyFileSync(path.join(root, 'web', 'lm.js'), full);
require(full);
const LM = globalThis.ZHUYIN_LM;
// 兩種精簡方式：
//   node tools/prune-lm.js 50              只留出現 ≥ 50 次的組合
//   node tools/prune-lm.js --keep 400000   依「重要性」只留前 40 萬筆
// 重要性＝次數 × |log(實際接續機率 ÷ 單詞機率)|：常見但對選字沒影響的組合（的＋時間）分數低，
// 次數不多但大幅改變機率的組合（填＋地址）分數高
const KEEP = process.argv[2] === '--keep' ? Number(process.argv[3]) : 0;
const MIN = KEEP ? 5 : Number(process.argv[2] || 20);
const TOP = KEEP ? 0 : Number(process.argv[3] || 0);

let selected = null;
if (KEEP) {
  require(path.join(root, 'web', 'dict.js'));
  const uni = new Map();
  for (const l of globalThis.ZHUYIN_DICT.split('\n')) {
    const [w, , s] = l.split('\t');
    if (w && !uni.has(w)) uni.set(w, Math.exp(Number(s)));
  }
  const scored = [];
  for (const line of LM.data.split('\n')) {
    const [p, f, , list] = line.split('\t');
    for (const x of list.split(',')) {
      const i = x.lastIndexOf(':');
      const w = x.slice(0, i), c = Number(x.slice(i + 1));
      const pu = uni.get(w) || 1e-9;
      scored.push([c * Math.abs(Math.log((c / Number(f)) / pu)), p + '\u0001' + w]);
    }
  }
  scored.sort((a, b) => b[0] - a[0]);
  selected = new Set(scored.slice(0, KEEP).map((x) => x[1]));
}

// 同音詞保護（HOMO=1）：次數不到門檻、但能分辨同音詞的組合也保留。
//   後詞有同音詞：「現在→不在」vs「現在→不再」
//   前詞有同音詞：「不在→公司」9 次 vs「不再→公司」0 次（決定性線索，次數少也要留）
// 條件：這筆 ≥ HOMO_MIN 次，而且是同音對手的 HOMO_RATIO 倍以上
const HOMO = process.env.HOMO === '1';
const HOMO_MIN = Number(process.env.HOMO_MIN || 5);
const HOMO_RATIO = Number(process.env.HOMO_RATIO || 3);
let homoKeep = null;
if (HOMO) {
  require(path.join(root, 'web', 'dict.js'));
  const byReading = new Map(); // 注音 → [詞, 分數]
  const readings = new Map(); // 詞 → [注音, 分數]
  for (const l of globalThis.ZHUYIN_DICT.split('\n')) {
    const [w, r, s] = l.split('\t');
    if (!w) continue;
    if (!byReading.has(r)) byReading.set(r, []);
    byReading.get(r).push([w, Number(s)]);
    if (!readings.has(w)) readings.set(w, []);
    readings.get(w).push([r, Number(s)]);
  }
  // 只算「真的會搶位置」的同音詞：分數跟這個詞差不多或更高（差距在 HOMO_MARGIN 以內）
  const HOMO_MARGIN = Number(process.env.HOMO_MARGIN || 1);
  const homophones = (w) => {
    const s = new Set();
    for (const [r, ws] of readings.get(w) || []) {
      for (const [h, hs] of byReading.get(r)) if (h !== w && hs >= ws - HOMO_MARGIN) s.add(h);
    }
    return s;
  };
  const table = new Map(); // 前詞 → Map(後詞 → 次數)
  for (const line of LM.data.split('\n')) {
    const [p, , , list] = line.split('\t');
    const m = new Map();
    for (const x of list.split(',')) { const i = x.lastIndexOf(':'); m.set(x.slice(0, i), Number(x.slice(i + 1))); }
    table.set(p, m);
  }
  const homoCache = new Map();
  const H = (w) => { let s = homoCache.get(w); if (!s) homoCache.set(w, (s = homophones(w))); return s; };
  homoKeep = new Set();
  for (const [p, m] of table) {
    const pHomo = H(p);
    for (const [w, c] of m) {
      if (c >= MIN || c < HOMO_MIN) continue;
      let keep = false;
      for (const h of H(w)) if (c >= HOMO_RATIO * ((m.get(h) || 0) + 1)) { keep = true; break; }
      if (!keep) for (const q of pHomo) { const qm = table.get(q); if (c >= HOMO_RATIO * (((qm && qm.get(w)) || 0) + 1)) { keep = true; break; } }
      if (keep) homoKeep.add(p + '\u0001' + w);
    }
  }
  console.log(`同音詞保護：多保留 ${homoKeep.size} 筆`);
}

let kept = 0;
const out = [];
for (const line of LM.data.split('\n')) {
  const [p, f, k, list] = line.split('\t');
  let items = list.split(',').map((x) => { const i = x.lastIndexOf(':'); return [x.slice(0, i), Number(x.slice(i + 1))]; })
    .filter(([w, c]) => (selected ? selected.has(p + '\u0001' + w) : c >= MIN || (homoKeep && homoKeep.has(p + '\u0001' + w)))).sort((a, b) => b[1] - a[1]);
  if (TOP) items = items.slice(0, TOP);
  if (!items.length) continue;
  kept += items.length;
  // 第三欄改存「退回用的次數」：總次數扣掉保留下來的組合（每個先折扣 D），
  // 這樣被刪掉的組合的次數會回到退回機率裡，不會被當成「從沒一起出現過」而扣分
  const D = 0.75;
  const backoff = Number(f) - items.reduce((s, [, c]) => s + (c - D), 0);
  void k;
  out.push(p + '\t' + f + '\t' + backoff.toFixed(2) + '\t' + items.map(([w, c]) => w + ':' + c).join(','));
}
const js = '// 自動產生：前後文模型（詞與詞相鄰出現的次數）\n' +
  '// 統計來源：中文維基百科（繁體句子），CC BY-SA 4.0，https://zh.wikipedia.org/\n' +
  (process.env.LM_SRC ? '// 口語：Tatoeba（CC BY 2.0 FR，https://tatoeba.org/）、Mozilla Common Voice 台灣華語句子（CC0）\n' : '') +
  'globalThis.ZHUYIN_LM = { tokens: ' + LM.tokens + ', min: ' + MIN + ', data: ' + JSON.stringify(out.join('\n')) + ' };\n';
fs.writeFileSync(path.join(root, 'web', 'lm.js'), js);
console.log(`${KEEP ? `依重要性留 ${KEEP} 筆` : `最少 ${MIN} 次${TOP ? `、每詞最多 ${TOP} 個` : ''}`}：${kept} 種組合、${out.length} 個前詞，${(Buffer.byteLength(js) / 1024 / 1024).toFixed(1)} MB`);
