#include "web_server.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <Update.h>
#include <new>
#include "cyd_config.h"
#include "ota_manager.h"
#include "transfer_state.h"

namespace {
const char kIndexHtml[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#101522"><title>ESP32 File Hub</title>
<style>
:root{color-scheme:dark;--bg:#101522;--panel:#192235;--panel2:#202c42;--text:#eaf0fb;--muted:#91a0b8;--accent:#70a7ff;--green:#57d6a0;--red:#ff7583}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,Segoe UI,sans-serif}
header{position:sticky;top:0;background:#101522ed;border-bottom:1px solid #29364d;padding:15px max(18px,calc((100vw - 1060px)/2));display:flex;align-items:center;justify-content:space-between;z-index:2}
h1{font-size:19px;margin:0}button,input,select{font:inherit;color:inherit}button{border:0;border-radius:10px;padding:10px 14px;background:var(--panel2);cursor:pointer}button.primary{background:#386fca}button.danger{background:#643443}
main{max-width:1060px;margin:24px auto;padding:0 18px}.tabs{display:flex;gap:8px;overflow:auto;margin-bottom:20px}.tabs button.active{background:#386fca}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:14px}.card{background:var(--panel);border:1px solid #29364d;border-radius:16px;padding:18px;margin-bottom:14px}.muted{color:var(--muted)}.value{font-size:25px;font-weight:650;margin-top:8px;overflow-wrap:anywhere}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:12px 4px;border-bottom:1px solid #29364d}.row:last-child{border:0}.row .name{overflow-wrap:anywhere}.actions{display:flex;gap:7px;flex-shrink:0}.small{font-size:12px;color:var(--muted)}
input{width:100%;padding:11px 12px;border-radius:9px;border:1px solid #34435d;background:#111a2a;margin:5px 0 12px}label{color:var(--muted);font-size:13px}.form{max-width:520px}.progress{height:10px;border-radius:8px;background:#303d52;overflow:hidden}.progress i{display:block;height:100%;width:0;background:var(--accent);transition:width .15s}.notice{padding:10px 12px;border-radius:9px;background:#26344a;margin:12px 0}.hidden{display:none!important}.drop{border:1px dashed #60789e;border-radius:12px;padding:22px;text-align:center}.drop.over{border-color:var(--accent);background:#26344a}.badge{color:var(--green)}.error{color:var(--red)}
@media(max-width:600px){header{padding:13px 15px}main{margin:16px auto;padding:0 12px}.row{align-items:flex-start}.actions{flex-wrap:wrap}button{padding:9px 11px}}
</style></head><body>
<header><h1>ESP32 File Hub</h1><div><span id="net" class="small">Connecting...</span> <button id="logout" class="hidden">Log out</button></div></header>
<main><section id="login" class="card form hidden"><h2>Sign in</h2><p class="muted">Default login: admin / admin12345</p><form id="loginForm"><label>Username</label><input name="user" autocomplete="username" required><label>Password</label><input name="password" type="password" autocomplete="current-password" required><button class="primary">Sign in</button></form><p id="loginError" class="error"></p></section>
<section id="app" class="hidden"><nav class="tabs"><button data-page="dashboard" class="active">Dashboard</button><button data-page="files">Files</button><button data-page="photos">Photos</button><button data-page="settings">Settings</button><button data-page="ota">Firmware OTA</button></nav>
<section id="dashboard"><div class="grid"><article class="card"><div class="muted">Internal SD</div><div class="value" id="used">—</div><div class="small" id="storageDetail"></div><div class="progress"><i id="storageBar"></i></div></article><article class="card"><div class="muted">Network</div><div class="value" id="ip">—</div><div class="small" id="wifi"></div></article><article class="card"><div class="muted">External SD</div><div class="value" id="externalSd">—</div><div class="small" id="externalInfo">Optional SPI socket</div></article></div><article class="card"><h3>Recent transfer</h3><div id="transferInfo" class="muted">No transfer yet</div><div class="progress"><i id="transferBar"></i></div></article></section>
<section id="files" class="hidden"><article class="card"><div class="row"><button id="up">Up</button><select id="volume"><option value="/">Internal SD</option><option value="/external">External SD</option></select><strong id="folder">/</strong><div><button id="newFolder">New folder</button> <label for="picker"><button class="primary">Upload files</button></label><input id="picker" type="file" multiple hidden></div></div><div id="drop" class="drop">Drop files here to upload</div><p id="fileStatus" class="small"></p><div id="fileList"></div></article></section>
<section id="photos" class="hidden"><article class="card"><h2>Download photos</h2><p class="muted">Browse albums and download JPG, PNG, or BMP photos to this device.</p><div class="row"><button id="photoUp">Up</button><select id="photoVolume"><option value="/">Internal SD</option><option value="/external">External SD</option></select><strong id="photoFolder">/</strong></div><p id="photoStatus" class="small"></p><div id="photoList"></div></article></section>
<section id="settings" class="hidden"><article class="card form"><h2>WiFi settings</h2><p class="muted">Station credentials are stored in device NVS. The setup access point remains available when station mode is enabled.</p><form id="wifiForm"><label>Network name (SSID)</label><input name="ssid" maxlength="32" required><label>Network password</label><input name="password" type="password" maxlength="63" autocomplete="new-password"><button class="primary">Save and connect</button></form><p id="wifiStatus" class="small"></p><button id="apMode">Switch to access-point-only mode</button></article><article class="card form"><h2>Display</h2><label>Brightness: <span id="brightLabel">80%</span></label><input id="brightness" type="range" min="5" max="100" value="80"><button id="saveBrightness">Save brightness</button></article><article class="card"><h2>Device</h2><button id="restart" class="danger">Restart device</button></article></section>
<section id="ota" class="hidden"><article class="card form"><h2>Firmware update</h2><p class="muted">Select a firmware .bin built for this exact device and partition layout. Keep power connected during update.</p><input id="firmware" type="file" accept=".bin,application/octet-stream"><button id="flash" class="primary">Upload firmware</button><p id="otaStatus" class="small"></p><div class="progress"><i id="otaBar"></i></div></article></section></section></main>
<script>
const $=s=>document.querySelector(s), api=async(path,opt={})=>{const r=await fetch(path,{credentials:'same-origin',...opt});if(r.status===401){showLogin();throw Error('Please sign in')}if(!r.ok){let m='Request failed';try{m=(await r.json()).error||m}catch{}throw Error(m)}return r.status===204?null:r.json()};
let currentPath='/',photoPath='/',lastTransfer={};
function showLogin(){ $('#login').classList.remove('hidden');$('#app').classList.add('hidden');$('#logout').classList.add('hidden') }
function showApp(){ $('#login').classList.add('hidden');$('#app').classList.remove('hidden');$('#logout').classList.remove('hidden');refresh();loadFiles('/');loadPhotos('/')}
function page(name){document.querySelectorAll('main section[id]').forEach(x=>{if(['dashboard','files','photos','settings','ota'].includes(x.id))x.classList.toggle('hidden',x.id!==name)});document.querySelectorAll('.tabs button').forEach(b=>b.classList.toggle('active',b.dataset.page===name));if(name==='files')loadFiles(currentPath);if(name==='photos')loadPhotos(photoPath);if(name==='dashboard')refresh()}
document.querySelectorAll('.tabs button').forEach(b=>b.onclick=()=>page(b.dataset.page));
$('#loginForm').onsubmit=async e=>{e.preventDefault();$('#loginError').textContent='';try{const d=new URLSearchParams(new FormData(e.target));await api('/api/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:d});showApp()}catch(x){$('#loginError').textContent=x.message}};
$('#logout').onclick=async()=>{try{await api('/api/logout',{method:'POST'})}finally{showLogin()}};
async function refresh(){try{const d=await api('/api/status');$('#net').textContent=d.wifi.mode+' | '+d.wifi.ip;$('#ip').textContent=d.wifi.ip;$('#wifi').textContent=d.wifi.ssid+(d.wifi.rssi?` | ${d.wifi.rssi} dBm`:'');$('#used').textContent=d.storage.mounted?fmt(d.storage.used):'Unavailable';$('#storageDetail').textContent=d.storage.mounted?`${fmt(d.storage.total-d.storage.used)} free of ${fmt(d.storage.total)}`:'Insert internal MicroSD';$('#storageBar').style.width=d.storage.total?Math.min(100,d.storage.used*100/d.storage.total)+'%':'0%';$('#externalSd').textContent=d.storage.externalMounted?fmt(d.storage.externalUsed):'Not detected';$('#externalInfo').textContent=d.storage.externalMounted?`${fmt(d.storage.externalTotal-d.storage.externalUsed)} free of ${fmt(d.storage.externalTotal)}`:'Optional SPI SD module on CS GPIO22';$('#brightLabel').textContent=d.brightness+'%';$('#brightness').value=d.brightness;if(d.transfer){lastTransfer=d.transfer;$('#transferInfo').textContent=`${d.transfer.status} | ${d.transfer.filename||'-'} | ${d.transfer.percent}%`;$('#transferBar').style.width=d.transfer.percent+'%'}}catch(e){$('#net').textContent=e.message}}
function fmt(n){if(!n)return '0 B';const u=['B','KB','MB','GB','TB'],i=Math.min(u.length-1,Math.floor(Math.log(n)/Math.log(1024)));return (n/1024**i).toFixed(i?1:0)+' '+u[i]}
async function loadFiles(path){currentPath=path;$('#folder').textContent=path;$('#volume').value=path.startsWith('/external')?'/external':'/';$('#fileList').textContent='Loading...';try{const d=await api('/api/files?path='+encodeURIComponent(path));$('#fileList').replaceChildren();d.entries.forEach(it=>{const row=document.createElement('div');row.className='row';const name=document.createElement('span');name.className='name';name.textContent=(it.directory?'[Folder] ':'')+it.name;const meta=document.createElement('span');meta.className='small';meta.textContent=it.directory?'Folder':fmt(it.size);const actions=document.createElement('span');actions.className='actions';if(it.directory){const open=document.createElement('button');open.textContent='Open';open.onclick=()=>loadFiles(it.path);actions.append(open)}else{const down=document.createElement('button');down.textContent='Download';down.onclick=()=>download(it);actions.append(down);const copy=document.createElement('button');copy.textContent='Copy';copy.onclick=()=>transferFile(it,false);actions.append(copy);const move=document.createElement('button');move.textContent='Move';move.onclick=()=>transferFile(it,true);actions.append(move)}const rename=document.createElement('button');rename.textContent='Rename';rename.onclick=()=>renameItem(it);const del=document.createElement('button');del.className='danger';del.textContent='Delete';del.onclick=()=>deleteItem(it);actions.append(rename,del);row.append(name,meta,actions);$('#fileList').append(row)});$('#fileStatus').textContent=`${d.count} entries, ${d.folders} folders${d.truncated?' (showing first 150)':''}`}catch(e){$('#fileList').textContent=e.message}}
async function loadPhotos(path){photoPath=path;$('#photoFolder').textContent=path;$('#photoVolume').value=path.startsWith('/external')?'/external':'/';$('#photoList').textContent='Loading...';try{const d=await api('/api/files?path='+encodeURIComponent(path));$('#photoList').replaceChildren();const entries=d.entries.filter(it=>it.directory||/\.(jpe?g|png|bmp)$/i.test(it.name));entries.forEach(it=>{const row=document.createElement('div');row.className='row';const name=document.createElement('span');name.className='name';name.textContent=(it.directory?'[Album] ':'')+it.name;const meta=document.createElement('span');meta.className='small';meta.textContent=it.directory?'Folder':fmt(it.size);const actions=document.createElement('span');actions.className='actions';const action=document.createElement('button');action.textContent=it.directory?'Open':'Download photo';action.className=it.directory?'':'primary';action.onclick=()=>it.directory?loadPhotos(it.path):download(it);actions.append(action);row.append(name,meta,actions);$('#photoList').append(row)});$('#photoStatus').textContent=`${entries.length} albums/photos in this folder${d.truncated?' (folder listing limited to 150 entries)':''}`;if(!entries.length)$('#photoList').textContent='No albums or supported images in this folder.'}catch(e){$('#photoList').textContent=e.message}}
$('#photoUp').onclick=()=>loadPhotos(photoPath==='/'?'/':photoPath.substring(0,photoPath.lastIndexOf('/'))||'/');
$('#photoVolume').onchange=e=>loadPhotos(e.target.value);
$('#up').onclick=()=>loadFiles(currentPath==='/'?'/':currentPath.substring(0,currentPath.lastIndexOf('/'))||'/');
$('#volume').onchange=e=>loadFiles(e.target.value);
async function transferFile(it,move){const other=currentPath.startsWith('/external')?'/':'/external';const dest=prompt('Destination path on '+(other==='/external'?'external':'internal')+' storage',other+'/'+it.name);if(!dest)return;try{await api('/api/'+(move?'move':'copy')+'?from='+encodeURIComponent(it.path)+'&to='+encodeURIComponent(dest),{method:'POST'});loadFiles(currentPath)}catch(e){alert(e.message)}}
async function deleteItem(it){if(!confirm(`Delete ${it.name}? Folders must be empty.`))return;try{await api('/api/delete?path='+encodeURIComponent(it.path),{method:'POST'});loadFiles(currentPath)}catch(e){alert(e.message)}}
async function renameItem(it){const name=prompt('New name',it.name);if(!name||name===it.name)return;const parent=currentPath==='/'?'':currentPath;try{await api('/api/rename?from='+encodeURIComponent(it.path)+'&to='+encodeURIComponent(parent+'/'+name),{method:'POST'});loadFiles(currentPath)}catch(e){alert(e.message)}}
$('#newFolder').onclick=async()=>{const name=prompt('Folder name');if(!name)return;try{await api('/api/mkdir?path='+encodeURIComponent((currentPath==='/'?'':currentPath)+'/'+name),{method:'POST'});loadFiles(currentPath)}catch(e){alert(e.message)}};
function upload(file){return new Promise(resolve=>{const xhr=new XMLHttpRequest();xhr.open('POST','/api/upload?path='+encodeURIComponent(currentPath));xhr.upload.onprogress=e=>{if(e.lengthComputable){$('#fileStatus').textContent=`Uploading ${file.name}: ${Math.round(e.loaded*100/e.total)}%`;reportTransfer(file.name,e.loaded,e.total,true)}};xhr.onload=()=>{try{const d=JSON.parse(xhr.responseText);if(xhr.status>=200&&xhr.status<300){$('#fileStatus').textContent=`Uploaded ${file.name}`;reportTransfer(file.name,file.size,file.size,true,true);resolve(true)}else{throw Error(d.error||'Upload failed')}}catch(e){$('#fileStatus').textContent=e.message;resolve(false)}};xhr.onerror=()=>{ $('#fileStatus').textContent='Network error during upload';resolve(false)};const f=new FormData();f.append('file',file,file.name);xhr.send(f)})}
async function reportTransfer(name,done,total,upload,complete=false){try{await fetch('/api/transfer?name='+encodeURIComponent(name)+'&done='+done+'&total='+total+'&upload='+(upload?1:0)+'&complete='+(complete?1:0),{method:'POST',credentials:'same-origin'})}catch{}}
$('#picker').onchange=async e=>{for(const f of e.target.files)await upload(f);e.target.value='';loadFiles(currentPath)};
const drop=$('#drop');drop.ondragover=e=>{e.preventDefault();drop.classList.add('over')};drop.ondragleave=()=>drop.classList.remove('over');drop.ondrop=async e=>{e.preventDefault();drop.classList.remove('over');for(const f of e.dataTransfer.files)await upload(f);loadFiles(currentPath)};
function download(it){const xhr=new XMLHttpRequest();xhr.open('GET','/download?file='+encodeURIComponent(it.path));xhr.withCredentials=true;xhr.responseType='blob';xhr.onprogress=e=>{if(e.lengthComputable)reportTransfer(it.name,e.loaded,e.total,false)};xhr.onload=()=>{if(xhr.status!==200){alert(xhr.status===401?'Please sign in again':'Download failed');return}const url=URL.createObjectURL(xhr.response);const a=document.createElement('a');a.href=url;a.download=it.name;document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);reportTransfer(it.name,xhr.response.size,xhr.response.size,false,true)};xhr.onerror=()=>alert('Network error during download');xhr.send()}
$('#wifiForm').onsubmit=async e=>{e.preventDefault();const p=new URLSearchParams(new FormData(e.target));try{await api('/api/settings/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});$('#wifiStatus').textContent='Saved; connecting to network...';setTimeout(refresh,1000)}catch(x){$('#wifiStatus').textContent=x.message}};
$('#apMode').onclick=async()=>{try{await api('/api/settings/ap',{method:'POST'});$('#wifiStatus').textContent='Access-point mode enabled'}catch(e){alert(e.message)}};
$('#brightness').oninput=e=>$('#brightLabel').textContent=e.target.value+'%';$('#saveBrightness').onclick=async()=>{try{await api('/api/settings/brightness?value='+$('#brightness').value,{method:'POST'});refresh()}catch(e){alert(e.message)}};
$('#restart').onclick=async()=>{if(confirm('Restart the device?')){try{await api('/api/restart',{method:'POST'});$('#wifiStatus').textContent='Restarting...'}catch(e){alert(e.message)}}};
$('#flash').onclick=()=>{const file=$('#firmware').files[0];if(!file){$('#otaStatus').textContent='Choose a .bin file first';return}if(!confirm('Install this firmware and restart?'))return;const x=new XMLHttpRequest();x.open('POST','/api/ota');x.upload.onprogress=e=>{if(e.lengthComputable){$('#otaBar').style.width=Math.round(e.loaded*100/e.total)+'%';$('#otaStatus').textContent=`Uploading ${Math.round(e.loaded*100/e.total)}%`}};x.onload=()=>$('#otaStatus').textContent=x.status===200?'Update installed; device is restarting':x.responseText;x.onerror=()=>$('#otaStatus').textContent='Connection lost during update';const f=new FormData();f.append('firmware',file,file.name);x.send(f)};
async function boot(){try{const d=await api('/api/status');if(d.authenticated)showApp();else showLogin()}catch(e){showLogin()}}boot();setInterval(()=>{if(!$('#app').classList.contains('hidden'))refresh()},4000);
</script></body></html>
)HTML";

struct UploadContext {
  String error;
  String filename;
  size_t received = 0;
  size_t total = 0;
  bool started = false;
  bool finished = false;
};

struct OtaContext {
  String error;
  size_t received = 0;
  size_t total = 0;
  bool started = false;
  bool finished = false;
};

String jsonError(const String& message) {
  DynamicJsonDocument doc(256);
  doc["error"] = message;
  String body;
  serializeJson(doc, body);
  return body;
}

bool safeValue(const String& value, size_t maxLength) {
  if (value.isEmpty() || value.length() > maxLength) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = value[i];
    if (c < 32 || c == 127) return false;
  }
  return true;
}

String fileNameOnly(String name) {
  name.replace('\\', '/');
  const int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);
  return name;
}
}  // namespace

WebServerManager::WebServerManager(FileManager& files, WifiManager& wifi,
                                   DisplayManager& display)
    : files_(files), wifi_(wifi), display_(display) {}

String WebServerManager::createToken() {
  char token[33];
  snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx",
           static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  return String(token);
}

void WebServerManager::begin() {
  Preferences preferences;
  preferences.begin("filehub-auth", false);
  preferences.putString("user", AppConfig::kDefaultDashboardUser);
  preferences.putString("pass", AppConfig::kDefaultDashboardPassword);
  preferences.end();
  Serial.printf("[AUTH] Default dashboard username: %s\n",
                AppConfig::kDefaultDashboardUser);
  registerRoutes();
  server_.begin();
  Serial.println("[WEB] Dashboard listening on port 80");
  Serial.printf("[WEB] Connect to the device AP and open %s\n",
                wifi_.accessPointUrl().c_str());
}

bool WebServerManager::validSession(AsyncWebServerRequest* request) {
  if (sessionToken_.isEmpty() || !request->hasHeader("Cookie")) return false;
  const String cookie = request->getHeader("Cookie")->value();
  const String key = "hub_session=";
  int position = cookie.indexOf(key);
  while (position >= 0) {
    const bool boundary = position == 0 || cookie[position - 1] == ';' ||
                          cookie[position - 1] == ' ';
    const int valueStart = position + key.length();
    int valueEnd = cookie.indexOf(';', valueStart);
    if (valueEnd < 0) valueEnd = cookie.length();
    if (boundary && cookie.substring(valueStart, valueEnd) == sessionToken_)
      break;
    position = cookie.indexOf(key, position + key.length());
  }
  if (position < 0) return false;
  if (millis() - lastActivity_ > AppConfig::kSessionTimeoutMs) {
    sessionToken_ = "";
    return false;
  }
  lastActivity_ = millis();
  return true;
}

bool WebServerManager::authorize(AsyncWebServerRequest* request) {
  if (validSession(request)) return true;
  AsyncWebServerResponse* response =
      request->beginResponse(401, "application/json", jsonError("Authentication required"));
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
  return false;
}

void WebServerManager::expireSession() {
  if (!sessionToken_.isEmpty() &&
      millis() - lastActivity_ > AppConfig::kSessionTimeoutMs) {
    sessionToken_ = "";
    Serial.println("[AUTH] Idle session expired");
  }
}

void WebServerManager::registerRoutes() {
  if (routesRegistered_) return;
  routesRegistered_ = true;
  server_.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response =
        request->beginResponse(200, "text/html; charset=utf-8",
                               reinterpret_cast<const uint8_t*>(kIndexHtml),
                               sizeof(kIndexHtml) - 1);
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Content-Type-Options", "nosniff");
    response->addHeader("X-Frame-Options", "DENY");
    response->addHeader("Referrer-Policy", "no-referrer");
    request->send(response);
  });

  auto copyMoveHandler = [this](AsyncWebServerRequest* request, bool move) {
    if (!authorize(request)) return;
    if (!request->hasParam("from") || !request->hasParam("to")) {
      request->send(400, "application/json",
                    jsonError("Missing source or destination"));
      return;
    }
    String error;
    if (!files_.copyPath(request->getParam("from")->value(),
                         request->getParam("to")->value(), move, error))
      request->send(400, "application/json", jsonError(error));
    else
      request->send(200, "application/json", "{\"ok\":true}");
  };
  server_.on("/api/copy", HTTP_POST,
             [copyMoveHandler](AsyncWebServerRequest* request) {
               copyMoveHandler(request, false);
             });
  server_.on("/api/move", HTTP_POST,
             [copyMoveHandler](AsyncWebServerRequest* request) {
               copyMoveHandler(request, true);
             });

  server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(1024);
    const bool authenticated = validSession(request);
    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["mode"] = wifi_.modeName();
    wifi["ip"] = wifi_.ipAddress();
    wifi["connected"] = wifi_.connected();
    JsonObject storage = doc.createNestedObject("storage");
    storage["mounted"] = authenticated && files_.isMounted();
    storage["total"] = authenticated ? files_.totalBytes() : 0;
    storage["used"] = authenticated ? files_.usedBytes() : 0;
    storage["externalMounted"] = authenticated && files_.isMounted(true);
    storage["externalTotal"] = authenticated ? files_.totalBytes(true) : 0;
    storage["externalUsed"] = authenticated ? files_.usedBytes(true) : 0;
    doc["brightness"] = display_.brightness();
    doc["authenticated"] = authenticated;
    const TransferState transfer = getTransferState();
    JsonObject tx = doc.createNestedObject("transfer");
    if (authenticated) {
      wifi["ssid"] = wifi_.ssid();
      wifi["rssi"] = wifi_.rssi();
      tx["filename"] = transfer.filename;
      tx["status"] = transfer.status;
      tx["active"] = transfer.active;
      tx["upload"] = transfer.upload;
      tx["transferred"] = transfer.transferred;
      tx["total"] = transfer.total;
      const uint64_t rawPercent =
          transfer.total
              ? static_cast<uint64_t>(transfer.transferred) * 100 /
                    transfer.total
              : 0;
      tx["percent"] = rawPercent > 100 ? 100 : rawPercent;
    }
    String body;
    serializeJson(doc, body);
    request->send(200, "application/json", body);
  });

  server_.on("/api/login", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!request->hasParam("user", true) || !request->hasParam("password", true)) {
      request->send(400, "application/json", jsonError("Missing credentials"));
      return;
    }
    const String user = request->getParam("user", true)->value();
    const String password = request->getParam("password", true)->value();
    Preferences preferences;
    preferences.begin("filehub-auth", true);
    const String storedUser = preferences.getString("user", "");
    const String storedPassword = preferences.getString("pass", "");
    preferences.end();
    if (user != storedUser || password != storedPassword) {
      request->send(401, "application/json", jsonError("Invalid username or password"));
      return;
    }
    sessionToken_ = createToken();
    lastActivity_ = millis();
    AsyncWebServerResponse* response =
        request->beginResponse(200, "application/json", "{\"ok\":true}");
    response->addHeader("Set-Cookie",
                        "hub_session=" + sessionToken_ +
                            "; Path=/; HttpOnly; SameSite=Strict");
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

  server_.on("/api/logout", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authorize(request)) return;
    sessionToken_ = "";
    AsyncWebServerResponse* response =
        request->beginResponse(200, "application/json", "{\"ok\":true}");
    response->addHeader("Set-Cookie",
                        "hub_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    request->send(response);
  });

  server_.on("/api/files", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (!authorize(request)) return;
    const String path =
        request->hasParam("path") ? request->getParam("path")->value() : "/";
    bool ok = false;
    String error;
    const String response = files_.listJson(path, ok, error);
    if (!ok) request->send(400, "application/json", jsonError(error));
    else request->send(200, "application/json", response);
  });

  server_.on("/api/delete", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authorize(request)) return;
    if (!request->hasParam("path")) {
      request->send(400, "application/json", jsonError("Missing path"));
      return;
    }
    String error;
    if (!files_.removePath(request->getParam("path")->value(), error))
      request->send(400, "application/json", jsonError(error));
    else
      request->send(200, "application/json", "{\"ok\":true}");
  });

