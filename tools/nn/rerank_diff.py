# 比較複審前後：哪些句子被修好、哪些被改壞、哪些還是錯
# 用法：.venv\Scripts\python tools/nn/rerank_diff.py [權重=0.5]
import json
import os
import sys

import torch

sys.path.insert(0, os.path.dirname(__file__))
from model import CharLM

ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
W = float(sys.argv[1]) if len(sys.argv) > 1 else 0.5
ck = torch.load(os.path.join(ROOT, 'data', 'nn', 'model.pt'))
idx = {c: i for i, c in enumerate(ck['vocab'])}
model = CharLM(len(ck['vocab']), **ck['config'])
model.load_state_dict(ck['model'])
model.eval()

fixed, broken, still = [], [], []
for item in json.load(open(os.path.join(ROOT, 'data', 'work', 'nbest.json'), encoding='utf-8')):
    cands = item['cands']
    lps = model.sentence_logprob([[idx.get(c, 1) for c in x['text']] for x in cands])
    before = cands[0]['text']
    after = max(zip(cands, lps), key=lambda p: p[0]['score'] + W * p[1])[0]['text']
    e = item['expect']
    if before != e and after == e:
        fixed.append(f'{before} → {after}')
    elif before == e and after != e:
        broken.append(f'{e} → {after}')
    elif after != e:
        still.append(f'{e} → {after}')
print(f'權重 {W}')
print(f'\n修好 {len(fixed)} 句：\n  ' + '\n  '.join(fixed))
print(f'\n改壞 {len(broken)} 句：\n  ' + '\n  '.join(broken))
print(f'\n還是錯 {len(still)} 句：\n  ' + '\n  '.join(still))
