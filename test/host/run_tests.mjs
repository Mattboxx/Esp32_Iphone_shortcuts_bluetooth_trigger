// End-to-end check of the settings contract between the web flasher and
// the firmware:  JS encodeConfig()  ->  bytes  ->  C btt_config_load().
// Run:  node test/host/run_tests.mjs   (needs a C compiler: cc)
import { execFileSync } from "node:child_process";
import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import assert from "node:assert/strict";
import { encodeConfig, decodeConfig, buildConfigSector, crc32, DEFAULTS, TRIGGER, LED, PULL } from "../../docs/js/config.js";

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, "..", "..");
const tmp = mkdtempSync(join(tmpdir(), "btt-"));
function compile(name, extra = []) {
  const out = join(tmp, name);
  try {
    execFileSync("cc", ["-std=c11", "-Wall", "-Wextra", "-Werror", ...extra,
      "-I", join(here, "stubs"), "-I", join(root, "src"),
      join(here, "test_config.c"), join(root, "src", "config.c"), "-o", out],
      { stdio: ["ignore", "pipe", "pipe"] });
  } catch (e) {
    console.error("C compile failed:\n" + (e.stderr?.toString() || e.message));
    process.exit(1);
  }
  return out;
}
const exe = compile("test_config");

function firmwareParse(bytes) {
  const f = join(tmp, "blob.bin");
  writeFileSync(f, bytes);
  return JSON.parse(execFileSync(exe, [f], { stdio: ["ignore", "pipe", "ignore"] }).toString());
}

let n = 0;
function test(name, fn) { fn(); n++; console.log("ok -", name); }

const full = {
  ...DEFAULTS, name: "Scooter Ninebot", reconnectS: 12, pulseS: 30, pairWindowS: 120,
  ledGpio: 27, ledType: LED.ws2812, ledActiveLow: false, ledBrightness: 80,
  buttonGpio: 39, triggerMode: TRIGGER.pulse, triggerGpio: 32, triggerActiveHigh: true,
  triggerPull: PULL.none, debounceMs: 250, holdS: 45, txPowerDbm: -12,
};

test("firmware reads every field written by the web flasher", () => {
  const fw = firmwareParse(encodeConfig(full));
  assert.equal(fw.fromFlash, true);
  for (const k of Object.keys(full)) assert.deepEqual(fw[k], full[k], k);
});

test("JS decode is the exact inverse of encode", () => {
  assert.deepEqual(decodeConfig(encodeConfig(full)), full);
});

test("blank flash (0xFF) -> firmware defaults", () => {
  const fw = firmwareParse(new Uint8Array(64).fill(0xff));
  assert.equal(fw.fromFlash, false);
  assert.equal(fw.name, "ESP32-BT-Trigger");
  assert.equal(fw.triggerMode, 0);
  assert.equal(decodeConfig(new Uint8Array(64).fill(0xff)), null);
});

test("pin trigger written by the web flasher overrides the power-on default", () => {
  const fw = firmwareParse(encodeConfig({ ...full, triggerMode: TRIGGER.switch, triggerGpio: 4 }));
  assert.equal(fw.triggerMode, 1);
  assert.equal(fw.triggerGpio, 4);
});

test("every resistor option survives the round trip", () => {
  for (const pull of Object.values(PULL)) {
    assert.equal(firmwareParse(encodeConfig({ ...full, triggerPull: pull })).triggerPull, pull);
  }
});

test("early v2 settings (bytes 54..59 = 0) -> auto resistor, 50 ms debounce, no hold", () => {
  const b = encodeConfig(full);
  b.fill(0, 54, 60);
  new DataView(b.buffer).setUint32(60, crc32(b.subarray(0, 60)), true);
  const fw = firmwareParse(b);
  assert.equal(fw.fromFlash, true);
  assert.equal(fw.triggerPull, PULL.auto);
  assert.equal(fw.debounceMs, 50);
  assert.equal(fw.holdS, 0);
});

test("out-of-range debounce / hold are clamped by the firmware", () => {
  const b = encodeConfig(full);
  const dv = new DataView(b.buffer);
  dv.setUint16(56, 1, true);
  dv.setUint16(58, 60000, true);
  dv.setUint32(60, crc32(b.subarray(0, 60)), true);
  const fw = firmwareParse(b);
  assert.equal(fw.debounceMs, 10);
  assert.equal(fw.holdS, 3600);
});

test("TX power: default, minimum, maximum, 0 dBm and clamping", () => {
  assert.equal(firmwareParse(encodeConfig({ ...full, txPowerDbm: null })).txPowerDbm, null);
  for (const dbm of [-24, -12, 0, 9, 21]) {
    assert.equal(firmwareParse(encodeConfig({ ...full, txPowerDbm: dbm })).txPowerDbm, dbm, `${dbm} dBm`);
  }
  assert.equal(firmwareParse(encodeConfig({ ...full, txPowerDbm: 40 })).txPowerDbm, 21);
});

test("TX power: firmware picks the nearest level the ESP32 supports", () => {
  const out = execFileSync(exe, ["--nearest", "-24", "-13", "0", "2", "9", "12", "21"]).toString().trim();
  // dBm asked : ESP32 Classic level (-12..+9 in 3 dB steps)
  assert.equal(out, "-24:-12 -13:-12 0:0 2:3 9:9 12:9 21:9");
});

test("corrupted byte -> CRC rejects it, defaults used", () => {
  const b = encodeConfig(full);
  b[20] ^= 0x01;
  assert.equal(firmwareParse(b).fromFlash, false);
  assert.equal(decodeConfig(b), null);
});

test("long UTF-8 name is cut on a character boundary (29 bytes max)", () => {
  const name = "Trigger Monopattino è già qui ✓✓✓";
  const fw = firmwareParse(encodeConfig({ ...full, name }));
  assert.ok(Buffer.byteLength(fw.name) <= 29);
  assert.ok(name.startsWith(fw.name));
  assert.equal(decodeConfig(encodeConfig({ ...full, name })).name, fw.name);
});

test("invalid pins are dropped by the firmware (flash pin, input-only LED)", () => {
  const fw = firmwareParse(encodeConfig({ ...full, ledType: LED.plain, ledGpio: 35, triggerGpio: 6, triggerMode: TRIGGER.switch }));
  assert.equal(fw.ledGpio, -1, "GPIO35 is input-only, cannot drive a LED");
  assert.equal(fw.ledType, 0);
  assert.equal(fw.triggerGpio, -1);
  assert.equal(fw.triggerMode, 0, "no input pin -> falls back to always");
});

test("every SPI-flash pin (GPIO6-11) is refused", () => {
  for (let g = 6; g <= 11; g++) {
    const fw = firmwareParse(encodeConfig({ ...full, buttonGpio: g, triggerGpio: g, ledGpio: g, ledType: LED.plain }));
    assert.equal(fw.buttonGpio, -1, `button ${g}`);
    assert.equal(fw.triggerGpio, -1, `trigger ${g}`);
    assert.equal(fw.ledGpio, -1, `led ${g}`);
  }
});

test("sector is 4 KB, record first, 0xFF padding", () => {
  const s = buildConfigSector(full);
  assert.equal(s.length, 4096);
  assert.deepEqual([...s.subarray(0, 4)], [0x42, 0x54, 0x54, 0x47]);
  assert.ok(s.subarray(64).every((x) => x === 0xff));
});

console.log(`\n${n} tests passed`);
