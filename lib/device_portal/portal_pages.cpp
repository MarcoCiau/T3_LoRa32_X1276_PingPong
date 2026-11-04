/**
 * @file portal_pages.cpp
 * @brief Card layout shared by the gateway setup pages.
 *
 * Colors and the home / config / update flow follow the provisioning demo.
 * There is no cellular form. The broker host is not collected here.
 */

#include "portal_pages.h"

namespace device_portal {

const char kStyle[] PROGMEM = R"css(
*{box-sizing:border-box;font-family:Arial,sans-serif}
body{margin:0;background:#f0f2f5;min-height:100vh;display:flex;justify-content:center;align-items:center}
.card{background:#fff;padding:2rem;border-radius:12px;box-shadow:0 4px 8px rgba(0,0,0,.1);width:100%;max-width:400px;margin:1rem}
h2{margin:0 0 1rem;text-align:center;color:#002b5c}
.row{margin-bottom:1rem}
.k{font-weight:700;color:#444}
.v{margin-top:.3rem;padding:.5rem;background:#f8f9fa;border:1px solid #ccc;border-radius:6px;color:#002b5c;word-break:break-all}
.ok{color:#28a745}.bad{color:#dc3545}
label{display:block;margin-top:1rem;font-weight:700;color:#444}
input[type=text],input[type=password]{width:100%;padding:.5rem;margin-top:.3rem;border:1px solid #ccc;border-radius:6px;font-size:1rem}
input[type=file]{display:none}
.file{display:block;margin-top:1rem;padding:.7rem;border:1px solid #ccc;border-radius:6px;background:#f8f9fa;cursor:pointer}
.hint{margin:.4rem 0 0;color:#666;font-size:.9rem}
.btn{display:block;width:100%;margin-top:.75rem;background:#002b5c;color:#fff;border:0;border-radius:6px;padding:.7rem;font-size:1rem;text-align:center;text-decoration:none;cursor:pointer}
.btn:hover{background:#0056b3}
.link{display:block;text-align:center;margin-top:1rem;color:#002b5c}
#prg{text-align:center;color:#444;min-height:1.2rem}
#prgbar{background:#f1f1f1;border-radius:10px;margin-top:.5rem;overflow:hidden}
#bar{background:#002b5c;width:0;height:10px}
@media(max-width:480px){.card{padding:1rem;margin:.5rem;border-radius:8px}}
)css";

const char kHome[] PROGMEM = R"html(
<!doctype html><html lang=en><head>
<meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Gateway</title>
<link rel=stylesheet href=/style.css>
</head><body><main class=card>
<h2>Gateway</h2>
<div class=row><div class=k>Device</div><div class=v id=name>Loading...</div></div>
<div class=row><div class=k>MAC address</div><div class=v id=mac>Loading...</div></div>
<div class=row><div class=k>Access point</div><div class=v id=ap>Loading...</div></div>
<div class=row><div class=k>Station</div><div class=v id=sta>Loading...</div></div>
<div class=row><div class=k>Station IP</div><div class=v id=ip>Loading...</div></div>
<div class=row><div class=k>Network</div><div class=v id=ssid>Loading...</div></div>
<div class=row><div class=k>ThingsBoard</div><div class=v id=tb>Loading...</div></div>
<a class=btn href=/config>Device config</a>
<a class=btn href=/ota>Firmware update</a>
</main><script>
function show(id,text,cls){var n=document.getElementById(id);n.textContent=text;if(cls)n.className=cls}
function tick(){fetch('/status').then(function(r){return r.json()}).then(function(d){
show('name',d.name||'—');
show('mac',d.mac||'—');
show('ap',d.ap||'—');
var up=d.sta==='up';
show('sta',up?'Connected':'Disconnected','v '+(up?'ok':'bad'));
show('ip',d.ip||'—');
show('ssid',d.ssid||'—');
show('tb',d.tb==='set'?'Stored':'Not set');
}).catch(function(){})}
tick();setInterval(tick,5000);
</script></body></html>
)html";

const char kConfig[] PROGMEM = R"html(
<!doctype html><html lang=en><head>
<meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Device config</title>
<link rel=stylesheet href=/style.css>
</head><body><main class=card>
<h2>Device config</h2>
<form method=post action=/save>
<label for=s>Network name</label>
<input id=s name=s type=text maxlength=32 required autocomplete=off>
<label for=p>Password</label>
<input id=p name=p type=password maxlength=64 autocomplete=off>
<label for=t>ThingsBoard id</label>
<input id=t name=t type=text maxlength=64 autocomplete=off placeholder="Leave blank to keep the current id">
<p class=hint>The broker address stays in the firmware.</p>
<button class=btn type=submit>Save</button>
</form>
<a class=link href=/>Home</a>
</main></body></html>
)html";

const char kOta[] PROGMEM = R"html(
<!doctype html><html lang=en><head>
<meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Firmware update</title>
<link rel=stylesheet href=/style.css>
</head><body><main class=card>
<h2>Firmware update</h2>
<form id=form>
<label class=file id=pick for=file>Choose a firmware file</label>
<input id=file name=update type=file accept=.bin required>
<button class=btn type=submit>Update</button>
</form>
<p id=prg></p>
<div id=prgbar><div id=bar></div></div>
<a class=link href=/>Home</a>
<script>
var form=document.getElementById('form');
var input=document.getElementById('file');
input.onchange=function(){
  var name=input.files&&input.files[0]?input.files[0].name:'Choose a firmware file';
  document.getElementById('pick').textContent=name;
};
form.onsubmit=function(ev){
  ev.preventDefault();
  var xhr=new XMLHttpRequest();
  xhr.open('POST','/update');
  xhr.upload.onprogress=function(e){
    if(!e.lengthComputable)return;
    var p=Math.round(e.loaded*100/e.total);
    document.getElementById('prg').textContent='Progress: '+p+'%';
    document.getElementById('bar').style.width=p+'%';
  };
  xhr.onload=function(){
    var ok=xhr.status===200&&xhr.responseText==='OK';
    document.getElementById('prg').textContent=ok?'Updated. The gateway is restarting.':'Update failed.';
  };
  xhr.onerror=function(){document.getElementById('prg').textContent='Update failed.'};
  xhr.send(new FormData(form));
};
</script>
</main></body></html>
)html";

const char kSaved[] PROGMEM = R"html(
<!doctype html><html lang=en><head>
<meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Saved</title>
<link rel=stylesheet href=/style.css>
</head><body><main class=card>
<h2>Saved</h2>
<p class=v>The gateway is joining that network. This access point will close.</p>
</main></body></html>
)html";

const char kSsidRequired[] PROGMEM = R"html(
<!doctype html><html lang=en><head>
<meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Device config</title>
<link rel=stylesheet href=/style.css>
</head><body><main class=card>
<h2>Device config</h2>
<p class="v bad">A network name is required.</p>
<a class=btn href=/config>Back</a>
</main></body></html>
)html";

}  // namespace device_portal
