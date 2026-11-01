// ESP32 BT Trigger - web installer.
// Flow: connect (detect chip) -> settings (pre-filled from the board if it
// already runs this firmware) -> install firmware + 4 KB settings sector.
import { ESPLoader, Transport } from "../vendor/esptool-js-0.7.0.js";
import {
  CONFIG_OFFSET, CFG_SIZE, DEFAULTS, LED, PULL, TRIGGER,
  buildConfigSector, decodeConfig, encodeName,
} from "./config.js";
import { BOARDS, CHIPS, UNSUPPORTED, boardsForChip, outputPins } from "./boards.js";

const $ = (id) => document.getElementById(id);
const FLASH_BAUD = 460800;            // safe for CP210x / CH340 / native USB
const PARTITION_TABLE = 0x8000;
const APP_OFFSET = 0x10000;
const NVS_OFFSET = 0x9000;

const state = {
  manifest: null,
  port: null,
  transport: null,
  loader: null,
  chip: null,            // key of CHIPS, e.g. "ESP32-C3"
  boardSettings: null,   // settings read back from the board (or null)
  hasOurFirmware: false, // partition table contains "btcfg"
  fwCache: new Map(),
  busy: false,
  edition: "power",      // "power" | "pin"
};

/* ------------------------------------------------------------------ log */
const logEl = $("log");
function log(line) {
  logEl.textContent += line + "\n";
  logEl.scrollTop = logEl.scrollHeight;
}
const terminal = {
  clean() { logEl.textContent = ""; },
  writeLine(d) { log(d); },
  write(d) { logEl.textContent += d; logEl.scrollTop = logEl.scrollHeight; },
};
function setStatus(text, kind = "") {
  const el = $("connect-status");
  el.textContent = text;
  el.className = "status " + kind;
}

/* ------------------------------------------------------------- manifest */
async function loadManifest() {
  try {
    const r = await fetch("firmware/manifest.json", { cache: "no-cache" });
    if (!r.ok) throw new Error(r.status);
    state.manifest = await r.json();
  } catch (e) {
    state.manifest = null;
  }
  renderDownloads();
}

function renderDownloads() {
  const tb = $("dl-table").querySelector("tbody");
  if (!state.manifest) {
    tb.innerHTML = `<tr><td colspan="5">Firmware files not found. Get them from the
      <a href="https://github.com/Mattboxx/Esp32_Iphone_shortcuts_bluetooth_trigger/releases">Releases page</a>.</td></tr>`;
    return;
  }
  tb.innerHTML = "";
  for (const b of state.manifest.builds) {
    const tr = document.createElement("tr");
    const bt = b.transport === "classic"
      ? 'Classic HID <span class="tag ok">tested</span>'
      : 'BLE HID <span class="tag">beta</span>';
    const ed = (b.edition || "power") === "pin" ? "📍 Pin" : "⚡ Power-on";
    tr.innerHTML = `<td>${b.chip}</td><td>${ed}</td><td>${bt}</td>
      <td><a href="firmware/${b.file}" download>${b.file}</a></td>
      <td>${(b.size / 1024).toFixed(0)} KB</td>`;
    tb.appendChild(tr);
  }
}

function buildFor(chip, edition = state.edition) {
  return state.manifest?.builds.find((b) => b.chip === chip && (b.edition || "power") === edition) || null;
}

/* ------------------------------------------------------------- edition */
function setEdition(ed, fromBoard = false) {
  state.edition = ed;
  const radio = document.querySelector(`input[name="edition"][value="${ed}"]`);
  if (radio) radio.checked = true;
  const pin = ed === "pin";
  $("trigger-fieldset").hidden = !pin;
  $("choice-always").hidden = true;          // "always" is the power edition itself
  const cur = document.querySelector('input[name="trigger"]:checked');
  if (pin && (!cur || cur.value === "0")) {
    document.querySelector('input[name="trigger"][value="1"]').checked = true;
  }
  if (pin && !fromBoard && $("trigger-gpio").value === "-1") setPin($("trigger-gpio"), 4);
  refreshForm();
  renderFwInfo();
}

