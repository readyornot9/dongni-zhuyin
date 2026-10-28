// 懂你注音 Windows 版的引擎服務：輸入法外殼（ShunDaTip.dll）透過 named pipe 把按鍵送過來，
// 這裡用跟測試台同一套引擎（web/engine.js、nn.js）處理，回傳組字區、候選字和要送出的文字。
// 只在本機運作：pipe 只接受同一台電腦、同一個使用者的連線，不連網路。
// 用法：node windows/server/ime-server.js（外殼找不到服務時會自己啟動）
'use strict';
const fs = require('fs');
const net = require('net');
const os = require('os');
const path = require('path');
const { Worker } = require('worker_threads');

const root = path.join(__dirname, '..', '..');
const web = (f) => path.join(root, 'web', f);
require(web('dict.js'));
require(web('lm.js'));
require(web('seed.js'));
const S = require(web('engine.js'));
const NeuralLM = require(web('nn.js'));

const PIPE = '\\\\.\\pipe\\shunda-ime-' + os.userInfo().username.replace(/[^\w.-]/g, '_');
const DATA_DIR = path.join(process.env.APPDATA || os.homedir(), 'ShunDa');
const STORE = path.join(DATA_DIR, 'user.json');
const LOG = path.join(DATA_DIR, 'server.log');
fs.mkdirSync(DATA_DIR, { recursive: true });
const log = (msg) => { try { fs.appendFileSync(LOG, new Date().toISOString() + ' ' + msg + '\n'); } catch (e) { /* 寫不了紀錄不影響打字 */ } };

// ── 引擎 ──
let stored = {};
try { stored = JSON.parse(fs.readFileSync(STORE, 'utf8')); } catch (e) { /* 第一次用，沒有紀錄 */ }
const SEED = globalThis.ZHUYIN_SEED || { learning: {} };
const user = new S.UserModel(stored.user, SEED.learning);
const engine = new S.Engine(new S.Dictionary(globalThis.ZHUYIN_DICT), user, new S.LanguageModel(globalThis.ZHUYIN_LM));
// strict：微軟打法（一個音按聲調或空白才結束，同位置的符號會換掉）；要改回可以不打聲調連打，把 user.json 的 strict 改成 false
const composer = new S.Composer(engine, { strict: stored.strict !== false });
if (stored.stats) Object.assign(composer.stats, stored.stats);
if (stored.mode === 'en') composer.mode = 'en';

let saveTimer = 0;
function save() {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    const tmp = STORE + '.tmp';
    fs.writeFileSync(tmp, JSON.stringify({ user: user.toJSON(), stats: composer.stats, mode: composer.mode, strict: composer.opts.strict }));
    fs.renameSync(tmp, STORE); // 先寫暫存檔再改名，當機也不會把學習紀錄寫壞
  }, 500);
}

// ── AI 複審（在另一個執行緒跑，打字不會卡） ──
// 每次按鍵後先用統計結果回應，背景同時複審；送出前如果還沒複審完，就等它（最多 NN_WAIT 毫秒）
const NN_WAIT = 1500;
const NN_IDLE = 150;
const nn = { worker: null, ready: false, seq: 0, pending: new Map(), doneSig: '', job: null };
try {
  nn.worker = new Worker(path.join(__dirname, 'nn-thread.js'), { workerData: { root } });
  nn.worker.on('message', (m) => {
    if (m.ready) { nn.ready = true; log('AI 複審已啟用'); return; }
    if (m.error) { log('AI 複審載入失敗：' + m.error); return; }
    const cb = nn.pending.get(m.id);
    if (cb) { nn.pending.delete(m.id); cb(m); }
  });
  nn.worker.on('error', (e) => { nn.ready = false; log('AI 執行緒錯誤：' + e); });
} catch (e) { log('AI 複審未啟用：' + e); }

