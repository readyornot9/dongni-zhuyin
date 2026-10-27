# 把 PyTorch 模型壓成 8 位元整數，輸出網頁用的 web/nnlm.wasm
# 用法：.venv\Scripts\python tools/nn/export.py [模型.pt 輸出.wasm]（預設 data/nn/model.pt → web/nnlm.wasm；反方向模型用 data/nn/model-rev.pt web/nnlm-rev.wasm）
# 二維權重每一列各自一個縮放係數（int8），偏移量和 LayerNorm 保持 float32（很小）
import json
import os
import sys

import numpy as np
import torch

ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'data', 'nn', 'model.pt')
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'web', 'nnlm.wasm')
ck = torch.load(SRC, weights_only=False)
sd = ck['model']
cfg = ck['config']


# 二進位格式：'SDNN' + 4 位元組表頭長度 + 表頭 JSON（設定、字表、各權重的位置）+ 權重資料（每段對齊 4 位元組）
chunks, size = [], 0


def put(a):
    global size
    b = np.ascontiguousarray(a).tobytes()
    pad = (-len(b)) % 4
    chunks.append(b + b'\0' * pad)
    at = size
    size += len(b) + pad
    return [at, len(b)]


def quantize(w):
    w = w.numpy().astype(np.float32)
    scale = np.abs(w).max(axis=1) / 127.0
    scale[scale == 0] = 1
    q = np.clip(np.round(w / scale[:, None]), -127, 127).astype(np.int8)
    return q, scale.astype(np.float32)


tensors = {}
for name, t in sd.items():
    if name == 'out.weight':  # 跟 emb.weight 共用
        continue
    if t.dim() == 2:
        q, s = quantize(t)
        tensors[name] = {'shape': list(t.shape), 'q': put(q), 'scale': put(s)}
    else:
        tensors[name] = {'shape': list(t.shape), 'f': put(t.numpy().astype(np.float32))}

meta = {'config': cfg, 'reverse': bool(ck.get('reverse')), 'vocab': ''.join(ck['vocab'][2:]), 'tensors': tensors,
        'source': '中文維基百科（CC BY-SA 4.0）、FineWeb-2（ODC-By 1.0）、Tatoeba（CC BY 2.0 FR）、Mozilla Common Voice（CC0）'}
head = json.dumps(meta, ensure_ascii=False).encode('utf-8')
head += b' ' * ((-len(head)) % 4)
out = OUT
with open(out, 'wb') as f:
    f.write(b'SDNN' + len(head).to_bytes(4, 'little') + head + b''.join(chunks))
print(f'完成：{os.path.getsize(out) / 1e6:.1f} MB → {out}')
if len(sys.argv) > 1:
    sys.exit()

# 驗證：把壓縮後的權重還原，存成另一個 .pt 給 rerank 比較準確率
sd2 = dict(sd)
for name, t in sd.items():
    if t.dim() == 2:
        q, s = quantize(t)
        sd2[name] = torch.from_numpy(q.astype(np.float32) * s[:, None])
sd2['out.weight'] = sd2['emb.weight']
torch.save({**ck, 'model': sd2}, os.path.join(ROOT, 'data', 'nn', 'model-int8.pt'))
print('壓縮後還原的權重 → data/nn/model-int8.pt（驗證用）')