async function fetchFirmware(build) {
  if (state.fwCache.has(build.file)) return state.fwCache.get(build.file);
  const r = await fetch(`firmware/${build.file}`, { cache: "no-cache" });
  if (!r.ok) throw new Error(`Download failed (${r.status})`);
  const data = new Uint8Array(await r.arrayBuffer());
  if (crypto?.subtle && build.sha256) {
    const h = new Uint8Array(await crypto.subtle.digest("SHA-256", data));
    const hex = [...h].map((x) => x.toString(16).padStart(2, "0")).join("");
    if (hex !== build.sha256) throw new Error("Firmware checksum mismatch: reload the page and retry.");
  }
  state.fwCache.set(build.file, data);
  return data;
}

/* ------------------------------------------------------------ settings UI */
function fillPinSelect(sel, pins, allowNone = true) {
  const keep = sel.value;
  sel.innerHTML = "";
  if (allowNone) sel.add(new Option("None", "-1"));
  for (const g of pins) sel.add(new Option(`GPIO ${g}`, String(g)));
  if ([...sel.options].some((o) => o.value === keep)) sel.value = keep;
}

function setPin(sel, gpio) {
  const v = String(gpio);
  sel.value = [...sel.options].some((o) => o.value === v) ? v : "-1";
}

function populateForChip(chip) {
  const c = CHIPS[chip];
  const boardSel = $("board");
  boardSel.innerHTML = "";
  for (const b of boardsForChip(chip)) boardSel.add(new Option(b.name, b.id));
  document.querySelectorAll("select.pin-in").forEach((s) => fillPinSelect(s, c.inputs));
  document.querySelectorAll("select.pin-out").forEach((s) => fillPinSelect(s, outputPins(chip)));
  $("reconnect-row").hidden = c.transport !== "classic";
}

function applyBoardPreset(id) {
  const b = BOARDS.find((x) => x.id === id);
  if (!b) return;
  $("led-type").value = String(b.led.type);
  setPin($("led-gpio"), b.led.gpio);
  $("led-pol").value = b.led.activeLow ? "1" : "0";
  setPin($("button-gpio"), b.button);
  $("board-note").textContent = b.note;
  refreshForm();
}

function readForm() {
  const picked = Number(document.querySelector('input[name="trigger"]:checked').value);
  const trig = state.edition === "power" ? TRIGGER.always : Math.max(TRIGGER.switch, picked);
  return {
    name: $("name").value.trim() || DEFAULTS.name,
    reconnectS: Number($("reconnect").value) || DEFAULTS.reconnectS,
    pulseS: Number($("pulse").value) || DEFAULTS.pulseS,
    pairWindowS: Number($("pairwin").value),
    ledType: Number($("led-type").value),
    ledGpio: Number($("led-gpio").value),
    ledActiveLow: $("led-pol").value === "1",
    ledBrightness: DEFAULTS.ledBrightness,
    buttonGpio: Number($("button-gpio").value),
    triggerMode: trig,
    triggerGpio: Number($("trigger-gpio").value),
    triggerActiveHigh: $("trigger-pol").value === "1",
    triggerPull: Number($("trigger-pull").value),
    debounceMs: Number($("debounce").value) || DEFAULTS.debounceMs,
    holdS: Math.max(0, Number($("hold").value) || 0),
  };
}

function writeForm(s) {
  $("name").value = s.name;
  $("reconnect").value = s.reconnectS;
  $("pulse").value = s.pulseS;
  const pw = $("pairwin");
  if (![...pw.options].some((o) => o.value === String(s.pairWindowS))) {
    pw.add(new Option(`only in the first ${s.pairWindowS} s after power-on`, String(s.pairWindowS)));
  }
  pw.value = String(s.pairWindowS);
  $("led-type").value = String(s.ledType);
  setPin($("led-gpio"), s.ledGpio);
  $("led-pol").value = s.ledActiveLow ? "1" : "0";
  setPin($("button-gpio"), s.buttonGpio);
  const r = document.querySelector(`input[name="trigger"][value="${s.triggerMode}"]`);
  if (r) r.checked = true;
  setPin($("trigger-gpio"), s.triggerGpio);
  $("trigger-pol").value = s.triggerActiveHigh ? "1" : "0";
  // PULL.auto (older settings) shown as the resistor it resolves to.
  const pull = s.triggerPull === PULL.auto || s.triggerPull === undefined
    ? (s.triggerActiveHigh ? PULL.down : PULL.up) : s.triggerPull;
  $("trigger-pull").value = String(pull);
  $("debounce").value = s.debounceMs || DEFAULTS.debounceMs;
  $("hold").value = s.holdS || 0;
  setEdition(s.triggerMode === TRIGGER.always ? "power" : "pin", true);
}

