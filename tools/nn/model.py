# 小型字元語言模型（Transformer decoder），用來判斷一句話通不通順
import math

import torch
import torch.nn as nn
import torch.nn.functional as F


class CharLM(nn.Module):
    def __init__(self, vocab, d=256, layers=4, heads=4, ff=1024, ctx=64, dropout=0.1):
        super().__init__()
        self.ctx = ctx
        self.emb = nn.Embedding(vocab, d)
        self.pos = nn.Embedding(ctx, d)
        layer = nn.TransformerEncoderLayer(d, heads, ff, dropout, batch_first=True, norm_first=True, activation='gelu')
        self.blocks = nn.TransformerEncoder(layer, layers)
        self.norm = nn.LayerNorm(d)
        self.out = nn.Linear(d, vocab, bias=False)
        self.out.weight = self.emb.weight  # 輸入輸出共用字向量，省一半參數
        mask = torch.triu(torch.full((ctx, ctx), float('-inf')), diagonal=1)
        self.register_buffer('mask', mask, persistent=False)
        # 字向量用小的初始值：預設的初始值太大，加上輸入輸出共用，一開始預測會極端，前段訓練都在白白修正
        nn.init.normal_(self.emb.weight, std=0.02)
        nn.init.normal_(self.pos.weight, std=0.02)

    def forward(self, x):
        t = x.size(1)
        h = self.emb(x) + self.pos(torch.arange(t, device=x.device))
        h = self.blocks(h, mask=self.mask[:t, :t], is_causal=True)
        return self.out(self.norm(h))

    def loss(self, x, y):
        logits = self(x)
        return F.cross_entropy(logits.reshape(-1, logits.size(-1)), y.reshape(-1))

    @torch.no_grad()
    def sentence_logprob(self, ids_list):
        # 每句：<s> 字... <s>，回傳整句的 log 機率（自然對數）總和
        out = []
        for ids in ids_list:
            seq = torch.tensor([[0] + ids + [0]][:1])[:, -self.ctx - 1:]
            x, y = seq[:, :-1], seq[:, 1:]
            lp = F.log_softmax(self(x), -1).gather(-1, y.unsqueeze(-1)).sum().item()
            out.append(lp)
        return out
