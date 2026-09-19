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
.sky{width:100%;max-width:210px;display:block;margin:8px auto 2px}
.bad{color:var(--jam)}
.warnv{color:var(--elev)}
.good{color:var(--clear)}
.hint{color:var(--dim);font-size:11px;margin:8px 0 0;line-height:1.5}
.legend{display:flex;flex-wrap:wrap;gap:8px;justify-content:center;
font-size:10px;color:var(--dim);margin-top:2px}
.legend i{display:inline-block;width:8px;height:8px;border-radius:50%;
margin-right:4px;vertical-align:middle}
.sub{color:var(--dim);font-size:11px;text-transform:uppercase;
letter-spacing:.5px;margin:12px 0 4px}
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

<h2 style="font-size:13px;color:var(--dim);text-transform:uppercase;
letter-spacing:.6px;margin:18px 0 8px">GNSS health</h2>
<div class="grid" id="gnss"></div>

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

const CONS_COL={GPS:'#58a6ff',GLONASS:'#f778ba',Galileo:'#3fb950',
BeiDou:'#d29922',QZSS:'#a371f7',combined:'#8b949e'};

function skySvg(sky){
  const S=200,c=S/2,R=c-14;
  let g=`<circle cx="${c}" cy="${c}" r="${R}" fill="#0d1117" stroke="#30363d"/>`;
  [2/3,1/3].forEach(f=>{g+=`<circle cx="${c}" cy="${c}" r="${(R*f).toFixed(1)}" fill="none" stroke="#30363d"/>`;});
  g+=`<line x1="${c}" y1="${c-R}" x2="${c}" y2="${c+R}" stroke="#30363d"/>`;
  g+=`<line x1="${c-R}" y1="${c}" x2="${c+R}" y2="${c}" stroke="#30363d"/>`;
  [['N',0],['E',90],['S',180],['W',270]].forEach(p=>{
    const a=p[1]*Math.PI/180,lx=c+(R+8)*Math.sin(a),ly=c-(R+8)*Math.cos(a);
    g+=`<text x="${lx.toFixed(1)}" y="${(ly+3).toFixed(1)}" fill="#8b949e" font-size="9" text-anchor="middle">${p[0]}</text>`;});
  let n=0;
  (sky||[]).forEach(s=>{
    if(s.el<0) return;
    n++;
    const el=Math.max(0,Math.min(90,s.el)),r=R*(90-el)/90,a=s.az*Math.PI/180;
    const x=c+r*Math.sin(a),y=c-r*Math.cos(a);
    const col=CONS_COL[s.c]||'#8b949e',t=s.snr>0;
    const op=t?(0.35+0.65*Math.min(1,s.snr/50)).toFixed(2):'0.4';
    g+=`<circle cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" r="3.4" fill="${t?col:'none'}" stroke="${col}" opacity="${op}"><title>${s.c} PRN ${s.prn} \u00b7 el ${s.el}\u00b0 az ${s.az}\u00b0 \u00b7 ${t?'C/N0 '+s.snr+' dBHz':'in view, not tracked'}</title></circle>`;
  });
  if(!n) return '';
  return `<svg viewBox="0 0 ${S} ${S}" class="sky">${g}</svg>`;
}

function healthCard(h,n){
  const row=(k,v,cls)=>`<div class="row"><span>${k}</span><span class="${cls||''}">${v}</span></div>`;
  let o=`<div class="card"><h2><span class="dot ${h.present?'on':''}"></span>Receiver ${n}</h2>`;
  o+=row('Link',`UART${h.uart} rx=${h.rx} tx=${h.tx} @${h.baud}`);
  o+=row('Bytes received',h.bytes,h.bytes?'good':'bad');

  if(!h.bytes){
    o+=`<p class="hint"><strong>Nothing is arriving on this port.</strong>
    The counter increments before any parsing, so this is the wire, not the
    firmware. Check that the module has power, that its TX goes to GPIO${h.rx},
    and that it is running at ${h.baud} baud.</p></div>`;
    return o;
  }

  o+=row('Last NMEA',h.nmeaAge<0?'never':h.nmeaAge+'s ago',h.nmeaAge>5?'warnv':'');
  o+=row('Telemetry',h.telemetry||'\u2014',
         h.telemetry==='none'?'bad':(h.telemetry?'good':''));
  if(h.mod) o+=row('Module',h.mod);
  if(h.sw)  o+=row('Firmware',h.sw);
  o+=row('Fix',h.fix?'yes':'NO',h.fix?'good':'bad');
  o+=row('Time to first fix',h.ttff<0?'not yet':h.ttff+'s');
  if(!h.fix) o+=row('Searching',h.searching+'s',h.searching>120?'warnv':'');
  if(h.antenna) o+=row('Antenna',['init','unknown','OK','short','open'][h.antenna]||h.antenna);

  if(h.telemetry==='none')
    o+=`<p class="hint">This receiver answers NMEA but not UBX, so it is
    probably not u-blox. Detection falls back to C/N0 collapse and fix loss,
    losing the jamming indicator and AGC \u2014 the two strongest signals.</p>`;

  if((h.cons||[]).length){
    o+='<div class="sub">Constellations</div>';
    h.cons.forEach(c=>{
      o+=row(`<i style="display:inline-block;width:8px;height:8px;border-radius:50%;background:${CONS_COL[c.name]||'#8b949e'};margin-right:6px"></i>${c.name}`,
        `${c.tracked}/${c.inView} tracked${c.snrMax?' \u00b7 max '+c.snrMax+' dBHz':''}`,
        c.tracked?'':'warnv');
    });
  }

  const sv=skySvg(h.sky);
  if(sv){
    o+='<div class="sub">Sky view</div>'+sv;
    const seen=[];
    (h.sky||[]).forEach(s=>{if(s.el>=0&&seen.indexOf(s.c)<0)seen.push(s.c);});
    o+='<div class="legend">'+seen.map(c=>
      `<span><i style="background:${CONS_COL[c]||'#8b949e'}"></i>${c}</span>`).join('')+'</div>';
  }
  return o+'</div>';
}

async function gnss(){
  let h; try{ h=await (await fetch('/api/gnss')).json(); }catch(e){ return; }
  $('gnss').innerHTML=healthCard(h.a,'A')+healthCard(h.b,'B');
}

function rebase(){ fetch('/api/reset-baseline',{method:'POST'}).then(tick); }
tick(); chart(); gnss();
setInterval(tick,1000); setInterval(chart,5000); setInterval(gnss,3000);
</script></body></html>)HTMLPAGE";
