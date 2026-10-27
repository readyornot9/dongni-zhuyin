// 比對 JavaScript 推論（web/nn.js）跟 Python 的結果，並量速度
// 用法：python tools/nn/check_js.py > ref.json；node tools/nn/check-js.js ref.json
'use strict';
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..', '..');
const NeuralLM = require(path.join(root, 'web', 'nn.js'));

let t = Date.now();
const lm = new NeuralLM(require('fs').readFileSync(path.join(root, 'web', 'nnlm.wasm')));
console.log(`載入 ${Date.now() - t} ms`);
const ref = JSON.parse(fs.readFileSync(process.argv[2], 'utf8').replace(/^﻿/, '').trim().split(/\r?\n/).pop());
const texts = Object.keys(ref);
const got = lm.score(texts);
let maxDiff = 0;
texts.forEach((s, i) => {
  maxDiff = Math.max(maxDiff, Math.abs(got[i] - ref[s]));
  console.log(`${s}：JS ${got[i].toFixed(3)}，Python ${ref[s].toFixed(3)}`);
});
console.log(`最大差距 ${maxDiff.toFixed(4)}（< 0.01 就算一致）`);

// 速度：10 個候選句（一般長度），模擬一次複審
const cands = ['我等一下要去開會', '我等一下要去開匯', '我等一下要去凱會', '我等一夏要去開會', '我等一下藥去開會',
  '我等一下要去開惠', '我等以下要去開會', '我等一下要趣開會', '我等一下要去開繪', '我等一嚇要去開會'];
lm.score(cands); // 暖機
t = Date.now();
for (let i = 0; i < 5; i++) lm.score(cands);
console.log(`複審 10 句（每句 8 字）平均 ${((Date.now() - t) / 5).toFixed(0)} ms`);
