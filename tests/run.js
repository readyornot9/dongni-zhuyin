// 用法：node tests/run.js
// 1. 跑內建測試題，看選字準確率
// 2. 測試按鍵狀態機（打字、選字、學習）
'use strict';
const path = require('path');
require(path.join(__dirname, '..', 'web', 'dict.js'));
const fs = require('fs');
const { Dictionary, Engine, UserModel, LanguageModel, Composer, KEYMAP, parseReading } = require(path.join(__dirname, '..', 'web', 'engine.js'));
const tests = require(path.join(__dirname, '..', 'web', 'testcases.js'));

const t0 = Date.now();
const dict = new Dictionary(globalThis.ZHUYIN_DICT);
console.log(`詞庫載入 ${Date.now() - t0} ms`);
// 前後文模型（有產生過才用；NO_LM=1 可以關掉比較）
let lmModel = null;
const lmFile = path.join(__dirname, '..', 'web', 'lm.js');
if (fs.existsSync(lmFile) && !process.env.NO_LM) {
  require(lmFile);
  lmModel = new LanguageModel(globalThis.ZHUYIN_LM);
  console.log(`前後文模型：${lmModel.size} 個前詞`);
}
const _Engine = Engine;
// 測試裡所有引擎都帶上前後文模型
function EngineLM(d, u) { return new _Engine(d, u, lmModel); }

let failed = 0;

// ── 選字準確率 ──
const engine = EngineLM(dict, new UserModel());
let pass = 0;
for (const t of tests) {
  const t1 = Date.now();
  const got = engine.convert(parseReading(t.reading)).text;
  const ok = got === t.expect;
  if (ok) pass++;
  console.log(`${ok ? '✔' : '✘'} [${t.group}] ${t.expect}${ok ? '' : `  → 得到：${got}`}  (${Date.now() - t1} ms)`);
}
console.log(`\n選字測試：${pass}/${tests.length} 正確\n`);

// ── 狀態機 ──
const REVERSE = {};
for (const [code, sym] of Object.entries(KEYMAP)) REVERSE[sym] = code;
function keysFor(reading) {
  // 一聲用空白鍵、沒聲調就直接打下一個音
  const keys = [];
  for (const s of parseReading(reading)) {
    for (const ch of s.body) keys.push({ code: REVERSE[ch] });
    if (s.tone === '') keys.push({ code: 'Space' });
    else if (s.tone) keys.push({ code: REVERSE[s.tone] });
  }
  return keys;
}
function type(c, keys) {
  let text = '';
  for (const k of keys) text += c.handleKey(k).commit;
  return text;
}
function check(name, cond) {
  if (!cond) failed++;
  console.log(`${cond ? '✔' : '✘'} ${name}`);
}

