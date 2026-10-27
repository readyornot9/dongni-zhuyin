// 把 data/raw/ 的小麥注音詞庫（MIT）轉成 web/dict.js
// 每行格式：詞<TAB>注音（音節以空白分隔）<TAB>分數（自然對數機率，越大越常用）
// 用法：node tools/build-dict.js

'use strict';
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const raw = (f) => fs.readFileSync(path.join(root, 'data', 'raw', f), 'utf8').split(/\r?\n/).filter(Boolean);

// 詞頻
const occ = new Map();
let total = 0;
for (const line of raw('phrase.occ')) {
  const i = line.lastIndexOf(' ');
  const n = Number(line.slice(i + 1));
  occ.set(line.slice(0, i), n);
  total += n;
}

// 破音字：同一個字的詞頻會被它的每個讀音共用（「會」連 ㄎㄨㄞˋ 都拿到高分）。
// 從含有這個字的詞推算每個讀音的使用比例，例如「會議、機會」→ ㄏㄨㄟˋ，「會計」→ ㄎㄨㄞˋ。
const mappings = raw('BPMFMappings.txt').map((l) => l.split(' ')).filter((p) => p.length >= 3);
const readingUse = new Map(); // 字 → Map(讀音 → 次數)
for (const p of mappings) {
  const chars = [...p[0]];
  const syl = p.slice(1);
  if (chars.length !== syl.length) continue;
  const n = occ.get(p[0]) || 0;
  chars.forEach((ch, i) => {
    let m = readingUse.get(ch);
    if (!m) readingUse.set(ch, (m = new Map()));
    m.set(syl[i], (m.get(syl[i]) || 0) + n);
  });
}
const charReadings = new Map(); // 字 → [讀音...]
for (const line of raw('BPMFBase.txt')) {
  const [ch, reading] = line.split(' ');
  if (!ch || !reading) continue;
  if (!charReadings.has(ch)) charReadings.set(ch, []);
  if (!charReadings.get(ch).includes(reading)) charReadings.get(ch).push(reading);
}
function readingShare(ch, reading) {
  const list = charReadings.get(ch);
  if (!list || list.length < 2) return 1;
  const use = readingUse.get(ch) || new Map();
  // 加 1 平滑，沒出現在任何詞裡的讀音也保留一點機會
  const total = list.reduce((s, r) => s + (use.get(r) || 0) + 1, 0);
  return ((use.get(reading) || 0) + 1) / total;
}

// 多音的詞也一樣：「說得」有 ㄕㄨㄛ ㄉㄜˊ 和很少用的 ㄕㄨㄟˋ ㄉㄜˊ（遊說得…）兩種讀法，
// 原本兩種都拿到整個詞的次數，打「ㄕㄨㄟˋ ㄉㄜˊ」時「說得」反而贏過「睡得」。
// 依每個字各讀音的比例推算：最常見的讀法保留全部次數，其他讀法按比例縮小。
// 只看音節本身（ㄕㄨㄛ／ㄕㄨㄟ），聲調不同（ㄉㄜˊ／ㄉㄜ˙）不算，打字時聲調本來就常常不一樣
const bodyOf = (syl) => syl.replace(/[ˊˇˋ˙]/g, '');
function bodyShare(ch, body) {
  const list = charReadings.get(ch);
  if (!list || list.length < 2) return 1;
  return list.filter((r) => bodyOf(r) === body).reduce((a, r) => a + readingShare(ch, r), 0);
}
const phraseReadings = new Map(); // 詞 → [讀音...]（之後在 add 裡用）
function phraseShare(word, reading) {
  const list = phraseReadings.get(word);
  if (!list || list.length < 2) return 1;
  const chars = [...word];
  const fit = (r) => {
    const syl = r.split(' ');
    return syl.length === chars.length ? chars.reduce((a, ch, i) => a * bodyShare(ch, bodyOf(syl[i])), 1) : 1;
  };
  const best = Math.max(...list.map(fit));
  return best > 0 ? fit(reading) / best : 1;
}

// 用維基百科「單獨當一個詞」的次數校正詞頻（data/work/wiki-unigram.tsv，tools/wiki-unigram.js 產生）。
// 小麥詞庫的單字次數包含了它出現在詞裡的次數（「說」在「說明、小說」裡也算），單獨使用時被高估。
// 兩個來源的機率加權平均；WIKI_WEIGHT=0 就是只用小麥詞庫
// 實測（評估句子 199 句）：0 → 170、0.3 → 170、0.5 → 167、0.9 → 163，百科用詞跟日常打字差太多，預設不用
const WIKI_WEIGHT = Number(process.env.WIKI_WEIGHT ?? 0);
const wikiFile = path.join(root, 'data', 'work', 'wiki-unigram.tsv');
const wiki = new Map();
let wikiTotal = 0;
if (WIKI_WEIGHT > 0 && fs.existsSync(wikiFile)) {
  for (const line of fs.readFileSync(wikiFile, 'utf8').split('\n')) {
    const [w, n] = line.split('\t');
    if (w === '#tokens') wikiTotal = Number(n); else if (w) wiki.set(w, Number(n));
  }
}

const entries = new Map(); // key: 詞\t注音 → 分數
function add(word, reading, rare) {
  const key = word + '\t' + reading;
  if (entries.has(key)) return;
  let n = occ.get(word) || 0;
  const share = [...word].length === 1 ? readingShare(word, reading) : phraseShare(word, reading);
  n *= share;
  // +0.5 平滑；沒有詞頻的罕用字（非 big5）再扣分，避免冒出奇怪的字
  let p = (n + 0.5) / total;
  if (wikiTotal) p = (1 - WIKI_WEIGHT) * p + WIKI_WEIGHT * (((wiki.get(word) || 0) * share + 0.5) / wikiTotal);
  let score = Math.log(p);
  if (rare && n === 0) score -= 3;
  entries.set(key, Math.round(score * 100) / 100);
}

