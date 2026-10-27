# 用 Python（壓縮後還原的權重）算幾句的 log 機率，給 tools/nn/check-js.js 比對
import json
import os
import sys

import torch

sys.path.insert(0, os.path.dirname(__file__))
from model import CharLM

ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
ck = torch.load(os.path.join(ROOT, 'data', 'nn', 'model-int8.pt'))
idx = {c: i for i, c in enumerate(ck['vocab'])}
m = CharLM(len(ck['vocab']), **ck['config'])
m.load_state_dict(ck['model'])
m.eval()
S = ['他現在不在公司', '他現在不再公司', '我想吃魟魚', '記得帶傘']
print(json.dumps(dict(zip(S, m.sentence_logprob([[idx.get(c, 1) for c in s] for s in S])))))
