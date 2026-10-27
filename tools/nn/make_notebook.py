# 產生 Google Colab 訓練筆記本 data/nn/colab/shunda_train.ipynb
# 模型程式碼直接讀 tools/nn/model.py，確保雲端訓練出來的模型在本機能直接載入
import json
import os

HERE = os.path.dirname(__file__)
ROOT = os.path.join(HERE, '..', '..')
model_src = open(os.path.join(HERE, 'model.py'), encoding='utf-8').read()


def md(text):
    return {'cell_type': 'markdown', 'metadata': {}, 'source': text.strip('\n').splitlines(keepends=True)}


def code(text):
    return {'cell_type': 'code', 'metadata': {}, 'execution_count': None, 'outputs': [], 'source': text.strip('\n').splitlines(keepends=True)}


cells = [
    md('''
# 順打注音：神經網路模型訓練

照順序執行每一格（點左邊的 ▶，或選取後按 Shift＋Enter）。

**開始前**
1. 上方選單「執行階段 → 變更執行階段類型」→ 選 **T4 GPU** → 儲存。
2. 把 `shunda-data.zip` 上傳到 Google 雲端硬碟的 **我的雲端硬碟 / shunda /** 資料夾（沒有就新建一個叫 `shunda` 的資料夾）。

訓練過程每 5 分鐘會把模型存到雲端硬碟的 `shunda/model.pt`，中途斷線也不會白跑。
'''),
    md('## 1. 確認有 GPU'),
    code('''
!nvidia-smi --query-gpu=name,memory.total --format=csv
import torch
assert torch.cuda.is_available(), '沒有 GPU：請到「執行階段 → 變更執行階段類型」選 T4 GPU，再重新執行'
print('GPU 可以用：', torch.cuda.get_device_name(0))
'''),
    md('## 2. 連接雲端硬碟、解壓縮資料\n會跳出授權視窗，請允許 Colab 存取你的 Google 雲端硬碟。'),
    code('''
from google.colab import drive
drive.mount('/content/drive')
import os, zipfile
SRC = '/content/drive/MyDrive/shunda/shunda-data.zip'
assert os.path.exists(SRC), f'找不到 {SRC}，請確認已上傳到「我的雲端硬碟 / shunda /」'
os.makedirs('/content/data', exist_ok=True)
zipfile.ZipFile(SRC).extractall('/content/data')
print(os.listdir('/content/data'))
'''),
    md('## 3. 模型程式碼\n（和本機 `tools/nn/model.py` 完全相同，不用修改）'),
    code(model_src),
    md('''
## 4. 訓練
- `MINUTES`：訓練幾分鐘。預設 60 分鐘。Colab 免費版連續使用有時間限制，建議不要超過 90。
- `SIZE`：`small` 約 500 萬參數（約 5 MB，速度快，建議先用這個）；`medium` 約 1,300 萬參數（更準，但檔案和運算大約是 small 的 2.5 倍）。
'''),
    code('''
MINUTES = 60
SIZE = 'small'

import math, time, numpy as np, torch
CONFIGS = {'small': dict(d=256, layers=4, heads=4, ff=1024), 'medium': dict(d=384, layers=6, heads=6, ff=1536)}
CTX, BATCH, LR = 64, 256, 2e-3
torch.manual_seed(0)
dev = 'cuda'
vocab = open('/content/data/vocab.txt', encoding='utf-8').read().split('\\n')
train = torch.from_numpy(np.fromfile('/content/data/train.bin', dtype=np.uint16).astype(np.int64)).to(dev)
valid = torch.from_numpy(np.fromfile('/content/data/valid.bin', dtype=np.uint16).astype(np.int64)).to(dev)
config = dict(CONFIGS[SIZE], ctx=CTX)
model = CharLM(len(vocab), **config).to(dev)
print(f'模型 {SIZE}：{sum(p.numel() for p in model.parameters()) / 1e6:.1f}M 參數，訓練資料 {len(train) / 1e6:.0f}M 字')

opt = torch.optim.AdamW(model.parameters(), lr=LR, weight_decay=0.01)
scaler = torch.amp.GradScaler()
offsets = torch.arange(CTX + 1, device=dev)

def batch(data):
    i = torch.randint(0, len(data) - CTX - 1, (BATCH,), device=dev)
    seq = data[i[:, None] + offsets]
    return seq[:, :-1], seq[:, 1:]

@torch.no_grad()
def evaluate():
    model.eval()
    with torch.autocast('cuda', dtype=torch.float16):
        v = sum(model.loss(*batch(valid)).item() for _ in range(50)) / 50
    model.train()
    return v

def save():
    torch.save({'model': {k: v.float().cpu() for k, v in model.state_dict().items()}, 'vocab': vocab, 'ctx': CTX, 'config': config},
               '/content/drive/MyDrive/shunda/model.pt')

start = time.time(); deadline = start + MINUTES * 60; last = start; step = 0; seen = 0
while time.time() < deadline:
    frac = (time.time() - start) / (deadline - start)
    lr = LR * min(1, (step + 1) / 500) * (0.05 + 0.95 * 0.5 * (1 + math.cos(math.pi * frac)))
    for g in opt.param_groups: g['lr'] = lr
    x, y = batch(train)
    with torch.autocast('cuda', dtype=torch.float16):
        loss = model.loss(x, y)
    opt.zero_grad(set_to_none=True)
    scaler.scale(loss).backward()
    scaler.unscale_(opt)
    torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
    scaler.step(opt); scaler.update()
    step += 1; seen += BATCH * CTX
    if step % 1000 == 0:
        el = time.time() - start
        print(f'第 {step} 步｜loss {loss.item():.3f}｜已看 {seen / 1e6:.0f}M 字（{seen / el / 1000:.0f}K 字/秒）｜{el / 60:.1f} 分鐘')
    if time.time() - last > 300:
        v = evaluate(); save(); last = time.time()
        print(f'  已存到雲端硬碟｜驗證困惑度 {math.exp(v):.1f}')

v = evaluate(); save()
print(f'完成！看了 {seen / 1e6:.0f}M 字（訓練資料的 {seen / len(train):.1f} 倍），驗證困惑度 {math.exp(v):.1f}')
print('模型已存到：我的雲端硬碟 / shunda / model.pt')
'''),
    md('''
## 5. 完成
訓練好的模型在 **我的雲端硬碟 / shunda / model.pt**。

把它下載到電腦，放到 `C:\\Users\\user\\type\\data\\nn\\model.pt`（取代舊的），然後跟 Claude 說「模型放好了」。

（也可以執行下面這格，直接從瀏覽器下載。）
'''),
    code('''
from google.colab import files
files.download('/content/drive/MyDrive/shunda/model.pt')
'''),
]

nb = {
    'cells': cells,
    'metadata': {'accelerator': 'GPU', 'colab': {'provenance': [], 'gpuType': 'T4'},
                 'kernelspec': {'name': 'python3', 'display_name': 'Python 3'}, 'language_info': {'name': 'python'}},
    'nbformat': 4, 'nbformat_minor': 0,
}
out = os.path.join(ROOT, 'data', 'nn', 'colab', 'shunda_train.ipynb')
with open(out, 'w', encoding='utf-8') as f:
    json.dump(nb, f, ensure_ascii=False, indent=1)
print('完成 →', out)
