// 簡拼整句驗證：node tools/nn/run-abbr.js data/work/abbr-sent-tests.txt …（OLD=1 用舊參數比較）
const root=require('path').join(__dirname,'..','..');
require(root+'/web/dict.js');require(root+'/web/lm.js');const S=require(root+'/web/engine.js');const N=require(root+'/web/nn.js');const fs=require('fs');const nn=new N(fs.readFileSync(root+'/web/nnlm.wasm'));const rp=process.env.REV_FILE||root+'/web/nnlm-rev.wasm';const rev=process.env.REV!=='0'&&fs.existsSync(rp)?new N(fs.readFileSync(rp)):null;if(process.env.REV_MIX)N.REV_MIX=Number(process.env.REV_MIX);
if(process.env.OLD){S.Engine.COLLOQ_WEIGHT=0;S.Engine.ABBR_SINGLE=6;S.Engine.EASY_REFUND=0;}
const e=new S.Engine(new S.Dictionary(globalThis.ZHUYIN_DICT),new S.UserModel(),new S.LanguageModel(globalThis.ZHUYIN_LM));
for (const f of process.argv.slice(2)) {
  const lines=require('fs').readFileSync(f,'utf8').split('\n').filter(Boolean);
  for (const gen of [false,true]) {
    let ok=0,bad=[],ms=0;
    for(const l of lines){const exp=l.match(/expect: '([^']+)'/)[1],rd=l.match(/reading: '([^']+)'/)[1];const syls=S.parseReading(rd);
      const r=e.convert(syls,{nbest:N.TOP,beam:N.BEAM,perState:N.PER_STATE});let got=r.text;
      if(N.shouldRerank(r.nbest)){const t=Date.now();const sc=N.both(nn,rev,r.nbest.map(c=>c.text));
        if(gen&&S.hasAbbr(syls)){const m=N.merge(e,syls,{},r.nbest,sc,(g=>{if(!rev||!g.length)return g;const s=N.both(nn,rev,g.map(x=>x.text));return g.map((x,i)=>({text:x.text,lp:s[i]}))})(nn.generate(e.charChoices(syls))));got=N.pick(m.list,m.scores).text}else got=N.pick(r.nbest,sc).text; ms+=Date.now()-t;}
      if(got===exp)ok++;else bad.push(exp+'→'+got)}
    console.log(f.split('/').pop(), gen?'AI 寫句子':'原本', ok+'/'+lines.length, '平均 '+Math.round(ms/lines.length)+'ms', '\n  '+bad.join('  '));
  }
}