const signature = () => composer.syls.map((s) => s.body + (s.tone === null ? '*' : s.tone)).join(' ') + '|' + JSON.stringify(composer.locks) + '|' + composer.lastCommittedWord;

// 回傳 Promise：複審完（或不需要、逾時）就 resolve
function rerank() {
  if (!nn.ready || !composer.syls.length || composer.pending) return Promise.resolve();
  const sig = signature();
  if (nn.doneSig === sig) return Promise.resolve();
  if (nn.job && nn.job.sig === sig) return nn.job.promise;
  const r = engine.convert(composer.syls, { locks: composer.locks, leftWord: composer.lastCommittedWord, nbest: NeuralLM.TOP, beam: NeuralLM.BEAM, perState: NeuralLM.PER_STATE });
  if (!NeuralLM.shouldRerank(r.nbest)) { nn.doneSig = sig; return Promise.resolve(); }
  const id = ++nn.seq;
  const gen = S.hasAbbr(composer.syls);
  const syls = composer.syls.slice(), opts = { locks: composer.locks.slice(), leftWord: composer.lastCommittedWord };
  const promise = new Promise((resolve) => {
    const timer = setTimeout(() => { nn.pending.delete(id); resolve(); }, NN_WAIT);
    nn.pending.set(id, (m) => {
      clearTimeout(timer);
      if (sig === signature()) {
        const merged = m.gen && m.gen.length ? NeuralLM.merge(engine, syls, opts, r.nbest, m.scores, m.gen) : { list: r.nbest, scores: m.scores };
        const pick = NeuralLM.pick(merged.list, merged.scores);
        composer.result = { nodes: pick.nodes, text: pick.text, score: pick.score };
        nn.doneSig = sig;
      }
      resolve();
    });
  });
  nn.job = { sig, promise };
  nn.worker.postMessage({ id, texts: r.nbest.map((c) => c.text), choices: gen ? engine.charChoices(syls, opts.locks) : null });
  return promise;
}

// ── 按鍵 ──
function willCommit(k) {
  if (!composer.composing || composer.cand || composer.symbol || composer.mode !== 'zh' || k.alt) return false;
  if (k.ctrl) return !!S.punctOf(k) && !composer.composing;
  return k.code === 'Enter' || k.code === 'NumpadEnter' || (k.code === 'Space' && !composer.pending) || (!!S.punctOf(k) && !composer.composing);
}

// 不改狀態，只判斷這個鍵輸入法要不要接手（TSF 的 OnTestKeyDown 用）
function wouldHandle(k) {
  if (k.ctrl && k.code === 'Space') return true;
  if (k.ctrl || k.alt) return !k.alt && composer.mode === 'zh' && !composer.symbol && !composer.cand && !!S.punctOf(k);
  if (composer.symbol || composer.cand) return true;
  if (composer.mode === 'en') return false;
  if (composer.composing) return !['ShiftLeft', 'ShiftRight', 'ControlLeft', 'ControlRight', 'CapsLock'].includes(k.code);
  if (k.code === 'Backquote' && !k.shift) return true;
  if (!k.shift && S.KEYMAP[k.code]) return true;
  if (S.punctOf(k)) return true;
  if (k.shift && /^Key[A-Z]$/.test(k.code)) return true;
  return false;
}

