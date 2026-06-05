#include "web_config.h"
#include "web_dashboard.h"
#include "web_login.h"

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
  <form action="/save" method="POST" onsubmit="setStatus('Saving...')">
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
    <button class="btn" type="submit">Save</button>
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

        /* Avatar dropdown */
        .avatar-wrap { position: relative; }
        .avatar {
            width: 40px; height: 40px; border-radius: 50%;
            background: var(--primary); color: white;
            font-size: 16px; font-weight: 700;
            display: flex; align-items: center; justify-content: center;
            cursor: pointer; user-select: none;
            box-shadow: 0 2px 8px rgba(37,99,235,0.35);
            transition: 0.2s;
        }
        .avatar:hover { filter: brightness(1.12); transform: scale(1.06); }
        .avatar-menu {
            display: none; position: absolute; right: 0; top: 50px;
            background: white; border-radius: 14px; min-width: 190px;
            box-shadow: 0 8px 30px rgba(0,0,0,0.15); border: 1px solid var(--border);
            overflow: hidden; z-index: 999;
            animation: fadeDown 0.15s ease;
        }
        .avatar-menu.open { display: block; }
        @keyframes fadeDown { from { opacity:0; transform:translateY(-8px); } to { opacity:1; transform:translateY(0); } }
        .avatar-menu-item {
            padding: 13px 18px; font-size: 14px; font-weight: 500;
            cursor: pointer; transition: background 0.15s; color: var(--text-main);
        }
        .avatar-menu-item:hover { background: #f1f5f9; }
        .avatar-menu-item.danger { color: var(--danger); }
        .avatar-menu-item.danger:hover { background: #fee2e2; }
        .avatar-menu-sep { height: 1px; background: var(--border); margin: 2px 0; }

        /* Modal */
        .modal-overlay {
            display: none; position: fixed; inset: 0;
            background: rgba(0,0,0,0.45); z-index: 1000;
            justify-content: center; align-items: center;
        }
        .modal-overlay.active { display: flex; }
        .modal-box {
            background: white; border-radius: 18px; padding: 28px;
            width: 90%; max-width: 380px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.2);
        }
        .modal-label { font-size: 13px; color: var(--text-sub); margin-top: 12px; display: block; }
        .modal-input { width: 100%; box-sizing: border-box; margin-top: 6px; }
        .modal-msg { padding: 10px 14px; border-radius: 8px; font-size: 13px; margin-bottom: 12px; }
        .modal-msg.ok { background: #d1fae5; color: #065f46; }
        .modal-msg.err { background: #fee2e2; color: #991b1b; }

    </style>
</head>
<body>
    <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:25px;">
        <h2 style="margin:0">Control Panel</h2>
        <div class="avatar-wrap" id="avatarWrap">
            <div class="avatar" onclick="toggleMenu()" title="Tài khoản" aria-label="Tài khoản">
            <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
              <circle cx="12" cy="8" r="4"/><path d="M4 20c0-4 3.6-7 8-7s8 3 8 7"/>
            </svg>
        </div>
            <div class="avatar-menu" id="avatarMenu">
                <div class="avatar-menu-item" onclick="showModal('changePassModal'); closeMenu()">🔑 Đổi mật khẩu</div>
                <div class="avatar-menu-sep"></div>
                <div class="avatar-menu-item danger" onclick="doLogout()">⏻ Đăng xuất</div>
            </div>
        </div>
    </div>
    
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

    <!-- Modal Đổi mật khẩu -->
    <div id="changePassModal" class="modal-overlay" onclick="closeModalOnBg(event,'changePassModal')">
        <div class="modal-box">
            <h3 style="margin-top:0">🔑 Đổi mật khẩu</h3>
            <div id="changePassMsg" class="modal-msg" style="display:none"></div>
            <label class="modal-label">Mật khẩu hiện tại</label>
            <input type="password" id="oldPass" class="modal-input" placeholder="Nhập mật khẩu cũ">
            <label class="modal-label">Mật khẩu mới</label>
            <input type="password" id="newPass" class="modal-input" placeholder="Tối thiểu 4 ký tự">
            <label class="modal-label">Xác nhận mật khẩu mới</label>
            <input type="password" id="confirmPass" class="modal-input" placeholder="Nhập lại mật khẩu mới">
            <div style="display:flex;gap:10px;margin-top:16px;">
                <button class="btn" style="background:#e2e8f0;color:#475569;flex:1" onclick="closeModal('changePassModal')">Hủy</button>
                <button class="btn btn-save" style="flex:2;margin-top:0" onclick="doChangePass()">Xác nhận</button>
            </div>
        </div>
    </div>

    <div class="card">
        <button class="btn btn-add" onclick="addNewSlaveGroup()">+ Add New Slave Unit</button>
        <div id="slaveGroupsContainer"></div>
        <button class="btn btn-save" onclick="saveAllConfig()">Save All Configuration</button>
    </div>

<script>
function addNewSlaveGroup(data = {}) {
    const id       = data.id       !== undefined ? data.id       : 1;
    const start    = data.start    !== undefined ? data.start    : 0;
    const count    = data.count    !== undefined ? data.count    : 1;
    const dataType = data.dataType !== undefined ? data.dataType : 0;
    const div      = data.div      !== undefined ? data.div      : 1.0;

    const container = document.getElementById('slaveGroupsContainer');
    const groupDiv  = document.createElement('div');
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
    `;
    container.appendChild(groupDiv);
}

function loadData() {
    fetch('/api/info').then(r => r.json()).then(data => {
        document.getElementById('mac').innerText  = data.mac;
        document.getElementById('ip').innerText   = data.ip;
        document.getElementById('wifi').innerText = data.wifi;
        document.getElementById('sim').innerText  = data.sim_ccid;
        document.getElementById('rssi').innerText = data.rssi + " dBm";
    }).catch(e => console.log("Offline"));
}

function saveAllConfig() {
    let slaves = [];
    document.querySelectorAll(".slave-unit").forEach(unit => {
        slaves.push({
            id:       parseInt(unit.querySelector(".u-id").value),
            start:    parseInt(unit.querySelector(".u-start").value),
            count:    parseInt(unit.querySelector(".u-count").value),
            dataType: parseInt(unit.querySelector(".u-type").value),
            div:      parseFloat(unit.querySelector(".u-div").value)
        });
    });

    fetch('/api/save_slaves', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ slaves: slaves })
    }).then(r => r.json()).then(() => alert("Đã lưu cấu hình!"));
}

function fetchSlaves() {
    fetch('/api/slaves').then(r => r.json()).then(data => {
        document.getElementById('slaveGroupsContainer').innerHTML = "";
        if (data.slaves) data.slaves.forEach(s => addNewSlaveGroup(s));
    });
}

function showModal(id) { document.getElementById(id).classList.add('active'); }
function closeModal(id) {
    document.getElementById(id).classList.remove('active');
    document.querySelectorAll('#'+id+' input').forEach(i => i.value = '');
    let msg = document.querySelector('#'+id+' .modal-msg');
    if (msg) { msg.style.display='none'; msg.className='modal-msg'; }
}
function closeModalOnBg(e, id) { if (e.target === document.getElementById(id)) closeModal(id); }

function showModalMsg(modalId, text, isOk) {
    let msg = document.querySelector('#'+modalId+' .modal-msg');
    msg.innerText = text;
    msg.className = 'modal-msg ' + (isOk ? 'ok' : 'err');
    msg.style.display = 'block';
}

function doChangePass() {
    let old = document.getElementById('oldPass').value;
    let np  = document.getElementById('newPass').value;
    let cp  = document.getElementById('confirmPass').value;
    if (!old || !np || !cp) { showModalMsg('changePassModal','Vui lòng điền đầy đủ!', false); return; }
    if (np !== cp)           { showModalMsg('changePassModal','Mật khẩu mới không khớp!', false); return; }
    if (np.length < 4)       { showModalMsg('changePassModal','Mật khẩu phải có ít nhất 4 ký tự!', false); return; }
    fetch('/api/change_pass', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({old_pass: old, new_pass: np})
    }).then(r => r.json()).then(d => {
        showModalMsg('changePassModal', d.msg, d.status === 'OK');
        if (d.status === 'OK') setTimeout(() => closeModal('changePassModal'), 2000);
    }).catch(() => showModalMsg('changePassModal','Lỗi kết nối!', false));
}

function toggleMenu() { document.getElementById('avatarMenu').classList.toggle('open'); }
function closeMenu()  { document.getElementById('avatarMenu').classList.remove('open'); }
document.addEventListener('click', function(e) {
    if (!document.getElementById('avatarWrap').contains(e.target)) closeMenu();
});

function doLogout() {
    fetch('/api/logout', {method:'POST'}).finally(() => window.location.href = '/login');
}

setInterval(loadData, 5000);
fetchSlaves();
loadData();
</script>
</body>
</html>
)rawliteral";

const char* htmlLogin = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 Login</title>
<style>
  * { box-sizing: border-box; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
  body { margin: 0; background: linear-gradient(135deg, #1e3a5f 0%, #2563eb 100%); display: flex; justify-content: center; align-items: center; min-height: 100vh; }
  .card { background: #fff; border-radius: 20px; padding: 36px 32px; width: 100%; max-width: 380px; box-shadow: 0 20px 60px rgba(0,0,0,0.25); }
  .logo { text-align: center; margin-bottom: 8px; font-size: 36px; }
  h2 { text-align: center; margin: 0 0 6px; font-size: 22px; color: #1e293b; }
  .subtitle { text-align: center; font-size: 13px; color: #94a3b8; margin-bottom: 28px; }
  .msg { padding: 10px 14px; border-radius: 10px; font-size: 13px; margin-bottom: 16px; display: none; }
  .msg.err { background: #fee2e2; color: #991b1b; display: block; }
  .msg.ok  { background: #d1fae5; color: #065f46; display: block; }
  label { font-size: 13px; color: #64748b; display: block; margin-bottom: 6px; }
  input[type=text], input[type=password] {
    width: 100%; height: 46px; padding: 0 14px; border: 1.5px solid #e2e8f0;
    border-radius: 10px; font-size: 14px; outline: none; transition: 0.2s; margin-bottom: 16px;
  }
  input:focus { border-color: #2563eb; box-shadow: 0 0 0 3px rgba(37,99,235,0.12); }
  .btn-login { width: 100%; height: 48px; border: none; border-radius: 12px; background: #2563eb; color: white; font-size: 15px; font-weight: 600; cursor: pointer; margin-top: 4px; transition: 0.2s; }
  .btn-login:hover { background: #1d4ed8; }
  .btn-login:active { transform: scale(0.98); }
  .forgot { text-align: center; margin-top: 18px; font-size: 13px; color: #64748b; }
  .forgot a { color: #2563eb; text-decoration: none; font-weight: 500; cursor: pointer; }
  /* Panel quên mật khẩu */
  .panel { display: none; margin-top: 20px; padding: 20px; background: #f8fafc; border-radius: 14px; border: 1px solid #e2e8f0; }
  .panel.active { display: block; }
  .panel h3 { margin: 0 0 14px; font-size: 15px; color: #1e293b; }
  .panel label { margin-bottom: 5px; }
  .panel input { margin-bottom: 12px; }
  .btn-reset { width: 100%; height: 44px; border: none; border-radius: 10px; background: #0f766e; color: white; font-size: 14px; font-weight: 600; cursor: pointer; transition: 0.2s; }
  .btn-reset:hover { background: #0d6460; }
  .back-link { display: block; text-align: center; margin-top: 10px; font-size: 12px; color: #94a3b8; cursor: pointer; }
  .back-link:hover { color: #2563eb; }
</style>
</head>
<body>
<div class="card">
  <div class="logo">
    <svg xmlns="http://www.w3.org/2000/svg" width="56" height="56" viewBox="0 0 24 24" fill="none" stroke="#2563eb" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
      <rect x="3" y="11" width="18" height="11" rx="2" ry="2"/>
      <path d="M7 11V7a5 5 0 0 1 10 0v4"/>
      <circle cx="12" cy="16" r="1.5" fill="#2563eb" stroke="none"/>
    </svg>
  </div>
  <h2>Control Panel</h2>
  <p class="subtitle">Đăng nhập để quản lý hệ thống</p>

  <!-- Panel đăng nhập -->
  <div id="loginPanel">
    <div id="loginMsg" class="msg"></div>
    <label>Tên đăng nhập</label>
    <input type="text" id="loginUser" placeholder="Tên đăng nhập" autocomplete="username">
    <label>Mật khẩu</label>
    <input type="password" id="loginPass" placeholder="Nhập mật khẩu" onkeydown="if(event.key==='Enter')doLogin()">
    <button class="btn-login" onclick="doLogin()">Đăng nhập</button>
    <div class="forgot"><a onclick="toggleForgot()">❓ Quên mật khẩu?</a></div>
  </div>

  <!-- Panel quên mật khẩu -->
  <div id="forgotPanel" class="panel">
    <h3>🔓 Đặt lại mật khẩu</h3>
    <div id="resetMsg" class="msg"></div>
    <label>Mã PIN bí mật</label>
    <input type="password" id="resetPin" placeholder="Nhập PIN">
    <label>Mật khẩu mới</label>
    <input type="password" id="resetNew" placeholder="Tối thiểu 4 ký tự">
    <label>Xác nhận mật khẩu mới</label>
    <input type="password" id="resetConfirm" placeholder="Nhập lại">
    <button class="btn-reset" onclick="doReset()">Xác nhận đặt lại</button>
    <a class="back-link" onclick="toggleForgot()">← Quay lại đăng nhập</a>
  </div>
</div>

<script>
function toggleForgot() {
  let lp = document.getElementById('loginPanel');
  let fp = document.getElementById('forgotPanel');
  let show = !fp.classList.contains('active');
  fp.classList.toggle('active', show);
  lp.style.display = show ? 'none' : 'block';
  clearMsg('loginMsg'); clearMsg('resetMsg');
}

function showMsg(id, text, isOk) {
  let el = document.getElementById(id);
  el.innerText = text;
  el.className = 'msg ' + (isOk ? 'ok' : 'err');
}
function clearMsg(id) {
  let el = document.getElementById(id);
  el.className = 'msg'; el.innerText = '';
}

function doLogin() {
  let user = document.getElementById('loginUser').value.trim();
  let pass = document.getElementById('loginPass').value;
  if (!user || !pass) { showMsg('loginMsg','Vui lòng nhập đầy đủ!', false); return; }
  fetch('/api/login', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({user: user, pass: pass})
  }).then(r => r.json()).then(d => {
    if (d.status === 'OK') {
      showMsg('loginMsg', 'Đăng nhập thành công! Đang chuyển hướng...', true);
      setTimeout(() => window.location.href = '/', 800);
    } else {
      showMsg('loginMsg', d.msg, false);
    }
  }).catch(() => showMsg('loginMsg','Lỗi kết nối!', false));
}

function doReset() {
  let pin  = document.getElementById('resetPin').value;
  let np   = document.getElementById('resetNew').value;
  let cp   = document.getElementById('resetConfirm').value;
  if (!pin || !np || !cp) { showMsg('resetMsg','Vui lòng điền đầy đủ!', false); return; }
  if (np !== cp)           { showMsg('resetMsg','Mật khẩu mới không khớp!', false); return; }
  if (np.length < 4)       { showMsg('resetMsg','Mật khẩu phải có ít nhất 4 ký tự!', false); return; }
  fetch('/api/reset_pass', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({pin: pin, new_pass: np})
  }).then(r => r.json()).then(d => {
    showMsg('resetMsg', d.msg, d.status === 'OK');
    if (d.status === 'OK') setTimeout(() => toggleForgot(), 2000);
  }).catch(() => showMsg('resetMsg','Lỗi kết nối!', false));
}
</script>
</body></html>
)rawliteral";