// 檢查一句話的候選名單和 AI 複審分數
// 用法：node tools/nn/inspect.js "注音（空白分隔）" [幾名=10]
'use strict';
const path = require('path');
const root = path.join(__dirname, '..', '..');
require(path.join(root, 'web', 'dict.js'));
require(path.join(root, 'web', 'lm.js'));
const S = require(path.join(root, 'web', 'engine.js'));
const NeuralLM = require(path.join(root, 'web', 'nn.js'));

const engine = new S.Engine(new S.Dictionary(globalThis.ZHUYIN_DICT), new S.UserModel(), new S.LanguageModel(globalThis.ZHUYIN_LM));
const nn = new NeuralLM(require('fs').readFileSync(path.join(root, 'web', 'nnlm.wasm')));
const N = Number(process.argv[3] || 10);
const r = engine.convert(S.parseReading(process.argv[2]), { nbest: N, beam: Number(process.env.BEAM || NeuralLM.BEAM), perState: Number(process.env.PER_STATE || NeuralLM.PER_STATE) });
const lp = nn.score(r.nbest.map((c) => c.text));
r.nbest.forEach((c, i) => {
  const total = c.score + NeuralLM.WEIGHT * lp[i];
  console.log(`${String(i + 1).padStart(2)}. ${c.text.padEnd(14, '　')} 統計 ${c.score.toFixed(1)}  AI ${lp[i].toFixed(1)}  合計 ${total.toFixed(1)}  ${c.nodes.map((n) => n.w).join('/')}`);
});
