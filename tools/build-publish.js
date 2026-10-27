// 產生網路發布版（dist/index.html）：去掉 html/head/body 外殼、把 CSS 內嵌
// 用法：node tools/build-publish.js
// JS 檔（dict.js、engine.js、testcases.js、app.js）以附加檔案一起發布，路徑不變
'use strict';
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'web', 'index.html'), 'utf8');
const css = fs.readFileSync(path.join(root, 'web', 'style.css'), 'utf8');

const title = /<title>[\s\S]*?<\/title>/.exec(html)[0];
const body = /<body>([\s\S]*)<\/body>/.exec(html)[1].trim();

const out = [
  title,
  '<meta name="claude:viewport-fit-sides" content="cover">',
  '<style>',
  css.trim(),
  '</style>',
  body,
  '',
].join('\n');

fs.mkdirSync(path.join(root, 'dist'), { recursive: true });
fs.writeFileSync(path.join(root, 'dist', 'index.html'), out);
console.log('完成 → dist/index.html');
