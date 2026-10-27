// 匯出每題的前幾名候選句，給神經網路模型複審實驗用
// 用法：node tools/nbest.js [每題幾句=10]  → data/work/nbest.json，並印出「正確答案在前 N 名內」的比例
'use strict';
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
require(path.join(root, 'web', 'dict.js'));
require(path.join(root, 'web', 'lm.js'));
const S = require(path.join(root, 'web', 'engine.js'));
const tests = require(path.join(root, 'web', 'testcases.js'));
const N = Number(process.argv[2] || 10);

const dict = new S.Dictionary(globalThis.ZHUYIN_DICT);
const lm = new S.LanguageModel(globalThis.ZHUYIN_LM);
const engine = new S.Engine(dict, new S.UserModel(), lm);

// 評估句子反查注音（同 tools/eval.js）
function natural(w, r) {
  const syl = r.split(' ');
  return [...w].every((ch, i) => (ch !== '不' || syl[i] === 'ㄅㄨˋ') && (ch !== '一' || syl[i] === 'ㄧ'));
}
const best = new Map();
for (const line of globalThis.ZHUYIN_DICT.split('\n')) {
  const [w, r] = line.split('\t');
  if (!best.has(w) || (!natural(w, best.get(w)) && natural(w, r))) best.set(w, r);
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
    out.push(found[1]);
    i += found[0];
  }
  return out.join(' ');
}
const evalSet = fs.readFileSync(path.join(root, 'docs', '評估句子.txt'), 'utf8').split(/\r?\n/).filter(Boolean)
  .map((t) => ({ set: 'eval', expect: t, reading: toReading(t) })).filter((t) => t.reading);
const all = tests.map((t) => ({ set: 'tests', expect: t.expect, reading: t.reading })).concat(evalSet);

const out = [];
const hit = { tests: [0, 0, 0], eval: [0, 0, 0] }; // 第一名對、前 N 名內有、總數
for (const t of all) {
  const r = engine.convert(S.parseReading(t.reading), { nbest: N, beam: 64, perState: 4 });
  const texts = r.nbest.map((c) => c.text);
  const h = hit[t.set];
  h[2]++;
  if (texts[0] === t.expect) h[0]++;
  if (texts.includes(t.expect)) h[1]++;
  out.push({ set: t.set, expect: t.expect, cands: r.nbest.map((c) => ({ text: c.text, score: c.score })) });
}
fs.writeFileSync(path.join(root, 'data', 'work', 'nbest.json'), JSON.stringify(out));
for (const [k, [top, inN, n]] of Object.entries(hit)) {
  console.log(`${k === 'eval' ? '評估句子' : '測試題'}：第一名就對 ${top}/${n}，正確答案在前 ${N} 名內 ${inN}/${n}`);
}