/** Validate and update dependent fields. Returns an error string or "". */
function refreshForm() {
  const s = readForm();
  const nameBytes = encodeName($("name").value.trim(), 1000).length;
  $("name-bytes").textContent = Math.min(nameBytes, 99);
  const nameHint = $("name-hint");
  nameHint.className = "";
  if (nameBytes > 29) {
    nameHint.textContent = "Too long: it will be cut to 29 bytes.";
    nameHint.className = "hint err";
  } else if (state.boardSettings && s.name !== state.boardSettings.name) {
    nameHint.textContent = `Renamed from "${state.boardSettings.name}": you'll need to re-pair on the iPhone.`;
  } else {
    nameHint.textContent = 'Pick something you\'ll recognize, e.g. "Scooter" or "Desk".';
  }

  const needsPin = s.triggerMode !== TRIGGER.always;
  $("trigger-pin-row").hidden = !needsPin;
  $("pulse-row").hidden = s.triggerMode !== TRIGGER.pulse;
  $("led-gpio").disabled = s.ledType === LED.none;
  $("led-pol").disabled = s.ledType !== LED.plain;

  $("hold-row").hidden = s.triggerMode !== TRIGGER.switch;
  const hint = $("trigger-hint");
  let err = "";
  if (needsPin && s.triggerGpio < 0) err = "Choose the input pin for the switch / button.";
  else if (needsPin && s.triggerGpio === s.ledGpio && s.ledType !== LED.none) err = "The input pin can't also be the LED pin.";
  else if (s.ledType !== LED.none && s.ledGpio >= 0 && s.ledGpio === s.buttonGpio) err = "The LED pin can't also be the reset button.";
  const chip = CHIPS[state.chip || "ESP32"];
  const advice = needsPin ? pinAdvice(s, chip) : { wiring: "", warn: "" };
  $("wiring").innerHTML = advice.wiring;
  if (err) {
    hint.textContent = err;
    hint.className = "hint err";
  } else {
    let w = advice.warn;
    if (needsPin && s.triggerGpio === s.buttonGpio && s.triggerGpio >= 0) {
      w += (w ? " " : "") + "Same pin as the reset button: holding it 5 s will also forget your phones.";
    }
    hint.textContent = w;
    hint.className = "hint warn";
  }
  updateButtons(err);
  return err;
}

/** Wiring explanation + non-blocking warnings for the chosen pin setup. */
function pinAdvice(s, chip) {
  const g = s.triggerGpio;
  if (g < 0) return { wiring: "", warn: "" };
  const active = s.triggerActiveHigh ? "HIGH" : "LOW";
  const activeTo = s.triggerActiveHigh ? "3.3 V" : "GND";
  const restTo = s.triggerActiveHigh ? "GND" : "3.3 V";
  const hasPull = !chip.noPull.includes(g);
  const warns = [];
  let wiring;

  if (s.triggerPull === PULL.none) {
    wiring = `<b>No internal resistor.</b> Your circuit must drive GPIO ${g}: a sensor/relay output that is ${active} when active, ` +
      `or a switch to ${activeTo} plus an external 10 kΩ resistor from GPIO ${g} to ${restTo}.`;
    warns.push("A pin left floating triggers at random.");
  } else {
    const pullTo = s.triggerPull === PULL.up ? "3.3 V" : "GND";
    const rest = s.triggerPull === PULL.up ? "HIGH" : "LOW";
    const matches = rest !== active;
    wiring = !hasPull
      ? `Wire your switch/button between <b>GPIO ${g}</b> and <b>${activeTo}</b>, and add an external 10 kΩ resistor from GPIO ${g} to ${restTo}.`
      : matches
      ? `Wire your switch/button between <b>GPIO ${g}</b> and <b>${activeTo}</b>. ` +
        `The internal ${s.triggerPull === PULL.up ? "pull-up" : "pull-down"} keeps it ${rest} (idle) when the contact is open.`
      : `The internal resistor pulls GPIO ${g} to ${pullTo}, so the pin rests <b>${rest}</b>, which is your <b>active</b> level: ` +
        `it is triggered whenever nothing drives the pin.`;
    if (!matches && hasPull) {
      warns.push(`Usually you want ${s.triggerActiveHigh ? "pull-down" : "pull-up"} with an active-${active} input. Keep this only if your circuit actively drives the pin.`);
    }
    if (!hasPull) {
      warns.push(`GPIO ${g} has no internal resistors: this setting has no effect. Choose "None" and add an external 10 kΩ to ${restTo}.`);
    }
  }
  if (s.triggerMode === TRIGGER.pulse && s.pulseS < 3) {
    warns.push("Under ~3 s the iPhone may not finish connecting, so the automation may not run.");
  }
  if (s.debounceMs > 1000) warns.push("Long debounce: presses shorter than that are ignored.");
  return { wiring, warn: warns.join(" ") };
}

