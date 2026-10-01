#pragma once
// Веб-интерфейс (одна страница, без внешних зависимостей — работает и в режиме точки доступа).
static const char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Плавка парафина</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--dim:#6b7385;--acc:#0a84c6;--ok:#1f9d55;--warn:#e07b00;--err:#d64545;--line:#e3e6ec}
@media(prefers-color-scheme:dark){:root{--bg:#12151b;--card:#1b2029;--fg:#e8ebf1;--dim:#8d96a8;--line:#2a313d}}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:920px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:0 0 12px}h2{font-size:15px;margin:0 0 10px;color:var(--dim);font-weight:600}
.grid{display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(260px,1fr))}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px}
.big{font-size:44px;font-weight:700;font-variant-numeric:tabular-nums}
.row{display:flex;justify-content:space-between;align-items:center;gap:8px;margin:4px 0}
.pill{display:inline-block;padding:3px 10px;border-radius:99px;font-weight:700;color:#fff;background:var(--dim)}
.HEAT{background:var(--ok)}.TUNE{background:var(--acc)}.FAULT{background:var(--err)}
.bar{height:12px;background:var(--line);border-radius:6px;overflow:hidden}.bar>div{height:100%;background:var(--warn);width:0}
button{font:inherit;padding:8px 14px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--fg);cursor:pointer}
button.p{background:var(--acc);color:#fff;border-color:var(--acc)}button.d{background:var(--err);color:#fff;border-color:var(--err)}
input,select{font:inherit;padding:6px 8px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--fg);width:120px}
.btns{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}
canvas{width:100%;height:220px;display:block}
.err{color:var(--err);font-weight:600}.dim{color:var(--dim)}
table{width:100%;border-collapse:collapse}td{padding:4px 2px;border-bottom:1px solid var(--line)}td:last-child{text-align:right}
details summary{cursor:pointer;font-weight:600;margin:4px 0}
</style></head><body><main>
<h1>Плавка парафина <span id="mode" class="pill">…</span></h1>
<div class="grid">
 <div class="card"><h2>Температура парафина</h2>
  <div class="big" id="pv">--.-</div>
  <div class="row"><span class="dim">Уставка (рампа)</span><b id="sp">–</b></div>
  <div class="row"><span class="dim">Датчик ТЭНа</span><b id="ht">–</b></div>
  <div class="row"><span class="dim">Мощность</span><b id="out">–</b></div>
  <div class="bar"><div id="outbar"></div></div>
  <div id="fault" class="err" style="margin-top:8px"></div>
 </div>
 <div class="card"><h2>Управление</h2>
  <div class="row"><span>Уставка, °C</span><span><input id="tgt" type="number" step="0.5"> <button onclick="setT()">OK</button></span></div>
  <div class="btns">
   <button class="p" onclick="cmd('start')">Пуск</button>
   <button onclick="cmd('stop')">Стоп</button>
   <button onclick="if(confirm('Запустить автотюнинг? Парафин должен быть полностью расплавлен.'))cmd('tune')">Автотюнинг</button>
   <button class="d" onclick="cmd('ack')">Сброс аварии</button>
  </div>
  <p class="dim" id="info" style="margin-bottom:0"></p>
 </div>
</div>
<div class="card" style="margin-top:12px"><h2>График (30 мин)</h2><canvas id="cv"></canvas></div>
<div class="card" style="margin-top:12px"><h2>Настройки регулятора</h2>
 <table id="tbl"></table>
 <div class="btns"><button class="p" onclick="saveCtl()">Сохранить</button></div>
</div>
<div class="card" style="margin-top:12px"><details><summary>Сеть и MQTT</summary>
 <table>
  <tr><td>Wi-Fi SSID</td><td><input id="ssid"></td></tr>
  <tr><td>Wi-Fi пароль</td><td><input id="pass" type="password" placeholder="без изменений"></td></tr>
  <tr><td>Имя хоста</td><td><input id="hostname"></td></tr>
  <tr><td>Пароль веб (логин admin)</td><td><input id="webPass" type="password" placeholder="без изменений"></td></tr>
  <tr><td>MQTT сервер</td><td><input id="mqttHost"></td></tr>
  <tr><td>MQTT порт</td><td><input id="mqttPort" type="number"></td></tr>
  <tr><td>MQTT логин</td><td><input id="mqttUser"></td></tr>
  <tr><td>MQTT пароль</td><td><input id="mqttPass" type="password" placeholder="без изменений"></td></tr>
  <tr><td>MQTT базовый топик</td><td><input id="mqttBase"></td></tr>
  <tr><td>Home Assistant discovery</td><td><select id="mqttHa"><option value="1">да</option><option value="0">нет</option></select></td></tr>
 </table>
 <div class="btns"><button class="p" onclick="saveNet()">Сохранить и перезагрузить</button></div>
