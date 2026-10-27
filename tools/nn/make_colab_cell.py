# 把 model.py＋colab_all.py＋評估題庫壓成「一行」Python，方便直接貼進 Colab 的程式格
# 用法：.venv\Scripts\python tools/nn/make_colab_cell.py → data/nn/colab/cell.txt
import base64
import os
import re
import zlib

HERE = os.path.dirname(__file__)
ROOT = os.path.join(HERE, '..', '..')
model = open(os.path.join(HERE, 'model.py'), encoding='utf-8').read()
main = open(os.path.join(HERE, 'colab_all.py'), encoding='utf-8').read()
exam = [l.strip() for l in open(os.path.join(ROOT, 'docs', '評估句子.txt'), encoding='utf-8') if l.strip()]
exam += re.findall(r"expect: '([^']+)'", open(os.path.join(ROOT, 'web', 'testcases.js'), encoding='utf-8').read())
# 所有測試句（盲測、開發、專門題）都不能進訓練資料
import glob
for f in glob.glob(os.path.join(ROOT, 'docs', '*句子*.txt')):
    exam += [l.strip() for l in open(f, encoding='utf-8') if l.strip()]
exam = sorted(set(exam))
src = f'EXAM = {exam!r}\n' + model + '\n' + main
payload = base64.b64encode(zlib.compress(src.encode('utf-8'), 9)).decode()
loader = f'import zlib,base64;exec(zlib.decompress(base64.b64decode("{payload}")).decode())'
for name, head in [('cell.txt', 'MINUTES=100;SIZE="medium";'),
                   ('kaggle-cell.txt', 'MINUTES=100;SIZE="medium";OUT_DIR="/kaggle/working";TMP_DIR="/tmp";')]:
    out = os.path.join(ROOT, 'data', 'nn', 'colab', name)
    open(out, 'w', encoding='utf-8').write(head + loader)
    print(f'完成：{len(head + loader)} 字元 → {out}')

# 口語續訓：第一階段（CPU 挑資料）、第二階段（GPU 續訓）
data_src = open(os.path.join(HERE, 'kaggle_data.py'), encoding='utf-8').read()
ft_src = f'EXAM = {exam!r}\n' + model + '\n' + open(os.path.join(HERE, 'kaggle_finetune.py'), encoding='utf-8').read()
for name, head, src in [('kaggle-data-cell.txt', 'HOURS=4;WORKERS=4;', data_src),
                        ('kaggle-finetune-cell.txt', 'MINUTES=90;COL_MIN=0.05;', ft_src),
                        ('kaggle-reverse-cell.txt', 'MINUTES=120;COL_MIN=0.03;REVERSE=True;LR=1e-3;', ft_src)]:
    payload = base64.b64encode(zlib.compress(src.encode('utf-8'), 9)).decode()
    out = os.path.join(ROOT, 'data', 'nn', 'colab', name)
    open(out, 'w', encoding='utf-8').write(head + f'import zlib,base64;exec(zlib.decompress(base64.b64decode("{payload}")).decode())')
    print(f'完成：{os.path.getsize(out)} 位元組 → {out}')
