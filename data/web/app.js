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
    // Mixer page (legacy): bind channels + poll state.
    // Drumpad page: state poll marks "connected" on successful load/pad.
    if (document.getElementById("mixer")) {
      bindMixer();
      loadState();
      setInterval(loadState, 2000);
    } else {
      loadState();
    }
  });
})();