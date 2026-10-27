// 從完整前後文模型（data/work/lm-full.js）取出每個詞在維基百科「單獨當一個詞」出現的次數
// 輸出 data/work/wiki-unigram.tsv（詞<TAB>次數），給 build-dict.js 校正詞頻
// 用法：node tools/wiki-unigram.js
'use strict';
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
require(path.join(root, 'data', 'work', 'lm-full.js'));
const LM = globalThis.ZHUYIN_LM;

// 每行第二欄是這個詞後面接任何詞的總次數，約等於它出現的次數（句尾那次沒算到，影響很小）
const rows = [];
for (const line of LM.data.split('\n')) {
  const t1 = line.indexOf('\t');
  const t2 = line.indexOf('\t', t1 + 1);
  rows.push(line.slice(0, t1) + '\t' + line.slice(t1 + 1, t2));
}
fs.writeFileSync(path.join(root, 'data', 'work', 'wiki-unigram.tsv'), `#tokens\t${LM.tokens}\n` + rows.join('\n'));
console.log(`完成：${rows.length} 個詞 → data/work/wiki-unigram.tsv`);