</details></div>
<p class="dim" id="foot"></p>
</main><script>
const $=id=>document.getElementById(id);let fields=[];
async function api(u,b){const r=await fetch(u,b?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)}:{});if(!r.ok)throw new Error(r.status);return r.json()}
const f1=(v,ok)=>ok?v.toFixed(1)+' °C':'--.-';
async function poll(){try{const s=await api('/api/status');
 $('mode').textContent=s.mode;$('mode').className='pill '+s.mode;
 $('pv').textContent=s.pvValid?s.pv.toFixed(1)+' °C':'--.-';
 $('sp').textContent=s.sp.toFixed(1)+' / '+s.target.toFixed(1)+' °C';
 $('ht').textContent=s.heaterPresent?f1(s.heater,s.heaterValid):'нет';
 $('out').textContent=s.out.toFixed(0)+' %'+(s.ssr?' ●':'');$('outbar').style.width=s.out+'%';
 $('fault').textContent=s.fault?('Авария: '+s.faultText):'';
 if(document.activeElement!==$('tgt'))$('tgt').value=s.target;
 $('info').textContent=s.mode=='TUNE'?('Автотюнинг: цикл '+s.tuneCycles):(s.ku?('Последний тюнинг: Ku='+s.ku.toFixed(2)+', Tu='+s.tu.toFixed(0)+' с'):'');
 $('foot').textContent=s.chip+' · FW '+s.fw+' · ID '+s.id+' · аптайм '+Math.floor(s.uptime/60)+' мин'+(s.rssi?' · RSSI '+s.rssi+' дБм':'');
}catch(e){$('mode').textContent='нет связи';$('mode').className='pill FAULT'}}
async function hist(){try{const h=await api('/api/history');draw(h)}catch(e){}}
function draw(h){const c=$('cv'),d=devicePixelRatio||1,W=c.clientWidth,H=c.clientHeight;c.width=W*d;c.height=H*d;
 const g=c.getContext('2d');g.scale(d,d);const cs=getComputedStyle(document.body);g.clearRect(0,0,W,H);
 const pv=h.pv,sp=h.sp,n=pv.length;if(n<2)return;let lo=1e9,hi=-1e9;
 for(let i=0;i<n;i++){if(pv[i]!==null){lo=Math.min(lo,pv[i]);hi=Math.max(hi,pv[i])}lo=Math.min(lo,sp[i]);hi=Math.max(hi,sp[i])}
 if(hi-lo<4){const m=(hi+lo)/2;lo=m-2;hi=m+2}lo-=.5;hi+=.5;
 const X=i=>40+(W-48)*i/(n-1),Y=v=>8+(H-24)*(hi-v)/(hi-lo);
 g.strokeStyle=cs.getPropertyValue('--line');g.fillStyle=cs.getPropertyValue('--dim');g.font='11px system-ui';
 for(let k=0;k<=4;k++){const v=lo+(hi-lo)*k/4,y=Y(v);g.beginPath();g.moveTo(40,y);g.lineTo(W-8,y);g.stroke();g.fillText(v.toFixed(1),2,y+4)}
 g.fillText('−'+Math.round(n*h.interval/60)+' мин',40,H-2);g.fillText('сейчас',W-48,H-2);
 g.setLineDash([5,4]);g.strokeStyle=cs.getPropertyValue('--dim');g.beginPath();sp.forEach((v,i)=>i?g.lineTo(X(i),Y(v)):g.moveTo(X(i),Y(v)));g.stroke();
 g.setLineDash([]);g.strokeStyle=cs.getPropertyValue('--acc');g.lineWidth=2;g.beginPath();let pen=false;
 pv.forEach((v,i)=>{if(v===null){pen=false;return}pen?g.lineTo(X(i),Y(v)):g.moveTo(X(i),Y(v));pen=true});g.stroke()}
async function cmd(c,v){try{const r=await api('/api/cmd',{cmd:c,value:v});if(!r.ok)alert(r.error||'Отказано');poll()}catch(e){alert('Ошибка: '+e.message)}}
function setT(){cmd('target',parseFloat($('tgt').value))}
async function loadSettings(){const s=await api('/api/settings');fields=s.fields;
 $('tbl').innerHTML=fields.map(f=>`<tr><td>${f.help}<br><span class="dim">${f.key}</span></td><td><input id="f_${f.key}" type="number" step="any" value="${f.value}"></td></tr>`).join('');
 const n=s.net;['ssid','hostname','mqttHost','mqttPort','mqttUser','mqttBase'].forEach(k=>$(k).value=n[k]);$('mqttHa').value=n.mqttHa}
async function saveCtl(){const b={ctl:{}};fields.forEach(f=>{const v=parseFloat($('f_'+f.key).value);if(!isNaN(v))b.ctl[f.key]=v});
 try{await api('/api/settings',b);await loadSettings();alert('Сохранено')}catch(e){alert('Ошибка: '+e.message)}}
async function saveNet(){const n={};['ssid','hostname','mqttHost','mqttUser','mqttBase'].forEach(k=>n[k]=$(k).value);
 n.mqttPort=parseInt($('mqttPort').value)||1883;n.mqttHa=parseInt($('mqttHa').value);
 ['pass','webPass','mqttPass'].forEach(k=>{if($(k).value)n[k]=$(k).value});
 try{await api('/api/settings',{net:n});await api('/api/reboot',{});alert('Перезагрузка…')}catch(e){alert('Ошибка: '+e.message)}}
poll();hist();loadSettings();setInterval(poll,2000);setInterval(hist,10000);
</script></body></html>)HTML";
