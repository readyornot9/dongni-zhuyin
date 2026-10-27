# 準備神經網路訓練資料：字表＋編碼後的訓練／驗證資料
# 用法：.venv\Scripts\python tools/nn/prepare.py [維基抽多少字=40000000] [口語重複幾次=10]
# 輸出 data/nn/vocab.txt、train.bin、valid.bin（uint16，0 = 句首/句尾分隔）
import os
import random
import sys
from collections import Counter

import numpy as np

ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
WORK = os.path.join(ROOT, 'data', 'work')
OUT = os.path.join(ROOT, 'data', 'nn')
WIKI_CHARS = int(sys.argv[1]) if len(sys.argv) > 1 else 40_000_000
COL_REPEAT = int(sys.argv[2]) if len(sys.argv) > 2 else 10
VOCAB = 7000
random.seed(42)
os.makedirs(OUT, exist_ok=True)

# 維基：檔案很大，用固定機率抽樣到需要的字數
wiki_path = os.path.join(WORK, 'wiki-trad.txt')
total_size = os.path.getsize(wiki_path)
keep_ratio = min(1.0, WIKI_CHARS * 3 * 1.1 / total_size)  # UTF-8 中文約 3 bytes/字
wiki, n = [], 0
with open(wiki_path, encoding='utf-8') as f:
    for line in f:
        if random.random() < keep_ratio:
            line = line.strip()
            if line:
                wiki.append(line)
                n += len(line)
                if n >= WIKI_CHARS:
                    break
col = [l.strip() for l in open(os.path.join(WORK, 'colloquial.txt'), encoding='utf-8') if l.strip()]
print(f'維基 {len(wiki)} 段 {n} 字；口語 {len(col)} 段 × {COL_REPEAT}')

counts = Counter()
for l in wiki:
    counts.update(l)
for l in col:
    for ch in l:
        counts[ch] += COL_REPEAT
# 0 = 分隔、1 = 罕用字
vocab = ['<s>', '<unk>'] + [c for c, _ in counts.most_common(VOCAB - 2)]
with open(os.path.join(OUT, 'vocab.txt'), 'w', encoding='utf-8') as f:
    f.write('\n'.join(vocab))
idx = {c: i for i, c in enumerate(vocab)}
covered = sum(counts[c] for c in vocab[2:]) / sum(counts.values())
print(f'字表 {len(vocab)} 字，涵蓋 {covered:.2%} 的字')

lines = wiki + col * COL_REPEAT
random.shuffle(lines)
split = int(len(lines) * 0.995)

def encode(ls, path):
    arr = np.empty(sum(len(l) + 1 for l in ls) + 1, dtype=np.uint16)
    arr[0] = 0
    p = 1
    for l in ls:
        for ch in l:
            arr[p] = idx.get(ch, 1)
            p += 1
        arr[p] = 0
        p += 1
    arr.tofile(path)
    return len(arr)

print('訓練', encode(lines[:split], os.path.join(OUT, 'train.bin')), '個字；驗證', encode(lines[split:], os.path.join(OUT, 'valid.bin')), '個字')
