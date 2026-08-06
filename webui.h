#ifndef _WEBUI_H_
#define _WEBUI_H_

#include <pgmspace.h>

// Neon single-page file manager, served from flash at GET /<dir>.
// Self-contained: no CDN, no external fonts - works on an offline LAN.
// Talks to the board with the existing WebDAV verbs (PUT/DELETE/MKCOL/MOVE)
// plus two tiny JSON endpoints: ?api=list and ?api=status.

static const char WEBUI_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SD-WIFI</title>
<style>
:root{--bg:#070b14;--panel:rgba(13,21,38,.82);--line:rgba(56,229,255,.22);--cy:#38e5ff;--mg:#ff4ded;--tx:#d9e8ff;--dim:#7d92b5;--ok:#3dff9a;--warn:#ffcc4d;--err:#ff5470;--r:12px}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:system-ui,'Segoe UI',sans-serif;background:var(--bg);color:var(--tx);min-height:100vh;padding-bottom:64px;
background-image:radial-gradient(1100px 520px at 75% -10%,rgba(56,229,255,.10),transparent 60%),
radial-gradient(900px 500px at 8% 108%,rgba(255,77,237,.09),transparent 55%),
repeating-linear-gradient(0deg,transparent 0 31px,rgba(56,229,255,.04) 31px 32px),
repeating-linear-gradient(90deg,transparent 0 31px,rgba(56,229,255,.04) 31px 32px)}
header{display:flex;flex-wrap:wrap;gap:10px;align-items:center;justify-content:space-between;padding:16px 20px;max-width:1020px;margin:0 auto}
.logo{font-size:24px;font-weight:800;letter-spacing:3px;color:#fff;text-shadow:0 0 8px var(--cy),0 0 26px rgba(56,229,255,.5);animation:pulse 3.5s ease-in-out infinite}
.logo i{color:var(--mg);font-style:normal;text-shadow:0 0 10px var(--mg)}
.logo em{font-style:normal;font-size:11px;letter-spacing:1px;color:var(--dim);text-shadow:none;margin-left:8px;vertical-align:middle}
@keyframes pulse{0%,100%{text-shadow:0 0 8px var(--cy),0 0 26px rgba(56,229,255,.5)}50%{text-shadow:0 0 10px var(--cy),0 0 40px rgba(56,229,255,.8)}}
.chips{display:flex;gap:8px;flex-wrap:wrap}
.chip{font-size:12px;padding:5px 12px;border:1px solid var(--line);border-radius:999px;background:rgba(56,229,255,.06);color:var(--tx);white-space:nowrap}
.hbtn{font:inherit;font-size:12px;padding:5px 14px;border-radius:999px;background:rgba(56,229,255,.09);border:1px solid rgba(56,229,255,.4);color:var(--tx);cursor:pointer;white-space:nowrap;transition:all .18s}
.hbtn:hover{border-color:var(--cy);box-shadow:0 0 12px rgba(56,229,255,.4)}
.chip b{color:var(--cy);font-weight:600}
.sig{display:inline-flex;align-items:flex-end;gap:2px;height:13px;vertical-align:-2px}
.sig i{width:3px;border-radius:1px;background:rgba(125,146,181,.35);transition:all .3s}
.sig i:nth-child(1){height:5px}.sig i:nth-child(2){height:8px}
.sig i:nth-child(3){height:11px}.sig i:nth-child(4){height:13px}
.sig i.on{background:var(--cy);box-shadow:0 0 6px var(--cy)}
.sig.weak i.on{background:var(--warn);box-shadow:0 0 6px var(--warn)}
main{max-width:1020px;margin:0 auto;padding:0 20px}
#busy{display:none;margin:0 0 12px;padding:10px 14px;border:1px solid rgba(255,204,77,.4);border-radius:var(--r);background:rgba(255,204,77,.08);color:var(--warn);font-size:13px;animation:blink 1.6s ease-in-out infinite}
#busy.err{border-color:rgba(255,84,112,.4);background:rgba(255,84,112,.07);color:var(--err);animation:none}
@keyframes blink{50%{opacity:.55}}
#crumbs{display:flex;flex-wrap:wrap;gap:2px;align-items:center;font-size:14px;margin:2px 0 12px;min-height:22px}
#crumbs a{color:var(--cy);text-decoration:none;padding:2px 6px;border-radius:6px;transition:background .15s}
#crumbs a:hover{background:rgba(56,229,255,.12);text-shadow:0 0 8px var(--cy)}
#crumbs s{color:var(--dim);text-decoration:none;margin:0 1px}
.bar{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-bottom:14px}
button{font:inherit;font-size:13px;color:var(--tx);background:rgba(56,229,255,.07);border:1px solid var(--line);border-radius:10px;padding:8px 14px;cursor:pointer;transition:all .18s}
button:hover{border-color:var(--cy);box-shadow:0 0 14px rgba(56,229,255,.35);transform:translateY(-1px)}
button:active{transform:translateY(0)}
button.pri{background:linear-gradient(135deg,rgba(56,229,255,.18),rgba(255,77,237,.14));border-color:rgba(56,229,255,.5)}
button.danger{border-color:rgba(255,84,112,.35);color:#ff9db0;margin-left:auto}
button.danger:hover{border-color:var(--err);box-shadow:0 0 14px rgba(255,84,112,.45)}
button:disabled{opacity:.4;pointer-events:none}
#drop{display:none;position:fixed;inset:14px;z-index:50;border:2px dashed var(--cy);border-radius:18px;background:rgba(7,11,20,.85);color:var(--cy);font-size:22px;align-items:center;justify-content:center;text-shadow:0 0 12px var(--cy);box-shadow:inset 0 0 60px rgba(56,229,255,.15)}
#up{display:none;margin-bottom:14px;padding:12px 14px;border:1px solid var(--line);border-radius:var(--r);background:var(--panel)}
#upN{font-size:13px;margin-bottom:8px;color:var(--cy)}
.pb{height:10px;border-radius:6px;background:rgba(56,229,255,.1);overflow:hidden}
.pb div{height:100%;width:0%;border-radius:6px;background:linear-gradient(90deg,var(--cy),var(--mg));box-shadow:0 0 12px rgba(56,229,255,.7);transition:width .15s}
#upI{font-size:12px;color:var(--dim);margin-top:6px}
#list{border:1px solid var(--line);border-radius:var(--r);background:var(--panel);overflow:hidden;box-shadow:0 8px 40px rgba(0,0,0,.45)}
.row{display:flex;align-items:center;gap:12px;padding:10px 16px;border-bottom:1px solid rgba(56,229,255,.08);animation:in .25s both;transition:background .15s}
.row:last-child{border-bottom:0}
.row:hover{background:rgba(56,229,255,.06)}
.row:hover .nm{text-shadow:0 0 10px rgba(56,229,255,.6)}
@keyframes in{from{opacity:0;transform:translateY(5px)}to{opacity:1;transform:none}}
.ck{width:15px;height:15px;accent-color:var(--cy);cursor:pointer;flex:none}
.ic{width:24px;text-align:center;flex:none}
.nm{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:var(--tx);text-decoration:none;cursor:pointer;transition:text-shadow .15s}
.nm.dir{color:var(--cy)}
.sz,.dt{flex:none;font-size:12px;color:var(--dim);font-variant-numeric:tabular-nums}
.sz{width:76px;text-align:right}
.dt{width:118px;text-align:right}
.act{flex:none;display:flex;gap:4px;opacity:0;transition:opacity .15s}
.row:hover .act{opacity:1}
@media(pointer:coarse){.act{opacity:.8}.dt{display:none}}
.act button{padding:4px 8px;font-size:12px;border-radius:8px}
.act .del:hover{border-color:var(--err);box-shadow:0 0 12px rgba(255,84,112,.45);color:var(--err)}
.empty{padding:34px;text-align:center;color:var(--dim);font-size:14px}
footer{position:fixed;left:0;right:0;bottom:0;display:flex;gap:18px;flex-wrap:wrap;align-items:center;padding:9px 20px;background:rgba(7,11,20,.92);border-top:1px solid var(--line);backdrop-filter:blur(8px);font-size:11px;color:var(--dim)}
footer b{color:var(--tx);font-weight:600;margin-left:4px;font-variant-numeric:tabular-nums}
.g{width:90px;height:7px;border-radius:5px;background:rgba(56,229,255,.12);overflow:hidden;display:inline-block;vertical-align:middle;margin-left:6px}
.g div{height:100%;width:0%;background:var(--ok);border-radius:5px;transition:width .4s,background .4s;box-shadow:0 0 8px currentColor}
#fmask{display:none;position:fixed;inset:0;z-index:90;background:rgba(4,7,13,.85);backdrop-filter:blur(4px);align-items:center;justify-content:center;text-align:center}
#farc{filter:drop-shadow(0 0 6px rgba(56,229,255,.8));transition:stroke-dashoffset .3s}
#fpct{font-size:30px;font-weight:700;color:#fff;text-shadow:0 0 14px var(--cy);margin-top:-102px;font-variant-numeric:tabular-nums}
#ftime{font-size:14px;color:var(--cy);margin-top:58px;font-variant-numeric:tabular-nums}
#fmsg{font-size:12px;color:var(--dim);margin-top:8px}
#toasts{position:fixed;top:14px;right:14px;z-index:100;display:flex;flex-direction:column;gap:8px}
.toast{padding:10px 16px;border-radius:10px;background:var(--panel);border:1px solid var(--line);font-size:13px;animation:tin .25s;box-shadow:0 4px 24px rgba(0,0,0,.5)}
.toast.err{border-color:rgba(255,84,112,.5);color:var(--err)}
.toast.ok{border-color:rgba(61,255,154,.4);color:var(--ok)}
@keyframes tin{from{opacity:0;transform:translateX(24px)}to{opacity:1;transform:none}}
#mask{display:none;position:fixed;inset:0;z-index:80;background:rgba(4,7,13,.7);backdrop-filter:blur(3px);align-items:center;justify-content:center}
#dlg{width:min(92vw,380px);padding:20px;border-radius:16px;background:var(--panel);border:1px solid rgba(56,229,255,.35);box-shadow:0 0 50px rgba(56,229,255,.18);animation:in .2s}
#dlg h3{font-size:15px;margin-bottom:14px;color:var(--cy);font-weight:600}
#dlg input{width:100%;margin-bottom:16px}
#dlg input,#sdlg input,#sdlg select{font:inherit;color:var(--tx);background:rgba(56,229,255,.06);border:1px solid var(--line);border-radius:9px;padding:9px 12px;outline:0}
#dlg input:focus,#sdlg input:focus,#sdlg select:focus{border-color:var(--cy);box-shadow:0 0 10px rgba(56,229,255,.35)}
#dlg .bt,#sdlg .bt{display:flex;gap:8px;justify-content:flex-end}
#smask{display:none;position:fixed;inset:0;z-index:80;background:rgba(4,7,13,.7);backdrop-filter:blur(3px);align-items:center;justify-content:center}
#sdlg{width:min(92vw,430px);max-height:88vh;overflow-y:auto;padding:20px;border-radius:16px;background:var(--panel);border:1px solid rgba(56,229,255,.35);box-shadow:0 0 50px rgba(56,229,255,.18);animation:in .2s}
#sdlg h3{font-size:15px;margin-bottom:14px;color:var(--cy);font-weight:600}
#sdlg .sec{margin-bottom:16px;padding-bottom:14px;border-bottom:1px solid rgba(56,229,255,.1)}
#sdlg label{font-size:12px;color:var(--cy);letter-spacing:.5px}
#sdlg .frow{display:flex;gap:8px;margin-top:7px}
#sdlg .frow input,#sdlg .frow select{flex:1;min-width:0}
#sdlg .hint{font-size:11px;color:var(--dim);margin-top:6px;line-height:1.5}
#sdlg select option{background:#0d1526;color:var(--tx)}
</style></head><body>
<header>
 <div class="logo">SD<i>-</i>WIFI<em id="fw"></em></div>
 <div class="chips"><span class="chip">&#127991; <b id="cN">-</b></span><span class="chip">&#127760; <b id="cI">-</b></span><span class="chip" id="cRw">&#128246; <span class="sig" id="cR"><i></i><i></i><i></i><i></i></span></span><span class="chip">&#128190; <b id="cC">-</b></span><button class="hbtn" id="bS" title="Board settings">&#128295; Settings</button></div>
</header>
<main>
 <div id="busy">&#9888; Card is in use by another host (USB reader / printer) - retrying automatically</div>
 <nav id="crumbs"></nav>
 <div class="bar">
  <button class="pri" id="bU">&#11014; Upload</button>
  <button id="bD">&#128193; New Folder</button>
  <button id="bR">&#10227; Refresh</button>
  <button id="bA" title="Select / deselect everything in this folder">&#9745; All</button>
  <button class="danger" id="bDel" style="display:none;margin-left:0">&#128465; Delete (<span id="delN">0</span>)</button>
  <button class="danger" id="bF">&#9888; Format</button>
 </div>
 <div id="up"><div id="upN"></div><div class="pb"><div id="upF"></div></div><div id="upI"></div></div>
 <div id="list"></div>
</main>
<div id="drop">&#11014; Drop files to upload</div>
<footer>
 <span>HEAP<b id="fH">-</b><span class="g"><div id="gH"></div></span></span>
 <span>MIN<b id="fM">-</b></span>
 <span>FRAG<b id="fF">-</b></span>
 <span>MAXBLK<b id="fB">-</b></span>
 <span>UPTIME<b id="fU">-</b></span>
</footer>
<div id="mask"><div id="dlg"><h3 id="dT"></h3><input id="dI"><div class="bt"><button id="dC">Cancel</button><button class="pri" id="dO">OK</button></div></div></div>
<div id="smask"><div id="sdlg">
 <h3>&#128295; Board Settings</h3>
 <div class="sec"><label>BOARD NAME</label>
  <div class="frow"><input id="sName" maxlength="23" placeholder="SDWIFI-01"><button class="pri" id="sNameGo">Apply</button></div>
  <div class="hint">A-Z, 0-9, dash. Restarts the board; web UI then also at http://&lt;name&gt;.local (mDNS), OTA uses the same name.</div></div>
 <div class="sec"><label>WIFI NETWORK</label>
  <div class="frow"><input id="sSsid" maxlength="31" placeholder="SSID"></div>
  <div class="frow"><input id="sPass" maxlength="63" placeholder="Password (blank = open network)"><button class="pri" id="sWifiGo">Connect</button></div>
  <div class="hint">Same as serial M50+M51+M52. Saves and restarts. If the board never comes back, fix via SETUP.INI on the card or serial commands.</div></div>
 <div class="sec"><label>SERIAL PORT (USB / gcode link)</label>
  <div class="frow"><select id="sBaud"><option>9600</option><option>19200</option><option>38400</option><option>57600</option><option>74880</option><option>115200</option><option>230400</option><option>250000</option><option>460800</option><option>921600</option></select><button class="pri" id="sBaudGo">Apply</button></div>
  <div class="hint">Applies immediately and persists. Boot messages always start at 115200, then switch to this rate.</div></div>
 <div class="sec"><label>PRINTER / CARD-READER SHARING</label>
  <div class="frow"><select id="sBlk"><option value="3">3 s - fastest WiFi, printer idle only</option><option value="5">5 s</option><option value="10">10 s - default</option><option value="20">20 s</option><option value="30">30 s - safe during long prints</option><option value="60">60 s - safest</option></select><button class="pri" id="sBlkGo">Apply</button></div>
  <div class="hint">How long the board keeps off the SD bus after the printer (or a USB reader) touches the card. The card has one bus and no arbitration line, so the two masters take turns: shorter = uploads squeeze in sooner between printer reads, longer = safer while a print is running. Applies immediately.</div></div>
 <div class="sec"><label>FIRMWARE UPDATE (OTA)</label>
  <div class="frow"><button class="pri" id="bO" style="flex:1">&#9881; Choose firmware .bin &amp; flash</button></div>
  <div class="hint">File is validated before flashing (ESP8266 magic byte + size check on both sides). Running now: <span id="sFw">-</span></div></div>
 <div class="sec" style="border-bottom:0"><label>SYSTEM</label>
  <div id="sSys" style="font-size:12px;color:var(--tx);line-height:2;margin-top:6px">-</div></div>
 <div class="bt"><button id="sClose">Close</button></div>
</div></div>
<div id="fmask"><div>
 <svg width="150" height="150" viewBox="0 0 120 120">
  <defs><linearGradient id="fgrad" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#38e5ff"/><stop offset="1" stop-color="#ff4ded"/></linearGradient></defs>
  <circle cx="60" cy="60" r="52" fill="none" stroke="rgba(56,229,255,.12)" stroke-width="9"/>
  <circle id="farc" cx="60" cy="60" r="52" fill="none" stroke="url(#fgrad)" stroke-width="9" stroke-linecap="round" stroke-dasharray="326.7" stroke-dashoffset="326.7" transform="rotate(-90 60 60)"/>
 </svg>
 <div id="fpct">0%</div>
 <div id="ftime">0.0 s</div>
 <div id="fmsg">Formatting card - do not power off</div>
</div></div>
<div id="toasts"></div>
<input type="file" id="fi" multiple hidden>
<input type="file" id="fo" accept=".bin" hidden>
<script>
'use strict';
var $=function(i){return document.getElementById(i)};
var path='/',busyT=null,uploading=false;
function enc(p){return p.split('/').map(encodeURIComponent).join('/')}
function toast(m,c){var d=document.createElement('div');d.className='toast '+(c||'');d.textContent=m;$('toasts').appendChild(d);setTimeout(function(){d.remove()},3500)}
function fmt(n){if(n<1024)return n+' B';if(n<1048576)return(n/1024).toFixed(1)+' KB';if(n<1073741824)return(n/1048576).toFixed(2)+' MB';return(n/1073741824).toFixed(2)+' GB'}
function icon(n,d){if(d)return'📁';n=n.toLowerCase();if(/\.(gcode|gco|g)$/.test(n))return'🧵';if(/\.(jpg|png|gif|bmp|svg)$/.test(n))return'🖼';if(/\.(zip|gz|7z|rar)$/.test(n))return'🗜';if(/\.(txt|ini|md|json|xml|log)$/.test(n))return'📝';return'📄'}
function ask(title,def){return new Promise(function(res){var m=$('mask'),i=$('dI');$('dT').textContent=title;
 i.style.display=(def===null)?'none':'';i.value=def||'';m.style.display='flex';if(def!==null)setTimeout(function(){i.focus();i.select()},50);
 function done(v){m.style.display='none';$('dO').onclick=$('dC').onclick=i.onkeydown=null;res(v)}
 $('dO').onclick=function(){done(def===null?true:i.value.trim())};
 $('dC').onclick=function(){done(null)};
 i.onkeydown=function(e){if(e.key==='Enter')$('dO').click();if(e.key==='Escape')done(null)}})}
function crumbs(){var c=$('crumbs');c.textContent='';var a=document.createElement('a');a.textContent='🏠 root';a.href='#';a.onclick=function(e){e.preventDefault();nav('/')};c.appendChild(a);
 var parts=path.split('/').filter(Boolean),acc='';
 parts.forEach(function(p){acc+='/'+p;var s=document.createElement('s');s.textContent='/';c.appendChild(s);
  var l=document.createElement('a');l.textContent=p;l.href='#';var t=acc;l.onclick=function(e){e.preventDefault();nav(t)};c.appendChild(l)})}
var sel=new Set(),lastItems=[];
function updateBulk(){$('delN').textContent=sel.size;$('bDel').style.display=sel.size?'':'none'}
function row(it){var r=document.createElement('div');r.className='row';
 var cb=document.createElement('input');cb.type='checkbox';cb.className='ck';
 cb.checked=sel.has(it.n);
 cb.onchange=function(){if(cb.checked)sel.add(it.n);else sel.delete(it.n);updateBulk()};
 r.appendChild(cb);
 var ic=document.createElement('span');ic.className='ic';ic.textContent=icon(it.n,it.d);r.appendChild(ic);
 var base=(path==='/'?'':path)+'/'+it.n;
 if(it.d){var nm=document.createElement('span');nm.className='nm dir';nm.textContent=it.n;nm.onclick=function(){nav(base)};r.appendChild(nm)}
 else{var a=document.createElement('a');a.className='nm';a.textContent=it.n;a.href=enc(base);a.setAttribute('download',it.n);r.appendChild(a)}
 var sz=document.createElement('span');sz.className='sz';sz.textContent=it.d?'-':fmt(it.s);r.appendChild(sz);
 var dt=document.createElement('span');dt.className='dt';dt.textContent=it.t||'';r.appendChild(dt);
 var ac=document.createElement('span');ac.className='act';
 if(!it.d){var bDl=document.createElement('button');bDl.textContent='⬇';bDl.title='Download';
  bDl.onclick=function(){var a=document.createElement('a');a.href=enc(base);a.download=it.n;a.click()};ac.appendChild(bDl)}
 var bR=document.createElement('button');bR.textContent='✏️';bR.title='Rename';bR.onclick=function(){ren(it,base)};ac.appendChild(bR);
 var bX=document.createElement('button');bX.className='del';bX.textContent='🗑️';bX.title='Delete';bX.onclick=function(){del(it,base)};ac.appendChild(bX);
 r.appendChild(ac);return r}
function render(items){var L=$('list');L.textContent='';
 sel.clear();lastItems=items;updateBulk();
 items.sort(function(a,b){return(b.d-a.d)||a.n.localeCompare(b.n)});
 if(!items.length){var e=document.createElement('div');e.className='empty';e.textContent='Empty folder - drag files here to upload';L.appendChild(e);return}
 items.forEach(function(it,i){var r=row(it);r.style.animationDelay=Math.min(i*22,300)+'ms';L.appendChild(r)})}
function busy(on){$('busy').style.display=on?'block':'none';
 if(on&&!busyT)busyT=setTimeout(function(){busyT=null;load()},5000);
 if(!on&&busyT){clearTimeout(busyT);busyT=null}}
function load(){return fetch(enc(path)+'?api=list',{cache:'no-store'}).then(function(r){
  if(r.status===503){var L=$('list');L.textContent='';var e=document.createElement('div');
   e.className='empty';e.textContent='SD card not available';L.appendChild(e);throw 0}
  if(!r.ok)throw new Error('HTTP '+r.status);return r.json()})
 .then(function(j){render(j.items)})
 .catch(function(e){if(e)toast('Load failed: '+e.message,'err')})}
function nav(p){path=p||'/';history.pushState(0,'',enc(path));crumbs();load()}
window.onpopstate=function(){path=decodeURIComponent(location.pathname)||'/';crumbs();load()};
function ren(it,base){ask('Rename "'+it.n+'"',it.n).then(function(v){if(!v||v===it.n)return;
 var dst=(path==='/'?'':path)+'/'+v;
 fetch(enc(base),{method:'MOVE',headers:{Destination:location.origin+enc(dst),Overwrite:'F'}})
 .then(function(r){if(r.status===412)throw new Error('Name already exists');if(!r.ok)throw new Error('HTTP '+r.status);toast('Renamed','ok');load()})
 .catch(function(e){toast(e.message,'err')})})}
function del(it,base){ask(it.d?'Delete folder "'+it.n+'" and ALL its contents ?':'Delete "'+it.n+'" ?',null).then(function(v){if(!v)return;
 fetch(enc(base),{method:'DELETE'}).then(function(r){
  if(!r.ok)throw new Error('Delete failed (HTTP '+r.status+')');
  toast('Deleted','ok');load()}).catch(function(e){toast(e.message,'err')})})}
$('bD').onclick=function(){ask('New folder name','').then(function(v){if(!v)return;
 fetch(enc((path==='/'?'':path)+'/'+v),{method:'MKCOL'}).then(function(r){
  if(!r.ok)throw new Error('HTTP '+r.status);toast('Folder created','ok');load()})
 .catch(function(e){toast(e.message,'err')})})};
$('bR').onclick=function(){load().then(function(){toast('Refreshed','ok')})};
$('bA').onclick=function(){
 var all=lastItems.length>0&&sel.size===lastItems.length;
 sel.clear();
 if(!all)lastItems.forEach(function(it){sel.add(it.n)});
 document.querySelectorAll('#list .ck').forEach(function(cb){cb.checked=!all});
 updateBulk()};
$('bDel').onclick=function(){if(!sel.size)return;
 ask('Delete '+sel.size+' selected item(s)? Folders are removed with ALL their contents.',null).then(function(v){
  if(!v)return;
  var names=Array.from(sel),i=0,okc=0;
  uploading=true;$('up').style.display='block';$('upF').style.width='0%';$('upI').textContent='';
  function next(){
   if(i>=names.length){uploading=false;$('up').style.display='none';
    toast('Deleted '+okc+'/'+names.length+' item(s)',okc===names.length?'ok':'err');load();return}
   var n=names[i++];
   $('upN').textContent='🗑 ['+i+'/'+names.length+'] '+n;
   $('upF').style.width=(i/names.length*100).toFixed(0)+'%';
   fetch(enc((path==='/'?'':path)+'/'+n),{method:'DELETE'})
   .then(function(r){if(r.ok)okc++;next()})
   .catch(function(){next()})}
  next()})};
var curName='',curSsid='',curBaud=115200,curBlk=10,curStat=null;
function sysHtml(j){return '&#128190; Flash/ROM: '+(j.sketch/1024).toFixed(0)+' KB used / '+(j.flashsize/1024).toFixed(0)+' KB &middot; '+(j.flashfree/1024).toFixed(0)+' KB free for OTA<br>'
 +'&#129504; RAM: '+(j.heap/1024).toFixed(1)+'k free &middot; min '+(j.minheap/1024).toFixed(1)+'k &middot; block '+(j.maxblk/1024).toFixed(1)+'k &middot; frag '+j.frag+'%<br>'
 +'&#9201; CPU '+j.cpu+' MHz &middot; uptime '+up2(j.up)}
function openSettings(){$('sName').value=curName;$('sSsid').value=curSsid;$('sPass').value='';
 $('sBaud').value=String(curBaud);
 $('sBlk').value=String(curBlk);
 if(curStat)$('sSys').innerHTML=sysHtml(curStat);
 $('smask').style.display='flex'}
function closeSettings(){$('smask').style.display='none'}
$('bS').onclick=openSettings;
$('sClose').onclick=closeSettings;
$('smask').onclick=function(e){if(e.target===this)closeSettings()};
$('sNameGo').onclick=function(){var v=$('sName').value.trim();
 if(!v||v===curName)return;
 if(!/^[A-Za-z0-9-]{1,23}$/.test(v)){toast('Invalid name - A-Z, 0-9 and dash only','err');return}
 fetch('/?api=name&v='+v,{method:'POST'}).then(function(r){return r.json()}).then(function(j){
  if(!j.ok)throw 0;closeSettings();
  toast('Renamed to '+j.name+' - board restarting...','ok');
  setTimeout(function(){location.reload()},8000)})
 .catch(function(){toast('Rename failed','err')})};
$('sWifiGo').onclick=function(){var ss=$('sSsid').value.trim(),pw=$('sPass').value;
 if(!ss){toast('SSID is required','err');return}
 fetch('/?api=wifi&ssid='+encodeURIComponent(ss)+'&pass='+encodeURIComponent(pw),{method:'POST'})
 .then(function(r){return r.json()}).then(function(j){
  if(!j.ok)throw 0;closeSettings();
  toast('Connecting to "'+ss+'" - board restarting... page reloads in 12 s','ok');
  setTimeout(function(){location.reload()},12000)})
 .catch(function(){toast('Save failed','err')})};
$('sBlkGo').onclick=function(){var s=$('sBlk').value;
 fetch('/?api=blockout&sec='+s,{method:'POST'}).then(function(r){return r.json()}).then(function(j){
  if(!j.ok)throw 0;curBlk=j.blockout;
  toast('Bus blockout now '+j.blockout+' s','ok')})
 .catch(function(){toast('Blockout change failed','err')})};
$('sBaudGo').onclick=function(){var b=$('sBaud').value;
 fetch('/?api=serial&baud='+b,{method:'POST'}).then(function(r){return r.json()}).then(function(j){
  if(!j.ok)throw 0;curBaud=j.baud;
  toast('Serial port now '+j.baud+' baud','ok')})
 .catch(function(){toast('Baud change failed','err')})};
$('bO').onclick=function(){$('fo').click()};
$('fo').onchange=function(){var f=this.files[0];this.value='';if(!f)return;
 if(f.size<102400||f.size>921600){toast('Rejected: not a plausible firmware size ('+fmt(f.size)+')','err');return}
 f.slice(0,1).arrayBuffer().then(function(b){
  if(new Uint8Array(b)[0]!==0xE9){toast('Rejected: not an ESP8266 firmware image (bad magic byte)','err');return}
  ask('Flash firmware "'+f.name+'" ('+fmt(f.size)+')? Board restarts when done.',null).then(function(v){
   if(!v)return;
   closeSettings();
   uploading=true;$('up').style.display='block';
   $('upN').textContent='⚙ Flashing '+f.name;$('upF').style.width='0%';$('upI').textContent='';
   var x=new XMLHttpRequest();x.open('POST','/?api=ota');x.timeout=300000;
   x.upload.onprogress=function(e){if(e.lengthComputable){
    $('upF').style.width=(e.loaded/e.total*100).toFixed(1)+'%';
    $('upI').textContent=fmt(e.loaded)+' / '+fmt(e.total)}};
   function fin(msg,cls){uploading=false;$('up').style.display='none';toast(msg,cls)}
   x.onload=function(){if(x.status<300){fin('Firmware updated - board restarting...','ok');
     setTimeout(function(){location.reload()},9000)}
    else fin('Update failed (HTTP '+x.status+')','err')};
   x.onerror=function(){fin('Update failed (network)','err')};
   x.ontimeout=function(){fin('Update timed out','err')};
   x.send(f)})})};
var fT0=0,fTimer=null;
function ringShow(){$('fmask').style.display='flex';ringPct(0);fT0=Date.now();
 fTimer=setInterval(function(){$('ftime').textContent=((Date.now()-fT0)/1000).toFixed(1)+' s'},100)}
function ringPct(p){p=Math.max(0,Math.min(100,p));
 $('fpct').textContent=Math.round(p)+'%';
 $('farc').style.strokeDashoffset=String((326.7*(1-p/100)).toFixed(1))}
function ringHide(){$('fmask').style.display='none';if(fTimer){clearInterval(fTimer);fTimer=null}}
$('bF').onclick=function(){ask('Type FORMAT to erase ALL data on the card','').then(function(v){
 if(v===null)return;
 if(v!=='FORMAT'){if(v)toast('Not formatted - you must type FORMAT','err');return}
 uploading=true;ringShow();
 fetch('/?api=format&confirm=FORMAT',{method:'POST'}).then(function(r){
  if(!r.ok||!r.body)throw new Error('HTTP '+r.status);
  var rd=r.body.getReader(),dec=new TextDecoder(),acc='',total=0,dots=0;
  function pump(){return rd.read().then(function(x){
   if(x.value){var s=dec.decode(x.value,{stream:true});
    for(var i=0;i<s.length;i++){
     if(s[i]==='.'){dots++;if(total)ringPct(dots/total*100)}
     else acc+=s[i]}
    if(!total){var m=acc.match(/T:(\d+)/);if(m)total=Math.max(1,+m[1])}
    if(acc.indexOf('FAIL')>=0)throw new Error('format failed on board');
    var ok=acc.match(/OK:(\d+)/);
    if(ok){ringPct(100);var secs=(+ok[1]/1000).toFixed(1);
     setTimeout(function(){ringHide();uploading=false;
      toast('Card formatted in '+secs+' s','ok');nav('/')},500);
     return}}
   if(x.done)throw new Error('connection lost');
   return pump()})}
  return pump()})
 .catch(function(e){ringHide();uploading=false;
  toast('Format failed: '+(e&&e.message||''),'err');load()})})};
$('bU').onclick=function(){$('fi').click()};
$('fi').onchange=function(){upload(Array.from(this.files));this.value=''};
function putFile(f,url,st){return new Promise(function(res,rej){var x=new XMLHttpRequest();
 x.open('PUT',url);x.timeout=0;/* no time cap - huge gcode is fine; board aborts stalls itself */
 x.upload.onprogress=function(e){if(!e.lengthComputable)return;
  $('upF').style.width=(e.loaded/e.total*100).toFixed(1)+'%';
  var sp=e.loaded/((Date.now()-st)/1000);
  $('upI').textContent=fmt(e.loaded)+' / '+fmt(e.total)+'  •  '+fmt(sp)+'/s'};
 x.onload=function(){x.status<300?res():rej(new Error('HTTP '+x.status))};
 x.onerror=function(){rej(new Error('network error'))};
 x.ontimeout=function(){rej(new Error('timeout'))};
 x.send(f)})}
function upload(files){if(!files.length||uploading)return;uploading=true;
 $('up').style.display='block';var i=0,okc=0;
 function next(){if(i>=files.length){uploading=false;$('up').style.display='none';
   toast('Uploaded '+okc+'/'+files.length+' files',okc===files.length?'ok':'err');load();return}
  var f=files[i++];$('upN').textContent='⬆ ['+i+'/'+files.length+'] '+f.name;$('upF').style.width='0%';
  putFile(f,enc((path==='/'?'':path)+'/'+f.name),Date.now())
  .then(function(){okc++;next()})
  .catch(function(e){toast(f.name+': '+e.message,'err');next()})}
 next()}
var dragN=0;
document.addEventListener('dragenter',function(e){e.preventDefault();if(++dragN)$('drop').style.display='flex'});
document.addEventListener('dragleave',function(e){e.preventDefault();if(--dragN<=0){dragN=0;$('drop').style.display='none'}});
document.addEventListener('dragover',function(e){e.preventDefault()});
document.addEventListener('drop',function(e){e.preventDefault();dragN=0;$('drop').style.display='none';
 upload(Array.from(e.dataTransfer.files))});
function up2(s){var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);
 return(d?d+'d ':'')+h+'h '+m+'m'}
function status(){if(uploading){setTimeout(status,3000);return}
 fetch('/?api=status',{cache:'no-store'}).then(function(r){return r.json()}).then(function(j){
  $('fw').textContent='FW '+j.fw+(j.build?' · '+j.build:'');
  $('sFw').textContent=j.fw+(j.build?' · '+j.build:'');
  curName=j.name;curSsid=j.ssid||'';curBaud=j.baud||115200;curBlk=j.blockout||10;curStat=j;
  if($('smask').style.display==='flex')$('sSys').innerHTML=sysHtml(j);
  $('cN').textContent=j.name;document.title=j.name+' - SD-WIFI';
  $('cI').textContent=location.hostname;
  var n=j.rssi>=-55?4:j.rssi>=-65?3:j.rssi>=-75?2:j.rssi>=-85?1:0;
  var sg=$('cR');sg.className='sig'+(n<2?' weak':'');$('cRw').title=j.rssi+' dBm';
  for(var i=0;i<4;i++)sg.children[i].className=i<n?'on':'';
  $('cC').textContent=j.sd?((j.cardmb/1024).toFixed(1)+' GB '+j.cardtype):'no card';
  $('fH').textContent=(j.heap/1024).toFixed(1)+'k';$('fM').textContent=(j.minheap/1024).toFixed(1)+'k';
  $('fF').textContent=j.frag+'%';$('fB').textContent=(j.maxblk/1024).toFixed(1)+'k';$('fU').textContent=up2(j.up);
  var g=$('gH'),pc=Math.min(100,j.heap/40960*100);g.style.width=pc+'%';
  g.style.background=j.heap>15000?'var(--ok)':(j.heap>8000?'var(--warn)':'var(--err)');
  var bn=$('busy');
  if(!j.sd){
   bn.className='err';
   bn.textContent='⚠ No SD card inserted - file functions are disabled until a FAT16/FAT32 card is present';
   bn.style.display='block';
   if(busyT){clearTimeout(busyT);busyT=null}
  }
  else if(j.busy){bn.className='';bn.textContent='⚠ Card is in use by another host (USB reader / printer) - retrying automatically';busy(true)}
  else {bn.style.display='none';busy(false)}
  if(j.sd&&hadSd===false){load()}   // card was just inserted - refresh the list
  hadSd=!!j.sd}).catch(function(){}).then(function(){setTimeout(status,3000)})}
var hadSd=null;
path=decodeURIComponent(location.pathname)||'/';if(path!=='/'&&path.endsWith('/'))path=path.slice(0,-1);
crumbs();load();status();
</script></body></html>
)HTML";

#endif