  server_.on("/api/rename", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authorize(request)) return;
    if (!request->hasParam("from") || !request->hasParam("to")) {
      request->send(400, "application/json", jsonError("Missing source or destination"));
      return;
    }
    String error;
    if (!files_.renamePath(request->getParam("from")->value(),
                           request->getParam("to")->value(), error))
      request->send(400, "application/json", jsonError(error));
    else
      request->send(200, "application/json", "{\"ok\":true}");
  });

  server_.on("/api/mkdir", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authorize(request)) return;
    if (!request->hasParam("path")) {
      request->send(400, "application/json", jsonError("Missing path"));
      return;
    }
    String error;
    if (!files_.makeDirectory(request->getParam("path")->value(), error))
      request->send(400, "application/json", jsonError(error));
    else
      request->send(200, "application/json", "{\"ok\":true}");
  });

  server_.on("/api/upload", HTTP_POST,
             [this](AsyncWebServerRequest* request) {
               if (!authorize(request)) return;
               UploadContext* context =
                   static_cast<UploadContext*>(request->_tempObject);
               if (context == nullptr || !context->finished) {
                 request->send(400, "application/json", jsonError("Incomplete upload"));
               } else if (!context->error.isEmpty()) {
                 request->send(500, "application/json", jsonError(context->error));
               } else {
                 request->send(200, "application/json", "{\"ok\":true}");
               }
               delete context;
               request->_tempObject = nullptr;
             },
             [this](AsyncWebServerRequest* request, String filename, size_t index,
                    uint8_t* data, size_t len, bool final) {
               handleUpload(request, filename, index, data, len, final);
             });

  auto downloadHandler = [this](AsyncWebServerRequest* request,
                                const char* parameter) {
    if (!authorize(request)) return;
    if (!request->hasParam(parameter)) {
      request->send(400, "application/json",
                    jsonError(String("Missing ") + parameter));
      return;
    }
    String path;
    if (!FileManager::normalizePath(request->getParam(parameter)->value(), path) ||
        path == "/") {
      request->send(400, "application/json", jsonError("Invalid path"));
      return;
    }
    String physicalPath;
    String error;
    FS* volume = nullptr;
    if (!files_.beginFileRead(path, volume, physicalPath, error)) {
      request->send(503, "application/json", jsonError(error));
      return;
    }
    if (volume == nullptr || !volume->exists(physicalPath)) {
      files_.endFileRead();
      request->send(404, "application/json", jsonError("File not found"));
      return;
    }
    File file = volume->open(physicalPath, FILE_READ);
    if (!file || file.isDirectory()) {
      if (file) file.close();
      files_.endFileRead();
      request->send(404, "application/json", jsonError("File not found"));
      return;
    }
    const size_t size = file.size();
    String filename = fileNameOnly(physicalPath);
    String disposition = "attachment; filename=\"";
    for (size_t i = 0; i < filename.length(); ++i) {
      const char c = filename[i];
      if (c == '"' || c == '\\') disposition += '\\';
      if (c >= 32 && c != 127) disposition += c;
    }
    disposition += '"';
    AsyncWebServerResponse* response =
        request->beginResponse(file, physicalPath,
                               FileManager::contentType(physicalPath), false);
    if (response == nullptr) {
      file.close();
      files_.endFileRead();
      setTransferState(filename.c_str(), "Failed", 0, size, false, false);
      request->send(500, "application/json", jsonError("Unable to stream file"));
      Serial.printf("[Download] Failed to create stream: %s\n", path.c_str());
      return;
    }
    setTransferState(filename.c_str(), "Downloading", 0, size, true, false);
    files_.endFileRead();
    response->addHeader("Content-Disposition", disposition);
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Content-Type-Options", "nosniff");
    request->send(response);
    Serial.printf("[Download] Streaming %s (%u bytes)\n", path.c_str(),
                  static_cast<unsigned>(size));
  };
  server_.on("/api/download", HTTP_GET, [downloadHandler](AsyncWebServerRequest* request) {
    downloadHandler(request, "path");
  });
  server_.on("/download", HTTP_GET, [downloadHandler](AsyncWebServerRequest* request) {
    downloadHandler(request, "file");
  });

  server_.on("/api/transfer", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authorize(request)) return;
    if (!request->hasParam("name") || !request->hasParam("done") ||
        !request->hasParam("total")) {
      request->send(400, "application/json", jsonError("Invalid transfer update"));
      return;
    }
    const String name = request->getParam("name")->value();
    const uint32_t done = request->getParam("done")->value().toInt();
    const uint32_t total = request->getParam("total")->value().toInt();
    const bool upload = request->hasParam("upload") &&
                        request->getParam("upload")->value() == "1";
    const bool complete = request->hasParam("complete") &&
                          request->getParam("complete")->value() == "1";
    setTransferState(name.c_str(), complete ? "Complete" : "Transferring",
                     done, total, !complete, upload);
    request->send(204);
  });

  server_.on("/api/settings/wifi", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authorize(request)) return;
    if (!request->hasParam("ssid", true)) {
      request->send(400, "application/json", jsonError("Missing SSID"));
      return;
    }
    const String ssid = request->getParam("ssid", true)->value();
    const String password = request->hasParam("password", true)
                                ? request->getParam("password", true)->value()
                                : "";
    String error;
    if (!wifi_.configureStation(ssid, password, error))
      request->send(400, "application/json", jsonError(error));
    else
      request->send(200, "application/json", "{\"ok\":true}");
  });

  server_.on("/api/settings/ap", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authorize(request)) return;
    String error;
    if (!wifi_.startAccessPoint(error))
      request->send(500, "application/json", jsonError(error));
    else
      request->send(200, "application/json", "{\"ok\":true}");
  });

  server_.on("/api/settings/brightness", HTTP_POST,
             [this](AsyncWebServerRequest* request) {
               if (!authorize(request)) return;
               if (!request->hasParam("value")) {
                 request->send(400, "application/json", jsonError("Missing brightness"));
                 return;
               }
               const int brightness = request->getParam("value")->value().toInt();
               if (brightness < 5 || brightness > 100) {
                 request->send(400, "application/json", jsonError("Brightness must be 5-100"));
                 return;
               }
               display_.setBrightness(brightness);
               Preferences prefs;
               prefs.begin("filehub", false);
               prefs.putUChar("brightness", brightness);
               prefs.end();
               request->send(200, "application/json", "{\"ok\":true}");
             });

  server_.on("/api/restart", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authorize(request)) return;
    restartAt_ = millis() + 1500;
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server_.on("/api/ota", HTTP_POST,
             [this](AsyncWebServerRequest* request) {
               if (!authorize(request)) return;
               OtaContext* context = static_cast<OtaContext*>(request->_tempObject);
               if (context == nullptr || !context->finished) {
                 request->send(400, "application/json", jsonError("Incomplete update"));
               } else if (!context->error.isEmpty()) {
                 request->send(500, "application/json", jsonError(context->error));
               } else {
                 request->send(200, "application/json", "{\"ok\":true}");
                 restartAt_ = millis() + 1500;
               }
               delete context;
               request->_tempObject = nullptr;
             },
             [this](AsyncWebServerRequest* request, String filename, size_t index,
                    uint8_t* data, size_t len, bool final) {
               handleOta(request, filename, index, data, len, final);
             });

  server_.onNotFound([](AsyncWebServerRequest* request) {
    request->redirect("/");
  });
}

