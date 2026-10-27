// 評估選字準確率：內建測試題＋docs/評估句子.txt，算整句正確率和逐字正確率
// 用法：node tools/eval.js                 （目前設定）
//       node tools/eval.js --sweep         （掃前後文模型參數）
//       EVAL_FILE=docs/再在句子.txt NN=1 node tools/eval.js  （改用別的句子檔，例如再／在專門題）
//       NO_LM=1 node tools/eval.js          （不用前後文模型，當基準）
'use strict';
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
require(path.join(root, 'web', 'dict.js'));
const S = require(path.join(root, 'web', 'engine.js'));
const tests = require(path.join(root, 'web', 'testcases.js'));

const dict = new S.Dictionary(globalThis.ZHUYIN_DICT);
let lm = null;
if (!process.env.NO_LM) {
  require(path.join(root, 'web', 'lm.js'));
  if (globalThis.ZHUYIN_LM) lm = new S.LanguageModel(globalThis.ZHUYIN_LM);
}

// 評估句子：從文字反查注音（優先本調，跟 tools/text-to-reading.js 相同）
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
const evalSet = fs.readFileSync(process.env.EVAL_FILE || path.join(root, 'docs', '評估句子.txt'), 'utf8').split(/\r?\n/).filter(Boolean)
  .map((t) => ({ group: '評估句子', expect: t, reading: toReading(t) })).filter((t) => t.reading);

// 神經網路複審（NN=1 開啟；NN_MIN 可調「最少幾個字才複審」）
let nn = null, nnRev = null, NeuralLM = null;
if (process.env.NN) {
  NeuralLM = require(path.join(root, 'web', 'nn.js'));
  nn = new NeuralLM(require('fs').readFileSync(path.join(root, 'web', 'nnlm.wasm')));
  // 反方向模型（REV=0 不用；REV_MIX 調正反比例）
  const revPath = process.env.REV_FILE || path.join(root, 'web', 'nnlm-rev.wasm');
  if (process.env.REV !== '0' && fs.existsSync(revPath)) nnRev = new NeuralLM(fs.readFileSync(revPath));
  if (process.env.REV_MIX) NeuralLM.REV_MIX = Number(process.env.REV_MIX);
  if (process.env.NN_MIN) NeuralLM.MIN_CHARS = Number(process.env.NN_MIN);
  if (process.env.NN_WEIGHT) NeuralLM.WEIGHT = Number(process.env.NN_WEIGHT);
}

function convert(engine, reading) {
  if (!nn) return engine.convert(S.parseReading(reading)).text;
  const r = engine.convert(S.parseReading(reading), { nbest: Number(process.env.NN_TOP || NeuralLM.TOP), beam: Number(process.env.BEAM || NeuralLM.BEAM), perState: Number(process.env.PER_STATE || NeuralLM.PER_STATE) });
  if (!NeuralLM.shouldRerank(r.nbest)) return r.text;
  const syls = S.parseReading(reading);
  const scores = NeuralLM.both(nn, nnRev, r.nbest.map((c) => c.text));
  if (process.env.GEN !== '0' && (process.env.GEN === 'all' || S.hasAbbr(syls))) {
    let gen = nn.generate(engine.charChoices(syls));
    if (nnRev && gen.length) { const g = NeuralLM.both(nn, nnRev, gen.map((x) => x.text)); gen = gen.map((x, i) => ({ text: x.text, lp: g[i] })); }
    const m = NeuralLM.merge(engine, syls, {}, r.nbest, scores, gen);
    return NeuralLM.pick(m.list, m.scores).text;
  }
  return NeuralLM.pick(r.nbest, scores).text;
}

function run(set, engine) {
  let ok = 0, chars = 0, right = 0;
  const bad = [];
  for (const t of set) {
    const got = convert(engine, t.reading);
    const e = [...t.expect], g = [...got];
    chars += e.length;
    e.forEach((c, i) => { if (g[i] === c) right++; });
    if (got === t.expect) ok++; else bad.push(`${t.expect}→${got}`);
  }
  return { ok, n: set.length, charAcc: right / chars, bad };
}

if (process.env.CHAR_MODE) S.LanguageModel.CHAR_MODE = process.env.CHAR_MODE;
if (process.env.USAGE) S.Engine.USAGE_PENALTY = Number(process.env.USAGE);
if (process.env.UNIGRAM_WEIGHT) S.Engine.UNIGRAM_WEIGHT = Number(process.env.UNIGRAM_WEIGHT);

function report(label) {
  const engine = new S.Engine(dict, new S.UserModel(), lm);
  const a = run(tests, engine), b = run(evalSet, engine);
  console.log(`${label}  測試題 ${a.ok}/${a.n}（逐字 ${(a.charAcc * 100).toFixed(1)}%）  評估句子 ${b.ok}/${b.n}（逐字 ${(b.charAcc * 100).toFixed(1)}%）`);
  return { a, b };
}

if (process.argv.includes('--sweep')) {
  for (const w of [0.5, 0.7, 1]) for (const pen of [2, 4, 6]) for (const boost of [4, 8]) {
    Object.assign(S.LanguageModel, { WEIGHT: w, MAX_PENALTY: pen, MAX_BOOST: boost });
    report(`權重 ${w} 扣分上限 ${pen} 加分上限 ${boost}`);
  }
} else {
  const { a, b } = report((lm ? '有前後文模型' : '沒有前後文模型') + (nn ? `＋神經網路複審（≥${NeuralLM.MIN_CHARS} 字${nnRev ? '，正反兩向' : ''}）` : ''));
  if (process.argv.includes('-v')) console.log('\n錯的：\n' + a.bad.concat(b.bad).join('\n'));
}
