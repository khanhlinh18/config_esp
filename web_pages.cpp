#include "web_config.h"
#include "web_dashboard.h"

const char* htmlForm = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 Config</title>
<style>
  * { box-sizing: border-box; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
  body { margin: 0; background: #f2f4f7; display: flex; justify-content: center; align-items: center; height: 100vh; }
  .container { width: 100%; max-width: 400px; padding: 20px; }
  .card { background: #fff; border-radius: 16px; padding: 24px; box-shadow: 0 4px 20px rgba(0,0,0,0.08); }
  h2 { text-align: center; margin-bottom: 20px; font-size: 22px; font-weight: 600; color: #333; }
  #status { text-align: center; font-size: 14px; margin-bottom: 15px; padding: 10px; border-radius: 8px; background: #eef2ff; color: #4f46e5; }
  label { font-size: 13px; color: #555; margin-top: 10px; display: block; }
  input, select { width: 100%; height: 45px; margin-top: 6px; margin-bottom: 10px; padding: 0 12px; border-radius: 10px; border: 1px solid #ddd; font-size: 14px; outline: none; transition: 0.2s; }
  input:focus, select:focus { border-color: #4f46e5; }
  .btn { width: 100%; height: 48px; border: none; border-radius: 12px; background: #4f46e5; color: white; font-size: 15px; font-weight: 500; cursor: pointer; margin-top: 10px; }
  .btn:active { transform: scale(0.98); }
  #wifi-list { display: none; max-height: 140px; overflow-y: auto; border-radius: 10px; background: #f9fafb; border: 1px solid #eee; margin-bottom: 10px; }
  .ssid-item { padding: 10px; font-size: 14px; border-bottom: 1px solid #eee; cursor: pointer; }
  .ssid-item:hover { background: #eef2ff; }
</style>
</head>
<body>
<div class="container"><div class="card">
  <h2>ESP32 Config</h2>
  <div id="status">Ready</div>
  <form action="/save" onsubmit="setStatus('Saving...')">
    <label>WiFi</label>
    <input type="text" id="ssid" name="ssid" placeholder="Tap to scan WiFi" onclick="scanWifi()" required>
    <div id="wifi-list"></div>
    <label>Password</label>
    <input type="text" name="pass" placeholder="Enter password">
    <label>MQTT Server</label>
    <input type="text" name="mqtt_srv" value="broker.emqx.io">
    <label>MQTT Port</label>
    <select name="mqtt_port">
      <option value="1883">1883</option>
      <option value="8883">8883</option>
    </select>
    <button class="btn">Save</button>
  </form>
</div></div>
<script>
function setStatus(text) { document.getElementById('status').innerText = text; }
function scanWifi() {
  setStatus('Scanning...');
  let list = document.getElementById('wifi-list');
  list.style.display = 'block'; list.innerHTML = 'Loading...';
  fetch('/scan').then(res => res.json()).then(data => {
    data.sort((a, b) => b.rssi - a.rssi);
    list.innerHTML = '';
    data.forEach(item => {
      let div = document.createElement('div');
      div.className = 'ssid-item';
      div.innerHTML = item.ssid + ' (' + item.rssi + 'dBm)';
      div.onclick = () => { document.getElementById('ssid').value = item.ssid; list.style.display = 'none'; setStatus('Selected: ' + item.ssid); };
      list.appendChild(div);
    });
    setStatus('Scan done');
  }).catch(() => setStatus('Scan error'));
}
</script>
</body></html>
)rawliteral";


const char* htmlDashboard = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Control Panel</title>
    <style>
        :root {
            --primary: #2563eb;
            --bg: #f8fafc;
            --card-bg: #ffffff;
            --text-main: #1e293b;
            --text-sub: #64748b;
            --success: #10b981;
            --danger: #ef4444;
            --border: #e2e8f0;
        }

        body { 
            font-family: 'Inter', 'Segoe UI', Tahoma, sans-serif; 
            background: var(--bg); 
            color: var(--text-main);
            margin: 0; 
            padding: 20px; 
            line-height: 1.5;
        }

        h2 { color: var(--text-main); font-weight: 700; margin-bottom: 25px; letter-spacing: -0.5px; }
        h3 { margin-top: 0; color: var(--primary); font-size: 1.1rem; margin-bottom: 20px; }

        .card { 
            background: var(--card-bg); 
            padding: 24px; 
            border-radius: 16px; 
            box-shadow: 0 10px 15px -3px rgba(0, 0, 0, 0.05), 0 4px 6px -2px rgba(0, 0, 0, 0.05);
            margin-bottom: 24px; 
            border: 1px solid rgba(255,255,255,0.8);
        }

        /* System Info Grid */
        .info-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 16px; }
        .info-item { 
            background: #ffffff; 
            padding: 15px; 
            border-radius: 12px; 
            border: 1px solid var(--border);
            transition: all 0.2s;
        }
        .info-item:hover { border-color: var(--primary); transform: translateY(-2px); }
        .info-item b { display: block; font-size: 11px; color: var(--text-sub); text-transform: uppercase; margin-bottom: 4px; }
        .info-item span { font-weight: 600; font-family: 'Monaco', monospace; font-size: 0.95rem; }

        /* Buttons */
        .btn { 
            padding: 10px 20px; border: none; border-radius: 10px; cursor: pointer; 
            font-weight: 600; transition: all 0.2s; font-size: 14px;
            display: inline-flex; align-items: center; gap: 8px;
        }
        .btn-add { background: var(--success); color: white; margin-bottom: 15px; }
        .btn-save { background: var(--primary); color: white; width: 100%; margin-top: 25px; font-size: 16px; justify-content: center; }
        .btn-del { background: #fee2e2; color: var(--danger); padding: 6px 12px; font-size: 12px; }
        .btn:hover { filter: brightness(1.1); transform: translateY(-1px); }
        .btn:active { transform: translateY(0); }

        /* Slave Units */
        .slave-unit { 
            border: 1px solid var(--border); 
            border-radius: 14px; 
            padding: 20px; 
            margin-bottom: 20px; 
            background: #fafafa; 
        }
        .unit-header { 
            display: flex; justify-content: space-between; align-items: flex-start; 
            margin-bottom: 20px; padding-bottom: 15px; border-bottom: 1px dashed var(--border); 
        }
        .header-inputs { display: flex; flex-wrap: wrap; gap: 15px; align-items: center; flex: 1; }
        .input-group { display: flex; align-items: center; gap: 6px; }
        .header-inputs b { font-size: 13px; color: var(--text-sub); }

        /* Form Elements */
        input, select { 
            padding: 8px 12px; border: 1px solid var(--border); border-radius: 8px; 
            outline: none; transition: border 0.2s; background: white;
        }
        input:focus, select:focus { border-color: var(--primary); box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.1); }
        .u-id, .u-start, .u-count, .u-div { width: 70px; font-weight: bold; text-align: center; }
        .u-type { padding: 8px; }

        /* Registers Table-like Grid */
        .reg-header { 
            display: grid; grid-template-columns: 1.5fr 1fr 1fr 2fr 50px; gap: 12px; 
            margin-bottom: 10px; padding: 0 10px; font-weight: 700; font-size: 12px; color: var(--text-sub);
        }
        .reg-row { 
            display: grid; grid-template-columns: 1.5fr 1fr 1fr 2fr 50px; gap: 12px; 
            margin-bottom: 10px; align-items: center; background: white; padding: 8px; border-radius: 10px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.02);
        }
        .reg-row input, .reg-row select { width: 100%; box-sizing: border-box; }
        
        .btn-small-del { 
            background: none; border: none; color: #cbd5e1; font-size: 18px; cursor: pointer; transition: 0.2s;
        }
        .btn-small-del:hover { color: var(--danger); }

    </style>
</head>
<body>
    <h2>Control Panel</h2>
    
    <div class="card">
        <h3>System Information</h3>
        <div class="info-grid">
            <div class="info-item"><b>MAC ADDRESS</b><span id="mac">--</span></div>
            <div class="info-item"><b>IP ADDRESS</b><span id="ip">--</span></div>
            <div class="info-item"><b>WIFI SSID</b><span id="wifi">--</span></div>
            <div class="info-item"><b>SIM CCID</b><span id="sim">--</span></div>
            <div class="info-item"><b>SIGNAL (RSSI)</b><span id="rssi">--</span></div>
        </div>
    </div>

    <div class="card">
        <h3>Modbus Slave Groups</h3>
        <button class="btn btn-add" onclick="addNewSlaveGroup()">+ Add New Slave Unit</button>
        <div id="slaveGroupsContainer"></div>
        <button class="btn btn-save" onclick="saveAllConfig()">Save All Configuration</button>
    </div>

<script>
function addNewSlaveGroup(data = {}) {
    // Giá trị mặc định nếu data trống (chống lỗi khi thêm mới)
    const id = data.id !== undefined ? data.id : 1;
    const start = data.start !== undefined ? data.start : 0;
    const count = data.count !== undefined ? data.count : 1;
    const dataType = data.dataType !== undefined ? data.dataType : 0;
    const div = data.div !== undefined ? data.div : 1.0;

    const container = document.getElementById('slaveGroupsContainer');
    const groupDiv = document.createElement('div');
    groupDiv.className = 'slave-unit';
    groupDiv.innerHTML = `
        <div class="unit-header">
            <div class="header-inputs">
                <div class="input-group">
                    <b>ID:</b> <input type="number" class="u-id" value="${id}">
                </div>
                <div class="input-group">
                    <b>Start Reg:</b> <input type="number" class="u-start" value="${start}">
                </div>
                <div class="input-group">
                    <b>Count:</b> <input type="number" class="u-count" value="${count}">
                </div>
                <div class="input-group">
                    <b>Type:</b> 
                    <select class="u-type">
                        <option value="0" ${dataType == 0 ? 'selected' : ''}>16-bit</option>
                        <option value="2" ${dataType == 2 ? 'selected' : ''}>32-bit (Float)</option>
                    </select>
                </div>
                <div class="input-group">
                    <b>Div:</b> <input type="number" step="any" class="u-div" value="${div}">
                </div>
            </div>
            <button class="btn btn-del" onclick="this.parentElement.parentElement.remove()">Delete Unit</button>
        </div>
        <div class="reg-header">
            <div>Type</div><div>Min</div><div>Max</div><div>Error Message</div><div></div>
        </div>
        <div class="regs-list"></div>
        <button class="btn" style="background:#e2e8f0; color:#475569; font-size:12px; margin-top:10px" onclick="addRegRow(this)">+ Add Register</button>
    `;
    container.appendChild(groupDiv);
    
    if (data.regs && data.regs.length > 0) {
        data.regs.forEach(r => addRegRow(groupDiv.querySelector('button[onclick*="addRegRow"]'), r));
    } else {
        addRegRow(groupDiv.querySelector('button[onclick*="addRegRow"]')); 
    }
}

function addRegRow(btn, regData = {type: 0, min: 0, max: 100, err: "ERR"}) {
    const list = btn.previousElementSibling;
    const row = document.createElement('div');
    row.className = 'reg-row';
    row.innerHTML = `
        <select class="r-type">
            <option value="0" ${regData.type==0?'selected':''}>0 (Range)</option>
            <option value="1" ${regData.type==1?'selected':''}>1 (State)</option>
        </select>
        <input type="number" step="any" class="r-min" value="${regData.min}">
        <input type="number" step="any" class="r-max" value="${regData.max}">
        <input type="text" class="r-err" value="${regData.err}">
        <button class="btn-small-del" onclick="this.parentElement.remove()">✕</button>
    `;
    list.appendChild(row);
}

function loadData() {
    fetch('/api/info').then(r => r.json()).then(data => {
        document.getElementById('mac').innerText = data.mac;
        document.getElementById('ip').innerText = data.ip;
        document.getElementById('wifi').innerText = data.wifi;
        document.getElementById('sim').innerText = data.sim_ccid;
        document.getElementById('rssi').innerText = data.rssi + " dBm";
    }).catch(e => console.log("Offline"));
}

function saveAllConfig() {
    let slaves = [];
    document.querySelectorAll(".slave-unit").forEach(unit => {
        let group = {
            id: parseInt(unit.querySelector(".u-id").value),
            start: parseInt(unit.querySelector(".u-start").value),
            count: parseInt(unit.querySelector(".u-count").value),
            dataType: parseInt(unit.querySelector(".u-type").value),
            div: parseFloat(unit.querySelector(".u-div").value),
            regs: []
        };
        unit.querySelectorAll(".reg-row").forEach(row => {
            group.regs.push({
                type: parseInt(row.querySelector(".r-type").value),
                min: parseFloat(row.querySelector(".r-min").value),
                max: parseFloat(row.querySelector(".r-max").value),
                err: row.querySelector(".r-err").value
            });
        });
        if (group.regs.length > 0) slaves.push(group);
    });

    fetch('/api/save_slaves', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ slaves: slaves })
    }).then(r => r.json()).then(data => alert("Đã lưu cấu hình!"));
}

function fetchSlaves() {
    fetch('/api/slaves').then(r => r.json()).then(data => {
        document.getElementById('slaveGroupsContainer').innerHTML = "";
        if(data.slaves) data.slaves.forEach(s => addNewSlaveGroup(s));
    });
}

setInterval(loadData, 5000);
fetchSlaves();
loadData();
</script>
</body>
</html>
)rawliteral";