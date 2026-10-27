# 第二階段（Kaggle GPU）：拿現有的中型模型繼續訓練，加重口語
# 輸入（Kaggle 的 Add Input）：
#   1. 上一次訓練的輸出（model.pt）
#   2. 第一階段的輸出（colloq-*.txt、general-*.txt，kaggle_data.py 產生）
# 語料比例：口語網頁（口語比例 ≥ COL_MIN）為主，加一般網頁和 Tatoeba／Common Voice 口語句子，避免忘掉書面語
# 字表沿用舊模型（網頁版的模型格式不用改）
import bz2
import glob
import math
import os
import re
import time
import urllib.request

import numpy as np
import torch

MINUTES = globals().get('MINUTES', 90)
COL_MIN = globals().get('COL_MIN', 0.05)
COL_CHARS = globals().get('COL_CHARS', 250_000_000)
GEN_CHARS = globals().get('GEN_CHARS', 80_000_000)
COL_REPEAT = 10
LR = globals().get('LR', 5e-4)
# 反方向模型：每行字倒過來訓練（從右往左讀），從正向模型的權重開始（字的意思可以沿用）
REVERSE = globals().get('REVERSE', False)
EXAM = {e for e in globals().get('EXAM', []) if len(e) >= 5}
EXAM_HEAD = {}  # 測試句的前 5 個字 → 測試句（逐位置查表，比每句都掃全部測試句快）
for e in EXAM:
    EXAM_HEAD.setdefault(e[:5], []).append(e)


def has_exam(s):
    for i in range(len(s) - 4):
        for e in EXAM_HEAD.get(s[i:i + 5], ()):
            if s.startswith(e, i):
                return True
    return False
OUT = globals().get('OUT_DIR', '/kaggle/working')
TMP = globals().get('TMP_DIR', '/tmp')
IN = globals().get('IN_DIR', '/kaggle/input')
T0 = time.time()


def log(msg):
    print(f'[{(time.time() - T0) / 60:5.1f} 分] {msg}', flush=True)


def find(pattern):
    got = sorted(glob.glob(f'{IN}/**/{pattern}', recursive=True))
    if not got:
        raise SystemExit(f'找不到 {pattern}，請確認有用 Add Input 加入')
    return got


ck = torch.load(find('model.pt')[0], map_location='cpu', weights_only=False)
vocab = ck['vocab']
idx = {c: i for i, c in enumerate(vocab)}
log(f'舊模型：{ck["config"]}，字表 {len(vocab)} 字，舊資料 {ck.get("data")}')

HAN = re.compile(r'[一-鿿]')
RUNS = re.compile(r'[一-鿿]{2,}')
SENT = re.compile(r'[。！？；\n]')
big5 = {}


def is_trad(s):
    for c in s:
        r = big5.get(c)
        if r is None:
            try:
                c.encode('big5')
                r = True
            except UnicodeEncodeError:
                r = False
            big5[c] = r
        if not r:
            return False
    return True


stats = {}
corpus = open(f'{TMP}/corpus.txt', 'w', encoding='utf-8')
valid = {k: open(f'{TMP}/valid-{k}.txt', 'w', encoding='utf-8') for k in ('口語網頁', '一般網頁', '口語')}
n_lines = 0
seen_sent = set()  # 去掉重複句（網站樣板、轉貼）


def emit(src, text, min_hans=4, repeat=1):
    global n_lines
    for s in SENT.split(text):
        hans = ''.join(HAN.findall(s))
        if len(hans) < min_hans or not is_trad(hans) or has_exam(hans):
            continue
        if repeat == 1:
            h = hash(hans)
            if h in seen_sent:
                continue
            seen_sent.add(h)
        for r in RUNS.findall(s):
            n_lines += 1
            if REVERSE:
                r = r[::-1]
            if n_lines % 200 == 0:
                valid[src].write(r + '\n')
                continue
            for _ in range(repeat):
                corpus.write(r + '\n')
            stats[src] = stats.get(src, 0) + len(r) * repeat


def unflat(s):
    return s.replace('\\n', '\n').replace('\\\\', '\\')


# ── 1. 口語網頁（第一階段挑出來的） ──
docs = 0
for p in find('colloq-*.txt'):
    for line in open(p, encoding='utf-8'):
        d, _, text = line.rstrip('\n').partition('\t')
        if float(d) < COL_MIN:
            continue
        docs += 1
        emit('口語網頁', unflat(text))
        if stats.get('口語網頁', 0) >= COL_CHARS:
            break
    if stats.get('口語網頁', 0) >= COL_CHARS:
        break
log(f'口語網頁：{docs} 篇，{stats.get("口語網頁", 0) / 1e6:.0f}M 字（口語比例 ≥ {COL_MIN}）')

# ── 2. 一般網頁 ──
for p in find('general-*.txt'):
    for line in open(p, encoding='utf-8'):
        emit('一般網頁', unflat(line.rstrip('\n')))
        if stats.get('一般網頁', 0) >= GEN_CHARS:
            break
    if stats.get('一般網頁', 0) >= GEN_CHARS:
        break
log(f'一般網頁：{stats.get("一般網頁", 0) / 1e6:.0f}M 字')

