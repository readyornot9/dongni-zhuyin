# 用神經網路模型複審候選句，算準確率
# 用法：.venv\Scripts\python tools/nn/rerank.py   （先跑 node tools/nbest.js 10）
# 最後分數＝統計引擎分數＋權重 × 模型的整句 log 機率，掃不同權重
import json
import os
import sys

import torch

sys.path.insert(0, os.path.dirname(__file__))
from model import CharLM

ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
ck = torch.load(os.path.join(ROOT, 'data', 'nn', os.environ.get('NN_MODEL', 'model.pt')))
vocab = ck['vocab']
idx = {c: i for i, c in enumerate(vocab)}
model = CharLM(len(vocab), **ck.get('config', {'ctx': ck['ctx']}))
model.load_state_dict(ck['model'])
model.eval()

data = json.load(open(os.path.join(ROOT, 'data', 'work', 'nbest.json'), encoding='utf-8'))
for item in data:
    lps = model.sentence_logprob([[idx.get(c, 1) for c in cand['text']] for cand in item['cands']])
    for cand, lp in zip(item['cands'], lps):
        cand['nn'] = lp

for w in [0, 0.25, 0.5, 0.75, 1, 1.5, 2, 3, 5, 1000]:
    res = {'tests': [0, 0], 'eval': [0, 0]}
    for item in data:
        best = max(item['cands'], key=lambda c: c['score'] + w * c['nn'])
        r = res[item['set']]
        r[1] += 1
        r[0] += best['text'] == item['expect']
    label = '只用神經網路' if w == 1000 else f'權重 {w}'
    print(f'{label:<10} 測試題 {res["tests"][0]}/{res["tests"][1]}  評估句子 {res["eval"][0]}/{res["eval"][1]}')
