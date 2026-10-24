#ifndef WEB_UI_H
#define WEB_UI_H

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>AERO LAB</title>
  <style>
    :root {
      --bg: #050505;
      --panel: #0b0b0d;
      --panel-glow: rgba(255,255,255,0.12);
      --accent: #f5f5f5;
      --accent-2: #d6d6d6;
      --muted: #9ca3af;
      --border: #1a1a1d;
      --surface: rgba(255,255,255,0.04);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Segoe UI", system-ui, -apple-system, sans-serif;
      background:
        radial-gradient(circle at 20% 10%, rgba(255,255,255,0.08), transparent 32%),
        radial-gradient(circle at 80% 0%, rgba(255,255,255,0.05), transparent 28%),
        linear-gradient(135deg, rgba(255,255,255,0.02), rgba(255,255,255,0.04)),
        var(--bg);
      color: #e5e7eb;
      min-height: 100vh;
      padding: 24px;
    }
    h1 {
      margin: 0;
      font-size: 28px;
      letter-spacing: 2px;
      text-transform: uppercase;
      font-weight: 300;
      background: linear-gradient(135deg, #ffffff, #bfbfbf);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
    }
    .card {
      position: relative;
      background: linear-gradient(145deg, rgba(255,255,255,0.02), var(--panel));
      border: 1px solid var(--border);
      border-radius: 16px;
      padding: 16px;
      margin-bottom: 18px;
      box-shadow: 0 12px 30px rgba(0,0,0,0.45), 0 0 0 1px var(--panel-glow);
      overflow: hidden;
    }
    .card:before {
      content: "";
      position: absolute;
      inset: 0;
      background: radial-gradient(circle at 20% 20%, var(--panel-glow), transparent 45%);
      pointer-events: none;
      z-index: 0;
    }
    .card > * { position: relative; z-index: 1; }
    .top-row {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 12px;
      margin-top: 12px;
    }
    .pill {
      background: linear-gradient(120deg, rgba(255,255,255,0.02), rgba(255,255,255,0.06));
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 10px 14px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      color: var(--muted);
      box-shadow: inset 0 0 0 1px rgba(255,255,255,0.03);
    }
    .pill strong { color: #e5e7eb; }
    label { color: var(--muted); font-size: 14px; }
    select, input[type="number"] {
      width: 100%;
      background: rgba(255,255,255,0.04);
      border: 1px solid var(--border);
      color: #e5e7eb;
      padding: 8px 10px;
      border-radius: 10px;
      font-size: 14px;
      transition: border-color 0.2s ease, box-shadow 0.2s ease;
    }
    select:focus, input[type="number"]:focus { outline: none; border-color: var(--accent); box-shadow: 0 0 0 2px rgba(255,116,24,0.2); }
    button {
      background: linear-gradient(135deg, #f5f5f5, #d9d9d9);
      color: #0b0b0d;
      border: none;
      border-radius: 12px;
      padding: 10px 14px;
      font-weight: 700;
      letter-spacing: 0.5px;
      cursor: pointer;
      box-shadow: 0 8px 20px rgba(255,255,255,0.12);
      transition: transform 0.08s ease, box-shadow 0.08s ease;
    }
    button.secondary {
      background: rgba(255,255,255,0.06);
      color: #e5e7eb;
      box-shadow: inset 0 0 0 1px var(--border);
    }
    button:hover { transform: translateY(-1px); }
    button:active { transform: translateY(0); }
    .toolbar {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
      gap: 10px;
      align-items: end;
      margin-bottom: 12px;
    }
    table {
      width: 100%;
      border-collapse: collapse;
      background: rgba(255,255,255,0.02);
      border: 1px solid var(--border);
      border-radius: 12px;
      overflow: hidden;
    }
    thead { background: rgba(255,255,255,0.04); }
    th, td {
      padding: 8px;
      text-align: center;
      border-bottom: 1px solid var(--border);
      color: #cbd5e1;
      font-size: 13px;
    }
    th { font-weight: 600; }
    tr:last-child td { border-bottom: none; }
    .status {
      height: 20px;
      font-size: 13px;
      color: var(--muted);
      margin-top: 6px;
    }
    input[readonly] {
      background: #0b1220;
      color: #64748b;
      border-style: dashed;
      cursor: not-allowed;
    }
    .graph-toolbar {
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
      align-items: flex-end;
      margin-bottom: 10px;
    }
    .toolbar-group {
      min-width: 200px;
      flex: 1 1 200px;
    }
    .toolbar-actions {
      display: flex;
      gap: 10px;
      flex-wrap: wrap;
    }
    .toolbar-row {
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
      align-items: flex-end;
      width: 100%;
    }
    .graph-meta {
      color: var(--muted);
      font-size: 13px;
      margin-top: 4px;
    }
    #curveCanvas {
      width: 100%;
      height: 360px;
      min-height: 320px;
      background: rgba(255,255,255,0.02);
      border: 1px solid var(--border);
      border-radius: 10px;
    }
    .servo-visual {
      display: flex;
      flex-direction: column;
      gap: 14px;
      margin: 12px 0;
      align-items: center;
      justify-content: center;
    }
    .servo-readouts {
      display: flex;
      gap: 14px;
      flex-wrap: wrap;
      color: var(--muted);
      font-size: 14px;
      justify-content: center;
      text-align: center;
    }
    .servo-readouts strong { color: #e5e7eb; }
    .servo-stats {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 12px;
      width: 100%;
      text-align: center;
    }
    .stat-panel {
      background: rgba(255,255,255,0.03);
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 10px;
      color: var(--muted);
      font-size: 13px;
    }
    .stat-panel strong { color: #e5e7eb; display: block; font-size: 16px; }
    .wing-wrap {
      display: flex;
      justify-content: center;
      width: 100%;
    }
    .wing-box {
      position: relative;
      width: 350px;
      height: 168px;
      background: rgba(255,255,255,0.02);
      border: 1px solid var(--border);
      border-radius: 14px;
      overflow: hidden;
      padding: 18px 16px;
    }
    .wing {
      position: absolute;
      bottom: 24px;
      left: 25px;
      width: 120px;
      height: 12px;
      background: linear-gradient(90deg, #f5f5f5, #bfbfbf);
      transform-origin: left center;
      border-radius: 8px;
      transition: transform 0.12s ease;
      box-shadow: 0 8px 18px rgba(255,255,255,0.15);
    }
    .wing.right {
      left: 187px;
      background: linear-gradient(90deg, #d6d6d6, #a8a8a8);
      box-shadow: 0 8px 18px rgba(255,255,255,0.12);
    }
    .wing-labels { display: none; }
    .legend {
      display: flex;
      align-items: center;
      gap: 6px;
      color: var(--muted);
      font-size: 13px;
    }
    .legend .dot {
      width: 10px;
      height: 10px;
      border-radius: 50%;
      display: inline-block;
    }
    @media (max-width: 720px) {
      .toolbar { grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); }
      .graph-toolbar { flex-direction: column; align-items: stretch; }
      .toolbar-actions { width: 100%; }
      th, td { font-size: 12px; }
    }
  </style>
</head>
<body>
  <div class="card">
      <div style="display:flex; justify-content:space-between; align-items:center; gap:10px; flex-wrap:wrap;">
        <div>
        <h1>AERO LAB</h1>
        </div>
        <button id="calibrateBtn">Calibrate here</button>
      </div>
    <div class="top-row">
      <div class="pill"><span>Roll</span><strong><span id="rollVal">--</span>°</strong></div>
      <div class="pill"><span>Pitch</span><strong><span id="pitchVal">--</span>°</strong></div>
      <div class="pill"><span>Preset in use</span><strong><span id="activePreset">--</span></strong></div>
      <div class="pill"><span>Offsets</span><strong><span id="offsets">--</span></strong></div>
    </div>
  </div>

  <div class="card">
    <div class="servo-visual">
      <div class="wing-wrap">
        <div class="wing-box">
          <div class="wing" id="wingLeft"></div>
          <div class="wing right" id="wingRight"></div>
          <div class="wing-labels"><span>0°</span><span>90°</span></div>
        </div>
      </div>
      <div class="servo-readouts">
        <div>Left servo (command): <strong><span id="leftServoVal">--</span>°</strong></div>
        <div>Right servo (command): <strong><span id="rightServoVal">--</span>°</strong></div>
      </div>
      <div class="servo-stats">
        <div class="stat-panel">
          <div id="servoConnMsg">--</div>
        </div>
        <div class="stat-panel">
          <div>Left actual</div>
          <strong><span id="leftActualVal">--</span></strong>
          <div>Δ vs command: <span id="leftDeltaVal">--</span></div>
        </div>
        <div class="stat-panel">
          <div>Right actual</div>
          <strong><span id="rightActualVal">--</span></strong>
          <div>Δ vs command: <span id="rightDeltaVal">--</span></div>
        </div>
      </div>
    </div>
  </div>

  <div class="card">
    <div class="graph-toolbar">
      <div class="toolbar-row">
        <div class="toolbar-group">
          <label for="presetSelect">Preset</label>
          <select id="presetSelect">
            <option value="0">Preset 0</option>
            <option value="1">Preset 1</option>
            <option value="2">Preset 2</option>
          </select>
        </div>
        <div class="toolbar-actions">
          <button class="secondary" id="resetBtn">Reset preset to defaults</button>
          <button id="savePresetBtn">Save preset to flash</button>
        </div>
      </div>
      <div class="toolbar-row">
        <div class="toolbar-group">
          <label for="curveSelect">Curve</label>
          <select id="curveSelect">
            <option value="rollToRight">Roll → Right servo</option>
            <option value="rollToLeft">Roll → Left servo</option>
            <option value="pitchBoth">Pitch → Both servos</option>
          </select>
          <div class="graph-meta" id="curveMeta">X is fixed degrees, Y is servo % (0-100).</div>
        </div>
        <div class="legend">
          <span class="dot" id="curveDot" style="background:#22c55e;"></span>
          <span id="curveLabel">Roll → Right servo</span>
        </div>
      </div>
    </div>
    <canvas id="curveCanvas" height="320"></canvas>
    <div class="graph-meta">Drag points to adjust output. Vertical scale is pinned 0–100%.</div>
  </div>

  <div class="card">
    <div class="toolbar" style="margin-bottom:8px;">
      <div>
        <button class="secondary" id="toggleTableBtn">Hide coordinates</button>
      </div>
    </div>
    <div id="tableContainer"></div>
    <div class="status" id="status"></div>
  </div>

  <script>
  (function() {
    const presetSelect = document.getElementById("presetSelect");
    const statusEl = document.getElementById("status");
    const rollEl = document.getElementById("rollVal");
    const pitchEl = document.getElementById("pitchVal");
    const activePresetEl = document.getElementById("activePreset");
    const offsetsEl = document.getElementById("offsets");
    const leftServoEl = document.getElementById("leftServoVal");
    const rightServoEl = document.getElementById("rightServoVal");
    const leftActualEl = document.getElementById("leftActualVal");
    const rightActualEl = document.getElementById("rightActualVal");
    const leftDeltaEl = document.getElementById("leftDeltaVal");
    const rightDeltaEl = document.getElementById("rightDeltaVal");
    const servoConnMsg = document.getElementById("servoConnMsg");
    const wingLeft = document.getElementById("wingLeft");
    const wingRight = document.getElementById("wingRight");
    const curveSelect = document.getElementById("curveSelect");
    const curveLabelEl = document.getElementById("curveLabel");
    const curveDotEl = document.getElementById("curveDot");
    const curveMetaEl = document.getElementById("curveMeta");
    const toggleTableBtn = document.getElementById("toggleTableBtn");
    const tableContainer = document.getElementById("tableContainer");
    const canvas = document.getElementById("curveCanvas");
    const ctx = canvas.getContext("2d");
    const fields = ["rollInput", "pitchInput", "rollToRight", "pitchToRight", "rollToLeft", "pitchToLeft"];
    const curves = [
      { id: "rollToRight", label: "Roll → Right servo", xField: "rollInput", field: "rollToRight", color: "#38bdf8" },
      { id: "rollToLeft", label: "Roll → Left servo (mirrored)", xField: "rollInput", field: "rollToLeft", color: "#38bdf8" },
      { id: "pitchBoth", label: "Pitch → Both servos", xField: "pitchInput", field: "pitchToRight", color: "#38bdf8" }
    ];
    let userPresetOverride = false;
    let lastLoadedPreset = -1;
    let loadingPreset = false;
    let currentPresetData = null;
    let currentCurveId = curves[0].id;
    let dragIndex = -1;
    let pendingChange = null;
    let changeTimer = null;
    let tableVisible = true;

    function setStatus(msg, ok = true) {
      statusEl.textContent = msg;
      statusEl.style.color = ok ? "#9ae6b4" : "#f87171";
    }

    function clamp(v, min, max) {
      return Math.max(min, Math.min(max, v));
    }

    function quantizeTenth(v) {
      return Math.round(v * 10) / 10;
    }

    function normalizePresetData() {
      if (!currentPresetData) return;
      ["rollInput","pitchInput","rollToRight","rollToLeft","pitchToRight","pitchToLeft"].forEach(f => {
        if (currentPresetData[f]) {
          currentPresetData[f] = currentPresetData[f].map(v => parseFloat(v));
        }
      });
    }

    function findMirrorIndexLocal(idx) {
      if (!currentPresetData || !currentPresetData.rollInput) return idx;
      const target = -Number(currentPresetData.rollInput[idx]);
      const arr = currentPresetData.rollInput.map(Number);
      const found = arr.findIndex(v => Math.abs(v - target) < 0.0001);
      return found >= 0 ? found : idx;
    }

    function enforcePitchTieLocal() {
      if (!currentPresetData) return;
      const right = (currentPresetData.pitchToRight || []).map(v => parseFloat(v));
      currentPresetData.pitchToLeft = currentPresetData.pitchToLeft || [];
      for (let i = 0; i < right.length; i++) {
        currentPresetData.pitchToLeft[i] = right[i];
        updateTableCell("pitchToLeft", i, right[i]);
      }
    }

    function enforceRollSymmetryLocal() {
      if (!currentPresetData) return;
      const right = (currentPresetData.rollToRight || []).map(v => parseFloat(v));
      currentPresetData.rollToLeft = currentPresetData.rollToLeft || [];
      for (let i = 0; i < right.length; i++) {
        const mirror = findMirrorIndexLocal(i);
        currentPresetData.rollToLeft[i] = right[mirror];
        updateTableCell("rollToLeft", i, currentPresetData.rollToLeft[i]);
      }
    }

    async function getJSON(url, opts = {}) {
      const res = await fetch(url, opts);
      if (!res.ok) throw new Error(await res.text() || res.statusText);
      return res.json();
    }

    async function postJSON(path, payload = {}) {
      return getJSON(path, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
    }

    async function loadPresetData() {
      if (loadingPreset) return;
      const preset = parseInt(presetSelect.value, 10);
      loadingPreset = true;
      try {
        const data = await getJSON(`/preset?p=${preset}`);
        currentPresetData = data;
        normalizePresetData();
        enforcePitchTieLocal();
        enforceRollSymmetryLocal();
        renderTable(currentPresetData);
        lastLoadedPreset = preset;
        drawCurve();
        setStatus(`Loaded preset ${preset}`);
      } catch (err) {
        setStatus(err.message, false);
      } finally {
        loadingPreset = false;
      }
    }

    async function applyPresetSelection(preset) {
      try {
        await postJSON("/use", { preset });
        presetSelect.value = preset;
        userPresetOverride = false; // sync back to live state after explicit use
        activePresetEl.textContent = preset;
        setStatus(`Preset ${preset} active`);
        await loadPresetData();
      } catch (err) {
        userPresetOverride = false;
        setStatus(err.message, false);
      }
    }

    async function savePreset() {
      const preset = parseInt(presetSelect.value, 10);
      try {
        await postJSON("/save", { preset });
        setStatus(`Preset ${preset} saved`);
      } catch (err) {
        setStatus(err.message, false);
      }
    }

    async function resetPreset() {
      const preset = parseInt(presetSelect.value, 10);
      try {
        await postJSON("/reset", { preset });
        await loadPresetData();
        setStatus(`Preset ${preset} reset to defaults`);
      } catch (err) {
        setStatus(err.message, false);
      }
    }

    function setCurve(id) {
      const curve = curves.find(c => c.id === id) || curves[0];
      currentCurveId = curve.id;
      curveSelect.value = curve.id;
      curveLabelEl.textContent = curve.label;
      curveDotEl.style.background = curve.color;
      const axis = curve.xField === "rollInput" ? "Roll (°)" : "Pitch (°)";
      curveMetaEl.textContent = `${curve.label} — X: ${axis}, Y: servo output (0–100%)`;
      drawCurve();
    }

    function ensureCanvasSize() {
      const rect = canvas.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      const width = Math.max(rect.width, 320) * dpr;
      const height = 360 * dpr; // pinned vertical dimension
      if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
      }
      return { width, height, dpr };
    }

    function getScale(xs) {
      const { width, height, dpr } = ensureCanvasSize();
      const padding = 50 * dpr;
      const xMin = Math.min(...xs);
      const xMax = Math.max(...xs);
      return { width, height, padding, xMin, xMax, yMin: 0, yMax: 100, dpr };
    }

    function dataToPixel(x, y, scale) {
      const usableW = scale.width - scale.padding * 2;
      const usableH = scale.height - scale.padding * 2;
      const xNorm = scale.xMax === scale.xMin ? 0.5 : (x - scale.xMin) / (scale.xMax - scale.xMin);
      const px = scale.padding + xNorm * usableW;
      const yNorm = (y - scale.yMin) / (scale.yMax - scale.yMin);
      const py = scale.height - scale.padding - yNorm * usableH;
      return { px, py };
    }

    function pixelToValue(py, scale) {
      const usableH = scale.height - scale.padding * 2;
      const yNorm = (scale.height - scale.padding - py) / usableH;
      return clamp(yNorm * (scale.yMax - scale.yMin) + scale.yMin, scale.yMin, scale.yMax);
    }

    let pointPixels = [];

    function drawCurve() {
      if (!currentPresetData) return;
      const curve = curves.find(c => c.id === currentCurveId) || curves[0];
      const xs = (currentPresetData[curve.xField] || []).map(Number);
      const ys = (currentPresetData[curve.field] || []).map(Number);
      if (!xs.length || !ys.length) return;

      const scale = getScale(xs);
      ctx.clearRect(0, 0, scale.width, scale.height);
      ctx.fillStyle = "rgba(255,255,255,0.02)";
      ctx.fillRect(0, 0, scale.width, scale.height);

      // Axes
      ctx.strokeStyle = "#1f2937";
      ctx.lineWidth = 1 * scale.dpr;
      ctx.beginPath();
      ctx.moveTo(scale.padding, scale.padding);
      ctx.lineTo(scale.padding, scale.height - scale.padding);
      ctx.lineTo(scale.width - scale.padding, scale.height - scale.padding);
      ctx.stroke();

      ctx.fillStyle = "#cbd5e1";
      ctx.font = `${12 * scale.dpr}px "Segoe UI", sans-serif`;
      ctx.textAlign = "right";
      ctx.textBaseline = "middle";
      for (let y = 0; y <= 100; y += 20) {
        const p = dataToPixel(scale.xMin, y, scale);
        ctx.fillText(`${y}`, scale.padding - 8 * scale.dpr, p.py);
        ctx.beginPath();
        ctx.moveTo(scale.padding, p.py);
        ctx.lineTo(scale.width - scale.padding, p.py);
        ctx.strokeStyle = "rgba(255,255,255,0.05)";
        ctx.stroke();
      }

      ctx.textAlign = "center";
      ctx.textBaseline = "top";
      xs.forEach(x => {
        const p = dataToPixel(x, scale.yMin, scale);
        ctx.fillStyle = "#cbd5e1";
        ctx.fillText(`${x}`, p.px, scale.height - scale.padding + 6 * scale.dpr);
      });

      ctx.save();
      ctx.fillStyle = "#cbd5e1";
      ctx.textAlign = "center";
      ctx.font = `${12 * scale.dpr}px "Segoe UI", sans-serif`;
      const axisXLabel = curve.xField === "rollInput" ? "Roll (degrees)" : "Pitch (degrees)";
      ctx.fillText(axisXLabel, scale.width / 2, scale.height - 14 * scale.dpr);
      ctx.rotate(-Math.PI / 2);
      ctx.textAlign = "center";
      ctx.fillText("Servo output (%)", -scale.height / 2, 16 * scale.dpr);
      ctx.restore();

      // Curve
      ctx.strokeStyle = curve.color;
      ctx.lineWidth = 2 * scale.dpr;
      ctx.beginPath();
      ys.forEach((y, i) => {
        const p = dataToPixel(xs[i], y, scale);
        if (i === 0) ctx.moveTo(p.px, p.py);
        else ctx.lineTo(p.px, p.py);
      });
      ctx.stroke();

      pointPixels = [];
      ys.forEach((y, i) => {
        const p = dataToPixel(xs[i], y, scale);
        pointPixels.push(p);
        ctx.beginPath();
        ctx.arc(p.px, p.py, 5 * scale.dpr, 0, Math.PI * 2);
        ctx.fillStyle = "#0b1220";
        ctx.fill();
        ctx.strokeStyle = curve.color;
        ctx.lineWidth = 2 * scale.dpr;
        ctx.stroke();
      });
    }

    function updateTableCell(field, index, value, format = true) {
      const input = document.querySelector(`input[data-field="${field}"][data-index="${index}"]`);
      if (input) {
        const numVal = parseFloat(value);
        input.value = format && !Number.isNaN(numVal) ? numVal.toFixed(1) : value;
      }
    }

    function queueSetValue(field, index, value) {
      const preset = parseInt(presetSelect.value, 10);
      pendingChange = { preset, field, index, value };
      if (changeTimer) return;
      changeTimer = setTimeout(async () => {
        const change = pendingChange;
        pendingChange = null;
        changeTimer = null;
        try {
          await postJSON("/set", change);
          const numVal = parseFloat(change.value);
          const displayVal = !Number.isNaN(numVal) ? numVal.toFixed(1) : change.value;
          setStatus(`Updated ${field}[${index}] to ${displayVal} on preset ${preset}`);
        } catch (err) {
          setStatus(err.message, false);
        }
      }, 120);
    }

    function getPointer(evt) {
      const rect = canvas.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      return {
        x: (evt.clientX - rect.left) * dpr,
        y: (evt.clientY - rect.top) * dpr
      };
    }

    function beginDrag(evt) {
      if (!currentPresetData) return;
      const pos = getPointer(evt);
      let nearest = -1;
      let nearestDist = Infinity;
      const threshold = 14 * (window.devicePixelRatio || 1);
      pointPixels.forEach((p, i) => {
        const dx = p.px - pos.x;
        const dy = p.py - pos.y;
        const d = Math.sqrt(dx * dx + dy * dy);
        if (d < nearestDist) {
          nearestDist = d;
          nearest = i;
        }
      });
      if (nearestDist <= threshold) {
        dragIndex = nearest;
        evt.preventDefault();
      } else {
        dragIndex = -1;
      }
    }

    function moveDrag(evt) {
      if (dragIndex === -1 || !currentPresetData) return;
      const curve = curves.find(c => c.id === currentCurveId) || curves[0];
      const xs = currentPresetData[curve.xField] || [];
      const ys = currentPresetData[curve.field] || [];
      if (!xs.length || !ys.length) return;
      const scale = getScale(xs);
      const pos = getPointer(evt);
      const newVal = pixelToValue(pos.y, scale);
      const clamped = clamp(newVal, 0, 100);
      const quantized = quantizeTenth(clamped);
      ys[dragIndex] = quantized;
      if (curve.field === "pitchToRight") {
        currentPresetData.pitchToRight[dragIndex] = quantized;
        enforcePitchTieLocal();
        updateTableCell("pitchToRight", dragIndex, quantized);
      } else if (curve.field === "rollToRight") {
        currentPresetData.rollToRight[dragIndex] = quantized;
        enforceRollSymmetryLocal();
        updateTableCell("rollToRight", dragIndex, quantized);
      } else if (curve.field === "rollToLeft") {
        const mirror = findMirrorIndexLocal(dragIndex);
        currentPresetData.rollToRight[mirror] = quantized;
        enforceRollSymmetryLocal();
        updateTableCell("rollToLeft", dragIndex, quantized);
      }
      drawCurve();
      const fieldForSet = curve.field === "pitchToRight" ? "pitchToRight" : curve.field;
      queueSetValue(fieldForSet === "rollToLeft" ? "rollToLeft" : fieldForSet, dragIndex, quantized);
      evt.preventDefault();
    }

    function endDrag() {
      dragIndex = -1;
    }

    function updateTableVisibility() {
      tableContainer.style.display = tableVisible ? "block" : "none";
      toggleTableBtn.textContent = tableVisible ? "Hide coordinates" : "Show coordinates";
    }

    async function calibrateHere() {
      try {
        const res = await postJSON("/calibrate", {});
        offsetsEl.textContent = `${res.rollOffset.toFixed(2)} / ${res.pitchOffset.toFixed(2)}°`;
        setStatus("Calibration saved");
      } catch (err) {
        setStatus(err.message, false);
      }
    }

    async function fetchStatus() {
      try {
        const s = await getJSON("/status");
        rollEl.textContent = s.calibratedRoll.toFixed(2);
        pitchEl.textContent = s.calibratedPitch.toFixed(2);
        activePresetEl.textContent = s.activePreset;
        offsetsEl.textContent = `${s.rollOffset.toFixed(2)} / ${s.pitchOffset.toFixed(2)}°`;
        const leftConnected = !!s.leftConnected;
        const rightConnected = !!s.rightConnected;
        const leftCmd = typeof s.leftServo === "number" ? s.leftServo : NaN;
        const rightCmd = typeof s.rightServo === "number" ? s.rightServo : NaN;
        const leftAct = typeof s.leftActual === "number" ? s.leftActual : NaN;
        const rightAct = typeof s.rightActual === "number" ? s.rightActual : NaN;
        const leftDelta = typeof s.leftDelta === "number" ? s.leftDelta : NaN;
        const rightDelta = typeof s.rightDelta === "number" ? s.rightDelta : NaN;

        if (!leftConnected && !rightConnected) {
          servoConnMsg.textContent = "Servos not connected";
        } else {
          const parts = [];
          if (leftConnected) parts.push("Left connected");
          if (rightConnected) parts.push("Right connected");
          servoConnMsg.textContent = parts.join(" · ");
        }

        if (!Number.isNaN(leftCmd)) {
          leftServoEl.textContent = leftCmd.toFixed(1);
          wingLeft.style.transform = `rotate(${-leftCmd}deg)`;
        }
        if (!Number.isNaN(rightCmd)) {
          rightServoEl.textContent = rightCmd.toFixed(1);
          wingRight.style.transform = `rotate(${-rightCmd}deg)`;
        }
        leftActualEl.textContent = leftConnected && !Number.isNaN(leftAct) ? leftAct.toFixed(1) + "°" : "—";
        rightActualEl.textContent = rightConnected && !Number.isNaN(rightAct) ? rightAct.toFixed(1) + "°" : "—";
        leftDeltaEl.textContent = leftConnected && !Number.isNaN(leftDelta) ? leftDelta.toFixed(1) + "°" : "—";
        rightDeltaEl.textContent = rightConnected && !Number.isNaN(rightDelta) ? rightDelta.toFixed(1) + "°" : "—";
        const currentSelect = parseInt(presetSelect.value, 10);
        // Only auto-sync the selector when the user hasn't manually chosen a different preset
        if (!userPresetOverride && currentSelect !== s.activePreset) {
          presetSelect.value = s.activePreset;
          if (lastLoadedPreset !== s.activePreset) {
            await loadPresetData();
          }
        } else if (!loadingPreset && lastLoadedPreset !== currentSelect) {
          // Ensure table matches selection when user has switched presets
          await loadPresetData();
        }
      } catch (err) {
        setStatus("Status error: " + err.message, false);
      }
    }

    function renderTable(data) {
      const container = document.getElementById("tableContainer");
      const len = (data.rollInput || []).length;
      let html = "<table><thead><tr><th>#</th>";
      fields.forEach(f => html += `<th>${f}</th>`);
      html += "</tr></thead><tbody>";
      for (let i = 0; i < len; i++) {
        html += `<tr><td>${i}</td>`;
        fields.forEach(f => {
          const rawVal = data[f] && data[f][i] !== undefined ? data[f][i] : "";
          const numVal = parseFloat(rawVal);
          const val = !Number.isNaN(numVal) ? numVal.toFixed(1) : rawVal;
          const readonly = f === "rollInput" || f === "pitchInput" || f === "pitchToLeft";
          html += `<td><input type="number" step="0.1" data-field="${f}" data-index="${i}" value="${val}" ${readonly ? "readonly" : ""}></td>`;
        });
        html += "</tr>";
      }
      html += "</tbody></table>";
      container.innerHTML = html;
      container.querySelectorAll("input[data-field]").forEach(el => {
        el.addEventListener("change", onFieldChange);
      });
    }

    async function onFieldChange(e) {
      const preset = parseInt(presetSelect.value, 10);
      const field = e.target.dataset.field;
      const index = parseInt(e.target.dataset.index, 10);
      const value = parseFloat(e.target.value);
      if (Number.isNaN(value)) {
        setStatus("Please enter a number", false);
        return;
      }
      if (field === "rollInput" || field === "pitchInput") {
        // Inputs are fixed; revert display to stored value
        if (currentPresetData && currentPresetData[field]) {
          e.target.value = currentPresetData[field][index];
        }
        setStatus("Input points are fixed", false);
        return;
      }
      const clamped = clamp(value, 0, 100);
      const quantized = quantizeTenth(clamped);
      e.target.value = quantized.toFixed(1);
      try {
        if (currentPresetData) {
          if (field === "pitchToRight" || field === "pitchToLeft") {
            currentPresetData.pitchToRight[index] = quantized;
            enforcePitchTieLocal();
            await postJSON("/set", { preset, field: "pitchToRight", index, value: quantized });
            setStatus(`Updated pitch[${index}] on preset ${preset}`);
          } else if (field === "rollToRight") {
            currentPresetData.rollToRight[index] = quantized;
            enforceRollSymmetryLocal();
            await postJSON("/set", { preset, field: "rollToRight", index, value: quantized });
            setStatus(`Updated roll right[${index}] on preset ${preset}`);
          } else if (field === "rollToLeft") {
            const mirror = findMirrorIndexLocal(index);
            currentPresetData.rollToRight[mirror] = quantized;
            enforceRollSymmetryLocal();
            await postJSON("/set", { preset, field: "rollToLeft", index, value: quantized });
            setStatus(`Updated roll left[${index}] on preset ${preset}`);
          }
        }
      } catch (err) {
        setStatus(err.message, false);
      }
      drawCurve();
    }

    presetSelect.addEventListener("change", () => {
      userPresetOverride = true;
      applyPresetSelection(parseInt(presetSelect.value, 10));
    });
    curveSelect.addEventListener("change", () => setCurve(curveSelect.value));
    canvas.addEventListener("pointerdown", (e) => {
      canvas.setPointerCapture(e.pointerId);
      beginDrag(e);
    });
    canvas.addEventListener("pointermove", moveDrag);
    canvas.addEventListener("pointerup", (e) => {
      endDrag();
      canvas.releasePointerCapture(e.pointerId);
    });
    canvas.addEventListener("pointerleave", endDrag);
    window.addEventListener("resize", drawCurve);

    toggleTableBtn.addEventListener("click", () => {
      tableVisible = !tableVisible;
      updateTableVisibility();
    });
    document.getElementById("resetBtn").addEventListener("click", resetPreset);
    document.getElementById("savePresetBtn").addEventListener("click", savePreset);
    document.getElementById("calibrateBtn").addEventListener("click", calibrateHere);

    window.addEventListener("load", () => {
      setCurve(curveSelect.value);
      fetchStatus();
      loadPresetData();
      setInterval(fetchStatus, 600);
      updateTableVisibility();
    });
  })();
  </script>
</body>
</html>
)rawliteral";

#endif