{
  const c = new Composer(EngineLM(dict, new UserModel()));
  type(c, keysFor('ㄨㄛˇ ㄧ ㄓˊ ㄗㄞˋ ㄒㄧㄤˇ'));
  check('打完整注音，組字區顯示「我一直在想」', c.result.text === '我一直在想');
  const out = type(c, [{ code: 'Enter' }]);
  check('按 Enter 送出', out === '我一直在想' && !c.composing);
}
{
  const c = new Composer(EngineLM(dict, new UserModel()), { strict: true });
  const keys = (s) => [...s].map((ch) => ({ code: ch === ' ' ? 'Space' : REVERSE[ch] }));
  type(c, keys('ㄊㄨㄢㄍ '));
  check('微軟打法：ㄊㄨㄢ 再按 ㄍ 換掉聲母（得到 關）', c.result.text === '關');
  type(c, keys('ㄜㄇ˙'));
  check('微軟打法：ㄜㄇ˙ 順序打反也是 麼', c.result.text === '關麼');
  type(c, [{ code: 'Escape' }]);
  type(c, keys('ㄈ'));
  const r = c.handleKey({ code: REVERSE['ㄦ'] });
  check('微軟打法：ㄈㄦ 湊不出音，不收 ㄦ 並提示', r.beep && c.pending === 'ㄈ');
  type(c, [{ code: 'Escape' }]);
  type(c, keys('ㄏ ㄠ ˇ'));
  check('微軟打法：ㄏ空白ㄠ空白ˇ 打出注音本身 ㄏㄠˇ', c.result.text === 'ㄏㄠˇ');
  type(c, [{ code: 'Escape' }]);
  type(c, keys('ㄨㄛˇㄏ '));
  check('微軟打法：前面有字時聲母加空白還是簡拼（我會）', c.result.text === '我會');
  type(c, [{ code: 'Escape' }]);
  check('打完聲調再按空白不送出，空白留在組字區', type(c, keys('ㄏㄠˇ ')) === '' && c.composing && c.result.text === '好 ');
  check('按 Enter 才送出', type(c, [{ code: 'Enter' }]) === '好 ' && !c.composing);
  check('沒在打字時空白直接打出', type(c, [{ code: 'Space' }]) === ' ');
}
{
  const c = new Composer(EngineLM(dict, new UserModel()));
  type(c, keysFor('ㄨㄛ* ㄧ* ㄓ* ㄗㄞ* ㄒㄧㄤ*'));
  c.flushPending();
  c.recompute();
  check('不打聲調也能組字', c.result.text === '我一直在想');
}
{
  const user = new UserModel();
  const c = new Composer(EngineLM(dict, user));
  type(c, keysFor('ㄇㄧㄥˊ ㄊㄧㄢ ㄗㄞˋ ㄊㄠˇ ㄌㄨㄣˋ'));
  const before = c.result.text;
  // 把游標移到「在」前面，打開選字窗選「再」
  type(c, [{ code: 'Home' }, { code: 'ArrowRight' }, { code: 'ArrowRight' }, { code: 'ArrowDown' }]);
  const idx = c.cand.list.findIndex((x) => x.w === '再' && x.e - x.s === 1);
  const page = Math.floor(idx / 9);
  c.cand.page = page;
  type(c, [{ code: 'Digit' + ((idx % 9) + 1) }]);
  check(`手動選字：${before} → ${c.result.text}`, c.result.text === '明天再討論');
  type(c, [{ code: 'Enter' }]);
  type(c, keysFor('ㄇㄧㄥˊ ㄊㄧㄢ ㄗㄞˋ ㄊㄠˇ ㄌㄨㄣˋ'));
  check('學習後，同樣的句子第二次就自動選對', c.result.text === '明天再討論');
  check('選過的組合自動記成新詞「明天再討論」', Object.keys(user.phrases).some((k) => k.endsWith('|明天再討論')));
  const abbr = EngineLM(dict, user).convert(parseReading('ㄇ* ㄊ* ㄗ* ㄊ* ㄌ*')).text;
  check(`新詞只打聲母也打得出來（ㄇㄊㄗㄊㄌ → ${abbr}）`, abbr === '明天再討論');
  type(c, [{ code: 'Escape' }]);
  type(c, keysFor('ㄨㄛˇ ㄗㄞˋ ㄐㄧㄚ'));
  check(`沒有學過頭：「我在家」不會變成「我再家」（得到 ${c.result.text}）`, c.result.text === '我在家');
}
{
  // 只打聲母 ㄉㄐㄏ：最後一個字不用按 Enter 就先顯示，可以直接選字
  const c = new Composer(EngineLM(dict, new UserModel()));
  type(c, [{ code: 'Digit2' }, { code: 'KeyR' }, { code: 'KeyC' }]);
  const v = c.view();
  check(`只打聲母時最後一個字也先猜好（得到 ${v.nodes.map((n) => n.w).join('')}）`, v.nodes.map((n) => n.w).join('') === '大家好' && v.pendingIndex === 2);
  type(c, [{ code: 'Space' }]);
  check('ㄏ 按空白鍵：結束這個音但不送出，仍是「大家好」', c.composing && !c.pending && c.result.text === '大家好');
  type(c, [{ code: 'ArrowDown' }]);
  check('接著按 ↓ 可以選字，第一個候選是「大家好」', !!c.cand && c.cand.list[0].w === '大家好');
  type(c, [{ code: 'Escape' }, { code: 'Escape' }]);
  type(c, [{ code: 'Digit5' }, { code: 'Space' }]);
  check('ㄓ 按空白鍵仍然是一聲「知」這類字（得到 ' + c.result.text + '）', c.syls[0].tone === '');
  type(c, [{ code: 'Escape' }]);
  type(c, [{ code: 'Digit2' }, { code: 'KeyR' }, { code: 'KeyC' }, { code: 'ArrowDown' }]);
  check('打到一半直接按 ↓ 也能選字', !!c.cand && c.cand.list[0].w === '大家好');
}
{
  const c = new Composer(EngineLM(dict, new UserModel()));
  const out = type(c, [...keysFor('ㄏㄠˇ'), { code: 'Comma', ctrl: true }]);
  check('Ctrl＋逗號是全形逗號，留在組字區不送出', out === '' && c.result.text === '好，');
  check('Shift＋逗號是半形 <、Shift＋1 是 !', type(c, [{ code: 'Comma', shift: true }, { code: 'Digit1', shift: true }]) === '' && c.result.text === '好，<!');
  check('Ctrl＋Shift 不當標點', !c.handleKey({ code: 'Comma', ctrl: true, shift: true }).handled);
  c.handleKey({ code: 'Backquote' });
  check('` 打開符號面板', !!c.symbol);
  type(c, [{ code: 'Digit2' }]);
  check('符號面板用數字選', c.result.text === '好，<!。' && !c.symbol);
  type(c, [...keysFor('ㄋㄧˇ'), { code: 'Home' }, { code: 'ArrowDown' }]);
  check('打完標點還能回到前面的字選字', !!c.cand && c.cand.list.some((x) => x.w === '好'));
  type(c, [{ code: 'Escape' }]);
  check('按 Enter 整句送出', type(c, [{ code: 'Enter' }]) === '好，<!。你' && !c.composing);
  check('沒在打字時按標點直接送出', type(c, [{ code: 'Comma', ctrl: true }]) === '，');
  type(c, [...keysFor('ㄓㄜˋ'), ...keysFor('ㄧㄤˋ'), { code: 'KeyO', shift: true }, { code: 'KeyK', shift: true }]);
  check('Shift＋字母的大寫英文接在組字區後面，不送出', c.composing && c.result.text.endsWith('OK'));
  type(c, [{ code: 'ArrowLeft' }, { code: 'ArrowLeft' }, { code: 'ArrowLeft' }, { code: 'ArrowDown' }]);
  check('打完英文還能回去選前面的中文', !!c.cand);
  type(c, [{ code: 'Escape' }, { code: 'End' }]);
  c.toggleMode({ commit: '' });
  check('切到英文不送出組字區', c.composing && c.mode === 'en');
  type(c, [{ code: 'Space' }, { code: 'KeyG' }, { code: 'KeyO' }]);
  check('英文模式打的字也接在組字區後面', c.result.text.endsWith('OK go'));
  check('英文模式按 Enter 整句送出', type(c, [{ code: 'Enter' }]).endsWith('OK go') && !c.composing);
  check('英文模式沒在打字時交給程式', !c.handleKey({ code: 'KeyA' }).handled);
  c.toggleMode({ commit: '' });
}

