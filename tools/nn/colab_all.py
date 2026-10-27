# 在 Google Colab 或 Kaggle 一次做完：下載公開語料 → 抽繁體句子 → 建字表 → 訓練 → 存 {OUT}/model.pt
# 不需要上傳任何檔案。模型程式碼來自 model.py（由 make_colab_cell.py 合併進來）
# 語料：維基百科（CC BY-SA 4.0）、FineWeb-2 中文網頁（ODC-By 1.0）、Tatoeba（CC BY 2.0 FR）、Common Voice（CC0）
# 資料量大，句子先寫到暫存檔，不全部放記憶體
import bz2
import glob
import math
import os
import re
import time
import urllib.request
import xml.etree.ElementTree as ET
from collections import Counter

import numpy as np
import torch

MINUTES = globals().get('MINUTES', 100)
SIZE = globals().get('SIZE', 'medium')
WIKI_CHARS = globals().get('WIKI_CHARS', 150_000_000)
WEB_CHARS = globals().get('WEB_CHARS', 300_000_000)
COL_REPEAT = 10
VOCAB = 7000
EXAM = {e for e in globals().get('EXAM', []) if len(e) >= 5}  # 評估題庫：語料裡含有這些句子就剔除，避免考題外洩
OUT = globals().get('OUT_DIR', '/content')  # 模型存哪裡（Kaggle：/kaggle/working，結束後會保留）
TMP = globals().get('TMP_DIR', '/content')  # 暫存語料（Kaggle 放 /tmp，不要佔輸出空間）
T0 = time.time()


def log(msg):
    print(f'[{(time.time() - T0) / 60:5.1f} 分] {msg}', flush=True)


def is_trad(s):
    # 只保留 Big5 字集裡的字（擋掉含簡體字、港式粵語用字的句子）
    try:
        s.encode('big5')
        return True
    except UnicodeEncodeError:
        return False


HAN = re.compile(r'[一-鿿]')
RUNS = re.compile(r'[一-鿿]{2,}')
SENT = re.compile(r'[。！？；\n]')

counts = Counter()
stats = {}
corpus = open(f'{TMP}/corpus.txt', 'w', encoding='utf-8')
valid = open(f'{TMP}/valid.txt', 'w', encoding='utf-8')
n_lines = 0


def emit(src, text, min_hans=4, repeat=1):
    # 把一段文字切成句子，繁體的寫進語料；回傳寫了幾個字
    global n_lines
    got = 0
    for s in SENT.split(text):
        hans = ''.join(HAN.findall(s))
        if len(hans) < min_hans or not is_trad(hans) or any(e in hans for e in EXAM):
            continue
        for r in RUNS.findall(s):
            n_lines += 1
            if n_lines % 200 == 0:  # 每 200 段留 1 段當驗證資料
                valid.write(r + '\n')
                continue
            for _ in range(repeat):
                corpus.write(r + '\n')
            counts.update(r * repeat if repeat > 1 else r)
            got += len(r)
    stats[src] = stats.get(src, 0) + got
    return got


# ── 1. 口語語料 ──
os.makedirs(f'{TMP}/raw', exist_ok=True)
urllib.request.urlretrieve('https://downloads.tatoeba.org/exports/per_language/cmn/cmn_sentences.tsv.bz2', f'{TMP}/raw/tatoeba.tsv.bz2')
CV = 'https://raw.githubusercontent.com/common-voice/common-voice/main/server/data/zh-TW/'
for f in ['sentence-collector', 'setences', 'chatlogs', 'lms', 'taipei_city_gov']:
    urllib.request.urlretrieve(CV + f + '.txt', f'{TMP}/raw/cv-{f}.txt')
for l in bz2.open(f'{TMP}/raw/tatoeba.tsv.bz2', 'rt', encoding='utf-8'):
    if l.count('\t') >= 2:
        emit('口語', l.split('\t')[2], 2, COL_REPEAT)
