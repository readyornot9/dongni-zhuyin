# 整理口語語料（Tatoeba、Mozilla Common Voice 台灣華語），輸出和 wiki-extract.py 相同格式
# 用法：python -I tools/colloquial-extract.py <專案根目錄>
# 只留整句都是 Big5 繁體字的句子；跟評估題庫、測試題重複或包含它們的句子會剔除，避免考題外洩
import bz2
import glob
import os
import re
import sys

root = sys.argv[1]
allowed = set(open(os.path.join(root, 'data/work/big5chars.txt'), encoding='utf-8').read())
HAN = re.compile(r'[一-鿿]')
RUNS = re.compile(r'[一-鿿]{2,}')
SENT = re.compile(r'[。！？；\n]')

# 考題：評估句子＋測試題（testcases.js 裡的 expect）
exam = set()
for line in open(os.path.join(root, 'docs/評估句子.txt'), encoding='utf-8'):
    if line.strip():
        exam.add(line.strip())
for m in re.finditer(r"expect: '([^']+)'", open(os.path.join(root, 'web/testcases.js'), encoding='utf-8').read()):
    exam.add(m.group(1))
# 太短的考題（謝謝、再見）是一般用語，不算外洩，不然會刪掉一大堆正常句子
exam = {e for e in exam if len(e) >= 5}

def sentences():
    with bz2.open(os.path.join(root, 'data/raw/tatoeba/cmn_sentences.tsv.bz2'), 'rt', encoding='utf-8') as f:
        for line in f:
            parts = line.rstrip('\n').split('\t')
            if len(parts) >= 3:
                yield 'tatoeba', parts[2]
    for path in glob.glob(os.path.join(root, 'data/raw/commonvoice/*.txt')):
        for line in open(path, encoding='utf-8'):
            yield 'commonvoice', line.strip()

stats = {}
with open(os.path.join(root, 'data/work/colloquial.txt'), 'w', encoding='utf-8') as out:
    for src, text in sentences():
        s = stats.setdefault(src, [0, 0, 0])
        for sent in SENT.split(text):
            hans = HAN.findall(sent)
            if len(hans) < 2:
                continue
            if any(c not in allowed for c in hans):
                s[1] += 1
                continue
            plain = ''.join(hans)
            if any(e in plain for e in exam):
                s[2] += 1
                continue
            s[0] += 1
            for run in RUNS.findall(sent):
                out.write(run + '\n')

for src, (kept, simp, leak) in stats.items():
    print(f'{src}: 保留 {kept} 句、丟掉簡體 {simp} 句、剔除和考題重複 {leak} 句')