function updateButtons(err = refreshFormErrorOnly()) {
  const connected = !!state.loader;
  const ready = connected && !state.busy && !err && buildFor(state.chip);
  $("btn-install").disabled = !ready;
  $("btn-settings-only").disabled = !(connected && !state.busy && !err && state.hasOurFirmware);
}
function refreshFormErrorOnly() {
  return $("trigger-hint").classList.contains("err") ? "x" : "";
}

function renderFwInfo() {
  const el = $("fw-info");
  if (!state.chip || !state.loader) { el.innerHTML = ""; return; }
  const c = CHIPS[state.chip];
  const build = buildFor(state.chip);
  const transportLabel = c.transport === "classic"
    ? 'Classic Bluetooth HID <span class="tag ok">tested on iPhone</span>'
    : 'Bluetooth LE HID <span class="tag">beta: please report if it works</span>';
  const ed = state.edition === "pin" ? "📍 Pin edition" : "⚡ Power-on edition";
  el.innerHTML = build
    ? `Firmware <b>v${state.manifest.version}</b> · <b>${ed}</b> for <b>${c.label}</b> · ${transportLabel}` +
      (state.hasOurFirmware ? "<br>Your board already runs BT Trigger: you can update it or just save the settings." : "")
    : `<span class="status err">No firmware file found for ${c.label}.</span>`;
  updateButtons();
}

function setStepsEnabled(on) {
  $("step-settings").setAttribute("aria-disabled", on ? "false" : "true");
  $("step-flash").setAttribute("aria-disabled", on ? "false" : "true");
}

/* ------------------------------------------------------------- connect */
function parsePartitionLabels(bytes) {
  const labels = [];
  for (let off = 0; off + 32 <= bytes.length; off += 32) {
    if (bytes[off] !== 0xaa || bytes[off + 1] !== 0x50) break;
    let label = "";
    for (let i = 12; i < 28 && bytes[off + i]; i++) label += String.fromCharCode(bytes[off + i]);
    labels.push(label);
  }
  return labels;
}

