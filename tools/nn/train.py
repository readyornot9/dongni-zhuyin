# 訓練小型字元語言模型
# 用法：.venv\Scripts\python tools/nn/train.py [訓練幾分鐘=120]
# 輸出 data/nn/model.pt（每 10 分鐘存一次，中途停掉也能用）
import math
import os
import sys
import time

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(__file__))
from model import CharLM

ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
NN = os.path.join(ROOT, 'data', 'nn')
MINUTES = float(sys.argv[1]) if len(sys.argv) > 1 else 120
CTX, BATCH, LR = 64, 64, 3e-3
torch.manual_seed(0)
torch.set_num_threads(os.cpu_count())

vocab = open(os.path.join(NN, 'vocab.txt'), encoding='utf-8').read().split('\n')
train = np.fromfile(os.path.join(NN, 'train.bin'), dtype=np.uint16).astype(np.int64)
valid = np.fromfile(os.path.join(NN, 'valid.bin'), dtype=np.uint16).astype(np.int64)
model = CharLM(len(vocab), ctx=CTX)
print(f'參數 {sum(p.numel() for p in model.parameters()) / 1e6:.1f}M，訓練資料 {len(train) / 1e6:.1f}M 字，{os.cpu_count()} 執行緒', flush=True)

opt = torch.optim.AdamW(model.parameters(), lr=LR, weight_decay=0.01)
deadline = time.time() + MINUTES * 60
start = time.time()


def batch(data):
    i = np.random.randint(0, len(data) - CTX - 1, BATCH)
    x = torch.from_numpy(np.stack([data[j:j + CTX] for j in i]))
    y = torch.from_numpy(np.stack([data[j + 1:j + CTX + 1] for j in i]))
    return x, y


@torch.no_grad()
def evaluate():
    model.eval()
    losses = [model.loss(*batch(valid)).item() for _ in range(20)]
    model.train()
    return sum(losses) / len(losses)


step, last_save, seen = 0, time.time(), 0
while time.time() < deadline:
    # 學習率：前 300 步暖身，之後依剩餘時間餘弦下降
    frac = (time.time() - start) / (deadline - start)
    lr = LR * min(1, (step + 1) / 300) * (0.1 + 0.9 * 0.5 * (1 + math.cos(math.pi * frac)))
    for g in opt.param_groups:
        g['lr'] = lr
    x, y = batch(train)
    loss = model.loss(x, y)
    opt.zero_grad()
    loss.backward()
    torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
    opt.step()
    step += 1
    seen += BATCH * CTX
    if step % 200 == 0:
        el = time.time() - start
        print(f'第 {step} 步｜訓練 loss {loss.item():.3f}｜已看 {seen / 1e6:.1f}M 字（{seen / el:.0f} 字/秒）｜{el / 60:.1f} 分鐘', flush=True)
    if time.time() - last_save > 600 or time.time() >= deadline:
        v = evaluate()
        torch.save({'model': model.state_dict(), 'vocab': vocab, 'ctx': CTX}, os.path.join(NN, 'model.pt'))
        print(f'  已存檔｜驗證 loss {v:.3f}（困惑度 {math.exp(v):.1f}）', flush=True)
        last_save = time.time()

v = evaluate()
torch.save({'model': model.state_dict(), 'vocab': vocab, 'ctx': CTX}, os.path.join(NN, 'model.pt'))
print(f'完成：{step} 步、看了 {seen / 1e6:.1f}M 字，驗證 loss {v:.3f}（困惑度 {math.exp(v):.1f}）', flush=True)
