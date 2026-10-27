# 從中文維基百科備份檔抽出繁體句子（只用 Python 內建模組）
# 用法：python -I tools/wiki-extract.py data/raw/wiki/zhwiki-latest-pages-articles.xml.bz2 data/work/big5chars.txt data/work/wiki-trad.txt
# 輸出：每行一段連續的中文字（遇到標點、數字、英文就斷開），只保留整句都是 Big5 繁體字的句子
import bz2
import re
import sys
import time
import xml.etree.ElementTree as ET

src, charset_file, out_file = sys.argv[1], sys.argv[2], sys.argv[3]
allowed = set(open(charset_file, encoding='utf-8').read())

HAN = re.compile(r'[一-鿿]')
RUNS = re.compile(r'[一-鿿]{2,}')
SENT = re.compile(r'[。！？；\n]')
CLEAN = [
    (re.compile(r'<!--.*?-->', re.S), ''),
    (re.compile(r'<ref[^>/]*/>'), ''),
    (re.compile(r'<ref[^>]*>.*?</ref>', re.S), ''),
    (re.compile(r'<(math|code|pre|syntaxhighlight|gallery|timeline|score)[^>]*>.*?</\1>', re.S), ''),
    (re.compile(r'<[^>]+>'), ''),
    (re.compile(r'-\{[^{}]*?:[^{}]*?\}-'), ''),        # 語言轉換規則整段丟掉
    (re.compile(r'-\{([^{}]*?)\}-'), r'\1'),
    (re.compile(r'\[\[(?:File|Image|檔案|文件|图像|圖像|Category|分類|分类):[^\]]*\]\]', re.I), ''),
    (re.compile(r'\[\[[^\]|]*\|([^\]]*)\]\]'), r'\1'),
    (re.compile(r'\[\[([^\]]*)\]\]'), r'\1'),
    (re.compile(r'\[https?://[^\s\]]+\s*([^\]]*)\]'), r'\1'),
    (re.compile(r"'{2,}"), ''),
    (re.compile(r'^[|!{].*$', re.M), ''),               # 表格
    (re.compile(r'^=+.*?=+\s*$', re.M), ''),            # 標題
]

def strip_templates(t):
    # 移除 {{...}}（可巢狀）
    out, depth, i = [], 0, 0
    n = len(t)
    while i < n:
        if t.startswith('{{', i):
            depth += 1; i += 2; continue
        if depth and t.startswith('}}', i):
            depth -= 1; i += 2; continue
        if not depth:
            out.append(t[i])
        i += 1
    return ''.join(out)

def clean(t):
    t = strip_templates(t)
    for pat, rep in CLEAN:
        t = pat.sub(rep, t)
    return t

pages = kept = dropped = 0
start = time.time()
def pages_of(f):
    # 檔案還沒下載完（或被截斷）時，讀到哪算到哪
    try:
        for event, el in ET.iterparse(f, events=('end',)):
            yield el
    except (EOFError, ET.ParseError) as e:
        print(f'檔案提前結束：{e}', flush=True)

with bz2.open(src, 'rb') as f, open(out_file, 'w', encoding='utf-8') as out:
    for el in pages_of(f):
        tag = el.tag.rsplit('}', 1)[-1]
        if tag != 'page':
            continue
        ns = el.find('{*}ns')
        text = el.find('{*}revision/{*}text')
        redirect = el.find('{*}redirect')
        if ns is not None and ns.text == '0' and redirect is None and text is not None and text.text:
            pages += 1
            for sent in SENT.split(clean(text.text)):
                hans = HAN.findall(sent)
                if len(hans) < 4:
                    continue
                if any(c not in allowed for c in hans):
                    dropped += 1
                    continue
                kept += 1
                for run in RUNS.findall(sent):
                    out.write(run + '\n')
            if pages % 50000 == 0:
                print(f'{pages} 篇，保留 {kept} 句、丟掉 {dropped} 句，{time.time() - start:.0f} 秒', flush=True)
        el.clear()

print(f'完成：{pages} 篇，保留 {kept} 句（繁體）、丟掉 {dropped} 句（含簡體字），{time.time() - start:.0f} 秒')