void WebServerManager::handleUpload(AsyncWebServerRequest* request,
                                    String filename, size_t index,
                                    uint8_t* data, size_t len, bool final) {
  UploadContext* context = static_cast<UploadContext*>(request->_tempObject);
  if (context == nullptr) {
    context = new (std::nothrow) UploadContext();
    request->_tempObject = context;
    if (context == nullptr) return;
  }
  if (index == 0) {
    context->total = request->contentLength();
    filename = fileNameOnly(filename);
    context->filename = filename;
    if (!safeValue(filename, 64)) context->error = "Invalid filename";
    else if (!validSession(request)) context->error = "Authentication required";
    else if (!request->hasParam("path")) context->error = "Missing folder path";
    else {
      String folder;
      if (!FileManager::normalizePath(request->getParam("path")->value(), folder)) {
        context->error = "Invalid folder path";
      } else {
        const String destination =
            folder == "/" ? "/" + filename : folder + "/" + filename;
        if (files_.beginUpload(destination, context->error)) {
          context->started = true;
          setTransferState(filename.c_str(), "Uploading", 0, context->total, true,
                           true);
        }
      }
    }
  }
  if (context->error.isEmpty() && len > 0) {
    if (files_.writeUpload(data, len, context->error)) context->received += len;
    setTransferState(context->filename.c_str(), context->error.isEmpty()
                                                    ? "Uploading"
                                                    : "Failed",
                     context->received, context->total, context->error.isEmpty(),
                     true);
  }
  if (final) {
    if (context->started) {
      String finishError;
      if (!files_.finishUpload(finishError) && context->error.isEmpty())
        context->error = finishError;
    }
    if (context->started && !context->error.isEmpty()) files_.abortUpload();
    context->finished = true;
    if (context->started) {
      setTransferState(context->filename.c_str(),
                       context->error.isEmpty() ? "Complete" : "Failed",
                       context->received, context->total, false, true);
    }
  }
}

