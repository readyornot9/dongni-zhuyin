// 測試引擎服務：模擬外殼送按鍵（不用裝輸入法就能確認服務正常）
// 用法：node windows/server/try-client.js "ㄨㄛˇ ㄇㄣ˙ ㄇㄧㄥˊ ㄊㄧㄢ"
'use strict';
const net = require('net');
const os = require('os');
const path = require('path');
const S = require(path.join(__dirname, '..', '..', 'web', 'engine.js'));

const PIPE = '\\\\.\\pipe\\shunda-ime-' + os.userInfo().username.replace(/[^\w.-]/g, '_');
const codeOf = new Map(Object.entries(S.KEYMAP).map(([code, sym]) => [sym, code]));
const keys = [];
for (const syl of (process.argv[2] || 'ㄨㄛˇ ㄇㄣ˙').split(' ')) {
  for (const ch of syl) keys.push({ t: 'key', code: codeOf.get(ch) });
  if (!/[ˊˇˋ˙]$/.test(syl)) keys.push({ t: 'key', code: 'Space' });
}
keys.push({ t: 'key', code: 'Enter' });

const sock = net.connect(PIPE);
sock.setEncoding('utf8');
let buf = '', i = 0;
const send = () => { const k = keys[i++]; const t = Date.now(); sock.write(JSON.stringify(k) + '\n'); return t; };
let t0 = send();
sock.on('data', (d) => {
  buf += d;
  let n;
  while ((n = buf.indexOf('\n')) >= 0) {
    const res = JSON.parse(buf.slice(0, n));
    buf = buf.slice(n + 1);
    console.log(`${keys[i - 1].code.padEnd(10)} ${String(Date.now() - t0).padStart(4)}ms  組字「${res.preedit}」游標 ${res.caret}${res.commit ? `  送出「${res.commit}」` : ''}`);
    if (i < keys.length) t0 = send(); else sock.end();
  }
});
sock.on('error', (e) => { console.error('連不上服務：' + e.message); process.exit(1); });
