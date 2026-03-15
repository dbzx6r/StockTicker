#include "WiFiSetup.h"
#include "config.h"
#include "Storage.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

static ESP8266WebServer _server(80);
static DNSServer        _dns;

// ─── Embedded Setup Page (served from PROGMEM) ────────────────────────────
static const char SETUP_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Stock Ticker Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;min-height:100vh;display:flex;align-items:center;justify-content:center}
.card{background:#16213e;border-radius:12px;padding:2rem;width:100%;max-width:440px;box-shadow:0 8px 32px rgba(0,0,0,.5)}
h1{font-size:1.4rem;margin-bottom:1.5rem;text-align:center;color:#e94560}
h2{font-size:.95rem;margin:1.2rem 0 .6rem;color:#a8dadc;text-transform:uppercase;letter-spacing:.08em}
label{display:block;font-size:.85rem;margin-bottom:.2rem;color:#bbb}
select,input[type=text],input[type=password]{width:100%;padding:.6rem .75rem;border-radius:6px;border:1px solid #0f3460;background:#0f3460;color:#eee;font-size:.95rem;margin-bottom:.75rem;appearance:none}
select:focus,input:focus{outline:2px solid #e94560;border-color:#e94560}
.stock-grid{display:grid;grid-template-columns:1fr 1fr;gap:.5rem .75rem;margin-bottom:.75rem}
.stock-grid input{margin-bottom:0;text-transform:uppercase}
button{width:100%;padding:.8rem;background:#e94560;color:#fff;border:none;border-radius:8px;font-size:1rem;font-weight:700;letter-spacing:.05em;cursor:pointer;margin-top:.5rem;transition:background .2s}
button:hover{background:#c73652}
button:disabled{background:#555;cursor:default}
#status{margin-top:.9rem;text-align:center;font-size:.88rem;color:#a8dadc;min-height:1.3em}
.spin{display:inline-block;width:13px;height:13px;border:2px solid #a8dadc;border-top-color:transparent;border-radius:50%;animation:s .7s linear infinite;vertical-align:middle;margin-right:5px}
@keyframes s{to{transform:rotate(360deg)}}
.hint{font-size:.78rem;color:#888;margin-top:-.5rem;margin-bottom:.6rem}
</style>
</head>
<body>
<div class="card">
  <h1>&#128200; Stock Ticker Setup</h1>

  <h2>Wi-Fi Network</h2>
  <label>Network (SSID)
    <select id="ssid"><option value="">&#9203; Scanning…</option></select>
  </label>
  <label>Password
    <input type="password" id="pass" placeholder="Leave blank for open network" autocomplete="off">
  </label>

  <h2>Stock Tickers (1 – 5)</h2>
  <p class="hint">Enter the ticker symbol exactly as shown on Yahoo Finance (e.g. AAPL, TSLA, BRK-B).</p>
  <div class="stock-grid">
    <input type="text" id="s0" placeholder="AAPL" maxlength="9" autocomplete="off">
    <input type="text" id="s1" placeholder="TSLA" maxlength="9" autocomplete="off">
    <input type="text" id="s2" placeholder="MSFT" maxlength="9" autocomplete="off">
    <input type="text" id="s3" placeholder="AMZN" maxlength="9" autocomplete="off">
    <input type="text" id="s4" placeholder="(optional)" maxlength="9" autocomplete="off">
  </div>

  <button id="btn" onclick="save()">Save &amp; Connect</button>
  <div id="status"></div>
</div>

<script>
(function(){
  // Force uppercase in ticker fields
  for(var i=0;i<5;i++){
    document.getElementById('s'+i).addEventListener('input',function(){
      this.value=this.value.toUpperCase();
    });
  }

  // Scan for WiFi networks on load
  fetch('/scan')
    .then(r=>r.json())
    .then(nets=>{
      var sel=document.getElementById('ssid');
      sel.innerHTML='';
      if(!nets||!nets.length){
        sel.innerHTML='<option value="">No networks found — refresh page</option>';
        return;
      }
      nets.forEach(function(n){
        var o=document.createElement('option');
        o.value=o.textContent=n;
        sel.appendChild(o);
      });
    })
    .catch(function(){
      document.getElementById('ssid').innerHTML='<option value="">Scan failed — refresh page</option>';
    });
})();

function save(){
  var ssid=document.getElementById('ssid').value.trim();
  var pass=document.getElementById('pass').value;
  if(!ssid){setStatus('Please select a Wi-Fi network.');return;}

  var stocks=[];
  for(var i=0;i<5;i++){
    var v=document.getElementById('s'+i).value.trim().toUpperCase();
    if(v) stocks.push(v);
  }
  if(!stocks.length){setStatus('Enter at least one stock ticker.');return;}

  document.getElementById('btn').disabled=true;
  setStatus('<span class="spin"></span>Saving configuration…');

  fetch('/save',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ssid:ssid,password:pass,stocks:stocks})
  })
  .then(function(r){
    if(r.ok){
      setStatus('&#10003; Saved! The device is restarting — this page will go offline.');
    } else {
      r.text().then(function(t){setStatus('&#10007; Save failed: '+t);});
      document.getElementById('btn').disabled=false;
    }
  })
  .catch(function(e){
    setStatus('&#10007; Network error: '+e.message);
    document.getElementById('btn').disabled=false;
  });
}

function setStatus(html){document.getElementById('status').innerHTML=html;}
</script>
</body>
</html>
)rawhtml";

// ─── Route Handlers ───────────────────────────────────────────────────────

static void handleRoot() {
    _server.send_P(200, "text/html", SETUP_HTML);
}

static void handleScan() {
    int n = WiFi.scanNetworks();
    DynamicJsonDocument doc(1024);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n; i++) {
        arr.add(WiFi.SSID(i));
    }
    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
    WiFi.scanDelete();
}

static void handleSave() {
    if (!_server.hasArg("plain")) {
        _server.send(400, "text/plain", "Missing body");
        return;
    }

    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    AppConfig cfg{};
    strlcpy(cfg.ssid,     doc["ssid"]     | "", sizeof(cfg.ssid));
    strlcpy(cfg.password, doc["password"] | "", sizeof(cfg.password));

    JsonArrayConst stocks = doc["stocks"].as<JsonArrayConst>();
    cfg.stockCount = 0;
    for (JsonVariantConst v : stocks) {
        if (cfg.stockCount >= MAX_STOCKS) break;
        strlcpy(cfg.stocks[cfg.stockCount++],
                v.as<const char*>(), MAX_TICKER_LEN);
    }

    if (strlen(cfg.ssid) == 0) {
        _server.send(400, "text/plain", "SSID required");
        return;
    }
    if (cfg.stockCount == 0) {
        _server.send(400, "text/plain", "At least one stock required");
        return;
    }

    if (!Storage::saveConfig(cfg)) {
        _server.send(500, "text/plain", "Storage write failed");
        return;
    }

    _server.send(200, "text/plain", "OK");
    delay(500);
    ESP.restart();
}

// Captive portal: redirect all unknown URLs back to the setup page.
static void handleNotFound() {
    _server.sendHeader("Location", "http://192.168.4.1/", true);
    _server.send(302, "text/plain", "");
}

// ─── Public API ───────────────────────────────────────────────────────────

void WiFiSetup::begin(AppConfig& cfg) {
    (void)cfg;

    // AP+STA mode lets us scan for networks while hosting the hotspot
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, strlen(AP_PASS) > 0 ? AP_PASS : nullptr);

    // Redirect all DNS queries to the captive portal IP
    _dns.start(53, "*", IPAddress(192, 168, 4, 1));

    _server.on("/",         HTTP_GET,  handleRoot);
    _server.on("/scan",     HTTP_GET,  handleScan);
    _server.on("/save",     HTTP_POST, handleSave);
    _server.onNotFound(handleNotFound);
    _server.begin();

    Serial.println(F("[WiFiSetup] AP started: " AP_SSID));
}

void WiFiSetup::handle() {
    _dns.processNextRequest();
    _server.handleClient();
}
