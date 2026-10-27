# 授權與來源

程式碼以 MIT License 釋出（見 LICENSE）。懂你注音另外使用了下列資料與軟體，依各授權條款標示：

| 檔案 | 內容 | 來源與授權 |
|---|---|---|
| `web/dict.js` | 詞庫 | [小麥注音輸入法（McBopomofo）](https://github.com/openvanilla/McBopomofo) 詞庫，MIT License |
| `web/lm.js` | 前後文統計（詞語相鄰次數） | 由中文維基百科繁體文章統計產生，原始內容 [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/deed.zh_TW)；本檔同樣依 CC BY-SA 4.0 釋出 |
| `web/lm.js`、`web/dict.js` 的口語詞頻 | 口語統計 | [Tatoeba](https://tatoeba.org/) 例句（CC BY 2.0 FR）、[Mozilla Common Voice](https://commonvoice.mozilla.org/) 台灣華語句子（CC0） |
| `web/nnlm.wasm` | AI 複審模型權重（約 1340 萬參數，自行訓練） | 訓練資料為上述來源，加上 [FineWeb-2](https://huggingface.co/datasets/HuggingFaceFW/fineweb-2) 繁體中文網頁（ODC-By 1.0） |
| 安裝檔裡的 `node\node.exe` | 執行環境 | [Node.js](https://nodejs.org/)，MIT License 等（由 OpenJS Foundation 簽章，非本專案建置） |

原始語料與訓練過程的中間檔不放在這個儲存庫；重新產生的方法見 `tools/` 裡各程式開頭的說明。
