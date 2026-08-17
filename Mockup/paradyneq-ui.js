(() => {
  "use strict";

  // ---- Design-space constants, matching the real plugin's own SpectrumAnalyzer/Parameters
  // exactly (Source/Parameters.h, Source/SpectrumAnalyzer.cpp) so the curve math here is the
  // same math that belongs in the eventual JUCE port, not an approximation of it. ----
  const MIN_FREQ = 20, MAX_FREQ = 20000;
  const GRAPH_MIN_DB = -60, GRAPH_MAX_DB = 12; // left axis (spectrum trace)
  const SAMPLE_RATE = 48000; // only used for the RBJ magnitude-response maths below
  const SVG_W = 1180, SVG_H = 339;
  // Inset the plotted area from the SVG's own edges -- matches the real plugin's
  // graphBounds() (Source/SpectrumAnalyzer.cpp), which reserves left/right/bottom margin for
  // axis labels so text never gets clipped by the panel's own overflow:hidden border.
  const GRAPH_LEFT = 34, GRAPH_RIGHT = SVG_W - 46, GRAPH_TOP = 12, GRAPH_BOTTOM = SVG_H - 20;
  const FREQ_TICKS = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
  const MAX_BANDS = 8;

  let gainRangeDb = 24; // right axis, selectable via the Range dropdown -- mirrors the real
                        // plugin's SpectrumAnalyzer::setGainRangeDb()

  // ---- Coordinate mapping -- identical formulas to SpectrumAnalyzer::frequencyToX /
  // ::spectrumDbToY / ::gainToY, just expressed in JS against the inset graph rect. ----
  const freqToX = (hz) => {
    const t = Math.log(Math.max(MIN_FREQ, Math.min(MAX_FREQ, hz)) / MIN_FREQ) / Math.log(MAX_FREQ / MIN_FREQ);
    return GRAPH_LEFT + t * (GRAPH_RIGHT - GRAPH_LEFT);
  };
  const xToFreq = (x) => {
    const t = Math.max(0, Math.min(1, (x - GRAPH_LEFT) / (GRAPH_RIGHT - GRAPH_LEFT)));
    return MIN_FREQ * Math.pow(MAX_FREQ / MIN_FREQ, t);
  };
  const dbToY = (db) => {
    const clamped = Math.max(GRAPH_MIN_DB, Math.min(GRAPH_MAX_DB, db));
    return GRAPH_BOTTOM - ((clamped - GRAPH_MIN_DB) / (GRAPH_MAX_DB - GRAPH_MIN_DB)) * (GRAPH_BOTTOM - GRAPH_TOP);
  };
  const gainToY = (db) => {
    const clamped = Math.max(-gainRangeDb, Math.min(gainRangeDb, db));
    return GRAPH_BOTTOM - ((clamped + gainRangeDb) / (gainRangeDb * 2)) * (GRAPH_BOTTOM - GRAPH_TOP);
  };
  const yToGain = (y) => {
    const t = Math.max(0, Math.min(1, (GRAPH_BOTTOM - y) / (GRAPH_BOTTOM - GRAPH_TOP)));
    return t * gainRangeDb * 2 - gainRangeDb;
  };

  // ---- RBJ Audio EQ Cookbook magnitude response -- ported verbatim from
  // SpectrumAnalyzer::bandMagnitudeResponseDb (Source/SpectrumAnalyzer.cpp), including the
  // shelf-slope fix (S derived from Q, clamped to (0,1]) documented there. This is a visual
  // approximation of the real SVFilter audio path, same caveat as the C++ original. ----
  function bandMagnitudeDb(band, hz) {
    if (!band.active) return 0;
    const w0 = 2 * Math.PI * band.freq / SAMPLE_RATE;
    const q = Math.max(0.05, band.q);
    const alpha = Math.sin(w0) / (2 * q);
    const cosw0 = Math.cos(w0);
    const A = Math.pow(10, band.gain / 40);
    const sqrtA = Math.sqrt(A);
    const shelfSlope = Math.max(0.1, Math.min(1, q / 2));
    const alphaShelf = (Math.sin(w0) / 2) * Math.sqrt((A + 1 / A) * (1 / shelfSlope - 1) + 2);

    let b0, b1, b2, a0, a1, a2;
    switch (band.filterType) {
      case "Low Shelf":
        b0 = A * ((A + 1) - (A - 1) * cosw0 + 2 * sqrtA * alphaShelf);
        b1 = 2 * A * ((A - 1) - (A + 1) * cosw0);
        b2 = A * ((A + 1) - (A - 1) * cosw0 - 2 * sqrtA * alphaShelf);
        a0 = (A + 1) + (A - 1) * cosw0 + 2 * sqrtA * alphaShelf;
        a1 = -2 * ((A - 1) + (A + 1) * cosw0);
        a2 = (A + 1) + (A - 1) * cosw0 - 2 * sqrtA * alphaShelf;
        break;
      case "High Shelf":
        b0 = A * ((A + 1) + (A - 1) * cosw0 + 2 * sqrtA * alphaShelf);
        b1 = -2 * A * ((A - 1) + (A + 1) * cosw0);
        b2 = A * ((A + 1) + (A - 1) * cosw0 - 2 * sqrtA * alphaShelf);
        a0 = (A + 1) - (A - 1) * cosw0 + 2 * sqrtA * alphaShelf;
        a1 = 2 * ((A - 1) - (A + 1) * cosw0);
        a2 = (A + 1) - (A - 1) * cosw0 - 2 * sqrtA * alphaShelf;
        break;
      case "High Pass":
        b0 = (1 + cosw0) / 2; b1 = -(1 + cosw0); b2 = (1 + cosw0) / 2;
        a0 = 1 + alpha; a1 = -2 * cosw0; a2 = 1 - alpha;
        break;
      case "Low Pass":
        b0 = (1 - cosw0) / 2; b1 = 1 - cosw0; b2 = (1 - cosw0) / 2;
        a0 = 1 + alpha; a1 = -2 * cosw0; a2 = 1 - alpha;
        break;
      default: // Bell
        b0 = 1 + alpha * A; b1 = -2 * cosw0; b2 = 1 - alpha * A;
        a0 = 1 + alpha / A; a1 = -2 * cosw0; a2 = 1 - alpha / A;
        break;
    }
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;

    const w = 2 * Math.PI * hz / SAMPLE_RATE;
    const zRe = Math.cos(-w), zIm = Math.sin(-w);
    const z2Re = Math.cos(-2 * w), z2Im = Math.sin(-2 * w);
    const numRe = b0 + b1 * zRe + b2 * z2Re, numIm = b1 * zIm + b2 * z2Im;
    const denRe = 1 + a1 * zRe + a2 * z2Re, denIm = a1 * zIm + a2 * z2Im;
    const numMag = Math.hypot(numRe, numIm), denMag = Math.hypot(denRe, denIm);
    let db = 20 * Math.log10(Math.max(1e-6, numMag / Math.max(1e-9, denMag)));

    if (band.filterType === "High Pass" || band.filterType === "Low Pass") {
      const orderMul = band.slope === 6 ? 0.5 : band.slope === 18 ? 1.5 : band.slope === 24 ? 2 : 1;
      db *= orderMul;
    }
    return db;
  }

  function summedResponseDb(bands, hz) {
    return bands.reduce((total, band) => total + bandMagnitudeDb(band, hz), 0);
  }

  // ---- Fixed pseudo-random "spectrum trace" -- there is no live audio in a mockup, this just
  // stands in for the real per-frame FFT trace the plugin draws from SpectrumFifo. ----
  function makeFakeSpectrum() {
    let seed = 1337;
    const rand = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
    const points = [];
    for (let i = 0; i <= 200; i++) {
      const t = i / 200;
      const hz = MIN_FREQ * Math.pow(MAX_FREQ / MIN_FREQ, t);
      const tilt = -18 * t; // gentle downward tilt toward the top end, like real programme material
      const noise = (rand() - 0.5) * 14 + Math.sin(t * 40) * 3;
      points.push({ hz, db: -22 + tilt + noise });
    }
    return points;
  }
  const fakeSpectrum = makeFakeSpectrum();

  // ---- Band data model ----
  function makeBand(freq, gain) {
    return {
      active: true, freq, gain, q: 0.707, filterType: "Bell", slope: 12, channelMode: "Stereo",
      dynamicEnabled: false, solo: false, mute: false,
      threshold: -24, ratio: 2, attack: 15, release: 150, knee: 6,
    };
  }
  const bands = [makeBand(1248.8, 0.08), makeBand(2400, -3)];
  let selected = 0;

  // ---- DOM refs ----
  const svg = document.getElementById("spectrumSvg");
  const panel = document.getElementById("spectrumPanel");
  const tabsHost = document.getElementById("bandTabs");
  const plugin = document.querySelector(".moth-plugin");
  const ns = "http://www.w3.org/2000/svg";
  const el = (tag, attrs) => { const e = document.createElementNS(ns, tag); for (const k in attrs) e.setAttribute(k, attrs[k]); return e; };

  function drawGrid() {
    FREQ_TICKS.forEach((hz) => {
      const x = freqToX(hz);
      svg.appendChild(el("line", { class: "paradyn-grid-line", x1: x, y1: 0, x2: x, y2: SVG_H }));
      const label = hz >= 1000 ? `${hz / 1000}k` : `${hz}`;
      svg.appendChild(Object.assign(el("text", { class: "paradyn-axis-label is-freq", x, y: SVG_H - 6 }), { textContent: label }));
    });
    for (let db = GRAPH_MIN_DB; db <= GRAPH_MAX_DB; db += 12) {
      const y = dbToY(db);
      svg.appendChild(el(db === 0 ? "line" : "line", { class: db === 0 ? "paradyn-grid-line" : "paradyn-grid-line", x1: 0, y1: y, x2: SVG_W, y2: y }));
      svg.appendChild(Object.assign(el("text", { class: "paradyn-axis-label is-left", x: 4, y: y - 3 }), { textContent: (db > 0 ? "+" : "") + db }));
    }
    for (let i = -2; i <= 2; i++) {
      const db = gainRangeDb * i * 0.5;
      const y = gainToY(db);
      svg.appendChild(Object.assign(el("text", { class: "paradyn-axis-label is-right", x: SVG_W - 4, y: y - 3 }), { textContent: (db > 0 ? "+" : "") + db.toFixed(1) }));
    }
    const zeroY = gainToY(0);
    svg.appendChild(el("line", { class: "paradyn-zero-line", x1: 0, y1: zeroY, x2: SVG_W, y2: zeroY }));
  }

  let spectrumPath, responsePath;
  function drawTraces() {
    const spectrumD = fakeSpectrum.map((p, i) => `${i === 0 ? "M" : "L"}${freqToX(p.hz).toFixed(1)},${dbToY(p.db).toFixed(1)}`).join(" ");
    spectrumPath.setAttribute("d", spectrumD);

    let responseD = "";
    for (let px = 0; px <= SVG_W; px += 4) {
      const hz = xToFreq(px);
      const db = summedResponseDb(bands, hz);
      responseD += `${px === 0 ? "M" : "L"}${px},${gainToY(db).toFixed(1)} `;
    }
    responsePath.setAttribute("d", responseD);
  }

  // ---- Band nodes (draggable) ----
  const nodeEls = [];
  function rebuildNodes() {
    nodeEls.forEach((n) => n.remove());
    nodeEls.length = 0;
    bands.forEach((band, i) => {
      const node = document.createElement("div");
      node.className = "paradyn-band-node" + (i === selected ? " is-selected" : "");
      panel.appendChild(node);
      nodeEls.push(node);
      positionNode(node, band);

      node.addEventListener("pointerdown", (event) => {
        selectBand(i);
        node.setPointerCapture(event.pointerId);
        const onMove = (moveEvent) => {
          const rect = panel.getBoundingClientRect();
          const x = ((moveEvent.clientX - rect.left) / rect.width) * SVG_W;
          const y = ((moveEvent.clientY - rect.top) / rect.height) * SVG_H;
          band.freq = Math.round(xToFreq(x) * 10) / 10;
          band.gain = Math.round(yToGain(y) * 100) / 100;
          positionNode(node, band);
          drawTraces();
          if (i === selected) syncEditorFromBand();
        };
        const onUp = () => {
          window.removeEventListener("pointermove", onMove);
          window.removeEventListener("pointerup", onUp);
        };
        window.addEventListener("pointermove", onMove);
        window.addEventListener("pointerup", onUp);
        event.preventDefault();
      });
    });
  }
  function positionNode(node, band) {
    node.style.left = (freqToX(band.freq) / SVG_W * 100) + "%";
    node.style.top = (gainToY(band.gain) / SVG_H * 100) + "%";
  }

  // ---- Band tabs ----
  function rebuildTabs() {
    tabsHost.innerHTML = "";
    bands.forEach((band, i) => {
      const tab = document.createElement("button");
      tab.className = "moth-tab" + (i === selected ? " is-active" : "");
      tab.textContent = `Band ${i + 1}`;
      tab.setAttribute("aria-selected", String(i === selected));
      tab.addEventListener("click", () => selectBand(i));
      tabsHost.appendChild(tab);
    });
    const addBtn = document.createElement("button");
    addBtn.className = "paradyn-add-band";
    addBtn.type = "button";
    addBtn.textContent = "+";
    addBtn.setAttribute("aria-label", "Add band");
    addBtn.addEventListener("click", addBand);
    tabsHost.appendChild(addBtn);
  }

  function addBand() {
    if (bands.length >= MAX_BANDS) return;
    const defaults = [100, 200, 400, 800, 1600, 3200, 6400, 12800];
    bands.push(makeBand(defaults[bands.length % defaults.length], 0));
    selected = bands.length - 1;
    rebuildTabs();
    rebuildNodes();
    drawTraces();
    syncEditorFromBand();
  }

  function selectBand(i) {
    selected = i;
    rebuildTabs();
    nodeEls.forEach((n, idx) => n.classList.toggle("is-selected", idx === selected));
    syncEditorFromBand();
  }

  // ---- Band editor <-> model sync ----
  const filterTypeSelect = document.getElementById("filterTypeSelect");
  const channelModeSelect = document.getElementById("channelModeSelect");
  const dynamicCheck = document.getElementById("dynamicCheck");
  const soloCheck = document.getElementById("soloCheck");
  const muteCheck = document.getElementById("muteCheck");
  const dynamicKnobs = document.getElementById("dynamicKnobs");
  const freqKnobInput = document.querySelector("#freqKnob input");
  const freqReadout = document.getElementById("freqReadout");

  const linearFields = [
    ["gain", document.querySelector('input[data-readout="gainReadout"]')],
    ["q", document.querySelector('input[data-readout="qReadout"]')],
    ["threshold", document.querySelector('input[data-readout="thresholdReadout"]')],
    ["ratio", document.querySelector('input[data-readout="ratioReadout"]')],
    ["attack", document.querySelector('input[data-readout="attackReadout"]')],
    ["release", document.querySelector('input[data-readout="releaseReadout"]')],
    ["knee", document.querySelector('input[data-readout="kneeReadout"]')],
  ];

  function freqToKnobPosition(hz) {
    return 1000 * Math.log(hz / MIN_FREQ) / Math.log(MAX_FREQ / MIN_FREQ);
  }
  function knobPositionToFreq(pos) {
    return MIN_FREQ * Math.pow(MAX_FREQ / MIN_FREQ, pos / 1000);
  }

  function syncEditorFromBand() {
    const band = bands[selected];
    filterTypeSelect.value = band.filterType;
    channelModeSelect.value = band.channelMode;
    dynamicCheck.checked = band.dynamicEnabled;
    soloCheck.checked = band.solo;
    muteCheck.checked = band.mute;
    dynamicKnobs.dataset.disabled = String(!band.dynamicEnabled);

    freqKnobInput.value = freqToKnobPosition(band.freq);
    freqKnobInput.dispatchEvent(new Event("input"));
    freqReadout.textContent = band.freq >= 1000 ? `${(band.freq / 1000).toFixed(3)} kHz` : `${band.freq.toFixed(1)} Hz`;

    linearFields.forEach(([key, input]) => {
      input.value = band[key];
      input.dispatchEvent(new Event("input"));
    });
    document.getElementById("ratioReadout").textContent = `${Number(document.querySelector('input[data-readout="ratioReadout"]').value).toFixed(2)} : 1`;
  }

  freqKnobInput.addEventListener("input", () => {
    const band = bands[selected];
    band.freq = Math.round(knobPositionToFreq(Number(freqKnobInput.value)) * 10) / 10;
    freqReadout.textContent = band.freq >= 1000 ? `${(band.freq / 1000).toFixed(3)} kHz` : `${band.freq.toFixed(1)} Hz`;
    positionNode(nodeEls[selected], band);
    drawTraces();
  });

  linearFields.forEach(([key, input]) => {
    input.addEventListener("input", () => {
      bands[selected][key] = Number(input.value);
      if (key === "ratio") document.getElementById("ratioReadout").textContent = `${Number(input.value).toFixed(2)} : 1`;
      if (key === "gain" || key === "q") { positionNode(nodeEls[selected], bands[selected]); drawTraces(); }
    });
  });

  filterTypeSelect.addEventListener("change", () => { bands[selected].filterType = filterTypeSelect.value; drawTraces(); });
  channelModeSelect.addEventListener("change", () => { bands[selected].channelMode = channelModeSelect.value; });
  dynamicCheck.addEventListener("change", () => {
    bands[selected].dynamicEnabled = dynamicCheck.checked;
    dynamicKnobs.dataset.disabled = String(!dynamicCheck.checked);
  });
  soloCheck.addEventListener("change", () => { bands[selected].solo = soloCheck.checked; });
  muteCheck.addEventListener("change", () => { bands[selected].mute = muteCheck.checked; });

  // ---- Header controls ----
  document.getElementById("bypassCheck").addEventListener("change", (event) => {
    plugin.classList.toggle("is-bypassed", event.target.checked);
  });
  document.getElementById("rangeSelect").addEventListener("change", (event) => {
    gainRangeDb = { "±3 dB": 3, "±6 dB": 6, "±12 dB": 12, "±24 dB": 24, "±48 dB": 48 }[event.target.value] ?? 24;
    redrawGrid();
  });

  function redrawGrid() {
    svg.innerHTML = "";
    drawGrid();
    spectrumPath = el("path", { class: "paradyn-spectrum-trace" });
    responsePath = el("path", { class: "paradyn-response-curve" });
    svg.appendChild(spectrumPath);
    svg.appendChild(responsePath);
    drawTraces();
    nodeEls.forEach((node, i) => positionNode(node, bands[i]));
  }

  // ---- Boot ----
  redrawGrid();
  rebuildTabs();
  rebuildNodes();
  syncEditorFromBand();
})();