// 單字：字 注音 拼音 按鍵 字集
for (const line of raw('BPMFBase.txt')) {
  const [ch, reading, , , charset] = line.split(' ');
  if (!ch || !reading) continue;
  add(ch, reading, charset !== 'big5');
}

// 先收集每個詞的所有讀法（含「不」「一」本調的寫法，跟下面 add 的一致）
const plainOf = (chars, syl) => syl.map((s, i) => (chars[i] === '不' && s === 'ㄅㄨˊ' ? 'ㄅㄨˋ' : chars[i] === '一' && (s === 'ㄧˊ' || s === 'ㄧˋ') ? 'ㄧ' : s));
for (const parts of mappings) {
  const w = parts[0], chars = [...w], syl = parts.slice(1);
  if (!phraseReadings.has(w)) phraseReadings.set(w, []);
  const list = phraseReadings.get(w);
  for (const r of [syl.join(' '), chars.length === syl.length ? plainOf(chars, syl).join(' ') : '']) if (r && !list.includes(r)) list.push(r);
}

// 詞：詞 音節1 音節2 ...
for (const parts of mappings) {
  add(parts[0], parts.slice(1).join(' '), false);
  // 「不」「一」的變調：詞庫常只收實際發音（不在 ㄅㄨˊ ㄗㄞˋ），但大家打字都打本調（ㄅㄨˋ、ㄧ），
  // 不補的話「不在」根本不會出現在候選裡，只能用單字湊成「部在」
  const chars = [...parts[0]];
  const syl = parts.slice(1);
  if (chars.length !== syl.length) continue;
  const plain = plainOf(chars, syl);
  if (plain.join(' ') !== syl.join(' ')) add(parts[0], plain.join(' '), false);
}

// 自己補的常用語（data/extra-phrases.txt）
const extraFile = path.join(root, 'data', 'extra-phrases.txt');
if (fs.existsSync(extraFile)) {
  for (const line of fs.readFileSync(extraFile, 'utf8').split(/\r?\n/)) {
    if (!line.trim() || line.startsWith('#')) continue;
    const [w, n, ...syl] = line.trim().split(/\s+/);
    occ.set(w, Math.max(occ.get(w) || 0, Number(n))); // 詞庫已有的詞：取比較大的次數（上班常用、但原詞庫偏低的詞）
    entries.delete(w + '\t' + syl.join(' ')); // 已經算過分數的話，用新的次數重算
    add(w, syl.join(' '), false);
  }
}

// 聊天常用短語（data/chat-phrases.txt）：第 4 欄標 1，只打聲母時引擎會優先
const chatFile = path.join(root, 'data', 'chat-phrases.txt');
const chat = new Set();
if (fs.existsSync(chatFile)) {
  for (const line of fs.readFileSync(chatFile, 'utf8').split(/\r?\n/)) if (line.trim() && !line.startsWith('#')) chat.add(line.trim());
  const have = new Set([...entries.keys()].map((k) => k.split('\t')[0]));
  const missing = [...chat].filter((w) => !have.has(w));
  if (missing.length) console.log('聊天短語不在詞庫（請加到 extra-phrases.txt）：' + missing.join('、'));
}
// 口語詞頻（data/work/colloq-unigram.tsv，tools/colloquial-unigram.js 產生）：第 5 欄，只打聲母時引擎會混用
const colloqFile = path.join(root, 'data', 'work', 'colloq-unigram.tsv');
const colloq = new Map();
let colloqTotal = 0;
if (fs.existsSync(colloqFile)) {
  for (const line of fs.readFileSync(colloqFile, 'utf8').split('\n')) {
    const [w, n] = line.split('\t');
    if (w === '#total') colloqTotal = Number(n); else if (w) colloq.set(w, Number(n));
  }
}
// 口語語料只有字、沒有讀音，一樣按讀音比例分（不然「會」唸 ㄎㄨㄞˋ、「吃」唸 ㄐㄧˊ 也拿到高分）
const colloqScore = (w, r) => {
  const share = [...w].length === 1 ? readingShare(w, r) : phraseShare(w, r);
  return Math.round(Math.log((colloq.get(w) * share + 0.5) / colloqTotal) * 100) / 100;
};
const lines = [...entries].map(([k, s]) => ({ k, s })).sort((a, b) => b.s - a.s).map((e) => {
  const [w, r] = e.k.split('\t');
  const extra = colloq.has(w) ? '\t' + (chat.has(w) ? '1' : '') + '\t' + colloqScore(w, r) : chat.has(w) ? '\t1' : '';
  return e.k + '\t' + e.s + extra;
});

const out = '// 自動產生，請勿手動修改。來源：McBopomofo 詞庫（MIT License）\n' +
  'globalThis.ZHUYIN_DICT = ' + JSON.stringify(lines.join('\n')) + ';\n' +
  // 口語語料沒出現過的詞的口語分數（第 5 欄空白時用）
  (colloqTotal ? `globalThis.ZHUYIN_COLLOQ_FLOOR = ${Math.round(Math.log(0.5 / colloqTotal) * 100) / 100};\n` : '');
fs.writeFileSync(path.join(root, 'web', 'dict.js'), out);
console.log(`完成：${lines.length} 筆，${(out.length / 1024 / 1024).toFixed(1)} MB → web/dict.js`);
