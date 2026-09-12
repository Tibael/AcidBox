/* ===========================================================================
 * AcidBox — mixer web UI
 * API :
 *   GET  /api/state  -> { synth1:{mute,solo,audible}, synth2:{...}, drums:{...} }
 *   POST /api/mute   { instrument, muted }
 *   POST /api/solo   { instrument, solo }
 * ========================================================================== */
(function () {
  "use strict";

  var connEl = document.getElementById("conn");
  var statusEl = document.getElementById("status");
  var instruments = ["synth1", "synth2", "drums"];

  // ---- helpers -------------------------------------------------------------
  function setConnected(ok) {
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

  // Apply the state of one channel to its UI block.
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

  // Full re-sync from the server (source of truth for audible_*).
  function loadState() {
    fetch("/api/state", { headers: { "Accept": "application/json" } })
      .then(function (r) {
        if (!r.ok) throw new Error("HTTP " + r.status);
        return r.json();
      })
      .then(function (state) {
        setConnected(true);
        for (var i = 0; i < instruments.length; i++) {
          if (state[instruments[i]]) applyChannel(instruments[i], state[instruments[i]]);
        }
        setStatus("ok");
      })
      .catch(function (err) {
        setConnected(false);
        setStatus("no server: " + err.message);
      });
  }

  // Optimistic toggle: flip local button state, then confirm via API.
  function sendAction(kind, inst, value) {
    var url = kind === "mute" ? "/api/mute" : "/api/solo";
    var payload = { instrument: inst };
    if (kind === "mute") payload.muted = value; else payload.solo = value;

    postJSON(url, payload)
      .then(function (r) { return r.json(); })
      .then(function (res) {
        if (!res || res.ok !== true) {
          setStatus(kind + " " + inst + ": refused");
          loadState(); // roll back to server truth
          return;
        }
        setStatus(kind + " " + inst + " = " + (value ? "on" : "off"));
        loadState(); // refetch audible_* computed on firmware side
      })
      .catch(function (err) {
        setConnected(false);
        setStatus("error: " + err.message);
        loadState();
      });
  }

  // ---- DOM binding ---------------------------------------------------------
  function bind() {
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
    bind();
    loadState();
    // Periodic re-sync keeps the UI coherent with firmware
    // (future: ESP-NOW remote mutes, etc). 2s cadence, cheap GET.
    setInterval(loadState, 2000);
  });
})();