# ── 3. 口語句子（Tatoeba CC BY 2.0 FR、Common Voice CC0），重複 10 次 ──
os.makedirs(f'{TMP}/raw', exist_ok=True)
SKIP_DOWNLOAD = globals().get('SKIP_DOWNLOAD', False)  # 本機測試不下載
if not SKIP_DOWNLOAD:
    urllib.request.urlretrieve('https://downloads.tatoeba.org/exports/per_language/cmn/cmn_sentences.tsv.bz2', f'{TMP}/raw/tatoeba.tsv.bz2')
CV = 'https://raw.githubusercontent.com/common-voice/common-voice/main/server/data/zh-TW/'
for f in ['sentence-collector', 'setences', 'chatlogs', 'lms', 'taipei_city_gov']:
    if not SKIP_DOWNLOAD:
        urllib.request.urlretrieve(CV + f + '.txt', f'{TMP}/raw/cv-{f}.txt')
if os.path.exists(f'{TMP}/raw/tatoeba.tsv.bz2'):
    for l in bz2.open(f'{TMP}/raw/tatoeba.tsv.bz2', 'rt', encoding='utf-8'):
        if l.count('\t') >= 2:
            emit('口語', l.split('\t')[2], 2, COL_REPEAT)
for p in glob.glob(f'{TMP}/raw/cv-*.txt'):
    for l in open(p, encoding='utf-8'):
        emit('口語', l, 2, COL_REPEAT)
corpus.close()
for v in valid.values():
    v.close()
log('語料完成：' + '、'.join(f'{k} {v / 1e6:.0f}M 字' for k, v in stats.items()))


def encode(path):
    total = sum(len(l) for l in open(path, encoding='utf-8')) + 1
    arr = np.zeros(total, dtype=np.int16)
    p = 1
    for l in open(path, encoding='utf-8'):
        l = l.rstrip('\n')
        arr[p:p + len(l)] = [idx.get(ch, 1) for ch in l]
        p += len(l) + 1
    return arr[:p]


dev = globals().get('DEV', 'cuda')  # 本機測試用 cpu
import contextlib
amp = (lambda: torch.autocast('cuda', dtype=torch.float16)) if dev == 'cuda' else contextlib.nullcontext
train = torch.from_numpy(encode(f'{TMP}/corpus.txt')).to(dev)
valids = {k: torch.from_numpy(encode(f'{TMP}/valid-{k}.txt')).to(dev) for k in valid}
log(f'訓練資料 {len(train) / 1e6:.0f}M 字')

# ── 4. 接著訓練（CharLM 由 model.py 合併進來） ──
cfg = ck['config']
CTX, BATCH = cfg['ctx'], 256
model = CharLM(len(vocab), **{k: v for k, v in cfg.items() if k != 'ctx'}, ctx=CTX).to(dev)
model.load_state_dict({k: v for k, v in ck['model'].items()})
opt = torch.optim.AdamW(model.parameters(), lr=LR, weight_decay=0.01)
scaler = torch.amp.GradScaler(enabled=dev == 'cuda')
offsets = torch.arange(CTX + 1, device=dev)
torch.manual_seed(1)


def batch(data):
    i = torch.randint(0, len(data) - CTX - 1, (BATCH,), device=dev)
    seq = data[i[:, None] + offsets].long()
    return seq[:, :-1], seq[:, 1:]


@torch.no_grad()
def evaluate():
    model.eval()
    out = {}
    with amp():
        for k, v in valids.items():
            if len(v) > CTX + 2:
                out[k] = math.exp(sum(model.loss(*batch(v)).item() for _ in range(30)) / 30)
    model.train()
    return '、'.join(f'{k} {p:.1f}' for k, p in out.items())


def save():
    data = dict(ck.get('data') or {})
    data.update({'續訓' + k: int(v) for k, v in stats.items()})
    torch.save({'model': {k: v.float().cpu() for k, v in model.state_dict().items()}, 'vocab': vocab, 'ctx': CTX,
                'config': cfg, 'data': data, 'reverse': REVERSE}, f'{OUT}/model.pt')


log(f'續訓前困惑度：{evaluate()}')
log(f'開始續訓 {MINUTES} 分鐘，學習率 {LR}')
start = time.time(); deadline = start + MINUTES * 60; last = start; step = 0; seen = 0
while time.time() < deadline:
    frac = (time.time() - start) / (deadline - start)
    lr = LR * min(1, (step + 1) / 300) * (0.05 + 0.95 * 0.5 * (1 + math.cos(math.pi * frac)))
    for g in opt.param_groups:
        g['lr'] = lr
    x, y = batch(train)
    with amp():
        loss = model.loss(x, y)
    opt.zero_grad(set_to_none=True)
    scaler.scale(loss).backward()
    scaler.unscale_(opt)
    torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
    scaler.step(opt)
    scaler.update()
    step += 1
    seen += BATCH * CTX
    if time.time() - last > 300:
        save(); last = time.time()
        log(f'續訓中：已看 {seen / 1e6:.0f}M 字，困惑度 {evaluate()}（已存檔）')
save()
log(f'完成！看了 {seen / 1e6:.0f}M 字，困惑度 {evaluate()}，模型在 {OUT}/model.pt')