void WebServerManager::handleOta(AsyncWebServerRequest* request,
                                 String filename, size_t index, uint8_t* data,
                                 size_t len, bool final) {
  OtaContext* context = static_cast<OtaContext*>(request->_tempObject);
  if (context == nullptr) {
    context = new (std::nothrow) OtaContext();
    request->_tempObject = context;
    if (context == nullptr) return;
  }
  if (index == 0) {
    context->total = request->contentLength();
    if (!validSession(request)) context->error = "Authentication required";
    else if (!filename.endsWith(".bin")) context->error = "Firmware must be a .bin file";
    else if (context->total > 0 && context->total > 4 * 1024 * 1024)
      context->error = "Firmware upload exceeds 4 MiB limit";
    else if (otaInProgress_) {
      context->error = "Another firmware update is active";
    } else {
      otaInProgress_ = true;
      if (OtaManager::begin(0, context->error)) {
        context->started = true;
        setTransferState(filename.c_str(), "Updating", 0, context->total, true,
                         true);
      } else {
        otaInProgress_ = false;
      }
    }
  }
  if (context->error.isEmpty() && len > 0) {
    if (OtaManager::write(data, len, context->error)) context->received += len;
    setTransferState(filename.c_str(), context->error.isEmpty() ? "Updating" : "Failed",
                     context->received, context->total, context->error.isEmpty(),
                     true);
  }
  if (final) {
    if (context->started && context->error.isEmpty() &&
        !OtaManager::finish(context->error)) {
      context->error = context->error.isEmpty() ? "Firmware finalization failed"
                                                : context->error;
    } else if (context->started && !context->error.isEmpty()) {
      OtaManager::abort();
    }
    context->finished = true;
    if (context->started) {
      otaInProgress_ = false;
      setTransferState(filename.c_str(),
                       context->error.isEmpty() ? "Complete" : "Failed",
                       context->received, context->total, false, true);
    }
  }
}

void WebServerManager::tick() {
  expireSession();
  const TransferState transfer = getTransferState();
  if (transfer.active && !transfer.upload && transfer.lastUpdateAt != 0 &&
      millis() - transfer.lastUpdateAt > 30000) {
    setTransferState(transfer.filename, "Timed out", transfer.transferred,
                     transfer.total, false, false);
    Serial.println("[Download] Transfer timed out; releasing SD bus");
  }
  if (restartAt_ != 0 && static_cast<int32_t>(millis() - restartAt_) >= 0) {
    Serial.println("[SYS] Restarting");
    ESP.restart();
  }
}