function state(out) {
  const v = composer.view();
  const preeditOf = () => v.nodes.map((n) => n.w).join('');
  let preedit = preeditOf(), caret = 0;
  // 游標位置：游標前面有幾個音節，就是前面有幾個字（詞庫沒有的音節整段顯示注音，算到它後面）
  for (const n of v.nodes) {
    if (n.e <= v.cursor) caret += [...n.w].length;
    else if (n.s < v.cursor) caret += [...n.w].length === n.e - n.s ? v.cursor - n.s : [...n.w].length;
  }
  if (composer.pending) {
    // 還在打的音節：顯示注音本身，看得出現在打到哪
    const chars = [...preedit];
    const before = chars.slice(0, caret - 1).join(''), after = chars.slice(caret).join('');
    preedit = before + composer.orderedPending() + after;
    caret = [...before].length + [...composer.orderedPending()].length;
  }
  let cand = null;
  if (composer.cand) {
    const ps = composer.opts.pageSize;
    cand = { items: composer.candPage().map((c) => c.w), hi: composer.cand.hi % ps, page: composer.cand.page, pages: Math.ceil(composer.cand.list.length / ps) };
  } else if (composer.symbol) {
    const s = composer.symbol;
    cand = { items: composer.symbolPage(), hi: -1, page: s.page, pages: Math.ceil(S.SYMBOLS[s.cat].items.length / composer.opts.pageSize), title: S.SYMBOLS[s.cat].name };
  }
  return { handled: !!out.handled, commit: out.commit || '', preedit, caret, cand, mode: composer.mode, beep: !!out.beep };
}

async function handle(msg) {
  const out = { handled: false, commit: '' };
  switch (msg.t) {
    case 'test':
      return { handled: wouldHandle(msg) };
    case 'key': {
      const k = { code: msg.code, shift: !!msg.shift, ctrl: !!msg.ctrl, alt: !!msg.alt, meta: false };
      if (willCommit(k)) { composer.flushPending(); await rerank(); }
      Object.assign(out, composer.handleKey(k));
      break;
    }
    case 'toggle': // Shift 單按：切換中英
      composer.toggleMode(out); out.handled = true;
      break;
    case 'commit': // 換視窗、點滑鼠：把組字區送出
      if (composer.composing) { composer.flushPending(); await rerank(); composer.commitAll(out); }
      composer.cand = null; composer.symbol = null;
      break;
    case 'cancel':
      composer.reset();
      break;
    case 'state':
      break;
    case 'settle': // 外殼在使用者停手後來問：馬上回目前的組字區，複審還沒好就標 busy，外殼過一下再問（不能讓外殼卡住等）
      if (!composer.pending) rerank();
      out.busy = nn.ready && composer.syls.length > 0 && !composer.pending && nn.doneSig !== signature() && !!nn.job && nn.job.sig === signature();
      break;
    default:
      return { error: 'unknown' };
  }
  if (out.commit) save();
  // 停手 NN_IDLE 毫秒才在背景複審（每個鍵都複審會讓工作堆積），送出時就直接用結果。
  // 複審在另一條執行緒算，不排進按鍵佇列，算的時候按鍵照樣馬上處理
  if (msg.t !== 'settle') {
    clearTimeout(nn.idleTimer);
    nn.idleTimer = setTimeout(rerank, NN_IDLE);
  }
  const res = state(out);
  if (out.busy) res.busy = true;
  return res;
}

// ── named pipe ──
// 一個連線一次處理一行 JSON；按順序處理，不會兩個按鍵同時改狀態
let queue = Promise.resolve();
const server = net.createServer((sock) => {
  sock.setEncoding('utf8');
  let buf = '';
  sock.on('data', (d) => {
    buf += d;
    let i;
    while ((i = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, i);
      buf = buf.slice(i + 1);
      queue = queue.then(async () => {
        let res;
        try { res = await handle(JSON.parse(line)); } catch (e) { log('處理失敗：' + (e && e.stack || e)); res = { error: String(e) }; }
        if (!sock.destroyed) sock.write(JSON.stringify(res) + '\n');
      });
    }
  });
  sock.on('error', () => {});
});
server.on('error', (e) => {
  // 已經有一個服務在跑（外殼重複啟動）：這個就結束
  log('pipe 錯誤：' + e.code);
  process.exit(e.code === 'EADDRINUSE' ? 0 : 1);
});
server.listen(PIPE, () => log('服務啟動：' + PIPE));
process.on('exit', () => { clearTimeout(saveTimer); });
module.exports = { handle, PIPE };