for p in glob.glob(f'{TMP}/raw/cv-*.txt'):
    for l in open(p, encoding='utf-8'):
        emit('口語', l, 2, COL_REPEAT)
log(f'口語語料：{stats["口語"] / 1e6:.1f}M 字（已重複 {COL_REPEAT} 次）')

# ── 2. FineWeb-2 中文網頁（貼近日常用語） ──
from datasets import load_dataset
ds = load_dataset('HuggingFaceFW/fineweb-2', name='cmn_Hani', split='train', streaming=True)
docs = kept_docs = 0
for row in ds:
    docs += 1
    text = row['text']
    hans = HAN.findall(text)
    if len(hans) < 50:
        continue
    # 整篇大部分是繁體才用（簡體網頁裡偶爾全是共用字的句子也一起排除）
    bad = sum(1 for c in hans if not is_trad(c))
    if bad > len(hans) * 0.002:
        continue
    kept_docs += 1
    emit('網頁', text)
    if docs % 200000 == 0:
        log(f'FineWeb-2：看了 {docs} 篇、用了 {kept_docs} 篇繁體，{stats.get("網頁", 0) / 1e6:.0f}M 字')
    if stats.get('網頁', 0) >= WEB_CHARS:
        break
log(f'FineWeb-2 完成：看了 {docs} 篇、用了 {kept_docs} 篇，{stats["網頁"] / 1e6:.0f}M 字')

# ── 3. 維基百科（邊下載邊讀，夠了就停） ──
CLEAN = [
    (re.compile(r'<!--.*?-->', re.S), ''), (re.compile(r'<ref[^>/]*/>'), ''), (re.compile(r'<ref[^>]*>.*?</ref>', re.S), ''),
    (re.compile(r'<(math|code|pre|syntaxhighlight|gallery|timeline|score)[^>]*>.*?</\1>', re.S), ''), (re.compile(r'<[^>]+>'), ''),
    (re.compile(r'-\{[^{}]*?:[^{}]*?\}-'), ''), (re.compile(r'-\{([^{}]*?)\}-'), r'\1'),
    (re.compile(r'\[\[(?:File|Image|檔案|文件|图像|圖像|Category|分類|分类):[^\]]*\]\]', re.I), ''),
    (re.compile(r'\[\[[^\]|]*\|([^\]]*)\]\]'), r'\1'), (re.compile(r'\[\[([^\]]*)\]\]'), r'\1'),
    (re.compile(r'\[https?://[^\s\]]+\s*([^\]]*)\]'), r'\1'), (re.compile(r"'{2,}"), ''),
    (re.compile(r'^[|!{].*$', re.M), ''), (re.compile(r'^=+.*?=+\s*$', re.M), ''),
]


def strip_templates(t):
    out, depth, i, n = [], 0, 0, len(t)
    while i < n:
        if t.startswith('{{', i):
            depth += 1; i += 2; continue
        if depth and t.startswith('}}', i):
            depth -= 1; i += 2; continue
        if not depth:
            out.append(t[i])
        i += 1
    return ''.join(out)


pages = 0
req = urllib.request.Request('https://dumps.wikimedia.org/zhwiki/latest/zhwiki-latest-pages-articles.xml.bz2', headers={'User-Agent': 'shunda-ime-research'})
with urllib.request.urlopen(req) as resp:
    for _, el in ET.iterparse(bz2.BZ2File(resp), events=('end',)):
        if el.tag.rsplit('}', 1)[-1] != 'page':
            continue
        ns, text, redirect = el.find('{*}ns'), el.find('{*}revision/{*}text'), el.find('{*}redirect')
        if ns is not None and ns.text == '0' and redirect is None and text is not None and text.text:
            pages += 1
            t = strip_templates(text.text)
            for pat, rep in CLEAN:
                t = pat.sub(rep, t)
            emit('維基', t)
            if pages % 50000 == 0:
                log(f'維基百科：{pages} 篇，{stats["維基"] / 1e6:.0f}M 字')
        el.clear()
        if stats.get('維基', 0) >= WIKI_CHARS:
            break