async function connect() {
  if (state.busy) return;
  state.busy = true;
  $("done").hidden = true;
  setStatus("Waiting for you to pick the port…");
  try {
    state.port = await navigator.serial.requestPort();
  } catch {
    setStatus("No port selected.");
    state.busy = false;
    return;
  }
  try {
    setStatus("Connecting… (if it hangs: hold BOOT, tap RESET, release BOOT)");
    state.transport = new Transport(state.port, false);
    state.loader = new ESPLoader({
      transport: state.transport,
      baudrate: FLASH_BAUD,
      romBaudrate: 115200,
      terminal,
      debugLogging: false,
    });
    await state.loader.main();
    const chipName = state.loader.chip.CHIP_NAME;
    log(`Detected chip: ${chipName}`);

    if (!CHIPS[chipName]) {
      const why = UNSUPPORTED[chipName] || "This chip isn't supported.";
      setStatus(`${chipName}: not compatible. ${why}`, "err");
      await disconnect(true);
      return;
    }
    state.chip = chipName;

    let flashSize = "";
    try { flashSize = (await state.loader.detectFlashSize()) || ""; } catch { /* optional */ }
    if (/^(256KB|512KB|1MB)$/.test(flashSize)) {
      setStatus(`${chipName} with ${flashSize} flash: at least 2 MB is required.`, "err");
      await disconnect(true);
      return;
    }

    // Does it already run this firmware? Read the partition table + settings.
    state.hasOurFirmware = false;
    state.boardSettings = null;
    try {
      const pt = await state.loader.readFlash(PARTITION_TABLE, 0x300);
      state.hasOurFirmware = parsePartitionLabels(pt).includes("btcfg");
      if (state.hasOurFirmware) {
        state.boardSettings = decodeConfig(await state.loader.readFlash(CONFIG_OFFSET, CFG_SIZE));
      }
    } catch (e) {
      log("Could not read current settings: " + e.message);
    }

    populateForChip(chipName);
    const first = boardsForChip(chipName)[0];
    $("board").value = first.id;
    applyBoardPreset(first.id);
    if (state.boardSettings) {
      writeForm(state.boardSettings);
      $("board-note").textContent = "Settings loaded from your board. Board preset only changes the pins.";
    }

    renderFwInfo();

    setStatus(`Connected: ${state.loader.chip.CHIP_NAME}${flashSize ? " · " + flashSize + " flash" : ""}`, "ok");
    $("btn-connect").hidden = true;
    $("btn-disconnect").hidden = false;
    setStepsEnabled(true);
  } catch (e) {
    log("ERROR: " + (e?.message || e));
    setStatus("Couldn't talk to the board. See tips below, then retry.", "err");
    await disconnect(true);
  } finally {
    state.busy = false;
    refreshForm();
  }
}

async function disconnect(keepStatus = false) {
  try { await state.transport?.disconnect(); } catch { /* already closed */ }
  state.transport = null;
  state.loader = null;
  $("btn-connect").hidden = false;
  $("btn-disconnect").hidden = true;
  setStepsEnabled(false);
  if (!keepStatus) setStatus("Disconnected.");
  updateButtons();
}

/* --------------------------------------------------------------- flash */
function progress(pct, label) {
  $("progress").hidden = false;
  $("bar").style.width = `${pct}%`;
  $("progress-label").textContent = label;
}

async function install(settingsOnly) {
  if (refreshForm()) return;
  const settings = readForm();
  const forget = $("forget").checked;
  const build = buildFor(state.chip);
  state.busy = true;
  updateButtons("busy");
  $("done").hidden = true;

  try {
    const files = [];
    if (!settingsOnly) {
      progress(0, "Downloading firmware…");
      const image = await fetchFirmware(build);
      if (forget) {
        // Merged image from 0x0: its 0xFF gap over NVS also wipes the pairings.
        files.push({ data: image, address: 0 });
      } else {
        // Write bootloader+partition table and app, skip NVS: pairings survive.
        files.push({ data: image.slice(0, NVS_OFFSET), address: 0 });
        files.push({ data: image.slice(APP_OFFSET), address: APP_OFFSET });
      }
    } else if (forget) {
      files.push({ data: new Uint8Array(APP_OFFSET - NVS_OFFSET).fill(0xff), address: NVS_OFFSET });
    }
    files.push({ data: buildConfigSector(settings), address: CONFIG_OFFSET });

    const total = files.reduce((a, f) => a + f.data.length, 0);
    const done = new Array(files.length).fill(0);
    log(`Writing ${files.length} region(s), ${(total / 1024).toFixed(0)} KB…`);

    await state.loader.writeFlash({
      fileArray: files,
      flashMode: "keep",
      flashFreq: "keep",
      flashSize: "keep",
      eraseAll: false,
      compress: true,
      reportProgress: (i, written, size) => {
        done[i] = (written / size) * files[i].data.length;
        const pct = Math.min(100, (done.reduce((a, b) => a + b, 0) / total) * 100);
        progress(pct, `Writing… ${pct.toFixed(0)}%`);
      },
    });

    progress(100, "Restarting the board…");
    await state.loader.after("hard_reset");
    await disconnect(true);

    progress(100, "Installed ✓");
    $("done-name").textContent = settings.name;
    const renamed = !!state.boardSettings && state.boardSettings.name !== settings.name;
    const rn = $("done-rename");
    rn.hidden = !(renamed || forget);
    rn.innerHTML = forget
      ? "The board forgot all phones: on the iPhone, open the old entry → <b>Forget This Device</b> → pair again."
      : "You changed the name: on the iPhone, open the old entry → <b>Forget This Device</b> → pair again.";
    $("done-pin").hidden = settings.triggerMode === TRIGGER.always;
    $("done").hidden = false;
    $("done").scrollIntoView({ behavior: "smooth", block: "center" });
    setStatus("Installed. The board restarted and disconnected from this page.", "ok");
    state.boardSettings = null;
  } catch (e) {
    log("ERROR: " + (e?.message || e));
    progress(0, "Failed: " + (e?.message || e));
    $("log-box").open = true;
  } finally {
    state.busy = false;
    updateButtons();
  }
}

