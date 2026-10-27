// 診斷：錯的句子，正確答案有沒有在候選名單裡、統計分數和 AI 分數各差多少
// 用法：node tools/nn/diag.js docs/盲測句子3.txt
const root=require('path').join(__dirname,'..','..');const fs=require('fs');
require(root+'/web/dict.js');require(root+'/web/lm.js');const S=require(root+'/web/engine.js');const N=require(root+'/web/nn.js');
const nn=new N(fs.readFileSync(root+'/web/nnlm.wasm'));
const e=new S.Engine(new S.Dictionary(globalThis.ZHUYIN_DICT),new S.UserModel(),new S.LanguageModel(globalThis.ZHUYIN_LM));
const best=new Map();for(const l of globalThis.ZHUYIN_DICT.split('\n')){const[w,r]=l.split('\t');const nat=(w,r)=>{const s=r.split(' ');return [...w].every((c,i)=>(c!=='不'||s[i]==='ㄅㄨˋ')&&(c!=='一'||s[i]==='ㄧ'))};if(!best.has(w)||(!nat(w,best.get(w))&&nat(w,r)))best.set(w,r)}
function toReading(t){const o=[],c=[...t];for(let i=0;i<c.length;){let f=null;for(let n=Math.min(9,c.length-i);n>=1;n--){const w=c.slice(i,i+n).join('');if(best.has(w)){f=[n,best.get(w)];break}}if(!f)return null;o.push(f[1]);i+=f[0]}return o.join(' ')}
for(const f of process.argv.slice(2))for(const t of fs.readFileSync(f,'utf8').split(/\r?\n/).filter(Boolean)){
  const rd=toReading(t);if(!rd)continue;const syls=S.parseReading(rd);
  const r=e.convert(syls,{nbest:N.TOP,beam:N.BEAM,perState:N.PER_STATE});const sc=nn.score(r.nbest.map(c=>c.text));
  const got=N.pick(r.nbest,sc).text;if(got===t)continue;
  const gi=r.nbest.findIndex(c=>c.text===got),ti=r.nbest.findIndex(c=>c.text===t);
  let line=`${t}→${got}  `;
  if(ti<0){const o=e.convert(syls,{only:t,nbest:0,beam:8,perState:4});const ts=nn.score([t])[0];line+=`不在前${N.TOP}名（統計 ${o.text===t?(o.score-r.nbest[gi].score).toFixed(1):'拼不出'}，AI ${(ts-sc[gi]).toFixed(1)}）`}
  else line+=`第${ti+1}名：統計 ${(r.nbest[ti].score-r.nbest[gi].score).toFixed(1)}，AI ${(sc[ti]-sc[gi]).toFixed(1)}`;
  console.log(line)}