{
  // 預設學習：內建在網頁裡的預設會生效，個人學習疊在上面，清除個人不影響預設
  const base = new UserModel();
  const c0 = new Composer(EngineLM(dict, base));
  c0.syls = parseReading('ㄊㄧㄢˊ ㄉㄧˋ ㄓˇ'); c0.cursor = 0; c0.recompute(); c0.openCandidates();
  c0.selectCandidate(c0.cand.list.find((x) => x.w === '填' && x.e === 1));
  c0.cursor = 1; c0.recompute(); c0.openCandidates();
  const addr = c0.cand.list.find((x) => x.w === '地址');
  if (addr) c0.selectCandidate(addr);
  const before = c0.result.text;
  c0.commitAll({ commit: '' });
  const viewer = new UserModel(null, base.merged());
  const got = EngineLM(dict, viewer).convert(parseReading('ㄊㄧㄢˊ ㄉㄧˋ ㄓˇ')).text;
  check(`擁有者教過的（${before}），新使用者一打開就會（得到 ${got}）`, got === '填地址');
  viewer.clear();
  check('清除個人紀錄不會清掉預設', EngineLM(dict, viewer).convert(parseReading('ㄊㄧㄢˊ ㄉㄧˋ ㄓˇ')).text === '填地址');
}

if (pass < tests.length) console.log(`\n注意：有 ${tests.length - pass} 題選字沒對（這是要改進的目標，不算程式錯誤）`);
if (failed) { console.log(`\n${failed} 項狀態機測試失敗`); process.exit(1); }