/* ------------------------------------------------------------- monitor */
let monitorReader = null;
async function monitor() {
  const btn = $("btn-monitor");
  if (monitorReader) {
    try { await monitorReader.cancel(); } catch { /* ignore */ }
    return;
  }
  try {
    const port = state.port || (await navigator.serial.requestPort());
    state.port = port;
    await port.open({ baudRate: 115200 });
    $("log-box").open = true;
    log("\n--- device log (115200 baud). Press the board's RESET to see the boot. ---");
    btn.textContent = "Stop device log";
    const decoder = new TextDecoderStream();
    const closed = port.readable.pipeTo(decoder.writable).catch(() => {});
    monitorReader = decoder.readable.getReader();
    for (;;) {
      const { value, done } = await monitorReader.read();
      if (done) break;
      terminal.write(value.replace(/\x1b\[[0-9;]*m/g, ""));
    }
    monitorReader.releaseLock();
    await closed;
    await port.close();
  } catch (e) {
    log("Monitor: " + (e?.message || e) + " (unplug/replug the board if it uses native USB, then click again)");
    state.port = null;
  } finally {
    monitorReader = null;
    btn.textContent = "Show live device log";
  }
}

/* ---------------------------------------------------------------- init */
function init() {
  const supported = "serial" in navigator && window.isSecureContext;
  if (!supported) {
    $("browser-warning").hidden = false;
    $("btn-connect").disabled = true;
  }
  // Show the form with ESP32 pins until a board is connected (preview).
  populateForChip("ESP32");
  $("board").value = "esp32-devkit";
  applyBoardPreset("esp32-devkit");

  // Support popup: opens only on click, closes with Esc, × or a click outside.
  const dlg = $("support-dialog");
  $("btn-support").addEventListener("click", (e) => {
    if (typeof dlg.showModal !== "function") return;   // old browser: plain #support anchor
    e.preventDefault();
    dlg.showModal();
  });
  dlg.addEventListener("click", (e) => { if (e.target === dlg) dlg.close(); });

  $("btn-connect").addEventListener("click", connect);
  $("btn-disconnect").addEventListener("click", () => disconnect());
  $("btn-install").addEventListener("click", () => install(false));
  $("btn-settings-only").addEventListener("click", () => install(true));
  $("btn-monitor").addEventListener("click", monitor);
  $("board").addEventListener("change", (e) => applyBoardPreset(e.target.value));
  // Changing polarity picks the matching resistor (user can still override).
  $("trigger-pol").addEventListener("change", () => {
    if ($("trigger-pull").value !== String(PULL.none)) {
      $("trigger-pull").value = $("trigger-pol").value === "1" ? String(PULL.down) : String(PULL.up);
    }
    refreshForm();
  });
  document.querySelectorAll('input[name="edition"]').forEach((r) =>
    r.addEventListener("change", (e) => setEdition(e.target.value)));
  $("settings").addEventListener("input", refreshForm);
  $("settings").addEventListener("change", refreshForm);
  $("forget").addEventListener("change", () => {
    $("forget-hint").textContent = $("forget").checked
      ? "The board will forget every iPhone. On each iPhone: tap the device → Forget This Device → pair again."
      : "Leave it off to keep your iPhone paired across updates.";
  });
  setEdition(new URLSearchParams(location.search).get("edition") === "pin" ? "pin" : "power");
  loadManifest().then(() => updateButtons());
}

init();

// Exposed for automated tests only.
window.__bttTest = { readForm, writeForm, refreshForm, populateForChip, applyBoardPreset, setEdition, buildFor, state };
