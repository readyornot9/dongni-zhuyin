# 懂你注音

會學習的 Windows 注音輸入法，打越多越懂你。跟微軟注音一樣的打法，選字更準；你打的字只在你的電腦上處理，不會上傳。

- 下載與線上試用：https://readyornot9.github.io/zhuyin-typing/
- 程式碼簽章政策：https://readyornot9.github.io/zhuyin-typing/code-signing.html

## 架構

| 位置 | 內容 |
|---|---|
| `web/engine.js` | 選字引擎：詞庫、前後文統計、Viterbi 選字、用字規則、使用者學習、按鍵組字（不依賴網頁，Windows 版共用） |
| `web/nn.js`、`web/nnlm.wasm` | AI 複審：字元 Transformer 小模型，WebAssembly SIMD 推論 |
| `web/index.html`、`web/app.js` | 網頁測試台 |
| `windows/tip/` | Windows 輸入法外殼（C++，Text Services Framework） |
| `windows/server/` | 本機引擎服務（Node.js），外殼透過 named pipe 跟它溝通 |
| `tools/` | 詞庫／統計產生、評估、MSI 打包 |
| `site/` | 打字練習網站 |

## 建置

需要 Visual Studio 2022 Build Tools（C++）和 Node.js 24。

```bat
npm test
windows\build.cmd
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-msi.ps1
```

產出 `dist\DongNiZhuyin.msi`。正式發布的安裝檔由 GitHub Actions（`.github/workflows/build.yml`）從這個儲存庫自動建置。

## 授權

程式碼：MIT License（見 `LICENSE`）。隨附的詞庫、統計資料與模型各有授權，見 `NOTICE.md`。
