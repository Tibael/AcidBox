/* ===========================================================================
 * AcidBox — web UI (drumpad + mixer)
 * API :
 *   GET  /api/state  -> { synth1:{mute,solo,audible}, synth2:{...}, drums:{...} }
 *   POST /api/mute   { instrument, muted }
 *   POST /api/solo   { instrument, solo }
 *   POST /api/pad    { note, velocity }
 * ========================================================================== */
(function () {
  "use strict";

  var connEl = document.getElementById("conn");
  var statusEl = document.getElementById("status");
  var instruments = ["synth1", "synth2", "drums"];

  // ---- helpers -------------------------------------------------------------
  function setConnected(ok) {
    if (!connEl) return;
    connEl.textContent = ok ? "connected" : "offline";
    connEl.className = "conn-state " + (ok ? "on" : "off");
  }

  function setStatus(msg) {
    if (statusEl) statusEl.textContent = msg;
  }

  function postJSON(url, payload) {
    return fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
  }

  // ---- drumpad -------------------------------------------------------------
  function playPad(note, padEl) {
    padEl.classList.add("active");
    setTimeout(function () { padEl.classList.remove("active"); }, 120);
    setStatus("Pad " + note + " played");
    postJSON("/api/pad", { note: parseInt(note, 10), velocity: 127 })
      .then(function (r) {
        if (!r.ok) throw new Error("HTTP " + r.status);
        setConnected(true);
      })
      .catch(function () {
        setConnected(false);
        setStatus("pad error");
      });
  }

  function bindPads() {
    if (!document.getElementById("drumpad")) return;
    var pads = document.querySelectorAll(".pad");
    for (var i = 0; i < pads.length; i++) {
      (function (pad) {
        pad.addEventListener("pointerdown", function (e) {
          if (e.pointerType === "touch") e.preventDefault();
          playPad(pad.getAttribute("data-note"), pad);
        });
      })(pads[i]);
    }
  }

  // ---- mixer (page /mixer or legacy index) ---------------------------------
  function applyChannel(inst, state) {
    var block = document.querySelector('.channel[data-instrument="' + inst + '"]');
    if (!block) return;
    var muteBtn = block.querySelector(".btn.mute");
    var soloBtn = block.querySelector(".btn.solo");
    var led = block.querySelector(".led");

    muteBtn.classList.toggle("on", !!state.mute);
    muteBtn.setAttribute("aria-pressed", state.mute ? "true" : "false");
    soloBtn.classList.toggle("on", !!state.solo);
    soloBtn.setAttribute("aria-pressed", state.solo ? "true" : "false");
    led.classList.toggle("on", !!state.audible);
    block.classList.toggle("muted", !!state.mute);
    block.classList.toggle("soloed", !!state.solo);
    block.classList.toggle("silent", !state.audible);
  }

  function loadState() {
    fetch("/api/state", { headers: { "Accept": "application/json" } })
      .then(function (r) {
        if (!r.ok) throw new Error("HTTP " + r.status);
        return r.json();
      })
      .then(function (state) {
        setConnected(true);
        if (document.getElementById("mixer")) {
          for (var i = 0; i < instruments.length; i++) {
            if (state[instruments[i]]) applyChannel(instruments[i], state[instruments[i]]);
          }
        }
        setStatus("ok");
      })
      .catch(function (err) {
        setConnected(false);
        setStatus("no server: " + err.message);
      });
  }

  function sendAction(kind, inst, value) {
    var url = kind === "mute" ? "/api/mute" : "/api/solo";
    var payload = { instrument: inst };
    if (kind === "mute") payload.muted = value; else payload.solo = value;

    postJSON(url, payload)
      .then(function (r) { return r.json(); })
      .then(function (res) {
        if (!res || res.ok !== true) {
          setStatus(kind + " " + inst + ": refused");
          loadState();
          return;
        }
        setStatus(kind + " " + inst + " = " + (value ? "on" : "off"));
        loadState();
      })
      .catch(function (err) {
        setConnected(false);
        setStatus("error: " + err.message);
        loadState();
      });
  }

  function bindMixer() {
    if (!document.getElementById("mixer")) return;
    for (var i = 0; i < instruments.length; i++) {
      (function (inst) {
        var block = document.querySelector('.channel[data-instrument="' + inst + '"]');
        if (!block) return;
        var muteBtn = block.querySelector(".btn.mute");
        var soloBtn = block.querySelector(".btn.solo");

        muteBtn.addEventListener("click", function () {
          var nowOn = muteBtn.classList.toggle("on");
          muteBtn.setAttribute("aria-pressed", nowOn ? "true" : "false");
          sendAction("mute", inst, nowOn);
        });
        soloBtn.addEventListener("click", function () {
          var nowOn = soloBtn.classList.toggle("on");
          soloBtn.setAttribute("aria-pressed", nowOn ? "true" : "false");
          sendAction("solo", inst, nowOn);
        });
      })(instruments[i]);
    }
  }

  // ---- init ----------------------------------------------------------------
  document.addEventListener("DOMContentLoaded", function () {
    bindPads();
    if (document.getElementById("mixer")) {
      bindMixer();
      loadState();
      setInterval(loadState, 2000);
    } else if (document.getElementById("filter-page")) {
      bindFilter();
      loadState();
    } else if (document.getElementById("memory-page")) {
      bindMemory();
      loadState();
    } else {
      loadState();
    }
  });

  // ---- filter page ----------------------------------------------------------
  function bindFilter() {
    var playBtn = document.querySelector(".play-selected");
    var chanSel = document.getElementById("filter-chan");
    function getChan() { return chanSel ? parseInt(chanSel.value, 10) : 10; }
    if (playBtn) {
      playBtn.addEventListener("click", function () {
        var noteInput = document.querySelector(".filter-note");
        var note = parseInt(noteInput.value, 10);
        if (note >= 0 && note <= 127) playPad(note, playBtn);
      });
    }
    // restore saved slider values
    try {
      var saved = JSON.parse(localStorage.getItem("acidbox-ccs") || "{}");
      var sliders = document.querySelectorAll(".cc-slider");
      for (var i = 0; i < sliders.length; i++) {
        var cc = parseInt(sliders[i].getAttribute("data-cc"), 10);
        if (saved[cc] !== undefined) {
          sliders[i].value = saved[cc];
          var out = sliders[i].parentNode.querySelector(".cc-val");
          if (out) out.textContent = saved[cc];
        }
      }
    } catch(e) {}
    var sliders = document.querySelectorAll(".cc-slider");
    for (var i = 0; i < sliders.length; i++) {
      (function (slider) {
        var output = slider.parentNode.querySelector(".cc-val");
        slider.addEventListener("input", function () {
          var cc = parseInt(slider.getAttribute("data-cc"), 10);
          var val = parseInt(slider.value, 10);
          if (output) output.textContent = val;
          // F5: persist
          try {
            var store = JSON.parse(localStorage.getItem("acidbox-ccs") || "{}");
            store[cc] = val;
            localStorage.setItem("acidbox-ccs", JSON.stringify(store));
          } catch(e) {}
          postJSON("/api/cc", { channel: getChan(), cc: cc, value: val })
            .catch(function () { setStatus("cc error"); });
        });
      })(sliders[i]);
    }
    // F5: send all CCs to firmware on load (from localStorage or HTML defaults)
    try {
      var saved = JSON.parse(localStorage.getItem("acidbox-ccs") || "{}");
      var hasKeys = false;
      Object.keys(saved).forEach(function(cc) {
        hasKeys = true;
        postJSON("/api/cc", { channel: getChan(), cc: parseInt(cc,10), value: saved[cc] });
      });
      // First visit: send all HTML default values to firmware
      if (!hasKeys) {
        var defaults = {};
        var sliders = document.querySelectorAll(".cc-slider");
        for (var i = 0; i < sliders.length; i++) {
          var cc = parseInt(sliders[i].getAttribute("data-cc"), 10);
          var val = parseInt(sliders[i].value, 10);
          defaults[cc] = val;
          postJSON("/api/cc", { channel: getChan(), cc: cc, value: val });
        }
        localStorage.setItem("acidbox-ccs", JSON.stringify(defaults));
      }
    } catch(e) {}
    // Presets
    refreshPresetList();
    document.getElementById("preset-save").addEventListener("click", function () {
      var name = document.getElementById("preset-name").value || "New";
      var note = parseInt(document.querySelector(".filter-note").value, 10);
      var ccs = {};
      document.querySelectorAll(".cc-slider").forEach(function(s){ ccs[s.dataset.cc]=parseInt(s.value,10); });
      postJSON("/api/preset/save", { name: name, note: note, ccs: ccs })
        .then(function (r) { return r.json(); })
        .then(function (res) { if (res.ok) { setStatus("Preset " + res.id + " saved"); refreshPresetList(); } })
        .catch(function () { setStatus("save failed"); });
    });
    document.getElementById("preset-load").addEventListener("click", function () {
      var sel = document.getElementById("preset-list");
      var id = parseInt(sel.value, 10);
      if (isNaN(id)) return;
      postJSON("/api/preset/load", { id: id })
        .then(function (r) { return r.json(); })
        .then(function (p) {
          document.querySelector(".filter-note").value = p.note;
          var ccs = p.ccs || {};
          var sliders = document.querySelectorAll(".cc-slider");
          var store = {};
          for (var i = 0; i < sliders.length; i++) {
            var cc = parseInt(sliders[i].getAttribute("data-cc"), 10);
            var val = ccs[cc] !== undefined ? ccs[cc] : 64;
            sliders[i].value = val;
            store[cc] = val;
            var out = sliders[i].parentNode.querySelector(".cc-val");
            if (out) out.textContent = val;
            postJSON("/api/cc", { channel: getChan(), cc: cc, value: val });
          }
          try { localStorage.setItem("acidbox-ccs", JSON.stringify(store)); } catch(e) {}
          document.getElementById("preset-name").value = p.name || "";
          setStatus("Preset loaded: " + (p.name || id));
        })
        .catch(function () { setStatus("load failed"); });
    });
    document.getElementById("preset-del").addEventListener("click", function () {
      var sel = document.getElementById("preset-list");
      var id = parseInt(sel.value, 10);
      if (isNaN(id)) return;
      if (!confirm("Delete preset " + id + "?")) return;
      postJSON("/api/preset/del", { id: id })
        .then(function (r) { return r.json(); })
        .then(function (res) { if (res.ok) { setStatus("Preset deleted"); refreshPresetList(); } })
        .catch(function () { setStatus("delete failed"); });
    });
    // Assign to Sensor
    document.getElementById("assign-btn").addEventListener("click", function () {
      var id = parseInt(document.getElementById("assign-trigger").value, 10);
      var note = parseInt(document.querySelector(".filter-note").value, 10);
      var vel = 127;
      postJSON("/api/trigger", { id: id, channel: 10, note: note, velocity: vel })
        .then(function (r) { return r.json(); })
        .then(function (res) { if (res.ok) { setStatus("Assigned to Trigger " + (id+1)); } })
        .catch(function () { setStatus("assign failed"); });
    });
  }
  function refreshPresetList() {
    fetch("/api/preset/list")
      .then(function (r) { return r.json(); })
      .then(function (data) {
        var sel = document.getElementById("preset-list");
        sel.innerHTML = "";
        var presets = data.presets || [];
        for (var i = 0; i < presets.length; i++) {
          var opt = document.createElement("option");
          opt.value = presets[i].id;
          opt.textContent = presets[i].id + ": " + (presets[i].name || "(empty)");
          sel.appendChild(opt);
        }
      })
      .catch(function () { setStatus("preset list failed"); });
  }
  // ---- memory page -----------------------------------------------------------
  function bindMemory() {
    refreshMemList();
    document.getElementById("mem-save").addEventListener("click", function () {
      var name = document.getElementById("mem-name").value || "Memory";
      var ccs = {};
      try { ccs = JSON.parse(localStorage.getItem("acidbox-ccs") || "{}"); } catch(e) {}
      postJSON("/api/memory/save", { name: name, ccs: ccs })
        .then(function (r) { return r.json(); })
        .then(function (res) { if (res.ok) { setStatus("Memory " + res.id + " created"); refreshMemList(); } })
        .catch(function () { setStatus("memory create failed"); });
    });
    document.getElementById("mem-load").addEventListener("click", function () {
      var id = parseInt(document.getElementById("mem-list").value, 10);
      if (isNaN(id)) return;
      postJSON("/api/memory/load", { id: id })
        .then(function (r) { return r.json(); })
        .then(function (res) {
          if (res.ok) setStatus("Memory " + id + " loaded — filters applied");
        })
        .catch(function () { setStatus("memory load failed"); });
    });
    document.getElementById("mem-del").addEventListener("click", function () {
      var id = parseInt(document.getElementById("mem-list").value, 10);
      if (isNaN(id)) return;
      postJSON("/api/memory/del", { id: id })
        .then(function (r) { return r.json(); })
        .then(function (res) { if (res.ok) { setStatus("Memory deleted"); refreshMemList(); } })
        .catch(function () { setStatus("memory delete failed"); });
    });
  }
  function refreshMemList() {
    fetch("/api/memory/list")
      .then(function (r) { return r.json(); })
      .then(function (data) {
        var sel = document.getElementById("mem-list");
        sel.innerHTML = "";
        var mems = data.memories || [];
        for (var i = 0; i < mems.length; i++) {
          var opt = document.createElement("option");
          opt.value = mems[i].id;
          opt.textContent = mems[i].id + ": " + (mems[i].name || "(empty)");
          sel.appendChild(opt);
        }
      })
      .catch(function () { setStatus("memory list failed"); });
  }
})();