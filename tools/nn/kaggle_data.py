# 第一階段（Kaggle，只用 CPU，不花 GPU 額度）：從 FineWeb-2 中文網頁挑出繁體、口語的文章存起來
# 語料：FineWeb-2 cmn_Hani（ODC-By 1.0）
# 口語判斷：文章裡「我、你、嗎、吧、啦、喔…」這類對話用字的比例（論壇、留言很高，百科、新聞很低）
# 輸出（/kaggle/working，執行結束後會保留，給第二階段當輸入）：
#   colloq-N.txt：口語文章，每行「口語比例<TAB>內文」（換行寫成 \n），第二階段再決定門檻
#   general-N.txt：一般繁體文章（避免模型忘掉書面語）
#   samples.txt：各口語比例的文章樣本，方便人工檢查
import multiprocessing as mp
import os
import re
import time

OUT = globals().get('OUT_DIR', '/kaggle/working')
HOURS = globals().get('HOURS', 4.0)
COL_CHARS = globals().get('COL_CHARS', 300_000_000)  # 口語文章要收多少字（全部 worker 加總）
GEN_CHARS = globals().get('GEN_CHARS', 100_000_000)
WORKERS = globals().get('WORKERS', 4)
COL_MIN = 0.02  # 口語比例超過這個就存進口語檔（第二階段可以再提高門檻）
MARK = set('我你妳嗎吧呢啦喔欸耶啊哈嗯囉哦唷吼齁咧啥嘛')
HAN = re.compile(r'[一-鿿]')
REPO = 'HuggingFaceFW/fineweb-2'


def log(msg, t0=[time.time()]):
    print(f'[{(time.time() - t0[0]) / 60:6.1f} 分] {msg}', flush=True)


def work(idx, files):
    from datasets import load_dataset
    big5 = {}

    def trad(c):
        r = big5.get(c)
        if r is None:
            try:
                c.encode('big5')
                r = True
            except UnicodeEncodeError:
                r = False
            big5[c] = r
        return r

    deadline = time.time() + HOURS * 3600
    col = gen = docs = kept = 0
    want_col, want_gen = COL_CHARS // WORKERS, GEN_CHARS // WORKERS
    fc = open(f'{OUT}/colloq-{idx}.txt', 'w', encoding='utf-8')
    fg = open(f'{OUT}/general-{idx}.txt', 'w', encoding='utf-8')
    fs = open(f'{OUT}/samples-{idx}.txt', 'w', encoding='utf-8')
    sampled = {}
    for f in files:
        ds = load_dataset('parquet', data_files=f'hf://datasets/{REPO}/{f}', split='train', streaming=True)
        for row in ds:
            docs += 1
            text = row['text']
            hans = HAN.findall(text)
            if len(hans) < 30:
                continue
            bad = sum(1 for c in hans if not trad(c))
            if bad > len(hans) * 0.002:
                continue
            kept += 1
            d = sum(1 for c in hans if c in MARK) / len(hans)
            flat = text.replace('\\', '\\\\').replace('\n', '\\n')
            if d >= COL_MIN:
                if col < want_col:
                    fc.write(f'{d:.4f}\t{flat}\n')
                    col += len(hans)
                b = round(d, 2)
                if sampled.get(b, 0) < 3:  # 每個比例區間留 3 篇樣本
                    sampled[b] = sampled.get(b, 0) + 1
                    fs.write(f'=== 口語比例 {d:.3f} ===\n{text[:400]}\n\n')
            elif gen < want_gen:
                fg.write(flat + '\n')
                gen += len(hans)
            if docs % 100000 == 0:
                log(f'worker {idx}：看了 {docs} 篇、繁體 {kept} 篇，口語 {col / 1e6:.0f}M 字、一般 {gen / 1e6:.0f}M 字')
                fc.flush(); fg.flush(); fs.flush()
            if (col >= want_col and gen >= want_gen) or time.time() > deadline:
                break
        if (col >= want_col and gen >= want_gen) or time.time() > deadline:
            break
    for h in (fc, fg, fs):
        h.close()
    log(f'worker {idx} 完成：看了 {docs} 篇、繁體 {kept} 篇，口語 {col / 1e6:.0f}M 字、一般 {gen / 1e6:.0f}M 字')
    return col, gen


if __name__ == '__main__' or 'get_ipython' in globals():
    from huggingface_hub import HfApi
    files = sorted(f for f in HfApi().list_repo_files(REPO, repo_type='dataset')
                   if f.startswith('data/cmn_Hani/train/') and f.endswith('.parquet'))
    log(f'FineWeb-2 cmn_Hani：{len(files)} 個檔案，{WORKERS} 個 worker 平行讀')
    # 每個 worker 輪流分配檔案（從不同的地方開始讀，口語文章分布比較平均）
    parts = [files[i::WORKERS] for i in range(WORKERS)]
    with mp.get_context('fork').Pool(WORKERS) as pool:
        res = pool.starmap(work, [(i, parts[i]) for i in range(WORKERS)])
    log(f'全部完成：口語 {sum(r[0] for r in res) / 1e6:.0f}M 字、一般 {sum(r[1] for r in res) / 1e6:.0f}M 字 → {OUT}')
