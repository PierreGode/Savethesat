/* The dashboard, served from flash.
 *
 * Self-contained on purpose: this AP has no uplink, so a phone loading this
 * page cannot reach a CDN. Every byte of CSS and JS is inline.
 */
#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Savethesat</title>
<style>
:root{--bg:#0d1117;--card:#161b22;--line:#30363d;--fg:#e6edf3;--dim:#8b949e;
--clear:#3fb950;--elev:#d29922;--jam:#f85149;--den:#ff4d4d;--warm:#6e7681}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);
font:14px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;
padding:env(safe-area-inset-top) 0 env(safe-area-inset-bottom)}
.wrap{max-width:720px;margin:0 auto;padding:16px}
header{display:flex;align-items:baseline;gap:8px;margin-bottom:14px}
h1{font-size:18px;margin:0;letter-spacing:.5px}
.ver{color:var(--dim);font-size:12px}
.up{margin-left:auto;color:var(--dim);font-size:12px;font-variant-numeric:tabular-nums}
.banner{border-radius:12px;padding:18px;text-align:center;margin-bottom:14px;
border:1px solid var(--line);background:var(--card);transition:background .3s}
.banner .lvl{font-size:30px;font-weight:700;letter-spacing:1px}
.banner .sc{font-size:13px;color:var(--dim);margin-top:4px}
.banner.CLEAR{background:#0f2417;border-color:var(--clear)}
.banner.CLEAR .lvl{color:var(--clear)}
.banner.ELEVATED{background:#2b2111;border-color:var(--elev)}
.banner.ELEVATED .lvl{color:var(--elev)}
.banner.JAMMED{background:#2d1315;border-color:var(--jam)}
.banner.JAMMED .lvl{color:var(--jam)}
.banner.DENIED{background:#3d1114;border-color:var(--den)}
.banner.DENIED .lvl{color:var(--den)}
.banner.WARMUP .lvl{color:var(--warm)}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:14px}
@media(max-width:520px){.grid{grid-template-columns:1fr}}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px}
.card h2{font-size:13px;margin:0 0 8px;color:var(--dim);font-weight:600;
text-transform:uppercase;letter-spacing:.6px;display:flex;gap:6px;align-items:center}
.dot{width:8px;height:8px;border-radius:50%;background:var(--warm);flex:none}
.dot.on{background:var(--clear)}
.row{display:flex;justify-content:space-between;padding:3px 0;
font-variant-numeric:tabular-nums}
.row span:first-child{color:var(--dim)}
.bars{margin-top:8px}
.bar{display:flex;align-items:center;gap:6px;margin:3px 0;font-size:12px}
.bar i{color:var(--dim);font-style:normal;width:56px;flex:none}
.bar u{flex:1;height:6px;background:#21262d;border-radius:3px;overflow:hidden}
.bar u b{display:block;height:100%;background:var(--elev);width:0;transition:width .3s}
canvas{width:100%;height:130px;display:block}
.btns{display:flex;gap:8px;flex-wrap:wrap;margin-top:14px}
button,a.btn{background:var(--card);color:var(--fg);border:1px solid var(--line);
border-radius:8px;padding:9px 14px;font-size:13px;cursor:pointer;text-decoration:none}
button:active{background:#21262d}
.peers{font-size:12px;color:var(--dim)}
.note{color:var(--dim);font-size:11px;margin-top:16px;line-height:1.6}
</style></head><body><div class="wrap">

<header><h1>SAVETHESAT</h1><span class="ver" id="ver"></span>
<span class="up" id="up"></span></header>

<div class="banner WARMUP" id="banner">
  <div class="lvl" id="lvl">WARMUP</div>
  <div class="sc" id="sc">learning the quiet baseline&hellip;</div>
</div>

<div class="card" style="margin-bottom:14px">
  <h2>Last 10 minutes</h2>
  <canvas id="chart" width="700" height="130"></canvas>
</div>

<div class="grid" id="rx"></div>

<div class="card">
  <h2>Mesh</h2>
  <div class="peers" id="peers">no other nodes heard</div>
</div>

<div class="btns">
  <button onclick="rebase()">Reset baseline</button>
  <a class="btn" href="/savethesat.csv" download>Download CSV</a>
</div>

<p class="note">This access point has no internet uplink and runs no captive
portal, so your phone keeps using mobile data for everything else while this
page stays reachable.<br>
Receive only. Savethesat never transmits in a GNSS band.</p>

</div><script>
const $=i=>document.getElementById(i);
const F=(v,d)=>v==null?'--':(d?v.toFixed(d):v);

function rxCard(r,n){
  const b=x=>`<div class="bar"><i>${x[0]}</i><u><b style="width:${x[1]}%"></b></u></div>`;
  return `<div class="card"><h2><span class="dot ${r.present?'on':''}"></span>
  Receiver ${n} ${r.mod?'&middot; '+r.mod:''} ${r.ubx?(r.monrf?'&middot; MON-RF':'&middot; MON-HW'):'&middot; NMEA only'}</h2>
  <div class="row"><span>Bytes received</span><span>${r.bytes}</span></div>
  <div class="row"><span>Level</span><span>${r.level}</span></div>
  <div class="row"><span>Jamming ind.</span><span>${F(r.jam)} / base ${F(r.baseJam,0)}</span></div>
  <div class="row"><span>AGC</span><span>${F(r.agc)} / base ${F(r.baseAgc,0)}</span></div>
  <div class="row"><span>Noise per ms</span><span>${F(r.noise)}</span></div>
  <div class="row"><span>C/N0 top-8</span><span>${F(r.cn0,1)} / base ${F(r.baseCn0,1)} dBHz</span></div>
  <div class="row"><span>Satellites</span><span>${r.satsUsed} used / ${r.satsVis} visible</span></div>
  <div class="row"><span>Fix</span><span>${r.fix?'yes':'NO'}</span></div>
  <div class="bars">${[['jamInd',r.cJam/35*100],['AGC',r.cAgc/20*100],
    ['noise',r.cNoise/10*100],['C/N0',r.cCn0/25*100],['fix',r.cFix/10*100]].map(b).join('')}</div>
  </div>`;
}

async function tick(){
  let s; try{ s=await (await fetch('/api/status')).json(); }catch(e){ return; }
  $('ver').textContent='v'+s.fw;
  const u=s.uptime; $('up').textContent=
    String((u/3600)|0).padStart(2,'0')+':'+String(((u/60)|0)%60).padStart(2,'0')+
    ':'+String(u%60).padStart(2,'0');
  $('banner').className='banner '+s.level;
  $('lvl').textContent=s.level;
  $('sc').textContent = s.level==='WARMUP'
    ? 'learning the quiet baseline… '+s.warmup+'s left'
    : 'score '+s.score+'/100'+(s.localOnly?' · local interference (reference channel clear)':'');
  $('rx').innerHTML=rxCard(s.a,'A')+rxCard(s.b,'B');
  $('peers').textContent = s.peers.length
    ? s.peers.map(p=>p.mac+'  '+p.level+'  score '+p.score).join('\n')
    : 'no other nodes heard';
  $('peers').style.whiteSpace='pre';
}

async function chart(){
  let h; try{ h=await (await fetch('/api/history')).json(); }catch(e){ return; }
  const c=$('chart'), x=c.getContext('2d'), W=c.width, H=c.height;
  x.clearRect(0,0,W,H);
  x.strokeStyle='#30363d'; x.lineWidth=1;
  for(let i=0;i<=4;i++){const y=H*i/4+.5;x.beginPath();x.moveTo(0,y);x.lineTo(W,y);x.stroke();}
  if(!h.score||!h.score.length) return;
  const n=h.score.length, dx=W/Math.max(n-1,1);
  const line=(arr,col,max)=>{
    x.strokeStyle=col; x.lineWidth=2; x.beginPath();
    arr.forEach((v,i)=>{const y=H-(v/max)*H;i?x.lineTo(i*dx,y):x.moveTo(i*dx,y);});
    x.stroke();
  };
  line(h.jamA,'#58a6ff',255);
  line(h.jamB,'#a371f7',255);
  line(h.score,'#f85149',100);
}

function rebase(){ fetch('/api/reset-baseline',{method:'POST'}).then(tick); }
tick(); chart(); setInterval(tick,1000); setInterval(chart,5000);
</script></body></html>)HTMLPAGE";
