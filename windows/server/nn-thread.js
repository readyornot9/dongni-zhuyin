// AI 複審執行緒（跟網頁版 nn-worker.js 做一樣的事，改用 Node 的 worker_threads）
'use strict';
const fs = require('fs');
const path = require('path');
const { parentPort, workerData } = require('worker_threads');

const web = (f) => path.join(workerData.root, 'web', f);
const NeuralLM = require(web('nn.js'));
let lm = null, rev = null;
try {
  lm = new NeuralLM(fs.readFileSync(web('nnlm.wasm')));
  // 反方向模型是選配的：沒有就只用正向
  if (fs.existsSync(web('nnlm-rev.wasm'))) rev = new NeuralLM(fs.readFileSync(web('nnlm-rev.wasm')));
  parentPort.postMessage({ ready: true, both: !!rev });
} catch (e) {
  parentPort.postMessage({ error: String(e) });
}

parentPort.on('message', ({ id, texts, choices }) => {
  const scores = NeuralLM.both(lm, rev, texts);
  let gen = choices ? lm.generate(choices) : []; // 簡拼時讓 AI 自己寫句子
  if (rev && gen.length) {
    const s = NeuralLM.both(lm, rev, gen.map((g) => g.text));
    gen = gen.map((g, i) => ({ text: g.text, lp: s[i] }));
  }
  parentPort.postMessage({ id, scores, gen });
});