corpus.close()
valid.close()
log(f'語料完成：' + '、'.join(f'{k} {v / 1e6:.0f}M 字' for k, v in stats.items()))

# ── 4. 字表、編碼（int16 省記憶體） ──
vocab = ['<s>', '<unk>'] + [c for c, _ in counts.most_common(VOCAB - 2)]
idx = {c: i for i, c in enumerate(vocab)}


def encode(path):
    total = sum(len(l) for l in open(path, encoding='utf-8')) + 1  # 每行的換行字元剛好當句尾
    arr = np.zeros(total, dtype=np.int16)
    p = 1
    for l in open(path, encoding='utf-8'):
        l = l.rstrip('\n')
        arr[p:p + len(l)] = [idx.get(ch, 1) for ch in l]
        p += len(l) + 1  # 句尾留 0
    return arr[:p]


dev = 'cuda'
train = torch.from_numpy(encode(f'{TMP}/corpus.txt')).to(dev)
valid = torch.from_numpy(encode(f'{TMP}/valid.txt')).to(dev)
log(f'訓練資料 {len(train) / 1e6:.0f}M 字、驗證 {len(valid) / 1e6:.1f}M 字，字表 {len(vocab)} 字')

# ── 5. 訓練（CharLM 由 model.py 合併進來） ──
CONFIGS = {'small': dict(d=256, layers=4, heads=4, ff=1024), 'medium': dict(d=384, layers=6, heads=6, ff=1536)}
CTX, BATCH, LR = 64, 256, 1.5e-3
torch.manual_seed(0)
config = dict(CONFIGS[SIZE], ctx=CTX)
model = CharLM(len(vocab), **config).to(dev)
opt = torch.optim.AdamW(model.parameters(), lr=LR, weight_decay=0.01)
scaler = torch.amp.GradScaler()
offsets = torch.arange(CTX + 1, device=dev)


def batch(data):
    i = torch.randint(0, len(data) - CTX - 1, (BATCH,), device=dev)
    seq = data[i[:, None] + offsets].long()
    return seq[:, :-1], seq[:, 1:]


@torch.no_grad()
def evaluate():
    model.eval()
    with torch.autocast('cuda', dtype=torch.float16):
        v = sum(model.loss(*batch(valid)).item() for _ in range(50)) / 50
    model.train()
    return v


def save():
    torch.save({'model': {k: v.float().cpu() for k, v in model.state_dict().items()}, 'vocab': vocab, 'ctx': CTX, 'config': config,
                'data': {k: int(v) for k, v in stats.items()}}, f'{OUT}/model.pt')


log(f'開始訓練：{SIZE}，{sum(p.numel() for p in model.parameters()) / 1e6:.1f}M 參數，{MINUTES} 分鐘')
start = time.time(); deadline = start + MINUTES * 60; last = start; step = 0; seen = 0
while time.time() < deadline:
    frac = (time.time() - start) / (deadline - start)
    lr = LR * min(1, (step + 1) / 1000) * (0.05 + 0.95 * 0.5 * (1 + math.cos(math.pi * frac)))
    for g in opt.param_groups:
        g['lr'] = lr
    x, y = batch(train)
    with torch.autocast('cuda', dtype=torch.float16):
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
        v = evaluate(); save(); last = time.time()
        log(f'訓練中：已看 {seen / 1e6:.0f}M 字，驗證困惑度 {math.exp(v):.1f}（已存檔）')
v = evaluate()
save()
log(f'完成！看了 {seen / 1e6:.0f}M 字，驗證困惑度 {math.exp(v):.1f}，模型在 {OUT}/model.pt（{os.path.getsize(f"{OUT}/model.pt") / 1e6:.0f} MB）')
